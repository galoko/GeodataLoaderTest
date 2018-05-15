#pragma once

#include <string>
#include <atomic>

using namespace std;

enum GeoType {
	L2J,
	PTS,
	INTERNAL
};

#define GET_GEO_HEIGHT(subblock) ((int16_t)(subblock & 0xFFF0) >> 1)
#define GET_GEO_NSWE(subblock) ((int16_t)(subblock & 0x0F))
#define MAKE_SUBBLOCK(height, NSWE) ((int16_t)((int16_t)(height & 0xFFF0) << 1 | (int16_t)(NSWE & 0x0F)))
#define TEST_NSWE(NSWE, Mask) ((NSWE & Mask) == Mask)

class L2Geodata {
public:
	// if flag is present then we can go to this offset, otherwise we can't
	const static int16_t EAST      = 0x01; // +X
	const static int16_t WEST      = 0x02; // -X
	const static int16_t SOUTH     = 0x04; // +Y
	const static int16_t NORTH     = 0x08; // -Y
	const static int16_t NSWE_ALL  = EAST | WEST | SOUTH | NORTH;
	const static int16_t NSWE_NONE = 0x00;

	const static uint32_t GEO_X_FIRST = 11;
	const static uint32_t GEO_Y_FIRST = 10;
	const static uint32_t GEO_X_LAST  = 26;
	const static uint32_t GEO_Y_LAST  = 26;

	const static uint32_t GEO_BLOCK_SIZE  = 8;

	const static uint32_t GEO_REGION_SIZE_IN_BLOCKS = 256;
	const static uint32_t GEO_REGION_SIZE = GEO_REGION_SIZE_IN_BLOCKS * GEO_BLOCK_SIZE;

	const static uint32_t GEO_WIDTH_IN_REGIONS  = GEO_X_LAST - GEO_X_FIRST + 1;
	const static uint32_t GEO_HEIGHT_IN_REGIONS = GEO_Y_LAST - GEO_Y_FIRST + 1;

	const static uint32_t GEO_WIDTH  = GEO_WIDTH_IN_REGIONS  * GEO_REGION_SIZE;
	const static uint32_t GEO_HEIGHT = GEO_HEIGHT_IN_REGIONS * GEO_REGION_SIZE;

	const static uint32_t GEO_FULL_SIZE = GEO_WIDTH * GEO_HEIGHT;
	const static uint32_t GEO_FULL_SIZE_IN_BYTES = GEO_FULL_SIZE * sizeof(int16_t);

	const static uint32_t GEO_BLOCK_AREA_SIZE = GEO_BLOCK_SIZE * GEO_BLOCK_SIZE;
	const static uint32_t GEO_REGION_AREA_SIZE = GEO_REGION_SIZE * GEO_REGION_SIZE;

	const static uint32_t GEO_ROW_SIZE = GEO_WIDTH_IN_REGIONS * GEO_REGION_AREA_SIZE;
	const static uint32_t GEO_REGION_COLUMN_SIZE = GEO_REGION_SIZE_IN_BLOCKS * GEO_BLOCK_AREA_SIZE;
	const static uint32_t GEO_BLOCK_COLUMN_SIZE = GEO_BLOCK_SIZE;

	const static int32_t GEO_COORDS_IN_WORLD_COORDS = 16;
	const static int32_t GEO_BLOCK_SIZE_IN_WORLD_COORDS = GEO_REGION_SIZE * GEO_COORDS_IN_WORLD_COORDS;

	const static int32_t MAP_MIN_X = ((int32_t) GEO_X_FIRST - 20) * GEO_BLOCK_SIZE_IN_WORLD_COORDS;
	const static int32_t MAP_MIN_Y = ((int32_t) GEO_Y_FIRST - 18) * GEO_BLOCK_SIZE_IN_WORLD_COORDS;

	const static int32_t MAP_MAX_X = ((int32_t) GEO_X_LAST  - 19) * GEO_BLOCK_SIZE_IN_WORLD_COORDS - 1;
	const static int32_t MAP_MAX_Y = ((int32_t) GEO_Y_LAST  - 17) * GEO_BLOCK_SIZE_IN_WORLD_COORDS - 1;

	const static uint32_t MULTILAYER_BLOCK_LIMIT = 1600 * 1000;
	const static uint32_t LAYERS_COUNT_LIMIT = 280 * 1000 * 1000;
	const static uint32_t LAYERS_PER_SUBBLOCK_LIMIT = 25;

	const static int16_t SPECIAL_SUBBLOCK_EMPTY = 0x7D7D;
	const static int16_t SPECIAL_SUBBLOCK_MULTILAYER = 0x7F7F;

	const static int HEIGHT_RESOLUTION = 8;
	const static int MIN_LAYER_DIFF = 4 * HEIGHT_RESOLUTION;

	static int16_t *FullData;
	static int32_t MultilayerBlockMap[GEO_WIDTH_IN_REGIONS][GEO_HEIGHT_IN_REGIONS][GEO_REGION_SIZE_IN_BLOCKS][GEO_REGION_SIZE_IN_BLOCKS];
	static int32_t MultilayerSubblockMap[MULTILAYER_BLOCK_LIMIT][GEO_BLOCK_SIZE][GEO_BLOCK_SIZE];
	static int16_t LayersTable[LAYERS_COUNT_LIMIT];

	static int32_t NextMultilayerBlockMapIndex;
	static int32_t NextLayersTableIndex;

	// Neighbor weight cache

	const static uint32_t NWC_FULL_SIZE_IN_BYTES = GEO_FULL_SIZE * sizeof(uint8_t);

	const static uint8_t SPECIAL_NEIGHBOR_WEIGHT_MULTILAYER = 255;

	static uint8_t *NWC_FullData;
	static int32_t NWC_MultilayerBlockMap[GEO_WIDTH_IN_REGIONS][GEO_HEIGHT_IN_REGIONS][GEO_REGION_SIZE_IN_BLOCKS][GEO_REGION_SIZE_IN_BLOCKS];
	static int32_t NWC_MultilayerSubblockMap[MULTILAYER_BLOCK_LIMIT][GEO_BLOCK_SIZE][GEO_BLOCK_SIZE];
	static uint8_t NWC_LayersTable[LAYERS_COUNT_LIMIT];

	static atomic<int32_t> NWC_NextMultilayerBlockMapIndex;
	static atomic<int32_t> NWC_NextLayersTableIndex;

	L2Geodata(void) { }

	static void AllocateData(void);
	static int32_t AllocateBlockMapEntry(void);
	static int32_t AllocateLayersEntries(uint32_t Count);

	static inline int16_t *GetGeoSubBlockPtrInternal(uint32_t RegionX, uint32_t RegionY,
		uint32_t BlockX, uint32_t BlockY, uint32_t SubBlockX, uint32_t SubBlockY);
	static inline void EraseGeoSubBlock(uint32_t RegionX, uint32_t RegionY,
		uint32_t BlockX, uint32_t BlockY, uint32_t SubBlockX, uint32_t SubBlockY);
	static inline void SetGeoSubBlockInternal(uint32_t RegionX, uint32_t RegionY,
		uint32_t BlockX, uint32_t BlockY, uint32_t SubBlockX, uint32_t SubBlockY, 
		int16_t SubBlockValue);
	static inline void SetGeoLayersInternal(uint32_t RegionX, uint32_t RegionY,
		uint32_t BlockX, uint32_t BlockY, uint32_t SubBlockX, uint32_t SubBlockY,
		int16_t LayersCount, int16_t* Layers);

	static inline void ValidateSubBlock(int16_t SubBlock);

	static bool LoadRegion(uint32_t RegionX, uint32_t RegionY, wstring FilePath, GeoType Type);

	// NWC

	static void AllocateNWCData(void);
	static int32_t AllocateNWCBlockMapEntry(void);
	static int32_t AllocateNWCLayersEntries(uint32_t Count);

	static inline uint8_t *GetNeighborWeightPtrInternal(uint32_t RegionX, uint32_t RegionY,
		uint32_t BlockX, uint32_t BlockY, uint32_t SubBlockX, uint32_t SubBlockY);
	static inline void SetNeighborWeightInternal(uint32_t RegionX, uint32_t RegionY, uint32_t BlockX, uint32_t BlockY, 
		uint32_t SubBlockX, uint32_t SubBlockY, uint8_t Weight);
	static inline void SetNeighborWeightsInternal(uint32_t RegionX, uint32_t RegionY, uint32_t BlockX, uint32_t BlockY,
		uint32_t SubBlockX, uint32_t SubBlockY, uint8_t WeightsCount, uint8_t* Weights);

	// Usage utils

	static void GetLowAndHighLayers(int16_t SubBlock, int16_t* Layers, int16_t LayersCount, int16_t& LowLayerIndex, int16_t& HighLayerIndex);
	static bool CanGoInThisDirection(int16_t SubBlock, int DirectionX, int DirectionY);
	static bool CanGoUnderneath(int16_t SubBlock, int16_t HigherSubBlock);
public:
	static void Init(void);

	static void Load(wstring Directory, GeoType Type);

	static void LoadEasyGeo(wstring FilePath);
	static void SaveEasyGeo(wstring FilePath);

	static void LoadNeighborWeightCache(wstring FilePath);
	static void SaveNeighborWeightCache(wstring FilePath);

	static uint8_t* GetNeighborWeights(int32_t WorldX, int32_t WorldY, uint8_t& Count);
	static void SetNeighborWeights(int32_t WorldX, int32_t WorldY, uint8_t Count, uint8_t* Weights);

	static int16_t* GetSubBlocks(int32_t WorldX, int32_t WorldY, int16_t& Count);
	static void SetSubBlocks(int32_t WorldX, int32_t WorldY, int16_t Count, ...);

	// return true and DestSubBlock will contain block that we gonna land on if we go in this direction, return false if we cannot go in this direction
	static bool GetDestLayerIndex(int16_t SubBlock, int OffsetX, int OffsetY, int16_t* Layers, int16_t LayersCount, int16_t& DestLayerIndex);

	static bool GetWallLayerIndex(int16_t SubBlock, int OffsetX, int OffsetY, int16_t* Layers, int16_t LayersCount, int16_t& DestLayerIndex);

	static bool GetGroundSubBlock(int32_t WorldX, int32_t WorldY, int32_t WorldZ, int16_t& GroundSubBlock, int16_t& GroundLayerIndex);

	static bool GeoToWorld(uint32_t GeoX, uint32_t GeoY, int32_t *WorldX, int32_t *WorldY);
	static bool WorldToGeo(int32_t WorldX, int32_t WorldY, uint32_t *GeoX, uint32_t *GeoY);
};