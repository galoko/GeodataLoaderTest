#include "stdafx.h"

static LARGE_INTEGER Freq;

void InitTime(void) {
	QueryPerformanceFrequency(&Freq);
}

LONGLONG SecondsToTime(double Seconds) {

	return (LONGLONG)(Seconds * Freq.QuadPart);
}

LONG TimeToMs(LONGLONG Time) {

	return (LONG)(Time * 1000 / Freq.QuadPart);
}

LONGLONG GetTime(void) {

	LARGE_INTEGER Result;

	QueryPerformanceCounter(&Result);

	return Result.QuadPart;
}