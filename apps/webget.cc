#include "socket.hh"

#include <cstdlib>
#include <iostream>
#include <span>
#include <string>

using namespace std;

void get_URL( const string& host, const string& path )
{
  Address addr_host( host, "http" );

  TCPSocket sock;
  sock.connect( addr_host );

  string http_method = "GET";
  string line_sep = "\r\n";
  string http_path = http_method + ' ' + path + " HTTP/1.1" + line_sep;
  string http_host = "Host: " + host + line_sep;
  string http_close = "Connection: close" + line_sep + line_sep;

  string payload = http_path + http_host + http_close;

  sock.write( payload );

  while ( !sock.eof() ) {
    string buffer;
    sock.read( buffer );

    cout << buffer;
  }
  // cout << endl;
}

int main( int argc, char* argv[] )
{
  try {
    if ( argc <= 0 ) {
      abort(); // For sticklers: don't try to access argv[0] if argc <= 0.
    }

    auto args = span( argv, argc );

    // The program takes two command-line arguments: the hostname and "path" part of the URL.
    // Print the usage message unless there are these two arguments (plus the program name
    // itself, so arg count = 3 in total).
    if ( argc != 3 ) {
      cerr << "Usage: " << args.front() << " HOST PATH\n";
      cerr << "\tExample: " << args.front() << " stanford.edu /class/cs144\n";
      return EXIT_FAILURE;
    }

    // Get the command-line arguments.
    const string host { args[1] };
    const string path { args[2] };

    // Call the student-written function.
    get_URL( host, path );
  } catch ( const exception& e ) {
    cerr << e.what() << "\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
