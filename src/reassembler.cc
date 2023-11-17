#include "reassembler.hh"
#include <algorithm>
using namespace std;

void Reassembler::_Reassembler::insert( uint64_t first_index, string data, bool is_last_substring )
{
  auto idx_expect = writer.bytes_pushed();

  // If expectation meets, casdade write everything into data;
  // Otherwise store it into `buffer`
  if ( first_index <= idx_expect ) {
    int64_t bytes_to_write = first_index + data.size() - idx_expect;
    if ( bytes_to_write <= 0 )
      goto last;

    // cascade writing.
    writer.push( move( data.substr( idx_expect - first_index ) ) );
    idx_expect += bytes_to_write;

    while ( !buffer.empty() && get<0>( buffer.top() ) <= idx_expect ) {
      auto [l, r, str_idx] = buffer.top();
      buffer.pop();

      bytes_to_write = r - idx_expect;
      if ( bytes_to_write <= 0 )
        continue;

      string str_to_write = move( storage[str_idx] );
      writer.push( move( str_to_write.substr( idx_expect - l ) ) );

      idx_expect = r;
    }
  } else {
    // store into buffer
    auto idx_data = storage.size();
    buffer.push( { first_index, first_index + data.size(), idx_data } );
    storage.emplace_back( move( data ) );
  }

last:
  lastOccured |= is_last_substring;
  if ( lastOccured && buffer.empty() )
    writer.close();
}
void Reassembler::insert( uint64_t first_index, string data, bool is_last_substring, Writer& output )
{
  // Check that the reassembler for`output` is constructed
  if ( writer_map.find( &output ) == writer_map.end() ) {
    writer_map.emplace( &output, output );
  }

  _Reassembler& reassembler = writer_map.at( &output );

  reassembler.insert( first_index, data, is_last_substring );
}

uint64_t Reassembler::_Reassembler::bytes_pending() const
{
  uint64_t ret = 0;
  auto buffer_copy = buffer;

  uint64_t start = 0;
  const auto capacity = writer.available_capacity() + writer.bytes_pushed();
  while ( !buffer_copy.empty() && start < capacity ) {
    while ( get<1>( buffer_copy.top() ) <= start )
      buffer_copy.pop();
    auto [l, r, _] = buffer_copy.top();
    buffer_copy.pop();

    r = min( r, capacity );

    ret += r - max( l, start );
    start = r;
  }
  return min( ret, writer.available_capacity() );
}

uint64_t Reassembler::bytes_pending() const
{
  // Your code here.
  const _Reassembler& reassembler = ( *writer_map.begin() ).second;
  return reassembler.bytes_pending();
}
