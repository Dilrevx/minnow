#include "router.hh"

#include <bitset>
#include <iostream>
#include <limits>

using namespace std;

// route_prefix: The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
// prefix_length: For this route to be applicable, how many high-order (most-significant) bits of
//    the route_prefix will need to match the corresponding bits of the datagram's destination address?
// next_hop: The IP address of the next hop. Will be empty if the network is directly attached to the router (in
//    which case, the next hop address should be the datagram's final destination).
// interface_num: The index of the interface to send the datagram out on.
void Router::add_route( const uint32_t route_prefix,
                        const uint8_t prefix_length,
                        const optional<Address> next_hop,
                        const size_t interface_num )
{
  cerr << "DEBUG: adding route " << Address::from_ipv4_numeric( route_prefix ).ip() << "/"
       << static_cast<int>( prefix_length ) << " => " << ( next_hop.has_value() ? next_hop->ip() : "(direct)" )
       << " on interface " << interface_num << "\n";

  route_table.emplace_back( make_tuple( route_prefix, prefix_length, next_hop, interface_num ) );
}

void Router::route()
{
  do {
    for ( auto& interface_ : interfaces_ ) {
      for ( auto opt = interface_.maybe_receive(); opt.has_value(); opt = interface_.maybe_receive() ) {
        auto& dgram = opt.value();
        auto& header = dgram.header;
        if ( header.ttl == 0 || --header.ttl == 0 )
          continue;
        header.compute_checksum();

        auto ip = header.dst;
        auto rtentry = match_rt_entry( ip );
        // no match route
        if ( not rtentry.has_value() )
          continue;

        auto [next_hop, if_n] = rtentry.value();
        auto& dstif = interface( if_n );
        dstif.send_datagram( dgram, next_hop );
      }
    }
  } while ( false );
}

// originally designed to return entry, now return
optional<pair<Address, size_t>> Router::match_rt_entry( const uint32_t ip )
{
  constexpr auto getMask = []( const uint32_t _ip, const uint8_t mask ) {
    uint64_t bits = numeric_limits<uint32_t>::max() * 1ULL << ( 32 - mask );
    return _ip & bits;
  };

  int maxLen = -1;
  const optional<Address>* p1 = nullptr;
  const size_t* p2 = nullptr;
  for ( const auto& [pref, mask, nhop, num] : route_table ) {
    if ( getMask( ip, mask ) != getMask( pref, mask ) )
      continue;
    if ( maxLen < mask ) {
      maxLen = mask;
      p1 = &nhop;
      p2 = &num;
    }
  }

  if ( !p1 )
    return nullopt;
  return make_pair( p1->value_or( Address::from_ipv4_numeric( ip ) ), *p2 );
}