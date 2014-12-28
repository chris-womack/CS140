#ifndef FIXEDPOINT_H
#define FIXEDPOINT_H

int integer_to_fixedpoint(int n);

int fixedpoint_to_integer(int x);

int fixedpoint_to_integer_nearest(int x);

int fixedpoint_add(int x, int y);

int fixedpoint_sub(int x, int y);

int fixedpoint_mul(int x, int y);

int fixedpoint_div(int x, int y);

#endif
