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
  bool has_mac = ip2eth.find( next_hop.ipv4_numeric() ) != ip2eth.end();

  // Send ARP
  if ( !has_mac ) {
    ARPMessage&& payload = {
      .opcode = ARPMessage::OPCODE_REQUEST,
      .sender_ethernet_address = ethernet_address_,
      .sender_ip_address = ip_address_.ipv4_numeric(),
      .target_ethernet_address = {},
      .target_ip_address = {},
    };
    EthernetFrame&& frame = { .header = ARP_REQUEST_HEADER, .payload = serialize( payload ) };
    sends.push_back( frame );
  }

  EthernetHeader&& header = {
    .dst = ip2eth[next_hop.ipv4_numeric()],
    .src = ethernet_address_,
    .type = EthernetHeader::TYPE_IPv4,
  };
  EthernetFrame&& frame = {
    .header = header,
    .payload = serialize( dgram ),
  };

  auto& q = has_mac ? sends : pendings;
  q.emplace_back( frame );
}

// frame: the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame( const EthernetFrame& frame )
{
  const auto& header = frame.header;
  const auto& payload = frame.payload;

  if ( header.dst != ETHERNET_BROADCAST && header.dst != ethernet_address_ ) {
    throw runtime_error( "New Ethernet Address: " + header.to_string() );
  }

  switch ( header.type ) {
    case EthernetHeader::TYPE_ARP: {
      ARPMessage msg;
      if ( !parse( msg, payload ) )
        return nullopt;

      switch ( msg.opcode ) {
        case ARPMessage::OPCODE_REPLY:
          ip2eth[msg.sender_ip_address] = msg.sender_ethernet_address;
          return nullopt;
        case ARPMessage::OPCODE_REQUEST: {
          // TODO: map 30 s
          ip2eth[msg.sender_ip_address] = msg.sender_ethernet_address;

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
          sends.emplace_back( rply_frame );
          return nullopt;
        }
        default:
          throw runtime_error( "Not supported ARP opcode: " + msg.to_string() );
      }
      break;
    }
    case EthernetHeader::TYPE_IPv4: {
      InternetDatagram ret;
      if ( !parse( ret, payload ) )
        return nullopt;

      ip2eth[ret.header.src] = header.src;
      return ret;
    }
    default:
      throw runtime_error( "Ethernet header type not supported: " + header.to_string() );
  }
}

// ms_since_last_tick: the number of milliseconds since the last call to this method
void NetworkInterface::tick( const size_t ms_since_last_tick )
{
  timer.elapse( ms_since_last_tick ).elapse( 0 );
}

optional<EthernetFrame> NetworkInterface::maybe_send()
{
  if ( sends.empty() )
    return nullopt;
  const auto ret = sends.front();
  sends.pop_front();
  return ret;
}
