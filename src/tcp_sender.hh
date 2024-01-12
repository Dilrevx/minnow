#pragma once

#include "byte_stream.hh"
#include "tcp_config.hh"
#include "tcp_receiver_message.hh"
#include "tcp_sender_message.hh"
#include <deque>

class TCPSender
{
  static constexpr auto MAX_PAYLOAD_SIZE = TCPConfig::MAX_PAYLOAD_SIZE;
  Wrap32 isn_;
  uint64_t initial_RTO_ms_;
  uint64_t RTO = 0;

  // bytes available
  uint64_t window_size = 1;
  std::deque<TCPSenderMessage> unacks = {};
  uint64_t s_seqno = 0;
  uint64_t s_seqack = 0;
  uint32_t inever_send = 0;

  uint32_t cnt_RT = 0;
  uint32_t sent_RT = 0;
  bool force_send = true;
  bool zero_window_handling = false;

  struct VanillaTimer
  {
    uint64_t ms_elapsed = 0;
    bool start = false;
    void reset() { ms_elapsed = start = 0; }
  };
  VanillaTimer timer = {};

public:
  /* Construct TCP sender with given default Retransmission Timeout and possible ISN */
  TCPSender( uint64_t initial_RTO_ms, std::optional<Wrap32> fixed_isn );

  /* Push bytes from the outbound stream */
  void push( Reader& outbound_stream );

  /* Send a TCPSenderMessage if needed (or empty optional otherwise) */
  std::optional<TCPSenderMessage> maybe_send();

  /* Generate an empty TCPSenderMessage */
  TCPSenderMessage send_empty_message() const;

  /* Receive an act on a TCPReceiverMessage from the peer's receiver */
  void receive( const TCPReceiverMessage& msg );

  /* Time has passed by the given # of milliseconds since the last time the tick() method was called. */
  void tick( uint64_t ms_since_last_tick );

  /* Accessors for use in testing */
  uint64_t sequence_numbers_in_flight() const;  // How many sequence numbers are outstanding?
  uint64_t consecutive_retransmissions() const; // How many consecutive *re*transmissions have happened?
};
