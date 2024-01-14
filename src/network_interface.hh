#pragma once

#include "address.hh"
#include "arp_message.hh"
#include "ethernet_frame.hh"
#include "ipv4_datagram.hh"

#include <iostream>
#include <list>
#include <optional>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <utility>

// A "network interface" that connects IP (the internet layer, or network layer)
// with Ethernet (the network access layer, or link layer).

// This module is the lowest layer of a TCP/IP stack
// (connecting IP with the lower-layer network protocol,
// e.g. Ethernet). But the same module is also used repeatedly
// as part of a router: a router generally has many network
// interfaces, and the router's job is to route Internet datagrams
// between the different interfaces.

// The network interface translates datagrams (coming from the
// "customer," e.g. a TCP/IP stack or router) into Ethernet
// frames. To fill in the Ethernet destination address, it looks up
// the Ethernet address of the next IP hop of each datagram, making
// requests with the [Address Resolution Protocol](\ref rfc::rfc826).
// In the opposite direction, the network interface accepts Ethernet
// frames, checks if they are intended for it, and if so, processes
// the the payload depending on its type. If it's an IPv4 datagram,
// the network interface passes it up the stack. If it's an ARP
// request or reply, the network interface processes the frame
// and learns or replies as necessary.
class NetworkInterface
{
  enum IP2ETH_STATES
  {
    IP2ETH_NONE,
    IP2ETH_ARP_SENT,
    IP2ETH_VALID
  };
  class Timer
  {
    template<typename T>
    using q_type = std::priority_queue<T, std::vector<T>, std::greater<T>>;
    // trigger time, ip
    using event_pair_t = std::pair<uint64_t, uint32_t>;

    size_t time_elapse = 0;
    q_type<event_pair_t> ip2eth_events {};
    q_type<event_pair_t> arp_events {};

  public:
    std::unordered_map<uint32_t, IP2ETH_STATES>& ip2eth;
    std::unordered_set<uint32_t> arps {};

  public:
    enum EVENT_TYPES
    {
      TIMER_IP2ETH_REFRESH,
      TIMER_ARP_TIMEOUT
    };

  public:
    Timer( std::unordered_map<uint32_t, IP2ETH_STATES>& _ip2eth ) : ip2eth( _ip2eth ) {}

    Timer& elapse( const size_t ms )
    {
      time_elapse += ms;
      while ( !ip2eth_events.empty() && ip2eth_events.top().first <= time_elapse ) {
        auto [_, ip] = ip2eth_events.top();
        ip2eth[ip] = IP2ETH_NONE;
        ip2eth_events.pop();
      }
      while ( !arp_events.empty() && arp_events.top().first <= time_elapse ) {
        auto [_, ip] = arp_events.top();
        arps.emplace( ip );
        arp_events.pop();
      }
      if ( ip2eth_events.empty() && arp_events.empty() )
        time_elapse = 0;
      return *this;
    }
    template<EVENT_TYPES event_type>
    Timer& set_event( const size_t period, const uint32_t ip )
    {
      if constexpr ( event_type == TIMER_IP2ETH_REFRESH ) {
        ip2eth_events.push( { period + time_elapse, ip } );
      } else {
        arp_events.push( { period + time_elapse, ip } );
      }
      return *this;
    }
    template<EVENT_TYPES event_type>
    bool check_event( const uint32_t& ip )
    {
      static_assert( event_type == TIMER_ARP_TIMEOUT );
      return arps.contains( ip ) && ( arps.erase( ip ) );
    }
    Timer( Timer& ) = delete;
    Timer( Timer&& ) = delete;
  };

private:
  static constexpr uint64_t ARP_DEFAULT_TIMEOUT_MS = 5 * 1000;
  static constexpr uint64_t IP2ETH_MAPPING_TIMEOUT_MS = 30 * 1000;

  // Ethernet (known as hardware, network-access, or link-layer) address of the interface
  EthernetAddress ethernet_address_;

  // IP (known as Internet-layer or network-layer) address of the interface
  Address ip_address_;
  EthernetHeader ARP_REQUEST_HEADER;

private:
  std::unordered_map<uint32_t, IP2ETH_STATES> meta_ip2eth {};
  std::unordered_map<uint32_t, EthernetAddress> ip2eth {};
  std::deque<EthernetFrame> pendings {};
  std::unordered_map<uint32_t, EthernetFrame> waitings {};

  Timer timer { meta_ip2eth };
  void ARP_handler( const EthernetHeader&, const decltype( EthernetFrame::payload )& );

public:
  // Construct a network interface with given Ethernet (network-access-layer) and IP (internet-layer)
  // addresses
  NetworkInterface( const EthernetAddress& ethernet_address, const Address& ip_address );

  // Access queue of Ethernet frames awaiting transmission
  std::optional<EthernetFrame> maybe_send();

  // Sends an IPv4 datagram, encapsulated in an Ethernet frame (if it knows the Ethernet destination
  // address). Will need to use [ARP](\ref rfc::rfc826) to look up the Ethernet destination address
  // for the next hop.
  // ("Sending" is accomplished by making sure maybe_send() will release the frame when next called,
  // but please consider the frame sent as soon as it is generated.)
  void send_datagram( const InternetDatagram& dgram, const Address& next_hop );

  // Receives an Ethernet frame and responds appropriately.
  // If type is IPv4, returns the datagram.
  // If type is ARP request, learn a mapping from the "sender" fields, and send an ARP reply.
  // If type is ARP reply, learn a mapping from the "sender" fields.
  std::optional<InternetDatagram> recv_frame( const EthernetFrame& frame );

  // Called periodically when time elapses
  void tick( size_t ms_since_last_tick );
  NetworkInterface( const NetworkInterface& ) = delete;
  NetworkInterface( const NetworkInterface&& ) = delete;
};
