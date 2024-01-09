#include "wrapping_integers.hh"
#include <initializer_list>
// #include <math.h>

using namespace std;

Wrap32 Wrap32::wrap( uint64_t n, Wrap32 zero_point )
{
  return zero_point + n;
}

uint64_t Wrap32::unwrap( Wrap32 zero_point, uint64_t checkpoint ) const
{
  // Your code here.
  uint64_t init = raw_value_ - zero_point.raw_value_;

  if ( init >= checkpoint )
    return init;

  // checkpoint > init
  uint64_t possible_value = init + ( ( checkpoint - init ) / ( UINT32_MAX + 1ull ) ) * ( UINT32_MAX + 1ull );

  if ( checkpoint - possible_value <= possible_value + UINT32_MAX + 1ull - checkpoint )
    return possible_value;
  else
    return possible_value + UINT32_MAX + 1ull;
}
