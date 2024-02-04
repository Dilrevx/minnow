#include "tcp_minnow_socket.hh"

#include "exception.hh"
#include "network_interface.hh"
#include "parser.hh"
#include "tun.hh"

#include <cstddef>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>

using namespace std;

static constexpr size_t TCP_TICK_MS = 10;

static inline uint64_t timestamp_ms()
{
  static_assert( std::is_same<std::chrono::steady_clock::duration, std::chrono::nanoseconds>::value );

  return std::chrono::steady_clock::now().time_since_epoch().count() / 1000000;
}

//! while condition, wait for next eventloop event to send TCPSegment(contains a send and a recv msg)
//! \param[in] condition is a function returning true if loop should continue
template<typename AdaptT>
void TCPMinnowSocket<AdaptT>::_tcp_loop( const function<bool()>& condition )
{
  auto base_time = timestamp_ms();
  while ( condition() ) {
    auto ret = _eventloop.wait_next_event( TCP_TICK_MS );
    if ( ret == EventLoop::Result::Exit or _abort ) {
      break;
    }

    if ( not _tcp.has_value() ) {
      throw runtime_error( "_tcp_loop entered before TCPPeer initialized" );
    }

    if ( _tcp.value().active() ) {
      const auto next_time = timestamp_ms();
      _tcp.value().tick( next_time - base_time );
      collect_segments();
      _datagram_adapter.tick( next_time - base_time );
      base_time = next_time;
    }
  }
}

//! \param[in] data_socket_pair is a pair of connected AF_UNIX SOCK_STREAM sockets
//! \param[in] datagram_interface is the interface for reading and writing datagrams
template<typename AdaptT>
TCPMinnowSocket<AdaptT>::TCPMinnowSocket( pair<FileDescriptor, FileDescriptor> data_socket_pair,
                                          AdaptT&& datagram_interface )
  : LocalStreamSocket( move( data_socket_pair.first ) )
  , _thread_data( move( data_socket_pair.second ) )
  , _datagram_adapter( move( datagram_interface ) )
{
  _thread_data.set_blocking( false );
  set_blocking( false );
}

template<typename AdaptT>
void TCPMinnowSocket<AdaptT>::_initialize_TCP( const TCPConfig& config )
{
  _tcp.emplace( config );

  // Set up the event loop

  // There are four possible events to handle:
  //
  // 1) Incoming datagram received (needs to be given to
  //    TCPConnection::segment_received method)
  //
  // 2) Outbound bytes received from local application via a write()
  //    call (needs to be read from the local stream socket and
  //    given to TCPConnection::data_written method)
  //
  // 3) Incoming bytes reassembled by the TCPConnection
  //    (needs to be read from the inbound_stream and written
  //    to the local stream socket back to the application)
  //
  // 4) Outbound segment generated by TCP (needs to be
  //    given to underlying datagram socket)

  // rule 1: read from filtered packet stream and dump into TCPConnection
  _eventloop.add_rule(
    "receive TCP segment from the network",
    _datagram_adapter.fd(),
    Direction::In,
    [&] {
      if ( auto seg = _datagram_adapter.read() ) {
        _tcp->receive( move( seg.value() ) );
        collect_segments();
      }

      // debugging output:
      if ( _thread_data.eof() and _tcp.value().sender().sequence_numbers_in_flight() == 0 and not _fully_acked ) {
        cerr << "DEBUG: Outbound stream to " << _datagram_adapter.config().destination.to_string()
             << " has been fully acknowledged.\n";
        _fully_acked = true;
      }
    },
    [&] { return _tcp->active(); } );

  // rule 2: read from pipe into outbound buffer
  _eventloop.add_rule(
    "push bytes to TCPPeer",
    _thread_data,
    Direction::In,
    [&] {
      string data;
      data.resize( _tcp->outbound_writer().available_capacity() );
      _thread_data.read( data );
      _tcp->outbound_writer().push( move( data ) );

      if ( _thread_data.eof() ) {
        _tcp->outbound_writer().close();
        _outbound_shutdown = true;

        // debugging output:
        cerr << "DEBUG: Outbound stream to " << _datagram_adapter.config().destination.to_string() << " finished ("
             << _tcp.value().sender().sequence_numbers_in_flight() << " seqno"
             << ( _tcp.value().sender().sequence_numbers_in_flight() == 1 ? "" : "s" ) << " still in flight).\n";
      }

      _tcp->push();
      collect_segments();
    },
    [&] {
      return ( _tcp->active() ) and ( not _outbound_shutdown )
             and ( _tcp->outbound_writer().available_capacity() > 0 );
    },
    [&] {
      _tcp->outbound_writer().close();
      _outbound_shutdown = true;
    } );

  // rule 3: read from inbound buffer into pipe
  _eventloop.add_rule(
    "read bytes from inbound stream",
    _thread_data,
    Direction::Out,
    [&] {
      Reader& inbound = _tcp->inbound_reader();
      // Write from the inbound_stream into
      // the pipe, handling the possibility of a partial
      // write (i.e., only pop what was actually written).
      if ( inbound.bytes_buffered() ) {
        const std::string_view buffer = inbound.peek();
        const auto bytes_written = _thread_data.write( buffer );
        inbound.pop( bytes_written );
      }

      if ( inbound.is_finished() or inbound.has_error() ) {
        _thread_data.shutdown( SHUT_WR );
        _inbound_shutdown = true;

        // debugging output:
        cerr << "DEBUG: Inbound stream from " << _datagram_adapter.config().destination.to_string() << " finished "
             << ( inbound.has_error() ? "with an error/reset.\n" : "cleanly.\n" );
      }
    },
    [&] {
      return _tcp->inbound_reader().bytes_buffered()
             or ( ( _tcp->inbound_reader().is_finished() or _tcp->inbound_reader().has_error() )
                  and not _inbound_shutdown );
    } );

  // rule 4: read outbound segments from TCPConnection and send as datagrams
  _eventloop.add_rule(
    "send TCP segment",
    _datagram_adapter.fd(),
    Direction::Out,
    [&] {
      while ( not outgoing_segments_.empty() ) {
        _datagram_adapter.write( outgoing_segments_.front() );
        outgoing_segments_.pop();
      }
    },
    [&] { return not outgoing_segments_.empty(); } );
}

//! \brief Call [socketpair](\ref man2::socketpair) and return connected Unix-domain sockets of specified type
//! \param[in] type is the type of AF_UNIX sockets to create (e.g., SOCK_SEQPACKET)
//! \returns a std::pair of connected sockets
static inline pair<FileDescriptor, FileDescriptor> socket_pair_helper( const int type )
{
  array<int, 2> fds {};
  CheckSystemCall( "socketpair", ::socketpair( AF_UNIX, type, 0, fds.data() ) );
  return { FileDescriptor( fds[0] ), FileDescriptor( fds[1] ) };
}

//! \param[in] datagram_interface is the underlying interface (e.g. to UDP, IP, or Ethernet)
template<typename AdaptT>
TCPMinnowSocket<AdaptT>::TCPMinnowSocket( AdaptT&& datagram_interface )
  : TCPMinnowSocket( socket_pair_helper( SOCK_STREAM ), move( datagram_interface ) )
{}

template<typename AdaptT>
TCPMinnowSocket<AdaptT>::~TCPMinnowSocket()
{
  try {
    if ( _tcp_thread.joinable() ) {
      cerr << "Warning: unclean shutdown of TCPMinnowSocket\n";
      // force the other side to exit
      _abort.store( true );
      _tcp_thread.join();
    }
  } catch ( const exception& e ) {
    cerr << "Exception destructing TCPMinnowSocket: " << e.what() << endl;
  }
}

template<typename AdaptT>
void TCPMinnowSocket<AdaptT>::wait_until_closed()
{
  shutdown( SHUT_RDWR );
  if ( _tcp_thread.joinable() ) {
    cerr << "DEBUG: Waiting for clean shutdown... ";
    _tcp_thread.join();
    cerr << "done.\n";
  }
}

//! \param[in] c_tcp is the TCPConfig for the TCPConnection
//! \param[in] c_ad is the FdAdapterConfig for the FdAdapter
template<typename AdaptT>
void TCPMinnowSocket<AdaptT>::connect( const TCPConfig& c_tcp, const FdAdapterConfig& c_ad )
{
  if ( _tcp ) {
    throw runtime_error( "connect() with TCPConnection already initialized" );
  }

  _initialize_TCP( c_tcp );

  _datagram_adapter.config_mut() = c_ad;

  cerr << "DEBUG: Connecting to " << c_ad.destination.to_string() << "...\n";

  if ( not _tcp.has_value() ) {
    throw runtime_error( "TCPPeer not successfully initialized" );
  }

  _tcp->push();
  collect_segments();

  if ( _tcp->sender().sequence_numbers_in_flight() != 1 ) {
    throw runtime_error( "After TCPConnection::connect(), expected sequence_numbers_in_flight() == 1" );
  }

  _tcp_loop( [&] { return _tcp->sender().sequence_numbers_in_flight() == 1; } );
  if ( not _tcp->inbound_reader().has_error() ) {
    cerr << "Successfully connected to " << c_ad.destination.to_string() << ".\n";
  } else {
    cerr << "Error on connecting to " << c_ad.destination.to_string() << ".\n";
  }

  _tcp_thread = thread( &TCPMinnowSocket::_tcp_main, this );
}

//! \param[in] c_tcp is the TCPConfig for the TCPConnection
//! \param[in] c_ad is the FdAdapterConfig for the FdAdapter
template<typename AdaptT>
void TCPMinnowSocket<AdaptT>::listen_and_accept( const TCPConfig& c_tcp, const FdAdapterConfig& c_ad )
{
  if ( _tcp ) {
    throw runtime_error( "listen_and_accept() with TCPConnection already initialized" );
  }

  _initialize_TCP( c_tcp );

  _datagram_adapter.config_mut() = c_ad;
  _datagram_adapter.set_listening( true );

  cerr << "DEBUG: Listening for incoming connection...\n";
  _tcp_loop( [&] { return ( not _tcp->has_ackno() ) or ( _tcp->sender().sequence_numbers_in_flight() ); } );
  cerr << "New connection from " << _datagram_adapter.config().destination.to_string() << ".\n";

  _tcp_thread = thread( &TCPMinnowSocket::_tcp_main, this );
}

template<typename AdaptT>
void TCPMinnowSocket<AdaptT>::_tcp_main()
{
  try {
    if ( not _tcp.has_value() ) {
      throw runtime_error( "no TCP" );
    }
    _tcp_loop( [] { return true; } );
    shutdown( SHUT_RDWR );
    if ( not _tcp.value().active() ) {
      cerr << "DEBUG: TCP connection finished "
           << ( _tcp->inbound_reader().has_error() ? "uncleanly.\n" : "cleanly.\n" );
    }
    _tcp.reset();
  } catch ( const exception& e ) {
    cerr << "Exception in TCPConnection runner thread: " << e.what() << "\n";
    throw e;
  }
}

template<typename AdaptT>
void TCPMinnowSocket<AdaptT>::collect_segments()
{
  if ( not _tcp.has_value() ) {
    return;
  }

  while ( auto seg = _tcp->maybe_send() ) {
    outgoing_segments_.push( move( seg.value() ) );
  }
}

//! Specialization of TCPMinnowSocket for TCPOverIPv4OverTunFdAdapter
template class TCPMinnowSocket<TCPOverIPv4OverTunFdAdapter>;

//! Specialization of TCPMinnowSocket for TCPOverIPv4OverEthernetAdapter
template class TCPMinnowSocket<TCPOverIPv4OverEthernetAdapter>;

//! Specialization of TCPMinnowSocket for LossyTCPOverIPv4OverTunFdAdapter
template class TCPMinnowSocket<LossyTCPOverIPv4OverTunFdAdapter>;

CS144TCPSocket::CS144TCPSocket() : TCPOverIPv4MinnowSocket( TCPOverIPv4OverTunFdAdapter( TunFD( "tun144" ) ) ) {}

void CS144TCPSocket::connect( const Address& address )
{
  TCPConfig tcp_config;
  tcp_config.rt_timeout = 100;

  FdAdapterConfig multiplexer_config;
  multiplexer_config.source = { "169.254.144.9", to_string( uint16_t( random_device()() ) ) };
  multiplexer_config.destination = address;

  TCPOverIPv4MinnowSocket::connect( tcp_config, multiplexer_config );
}

static constexpr char const* const LOCAL_TAP_IP_ADDRESS = "169.254.10.9";
static constexpr char const* const LOCAL_TAP_NEXT_HOP_ADDRESS = "169.254.10.1";

EthernetAddress random_private_ethernet_address()
{
  EthernetAddress addr;
  for ( auto& byte : addr ) {
    byte = random_device()(); // use a random local Ethernet address
  }
  addr.at( 0 ) |= 0x02; // "10" in last two binary digits marks a private Ethernet address
  addr.at( 0 ) &= 0xfe;

  return addr;
}

FullStackSocket::FullStackSocket()
  : TCPOverIPv4OverEthernetMinnowSocket(
    TCPOverIPv4OverEthernetAdapter( TapFD( "tap10" ),
                                    random_private_ethernet_address(),
                                    Address( LOCAL_TAP_IP_ADDRESS, "0" ),
                                    Address( LOCAL_TAP_NEXT_HOP_ADDRESS, "0" ) ) )
{}

void FullStackSocket::connect( const Address& address )
{
  TCPConfig tcp_config;
  tcp_config.rt_timeout = 100;

  FdAdapterConfig multiplexer_config;
  multiplexer_config.source = { LOCAL_TAP_IP_ADDRESS, to_string( uint16_t( random_device()() ) ) };
  multiplexer_config.destination = address;

  TCPOverIPv4OverEthernetMinnowSocket::connect( tcp_config, multiplexer_config );
}
