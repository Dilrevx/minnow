#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"

using namespace std;

// ethernet_address: Ethernet (what ARP calls "hardware") address of the interface
// ip_address: IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface( const EthernetAddress& ethernet_address, const Address& ip_address )
  : ethernet_address_( ethernet_address )
  , ip_address_( ip_address )
  , ARP_REQUEST_HEADER( { .dst = ETHERNET_BROADCAST, .src = ethernet_address, .type = EthernetHeader::TYPE_ARP } )
  , meta_ip2eth( make_shared<unordered_map<uint32_t, IP2ETH_STATES>>() )
{
  cerr << "DEBUG: Network interface has Ethernet address " << to_string( ethernet_address_ ) << " and IP address "
       << ip_address.ip() << "\n";
}

// dgram: the IPv4 datagram to be sent
// next_hop: the IP address of the interface to send it to (typically a router or default gateway, but
// may also be another host if directly connected to the same network as the destination)

// Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) by using the
// Address::ipv4_numeric() method.
void NetworkInterface::send_datagram( const InternetDatagram& dgram, const Address& next_hop )
{
  const auto ip = next_hop.ipv4_numeric();
  IP2ETH_STATES& meta = ( *meta_ip2eth )[ip];

  // Send ARP
  if ( meta == IP2ETH_NONE ) {
    ARPMessage&& payload = {
      .opcode = ARPMessage::OPCODE_REQUEST,
      .sender_ethernet_address = ethernet_address_,
      .sender_ip_address = ip_address_.ipv4_numeric(),
      .target_ethernet_address = {},
      .target_ip_address = ip,
    };
    EthernetFrame&& frame = {
      .header = ARP_REQUEST_HEADER,
      .payload = serialize( payload ),
    };
    pendings.push_back( frame );
    meta = IP2ETH_ARP_SENT;
    timer.set_event<Timer::TIMER_ARP_TIMEOUT>( ARP_DEFAULT_TIMEOUT_MS, ip );
  }

  EthernetHeader&& header = {
    .dst = ip2eth[ip],
    .src = ethernet_address_,
    .type = EthernetHeader::TYPE_IPv4,
  };
  EthernetFrame&& frame = {
    .header = header,
    .payload = serialize( dgram ),
  };

  if ( meta == IP2ETH_VALID ) {
    pendings.emplace_back( frame );
  } else {
    waitings[ip] = frame;
  }
}

// frame: the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame( const EthernetFrame& frame )
{
  const auto& header = frame.header;
  const auto& payload = frame.payload;

  if ( header.dst != ETHERNET_BROADCAST && header.dst != ethernet_address_ ) {
    if constexpr ( ETHERNET_DEBUG )
      throw runtime_error( "New Ethernet Address: " + header.to_string() );
    else
      return nullopt;
  }

  switch ( header.type ) {
    case EthernetHeader::TYPE_ARP:
      ARP_handler( header, payload );
      break;

    case EthernetHeader::TYPE_IPv4: {
      InternetDatagram ret;
      if ( !parse( ret, payload ) )
        break;

      ip2eth[ret.header.src] = header.src;
      timer.set_event<Timer::TIMER_IP2ETH_REFRESH>( IP2ETH_MAPPING_TIMEOUT_MS, ret.header.src );
      return ret;
    }
    default:
      if constexpr ( ETHERNET_DEBUG )
        throw runtime_error( "Ethernet header type not supported: " + header.to_string() );
  }
  return nullopt;
}

// ms_since_last_tick: the number of milliseconds since the last call to this method
void NetworkInterface::tick( const size_t ms_since_last_tick )
{
  timer.elapse( ms_since_last_tick );
}

optional<EthernetFrame> NetworkInterface::maybe_send()
{
  if ( pendings.empty() )
    return nullopt;
  const auto ret = pendings.front();
  pendings.pop_front();
  return ret;
}

void NetworkInterface::ARP_handler( [[maybe_unused]] const EthernetHeader& header,
                                    const decltype( EthernetFrame::payload )& payload )
{
  ARPMessage msg;
  if ( !parse( msg, payload ) )
    return;

  ( *meta_ip2eth )[msg.sender_ip_address] = IP2ETH_VALID;
  ip2eth[msg.sender_ip_address] = msg.sender_ethernet_address;
  timer.set_event<Timer::TIMER_IP2ETH_REFRESH>( IP2ETH_MAPPING_TIMEOUT_MS, msg.sender_ip_address );

  switch ( msg.opcode ) {
    case ARPMessage::OPCODE_REPLY:
      if ( waitings.count( msg.sender_ip_address ) ) {
        auto& f = waitings[msg.sender_ip_address];
        f.header.dst = msg.sender_ethernet_address;
        pendings.push_back( f );
      }
      break;
    case ARPMessage::OPCODE_REQUEST: {
      if ( msg.target_ip_address != ip_address_.ipv4_numeric() )
        break;
      EthernetHeader&& rply_header = {
        .dst = msg.sender_ethernet_address,
        .src = ethernet_address_,
        .type = EthernetHeader::TYPE_ARP,
      };
      ARPMessage&& rply_msg = {
        .opcode = ARPMessage::OPCODE_REPLY,
        .sender_ethernet_address = ethernet_address_,
        .sender_ip_address = ip_address_.ipv4_numeric(),
        .target_ethernet_address = msg.sender_ethernet_address,
        .target_ip_address = msg.sender_ip_address,
      };
      EthernetFrame&& rply_frame = {
        .header = rply_header,
        .payload = serialize( rply_msg ),
      };
      pendings.emplace_back( rply_frame );
      break;
    }
    default:
      if constexpr ( ETHERNET_DEBUG )
        throw runtime_error( "Not supported ARP opcode: " + msg.to_string() );
  }
}