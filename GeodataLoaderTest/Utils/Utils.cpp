#include "stdafx.h"
#include <cmath>

// color

COLORREF getSpectrumColor(double t)
{
	double w;

	w = 380.0 + t * (780.0 - 380.0);
	// w = 440.0 + t * (645.0 - 440.0);

	double R, G, B;

	if (w >= 380.0 && w < 440.0)
	{
		R = -(w - 440.0) / (440.0 - 380.0);
		G = 0.0;
		B = 1.0;
	}
	else
		if (w >= 440.0 && w < 490.0)
		{
			R = 0.0;
			G = (w - 440.0) / (490.0 - 440.0);
			B = 1.0;
		}
		else
			if (w >= 490.0 && w < 510.0)
			{
				R = 0.0;
				G = 1.0;
				B = -(w - 510.0) / (510.0 - 490.0);
			}
			else
				if (w >= 510.0 && w < 580.0)
				{
					R = (w - 510.0) / (580.0 - 510.0);
					G = 1.0;
					B = 0.0;
				}
				else
					if (w >= 580.0 && w < 645.0)
					{
						R = 1.0;
						G = -(w - 645.0) / (645.0 - 580.0);
						B = 0.0;
					}
					else
						if (w >= 645.0 && w <= 780.0)
						{
							R = 1.0;
							G = 0.0;
							B = 0.0;
						}
						else
						{
							R = 0.0;
							G = 0.0;
							B = 0.0;
						}

	double SSS;

	if (w >= 380.0 && w < 420.0)
		SSS = 0.3 + 0.7 * (w - 350.0) / (420.0 - 350.0);
	else
		if (w >= 420.0 && w <= 700.0)
			SSS = 1.0;
		else
			if (w > 700.0 && w <= 780.0)
				SSS = 0.3 + 0.7 * (780.0 - w) / (780.0 - 700.0);
			else
				SSS = 0.0;

	SSS = SSS * 255.0;

	return RGB((int)round(B * SSS), (int)round(G * SSS), (int)round(R * SSS));
}

// time

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

double TimeToSeconds(LONGLONG Time) {

	return (double)Time / (double)Freq.QuadPart;
}

LONGLONG GetTime(void) {

	LARGE_INTEGER Result;

	QueryPerformanceCounter(&Result);

	return Result.QuadPart;
}

// forms

ATOM RegisterClass(LPCWSTR ClassName, WNDPROC WndProc, HINSTANCE hInstance)
{
	WNDCLASSEXW wcex;

	wcex.cbSize = sizeof(WNDCLASSEX);

	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	wcex.hIcon = 0;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wcex.lpszMenuName = 0;
	wcex.lpszClassName = ClassName;
	wcex.hIconSm = 0;

	return RegisterClassExW(&wcex);
}

bool ProcessMessages(void) {

	MSG Msg;

	// messages
	while (PeekMessage(&Msg, NULL, 0, 0, PM_REMOVE)) {

		if (Msg.message == WM_QUIT)
			return false;

		TranslateMessage(&Msg);
		DispatchMessage(&Msg);
	}

	return true;
}

typedef void(*TickCallback)(double dt);

void Run(double FPS, TickCallback Callback) {

	double Dt;

	if (FPS != 0.0)
		Dt = 1.0 / FPS;
	else
		Dt = 0.0;

	LONGLONG StartTime = GetTime();
	LONGLONG TickCounter = 0;
	LONGLONG LastTick = GetTime();

	// Main message loop:
	while (TRUE)
	{
		LONGLONG NextTickTime;

		// tick
		LONGLONG Now = GetTime();
		while (TRUE) {

			if (Dt == 0.0) {

				LONGLONG Now = GetTime();
				double Elapsed = TimeToSeconds(Now - LastTick);
				LastTick = Now;

				if (Callback)
					Callback(Elapsed);

				NextTickTime = StartTime;

				break;
			}
			else {
				NextTickTime = StartTime + SecondsToTime(TickCounter * Dt);
				if (Now >= NextTickTime) {

					if (Callback)
						Callback(Dt);

					TickCounter++;
				}
				else
					break;
			}
		}

		if (!ProcessMessages())
			break;

		// wait message or next tick time
		Now = GetTime();
		LONG Delta = TimeToMs(NextTickTime - Now);
		if (Delta > 0)
			MsgWaitForMultipleObjects(0, NULL, FALSE, (DWORD)Delta, QS_ALLEVENTS);
	}
}