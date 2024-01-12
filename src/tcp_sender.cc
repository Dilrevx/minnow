#include "tcp_sender.hh"
#include "tcp_config.hh"

#include <algorithm>
#include <random>

using namespace std;

/* TCPSender constructor (uses a random ISN if none given) */
TCPSender::TCPSender( uint64_t initial_RTO_ms, optional<Wrap32> fixed_isn )
  : isn_( fixed_isn.value_or( Wrap32 { random_device()() } ) ), initial_RTO_ms_( initial_RTO_ms )
{}

uint64_t TCPSender::sequence_numbers_in_flight() const
{
  // Your code here.
  return s_seqno - s_seqack;
}

uint64_t TCPSender::consecutive_retransmissions() const
{
  // Your code here.
  return cnt_RT;
}

optional<TCPSenderMessage> TCPSender::maybe_send()
{
  if ( sent_RT < cnt_RT ) {
    timer.start = true;
    sent_RT++;
    return unacks[0];
  }
  if ( inever_send < unacks.size() ) {
    timer.start = true;
    return unacks[inever_send++];
  }

  return nullopt;
}

void TCPSender::push( Reader& outbound_stream )
{
  // Loop to send as much as possible
  if ( outbound_stream.is_finished() )
    force_send = true;

  while ( ( outbound_stream.bytes_buffered() || force_send )
          && ( !window_size || s_seqno - s_seqack < window_size ) ) {
    TCPSenderMessage msg;
    force_send = false;

    msg.SYN = s_seqno == 0;
    msg.seqno = isn_ + s_seqno;

    auto max_payload = min( MAX_PAYLOAD_SIZE, window_size - ( s_seqno - s_seqack ) ) - msg.SYN;

    // special case for window = 0
    if ( window_size == 0 )
      max_payload = 1;
    auto len = min( max_payload, outbound_stream.bytes_buffered() );

    string_view next_bytes = outbound_stream.peek().substr( 0, len );
    static_cast<string&>( msg.payload ) += next_bytes;
    outbound_stream.pop( len );

    if ( outbound_stream.is_finished() ) {
      if ( msg.sequence_length() < max_payload ) {
        msg.FIN = 1;
      } else
        force_send = true;
    }

    // TODO: check if msg is clear after move.
    unacks.emplace_back( msg );
    s_seqno += msg.sequence_length();
  }
}

TCPSenderMessage TCPSender::send_empty_message() const
{
  // Your code here.
  return {
    { isn_ + s_seqno },
  };
}

void TCPSender::receive( const TCPReceiverMessage& msg )
{
  // Your code here.
  window_size = msg.window_size;

  if ( !msg.ackno.has_value() )
    return;
  const auto msg_seqno = msg.ackno.value().unwrap( isn_, s_seqack );

  while ( s_seqack < s_seqno ) {
    auto check_msg = unacks.front();
    auto check_seqno = check_msg.seqno.unwrap( isn_, s_seqack );

    if ( check_seqno + check_msg.sequence_length() <= msg_seqno ) {
      s_seqack += check_msg.sequence_length();
      inever_send--;
      unacks.pop_front();

      RTO = initial_RTO_ms_;
      cnt_RT = sent_RT = 0;
      timer.ms_elapsed = 0; // TODO: maybe half accept should clear this.
    } else
      break;
  }

  if ( s_seqack == s_seqno )
    timer.start = false;
}

void TCPSender::tick( const size_t ms_since_last_tick )
{
  // Your code here.
  if ( !RTO )
    RTO = initial_RTO_ms_;

  if ( timer.start ) {
    timer.ms_elapsed += ms_since_last_tick;
    if ( timer.ms_elapsed >= RTO ) {
      timer.start = false;
      timer.ms_elapsed = 0;
      cnt_RT++;
      RTO *= 2;
    }
  }
}
