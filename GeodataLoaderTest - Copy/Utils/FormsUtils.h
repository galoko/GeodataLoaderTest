#include "stdafx.h"

ATOM RegisterClass(LPCWSTR ClassName, WNDPROC WndProc, HINSTANCE hInstance);

bool ProcessMessages(void);

typedef void(*TickCallback)(double dt);

void Run(double FPS, TickCallback Callback);