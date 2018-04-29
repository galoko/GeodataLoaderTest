#include "stdafx.h"

#include <inttypes.h>
#define _USE_MATH_DEFINES
#include <cmath>
#include <array>
#include <stack>

#include <d3d11.h>
#include <DirectXMath.h>

#include "Geodata\L2Geodata.h"
#include "Geodata\L2GeodataModelGenerator.h"

using namespace DirectX;

class Geo3DViewForm {
public:
	static Geo3DViewForm& GetInstance() {
		static Geo3DViewForm instance;

		return instance;
	}

	Geo3DViewForm(Geo3DViewForm const&) = delete;
	void operator=(Geo3DViewForm const&) = delete;
private:
	Geo3DViewForm(void) { };

	// Window

	unsigned int Width, Height;
	HWND WindowHandle;

	// DirectX 11 

	ID3D11Device *DirectDevice;
	ID3D11DeviceContext *DirectDeviceCtx;
	IDXGISwapChain *SwapChain;
	ID3D11RenderTargetView *RenderTargetView;

	ID3D11VertexShader *VertexShader;
	ID3D11PixelShader *PixelShader;

	ID3D11InputLayout *VertexLayout;

	ID3D11Texture2D *DepthStencilBuffer;
	ID3D11DepthStencilView *DepthStencilView;

	ID3D11DepthStencilState *DepthStencilState3D, *DepthStencilState2D;

	XMMATRIX WorldMatrix;

	XMMATRIX ViewMatrix;
	XMMATRIX ProjectionMatrix;
	XMMATRIX OrthogonalMatrix;

	struct {
		XMFLOAT4X4 FinalMatrix;
		XMFLOAT4X4 OrthogonalMatrix;
		float ScaleWorld;
		float Padding[3];
	} PersistentVertexVariables;
	ID3D11Buffer *PersistentVertexVariablesRef;

	struct {
		XMFLOAT4X4 WorldMatrix;
		float IsOrthogonal;
		float Padding[3];
	} PerDrawVertexVariables;
	ID3D11Buffer *PerDrawVertexVariablesRef;

	struct {

		XMFLOAT3 AmbientColor;
		float Padding1;

		XMFLOAT3 DiffuseColor;
		float Padding2;

		XMFLOAT3 LightDirection;
		float Padding3;
	} PersistentPixelVariables;
	ID3D11Buffer *PersistentPixelVariablesRef;

	struct {
		float LightEnabled, IsOrthogonal, UseStaticColor, UseNSWE;
		XMFLOAT3 StaticColor;
		float Padding;
	} PerDrawPixelVariables;
	ID3D11Buffer *PerDrawPixelVariablesRef;

	ID3D11Texture2D *NSWETexture;
	ID3D11ShaderResourceView *NSWEView;

	ID3D11Texture2D *L2MapTexture;
	ID3D11ShaderResourceView *L2MapTextureView;
	ID3D11Buffer *L2MapVertices;
	XMVECTOR L2MapScreenPoint, L2MapScale, L2MapPlayerPosition;

	ID3D11Buffer *PathFindMarkerBuffer;

	ID3D11SamplerState *PointSampler;
	ID3D11SamplerState *SmoothSampler;

	ID3D11RasterizerState *SolidMode;
	ID3D11RasterizerState *WireFrameMode;

	// for input lag reduction
	ID3D11Query *SyncQuery;

	// Scene loader

	static const int MAX_VERTICES = 700 * 1000 * 10;
	static const int MAX_INDEXES = MAX_VERTICES * 2;
	static const int BUFFERS_COUNT = 3;
	static const int THREADS_COUNT = BUFFERS_COUNT * 2;

	static const int REGION_SIZE = L2Geodata::GEO_REGION_SIZE / 4;

	struct ModelBuffer {
		GeodataVertex* VertexBuffer;
		uint32_t* IndexBuffer;
	};

	stack<ModelBuffer> BufferPool;
	uint32_t AvailableTexturesLoadCount;
	PTP_POOL ExecutionPool;

	struct ScheduledGeneration {

		POINT Region;

		bool IsModelScheduled, IsTextureScheduled, IsModelDone, IsTextureDone;
	};

	vector<ScheduledGeneration> ScheduledRegions;

	// DirectX Scene definition

	struct RegionModel {

		ID3D11Buffer *VertexBuffer, *IndexBuffer;
		uint32_t VertexCount, IndexCount;

		ID3D11Texture2D *Texture;
		ID3D11ShaderResourceView *TextureView;

		bool IsModelDone, IsTextureDone;

		void Free(void);
	};

	typedef RegionModel RegionModels[3][3];

	POINT CenterRegion;
	RegionModels Regions;

	float ScaleWorld, ScaleWorldZ;

	// GUI

	XMFLOAT3 CameraPosition;
	XMFLOAT2 CameraAngle;
	XMVECTOR TargetVector;
	XMVECTOR Up;

	bool DrawTrianglesAsLines;
	bool UseVSync = true;
	bool IsInFocus;
	bool NSWETextureEnabled;
	bool DrawMap;

#define KEYS_COUNT 256
	bool PressedKeys[KEYS_COUNT];

	float MoveSpeed = 30.0f;

	// Window and Input events processing

	void ProcessWindowState(void);
	void ProcessMouseInput(LONG dx, LONG dy);
	void ProcessKeyboardInput(double dt);

	bool HandleKeyPress(int Key);
	void PrintCurrentCoord(void);
	void SwitchNSWETextureMode(void);

	// Window Callback

	LRESULT WndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
	static LRESULT CALLBACK WndProcCallback(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);

	// 3D Utils

	void CalcL2MapPosition(void);

	void UpdateAllPerDrawVariables(void);

	void BuildViewMatrix(void);
	void BuildOrthogonalMatrix(unsigned int Width, unsigned int Height);
	void UpdatePersistentVertexVariables(void);

	void GenerateNSWETexture(void);
	void LoadL2Map(unsigned int Width, unsigned int Height);
	void GeneratePathFindMarkers(void);

	void DrawScene(void);

	// Scene loader

	struct ModelGenerationRequest {

		// input
		int RegionX;
		int RegionY;
		ModelBuffer Buffer;

		// output
		ID3D11Buffer *VertexBuffer, *IndexBuffer;
		uint32_t VertexCount, IndexCount;
	};

	struct TextureLoadRequest {

		// input
		int RegionX;
		int RegionY;
		int LayerIndex;

		// output
		ID3D11Texture2D* Texture;
		ID3D11ShaderResourceView *TextureView;
	};

	float ToScene(float F);
	void ToScene(int X, int Y, int Z, float& FX, float& FY, float& FZ);
	void ToInt(float FX, float FY, float FZ, int32_t& X, int32_t& Y, int32_t& Z);

	void TeleportTo(int WorldX, int WorldY, int WorldZ);
	void ScheduleModelGeneration(int RegionX, int RegionY, ModelBuffer Buffer);
	void ScheduleTextureLoad(int RegionX, int RegionY, int LayerIndex);
	int GetGenerationIndexByRegion(int RegionX, int RegionY, bool AutoCreate);
	void ScheduleRegionLoad(RegionModel* Region, int RegionX, int RegionY);
	void ScheduleRegions(void);
	void CheckRegions(bool ForceSchedule = false);
	void CleanupGeneration(int GenerationIndex);

	static const int WM_SCHEDULED_RESULT = WM_USER + 5;
	static const int ID_MODEL_GENERATION = 1;
	static const int ID_TEXTURE_LOAD     = 2;

	void ModelGenerationWork(ModelGenerationRequest* Request);
	void TextureLoadWork(TextureLoadRequest* Request);

	void ReadModelGenerationResult(ModelGenerationRequest* Request);
	void ReadTextureLoadResult(TextureLoadRequest* Request);

	static VOID NTAPI ModelGenerationWorkCallback(PTP_CALLBACK_INSTANCE Instance, PVOID Context, PTP_WORK Work);
	static VOID NTAPI TextureLoadWorkCallback(PTP_CALLBACK_INSTANCE Instance, PVOID Context, PTP_WORK Work);
public:
	void Init(unsigned int Width, unsigned int Height, WCHAR *WindowClass, WCHAR *Title, HINSTANCE hInstance);
	void Show(void);
	void WaitForNextFrame(void);
	void Tick(double dt);
};