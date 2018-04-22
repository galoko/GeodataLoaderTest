#include "stdafx.h"

#include <inttypes.h>

#include <d3d11.h>
#include <DirectXMath.h>

#define _USE_MATH_DEFINES
#include <cmath> 

using namespace DirectX;

struct InputVertex;
struct NeighborInfo;
enum PlaneSide;

class Geo3DViewForm {
private:
	static void BuildViewMatrix(void);
	static void BuildWorldMatrix(void);
	static void BuildShaderMatrix(void);

	static uint32_t AllocateVertexIndex(void);
	static uint32_t AllocateIndexIndex(uint32_t Index);
	static void ResetScene(void);
	static void CommitScene(void);

	static void GenerateDebugStaticScene(void);

	static void GetGeoLayers(int32_t GridX, int32_t GridY, int16_t& LayersCount, int16_t*& Layers);
	static void FindCorrespondingLayer(int16_t LayersCount, int16_t* Layers, int16_t LayerIndex, int16_t OtherLayersCount, 
		int16_t* OtherLayers, int16_t& CorrespondingLayerIndex, int16_t& CorrespondingHeight);
	static void GetNeighbors(int GridX, int GridY, int16_t LayersCount, int16_t* Layers, int16_t LayerIndex, NeighborInfo Neighbors[3][3]);
	static void AddLine(InputVertex *P1, InputVertex *P2);
	static void AddTriangleStrip(const int32_t Strip[], int Length);
	static void AddPlane(int GridX, int GridY, int16_t Height);
	static void AddSidePlane(int GridX, int GridY, int16_t Height, int16_t DestHeight, int OffsetX, int OffsetY);
	static void VisualizeNormals(void);
	static void VisualizeTriangles(void);
	static void GenerateTopPlanes(int GridX, int GridY);
	static void GenerateSidePlanes(int GridX, int GridY, int OffsetX, int OffsetY);

	static void SetupGenerationGrid(int32_t WorldX, int32_t WorldY, uint32_t Width, uint32_t Height);
	static bool GetGridUsage(int GridX, int GridY, int Side, int16_t LayerIndex);
	static void SetGridSideUsage(int GridX, int GridY, int Side, int16_t LayerIndex);
	static void FinalizeGenerationGrid(void);

	static void GenerateGeodataScene(int32_t WorldX, int32_t WorldY, uint32_t Width, uint32_t Height);
	static void GenerateDebugGeodataScene(void);

	static void ProcessMouseInput(LONG dx, LONG dy);
	static void ProcessKeyboardInput(double dt);
	static void DrawScene(void);

	static LRESULT CALLBACK WndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
public:
	static void Init(unsigned int Width, unsigned int Height, WCHAR *WindowClass, WCHAR *Title, HINSTANCE hInstance);
	static void Show(void);
	static void Tick(double dt);
};