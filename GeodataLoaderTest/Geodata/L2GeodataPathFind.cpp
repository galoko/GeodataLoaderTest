#include "stdafx.h"

#include "L2GeodataPathFind.h"

#include "TimeUtils.h"

#include "SimplexNoise.h"

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
	return;

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
	auto Iterator = prev(PointsToCheck.end());

	PathFindPoint Point = *Iterator;

	PointsToCheck.erase(Iterator);

	return Point;
}

bool L2GeodataPathFind::GetNextLinePoint(PathFindPoint& PrevPoint, POINT& Direction, PathFindPoint& NextPoint, bool IsDiagonal)
{
	if (Direction.x == 0 || Direction.y == 0) {

		// straight case

		POINT GridPoint = AddPoint({ PrevPoint.GridX, PrevPoint.GridY }, Direction);
		POINT WorldPoint = ToWorld(GridPoint);

		int16_t LayersCount;
		int16_t* Layers = L2Geodata::GetSubBlocks(WorldPoint.x, WorldPoint.y, LayersCount);

		int16_t DestLayerIndex;
		bool CanGo = L2Geodata::GetDestLayerIndex(PrevPoint.SubBlock, Direction.x, Direction.y, Layers, LayersCount, DestLayerIndex);
		if (!CanGo)
			return false;

		NextPoint = PathFindPoint(GridPoint.x, GridPoint.y, DestLayerIndex, Layers[DestLayerIndex]);
		NextPoint.Weight = PathFindPoint::CalcWeight(IsDiagonal, PrevPoint, NextPoint);

		return true;
	}
	else {

		// diagonal case

		POINT Direction0 = { Direction.x, 0 };
		POINT Direction1 = { 0, Direction.y };

		PathFindPoint NextPoint00, NextPoint01;
		bool HavePoint0 = GetNextLinePoint(PrevPoint, Direction0, NextPoint00, true) && GetNextLinePoint(NextPoint00, Direction1, NextPoint01, true);
		uint32_t Weight0 = NextPoint00.Weight + NextPoint01.Weight;

		PathFindPoint NextPoint10, NextPoint11;
		bool HavePoint1 = GetNextLinePoint(PrevPoint, Direction1, NextPoint10, true) && GetNextLinePoint(NextPoint10, Direction0, NextPoint11, true);
		uint32_t Weight1 = NextPoint10.Weight + NextPoint11.Weight;

		if (HavePoint0 && (HavePoint1 && Weight0 <= Weight1 || !HavePoint1)) {
			NextPoint = NextPoint01;
			NextPoint.Weight = Weight0;
		}
		else
			if (HavePoint1) {
				NextPoint = NextPoint11;
				NextPoint.Weight = Weight1;
			}
			else
				return false;

		return true;
	}

	return false;
}

bool L2GeodataPathFind::ConstructLineBetweenPoints(PathFindPoint& Start, PathFindPoint& Finish, vector<XMINT3>& LinePoints, float WeightThreshold)
{
	int32_t PixelsCount = max(abs(Finish.GridX - Start.GridX), abs(Finish.GridY - Start.GridY));

	PathFindPoint PrevPoint = Start;

	uint32_t MaxWeight = (uint32_t)((Finish.Weight - Start.Weight) / WeightThreshold) + 1;

	uint32_t Weight = 0;

	LinePoints.push_back(PrevPoint.GetWorldPoint());

	for (int32_t Counter = 1; Counter <= PixelsCount; Counter++) {

		int32_t GridX = (int32_t)round(Start.GridX + (Finish.GridX - Start.GridX) * Counter / (float)PixelsCount);
		int32_t GridY = (int32_t)round(Start.GridY + (Finish.GridY - Start.GridY) * Counter / (float)PixelsCount);

		POINT Direction = { GridX - PrevPoint.GridX, GridY - PrevPoint.GridY };

		assert(Direction.x != 0 || Direction.y != 0);

		PathFindPoint NextPoint;
		if (!GetNextLinePoint(PrevPoint, Direction, NextPoint, false))
			return false;

		Weight += NextPoint.Weight;
		if (Weight > MaxWeight)
			return false;

		if (Counter < PixelsCount) {
			LinePoints.push_back(NextPoint.GetWorldPoint());

			uint32_t Dist = PathFindPoint::GetDistManhattan2D(NextPoint, PrevPoint);
			if (Dist > 2)
				cout << "pidor " << Dist << endl;

			PrevPoint = NextPoint;
		}
	}

	return true;
}

uint32_t L2GeodataPathFind::ApplyLinearApproximation(vector<PathFindPoint>& Path, vector<vector<XMINT3>>& Points)
{
	if (Path.size() < 2)
		throw new runtime_error("Invalid points count as input in linear approximation");

	PathFindPoint* PrevLineLastPoint = NULL;
	vector<XMINT3> PrevLinePoints;
	bool HavePrevLine = false;

	Points.clear();

	PathFindPoint* CurrentPoint = &Path[Path.size() - 1];

	int Index = (int)Path.size() - 2;
	while (Index >= 0) {

		PathFindPoint* NextPoint = &Path[Index];

		vector<XMINT3> CurrentLinePoints;
		bool CanConstructALine = ConstructLineBetweenPoints(*CurrentPoint, *NextPoint, CurrentLinePoints, 0.9f);
		if (!CanConstructALine) {

			if (!HavePrevLine)
				throw new runtime_error("Couldn't construct a line between consecutive points");

			Points.push_back(PrevLinePoints);

			CurrentPoint = PrevLineLastPoint;
			HavePrevLine = false;
		}
		else {
			PrevLinePoints = CurrentLinePoints;

			PrevLineLastPoint = NextPoint;
			HavePrevLine = true;

			Index--;
		}
	}

	if (HavePrevLine)
		Points.push_back(PrevLinePoints);
	else
		Points.push_back({ CurrentPoint->GetWorldPoint() });

	Points[Points.size() - 1].push_back(Path[0].GetWorldPoint());

	return Path[0].Weight;
}

uint32_t L2GeodataPathFind::GetPathAsSingleLine(vector<PathFindPoint>& Path, vector<vector<XMINT3>>& Points)
{
	vector<XMINT3> Line;

	for (int Index = (int)Path.size() - 1; Index >= 0; Index--) 
		Line.push_back(Path[Index].GetWorldPoint());

	Points.push_back(Line);

	return Path[0].Weight;
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

void L2GeodataPathFind::TraceBack(PathFindPoint& Finish, PathFindPoint& Start, vector<PathFindPoint>& Output)
{
	Output.clear();

	Output.push_back(Finish);

	PathFindPoint CurrentPoint = Finish;
	while (!(CurrentPoint == Start)) {

		RegionBufferEntry Entry = GetPointEntry(CurrentPoint);
		if (!Entry.IsChecked)
			throw new runtime_error("Traceback found unchecked point");

		CurrentPoint = CurrentPoint.ApplyEntry(Entry);

		Output.push_back(CurrentPoint);
	}
}

void L2GeodataPathFind::RecalculateWeights(vector<PathFindPoint>& Path)
{
	PathFindPoint* StartPoint = &Path[(int)Path.size() - 1];
	PathFindPoint* FinishPoint = &Path[0];
		
	StartPoint->Weight = PathFindPoint::CalcWeight(false, *StartPoint, *FinishPoint);
	StartPoint->HeuristicWeight = 0;

	PathFindPoint* PrevPoint = StartPoint;
	POINT PrevDirection = Directions[0];

	for (int Index = (int)Path.size() - 2; Index >= 0; Index--) {

		PathFindPoint* CurrentPoint = &Path[Index];

		POINT CurrentDirection = { CurrentPoint->GridX - PrevPoint->GridX, CurrentPoint->GridY - PrevPoint->GridY };
		bool IsDiagonal = (abs(CurrentDirection.x) == abs(PrevDirection.y) && abs(CurrentDirection.y) == abs(PrevDirection.x));
		IsDiagonal = false;

		CurrentPoint->Weight = PrevPoint->Weight + PathFindPoint::CalcWeight(IsDiagonal, *PrevPoint, *CurrentPoint);
		CurrentPoint->HeuristicWeight = 0;

		PrevPoint = CurrentPoint;
		PrevDirection = CurrentDirection;
	}
}

bool L2GeodataPathFind::FindPath(XMINT3 Start, XMINT3 Finish, vector<vector<XMINT3>>& Output, uint32_t& Weight, DebugCallbackFunc DebugCallback)
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
	PathStart.CalcAllWeights(false, PathStart, PathFinish);

	cout << "Start to finish heuristic weight: " << PathStart.HeuristicWeight << endl;

	PointsToCheck.push_back(PathStart);
	SetPointEntry(PathStart, { true, 0, 0 });

	DoDebugCallback();

	uint64_t DebugCounter = 0;
	uint64_t NextDebugCounter = DebugCounter + 1;

	uint32_t CheckedPointsIndex = 0;

	while (!PointsToCheck.empty()) {

		PathFindPoint Point = ExtractPointWithLowestWeight();

		if (Point == PathFinish) {

			vector<PathFindPoint> Path;
			TraceBack(Point, PathStart, Path);

			RecalculateWeights(Path);

			Weight = ApplyLinearApproximation(Path, Output);
			// Weight = GetPathAsSingleLine(Path, Output);
				
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

					bool IsDiagonal = (abs(Direction.x) == abs(PointDirection.y) && abs(Direction.y) == abs(PointDirection.x));
					IsDiagonal = false;

					Neighbour.CalcAllWeights(IsDiagonal, Point, PathFinish);

					auto InsertionPoint = lower_bound(PointsToCheck.begin(), PointsToCheck.end(), Neighbour);

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

const static int NWC_GENEREATION_TASK_COUNT = 120;
const static int WIDTH_PER_TASK = (L2Geodata::GEO_WIDTH + NWC_GENEREATION_TASK_COUNT - 1) / NWC_GENEREATION_TASK_COUNT;

VOID L2GeodataPathFind::GenerateNeighborWeightCacheWorkCallback(PTP_CALLBACK_INSTANCE Instance, PVOID Context, PTP_WORK Work)
{
	int WorkNum = (int)(SIZE_T)Context;

	uint32_t StartX = WorkNum * WIDTH_PER_TASK;
	uint32_t EndX = min(StartX + WIDTH_PER_TASK, L2Geodata::GEO_WIDTH) - 1;

	for (uint32_t X = StartX; X <= EndX; X++)
		for (uint32_t Y = 0; Y < L2Geodata::GEO_HEIGHT; Y++) {

			int32_t WorldX, WorldY;

			if (!L2Geodata::GeoToWorld(X, Y, &WorldX, &WorldY))
				throw new runtime_error("Wrong Geo Coord in GenerateNeighborWeightCache work callback");

			int16_t LayersCount;
			int16_t* Layers = L2Geodata::GetSubBlocks(WorldX, WorldY, LayersCount);

			uint8_t WeightsCount = (uint8_t)LayersCount;
			uint8_t Weights[L2Geodata::LAYERS_PER_SUBBLOCK_LIMIT];

			for (int WeightIndex = 0; WeightIndex < WeightsCount; WeightIndex++) {

				POINT World = { WorldX, WorldY };
				POINT Grid = ToGrid(World);

				PathFindPoint P = PathFindPoint(Grid.x, Grid.y, WeightIndex, Layers[WeightIndex]);

				uint8_t Weight = (uint8_t)min(PathFindPoint::CalcNeighborsWeight(P), L2Geodata::SPECIAL_NEIGHBOR_WEIGHT_MULTILAYER - 1);

				Weights[WeightIndex] = Weight;
			}

			L2Geodata::SetNeighborWeights(WorldX, WorldY, WeightsCount, Weights);
		}
}

void L2GeodataPathFind::GenerateNeighborWeightCache(void)
{
	PTP_POOL Pool = CreateThreadpool(NULL);

	LONGLONG StartTime = GetTime();

	PTP_WORK Works[NWC_GENEREATION_TASK_COUNT];

	for (int WorkNum = 0; WorkNum < NWC_GENEREATION_TASK_COUNT; WorkNum++) {

		PTP_WORK Work = CreateThreadpoolWork(GenerateNeighborWeightCacheWorkCallback, (PVOID)(SIZE_T)WorkNum, NULL);
		if (Work == NULL)
			throw new runtime_error("Couldn't create model generation work");

		SubmitThreadpoolWork(Work);

		Works[WorkNum] = Work;
	}

	for (int WorkNum = 0; WorkNum < NWC_GENEREATION_TASK_COUNT; WorkNum++)
		WaitForThreadpoolWorkCallbacks(Works[WorkNum], false);
	
	CloseThreadpool(Pool);

	LONGLONG EndTime = GetTime();

	cout << "Neighbor Weight Cache generated for " << TimeToMs(EndTime - StartTime) << " ms" << endl;
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

uint32_t L2GeodataPathFind::PathFindPoint::GetDistEuclidean2D(PathFindPoint & P1, PathFindPoint & P2)
{
	int32_t DX, DY;

	DX = P2.GridX - P1.GridX;
	DY = P2.GridY - P1.GridY;

	return (uint32_t)round(sqrt((float)(DX * DX + DY * DY)));
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

uint32_t L2GeodataPathFind::PathFindPoint::CalcDistanceWeight(bool IsDiagonal, PathFindPoint& From, PathFindPoint& To)
{
	return (IsDiagonal ? DIAGONAL_HALF_WEIGHT : STRAIGHT_WEIGHT) + CalcHeightWeight(From, To);
}

uint32_t L2GeodataPathFind::PathFindPoint::CalcNeighborsWeight(PathFindPoint& StartPoint)
{
	POINT GridOffset = { StartPoint.GridX - NEIGHBORS_REGION_SIZE / 2, StartPoint.GridY - NEIGHBORS_REGION_SIZE / 2 };

	NeighborsRegionBuffer Neighbors = { };
	NeighborsRegionQueue Queue = NeighborsRegionQueue();

	uint8_t PRegionBasedX = (uint8_t)(StartPoint.GridX - GridOffset.x);
	uint8_t PRegionBasedY = (uint8_t)(StartPoint.GridY - GridOffset.y);

	Queue.Push(PRegionBasedX, PRegionBasedY, (uint8_t)StartPoint.LayerIndex);
	Neighbors.SetPointCheched(PRegionBasedX, PRegionBasedY);

	int16_t SrcHeight = GET_GEO_HEIGHT(StartPoint.SubBlock);

	uint32_t Weight = 0;

	while (true) {

		uint8_t X, Y, LayerIndex;
		if (!Queue.Pop(X, Y, LayerIndex))
			break;

		POINT GridPoint = AddPoint({ X, Y }, GridOffset);

		POINT WorldPoint = ToWorld(GridPoint);

		int16_t LayersCount;
		int16_t* Layers = L2Geodata::GetSubBlocks(WorldPoint.x, WorldPoint.y, LayersCount);

		PathFindPoint CurrentPoint = PathFindPoint(GridPoint.x, GridPoint.y, (int16_t)LayerIndex, Layers[LayerIndex]);

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
			if (!L2Geodata::GetDestLayerIndex(CurrentPoint.SubBlock, Direction.x, Direction.y, NeighborLayers, NeighborLayersCount, DestLayerIndex))
				continue;

			PathFindPoint Neighbor = PathFindPoint(NeighborPoint.x, NeighborPoint.y, DestLayerIndex, NeighborLayers[DestLayerIndex]);

			uint32_t HeightWeight = CalcHeightWeight(CurrentPoint, Neighbor);
			uint32_t NeighborWallWeight = GET_GEO_NSWE(Neighbor.SubBlock) != L2Geodata::NSWE_ALL ? NEIGHBOR_WALL_WEIGHT : 0;

			uint32_t DistanceMul = MAX_NEIGHBOR_DIST + 1 - GetDistManhattan2D(StartPoint, Neighbor);

			Weight += (HeightWeight * DistanceMul) + NeighborWallWeight * DistanceMul;

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

			uint32_t DistanceMul = MAX_NEIGHBOR_DIST + 1 - GetDistManhattan2D(StartPoint, Neighbor);

			Weight += NEIGHBOR_INACCESSIBLE_WEIGHT * DistanceMul;
		}

	Weight = (int)(Weight / 3 / (NEIGHBORS_REGION_SIZE * NEIGHBORS_REGION_SIZE));

	return Weight;
}

POINT L2GeodataPathFind::Offset;

#define USE_NWC true

uint32_t L2GeodataPathFind::PathFindPoint::GetNeighborsWeight(PathFindPoint& StartPoint)
{
	uint32_t Weight;
	
	if (USE_NWC) {
		POINT World = ToWorld({ StartPoint.GridX, StartPoint.GridY });

		uint8_t WeightsCount;
		uint8_t* Weights = L2Geodata::GetNeighborWeights(World.x, World.y, WeightsCount);

		if (StartPoint.LayerIndex >= WeightsCount)
			throw new runtime_error("Layer index out of bound (NWC)");

		Weight = Weights[StartPoint.LayerIndex];
	}
	else {
		Weight = CalcNeighborsWeight(StartPoint);
	}

	return Weight;

	uint32_t FinalWeight = Weight;

	static const float GridToNoiseScale = 0.01f;
	float t = SimplexNoise::noise((StartPoint.GridX + Offset.x) * GridToNoiseScale, (StartPoint.GridY + Offset.y) * GridToNoiseScale);

	t = (t + 1.0f) / 2.0f;

	static const float MaxScale = 3.0f;
	static const float MinScale = 1.0f / MaxScale;
	
	t = t * (MaxScale - MinScale) + MinScale;

	return (int32_t)(FinalWeight * t);
}

uint32_t L2GeodataPathFind::PathFindPoint::CalcWeight(bool IsDiagonal, PathFindPoint& From, PathFindPoint& To)
{
	uint32_t DistanceWeight = CalcDistanceWeight(IsDiagonal, From, To);
	uint32_t NeighborsWeight = GetNeighborsWeight(To);

	return DistanceWeight + NeighborsWeight;
}

uint32_t L2GeodataPathFind::PathFindPoint::CalcHeuristicWeight(PathFindPoint& From, PathFindPoint& To)
{
	// return GetDistManhattan2D(From, To) * STRAIGHT_WEIGHT + CalcHeightWeight(From, To);
	return GetDistEuclidean2D(From, To) * STRAIGHT_WEIGHT + CalcHeightWeight(From, To);
	// return 0;
}

XMINT3 L2GeodataPathFind::PathFindPoint::GetWorldPoint(void)
{
	POINT WorldPoint = ToWorld({ GridX, GridY });

	return { WorldPoint.x, WorldPoint.y, GET_GEO_HEIGHT(SubBlock) };
}

void L2GeodataPathFind::PathFindPoint::CalcAllWeights(bool IsDiagonal, PathFindPoint& Prev, PathFindPoint& Finish)
{
	Weight = Prev.Weight + CalcWeight(IsDiagonal, Prev, *this);
	HeuristicWeight = Weight + CalcHeuristicWeight(*this, Finish);
}

L2GeodataPathFind::PathFindPoint L2GeodataPathFind::PathFindPoint::ApplyEntry(RegionBufferEntry Entry)
{
	POINT ReversedDirection = ReversedDirections[Entry.DirectionIndex];
	int32_t NextGridX = GridX + ReversedDirection.x;
	int32_t NextGridY = GridY + ReversedDirection.y;

	POINT WorldPoint = ToWorld({ NextGridX, NextGridY });

	int16_t LayersCount;
	int16_t* Layers = L2Geodata::GetSubBlocks(WorldPoint.x, WorldPoint.y, LayersCount);
	if (Entry.PrevLayerIndex >= LayersCount)
		throw new runtime_error("Prev layer index is out of bound");

	return PathFindPoint(NextGridX, NextGridY, Entry.PrevLayerIndex, Layers[Entry.PrevLayerIndex]);
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