#include "stdafx.h"

#include <string>
#include <iostream>

#include "TimeUtils.h"
#include "FormsUtils.h"

#include "GeodataLoaderTest.h"
#include "Geodata\L2Geodata.h"
#include "Forms\Geo3DViewForm.h"

void OpenConsole(void) {
	AllocConsole();
	FILE* fp;
	freopen_s(&fp, "CONOUT$", "w", stdout);
}

void Tick(double dt) {

	Geo3DViewForm::Tick(dt);
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, 
	_In_ LPWSTR lpCmdLine,  _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

	OpenConsole();

	InitTime();

	L2Geodata::Init();
	// L2Geodata::Load(L"..\\data\\pts", GeoType::PTS);
	// L2Geodata::SaveEasyGeo(L"..\\data\\easygeo.bin");

	L2Geodata::LoadEasyGeo(L"..\\data\\easygeo.bin");

	Geo3DViewForm::Init(1280, 960, L"Geo3DView", L"Geodata 3D View", hInstance);
	Geo3DViewForm::Show();

	Run(0.0, Tick);

	return 0;
}