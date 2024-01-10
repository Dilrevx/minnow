#include "reassembler.hh"
#include <algorithm>
using namespace std;

void Reassembler::insert( uint64_t first_index, string data, bool is_last_substring, Writer& output )
{
  auto idx_expect = output.bytes_pushed();
  if ( first_index <= idx_expect ) {
    int64_t bytes_to_write = first_index + data.size() - idx_expect;
    if ( bytes_to_write <= 0 )
      goto last;

    // bytes > 0
    data = first_index == idx_expect ? data : data.substr( idx_expect - first_index );
    output.push( move( data ) );
    idx_expect = output.bytes_pushed();

    while ( !meta_buffer.empty() && get<0>( meta_buffer.top() ) <= idx_expect ) {
      auto [l, r, istr] = meta_buffer.top();
      auto str_to_write = move( storage[istr] );
      meta_buffer.pop();
      if ( r <= idx_expect )
        continue;

      // something to write
      str_to_write = l == idx_expect ? str_to_write : str_to_write.substr( idx_expect - l );
      output.push( move( str_to_write ) );
      idx_expect = output.bytes_pushed();
    }
  } else {
    if ( first_index >= idx_expect + output.available_capacity() )
      goto last;

    data.resize( min( data.size(), output.available_capacity() + idx_expect - first_index ) );
    meta_buffer.push( { first_index, first_index + data.size(), storage.size() } );
    storage.emplace_back( move( data ) );
  }
last:
  lastOccured |= is_last_substring;
  if ( lastOccured && meta_buffer.empty() )
    output.close();
}
uint64_t Reassembler::bytes_pending() const
{
  // Your code here.
  auto ret = 0;
  for ( auto& s : storage )
    ret += s.size();
  return ret;
}
