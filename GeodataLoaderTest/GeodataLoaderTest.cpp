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

	// L2GeodataPathFind::GenerateNeighborWeightCache();
	// L2Geodata::SaveNeighborWeightCache(L"..\\data\\nwc_cache.bin");
	L2Geodata::LoadNeighborWeightCache(L"..\\data\\nwc_cache.bin");

	Geo3DViewForm::GetInstance().Init(1280, 960, L"Geo3DView", L"Geodata 3D View", hInstance);
	Geo3DViewForm::GetInstance().Show();

	LONGLONG LastTick = GetTime();

	// Main message loop:
	while (TRUE)
	{
		// wait for frame
		Geo3DViewForm::GetInstance().WaitForNextFrame();

		// process input
		if (!ProcessMessages())
			break;

		// time calculation
		LONGLONG Now = GetTime();
		double dt = TimeToSeconds(Now - LastTick);
		LastTick = Now;

		// actual draw call
		Geo3DViewForm::GetInstance().Tick(dt);
	}

	return 0;
}