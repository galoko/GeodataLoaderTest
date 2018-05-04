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
	POINT RegionBasedPoint = { Point.GridX - RegionOffset.x, Point.GridY - RegionOffset.y };
	RegionPoint = { (int)floor((float)RegionBasedPoint.x / REGION_SIZE), (int)floor((float)RegionBasedPoint.y / REGION_SIZE) };
	RegionBasePoint = SubtractPoint(RegionBasedPoint, { RegionPoint.x * REGION_SIZE, RegionPoint.y * REGION_SIZE });
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
	POINT NoDirection = { 0, 0 };
	PathStart.CalcAllWeights(NoDirection, PathStart, PathFinish);

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

			POINT Direction = Directions[DirectionIndex];

			POINT NeighbourPoint = AddPoint({ Point.GridX, Point.GridY }, Direction);
			POINT NeighbourWorldPoint = ToWorld(NeighbourPoint);

			int16_t LayersCount;
			int16_t* Layers = L2Geodata::GetSubBlocks(NeighbourWorldPoint.x, NeighbourWorldPoint.y, LayersCount);

			int16_t DestLayerIndex;
			bool CanGo;
			CanGo = L2Geodata::GetDestLayerIndex(Point.SubBlock, Direction.x, Direction.y, Layers, LayersCount, DestLayerIndex);

			if (CanGo) {

				PathFindPoint Neighbour(NeighbourPoint.x, NeighbourPoint.y, DestLayerIndex, Layers[DestLayerIndex]);

				if (!IsPointChecked(Neighbour)) {

					POINT PointDirection = Directions[GetPointEntry(Point).DirectionIndex];

					Neighbour.CalcAllWeights(PointDirection, Point, PathFinish);

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

	return (uint32_t)round(sqrt((float)(DX * DX + DY * DY + DZ * DZ)));
}

uint32_t L2GeodataPathFind::PathFindPoint::GetDistManhattan(PathFindPoint& P1, PathFindPoint& P2)
{
	int32_t DX, DY, DZ;

	DX = P2.GridX - P1.GridX;
	DY = P2.GridY - P1.GridY;
	DZ = (GET_GEO_HEIGHT(P2.SubBlock) - GET_GEO_HEIGHT(P1.SubBlock)) / L2Geodata::HEIGHT_RESOLUTION;

	return abs(DX) + abs(DY) + abs(DZ);
}

uint32_t L2GeodataPathFind::PathFindPoint::GetDistManhattan2D(PathFindPoint & P1, PathFindPoint & P2)
{
	int32_t DX, DY;

	DX = P2.GridX - P1.GridX;
	DY = P2.GridY - P1.GridY;

	return abs(DX) + abs(DY);
}

uint32_t L2GeodataPathFind::PathFindPoint::CalcHeightWeight(PathFindPoint& From, PathFindPoint& To)
{
	int32_t HeightDiff;

	HeightDiff = abs(GET_GEO_HEIGHT(To.SubBlock) - GET_GEO_HEIGHT(From.SubBlock)) / L2Geodata::HEIGHT_RESOLUTION * HEIGHT_WEIGHT;

	return HeightDiff;
}

uint32_t L2GeodataPathFind::PathFindPoint::CalcDistanceWeight(POINT& PrevDirection, PathFindPoint& From, PathFindPoint& To)
{
	POINT CurrentDirection = { To.GridX - From.GridX, To.GridY - From.GridY };

	bool IsDiagonal = (abs(CurrentDirection.x) == abs(PrevDirection.y) && abs(CurrentDirection.y) == abs(PrevDirection.x));

	return (IsDiagonal ? DIAGONAL_ADDITIONAL_WEIGHT : STRAIGHT_WEIGHT) + CalcHeightWeight(From, To);
}

uint32_t L2GeodataPathFind::PathFindPoint::CalcNeighborsWeight(PathFindPoint& P)
{
	uint32_t Weight = 0;

	POINT GridOffset = { P.GridX - NEIGHBORS_REGION_SIZE / 2, P.GridY - NEIGHBORS_REGION_SIZE / 2 };

	NeighborsRegionBuffer Neighbors = { };
	NeighborsRegionQueue Queue = NeighborsRegionQueue();

	uint8_t PRegionBasedX = (uint8_t)(P.GridX - GridOffset.x);
	uint8_t PRegionBasedY = (uint8_t)(P.GridY - GridOffset.y);

	Queue.Push(PRegionBasedX, PRegionBasedY, (uint8_t)P.LayerIndex);
	Neighbors.SetPointCheched(PRegionBasedX, PRegionBasedY);

	int16_t SrcHeight = GET_GEO_HEIGHT(P.SubBlock);

	while (true) {

		uint8_t X, Y, LayerIndex;
		if (!Queue.Pop(X, Y, LayerIndex))
			break;

		POINT GridPoint = AddPoint({ X, Y }, GridOffset);

		POINT WorldPoint = ToWorld(GridPoint);

		int16_t LayersCount;
		int16_t* Layers = L2Geodata::GetSubBlocks(WorldPoint.x, WorldPoint.y, LayersCount);

		PathFindPoint P = PathFindPoint(GridPoint.x, GridPoint.y, (int16_t)LayerIndex, Layers[LayerIndex]);

		for (int DirectionIndex = 0; DirectionIndex < 4; DirectionIndex++) {

			POINT Direction = Directions[DirectionIndex];

			POINT NeighborPoint = AddPoint(GridPoint, Direction);

			// Neighbor is out of bound
			if (
				NeighborPoint.x < GridOffset.x || NeighborPoint.x >= GridOffset.x + NEIGHBORS_REGION_SIZE || 
				NeighborPoint.y < GridOffset.y || NeighborPoint.y >= GridOffset.y + NEIGHBORS_REGION_SIZE)
				continue;

			uint8_t NeighborRegionBasedX = (uint8_t)(NeighborPoint.x - GridOffset.x);
			uint8_t NeighborRegionBasedY = (uint8_t)(NeighborPoint.y - GridOffset.y);
			if (Neighbors.IsPointChecked(NeighborRegionBasedX, NeighborRegionBasedY))
				continue;

			POINT NeighborWorld = ToWorld(NeighborPoint);

			int16_t NeighborLayersCount;
			int16_t* NeighborLayers = L2Geodata::GetSubBlocks(NeighborWorld.x, NeighborWorld.y, NeighborLayersCount);

			int16_t DestLayerIndex;
			if (!L2Geodata::GetDestLayerIndex(P.SubBlock, Direction.x, Direction.y, NeighborLayers, NeighborLayersCount, DestLayerIndex))
				continue;

			PathFindPoint Neighbor = PathFindPoint(NeighborPoint.x, NeighborPoint.y, DestLayerIndex, NeighborLayers[DestLayerIndex]);

			uint32_t HeightWeight = CalcHeightWeight(P, Neighbor);
			uint32_t NeighborWallWeight = GET_GEO_NSWE(Neighbor.SubBlock) != L2Geodata::NSWE_ALL ? NEIGHBOR_WALL_WEIGHT : 0;

			uint32_t DistanceMul = MAX_NEIGHBOR_DIST - GetDistManhattan2D(P, Neighbor);

			Weight += (HeightWeight + NeighborWallWeight) * DistanceMul;

			Neighbors.SetPointCheched(NeighborRegionBasedX, NeighborRegionBasedY);

			Queue.Push(NeighborRegionBasedX, NeighborRegionBasedY, (uint8_t)DestLayerIndex);
		}
	}

	for (uint8_t X = 0; X < NEIGHBORS_REGION_SIZE; X++)
		for (uint8_t Y = 0; Y < NEIGHBORS_REGION_SIZE; Y++) {

			if (Neighbors.IsPointChecked(X, Y))
				continue;

			POINT NeighborPoint = AddPoint({ X, Y }, GridOffset);

			PathFindPoint Neighbor = PathFindPoint(NeighborPoint.x, NeighborPoint.y, 0, 0);

			uint32_t DistanceMul = MAX_NEIGHBOR_DIST - GetDistManhattan2D(P, Neighbor);

			Weight += NEIGHBOR_INACCESSIBLE_WEIGHT * DistanceMul;
		}

	return Weight;
}

uint32_t L2GeodataPathFind::PathFindPoint::CalcWeight(POINT& PrevDirection, PathFindPoint& From, PathFindPoint& To)
{
	uint32_t DistanceWeight = CalcDistanceWeight(PrevDirection, From, To);
	uint32_t NeighborsWeight = CalcNeighborsWeight(To);

	return DistanceWeight * 5 + NeighborsWeight;
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

void L2GeodataPathFind::PathFindPoint::CalcAllWeights(POINT& PrevDirection, PathFindPoint& Prev, PathFindPoint& Finish)
{
	Weight = Prev.Weight + CalcWeight(PrevDirection, Prev, *this);
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

// NeighborsRegionBuffer

#define GET_NEIGHBORS_REGION_BIT_INDEX (X * NEIGHBORS_REGION_SIZE + Y)

bool L2GeodataPathFind::NeighborsRegionBuffer::IsPointChecked(uint8_t X, uint8_t Y) {

	uint32_t BitIndex = GET_NEIGHBORS_REGION_BIT_INDEX;

	uint8_t Mask = 1 << (BitIndex % 8);

	return (Data[BitIndex / 8] & Mask) != 0;
}

void L2GeodataPathFind::NeighborsRegionBuffer::SetPointCheched(uint8_t X, uint8_t Y) {

	uint32_t BitIndex = GET_NEIGHBORS_REGION_BIT_INDEX;

	uint8_t Mask = 1 << (BitIndex % 8);

	Data[BitIndex / 8] |= Mask;
}

// NeighborsRegionQueue

L2GeodataPathFind::NeighborsRegionQueue::NeighborsRegionQueue(void)
{
	CurrentPushIndex = 0;
	CurrentPopIndex = 0;
}

void L2GeodataPathFind::NeighborsRegionQueue::Push(uint8_t X, uint8_t Y, uint8_t LayerIndex)
{
	if (CurrentPushIndex >= DATA_LEN)
		throw new runtime_error("NeighborsRegionQueue overflow");

	if (X >= NEIGHBORS_REGION_SIZE || Y >= NEIGHBORS_REGION_SIZE || LayerIndex >= L2Geodata::LAYERS_PER_SUBBLOCK_LIMIT)
		return;

	Data[CurrentPushIndex++] = { X, Y, LayerIndex };
}

bool L2GeodataPathFind::NeighborsRegionQueue::Pop(uint8_t& X, uint8_t& Y, uint8_t& LayerIndex)
{
	if (CurrentPopIndex >= CurrentPushIndex)
		return false;

	X = Data[CurrentPopIndex].X;
	Y = Data[CurrentPopIndex].Y;
	LayerIndex = Data[CurrentPopIndex].LayerIndex;
	CurrentPopIndex++;

	return true;
}