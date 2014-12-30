#ifndef FIXEDPOINT_H
#define FIXEDPOINT_H
#include <stdint.h>
/* 
 * Our fixpoint has 17.14 format, which means 
 * the lowest 14 bits of a signed 32-bit integer as fractional bits, 
 * so that an integer x represents the real number x/2^14
 */
static const int f = 1 << 14;

int integer_to_fixedpoint(int n);

int fixedpoint_to_integer(int x);

int fixedpoint_to_integer_nearest(int x);

int fixedpoint_add(int x, int y);

int fixedpoint_sub(int x, int y);

int fixedpoint_mul(int x, int y);

int mix_mul(int x, int n);

int fixedpoint_div(int x, int y);

int mix_div(int x, int n);

int
integer_to_fixedpoint ( int n ) {
  register int nr = n;
  return nr * f;
}

int
fixedpoint_to_integer ( int x ) {
  register int xr = x;
  return xr / f;
}

int
fixedpoint_to_integer_nearest ( int x ) {
  register int xr = x;
  if ( xr >= 0 )
    return ( x + f / 2 ) / f;
  else
    return ( x - f / 2 ) / f;
}

int
fixedpoint_add ( int x, int y ) {
  return x + y;
}

int
fixedpoint_sub ( int x, int y ) {
  return x - y;
}

int
fixedpoint_mul ( int x, int y ) {
  return ((int64_t) x) * y / f;
}

int
mix_mul (int x, int n) {
  return x * n;
}

int
fixedpoint_div ( int x, int y ) {
  return ((int64_t) x) * f / y;
}

int
mix_div (int x, int n) {
  return x / n;
}

#endif
