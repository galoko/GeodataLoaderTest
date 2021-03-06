#include "stdafx.h"

#include "L2Geodata.h"

#include <iostream>
#include <experimental/filesystem>
#include <filesystem> 
#include <sstream>
#include <fstream>

#include <time.h>

#include "TimeUtils.h"

using namespace experimental::filesystem::v1;
using namespace string_literals;

int16_t *L2Geodata::FullData;
int32_t L2Geodata::MultilayerBlockMap[GEO_WIDTH_IN_REGIONS][GEO_HEIGHT_IN_REGIONS][GEO_REGION_SIZE_IN_BLOCKS][GEO_REGION_SIZE_IN_BLOCKS];
int32_t L2Geodata::MultilayerSubblockMap[MULTILAYER_BLOCK_LIMIT][GEO_BLOCK_SIZE][GEO_BLOCK_SIZE];
int16_t L2Geodata::LayersTable[LAYERS_COUNT_LIMIT];

int32_t L2Geodata::NextMultilayerBlockMapIndex;
int32_t L2Geodata::NextLayersTableIndex;

uint8_t *L2Geodata::NWC_FullData;
int32_t L2Geodata::NWC_MultilayerBlockMap[GEO_WIDTH_IN_REGIONS][GEO_HEIGHT_IN_REGIONS][GEO_REGION_SIZE_IN_BLOCKS][GEO_REGION_SIZE_IN_BLOCKS];
int32_t L2Geodata::NWC_MultilayerSubblockMap[MULTILAYER_BLOCK_LIMIT][GEO_BLOCK_SIZE][GEO_BLOCK_SIZE];
uint8_t L2Geodata::NWC_LayersTable[LAYERS_COUNT_LIMIT];
		
atomic<int32_t> L2Geodata::NWC_NextMultilayerBlockMapIndex;
atomic<int32_t> L2Geodata::NWC_NextLayersTableIndex;

// get

int16_t *L2Geodata::GetGeoSubBlockPtrInternal(uint32_t RegionX, uint32_t RegionY, uint32_t BlockX, uint32_t BlockY,
	uint32_t SubBlockX, uint32_t SubBlockY) {
	uint32_t Index;

	Index =
		RegionY * GEO_ROW_SIZE + RegionX * GEO_REGION_AREA_SIZE +
		BlockX * GEO_REGION_COLUMN_SIZE + BlockY * GEO_BLOCK_AREA_SIZE +
		SubBlockX * GEO_BLOCK_COLUMN_SIZE + SubBlockY * 1;

	return &FullData[Index];
}

bool L2Geodata::WorldToGeo(int32_t WorldX, int32_t WorldY, uint32_t *GeoX, uint32_t *GeoY) {

	if (WorldX < MAP_MIN_X || WorldX > MAP_MAX_X || WorldY < MAP_MIN_Y || WorldY > MAP_MAX_Y)
		return false;

	*GeoX = (uint32_t)((WorldX - MAP_MIN_X) / GEO_COORDS_IN_WORLD_COORDS);
	*GeoY = (uint32_t)((WorldY - MAP_MIN_Y) / GEO_COORDS_IN_WORLD_COORDS);

	return true;
}

bool L2Geodata::GeoToWorld(uint32_t GeoX, uint32_t GeoY, int32_t *WorldX, int32_t *WorldY) {

	if (GeoX < 0 || GeoX >= GEO_WIDTH || GeoY < 0 || GeoY > GEO_HEIGHT)
		return false;

	*WorldX = (uint32_t)(GeoX * GEO_COORDS_IN_WORLD_COORDS + MAP_MIN_X);
	*WorldY = (uint32_t)(GeoY * GEO_COORDS_IN_WORLD_COORDS + MAP_MIN_Y);

	return true;
}

#define SplitGeoCoordinates() \
	RegionX = GeoX / GEO_REGION_SIZE; \
	RegionY = GeoY / GEO_REGION_SIZE; \
	\
	BlockX = GeoX % GEO_REGION_SIZE / GEO_BLOCK_SIZE; \
	BlockY = GeoY % GEO_REGION_SIZE / GEO_BLOCK_SIZE; \
	\
	SubBlockX = GeoX % GEO_BLOCK_SIZE / 1; \
	SubBlockY = GeoY % GEO_BLOCK_SIZE / 1;

int16_t* L2Geodata::GetSubBlocks(int32_t WorldX, int32_t WorldY, int16_t& Count) {

	uint32_t GeoX, GeoY;

	if (WorldToGeo(WorldX, WorldY, &GeoX, &GeoY)) {
		
		uint32_t RegionX, RegionY, BlockX, BlockY, SubBlockX, SubBlockY;

		SplitGeoCoordinates();

		int16_t* SubBlock = GetGeoSubBlockPtrInternal(RegionX, RegionY, BlockX, BlockY, SubBlockX, SubBlockY);
		if (*SubBlock == SPECIAL_SUBBLOCK_EMPTY) {
			Count = 0;
			return nullptr;
		}
		else if (*SubBlock == SPECIAL_SUBBLOCK_MULTILAYER) {

			int32_t BlockIndex = MultilayerBlockMap[RegionX][RegionY][BlockX][BlockY];
			if (BlockIndex == -1) {
				Count = 0;
				return nullptr;
			}

			int32_t SubBlockLayersIndex = MultilayerSubblockMap[BlockIndex][SubBlockX][SubBlockY];
			if (SubBlockLayersIndex == -1) {
				Count = 0;
				return nullptr;
			}

			Count = LayersTable[SubBlockLayersIndex];
			return &LayersTable[SubBlockLayersIndex + 1];
		}
		else {
			Count = 1;
			return SubBlock;
		}
	}
	else {
		Count = 0;
		return nullptr;
	}
}
// set

inline void L2Geodata::ValidateSubBlock(int16_t SubBlock) {

	if (SubBlock == SPECIAL_SUBBLOCK_EMPTY || SubBlock == SPECIAL_SUBBLOCK_MULTILAYER)
		throw new runtime_error("Input subblock have special value, you need to either change geodata subblock or change special values");
}

inline void L2Geodata::SetGeoSubBlockInternal(uint32_t RegionX, uint32_t RegionY, uint32_t BlockX, uint32_t BlockY,
	uint32_t SubBlockX, uint32_t SubBlockY, int16_t SubBlock) {

	ValidateSubBlock(SubBlock);

	int16_t* DestSubBlock = GetGeoSubBlockPtrInternal(RegionX, RegionY, BlockX, BlockY, SubBlockX, SubBlockY);
	if (*DestSubBlock != SPECIAL_SUBBLOCK_EMPTY)
		throw new runtime_error("Subblock override prevention");

	*DestSubBlock = SubBlock;
}

inline void L2Geodata::SetGeoLayersInternal(uint32_t RegionX, uint32_t RegionY, uint32_t BlockX, uint32_t BlockY,
	uint32_t SubBlockX, uint32_t SubBlockY, int16_t LayersCount, int16_t* Layers) {

	int16_t* FullGeoSubBlock = GetGeoSubBlockPtrInternal(RegionX, RegionY, BlockX, BlockY, SubBlockX, SubBlockY); 

	if (*FullGeoSubBlock != SPECIAL_SUBBLOCK_EMPTY)
		throw new runtime_error("Geo subblock is not empty");

	*FullGeoSubBlock = SPECIAL_SUBBLOCK_MULTILAYER;

	for (int Index = 0; Index < LayersCount; Index++)
		ValidateSubBlock(Layers[Index]);

	int32_t BlockIndex = MultilayerBlockMap[RegionX][RegionY][BlockX][BlockY];
	if (BlockIndex == -1) {

		BlockIndex = AllocateBlockMapEntry();
		MultilayerBlockMap[RegionX][RegionY][BlockX][BlockY] = BlockIndex;
	}

	int32_t SubBlockLayersIndex = MultilayerSubblockMap[BlockIndex][SubBlockX][SubBlockY];
	if (SubBlockLayersIndex != -1)
		throw new runtime_error("Block layers are already set");

	SubBlockLayersIndex = AllocateLayersEntries(1 + LayersCount);
	MultilayerSubblockMap[BlockIndex][SubBlockX][SubBlockY] = SubBlockLayersIndex;

	LayersTable[SubBlockLayersIndex] = LayersCount;
	memcpy(&LayersTable[SubBlockLayersIndex + 1], Layers, LayersCount * sizeof(*Layers));

	for (int Index = 1; Index < LayersCount; Index++) {

		int16_t PrevHeight = GET_GEO_HEIGHT(Layers[Index - 1]);
		int16_t Height = GET_GEO_HEIGHT(Layers[Index]);

		if (PrevHeight <= Height)
			throw new runtime_error("Layers required to be sorted (" + to_string(PrevHeight) + " > " + to_string(Height) + ")");
	}
}

inline void L2Geodata::EraseGeoSubBlock(uint32_t RegionX, uint32_t RegionY, uint32_t BlockX, uint32_t BlockY, uint32_t SubBlockX, uint32_t SubBlockY)
{
	int32_t BlockIndex = MultilayerBlockMap[RegionX][RegionY][BlockX][BlockY];
	if (BlockIndex != -1)
		MultilayerSubblockMap[BlockIndex][SubBlockX][SubBlockY] = -1;

	int16_t* DestSubBlock = GetGeoSubBlockPtrInternal(RegionX, RegionY, BlockX, BlockY, SubBlockX, SubBlockY);
	*DestSubBlock = SPECIAL_SUBBLOCK_EMPTY;
}

void L2Geodata::SetSubBlocks(int32_t WorldX, int32_t WorldY, int16_t Count, ...)
{
	if (Count < 0 || Count > LAYERS_PER_SUBBLOCK_LIMIT)
		throw new runtime_error("Invalid subblock count");

	uint32_t GeoX, GeoY;

	if (WorldToGeo(WorldX, WorldY, &GeoX, &GeoY)) {

		uint32_t RegionX, RegionY, BlockX, BlockY, SubBlockX, SubBlockY;

		SplitGeoCoordinates();

		EraseGeoSubBlock(RegionX, RegionY, BlockX, BlockY, SubBlockX, SubBlockY);

		if (Count > 0) {
			int16_t Layers[LAYERS_PER_SUBBLOCK_LIMIT];

			va_list ap;
			va_start(ap, Count);
			for (int Index = 0; Index < Count; Index++)
				Layers[Index] = va_arg(ap, int16_t);
			va_end(ap);

			sort(begin(Layers), begin(Layers) + Count);
			reverse(begin(Layers), begin(Layers) + Count);

			if (Count == 1)
				SetGeoSubBlockInternal(RegionX, RegionY, BlockX, BlockY, SubBlockX, SubBlockY, Layers[0]);
			else {
				SetGeoLayersInternal(RegionX, RegionY, BlockX, BlockY, SubBlockX, SubBlockY, Count, Layers);
			}
		}
	}
}

// alloc

void L2Geodata::AllocateData(void) {

	if (FullData)
		throw new runtime_error("GeoData is already allocated");

	if (((SPECIAL_SUBBLOCK_EMPTY >> 8) & 0xFF) != (SPECIAL_SUBBLOCK_EMPTY & 0xFF))
		throw new runtime_error("Invalid empty block special value");
	
	FullData = (int16_t*)malloc(GEO_FULL_SIZE_IN_BYTES);

	memset(FullData, SPECIAL_SUBBLOCK_EMPTY & 0xFF, GEO_FULL_SIZE_IN_BYTES);
	memset(MultilayerBlockMap, 0xFF, sizeof(MultilayerBlockMap));
	memset(MultilayerSubblockMap, 0xFF, sizeof(MultilayerSubblockMap));
}

int32_t L2Geodata::AllocateBlockMapEntry(void)
{
	if (NextMultilayerBlockMapIndex + 1 > MULTILAYER_BLOCK_LIMIT)
		throw new runtime_error("Multilayer block count exceeds the limit");

	int32_t Index = NextMultilayerBlockMapIndex;
	NextMultilayerBlockMapIndex++;

	return Index;
}

int32_t L2Geodata::AllocateLayersEntries(uint32_t Count)
{
	if (NextLayersTableIndex + Count > LAYERS_COUNT_LIMIT)
		throw new runtime_error("Layers entries count exceeds the limit");

	int32_t Index = NextLayersTableIndex;
	NextLayersTableIndex += Count;

	return Index;
}

// loading

#define PTS_BLOCK_FLAT 0
#define PTS_BLOCK_COMPLEX 0x40

static int32_t LayersCountInAllSubBlocks = 0;
static int32_t MultiLayerSubBlockCount = 0;
static int32_t MaxLayersCountPerSubBlock = 0;
static int32_t MultiLayerBlockCount = 0;

bool L2Geodata::LoadRegion(uint32_t RegionX, uint32_t RegionY, wstring FilePath, GeoType Type) {

	RegionX -= GEO_X_FIRST;
	RegionY -= GEO_Y_FIRST;

	if (Type == PTS) {

		ifstream Stream(FilePath, ios::binary | ios::ate);
		if (!Stream.is_open())
			return false;

		streamsize size = Stream.tellg();
		if (size <= 18)
			return false;

		size -= 18;

		vector<int16_t> buffer(size / sizeof(int16_t));

		if (!Stream.seekg(18, ios_base::beg) || !Stream.read((char *)buffer.data(), size))
			return false;

		int32_t position = 0;

		for (int BlockX = 0; BlockX < GEO_REGION_SIZE_IN_BLOCKS; BlockX++)
			for (int BlockY = 0; BlockY < GEO_REGION_SIZE_IN_BLOCKS; BlockY++) {

				int16_t SubBlockCount = buffer[position++];

				int16_t SubBlock;

				switch (SubBlockCount) {
				case PTS_BLOCK_FLAT:

					// skip max height (unused)
					position++;

					SubBlock = buffer[position++];
					SubBlock = ((SubBlock << 1) & 0xFFF0) | NSWE_ALL;

					for (int SubBlockX = 0; SubBlockX < GEO_BLOCK_SIZE; SubBlockX++)
						for (int SubBlockY = 0; SubBlockY < GEO_BLOCK_SIZE; SubBlockY++) {

							SetGeoSubBlockInternal(RegionX, RegionY, BlockX, BlockY, SubBlockX, SubBlockY, SubBlock);
						}

					break;
				case PTS_BLOCK_COMPLEX:

					for (int SubBlockX = 0; SubBlockX < GEO_BLOCK_SIZE; SubBlockX++) 
						for (int SubBlockY = 0; SubBlockY < GEO_BLOCK_SIZE; SubBlockY++) {

							SubBlock = buffer[position++];

							SetGeoSubBlockInternal(RegionX, RegionY, BlockX, BlockY, SubBlockX, SubBlockY, SubBlock);
						}

					break;
				default:

					if (SubBlockCount <= PTS_BLOCK_COMPLEX)
						return false;

					MultiLayerBlockCount++;

					int SubBlockCount = 0;

					for (int SubBlockX = 0; SubBlockX < GEO_BLOCK_SIZE; SubBlockX++)
						for (int SubBlockY = 0; SubBlockY < GEO_BLOCK_SIZE; SubBlockY++) {

							int16_t LayersCount = buffer[position++];

							if (LayersCount > 1) {

								SetGeoLayersInternal(RegionX, RegionY, BlockX, BlockY, SubBlockX, SubBlockY, LayersCount, &buffer[position]);
								position += LayersCount;

								LayersCountInAllSubBlocks += LayersCount;
								MultiLayerSubBlockCount++;

								if (LayersCount > MaxLayersCountPerSubBlock)
									MaxLayersCountPerSubBlock = LayersCount;
							}
							else {
								SubBlock = buffer[position++];

								SetGeoSubBlockInternal(RegionX, RegionY, BlockX, BlockY, SubBlockX, SubBlockY, SubBlock);
							}

							SubBlockCount += LayersCount;
						}

					if (SubBlockCount != SubBlockCount)
						return false;

					break;
				}
			}

		if (position != buffer.size())
			return false;
	}

	return true;
}

void L2Geodata::Init(void)
{
	AllocateData();
	AllocateNWCData();
}

void L2Geodata::Load(wstring Directory, GeoType Type) {

	LONGLONG StartTime = GetTime();

	for (directory_entry p : directory_iterator(Directory)) {

		path path = p.path();
		wstring filename = path.filename().replace_extension("");

		unsigned int regionX, regionY;

		const wchar_t *str = filename.c_str();
		wchar_t *end;

		regionX = wcstol(str, &end, 10);
		if (str == end || errno == ERANGE || *end != '_')
			continue;

		str = end + 1;
		regionY = wcstol(str, &end, 10);
		if (str == end || errno == ERANGE)
			continue;

		bool Loaded = LoadRegion(regionX, regionY, path, Type);
		if (!Loaded)
			throw new runtime_error("Coudn't load geo file");
	}

	LONGLONG EndTime = GetTime();

	cout << "Geo loaded for " << TimeToMs(EndTime - StartTime) << " ms" << endl;

	cout << "Layers count: " << LayersCountInAllSubBlocks << endl;
	cout << "Layers subblock count: " << MultiLayerSubBlockCount << endl;
	cout << "Max layer count per subblock: " << MaxLayersCountPerSubBlock << endl;
	cout << "Multilayer block count: " << MultiLayerBlockCount << endl;

	cout << "Free multilayer blocks count: " << MULTILAYER_BLOCK_LIMIT - NextMultilayerBlockMapIndex << endl;
	cout << "Free layers count: " << LAYERS_COUNT_LIMIT - NextLayersTableIndex << endl;
}

void L2Geodata::LoadEasyGeo(wstring FilePath) {

	LONGLONG StartTime = GetTime();

	ifstream Stream(FilePath, ios::binary);

	Stream.read((char *)FullData, GEO_FULL_SIZE_IN_BYTES);
	Stream.read((char *)&MultilayerBlockMap, sizeof(MultilayerBlockMap));
	Stream.read((char *)&MultilayerSubblockMap, sizeof(MultilayerSubblockMap));
	Stream.read((char *)&LayersTable, sizeof(LayersTable));

	Stream.read((char *)&NextMultilayerBlockMapIndex, sizeof(NextMultilayerBlockMapIndex));
	Stream.read((char *)&NextLayersTableIndex, sizeof(NextLayersTableIndex));

	if (Stream.fail())
		throw new runtime_error("Couldn't load easygeo");

	LONGLONG EndTime = GetTime();

	cout << "Easy geo loaded for " << TimeToMs(EndTime - StartTime) << " ms" << endl;
}

void L2Geodata::SaveEasyGeo(wstring FilePath) {

	ofstream Stream(FilePath, ios::binary);

	Stream.write((char *)FullData, GEO_FULL_SIZE_IN_BYTES);
	Stream.write((char *)&MultilayerBlockMap, sizeof(MultilayerBlockMap));
	Stream.write((char *)&MultilayerSubblockMap, sizeof(MultilayerSubblockMap));
	Stream.write((char *)&LayersTable, sizeof(LayersTable));
	Stream.write((char *)&NextMultilayerBlockMapIndex, sizeof(NextMultilayerBlockMapIndex));
	Stream.write((char *)&NextLayersTableIndex, sizeof(NextLayersTableIndex));
}

// Neighbor Weight Cache

void L2Geodata::AllocateNWCData(void)
{
	if (NWC_FullData)
		throw new runtime_error("Neighbor Weight Cache is already allocated");

	NWC_FullData = (uint8_t*)calloc(NWC_FULL_SIZE_IN_BYTES, 1);

	memset(NWC_MultilayerBlockMap, 0xFF, sizeof(NWC_MultilayerBlockMap));
	memset(NWC_MultilayerSubblockMap, 0xFF, sizeof(NWC_MultilayerSubblockMap));
}

int32_t L2Geodata::AllocateNWCBlockMapEntry(void)
{
	int32_t Index = NWC_NextMultilayerBlockMapIndex.fetch_add(1);

	if (Index > MULTILAYER_BLOCK_LIMIT)
		throw new runtime_error("Multilayer weight count exceeds the limit");

	return Index;
}

int32_t L2Geodata::AllocateNWCLayersEntries(uint32_t Count)
{
	int32_t Index = NWC_NextLayersTableIndex.fetch_add(Count);

	if (Index > LAYERS_COUNT_LIMIT)
		throw new runtime_error("Layers entries count exceeds the limit (NWC)");

	return Index;
}

void L2Geodata::LoadNeighborWeightCache(wstring FilePath)
{
	ifstream Stream(FilePath, ios::binary);

	Stream.read((char *)NWC_FullData, NWC_FULL_SIZE_IN_BYTES);
	Stream.read((char *)&NWC_MultilayerBlockMap, sizeof(NWC_MultilayerBlockMap));
	Stream.read((char *)&NWC_MultilayerSubblockMap, sizeof(NWC_MultilayerSubblockMap));
	Stream.read((char *)&NWC_LayersTable, sizeof(NWC_LayersTable));
	uint32_t NWC_NextMultilayerBlockMapIndex_Local, NWC_NextLayersTableIndex_Local;
	Stream.read((char *)&NWC_NextMultilayerBlockMapIndex_Local, sizeof(NWC_NextMultilayerBlockMapIndex_Local));
	Stream.read((char *)&NWC_NextLayersTableIndex_Local, sizeof(NWC_NextLayersTableIndex_Local));
}

void L2Geodata::SaveNeighborWeightCache(wstring FilePath)
{
	ofstream Stream(FilePath, ios::binary);

	Stream.write((char *)NWC_FullData, NWC_FULL_SIZE_IN_BYTES);
	Stream.write((char *)&NWC_MultilayerBlockMap, sizeof(NWC_MultilayerBlockMap));
	Stream.write((char *)&NWC_MultilayerSubblockMap, sizeof(NWC_MultilayerSubblockMap));
	Stream.write((char *)&NWC_LayersTable, sizeof(NWC_LayersTable));
	uint32_t NWC_NextMultilayerBlockMapIndex_Local = NWC_NextMultilayerBlockMapIndex;
	uint32_t NWC_NextLayersTableIndex_Local = NWC_NextLayersTableIndex;
	Stream.write((char *)&NWC_NextMultilayerBlockMapIndex_Local, sizeof(NWC_NextMultilayerBlockMapIndex_Local));
	Stream.write((char *)&NWC_NextLayersTableIndex_Local, sizeof(NWC_NextLayersTableIndex_Local));
}

inline uint8_t* L2Geodata::GetNeighborWeightPtrInternal(uint32_t RegionX, uint32_t RegionY, uint32_t BlockX, uint32_t BlockY,
	uint32_t SubBlockX, uint32_t SubBlockY)
{
	uint32_t Index;

	Index =
		RegionY * GEO_ROW_SIZE + RegionX * GEO_REGION_AREA_SIZE +
		BlockX * GEO_REGION_COLUMN_SIZE + BlockY * GEO_BLOCK_AREA_SIZE +
		SubBlockX * GEO_BLOCK_COLUMN_SIZE + SubBlockY * 1;

	return &NWC_FullData[Index];
}

uint8_t* L2Geodata::GetNeighborWeights(int32_t WorldX, int32_t WorldY, uint8_t& Count)
{
	uint32_t GeoX, GeoY;

	if (WorldToGeo(WorldX, WorldY, &GeoX, &GeoY)) {

		uint32_t RegionX, RegionY, BlockX, BlockY, SubBlockX, SubBlockY;

		SplitGeoCoordinates();

		uint8_t* Weight = GetNeighborWeightPtrInternal(RegionX, RegionY, BlockX, BlockY, SubBlockX, SubBlockY);
		if (*Weight == SPECIAL_NEIGHBOR_WEIGHT_MULTILAYER) {

			int32_t BlockIndex = NWC_MultilayerBlockMap[RegionX][RegionY][BlockX][BlockY];
			if (BlockIndex == -1) {
				Count = 0;
				return nullptr;
			}

			int32_t SubBlockLayersIndex = NWC_MultilayerSubblockMap[BlockIndex][SubBlockX][SubBlockY];
			if (SubBlockLayersIndex == -1) {
				Count = 0;
				return nullptr;
			}

			Count = NWC_LayersTable[SubBlockLayersIndex];
			return &NWC_LayersTable[SubBlockLayersIndex + 1];
		}
		else {
			Count = 1;
			return Weight;
		}
	}
	else {
		Count = 0;
		return nullptr;
	}
}

inline void L2Geodata::SetNeighborWeightInternal(uint32_t RegionX, uint32_t RegionY, uint32_t BlockX, uint32_t BlockY, uint32_t SubBlockX,
	uint32_t SubBlockY, uint8_t Weight)
{
	uint8_t* DestWeight = GetNeighborWeightPtrInternal(RegionX, RegionY, BlockX, BlockY, SubBlockX, SubBlockY);

	*DestWeight = Weight;
}

inline void L2Geodata::SetNeighborWeightsInternal(uint32_t RegionX, uint32_t RegionY, uint32_t BlockX, uint32_t BlockY, uint32_t SubBlockX, 
	uint32_t SubBlockY, uint8_t WeightsCount, uint8_t* Weights)
{
	uint8_t* FullWeight = GetNeighborWeightPtrInternal(RegionX, RegionY, BlockX, BlockY, SubBlockX, SubBlockY);

	*FullWeight = SPECIAL_NEIGHBOR_WEIGHT_MULTILAYER;

	int32_t BlockIndex = NWC_MultilayerBlockMap[RegionX][RegionY][BlockX][BlockY];
	if (BlockIndex == -1) {

		BlockIndex = AllocateNWCBlockMapEntry();
		NWC_MultilayerBlockMap[RegionX][RegionY][BlockX][BlockY] = BlockIndex;
	}

	int32_t WeightsIndex = NWC_MultilayerSubblockMap[BlockIndex][SubBlockX][SubBlockY];
	if (WeightsIndex != -1)
		throw new runtime_error("Block weights are already set");

	WeightsIndex = AllocateNWCLayersEntries(1 + WeightsCount);
	NWC_MultilayerSubblockMap[BlockIndex][SubBlockX][SubBlockY] = WeightsIndex;

	NWC_LayersTable[WeightsIndex] = WeightsCount;
	memcpy(&NWC_LayersTable[WeightsIndex + 1], Weights, WeightsCount * sizeof(*Weights));
}

void L2Geodata::SetNeighborWeights(int32_t WorldX, int32_t WorldY, uint8_t Count, uint8_t* Weights)
{
	if (Count < 0 || Count > LAYERS_PER_SUBBLOCK_LIMIT)
		throw new runtime_error("Invalid weights count");

	uint32_t GeoX, GeoY;

	if (WorldToGeo(WorldX, WorldY, &GeoX, &GeoY)) {

		uint32_t RegionX, RegionY, BlockX, BlockY, SubBlockX, SubBlockY;

		SplitGeoCoordinates();

		if (Count == 0)
			SetNeighborWeightInternal(RegionX, RegionY, BlockX, BlockY, SubBlockX, SubBlockY, 0);
		else
		if (Count == 1)
			SetNeighborWeightInternal(RegionX, RegionY, BlockX, BlockY, SubBlockX, SubBlockY, Weights[0]);
		else 
			SetNeighborWeightsInternal(RegionX, RegionY, BlockX, BlockY, SubBlockX, SubBlockY, Count, Weights);
	}
}

// Utils forward declaration

int OffsetToNSWE(int OffsetX, int OffsetY);
int32_t GetHeightDiff(int16_t From, int16_t To);

// Usage utils

void L2Geodata::GetLowAndHighLayers(int16_t SubBlock, int16_t* Layers, int16_t LayersCount, int16_t& LowLayerIndex, int16_t& HighLayerIndex) {

	LowLayerIndex = -1;
	HighLayerIndex = -1;

	int16_t Height = GET_GEO_HEIGHT(SubBlock);

	for (int Index = 0; Index < LayersCount; Index++) {

		int16_t CurrentHeight = GET_GEO_HEIGHT(Layers[Index]);

		if (CurrentHeight <= Height) {

			LowLayerIndex = Index;
			break;
		}

		HighLayerIndex = Index;
	}
}

bool L2Geodata::CanGoInThisDirection(int16_t SubBlock, int DirectionX, int DirectionY)
{
	int NSWE = GET_GEO_NSWE(SubBlock);

	return TEST_NSWE(NSWE, OffsetToNSWE(DirectionX, DirectionY));
}

bool L2Geodata::CanGoUnderneath(int16_t SubBlock, int16_t HigherSubBlock)
{
	int16_t Height = GET_GEO_HEIGHT(SubBlock);
	int16_t HigherHeight = GET_GEO_HEIGHT(HigherSubBlock);

	return GetHeightDiff(Height, HigherHeight) > L2Geodata::MIN_LAYER_DIFF;
}

bool L2Geodata::GetDestLayerIndex(int16_t SubBlock, int OffsetX, int OffsetY, int16_t* Layers, int16_t LayersCount, int16_t& DestLayerIndex)
{
	// just can't go in this direction
	if (!CanGoInThisDirection(SubBlock, OffsetX, OffsetY))
		return false;

	int16_t LowLayerIndex, HighLayerIndex;
	GetLowAndHighLayers(SubBlock, Layers, LayersCount, LowLayerIndex, HighLayerIndex);

	if (HighLayerIndex != -1) {

		int16_t HigherSubBlock = Layers[HighLayerIndex];
		// if we cannot go underneath the higher subblock then we'll go right on it
		if (!CanGoUnderneath(SubBlock, HigherSubBlock)) {

			DestLayerIndex = HighLayerIndex;
			return true;
		}
	}

	// at this moment we already not struggling with higher subblock
	if (LowLayerIndex != -1) {

		DestLayerIndex = LowLayerIndex;
		return true;
	}

	// or else we cannot go on higher block and don't even have lower block so we kinda stuck
	return false;
}

bool L2Geodata::GetWallLayerIndex(int16_t SubBlock, int OffsetX, int OffsetY, int16_t* Layers, int16_t LayersCount, int16_t& DestLayerIndex)
{
	int16_t LowLayerIndex, HighLayerIndex;
	GetLowAndHighLayers(SubBlock, Layers, LayersCount, LowLayerIndex, HighLayerIndex);
	if (HighLayerIndex == -1)
		return false;

	DestLayerIndex = HighLayerIndex;

	bool CantGoInThisDirection = !CanGoInThisDirection(SubBlock, OffsetX, OffsetY);
	bool CantGoUnderneathHigherLayer = !CanGoUnderneath(SubBlock, Layers[HighLayerIndex]);
	bool IsNothingUnderHigherLayer = LowLayerIndex == -1;

	bool WallRequired = CantGoInThisDirection || CantGoUnderneathHigherLayer || IsNothingUnderHigherLayer;

	return WallRequired;
}

bool L2Geodata::GetGroundSubBlock(int32_t WorldX, int32_t WorldY, int32_t WorldZ, int16_t& GroundSubBlock, int16_t& GroundLayerIndex)
{
	int16_t LayersCount;
	int16_t* Layers = GetSubBlocks(WorldX, WorldY, LayersCount);

	for (int16_t LayerIndex = 0; LayerIndex < LayersCount; LayerIndex++) {

		int16_t SubBlock = Layers[LayerIndex];

		if (GET_GEO_HEIGHT(SubBlock) <= WorldZ + MIN_LAYER_DIFF) {
			GroundSubBlock = SubBlock;
			GroundLayerIndex = LayerIndex;
			return true;
		}
	}

	return false;
}

// Utils

int OffsetToNSWE(int OffsetX, int OffsetY) {

	int Result = 0;

	if (OffsetX == 1)
		Result |= L2Geodata::EAST;
	else
		if (OffsetX == -1)
			Result |= L2Geodata::WEST;

	if (OffsetY == 1)
		Result |= L2Geodata::SOUTH;
	else
		if (OffsetY == -1)
			Result |= L2Geodata::NORTH;

	return Result;
}

int32_t GetHeightDiff(int16_t From, int16_t To) {
	return (int32_t)To - (int32_t)From;
}