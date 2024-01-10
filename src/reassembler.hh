#pragma once

#include "byte_stream.hh"

#include <queue>
#include <string>
#include <unordered_map>

class Reassembler
{
  typedef std::tuple<uint64_t, uint64_t, uint64_t> meta;
  struct CompareFunction
  {
    bool operator()( const meta& a, const meta& b ) const
    {
      const auto [l_a, r_a, i_a] = a;
      const auto [l_b, r_b, i_b] = b;
      // return a > b. a < b if l_a < l_b or i_a > i_b
      return l_a > l_b || ( ( l_a == l_b ) && ( i_a < i_b ) );
    }
  };

  bool lastOccured = false;
  /// @brief internal storage, sorted <l,r, string index>, where string index points to the trimmed incoming
  /// string
  mutable std::priority_queue<meta, std::vector<meta>, CompareFunction> meta_buffer = {};
  std::vector<std::string> storage = {};

public:
  /*
   * Insert a new substring to be reassembled into a ByteStream.
   *   `first_index`: the index of the first byte of the substring
   *   `data`: the substring itself
   *   `is_last_substring`: this substring represents the end of the stream
   *   `output`: a mutable reference to the Writer
   *
   * The Reassembler's job is to reassemble the indexed substrings (possibly out-of-order
   * and possibly overlapping) back into the original ByteStream. As soon as the Reassembler
   * learns the next byte in the stream, it should write it to the output.
   *
   * If the Reassembler learns about bytes that fit within the stream's available capacity
   * but can't yet be written (because earlier bytes remain unknown), it should store them
   * internally until the gaps are filled in.
   *
   * The Reassembler should discard any bytes that lie beyond the stream's available capacity
   * (i.e., bytes that couldn't be written even if earlier gaps get filled in).
   *
   * The Reassembler should close the stream after writing the last byte.
   */
  void insert( uint64_t first_index, std::string data, bool is_last_substring, Writer& output );

  // How many bytes are stored in the Reassembler itself?
  uint64_t bytes_pending() const;
};
