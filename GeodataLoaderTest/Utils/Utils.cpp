#include "stdafx.h"

#include <Windows.h>
#include <cmath>
#include <iostream>

// points

POINT CrossProduct(POINT P, int S) {
	return { -P.y * S, P.x * S };
}

POINT AddPoint(POINT P1, POINT P2) {
	return { P1.x + P2.x, P1.y + P2.y };
}

POINT SubtractPoint(POINT P1, POINT P2) {
	return { P1.x - P2.x, P1.y - P2.y };
}

POINT NegatePoint(POINT P) {
	return { -P.x, -P.y };
}

bool Equals(POINT P1, POINT P2) {
	return (P1.x == P2.x && P1.y == P2.y);
}

POINT ToZeroBasePoint(POINT P) {
	return { (P.x + 1) / 2, (P.y + 1) / 2 };
}

// color

COLORREF GetSpectrumColor(double t)
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

int DumpBMP32(uint8_t* pixels, int width, int height, const char* filePath) {

	FILE *bmpFile = NULL;
	int fileSize = 54 + 4 * width * height;

	unsigned char bmpfileheader[14] = { 'B','M', 0,0,0,0, 0,0, 0,0, 54,0,0,0 };
	unsigned char bmpinfoheader[40] = { 40,0,0,0, 0,0,0,0, 0,0,0,0, 1,0, 32,0 };
	unsigned char bmppad[3] = { 0,0,0 };

	bmpfileheader[2] = (unsigned char)(fileSize);
	bmpfileheader[3] = (unsigned char)(fileSize >> 8);
	bmpfileheader[4] = (unsigned char)(fileSize >> 16);
	bmpfileheader[5] = (unsigned char)(fileSize >> 24);

	bmpinfoheader[4] = (unsigned char)(width);
	bmpinfoheader[5] = (unsigned char)(width >> 8);
	bmpinfoheader[6] = (unsigned char)(width >> 16);
	bmpinfoheader[7] = (unsigned char)(width >> 24);
	bmpinfoheader[8] = (unsigned char)(height);
	bmpinfoheader[9] = (unsigned char)(height >> 8);
	bmpinfoheader[10] = (unsigned char)(height >> 16);
	bmpinfoheader[11] = (unsigned char)(height >> 24);

	fopen_s(&bmpFile, filePath, "wb");
	fwrite(bmpfileheader, 1, sizeof(bmpfileheader), bmpFile);
	fwrite(bmpinfoheader, 1, sizeof(bmpinfoheader), bmpFile);
	for (int i = 0; i<height; i++)
		fwrite(pixels + (width * (height - i - 1) * 4), 4, (size_t)width, bmpFile);

	fclose(bmpFile);

	return 1;
}