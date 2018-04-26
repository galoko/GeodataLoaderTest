#include "stdafx.h"

#include <inttypes.h>

#include <d3d11.h>
#include <DirectXMath.h>

#define _USE_MATH_DEFINES
#include <cmath>

#include <array>

#include "earcut.hpp"

#include "Geodata\L2Geodata.h"

using namespace DirectX;

struct InputVertex;
struct NeighborInfo;
struct UsageInfo;
struct HeightRange;

using Point = array<int32_t, 2>;

class Geo3DViewForm {
private:
	static const int MAX_HEIGHT_RANGES_COUNT = L2Geodata::LAYERS_PER_SUBBLOCK_LIMIT * 2; 

	static void BuildViewMatrix(void);
	static void BuildWorldMatrix(void);
	static void BuildShaderMatrix(void);

	static uint32_t AllocateVertexIndex(void);
	static uint32_t AllocateIndexIndex(uint32_t Index);
	static void ResetScene(void);
	static void ApplyTootle(void);
	static void CommitScene(void);

	static void GenerateNSWETexture(void);

	static void GetGeoLayers(int32_t GridX, int32_t GridY, int16_t& LayersCount, int16_t*& Layers);
	static void FindCorrespondingLayer(int16_t LayersCount, int16_t* Layers, int16_t LayerIndex, int16_t OtherLayersCount, 
		int16_t* OtherLayers, int16_t& CorrespondingLayerIndex, int16_t& CorrespondingHeight);
	static void GetNeighbors(int GridX, int GridY, int16_t LayersCount, int16_t* Layers, int16_t LayerIndex, NeighborInfo Neighbors[3][3]);
	static XMFLOAT3 BakeLightColor(XMVECTOR TextureColor, XMVECTOR Normal);

	static bool GenerateConcaveHull(vector<Point>& Dest, int StartX, int StartY, int TurnDirection);
	static bool GenerateConcaveHullAndHolesFromUsageMap(void);
	static bool GenerateModelFromUsageMap(void);

	static void SetupFloodFillStack(int StackSize);
	static void ResetFloodFillStack(void);
	static void PushGridCell(int GridX, int GridY);
	static void PushGridNeighbors(int GridX, int GridY);
	static void PushHeightRangeNeighborsOneSide(int GridX, int GridY, int OffsetX, int OffsetY, POINT Direction, HeightRange* Range);
	static void PushHeightRangeNeighborsBothSides(int GridX, int GridY, int OffsetX, int OffsetY, POINT Direction, HeightRange* Range);
	static bool PopStackPoint(POINT& Point);
	static void FinalizeFloodFillStack(void);

	static void SetupPointUsageMap(int MapWidth, int MapHeight);
	static void ResetPointUsageMap(int CenterGridX, int CenterGridY, bool CenterX, bool CenterY);
	static void ApplyGridCellToPointUsageMap(int GridX, int GridY);
	static bool ApplyHeightRangeToPointUsageMap(int GridX, HeightRange *Range);
	static UsageInfo GetPointUsage(int GridX, int GridY);
	static void SetPointUsageCount(int GridX, int GridY, uint8_t UsageCount);
	static bool IsCellInUsageBound(int GridX, int GridY);
	static bool IsPointInUsageBound(int GridX, int GridY);
	static bool FindPointByUsageCount(uint8_t UsageCount, POINT& Point);
	static void FinalizePointUsageMap(void);

	static void AddTopPlaneModel(int16_t SubBloc, int Direction);
	static void GenerateTopPlanes(int GridX, int GridY);

	static void GetLowAndHighLayers(int16_t SubBlock, int16_t* Layers, int16_t LayersCount, int16_t& LowLayerIndex, int16_t& HighLayerIndex);
	static bool CanGoInThisDirection(int16_t SubBlock, int DirectionX, int DirectionY);
	static bool CanGoUnderneath(int16_t SubBlock, int16_t HigherSubBlock);
	static bool GetDestSubBlock(int16_t SubBlock, int OffsetX, int OffsetY, int16_t* Layers, int16_t LayersCount, int16_t& DestSubBlock);
	static bool GetWallDestHeight(int16_t SubBlock, int OffsetX, int OffsetY, int16_t* Layers, int16_t LayersCount, int16_t& DestHeight);
	static void GetHeightRanges(int GridX, int GridY, int OffsetX, int OffsetY, HeightRange *DestHeightRanges, int& DestHeightRangesCount);
	static void GenerateSidePlanes(int GridX, int GridY, int OffsetX, int OffsetY);

	static void SetupGenerationGrid(int32_t WorldX, int32_t WorldY, uint32_t Width, uint32_t Height);
	static bool IsInGridBound(int GridX, int GridY);
	static bool GetGridUsage(int GridX, int GridY, int Side, int16_t LayerOrHeightIndex);
	static void SetGridSideUsage(int GridX, int GridY, int Side, int16_t LayerOrHeightIndex);
	static void FinalizeGenerationGrid(void);

	static void GenerateGeodataScene(int32_t WorldX, int32_t WorldY, uint32_t Width, uint32_t Height);
	static void GenerateDebugGeodataScene(void);

	static void GenerateDebugStaticScene(void);

	static void PrintCurrentCoord(void);
	static bool HandleKeyPress(int Key);

	static void ProcessWindowState(void);
	static void ProcessMouseInput(LONG dx, LONG dy);
	static void ProcessKeyboardInput(double dt);
	static void DrawScene(void);

	static LRESULT CALLBACK WndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
public:
	static void Init(unsigned int Width, unsigned int Height, WCHAR *WindowClass, WCHAR *Title, HINSTANCE hInstance);
	static void Show(void);
	static void WaitForNextFrame(void);
	static void Tick(double dt);
};