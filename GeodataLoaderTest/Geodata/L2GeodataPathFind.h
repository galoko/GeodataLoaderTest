#pragma once

#include <iostream>
#include <vector>
#include <unordered_map>
#include <DirectXMath.h>

#include "MathUtils.h"
#include "Geodata\L2Geodata.h"

using namespace std;
using namespace DirectX;

class L2GeodataPathFind;

typedef uint32_t (*DebugCallbackFunc)(L2GeodataPathFind& PathFind);

class L2GeodataPathFind {
private:
	struct PathFindPoint {
		uint32_t Weight;
		XMINT3 PrevPoint, ThisPoint;
		int NSWE;

		int16_t LayersCount;
		int16_t *Layers;

		PathFindPoint(XMINT3 StartPoint);
		PathFindPoint(const PathFindPoint& Prev, POINT Offset);

		void GetLayersFromGeodata(void);
		void FindNSWE(void);
		void SetSubBlock(int16_t SubBlock);

		bool operator==(const PathFindPoint& Other);

		int16_t GetSubBlock(void);
	};

	DebugCallbackFunc DebugCallback;

	vector<PathFindPoint> PointsToCheck, CheckedPoints;

	static void ToGrid(XMINT3& World);
	static void ToWorld(XMINT3& Grid);

	bool IsAlreadyChecked(const PathFindPoint& Point);

	void DoDebugCallback(void);
public:
	bool FindPath(XMINT3 Start, XMINT3 Finish, vector<XMINT3> Output, DebugCallbackFunc DebugCallback = NULL);

	vector<XMINT3> GetPointsToCheck(void);
	vector<XMINT3> GetCheckedPoints(void);
};