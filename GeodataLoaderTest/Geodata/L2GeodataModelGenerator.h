#pragma once

#include <inttypes.h>
#include <array>
#include <DirectXMath.h>

#include "L2Geodata.h"
#include "earcut.hpp"

using namespace std;
using namespace DirectX;

#pragma pack(push,1)
struct GeodataVertex
{
	XMFLOAT3 Pos;
	XMFLOAT2 Tex;
	XMFLOAT3 NSWETex;
	XMFLOAT3 Normal;

	GeodataVertex() {
	}
};
#pragma pack(pop)

class L2GeodataModelGenerator {
private:
	// Consts

	static const int MAX_HEIGHT_RANGES_COUNT = L2Geodata::LAYERS_PER_SUBBLOCK_LIMIT * 2;

	// Types

	struct HeightRange {
		int Direction;
		int16_t Start, End;

		HeightRange() { };

		HeightRange(int16_t Start, int16_t End);
		bool SortedOverlapTestWith(const HeightRange* OtherRange);
		bool OverlapTestWith(const HeightRange* OtherRange);
		bool DirectionInvariantOverlapTestWith(const HeightRange* OtherRange);
		void MergeWith(const HeightRange* OtherRange);
		int16_t GetMeanHeight(void);

		bool operator < (const HeightRange& Other);
	};

#pragma pack(push,1)
	struct UsageInfo {
		bool IsSet : 1;
		uint8_t UsageCount : 3;
	};
#pragma pack(pop)

	using Point = array<int32_t, 2>;
	
	// Fields

	// Output
	GeodataVertex *VertexBuffer;
	uint32_t VertexBufferSize, NextVertexIndex;

	uint32_t *IndexBuffer;
	uint32_t IndexBufferSize, NextIndexIndex;

	// Generation Grid definition

	uint32_t GridWorldX;
	uint32_t GridWorldY;
	uint32_t GridWidth;
	uint32_t GridHeight;

	// Grid that hold every cell and layer usage inside the grid
	uint8_t *GridUsageMap;

	// Flood Fill's stack

	POINT *FloodFillStack;
	uint32_t FloodFillStackSize;
	int32_t FloodFillStackIndex;

	// Point usage counter table, gets filled by Flood Fill algorithm

	uint32_t PointUsageMapWidth;
	uint32_t PointUsageMapHeight;
	int32_t PointUsageMapGridX;
	int32_t PointUsageMapGridY;
	UsageInfo *PointUsageMap;
	RECT PointUsageMapUsedBoundBox;

	// Mem cache for hull generation
	vector<Point> Hull;

	// Points generated based on Point Usage map, used by EarCut algorithm

	// EarCut input
	vector<vector<Point>> SeparatedPoints;
	// EarCut output index for this list
	vector<Point> Points;
	// EarCut also output Indices
	vector<uint32_t> Indices;

	// Methods

	// Allocation

	void SetupOutputBuffers(GeodataVertex *VertexBuffer, uint32_t VertexBufferSize, uint32_t *IndexBuffer, uint32_t IndexBufferSize);
	uint32_t AllocateVertexIndex(void);
	uint32_t AllocateIndexIndex(uint32_t Index);

	// Geodata utils

	void GetGeoLayers(int32_t GridX, int32_t GridY, int16_t& LayersCount, int16_t*& Layers);
	void GetHeightRanges(int GridX, int GridY, int OffsetX, int OffsetY, HeightRange *DestHeightRanges, int& DestHeightRangesCount);

	// Model generation utils

	bool GenerateConcaveHull(vector<Point>& Dest, int StartX, int StartY, int TurnDirection);
	bool GenerateConcaveHullAndHolesFromUsageMap(void);
	bool GenerateModelFromUsageMap(void);

	void AddTopPlaneModel(int16_t SubBloc, int Direction);

	void GenerateTopPlanes(int GridX, int GridY);
	void GenerateSidePlanes(int GridX, int GridY, int OffsetX, int OffsetY);

	// Generation Grid

	void SetupGenerationGrid(int32_t WorldX, int32_t WorldY, uint32_t Width, uint32_t Height);
	bool IsInGridBound(int GridX, int GridY);
	bool GetGridUsage(int GridX, int GridY, int Side, int16_t LayerOrHeightIndex);
	void SetGridSideUsage(int GridX, int GridY, int Side, int16_t LayerOrHeightIndex);
	void FinalizeGenerationGrid(void);

	// Point Usage

	void SetupPointUsageMap(int MapWidth, int MapHeight);
	void ResetPointUsageMap(int CenterGridX, int CenterGridY, bool CenterX, bool CenterY);
	void ApplyGridCellToPointUsageMap(int GridX, int GridY);
	bool ApplyHeightRangeToPointUsageMap(int GridX, HeightRange *Range);
	UsageInfo GetPointUsage(int GridX, int GridY);
	void SetPointUsageCount(int GridX, int GridY, uint8_t UsageCount);
	bool IsCellInUsageBound(int GridX, int GridY);
	bool IsPointInUsageBound(int GridX, int GridY);
	bool FindPointByUsageCount(uint8_t UsageCount, POINT& Point);
	void FinalizePointUsageMap(void);

	// Flood Fill

	void SetupFloodFillStack(int StackSize);
	void ResetFloodFillStack(void);
	void PushGridCell(int GridX, int GridY);
	void PushGridNeighbors(int GridX, int GridY);
	void PushHeightRangeNeighborsOneSide(int GridX, int GridY, int OffsetX, int OffsetY, POINT Direction, HeightRange* Range);
	void PushHeightRangeNeighborsBothSides(int GridX, int GridY, int OffsetX, int OffsetY, POINT Direction, HeightRange* Range);
	bool PopStackPoint(POINT& Point);
	void FinalizeFloodFillStack(void);
public:
	L2GeodataModelGenerator(void) { };

	static const int NSWE_TEX_WIDTH = 16;
	static const int NSWE_TEX_HEIGHT = 16 * NSWE_TEX_WIDTH;
		
	void GenerateGeodataScene(int32_t WorldX, int32_t WorldY, uint32_t Width, uint32_t Height, GeodataVertex *VertexBuffer, uint32_t &VertexBufferSize,
		uint32_t *IndexBuffer, uint32_t &IndexBufferSize);

	static void GenerateNSWETexture(uint32_t Pixels[NSWE_TEX_HEIGHT][NSWE_TEX_WIDTH]);
};