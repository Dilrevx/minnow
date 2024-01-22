#include "address.hh"
#include "eventloop.hh"
#include "exception.hh"

#include "tcp_minnow_socket.cc"
#include "tcp_over_ip.hh"

#include <cstdlib>
#include <functional>
#include <iostream>
#include <thread>
using namespace std;

int main( int argc, char** argv )
{
  if ( argc != 3 ) {
    cerr << "Usage: " << argv[0] << " <port> <port>\n";
    return EXIT_FAILURE;
  }

  auto args = span( argv, argc );

  const string localhost = "127.0.0.1"s;
  const string s_port = args[1], c_port = args[2];
  Address server { localhost, 0 }, client { localhost, 0 };

  UDPSocket s_sock, c_sock;
  s_sock.bind( { localhost, s_port } );
  c_sock.bind( { localhost, c_port } );

  auto server_pair = make_pair( server, move( s_sock ) );
  auto client_pair = make_pair( client, move( c_sock ) );

  function a2b = [&]( pair<Address, UDPSocket>& a, pair<Address, UDPSocket>& b ) {
    pollfd afd { a.second.fd_num(), POLLIN, 0 };
    while ( true ) {
      CheckSystemCall( "poll", ::poll( &afd, 1, -1 ) );
      string buf;
      if ( afd.revents & POLLIN ) {
        a.second.recv( a.first, buf );
      } else
        continue;

      if ( b.first.port() != 0 ) {
        b.second.sendto( b.first, buf );
      }
    }
  };

  pair<thread, thread> threads = make_pair( thread { a2b, ref( server_pair ), ref( client_pair ) },
                                            thread { a2b, ref( client_pair ), ref( server_pair ) } );

  threads.first.detach();
  threads.second.detach();
  return EXIT_SUCCESS;
}