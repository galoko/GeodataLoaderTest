#include "stdafx.h"

#include "L2GeodataModelGenerator.h"

#include "MathUtils.h"

// Constructor

void L2GeodataModelGenerator::GenerateGeodataScene(int32_t WorldX, int32_t WorldY, uint32_t Width, uint32_t Height, 
	float ScaleWorld, float ScaleWorldZ, GeodataVertex* VertexBuffer, uint32_t& VertexBufferSize, uint32_t* IndexBuffer, uint32_t& IndexBufferSize)
{
	this->ScaleWorld = ScaleWorld;
	this->ScaleWorldZ = ScaleWorldZ;

	SetupOutputBuffers(VertexBuffer, VertexBufferSize, IndexBuffer, IndexBufferSize);

	SetupFloodFillStack(1000 * 1000);
	SetupPointUsageMap(400 + 1, 4096 + 1);
	SetupGenerationGrid(WorldX, WorldY, Width, Height);

	for (uint32_t GridX = 0; GridX < GridWidth; GridX++)
		for (uint32_t GridY = 0; GridY < GridHeight; GridY++) {

			GenerateTopPlanes(GridX, GridY);
			GenerateSidePlanes(GridX, GridY, 1, 0);
			GenerateSidePlanes(GridX, GridY, 0, 1);
		}

	FinalizeGenerationGrid();
	FinalizePointUsageMap();
	FinalizeFloodFillStack();

	// Setup output
	VertexBufferSize = NextVertexIndex;
	IndexBufferSize = NextIndexIndex;

	SetOutputBuffersToDefaultValues();
}

// Allocation

void L2GeodataModelGenerator::SetupOutputBuffers(GeodataVertex* VertexBuffer, uint32_t VertexBufferSize, uint32_t* IndexBuffer, uint32_t IndexBufferSize)
{
	this->VertexBuffer = VertexBuffer;
	this->VertexBufferSize = VertexBufferSize;
	this->IndexBuffer = IndexBuffer;
	this->IndexBufferSize = IndexBufferSize;
	
	this->NextVertexIndex = 0;
	this->NextIndexIndex = 0;
}

uint32_t L2GeodataModelGenerator::AllocateVertexIndex(void)
{
	if (NextVertexIndex >= VertexBufferSize)
		throw new runtime_error("Vertices limit is reached");

	uint32_t Ret = NextVertexIndex;
	NextVertexIndex++;

	return Ret;
}

uint32_t L2GeodataModelGenerator::AllocateIndexIndex(uint32_t Index)
{
	if (NextIndexIndex >= IndexBufferSize)
		throw new runtime_error("Indexes limit is reached");

	uint32_t Ret = NextIndexIndex;
	NextIndexIndex++;

	IndexBuffer[Ret] = Index;

	return Ret;
}

void L2GeodataModelGenerator::SetOutputBuffersToDefaultValues(void)
{
	VertexBuffer = NULL;
	VertexBufferSize = 0;
	NextVertexIndex = 0;

	IndexBuffer = NULL;
	IndexBufferSize = 0;
	NextIndexIndex = 0;
}

// Geodata utils

void L2GeodataModelGenerator::GetGeoLayers(int32_t GridX, int32_t GridY, int16_t& LayersCount, int16_t*& Layers)
{
	int32_t X = (GridSubBlockX + GridX) * L2Geodata::GEO_COORDS_IN_WORLD_COORDS;
	int32_t Y = (GridSubBlockY + GridY) * L2Geodata::GEO_COORDS_IN_WORLD_COORDS;

	Layers = L2Geodata::GetSubBlocks(X, Y, LayersCount);
}

void L2GeodataModelGenerator::GetHeightRanges(int GridX, int GridY, int OffsetX, int OffsetY, HeightRange *DestHeightRanges, int& DestHeightRangesCount)
{
	DestHeightRangesCount = 0;

	int16_t LayersCount, DestLayersCount;
	int16_t *Layers, *DestLayers;

	GetGeoLayers(GridX, GridY, LayersCount, Layers);
	GetGeoLayers(GridX + OffsetX, GridY + OffsetY, DestLayersCount, DestLayers);

	HeightRange HeightRanges[MAX_HEIGHT_RANGES_COUNT];
	int HeightRangesIndex = 0;

	for (int LayerIndex = 0; LayerIndex < LayersCount; LayerIndex++) {

		int16_t DestLayerIndex;

		if (!L2Geodata::GetWallLayerIndex(Layers[LayerIndex], OffsetX, OffsetY, DestLayers, DestLayersCount, DestLayerIndex))
			continue;

		int16_t Height = GET_GEO_HEIGHT(Layers[LayerIndex]);
		int16_t DestHeight = GET_GEO_HEIGHT(DestLayers[DestLayerIndex]);

		HeightRanges[HeightRangesIndex++] = HeightRange(Height, LayerIndex, DestHeight, DestLayerIndex);
		HeightRanges[HeightRangesIndex++] = HeightRange(DestHeight, DestLayerIndex, Height, LayerIndex);
	}

	for (int DestLayerIndex = 0; DestLayerIndex < DestLayersCount; DestLayerIndex++) {

		int16_t LayerIndex;

		if (!L2Geodata::GetWallLayerIndex(DestLayers[DestLayerIndex], -OffsetX, -OffsetY, Layers, LayersCount, LayerIndex))
			continue;

		int16_t Height = GET_GEO_HEIGHT(Layers[LayerIndex]);
		int16_t DestHeight = GET_GEO_HEIGHT(DestLayers[DestLayerIndex]);

		HeightRanges[HeightRangesIndex++] = HeightRange(DestHeight, DestLayerIndex, Height, LayerIndex);
		HeightRanges[HeightRangesIndex++] = HeightRange(Height, LayerIndex, DestHeight, DestLayerIndex);
	}

	if (HeightRangesIndex > 0) {
		sort(begin(HeightRanges), begin(HeightRanges) + HeightRangesIndex);

		HeightRange* CurrentRange = &DestHeightRanges[DestHeightRangesCount++];
		*CurrentRange = HeightRanges[0];

		for (int Index = 1; Index < HeightRangesIndex; Index++) {

			HeightRange* NextRange = &HeightRanges[Index];
			if (CurrentRange->SortedOverlapTestWith(NextRange)) {

				if (CurrentRange->IsLayersCompatible(NextRange))
					CurrentRange->MergeWith(NextRange);
				else {

					if (CurrentRange->HaveTopLayer())
						NextRange->CutTo(CurrentRange, false);
					else
						CurrentRange->CutTo(NextRange, true);

					CurrentRange = &DestHeightRanges[DestHeightRangesCount++];
					*CurrentRange = *NextRange;
				}
			}
			else {
				CurrentRange = &DestHeightRanges[DestHeightRangesCount++];
				*CurrentRange = *NextRange;
			}
		}
	}
}

// Utils forward declaration

int GetGridZ(int16_t Height);
int16_t GridZToHeight(int GridZ);

// Model generation utils

bool L2GeodataModelGenerator::GenerateConcaveHull(vector<Point>& Dest, int StartX, int StartY, int TurnDirection)
{
	POINT FirstPoint = { StartX, StartY };

	SetPointUsageCount(FirstPoint.x, FirstPoint.y, 0);
	Dest.push_back({ FirstPoint.x, FirstPoint.y });

	POINT CurrentPoint = FirstPoint;
	POINT Direction = { 1, 0 };

	while (true) {

		POINT NextPoint = AddPoint(CurrentPoint, Direction);

		if (Equals(NextPoint, FirstPoint))
			break;

		uint8_t NextValue = GetPointUsage(NextPoint.x, NextPoint.y).UsageCount;
		if (NextValue == 1) {

			CurrentPoint = NextPoint;
			SetPointUsageCount(CurrentPoint.x, CurrentPoint.y, 0);

			// turn right
			Direction = CrossProduct(Direction, TurnDirection);

			Dest.push_back({ CurrentPoint.x, CurrentPoint.y });
		}
		else
			if (NextValue == 2) {

				// diagonal case
				if (
					IsCellInUsageBound(NextPoint.x - 1, NextPoint.y - 1) &&
					(GetPointUsage(NextPoint.x, NextPoint.y).IsSet == GetPointUsage(NextPoint.x - 1, NextPoint.y - 1).IsSet)
					) {

					CurrentPoint = NextPoint;
					SetPointUsageCount(CurrentPoint.x, CurrentPoint.y, 1);

					// turn right
					Direction = CrossProduct(Direction, TurnDirection);

					Dest.push_back({ CurrentPoint.x, CurrentPoint.y });
				}
				else {
					// skip colinear point

					CurrentPoint = NextPoint;
					SetPointUsageCount(CurrentPoint.x, CurrentPoint.y, 0);
				}
			}
			else
				if (NextValue == 3) {

					CurrentPoint = NextPoint;
					SetPointUsageCount(CurrentPoint.x, CurrentPoint.y, 0);

					// turn left
					Direction = CrossProduct(Direction, -TurnDirection);

					Dest.push_back({ CurrentPoint.x, CurrentPoint.y });
				}
				else
					throw new runtime_error("Unexpected value in flood fill");
	}

	return true;
}

bool L2GeodataModelGenerator::GenerateConcaveHullAndHolesFromUsageMap(void)
{
	SeparatedPoints.clear();
	Points.clear();

	POINT FirstPoint;
	if (!FindPointByUsageCount(1, FirstPoint))
		return true;

	Hull.clear();
	if (!GenerateConcaveHull(Hull, FirstPoint.x, FirstPoint.y, 1))
		return false;

	SeparatedPoints.push_back(Hull);
	Points.insert(Points.end(), Hull.begin(), Hull.end());

	while (true) {

		POINT FirstPoint;

		if (!FindPointByUsageCount(3, FirstPoint))
			break;

		vector<Point> Hole;
		if (!GenerateConcaveHull(Hole, FirstPoint.x, FirstPoint.y, -1))
			return false;

		SeparatedPoints.push_back(Hole);
		Points.insert(Points.end(), Hole.begin(), Hole.end());
	}

	return true;
}

bool L2GeodataModelGenerator::GenerateModelFromUsageMap(void)
{
	if (!GenerateConcaveHullAndHolesFromUsageMap()) {
		assert(false);
		return false;
	}

	Indices = mapbox::earcut<uint32_t>(SeparatedPoints);

	return true;
}

void L2GeodataModelGenerator::AddTopPlaneModel(int16_t SubBlock, int Direction, int16_t LayerIndex)
{
	int NSWE = GET_GEO_NSWE(SubBlock);
	float TexOffsetY = (float)(15 - NSWE);

	uint32_t VertexOffset = NextVertexIndex;
	for (int Index = 0; Index < Points.size(); Index++) {

		Point P = Points[Index];

		uint32_t VertexIndex = AllocateVertexIndex();

		GeodataVertex *Vertex = &VertexBuffer[VertexIndex];

		ToFloat(GridSubBlockX + P[0], GridSubBlockY + P[1], GET_GEO_HEIGHT(SubBlock), Vertex->Pos.x, Vertex->Pos.y, Vertex->Pos.z);

		Vertex->Normal = { 0, 0, (float)Direction };
		if (LayerIndex == 0 && Direction == 1)
			Vertex->Tex = { (float)P[0] / (float)GridWidth, (float)P[1] / (float)GridHeight };
		else
			Vertex->Tex = { -1, 0 };
		Vertex->NSWE = { TexOffsetY };
	}

	if (Direction == 1) {
		for (int Index = 0; Index < Indices.size(); Index++)
			AllocateIndexIndex(VertexOffset + Indices[Index]);
	}
	else {
		for (int Index = (int)Indices.size() - 1; Index >= 0; Index--)
			AllocateIndexIndex(VertexOffset + Indices[Index]);
	}
}

void L2GeodataModelGenerator::GenerateTopPlanes(int GridX, int GridY)
{
	int16_t LayersCount;
	int16_t *Layers;

	GetGeoLayers(GridX, GridY, LayersCount, Layers);
	for (int LayerIndex = 0; LayerIndex < LayersCount; LayerIndex++) {

		if (GetGridUsage(GridX, GridY, 0, LayerIndex))
			continue;

		int16_t SubBlock = Layers[LayerIndex];

		ResetPointUsageMap(GridX, GridY, true, true);
		ResetFloodFillStack();

		POINT CurrentPoint = { GridX, GridY };
		int16_t LayerIndexToUse = LayerIndex;

		while (true) {

			// should not normally happen
			if (IsInGridBound(CurrentPoint.x, CurrentPoint.y) && IsCellInUsageBound(CurrentPoint.x, CurrentPoint.y)) {

				if (LayerIndexToUse == -1) {

					int16_t CurrentLayersCount;
					int16_t *CurrentLayers;
					GetGeoLayers(CurrentPoint.x, CurrentPoint.y, CurrentLayersCount, CurrentLayers);
					for (int CurrentLayerIndex = 0; CurrentLayerIndex < CurrentLayersCount; CurrentLayerIndex++) {

						if (GetGridUsage(CurrentPoint.x, CurrentPoint.y, 0, CurrentLayerIndex))
							continue;

						if (CurrentLayers[CurrentLayerIndex] != SubBlock)
							continue;

						LayerIndexToUse = CurrentLayerIndex;
						break;
					}
				}
			}
			else
				LayerIndexToUse = -1;

			if (TOP_LAYER_COMPLIANCE && (LayerIndex == 0 || LayerIndexToUse == 0) && LayerIndexToUse != LayerIndex)
				LayerIndexToUse = -1;

			if (LayerIndexToUse != -1) {

				SetGridSideUsage(CurrentPoint.x, CurrentPoint.y, 0, LayerIndexToUse);

				ApplyGridCellToPointUsageMap(CurrentPoint.x, CurrentPoint.y);

				PushGridNeighbors(CurrentPoint.x, CurrentPoint.y);

				LayerIndexToUse = -1;
			}

			if (!PopStackPoint(CurrentPoint))
				break;
		}

		if (!GenerateModelFromUsageMap())
			throw new runtime_error("Couldn't generate model for top plane");

		AddTopPlaneModel(SubBlock,  1, LayerIndex);
		AddTopPlaneModel(SubBlock, -1, LayerIndex);
	}
}

void L2GeodataModelGenerator::GenerateSidePlanes(int GridX, int GridY, int OffsetX, int OffsetY)
{
	assert((OffsetX ^ OffsetY) == 1);

	POINT Direction = { OffsetY, OffsetX };
	bool DynamicX = Direction.x != 0;
	int Side = OffsetX + 2 * OffsetY;

	HeightRange StartRanges[MAX_HEIGHT_RANGES_COUNT];
	int StartRangesCount;

	GetHeightRanges(GridX, GridY, OffsetX, OffsetY, StartRanges, StartRangesCount);
	if (StartRangesCount == 0)
		return;

	for (int16_t StartHeightIndex = 0; StartHeightIndex < StartRangesCount; StartHeightIndex++) {

		HeightRange *StartRange = &StartRanges[StartHeightIndex];

		int PlaneDirection = StartRange->Direction;
		POINT CurrentPoint = { GridX, GridY };

		ResetPointUsageMap(DynamicX ? CurrentPoint.x : CurrentPoint.y, GetGridZ(StartRange->GetMeanHeight()), true, true);
		ResetFloodFillStack();

		PushGridCell(DynamicX ? CurrentPoint.x : CurrentPoint.y, StartHeightIndex);

		while (true) {

			POINT P;
			if (!PopStackPoint(P))
				break;

			CurrentPoint = { DynamicX ? P.x : GridX, !DynamicX ? P.x : GridY };
			int16_t HeightIndex = (int16_t)P.y;
			assert(HeightIndex >= 0 && HeightIndex < MAX_HEIGHT_RANGES_COUNT);

			if (!IsInGridBound(CurrentPoint.x, CurrentPoint.y))
				continue;

			if (GetGridUsage(CurrentPoint.x, CurrentPoint.y, Side, HeightIndex))
				continue;

			HeightRange CurrentRanges[MAX_HEIGHT_RANGES_COUNT];
			int CurrentRangesCount;

			GetHeightRanges(CurrentPoint.x, CurrentPoint.y, OffsetX, OffsetY, CurrentRanges, CurrentRangesCount);

			HeightRange* CurrentRange = &CurrentRanges[HeightIndex];

			if (ApplyHeightRangeToPointUsageMap(DynamicX ? CurrentPoint.x : CurrentPoint.y, CurrentRange)) {
				SetGridSideUsage(CurrentPoint.x, CurrentPoint.y, Side, HeightIndex);
				PushHeightRangeNeighborsBothSides(CurrentPoint.x, CurrentPoint.y, OffsetX, OffsetY, Direction, CurrentRange);
			}
		}

		if (!GenerateModelFromUsageMap())
			throw new runtime_error("Couldn't generate model for side plane");

		uint32_t VertexOffset = NextVertexIndex;
		for (int Index = 0; Index < Points.size(); Index++) {

			Point P = Points[Index];

			uint32_t VertexIndex = AllocateVertexIndex();

			GeodataVertex *Vertex = &VertexBuffer[VertexIndex];

			ToFloat(GridSubBlockX + (DynamicX ? P[0] : GridX + 1), GridSubBlockY + (!DynamicX ? P[0] : GridY + 1), GridZToHeight(P[1]),
				Vertex->Pos.x, Vertex->Pos.y, Vertex->Pos.z);
			
			Vertex->Normal = { (float)(OffsetX * -PlaneDirection), (float)(OffsetY * -PlaneDirection), 0 };
			if (StartRange->TopLayerIndex == 0 && StartRange->LowestLayerIndex == 0)
				Vertex->Tex = { (float)(DynamicX ? P[0] : GridX + 1) / (float)GridWidth, (float)(!DynamicX ? P[0] : GridY + 1) / (float)GridHeight };
			else
				Vertex->Tex = { -1, 0 };
			Vertex->NSWE = { -1 };
		}

		if (PlaneDirection == (DynamicX ? 1 : -1)) {
			for (int Index = 0; Index < Indices.size(); Index++)
				AllocateIndexIndex(VertexOffset + Indices[Index]);
		}
		else {
			for (int Index = (int)Indices.size() - 1; Index >= 0; Index--)
				AllocateIndexIndex(VertexOffset + Indices[Index]);
		}
	}
}

// Int to float conversion utils

void L2GeodataModelGenerator::ToFloat(int X, int Y, int Z, float& FX, float& FY, float& FZ)
{
	FX = (float)X * ScaleWorld;
	FY = (float)Y * ScaleWorld;
	FZ = (float)Z * ScaleWorldZ;
}

// Generation Grid

void L2GeodataModelGenerator::SetupGenerationGrid(int32_t WorldX, int32_t WorldY, uint32_t Width, uint32_t Height)
{
	GridSubBlockX = WorldX / L2Geodata::GEO_COORDS_IN_WORLD_COORDS;
	GridSubBlockY = WorldY / L2Geodata::GEO_COORDS_IN_WORLD_COORDS;

	GridWidth = Width / L2Geodata::GEO_COORDS_IN_WORLD_COORDS;
	GridHeight = Height / L2Geodata::GEO_COORDS_IN_WORLD_COORDS;

	GridUsageMap = (uint8_t*)calloc((GridWidth * GridHeight * 3 * MAX_HEIGHT_RANGES_COUNT + 7) / 8, 1);
}

bool L2GeodataModelGenerator::IsInGridBound(int GridX, int GridY)
{
	return (GridX >= 0 && GridX < (int32_t)GridWidth && GridY >= 0 && GridY < (int32_t)GridHeight);
}

#define GET_GRID_BIT_INDEX \
GridX * GridHeight * 3 * MAX_HEIGHT_RANGES_COUNT + \
GridY * 3 * MAX_HEIGHT_RANGES_COUNT + \
Side * MAX_HEIGHT_RANGES_COUNT + \
LayerOrHeightIndex

bool L2GeodataModelGenerator::GetGridUsage(int GridX, int GridY, int Side, int16_t LayerOrHeightIndex)
{
	assert(IsInGridBound(GridX, GridY));

	int Index = GET_GRID_BIT_INDEX;

	return (GridUsageMap[Index / 8] >> (Index % 8)) & 1;
}

void L2GeodataModelGenerator::SetGridSideUsage(int GridX, int GridY, int Side, int16_t LayerOrHeightIndex)
{
	assert(IsInGridBound(GridX, GridY));

	int Index = GET_GRID_BIT_INDEX;

	GridUsageMap[Index / 8] |= 1 << (Index % 8);
}

void L2GeodataModelGenerator::FinalizeGenerationGrid(void)
{
	free(GridUsageMap);
	GridUsageMap = NULL;

	GridSubBlockX = 0;
	GridSubBlockY = 0;

	GridWidth = 0;
	GridHeight = 0;
}

// Point Usage

void L2GeodataModelGenerator::SetupPointUsageMap(int MapWidth, int MapHeight)
{
	PointUsageMapWidth = MapWidth * 2;
	PointUsageMapHeight = MapHeight * 2;
	PointUsageMapGridX = 0;
	PointUsageMapGridY = 0;

	PointUsageMap = (UsageInfo*)calloc(PointUsageMapWidth * PointUsageMapHeight, sizeof(UsageInfo));

	PointUsageMapUsedBoundBox = { MAXLONG32, MAXLONG32, MINLONG32, MINLONG32 };
}

#define GET_USAGE_MAP_INDEX \
MapX * PointUsageMapHeight + MapY;

void L2GeodataModelGenerator::ResetPointUsageMap(int CenterGridX, int CenterGridY, bool CenterX, bool CenterY)
{
	for (int GridX = PointUsageMapUsedBoundBox.left; GridX < PointUsageMapUsedBoundBox.right; GridX++) {

		int MapX = GridX - PointUsageMapGridX;
		int MapY;

		MapY = PointUsageMapUsedBoundBox.top - PointUsageMapGridY;
		int StartIndex = GET_USAGE_MAP_INDEX;

		MapY = PointUsageMapUsedBoundBox.bottom - PointUsageMapGridY;
		int EndIndex = GET_USAGE_MAP_INDEX;

		memset(&PointUsageMap[StartIndex], 0, (EndIndex - StartIndex) * sizeof(UsageInfo));
	}

	PointUsageMapGridX = CenterX ? CenterGridX - (PointUsageMapWidth / 2) : CenterGridX;
	PointUsageMapGridY = CenterY ? CenterGridY - (PointUsageMapHeight / 2) : CenterGridY;

	PointUsageMapUsedBoundBox = { MAXLONG32, MAXLONG32, MINLONG32, MINLONG32 };
}

void L2GeodataModelGenerator::ApplyGridCellToPointUsageMap(int GridX, int GridY)
{
	assert(IsCellInUsageBound(GridX, GridY));

	int MapX = GridX - PointUsageMapGridX;
	int MapY = GridY - PointUsageMapGridY;
	int Index = GET_USAGE_MAP_INDEX;

	PointUsageMap[Index].IsSet = true;

	for (int OffsetX = 0; OffsetX <= 1; OffsetX++)
		for (int OffsetY = 0; OffsetY <= 1; OffsetY++) {

			int MapX = (GridX + OffsetX) - PointUsageMapGridX;
			int MapY = (GridY + OffsetY) - PointUsageMapGridY;
			int Index = GET_USAGE_MAP_INDEX;

			UsageInfo *Info = &PointUsageMap[Index];
			if (Info->UsageCount >= 4)
				throw new runtime_error("Too much point usage");

			Info->UsageCount++;
		}

	// update bounding box
	PointUsageMapUsedBoundBox.left = min(PointUsageMapUsedBoundBox.left, GridX);
	PointUsageMapUsedBoundBox.top = min(PointUsageMapUsedBoundBox.top, GridY);
	PointUsageMapUsedBoundBox.right = max(PointUsageMapUsedBoundBox.right, GridX + 1 + 1);
	PointUsageMapUsedBoundBox.bottom = max(PointUsageMapUsedBoundBox.bottom, GridY + 1 + 1);
}

bool L2GeodataModelGenerator::ApplyHeightRangeToPointUsageMap(int GridX, HeightRange* Range)
{
	int FirstGridZ = GetGridZ(Range->Start);
	int LastGridZ = GetGridZ(Range->End) - 1;

	if (!IsCellInUsageBound(GridX, FirstGridZ) || !IsCellInUsageBound(GridX, LastGridZ))
		return false;

	for (int GridZ = FirstGridZ; GridZ <= LastGridZ; GridZ++)
		if (IsCellInUsageBound(GridX, GridZ))
			ApplyGridCellToPointUsageMap(GridX, GridZ);
		else
			throw new runtime_error("ApplyHeightRangeToPointUsageMap out of bound");

	return true;
}

L2GeodataModelGenerator::UsageInfo L2GeodataModelGenerator::GetPointUsage(int GridX, int GridY)
{
	assert(IsPointInUsageBound(GridX, GridY));

	int MapX = GridX - PointUsageMapGridX;
	int MapY = GridY - PointUsageMapGridY;
	int Index = GET_USAGE_MAP_INDEX;

	return PointUsageMap[Index];
}

void L2GeodataModelGenerator::SetPointUsageCount(int GridX, int GridY, uint8_t UsageCount)
{
	assert(IsPointInUsageBound(GridX, GridY));
	assert(UsageCount <= 1);

	int MapX = GridX - PointUsageMapGridX;
	int MapY = GridY - PointUsageMapGridY;
	int Index = GET_USAGE_MAP_INDEX;

	PointUsageMap[Index].UsageCount = UsageCount;
}

bool L2GeodataModelGenerator::IsCellInUsageBound(int GridX, int GridY)
{
	int MapX = GridX - PointUsageMapGridX;
	int MapY = GridY - PointUsageMapGridY;

	return (MapX >= 0 && MapX + 1 < (int32_t)PointUsageMapWidth && MapY >= 0 && MapY + 1 < (int32_t)PointUsageMapHeight);
}

bool L2GeodataModelGenerator::IsPointInUsageBound(int GridX, int GridY)
{
	int MapX = GridX - PointUsageMapGridX;
	int MapY = GridY - PointUsageMapGridY;

	return (MapX >= 0 && MapX < (int32_t)PointUsageMapWidth && MapY >= 0 && MapY < (int32_t)PointUsageMapHeight);
}

bool L2GeodataModelGenerator::FindPointByUsageCount(uint8_t UsageCount, POINT& Point)
{
	for (int GridX = PointUsageMapUsedBoundBox.left; GridX < PointUsageMapUsedBoundBox.right; GridX++)
		for (int GridY = PointUsageMapUsedBoundBox.top; GridY < PointUsageMapUsedBoundBox.bottom; GridY++)
			if (GetPointUsage(GridX, GridY).UsageCount == UsageCount) {
				Point = { GridX, GridY };
				return true;
			}

	return false;
}

void L2GeodataModelGenerator::FinalizePointUsageMap(void)
{
	free(PointUsageMap);
	PointUsageMap = NULL;

	PointUsageMapWidth = 0;
	PointUsageMapHeight = 0;

	PointUsageMapGridX = 0;
	PointUsageMapGridY = 0;
}

// Flood Fill

void L2GeodataModelGenerator::SetupFloodFillStack(int StackSize)
{
	FloodFillStackSize = StackSize;
	FloodFillStack = (POINT*)malloc(FloodFillStackSize * sizeof(POINT));
	FloodFillStackIndex = -1;
}

void L2GeodataModelGenerator::ResetFloodFillStack(void)
{
	FloodFillStackIndex = -1;
}

void L2GeodataModelGenerator::PushGridCell(int GridX, int GridY)
{
	if ((uint32_t)(FloodFillStackIndex + 1) >= FloodFillStackSize)
		throw new runtime_error("Stack overflow");

	FloodFillStackIndex++;
	FloodFillStack[FloodFillStackIndex] = { GridX, GridY };
}

void L2GeodataModelGenerator::PushGridNeighbors(int GridX, int GridY)
{
	PushGridCell(GridX + 1, GridY + 0);
	PushGridCell(GridX - 1, GridY + 0);
	PushGridCell(GridX + 0, GridY + 1);
	PushGridCell(GridX + 0, GridY - 1);
}

void L2GeodataModelGenerator::PushHeightRangeNeighborsOneSide(int GridX, int GridY, int OffsetX, int OffsetY, POINT Direction, HeightRange* Range)
{
	bool DynamicX = Direction.x != 0;

	POINT NeighborPoint = AddPoint({ GridX, GridY }, Direction);

	HeightRange NeighboRanges[MAX_HEIGHT_RANGES_COUNT];
	int NeighboRangesCount;

	GetHeightRanges(NeighborPoint.x, NeighborPoint.y, OffsetX, OffsetY, NeighboRanges, NeighboRangesCount);

	for (int NeighborHeightIndex = 0; NeighborHeightIndex < NeighboRangesCount; NeighborHeightIndex++) {

		HeightRange* NeighborRange = &NeighboRanges[NeighborHeightIndex];

		if (Range->OverlapTestWith(NeighborRange))
			PushGridCell(DynamicX ? NeighborPoint.x : NeighborPoint.y, NeighborHeightIndex);
	}
}

void L2GeodataModelGenerator::PushHeightRangeNeighborsBothSides(int GridX, int GridY, int OffsetX, int OffsetY, POINT Direction, HeightRange* Range)
{
	PushHeightRangeNeighborsOneSide(GridX, GridY, OffsetX, OffsetY, Direction, Range);
	PushHeightRangeNeighborsOneSide(GridX, GridY, OffsetX, OffsetY, NegatePoint(Direction), Range);
}

bool L2GeodataModelGenerator::PopStackPoint(POINT & Point)
{
	if (FloodFillStackIndex < 0)
		return false;

	Point = FloodFillStack[FloodFillStackIndex];
	FloodFillStackIndex--;

	return true;
}

void L2GeodataModelGenerator::FinalizeFloodFillStack(void)
{
	free(FloodFillStack);
	FloodFillStack = NULL;

	FloodFillStackSize = 0;
	FloodFillStackIndex = -1;
}

// Other

void L2GeodataModelGenerator::GenerateNSWETexture(uint32_t* Pixels, int32_t Width, int32_t Height)
{
	memset(Pixels, 0, Height * Width * sizeof(uint32_t));

	if (Width < 3)
		return;

	uint32_t Alpha = (0xFF * Width / NSWE_TEX_WIDTH) << 24;
	uint32_t Green = 0x00AA00 | Alpha;
	uint32_t Red = 0xFF0000 | Alpha;

	for (int X = 0; X < Width; X++)
		for (int Y = 0; Y < Height; Y++) {

			int CellX = X % Width;
			int CellY = Y % Width;

			if (CellX == 0 || CellX == Width - 1 || CellY == 0 || CellY == Width - 1)
				Pixels[Y * Width + X] = Green;
		}

	for (int NSWE = 0; NSWE <= 15; NSWE++) {

		int Y = (15 - NSWE) * Width;

		if (!TEST_NSWE(NSWE, L2Geodata::EAST)) {

			int X = Width - 1;

			for (int OffsetY = 1; OffsetY < Width - 1; OffsetY++)
				Pixels[(Y + OffsetY) * Width + (X + 0)] = Red;
		}

		if (!TEST_NSWE(NSWE, L2Geodata::WEST)) {

			int X = 0;

			for (int OffsetY = 1; OffsetY < Width - 1; OffsetY++)
				Pixels[(Y + OffsetY) * Width + (X + 0)] = Red;
		}

		if (!TEST_NSWE(NSWE, L2Geodata::NORTH)) {

			for (int XOffset = 1; XOffset < Width - 1; XOffset++)
				Pixels[(Y + 0) * Width + (0 + XOffset)] = Red;
		}

		if (!TEST_NSWE(NSWE, L2Geodata::SOUTH)) {

			for (int OffsetX = 1; OffsetX < Width - 1; OffsetX++)
				Pixels[(Y + Width - 1) * Width + (0 + OffsetX)] = Red;
		}
	}
}

// Utils

int GetGridZ(int16_t Height) {

	return Height / L2Geodata::HEIGHT_RESOLUTION;
}

int16_t GridZToHeight(int GridZ) {

	return GridZ * L2Geodata::HEIGHT_RESOLUTION;
}

int GetDirection(int16_t From, int16_t To) {
	return Sign((int32_t)To - (int32_t)From);
}

// HeightRange

L2GeodataModelGenerator::HeightRange::HeightRange(int16_t Start, int16_t StartLayerIndex, int16_t End, int16_t EndLayerIndex) {

	Direction = GetDirection(Start, End);
	assert(Direction != 0);

	if (Direction == -1) {
		this->Start = End;
		this->End = Start;
	}
	else {
		this->Start = Start;
		this->End = End;
	}

	this->TopLayerIndex = min(StartLayerIndex, EndLayerIndex);
	this->LowestLayerIndex = max(StartLayerIndex, EndLayerIndex);
}

bool L2GeodataModelGenerator::HeightRange::SortedOverlapTestWith(HeightRange* OtherRange) {

	return (OtherRange->Direction == this->Direction && OtherRange->Start <= this->End);
}

bool L2GeodataModelGenerator::HeightRange::OverlapTestWith(HeightRange* OtherRange) {

	return (IsLayersCompatible(OtherRange) && OtherRange->Direction == this->Direction && OtherRange->Start < this->End && OtherRange->End > this->Start);
}

void L2GeodataModelGenerator::HeightRange::MergeWith(HeightRange* OtherRange) {
	this->End = max(OtherRange->End, this->End);
	this->TopLayerIndex = min(OtherRange->TopLayerIndex, this->TopLayerIndex);
	this->LowestLayerIndex = max(OtherRange->LowestLayerIndex, this->LowestLayerIndex);
}

int16_t L2GeodataModelGenerator::HeightRange::GetMeanHeight(void) {
	return (int16_t)(((int32_t)Start + (int32_t)End) / 2);
}

bool L2GeodataModelGenerator::HeightRange::HaveTopLayer(void)
{
	return !TOP_LAYER_COMPLIANCE || (this->TopLayerIndex == 0 && this->LowestLayerIndex == 0);
}

bool L2GeodataModelGenerator::HeightRange::IsLayersCompatible(HeightRange* OtherRange)
{
	return this->HaveTopLayer() == OtherRange->HaveTopLayer();
}

void L2GeodataModelGenerator::HeightRange::CutTo(HeightRange* OtherRange, bool Direction)
{
	if (OtherRange->Direction != this->Direction)
		throw new runtime_error("Attemp to cut ranges with different direction");

	if (Direction)
		this->End = min(this->End, OtherRange->Start);
	else
		this->Start = max(this->Start, OtherRange->End);
}

bool L2GeodataModelGenerator::HeightRange::operator < (const HeightRange& Other) {

	int32_t LeftWeight = (int32_t)this->Direction * 100000 + (int32_t)this->Start;
	int32_t RightWeight = (int32_t)Other.Direction * 100000 + (int32_t)Other.Start;

	return LeftWeight < RightWeight;
}