#include "stdafx.h"

#include "L2GeodataPathFind.h"

void L2GeodataPathFind::ToGrid(XMINT3& World)
{
	World.x /= L2Geodata::GEO_COORDS_IN_WORLD_COORDS;
	World.y /= L2Geodata::GEO_COORDS_IN_WORLD_COORDS;
	World.z /= L2Geodata::HEIGHT_RESOLUTION;
}

void L2GeodataPathFind::ToWorld(XMINT3& Grid)
{
	Grid.x *= L2Geodata::GEO_COORDS_IN_WORLD_COORDS;
	Grid.y *= L2Geodata::GEO_COORDS_IN_WORLD_COORDS;
	Grid.z *= L2Geodata::HEIGHT_RESOLUTION;
}

bool L2GeodataPathFind::IsAlreadyChecked(const PathFindPoint& Point)
{
	for (PathFindPoint& CheckedPoint : CheckedPoints)
		if (CheckedPoint == Point)
			return true;

	return false;
}

void L2GeodataPathFind::DoDebugCallback(void)
{
	if (DebugCallback) {
		uint32_t Delay = DebugCallback(*this);
		Sleep(Delay);
	}
}

bool L2GeodataPathFind::FindPath(XMINT3 Start, XMINT3 Finish, vector<XMINT3> Output, DebugCallbackFunc DebugCallback)
{
	this->DebugCallback = DebugCallback;

	PointsToCheck.clear();
	CheckedPoints.clear();

	ToGrid(Start);
	ToGrid(Finish);

	PathFindPoint PathStart(Start);
	PathFindPoint PathFinish(Finish);

	PointsToCheck.push_back(PathStart);

	DoDebugCallback();

	while (PointsToCheck.size() > 0) {
		
		vector<PathFindPoint>::iterator LastPointIterator = PointsToCheck.begin() + (PointsToCheck.size() - 1);
		PathFindPoint Point = *LastPointIterator;
		PointsToCheck.erase(LastPointIterator);

		if (Point == PathFinish) {
			return true;
		}

		CheckedPoints.push_back(Point);

		POINT Offset = { 1, 0 };
		for (int Counter = 0; Counter < 4; Counter++) {

			PathFindPoint NeighbourPoint(Point, Offset);

			int16_t DestSubBlock;
			bool CanGo;
			CanGo = L2Geodata::GetDestSubBlock(Point.GetSubBlock(), Offset.x, Offset.y, NeighbourPoint.Layers, NeighbourPoint.LayersCount,
				DestSubBlock);

			if (CanGo) {
				NeighbourPoint.SetSubBlock(DestSubBlock);

				if (!IsAlreadyChecked(NeighbourPoint)) {

					// TODO calculate weight by more complex means like checking if walls are near the point
					NeighbourPoint.Weight = Point.Weight + 1;

					PointsToCheck.push_back(NeighbourPoint);
				}
			}

			Offset = CrossProduct(Offset, 1);
		}

		DoDebugCallback();
	}

	return false;
}

vector<XMINT3> L2GeodataPathFind::GetPointsToCheck(void)
{
	vector<XMINT3> Result;

	for (PathFindPoint& Point : PointsToCheck) {

		XMINT3 GridPoint = Point.ThisPoint;
		ToWorld(GridPoint);

		// cout << "To check point: " << GridPoint.x << " " << GridPoint.y << " " << GridPoint.z << endl;

		Result.push_back(GridPoint);
	}

	return Result;
}

vector<XMINT3> L2GeodataPathFind::GetCheckedPoints(void)
{
	vector<XMINT3> Result;

	for (PathFindPoint& Point : CheckedPoints) {

		XMINT3 GridPoint = Point.ThisPoint;
		ToWorld(GridPoint);

		// cout << "Checked point: " << GridPoint.x << " " << GridPoint.y << " " << GridPoint.z << endl;

		Result.push_back(GridPoint);
	}

	return Result;
}

L2GeodataPathFind::PathFindPoint::PathFindPoint(XMINT3 StartPoint)
{
	Weight = 0;
	PrevPoint = StartPoint;
	ThisPoint = StartPoint;

	GetLayersFromGeodata();

	FindNSWE();
}

L2GeodataPathFind::PathFindPoint::PathFindPoint(const PathFindPoint& Prev, POINT Offset)
{
	PrevPoint = Prev.ThisPoint;
	ThisPoint = { PrevPoint.x + Offset.x, PrevPoint.y + Offset.y, 0 };

	GetLayersFromGeodata();
}

void L2GeodataPathFind::PathFindPoint::GetLayersFromGeodata(void)
{
	XMINT3 GeoPoint = ThisPoint;
	ToWorld(GeoPoint);

	Layers = L2Geodata::GetSubBlocks(GeoPoint.x, GeoPoint.y, LayersCount);
}

void L2GeodataPathFind::PathFindPoint::FindNSWE(void)
{
	XMINT3 GeoPoint = ThisPoint;
	ToWorld(GeoPoint);

	bool Found = false;
	for (int16_t LayerIndex = 0; LayerIndex < LayersCount; LayerIndex++) {

		int16_t SubBlock = Layers[LayerIndex];

		if (GET_GEO_HEIGHT(SubBlock) == GeoPoint.z) {
			NSWE = GET_GEO_NSWE(SubBlock);
			Found = true;
			break;
		}
	}

	if (!Found)
		throw new runtime_error("Couldn't find point in geodata");
}

void L2GeodataPathFind::PathFindPoint::SetSubBlock(int16_t SubBlock)
{
	NSWE = GET_GEO_NSWE(SubBlock);

	XMINT3 GeoPoint = ThisPoint;
	ToWorld(GeoPoint);
	GeoPoint.z = GET_GEO_HEIGHT(SubBlock);
	ToGrid(GeoPoint);

	ThisPoint = GeoPoint;
}

bool L2GeodataPathFind::PathFindPoint::operator==(const PathFindPoint& Other)
{
	return (Other.ThisPoint.x == this->ThisPoint.x && Other.ThisPoint.y == this->ThisPoint.y && Other.ThisPoint.z == this->ThisPoint.z);
}

int16_t L2GeodataPathFind::PathFindPoint::GetSubBlock(void)
{
	return MAKE_SUBBLOCK(ThisPoint.z * L2Geodata::HEIGHT_RESOLUTION, NSWE);
}
