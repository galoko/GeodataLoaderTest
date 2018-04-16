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

int16_t *L2Geodata::GetGeoHeightPtrInternal(uint32_t RegionX, uint32_t RegionY, uint32_t BlockX, uint32_t BlockY,
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

int16_t *L2Geodata::GetGeoHeightPtr(uint32_t GeoX, uint32_t GeoY) {

	uint32_t RegionX, RegionY, BlockX, BlockY, SubBlockX, SubBlockY;

	SplitGeoCoordinates();

	return GetGeoHeightPtrInternal(RegionX, RegionY, BlockX, BlockY, SubBlockX, SubBlockY);
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

int16_t *L2Geodata::GetWorldHeightPtr(int32_t WorldX, int32_t WorldY) {

	uint32_t GeoX, GeoY;

	if (WorldToGeo(WorldX, WorldY, &GeoX, &GeoY))
		return GetGeoHeightPtr(GeoX, GeoY);
	else
		return NULL;
}

int16_t* L2Geodata::GetLayeredHeight(int32_t WorldX, int32_t WorldY, int16_t& Count) {

	uint32_t GeoX, GeoY;

	if (WorldToGeo(WorldX, WorldY, &GeoX, &GeoY)) {
		
		uint32_t RegionX, RegionY, BlockX, BlockY, SubBlockX, SubBlockY;

		SplitGeoCoordinates();

		int16_t* Height = GetGeoHeightPtrInternal(RegionX, RegionY, BlockX, BlockY, SubBlockX, SubBlockY);
		if (*Height == SPECIAL_HEIGHT_EMPTY) {
			Count = 0;
			return NULL;
		}
		else if (*Height == SPECIAL_HEIGHT_MULTILAYER) {

			int32_t BlockIndex = MultilayerBlockMap[RegionX][RegionY][BlockX][BlockY];
			if (BlockIndex == -1) {
				Count = 0;
				return NULL;
			}

			int32_t SubBlockLayersIndex = MultilayerSubblockMap[BlockIndex][SubBlockX][SubBlockY];
			if (SubBlockLayersIndex == -1) {
				Count = 0;
				return NULL;
			}

			Count = LayersTable[SubBlockLayersIndex];
			return &LayersTable[SubBlockLayersIndex + 1];
		}
		else {
			Count = 1;
			return Height;
		}
	}
	else {
		Count = 0;
		return NULL;
	}
}

int16_t L2Geodata::GetHeight(int32_t WorldX, int32_t WorldY)
{
	int16_t Count;
	int16_t *Height = GetLayeredHeight(WorldX, WorldY, Count);

	if (Count > 0)
		return *Height;
	else
		return 0; // ?
}

// set

inline void L2Geodata::ValidateHeight(int16_t Height) {

	if (Height == SPECIAL_HEIGHT_EMPTY || Height == SPECIAL_HEIGHT_MULTILAYER)
		throw new std::runtime_error("Input height have special value, you need to either change geodata height or change special values");
}

inline void L2Geodata::SetGeoHeightInternal(uint32_t RegionX, uint32_t RegionY, uint32_t BlockX, uint32_t BlockY,
	uint32_t SubBlockX, uint32_t SubBlockY, short Height) {

	ValidateHeight(Height);

	*GetGeoHeightPtrInternal(RegionX, RegionY, BlockX, BlockY, SubBlockX, SubBlockY) = Height;
}

inline void L2Geodata::SetGeoLayersInternal(uint32_t RegionX, uint32_t RegionY, uint32_t BlockX, uint32_t BlockY,
	uint32_t SubBlockX, uint32_t SubBlockY, int16_t LayersCount, int16_t* Layers) {

	int16_t* FullGeoHeight = GetGeoHeightPtrInternal(RegionX, RegionY, BlockX, BlockY, SubBlockX, SubBlockY); 

	if (*FullGeoHeight != SPECIAL_HEIGHT_EMPTY)
		throw new std::runtime_error("Geo height is not empty");

	*FullGeoHeight = SPECIAL_HEIGHT_MULTILAYER;

	for (int Index = 0; Index < LayersCount; Index++)
		ValidateHeight(Layers[Index]);

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

		int16_t PrevHeight = Layers[Index - 1];
		int16_t Height = Layers[Index];

		if (PrevHeight <= Height)
			throw new std::runtime_error("Layers required to be sorted (" + to_string(PrevHeight) + " > " + to_string(Height) + ")");
	}
}

// alloc

void L2Geodata::AllocateData(void) {

	if (FullData)
		throw new std::runtime_error("GeoData is already allocated");

	if (((SPECIAL_HEIGHT_EMPTY >> 8) & 0xFF) != (SPECIAL_HEIGHT_EMPTY & 0xFF))
		throw new std::runtime_error("Invalid empty block special value");

	FullData = (short *)malloc(GEO_FULL_SIZE_IN_BYTES);

	memset(FullData, SPECIAL_HEIGHT_EMPTY & 0xFF, GEO_FULL_SIZE_IN_BYTES);
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

				int16_t Height;

				switch (SubBlockCount) {
				case PTS_BLOCK_FLAT:

					// skip max height (unused)
					position++;

					Height = buffer[position++];
					Height = ((Height << 1) & 0xFFF0) | NSWE_ALL;

					for (int SubBlockX = 0; SubBlockX < GEO_BLOCK_SIZE; SubBlockX++)
						for (int SubBlockY = 0; SubBlockY < GEO_BLOCK_SIZE; SubBlockY++) {

							SetGeoHeightInternal(RegionX, RegionY, BlockX, BlockY, SubBlockX, SubBlockY, Height);
						}

					break;
				case PTS_BLOCK_COMPLEX:

					for (int SubBlockX = 0; SubBlockX < GEO_BLOCK_SIZE; SubBlockX++) 
						for (int SubBlockY = 0; SubBlockY < GEO_BLOCK_SIZE; SubBlockY++) {

							Height = buffer[position++];

							SetGeoHeightInternal(RegionX, RegionY, BlockX, BlockY, SubBlockX, SubBlockY, Height);
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
								Height = buffer[position++];

								SetGeoHeightInternal(RegionX, RegionY, BlockX, BlockY, SubBlockX, SubBlockY, Height);
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

void L2Geodata::Load(wstring Directory, GeoType Type) {

	AllocateData();

	int32_t GeoX = 9 * GEO_REGION_SIZE;
	int32_t GeoY = 12 * GEO_REGION_SIZE;
	int32_t WorldX, WorldY;
	GeoToWorld(GeoX, GeoY, &WorldX, &WorldY);

	cout << WorldX << ", " << WorldY << endl;

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