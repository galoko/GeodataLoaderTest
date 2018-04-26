#include <Windows.h>

inline POINT CrossProduct(POINT P, int S);
inline POINT AddPoint(POINT P1, POINT P2);
inline POINT NegatePoint(POINT P);
inline bool Equals(POINT P1, POINT P2);
inline POINT ToZeroBasePoint(POINT P);

template <typename T> int Sign(T val) {
	return (T(0) < val) - (val < T(0));
}