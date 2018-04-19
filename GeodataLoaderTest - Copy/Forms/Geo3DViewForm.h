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

	static void AllocateGrid(int32_t GridWorldX, int32_t GridWorldY, uint32_t GridWidth, uint32_t GridHeight, uint32_t MaxLayersCount);
	static int32_t* GetGridPtr(uint32_t GridX, uint32_t GridY, uint32_t LayerIndex, uint32_t VertexX, uint32_t VertexY);
	static int32_t GetGridVertexIndex(uint32_t GridX, uint32_t GridY, uint32_t LayerIndex, uint32_t VertexX, uint32_t VertexY);
	static void GetGeoLayers(int32_t GridX, int32_t GridY, int16_t& LayersCount, int16_t*& Layers);
	static void FindCorrespondingLayer(int16_t LayersCount, int16_t* Layers, int16_t LayerIndex, int16_t OtherLayersCount, 
		int16_t* OtherLayers, int16_t& CorrespondingLayerIndex, int16_t& CorrespondingHeight);
	static void GetNeighbors(int GridX, int GridY, int16_t LayersCount, int16_t* Layers, int16_t LayerIndex, NeighborInfo Neighbors[3][3]);
	static void FreeGrid(void);
	static void AddLine(InputVertex *P1, InputVertex *P2);
	static void AddNormal(InputVertex *V, XMVECTOR N);
	static void AddTriangleStrip(const int32_t Strip[], int Length);
	static void AddPlane(int GridX, int GridY, int LayerIndex, int16_t Height);
	static void GetSideVertexIndexes(int GridX, int GridY, NeighborInfo Neighbors[3][3], int16_t Height, int OffsetX, int OffsetY, 
		int Direction, int32_t Indexes[4], int& IndexesCount);
	static void GenerateSideTriangleList(int32_t LeftSide[3], int LeftSideCount, int32_t RightSide[3], int RightSideCount);
	static void AddSidePlane(int GridX, int GridY, NeighborInfo Neighbors[3][3], int16_t Height, int OffsetX, int OffsetY);
	static void VisualizeNormals(void);
	static void VisualizeTriangles(void);
	static void NormalizeNormals(void);
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