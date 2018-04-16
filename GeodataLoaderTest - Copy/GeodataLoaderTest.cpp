// GeodataLoaderTest.cpp : Defines the entry point for the application.
//

#include "stdafx.h"

#include <string>
#include <iostream>

#include "TimeUtils.h"
#include "GeodataLoaderTest.h"

#include "Geodata\L2Geodata.h"

#define WIDTH  560
#define HEIGHT 440

BITMAPINFO BitmapInfo;
COLORREF Pixels[HEIGHT][WIDTH];

void SetPixel(int X, int Y, COLORREF Color) {
	Pixels[Y][X] = Color;
}

// Global Variables:
HINSTANCE hInst;               // current instance
WCHAR *WindowClass;            // the main window class name
WCHAR *Title;                  // The title bar text
HWND hWnd;                      

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

double t;

void Tick(double dt) {

	t += dt;

	double part_t = t / 4.0;

	part_t = (part_t - (int) part_t) * 2.0;
	if (part_t > 1.0)
		part_t = 2.0 - part_t;

	for (int y = 0; y < HEIGHT; y++)
		for (int x = 0; x < WIDTH; x++)
			SetPixel(x, y, RGB((int) (part_t * 255), x * 256 / WIDTH, y * 256 / HEIGHT));
}

int16_t GeoToHeight(int16_t GeoHeightWithNWSE) {
	return (GeoHeightWithNWSE & 0xFFF0) >> 1;
}

void DrawGeodata(int StartX, int StartY) {

	int16_t MinValue = MAXSHORT;
	int16_t MaxValue = MINSHORT;

	for (int Y = 0; Y < HEIGHT; Y++)
		for (int X = 0; X < WIDTH; X++) {

			int16_t Height = GeoToHeight(L2Geodata::GetHeight(StartX + X * 16, StartY + Y * 16));

			MinValue = min(MinValue, Height);
			MaxValue = max(MaxValue, Height);
		}

	int16_t Range = MaxValue - MinValue;

	for (int Y = 0; Y < HEIGHT; Y++)
		for (int X = 0; X < WIDTH; X++) {

			int16_t Height = GeoToHeight(L2Geodata::GetHeight(StartX + X * 16, StartY + Y * 16));

			double t = (double) (Height - MinValue) / (double) Range;
			if (t < 0.0 || t > 1.0)
				throw new std::runtime_error("t is invalid");

			SetPixel(X, Y, getSpectrumColor(t));
		}
}

void OpenConsole(void) {
	AllocConsole();
	FILE* fp;
	freopen_s(&fp, "CONOUT$", "w", stdout);
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

	OpenConsole();

	WindowClass = L"MainWindow";
	Title = L"Geodata loader test";

    // Initialize global strings
    MyRegisterClass(hInstance);

    // Perform application initialization:
    if (!InitInstance (hInstance, nCmdShow))
        return FALSE;

	InitTime();

	L2Geodata::Load(L"..\\data\\pts", GeoType::PTS);

	DrawGeodata(13000, 140572);

	double Dt = 1.0 / 60.0;

	LONGLONG StartTime = GetTime();
	LONGLONG TickCounter = 0;

    MSG msg;

    // Main message loop:
    while (TRUE)
    {		
		LONGLONG NextTickTime;

		// tick
		BOOL Ticked = FALSE;
		LONGLONG Now = GetTime();
		while (TRUE) {

			NextTickTime = StartTime + SecondsToTime(TickCounter * Dt);
			if (Now >= NextTickTime) {

				// Tick(Dt);

				TickCounter++;

				Ticked = TRUE;
			}
			else
				break;
		}
		if (Ticked)
			RedrawWindow(hWnd, NULL, 0, RDW_INVALIDATE);

		// messages
		while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {

			if (msg.message == WM_QUIT)
				goto exit;

			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		// wait message or next tick time
		Now = GetTime();
		LONG Delta = TimeToMs(NextTickTime - Now);
		if (Delta > 0)
			MsgWaitForMultipleObjects(0, NULL, FALSE, (DWORD) Delta, QS_ALLEVENTS);
    }

exit:
    return (int) msg.wParam;
}

ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
	wcex.hIcon          = 0;
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName   = 0;
    wcex.lpszClassName  = WindowClass;
	wcex.hIconSm        = 0;

    return RegisterClassExW(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
	hInst = hInstance; // Store instance handle in our global variable

	hWnd = CreateWindowW(WindowClass, Title, WS_POPUP,
		(GetSystemMetrics(SM_CXSCREEN) - WIDTH) / 2, (GetSystemMetrics(SM_CYSCREEN) - HEIGHT) / 2, WIDTH, HEIGHT, nullptr, nullptr, hInstance, nullptr);

	if (!hWnd)
	{
		return FALSE;
	}

	ZeroMemory(&BitmapInfo, sizeof(BITMAPINFO));
	BitmapInfo.bmiHeader.biBitCount = 32;
	BitmapInfo.bmiHeader.biWidth = WIDTH;
	BitmapInfo.bmiHeader.biHeight = HEIGHT;
	BitmapInfo.bmiHeader.biPlanes = 1;
	BitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	BitmapInfo.bmiHeader.biSizeImage = sizeof(Pixels);
	BitmapInfo.bmiHeader.biCompression = BI_RGB;

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

	return TRUE;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);

			int StartX, StartY;

			StartX = 0;
			StartY = 0;

			StretchDIBits(hdc, StartX, StartY + HEIGHT - 1, WIDTH, -HEIGHT, 0, 0, WIDTH, HEIGHT, &Pixels, &BitmapInfo, DIB_RGB_COLORS, SRCCOPY);
            
            EndPaint(hWnd, &ps);
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}
