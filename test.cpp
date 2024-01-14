#include <bits/stdc++.h>
using namespace std;

class A
{
  vector<int>& b;

public:
  A( vector<int>& a ) : b( a )
  {
    void* ja = &b;
    cout << 1;
  }
};
int main()
{
  vector<int> c( { 1, 23, 4 } );
  A d( c );
  cout << 1;
}