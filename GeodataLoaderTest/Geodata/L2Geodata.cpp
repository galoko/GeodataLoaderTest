#include "stdafx.h"

#include "L2Geodata.h"

#include <iostream>
#include <experimental/filesystem>
#include <filesystem> 
#include <sstream>
#include <fstream>

#include <time.h>

#include "TimeUtils.h"

namespace fs = std::experimental::filesystem::v1;

using namespace fs;
using namespace string_literals;

int16_t *L2Geodata::FullData;
int32_t L2Geodata::MultilayerBlockMap[GEO_WIDTH_IN_REGIONS][GEO_HEIGHT_IN_REGIONS][GEO_REGION_SIZE_IN_BLOCKS][GEO_REGION_SIZE_IN_BLOCKS];
int32_t L2Geodata::MultilayerSubblockMap[MULTILAYER_BLOCK_LIMIT][GEO_BLOCK_SIZE][GEO_BLOCK_SIZE];
int16_t L2Geodata::LayersTable[LAYERS_COUNT_LIMIT];

int32_t L2Geodata::NextMultilayerBlockMapIndex;
int32_t L2Geodata::NextLayersTableIndex;

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

int16_t *L2Geodata::GetGeoLayersPtrInternal(uint32_t RegionX, uint32_t RegionY, uint32_t BlockX, uint32_t BlockY,
	uint32_t SubBlockX, uint32_t SubBlockY) {
	uint32_t Index;

	Index =
		RegionY * GEO_ROW_SIZE + RegionX * GEO_REGION_AREA_SIZE +
		BlockX * GEO_REGION_COLUMN_SIZE + BlockY * GEO_BLOCK_AREA_SIZE +
		SubBlockX * GEO_BLOCK_COLUMN_SIZE + SubBlockY * 1;

	return &FullData[Index];
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

int16_t *L2Geodata::GetGeoSubBlockPtr(uint32_t GeoX, uint32_t GeoY) {

	uint32_t RegionX, RegionY, BlockX, BlockY, SubBlockX, SubBlockY;

	SplitGeoCoordinates();

	return GetGeoSubBlockPtrInternal(RegionX, RegionY, BlockX, BlockY, SubBlockX, SubBlockY);
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

int16_t *L2Geodata::GetWorldSubBlockPtr(int32_t WorldX, int32_t WorldY) {

	uint32_t GeoX, GeoY;

	if (WorldToGeo(WorldX, WorldY, &GeoX, &GeoY))
		return GetGeoSubBlockPtr(GeoX, GeoY);
	else
		return nullptr;
}

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
		throw new std::runtime_error("Input subblock have special value, you need to either change geodata subblock or change special values");
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
		throw new std::runtime_error("Geo subblock is not empty");

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
		throw new std::runtime_error("Block layers are already set");

	SubBlockLayersIndex = AllocateLayersEntries(1 + LayersCount);
	MultilayerSubblockMap[BlockIndex][SubBlockX][SubBlockY] = SubBlockLayersIndex;

	LayersTable[SubBlockLayersIndex] = LayersCount;
	memcpy(&LayersTable[SubBlockLayersIndex + 1], Layers, LayersCount * sizeof(*Layers));

	for (int Index = 1; Index < LayersCount; Index++) {

		int16_t PrevHeight = GET_GEO_HEIGHT(Layers[Index - 1]);
		int16_t Height = GET_GEO_HEIGHT(Layers[Index]);

		if (PrevHeight <= Height)
			throw new std::runtime_error("Layers required to be sorted (" + to_string(PrevHeight) + " > " + to_string(Height) + ")");
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
		throw new std::runtime_error("GeoData is already allocated");

	if (((SPECIAL_SUBBLOCK_EMPTY >> 8) & 0xFF) != (SPECIAL_SUBBLOCK_EMPTY & 0xFF))
		throw new std::runtime_error("Invalid empty block special value");

	FullData = (int16_t*)malloc(GEO_FULL_SIZE_IN_BYTES);

	memset(FullData, SPECIAL_SUBBLOCK_EMPTY & 0xFF, GEO_FULL_SIZE_IN_BYTES);
	memset(MultilayerBlockMap, 0xFF, sizeof(MultilayerBlockMap));
	memset(MultilayerSubblockMap, 0xFF, sizeof(MultilayerSubblockMap));
}

int32_t L2Geodata::AllocateBlockMapEntry(void)
{
	if (NextMultilayerBlockMapIndex + 1 > MULTILAYER_BLOCK_LIMIT)
		throw new std::runtime_error("Multilayer block count exceeds the limit");

	int32_t Index = NextMultilayerBlockMapIndex;
	NextMultilayerBlockMapIndex++;

	return Index;
}

int32_t L2Geodata::AllocateLayersEntries(uint32_t Count)
{
	if (NextLayersTableIndex + Count > LAYERS_COUNT_LIMIT)
		throw new std::runtime_error("Layers entries count exceeds the limit");

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

		ifstream Stream(FilePath, std::ios::binary | std::ios::ate);
		if (!Stream.is_open())
			return false;

		streamsize size = Stream.tellg();
		if (size <= 18)
			return false;

		size -= 18;

		std::vector<int16_t> buffer(size / sizeof(int16_t));

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
					SubBlock = ((SubBlock & 0xFFF0) << 1) | NSWE_ALL;

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
			throw new std::runtime_error("Coudn't load geo file");
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

	ifstream Stream(FilePath, std::ios::binary);

	Stream.read((char *)FullData, GEO_FULL_SIZE_IN_BYTES);
	Stream.read((char *)&MultilayerBlockMap, sizeof(MultilayerBlockMap));
	Stream.read((char *)&MultilayerSubblockMap, sizeof(MultilayerSubblockMap));
	Stream.read((char *)&LayersTable, sizeof(LayersTable));

	Stream.read((char *)&NextMultilayerBlockMapIndex, sizeof(NextMultilayerBlockMapIndex));
	Stream.read((char *)&NextLayersTableIndex, sizeof(NextLayersTableIndex));

	if (Stream.fail())
		throw new std::runtime_error("Couldn't load easygeo");

	LONGLONG EndTime = GetTime();

	cout << "Easy geo loaded for " << TimeToMs(EndTime - StartTime) << " ms" << endl;
}

void L2Geodata::SaveEasyGeo(wstring FilePath) {

	ofstream Stream(FilePath, std::ios::binary);

	Stream.write((char *)FullData, GEO_FULL_SIZE_IN_BYTES);
	Stream.write((char *)&MultilayerBlockMap, sizeof(MultilayerBlockMap));
	Stream.write((char *)&MultilayerSubblockMap, sizeof(MultilayerSubblockMap));
	Stream.write((char *)&LayersTable, sizeof(LayersTable));
	Stream.write((char *)&NextMultilayerBlockMapIndex, sizeof(NextMultilayerBlockMapIndex));
	Stream.write((char *)&NextLayersTableIndex, sizeof(NextLayersTableIndex));
}