#pragma once

#include <Windows.h>

POINT CrossProduct(POINT P, int S);
POINT AddPoint(POINT P1, POINT P2);
POINT SubtractPoint(POINT P1, POINT P2);
POINT MulPoint(POINT P, int32_t S);
POINT NegatePoint(POINT P);
bool Equals(POINT P1, POINT P2);
POINT ToZeroBasePoint(POINT P);
POINT GetMidPoint(POINT P1, POINT P2);

template <typename T> int Sign(T val) {
	return (T(0) < val) - (val < T(0));
}