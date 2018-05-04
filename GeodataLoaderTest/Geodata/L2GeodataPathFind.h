#pragma once

#include <iostream>
#include <vector>
#include <queue>
#include <DirectXMath.h>

#include "MathUtils.h"
#include "Geodata\L2Geodata.h"

using namespace std;
using namespace DirectX;

class L2GeodataPathFind;

typedef uint32_t (*DebugCallbackFunc)(L2GeodataPathFind& PathFind);

class L2GeodataPathFind {
private:
	const static int REGION_SIZE = 512;
	const static int NEIGHBORS_REGION_SIZE = 5;
	const static int MAX_NEIGHBOR_DIST = (NEIGHBORS_REGION_SIZE / 2) * 2;
	const static int CHECKED_POINTS_IN_LIST_LIMIT = 10000;

	const static int STRAIGHT_WEIGHT = 10;
	const static int DIAGONAL_ADDITIONAL_WEIGHT = 7;
	const static int HEIGHT_WEIGHT = 2;
	const static int NEIGHBOR_WALL_WEIGHT = 10;
	const static int NEIGHBOR_INACCESSIBLE_WEIGHT = 10;

#pragma pack(push,1)
	struct RegionBufferEntry {
		bool IsChecked : 1;
		uint8_t DirectionIndex : 2;
		uint8_t PrevLayerIndex : 5;
	};
#pragma pack(pop)

	struct RegionBuffer {

		POINT RegionPoint;

		RegionBufferEntry Data[REGION_SIZE * REGION_SIZE * L2Geodata::LAYERS_PER_SUBBLOCK_LIMIT];

		void SetPointEntry(XMINT3 Point, RegionBufferEntry Entry);
		RegionBufferEntry GetPointEntry(XMINT3 Point);
	};

	struct NeighborsRegionBuffer {
		uint8_t Data[(NEIGHBORS_REGION_SIZE * NEIGHBORS_REGION_SIZE + 7) / 8];

		bool IsPointChecked(uint8_t X, uint8_t Y);
		void SetPointCheched(uint8_t X, uint8_t Y);
	};

	struct NeighborsRegionQueue {

		const static int DATA_LEN = NEIGHBORS_REGION_SIZE * NEIGHBORS_REGION_SIZE;

		struct {
			uint8_t X, Y, LayerIndex;
		} Data[DATA_LEN];

		int32_t CurrentPushIndex, CurrentPopIndex;

		NeighborsRegionQueue(void);
		void Push(uint8_t X, uint8_t Y, uint8_t LayerIndex);
		bool Pop(uint8_t &X, uint8_t &Y, uint8_t& LayerIndex);
	};
		
	struct PathFindPoint {
		uint32_t Weight, HeuristicWeight;
		
		int32_t GridX, GridY;
		int16_t LayerIndex, SubBlock;

		PathFindPoint(int32_t GridX, int32_t GridY, int16_t Height);
 		PathFindPoint(int32_t GridX, int32_t GridY, int16_t LayerIndex, int16_t SubBlock);

		static uint32_t GetDistEuclidean(PathFindPoint& P1, PathFindPoint& P2);
		static uint32_t GetDistManhattan(PathFindPoint& P1, PathFindPoint& P2);
		static uint32_t GetDistManhattan2D(PathFindPoint& P1, PathFindPoint& P2);

		static uint32_t CalcHeightWeight(PathFindPoint& From, PathFindPoint& To);
		static uint32_t CalcDistanceWeight(POINT& PrevDirection, PathFindPoint& From, PathFindPoint& To);
		static uint32_t CalcNeighborsWeight(PathFindPoint& P);
		static uint32_t CalcWeight(POINT& PrevDirection, PathFindPoint& From, PathFindPoint& To);
		static uint32_t CalcHeuristicWeight(PathFindPoint& From, PathFindPoint& To);

		XMINT3 GetWorldPoint(void);

		void CalcAllWeights(POINT& PrevDirection, PathFindPoint& Prev, PathFindPoint& Finish);

		void ApplyEntry(RegionBufferEntry Entry);

		bool operator==(const PathFindPoint& Other);
		bool operator<(const PathFindPoint& Other);
	};

	DebugCallbackFunc DebugCallback;

	vector<RegionBuffer*> Regions;
	POINT RegionOffset;

	RegionBuffer* LastRegion;
	POINT LastRegionPoint;

	vector<PathFindPoint> PointsToCheck;
	vector<XMINT3> CheckedPoints;

	static POINT ToGrid(POINT World);
	static POINT ToWorld(POINT Grid);

	void GetRegionPoints(PathFindPoint& Point, POINT& RegionPoint, POINT& RegionBasePoint);
	RegionBuffer* GetRegion(POINT RegionPoint);
	bool IsPointChecked(PathFindPoint& Point); 
	RegionBufferEntry GetPointEntry(PathFindPoint& Point);
	void SetPointEntry(PathFindPoint& Point, RegionBufferEntry Entry);

	PathFindPoint ExtractPointWithLowestWeight(void);

	void TraceBack(PathFindPoint& Point, PathFindPoint& Start, vector<XMINT3>& Output);

	void DoDebugCallback(void);
public:
	bool FindPath(XMINT3 Start, XMINT3 Finish, vector<XMINT3>& Output, uint32_t& Weight, DebugCallbackFunc DebugCallback = NULL);

	vector<XMINT3> GetPointsToCheck(void);
	vector<XMINT3> GetCheckedPoints(void);
};