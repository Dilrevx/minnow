#include <stdexcept>

#include "byte_stream.hh"

using namespace std;

ByteStream::ByteStream( uint64_t capacity ) : capacity_( capacity ), buffers(), read_index() {}

void Writer::push( string data )
{
  // Your code here.
  uint64_t accept_size = min( capacity_ - cur_size, data.size() );

  if ( !accept_size )
    return;
  if ( accept_size == data.size() )
    buffers.push_back( data );
  else
    buffers.push_back( data.substr( 0, accept_size ) );

  cur_size += accept_size;
  cumulative_size += accept_size;
}

void Writer::close()
{
  // Your code here.
  closed = true;
}

void Writer::set_error()
{
  // Your code here.
  error = true;
}

bool Writer::is_closed() const
{
  // Your code here.
  return closed;
}

uint64_t Writer::available_capacity() const
{
  // Your code here.
  return capacity_ - cur_size;
}

uint64_t Writer::bytes_pushed() const
{
  // Your code here.
  return cumulative_size;
}

string_view Reader::peek() const
{
  // Your code here.

  // synthesis the string into one
  vector<string> tmp( 1 );

  for ( auto i = read_index; i < buffers.size(); i++ ) {
    tmp[0] += buffers[i];
  }

  tmp.swap( buffers );
  read_index = 0;
  return string_view( buffers[0] );
}

bool Reader::is_finished() const
{
  // Your code here.
  return closed && read_index == buffers.size();
}

bool Reader::has_error() const
{
  // Your code here.
  return error;
}

void Reader::pop( uint64_t len )
{
  // Your code here.
  if ( len > cur_size )
    throw std::runtime_error( "Reader::pop len exceed cur_size" );

  while ( len > 0 ) {
    auto poping_size = buffers[read_index].size();
    if ( len >= poping_size ) {
      cur_size -= poping_size;
      buffers[read_index++].clear();
      len -= poping_size;
    } else {
      cur_size -= len;
      buffers[read_index] = buffers[read_index].substr( len );
      len = 0;
    }
  }
}

uint64_t Reader::bytes_buffered() const
{
  // Your code here.
  return cur_size;
}

uint64_t Reader::bytes_popped() const
{
  // Your code here.
  return cumulative_size - cur_size;
}
