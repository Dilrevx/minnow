#include "tcp_receiver.hh"

using namespace std;

// The writer is the reassembler's writer.
void TCPReceiver::receive( TCPSenderMessage message, Reassembler& reassembler, Writer& inbound_stream )
{
  if ( message.SYN ) {
    connected = true;
    // their_absseq = my_absseq = 0;
    their_zero = message.seqno;
    their_seqno = their_zero;
    message.seqno = message.seqno + 1;
  }
  if ( !connected )
    return;
  reassembler.insert( message.seqno.unwrap( their_zero, inbound_stream.bytes_pushed() + 1 ) - 1,
                      message.payload,
                      message.FIN,
                      inbound_stream );

  their_seqno = their_zero + inbound_stream.bytes_pushed() + 1;

  if ( message.FIN )
    finished = true;
  if ( finished && reassembler.bytes_pending() == 0 )
    their_seqno = their_seqno + 1;
}

TCPReceiverMessage TCPReceiver::send( const Writer& inbound_stream ) const
{
  TCPReceiverMessage msg;
  msg.window_size = static_cast<uint16_t>( min( UINT16_MAX * 1UL, inbound_stream.available_capacity() ) );
  if ( connected )
    msg.ackno = their_seqno;
  return msg;
}
