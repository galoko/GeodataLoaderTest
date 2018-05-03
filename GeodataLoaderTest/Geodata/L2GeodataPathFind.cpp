#include "stdafx.h"

#include "L2GeodataPathFind.h"

POINT L2GeodataPathFind::ToGrid(POINT World)
{
	return { World.x / L2Geodata::GEO_COORDS_IN_WORLD_COORDS, World.y / L2Geodata::GEO_COORDS_IN_WORLD_COORDS };
}

POINT L2GeodataPathFind::ToWorld(POINT Grid)
{
	return { Grid.x * L2Geodata::GEO_COORDS_IN_WORLD_COORDS, Grid.y * L2Geodata::GEO_COORDS_IN_WORLD_COORDS };
}

void L2GeodataPathFind::DoDebugCallback(void)
{
	if (DebugCallback) {
		uint32_t Delay = DebugCallback(*this);
		if (Delay > 0)
			Sleep(Delay);
	}
}

void L2GeodataPathFind::GetRegionPoints(PathFindPoint & Point, POINT& RegionPoint, POINT &RegionBasePoint)
{
	POINT OffsettedPoint = { Point.GridX - RegionOffset.x, Point.GridY - RegionOffset.y };
	RegionPoint = { (int)floor((float)OffsettedPoint.x / REGION_SIZE), (int)floor((float)OffsettedPoint.y / REGION_SIZE) };
	RegionBasePoint = SubtractPoint(OffsettedPoint, { RegionPoint.x * REGION_SIZE, RegionPoint.y * REGION_SIZE });
}

L2GeodataPathFind::RegionBuffer* L2GeodataPathFind::GetRegion(POINT RegionPoint)
{
	if (LastRegion != NULL && Equals(LastRegionPoint, RegionPoint))
		return LastRegion;

	for (RegionBuffer* Region : Regions) {

		if (Equals(Region->RegionPoint, RegionPoint)) {

			LastRegion = Region;
			LastRegionPoint = RegionPoint;

			return LastRegion;
		}
	}

	RegionBuffer* NewRegion = (RegionBuffer*)calloc(1, sizeof(RegionBuffer));
	NewRegion->RegionPoint = RegionPoint;

	Regions.push_back(NewRegion);

	LastRegion = NewRegion;
	LastRegionPoint = RegionPoint;

	return LastRegion;
}

bool L2GeodataPathFind::IsPointChecked(PathFindPoint& Point)
{
	return GetPointEntry(Point).IsChecked;
}

L2GeodataPathFind::RegionBufferEntry L2GeodataPathFind::GetPointEntry(PathFindPoint& Point)
{
	POINT RegionPoint, RegionRasePoint;
	GetRegionPoints(Point, RegionPoint, RegionRasePoint);
	RegionBuffer* Region = GetRegion(RegionPoint);

	return Region->GetPointEntry({ RegionRasePoint.x, RegionRasePoint.y, Point.LayerIndex });
}

void L2GeodataPathFind::SetPointEntry(PathFindPoint& Point, RegionBufferEntry Entry)
{
	POINT RegionPoint, RegionRasePoint;
	GetRegionPoints(Point, RegionPoint, RegionRasePoint);
	RegionBuffer* Region = GetRegion(RegionPoint);

	Region->SetPointEntry({ RegionRasePoint.x, RegionRasePoint.y, Point.LayerIndex }, Entry);
}

L2GeodataPathFind::PathFindPoint L2GeodataPathFind::ExtractPointWithLowestWeight(void)
{
	vector<PathFindPoint>::iterator Iterator = PointsToCheck.end() - 1;

	PathFindPoint Point = *Iterator;

	PointsToCheck.erase(Iterator);

	return Point;
}

static const POINT Directions[4] = {
	{  1,  0 },
	{ -1,  0 },
	{  0,  1 },
	{  0, -1 }
};

static const POINT ReversedDirections[4] = {
	{ -1,  0 },
	{  1,  0 },
	{  0, -1 },
	{  0,  1 }
};

void L2GeodataPathFind::TraceBack(PathFindPoint& Finish, PathFindPoint& Start, vector<XMINT3>& Output)
{
	Output.clear();

	Output.push_back(Finish.GetWorldPoint());

	PathFindPoint CurrentPoint = Finish;
	while (!(CurrentPoint == Start)) {

		RegionBufferEntry Entry = GetPointEntry(CurrentPoint);
		if (!Entry.IsChecked)
			throw new runtime_error("Traceback found unchecked point");

		CurrentPoint.ApplyEntry(Entry);

		Output.push_back(CurrentPoint.GetWorldPoint());
	}

	Output.push_back(Start.GetWorldPoint());
}

bool L2GeodataPathFind::FindPath(XMINT3 Start, XMINT3 Finish, vector<XMINT3>& Output, uint32_t& Weight, DebugCallbackFunc DebugCallback)
{
	this->DebugCallback = DebugCallback;

	PointsToCheck.clear();
	CheckedPoints.clear();

	// TODO free buffers at the end
	for (RegionBuffer* Region : Regions)
		free(Region);
	Regions.clear();

	POINT StartPoint = ToGrid({ Start.x, Start.y });
	POINT FinishPoint = ToGrid({ Finish.x, Finish.y });

	POINT MidPoint = GetMidPoint(StartPoint, FinishPoint);
	RegionOffset = { MidPoint.x - REGION_SIZE / 2, MidPoint.y - REGION_SIZE / 2 };

	PathFindPoint PathStart(StartPoint.x, StartPoint.y, Start.z);
	PathFindPoint PathFinish(FinishPoint.x, FinishPoint.y, Finish.z);
	PathStart.CalcAllWeights(PathStart, PathFinish);

	cout << "Start to finish heuristic weight: " << PathStart.HeuristicWeight << endl;

	PointsToCheck.push_back(PathStart);
	SetPointEntry(PathStart, { true, 0, 0 });

	DoDebugCallback();

	uint64_t DebugCounter = 0;
	uint64_t NextDebugCounter = DebugCounter + 1;

	uint32_t CheckedPointsIndex = 0;

	while (PointsToCheck.size() > 0) {

		PathFindPoint Point = ExtractPointWithLowestWeight();

		if (Point == PathFinish) {
			TraceBack(Point, PathStart, Output);
			Weight = Point.Weight;
			return true;
		}

		if (CheckedPoints.size() < CHECKED_POINTS_IN_LIST_LIMIT)
			CheckedPoints.push_back(Point.GetWorldPoint());
		else
			CheckedPoints[CheckedPointsIndex] = Point.GetWorldPoint();
		CheckedPointsIndex = (CheckedPointsIndex + 1) % CHECKED_POINTS_IN_LIST_LIMIT;

		for (uint8_t DirectionIndex = 0; DirectionIndex < 4; DirectionIndex++) {

			POINT Offset = Directions[DirectionIndex];

			POINT NeighbourPoint = AddPoint({ Point.GridX, Point.GridY }, Offset);
			POINT NeighbourWorldPoint = ToWorld(NeighbourPoint);

			int16_t LayersCount;
			int16_t* Layers = L2Geodata::GetSubBlocks(NeighbourWorldPoint.x, NeighbourWorldPoint.y, LayersCount);

			int16_t DestLayerIndex;
			bool CanGo;
			CanGo = L2Geodata::GetDestLayerIndex(Point.SubBlock, Offset.x, Offset.y, Layers, LayersCount, DestLayerIndex);

			if (CanGo) {

				PathFindPoint Neighbour(NeighbourPoint.x, NeighbourPoint.y, DestLayerIndex, Layers[DestLayerIndex]);

				if (!IsPointChecked(Neighbour)) {

					PathStart.CalcAllWeights(Point, PathFinish);

					vector<PathFindPoint>::iterator InsertionPoint;
					InsertionPoint = lower_bound(PointsToCheck.begin(), PointsToCheck.end(), Neighbour);

					PointsToCheck.insert(InsertionPoint, Neighbour);

					SetPointEntry(Neighbour, { true, DirectionIndex, (uint8_t)Point.LayerIndex });
				}
			}
		}
		
		DebugCounter++;
		if (DebugCounter == NextDebugCounter) {
			DoDebugCallback();

			NextDebugCounter += PointsToCheck.size() * 3 + 1;
		}

		// DoDebugCallback();
	}

	return false;
}

vector<XMINT3> L2GeodataPathFind::GetPointsToCheck(void)
{
	vector<XMINT3> Result;

	for (PathFindPoint& Point : PointsToCheck)
		Result.push_back(Point.GetWorldPoint());

	return Result;
}

vector<XMINT3> L2GeodataPathFind::GetCheckedPoints(void)
{
	vector<XMINT3> Result(CheckedPoints);
	return Result;
}

// PathFindPoint

L2GeodataPathFind::PathFindPoint::PathFindPoint(int32_t GridX, int32_t GridY, int16_t Height)
{
	Weight = 0;
	HeuristicWeight = 0;
	
	this->GridX = GridX;
	this->GridY = GridY;

	POINT WorldPoint = ToWorld({ GridX, GridY });
	if (!L2Geodata::GetGroundSubBlock(WorldPoint.x, WorldPoint.y, Height, SubBlock, LayerIndex))
		throw new runtime_error("Couldn't setup Z in PathFindPoint");
}

L2GeodataPathFind::PathFindPoint::PathFindPoint(int32_t GridX, int32_t GridY, int16_t LayerIndex, int16_t SubBlock)
{
	this->GridX = GridX;
	this->GridY = GridY;

	this->LayerIndex = LayerIndex;
	this->SubBlock = SubBlock;
}

uint32_t L2GeodataPathFind::PathFindPoint::GetDistEuclidean(PathFindPoint& P1, PathFindPoint& P2)
{
	int32_t DX, DY, DZ;

	DX = P2.GridX - P1.GridX;
	DY = P2.GridY - P1.GridY;
	DZ = (GET_GEO_HEIGHT(P2.SubBlock) - GET_GEO_HEIGHT(P1.SubBlock)) / L2Geodata::HEIGHT_RESOLUTION;
	DZ = 0;

	return (uint32_t)round(sqrt((float)(DX * DX + DY * DY + DZ * DZ)));
}

uint32_t L2GeodataPathFind::PathFindPoint::GetDistManhattan(PathFindPoint& P1, PathFindPoint& P2)
{
	int32_t DX, DY, DZ;

	DX = P2.GridX - P1.GridX;
	DY = P2.GridY - P1.GridY;
	DZ = (GET_GEO_HEIGHT(P2.SubBlock) - GET_GEO_HEIGHT(P1.SubBlock)) / L2Geodata::HEIGHT_RESOLUTION;
	DZ = 0;

	return abs(DX) + abs(DY) + abs(DZ);
}

uint32_t L2GeodataPathFind::PathFindPoint::CalcWeight(PathFindPoint& From, PathFindPoint& To)
{
	return 1;
}

uint32_t L2GeodataPathFind::PathFindPoint::CalcHeuristicWeight(PathFindPoint& From, PathFindPoint& To)
{
	// return GetDistManhattan(From, To);
	// return GetDistEuclidean(From, To);
	return 0;
}

XMINT3 L2GeodataPathFind::PathFindPoint::GetWorldPoint(void)
{
	POINT WorldPoint = ToWorld({ GridX, GridY });

	return { WorldPoint.x, WorldPoint.y, GET_GEO_HEIGHT(SubBlock) };
}

void L2GeodataPathFind::PathFindPoint::CalcAllWeights(PathFindPoint& Prev, PathFindPoint& Finish)
{
	Weight = Prev.Weight + CalcWeight(Prev, *this);
	HeuristicWeight = Weight + CalcHeuristicWeight(*this, Finish);
}

void L2GeodataPathFind::PathFindPoint::ApplyEntry(RegionBufferEntry Entry)
{
	POINT ReversedDirection = ReversedDirections[Entry.DirectionIndex];
	GridX += ReversedDirection.x;
	GridY += ReversedDirection.y;
	LayerIndex = Entry.PrevLayerIndex;

	POINT WorldPoint = ToWorld({ GridX, GridY });

	int16_t LayersCount;
	int16_t* Layers = L2Geodata::GetSubBlocks(WorldPoint.x, WorldPoint.y, LayersCount);
	if (LayerIndex >= LayersCount)
		throw new runtime_error("Prev layer index is out of bound");

	SubBlock = Layers[LayerIndex];
}

bool L2GeodataPathFind::PathFindPoint::operator==(const PathFindPoint& Other)
{
	return (Other.GridX == this->GridX && Other.GridY == this->GridY && Other.LayerIndex == this->LayerIndex);
}

bool L2GeodataPathFind::PathFindPoint::operator<(const PathFindPoint& Other)
{
	return HeuristicWeight >= Other.HeuristicWeight;
}

// RegionBuffer

#define GET_REGION_INDEX \
Point.z * REGION_SIZE * REGION_SIZE + \
Point.x * REGION_SIZE + \
Point.y

void L2GeodataPathFind::RegionBuffer::SetPointEntry(XMINT3 Point, RegionBufferEntry Entry)
{
	if (Point.x < 0 || Point.y < 0 || Point.z < 0 || Point.x >= REGION_SIZE || Point.y >= REGION_SIZE || Point.z >= L2Geodata::LAYERS_PER_SUBBLOCK_LIMIT)
		throw new runtime_error("RegionBuffer out of bound");

	uint32_t Index = GET_REGION_INDEX;

	Data[Index] = Entry;
}

L2GeodataPathFind::RegionBufferEntry L2GeodataPathFind::RegionBuffer::GetPointEntry(XMINT3 Point)
{
	if (Point.x < 0 || Point.y < 0 || Point.z < 0 || Point.x >= REGION_SIZE || Point.y >= REGION_SIZE || Point.z >= L2Geodata::LAYERS_PER_SUBBLOCK_LIMIT)
		throw new runtime_error("RegionBuffer out of bound");

	uint32_t Index = GET_REGION_INDEX;

	return Data[Index];
}