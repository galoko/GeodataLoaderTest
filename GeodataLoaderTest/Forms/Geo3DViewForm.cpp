#include "stdafx.h"

#undef USE_TOOTLE

#include "Geo3DViewForm.h"

#include "FormsUtils.h"
#include "TimeUtils.h"

#include <stdexcept>
#include <iostream>
#include <D3Dcompiler.h>

#ifdef USE_TOOTLE
#include "tootlelib.h"
#endif

#include <algorithm>

#pragma comment (lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

#undef DEBUG_USE_RANDOM_COLORS
#undef NO_LIGHTING
#undef VERBOSE_LOG

// Window

static unsigned int Width, Height;
static HWND WindowHandle;

// DirectX 11 

#pragma pack(push,1)
struct InputVertex
{
	XMFLOAT3 Pos;
	XMFLOAT3 Color;
	XMFLOAT3 Tex;

	InputVertex() {
	}

	InputVertex(float x, float y, float z, float r, float g, float b) : Pos(x, y, z), Color(r, g, b) {
	}
};
#pragma pack(pop)

static ID3D11Device *DirectDevice;
static ID3D11DeviceContext *DirectDeviceCtx;
static IDXGISwapChain *SwapChain;

static ID3D11VertexShader *VertexShader;
static ID3D11PixelShader *PixelShader;

static ID3D11InputLayout *VertexLayout;

static ID3D11RenderTargetView *RenderTargetView;
static ID3D11Texture2D *DepthStencilBuffer;
static ID3D11DepthStencilView *DepthStencilView;

static XMMATRIX WorldMatrix;
static XMMATRIX ViewMatrix;
static XMMATRIX ProjectionMatrix;
static ID3D11Buffer *ShaderMatrixRef;

static ID3D11Texture2D *NSWETexture;
static ID3D11ShaderResourceView *NSWEView;

static ID3D11Buffer *SceneVertexBuffer;
static ID3D11Buffer *SceneIndexBuffer;

static ID3D11SamplerState *Sampler;

static ID3D11RasterizerState *SolidMode;
static ID3D11RasterizerState *WireFrameMode;

// for input lag reduction
static ID3D11Query *SyncQuery;

// DirectX Scene definition

#define MAX_VERTICES (700 * 1000 * 22)
static InputVertex *VertexBuffer;
static uint32_t NextVertexIndex;

#define MAX_INDEXES (MAX_VERTICES * 2)
static uint32_t *IndexBuffer;
static uint32_t NextIndexIndex;

// GUI

static XMFLOAT3 CameraPosition;
static XMFLOAT2 CameraAngle;
static XMVECTOR TargetVector;
static XMVECTOR Up;

static bool DrawTrianglesAsLines;
static bool UseVSync = true;
static bool IsInFocus;

#define KEYS_COUNT 256
static bool PressedKeys[KEYS_COUNT];

static float MoveSpeed = 30.0f;

// 3D Model Generation

// Generation Grid definition

static uint32_t GridWorldX;
static uint32_t GridWorldY;
static uint32_t GridWidth;
static uint32_t GridHeight;

// Grid that hold every cell and layer usage inside the grid
static uint8_t *GridUsageMap;

// Flood Fill's stack

static POINT *FloodFillStack;
static uint32_t FloodFillStackSize;
static int32_t FloodFillStackIndex;

// Point usage counter table, gets filled by Flood Fill algorithm

#pragma pack(push,1)
struct UsageInfo {
	bool IsSet : 1;
	uint8_t UsageCount : 3;
};
#pragma pack(pop)

static uint32_t PointUsageMapWidth;
static uint32_t PointUsageMapHeight;
static int32_t PointUsageMapGridX;
static int32_t PointUsageMapGridY;
static UsageInfo *PointUsageMap;
static RECT PointUsageMapUsedBoundBox;

// Points generated based on Point Usage map, used by EarCut algorithm

// EarCut input
static vector<vector<Point>> SeparatedPoints;
// EarCut output index for this list
static vector<Point> Points;
// EarCut also output Indices
static vector<uint32_t> Indices;

static XMVECTOR LightDirection;

inline POINT CrossProduct(POINT P, int S) {
	return { -P.y * S, P.x * S };
}

inline POINT AddPoint(POINT P1, POINT P2) {
	return { P1.x + P2.x, P1.y + P2.y };
}

inline POINT NegatePoint(POINT P) {
	return { -P.x, -P.y };
}

inline bool Equals(POINT P1, POINT P2) {
	return (P1.x == P2.x && P1.y == P2.y);
}

inline POINT ToZeroBasePoint(POINT P) {
	return { (P.x + 1) / 2, (P.y + 1) / 2 };
}

template <typename T> int Sign(T val) {
	return (T(0) < val) - (val < T(0));
}

inline int GetDirection(int16_t from, int16_t to) {
	return Sign((int32_t)to - (int32_t)from);
}

struct HeightRange {
	int Direction;
	int16_t Start, End;

	HeightRange() {
	}

	HeightRange(int16_t Start, int16_t End) {

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
	}

	bool SortedOverlapTestWith(const HeightRange* OtherRange) {
		return (OtherRange->Direction == this->Direction && OtherRange->Start <= this->End);
	}

	bool OverlapTestWith(const HeightRange* OtherRange) {
		return (OtherRange->Direction == this->Direction && OtherRange->Start < this->End && OtherRange->End > this->Start);
	}

	bool DirectionInvariantOverlapTestWith(const HeightRange* OtherRange) {
		return (OtherRange->Start < this->End && OtherRange->End > this->Start);
	}
		 
	void MergeWith(const HeightRange* OtherRange) {
		this->End = max(OtherRange->End, this->End);
	}

	int16_t GetMeanHeight(void) {
		return (int16_t) (((int32_t)Start + (int32_t)End) / 2);
	}
};

bool operator < (const HeightRange& Left, const HeightRange& Right) {

	int32_t LeftWeight = (int32_t)Left.Direction * 100000 + (int32_t)Left.Start;
	int32_t RightWeight = (int32_t)Right.Direction * 100000 + (int32_t)Right.Start;

	return LeftWeight < RightWeight;
}

void Geo3DViewForm::Init(unsigned int Width, unsigned int Height, WCHAR *WindowClass, WCHAR *Title,
	HINSTANCE hInstance) {

	// creating window

	RegisterClass(WindowClass, WndProc, hInstance);

	POINT WindowPos;
	WindowPos.x = (GetSystemMetrics(SM_CXSCREEN) - Width) / 2;
	WindowPos.y = (GetSystemMetrics(SM_CYSCREEN) - Height) / 2;

	WindowHandle = CreateWindowW(WindowClass, Title, WS_POPUP, WindowPos.x, WindowPos.y, Width, Height, nullptr, nullptr, hInstance, nullptr);
	if (WindowHandle == 0)
		throw new std::runtime_error("Couldn't create window");

	DXGI_SWAP_CHAIN_DESC sd = {};
	sd.BufferCount = 1;
	sd.BufferDesc.Width = Width;
	sd.BufferDesc.Height = Height;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferDesc.RefreshRate.Numerator = 0;
	sd.BufferDesc.RefreshRate.Denominator = 0;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow = WindowHandle;
	sd.SampleDesc.Count = 1;
	sd.SampleDesc.Quality = 0;
	sd.Windowed = TRUE;

	HRESULT res;

	// creating directx 11 device and swap chain
	res = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, 0, D3D11_CREATE_DEVICE_SINGLETHREADED, NULL, 0,
		D3D11_SDK_VERSION, &sd, &SwapChain, &DirectDevice, NULL, &DirectDeviceCtx);
	if (FAILED(res))
		throw new std::runtime_error("Couldn't create DX device and swap chain");

	// Setup the viewport
	D3D11_VIEWPORT vp = {};
	vp.Width = (float)Width;
	vp.Height = (float)Height;
	vp.MinDepth = 0;
	vp.MaxDepth = 1;
	vp.TopLeftX = 0;
	vp.TopLeftY = 0;
	DirectDeviceCtx->RSSetViewports(1, &vp);

	// loading shader from resources
	HRSRC hShaderRes = FindResource(0, L"texture_shader", RT_RCDATA);
	HANDLE hShaderResGlobal = LoadResource(0, hShaderRes);
	LPVOID ShaderData = LockResource(hShaderResGlobal);
	DWORD ShaderSize = SizeofResource(0, hShaderRes);

	// compiling and creating vertex shader
	ID3DBlob *VSBlob;
	ID3DBlob *Errors;
	res = D3DCompile(ShaderData, ShaderSize, NULL, NULL, NULL, "VS", "vs_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &VSBlob, &Errors);
	if (FAILED(res)) {

		string ErrorText = string((const char *)Errors->GetBufferPointer(), Errors->GetBufferSize());

		throw new std::runtime_error("Couldn't compile vertex shader");
	}

	res = DirectDevice->CreateVertexShader(VSBlob->GetBufferPointer(), VSBlob->GetBufferSize(), NULL, &VertexShader);
	if (FAILED(res))
		throw new std::runtime_error("Couldn't create vertex shader");

	// compiling and creating pixel shader
	ID3DBlob *PSBlob;
	res = D3DCompile(ShaderData, ShaderSize, NULL, NULL, NULL, "PS", "ps_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &PSBlob, NULL);
	if (FAILED(res))
		throw new std::runtime_error("Couldn't compile pixel shader");

	res = DirectDevice->CreatePixelShader(PSBlob->GetBufferPointer(), PSBlob->GetBufferSize(), NULL, &PixelShader);
	if (FAILED(res))
		throw new std::runtime_error("Couldn't create pixel shader");

	// creating input layout
	static const D3D11_INPUT_ELEMENT_DESC VertexLayoutDesc[3] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 }
	};

	res = DirectDevice->CreateInputLayout(VertexLayoutDesc, sizeof(VertexLayoutDesc) / sizeof(*VertexLayoutDesc),
		VSBlob->GetBufferPointer(), VSBlob->GetBufferSize(), &VertexLayout);
	if (FAILED(res))
		throw new std::runtime_error("Couldn't create input layout");

	// Create a render target view
	ID3D11Texture2D *BackBuffer;
	res = SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&BackBuffer);
	if (FAILED(res))
		throw new std::runtime_error("Couldn't get back buffer");

	res = DirectDevice->CreateRenderTargetView(BackBuffer, NULL, &RenderTargetView);
	if (FAILED(res))
		throw new std::runtime_error("Couldn't create render target");

	// creating depth stencil
	D3D11_TEXTURE2D_DESC DepthStencilDesc;

	DepthStencilDesc.Width = Width;
	DepthStencilDesc.Height = Height;
	DepthStencilDesc.MipLevels = 1;
	DepthStencilDesc.ArraySize = 1;
	DepthStencilDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;

	DepthStencilDesc.SampleDesc.Count = 1;
	DepthStencilDesc.SampleDesc.Quality = 0;

	DepthStencilDesc.Usage = D3D11_USAGE_DEFAULT;
	DepthStencilDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	DepthStencilDesc.CPUAccessFlags = 0;
	DepthStencilDesc.MiscFlags = 0;

	res = DirectDevice->CreateTexture2D(&DepthStencilDesc, 0, &DepthStencilBuffer);
	if (FAILED(res))
		throw new std::runtime_error("Couldn't create depth stencil buffer");

	res = DirectDevice->CreateDepthStencilView(DepthStencilBuffer, 0, &DepthStencilView);
	if (FAILED(res))
		throw new std::runtime_error("Couldn't create depth stencil view");

	// setup cull mode

	D3D11_RASTERIZER_DESC CullModeDesc = { };
	CullModeDesc.CullMode = D3D11_CULL_FRONT;
	CullModeDesc.FrontCounterClockwise = true;

	CullModeDesc.FillMode = D3D11_FILL_SOLID;
	res = DirectDevice->CreateRasterizerState(&CullModeDesc, &SolidMode);
	if (FAILED(res))
		throw new std::runtime_error("Couldn't create solid cullmode");

	CullModeDesc.FillMode = D3D11_FILL_WIREFRAME;
	res = DirectDevice->CreateRasterizerState(&CullModeDesc, &WireFrameMode);
	if (FAILED(res))
		throw new std::runtime_error("Couldn't create wireframe cullmode");

	DirectDeviceCtx->RSSetState(SolidMode);

	// creating buffers

	D3D11_BUFFER_DESC SceneVertexBufferDesc = {};
	SceneVertexBufferDesc.ByteWidth = sizeof(InputVertex) * MAX_VERTICES;
	SceneVertexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	SceneVertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	res = DirectDevice->CreateBuffer(&SceneVertexBufferDesc, NULL, &SceneVertexBuffer);
	if (FAILED(res))
		throw new std::runtime_error("Couldn't create vertex buffer");

	D3D11_BUFFER_DESC SceneIndexBufferDesc = {};
	SceneIndexBufferDesc.ByteWidth = sizeof(uint32_t) * MAX_INDEXES;
	SceneIndexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	SceneIndexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	res = DirectDevice->CreateBuffer(&SceneIndexBufferDesc, NULL, &SceneIndexBuffer);
	if (FAILED(res))
		throw new std::runtime_error("Couldn't create index buffer");

	// create shader variable reference
	D3D11_BUFFER_DESC MatrixDesc = {};
	MatrixDesc.Usage = D3D11_USAGE_DEFAULT;
	MatrixDesc.ByteWidth = sizeof(XMFLOAT4X4);
	MatrixDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	res = DirectDevice->CreateBuffer(&MatrixDesc, NULL, &ShaderMatrixRef);
	if (FAILED(res))
		throw new std::runtime_error("Couldn't create shader matrix buffer");

	DirectDeviceCtx->VSSetConstantBuffers(0, 1, &ShaderMatrixRef);

	// setup frame
	DirectDeviceCtx->OMSetRenderTargets(1, &RenderTargetView, DepthStencilView);

	DirectDeviceCtx->IASetInputLayout(VertexLayout);

	DirectDeviceCtx->VSSetShader(VertexShader, 0, 0);
	DirectDeviceCtx->PSSetShader(PixelShader, 0, 0);

	UINT stride = sizeof(InputVertex);
	UINT offset = 0;
	DirectDeviceCtx->IASetVertexBuffers(0, 1, &SceneVertexBuffer, &stride, &offset);

	DirectDeviceCtx->IASetIndexBuffer(SceneIndexBuffer, DXGI_FORMAT_R32_UINT, 0);

	// setup camera

	CameraAngle.y = 3;
	CameraAngle.x = 0;

	CameraPosition.z = -269.5f;
	// CameraPosition.z = 999.8f;

	Up = XMVectorSet(0, 0, 1, 1);

	// setup matrices

	ProjectionMatrix = XMMatrixPerspectiveFovLH(0.5f * (float)M_PI, (float)Width / (float)Height, 0.1f, 4500.0f);

	BuildViewMatrix();
	BuildWorldMatrix();

	BuildShaderMatrix();

	VertexBuffer = new InputVertex[MAX_VERTICES];
	IndexBuffer = new uint32_t[MAX_INDEXES];

	LightDirection = -XMVector3Normalize(XMVectorSet(-0.512651205f, 0.189535633f, -0.837415695f, 1.0f));

	// creating sampler

	D3D11_SAMPLER_DESC SamplerDesc = { };
	SamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	SamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	SamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	SamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	SamplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	SamplerDesc.MinLOD = 0;
	SamplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
	res = DirectDevice->CreateSamplerState(&SamplerDesc, &Sampler);
	if (FAILED(res))
		throw new runtime_error("Couldn't create sampler");

	DirectDeviceCtx->PSSetSamplers(0, 1, &Sampler);

	GenerateNSWETexture();

#ifdef USE_TOOTLE
	TootleInit();
#endif

	srand(57);

	// GenerateDebugStaticScene();
	// GenerateDebugGeodataScene();
	GenerateGeodataScene(13100, 140572, 16 * 700, 16 * 700);
	// GenerateGeodataScene(79625, 143498, 16 * 500, 16 * 500);
	// GenerateGeodataScene(109780, 11200, 16 * 800, 16 * 800);
	// GenerateGeodataScene(112964, 39885, 16 * 800, 16 * 800);
	// GenerateGeodataScene(109780 + 16 * 202, 11200 + 16 * 156, 16 * 1, 16 * 2);
	// GenerateGeodataScene(109780 + 16 * 385, 11200 + 16 * 149, 16 * 2, 16 * 1);
	// GenerateGeodataScene(13100 + 16 * 253, 140572 + 16 * 267, 16 * 2, 16 * 1);
	// GenerateGeodataScene(13100 + 16 * 79, 140572 + 16 * 120, 16 * 2, 16 * 2);
	// GenerateGeodataScene(13100 + 16 * 1500, 140572 + 16 * 1500, 16 * 2500, 16 * 2500);
	// GenerateGeodataScene(13100 + 16 * 137, 140572 + 16 * 73, 16 * 20, 16 * 10);
	// GenerateGeodataScene(13100, 140572, 16 * 4, 16 * 4);
	// GenerateGeodataScene(13100 + 15 * 16 - 3 * 16, 140572 + 272 * 16 - 3 * 16, 2 * 16, 2 * 16);

	// register raw input
	RAWINPUTDEVICE Rid[1] = {};

	Rid[0].usUsagePage = 0x01;
	Rid[0].usUsage = 0x02;
	Rid[0].dwFlags = RIDEV_INPUTSINK;   // adds HID mouse and also ignores legacy mouse messages
	Rid[0].hwndTarget = WindowHandle;

	if (!RegisterRawInputDevices(Rid, 1, sizeof(Rid[0])))
		throw new std::runtime_error("Couldn't register raw devices");
}

void Geo3DViewForm::GenerateDebugStaticScene(void) {

	uint32_t Vertices[4];
	Vertices[0] = AllocateVertexIndex();
	Vertices[1] = AllocateVertexIndex();
	Vertices[2] = AllocateVertexIndex();
	Vertices[3] = AllocateVertexIndex();

	VertexBuffer[Vertices[0]] = InputVertex(0.0f, 0.0f, -1.0f, 0, 0, 0);
	VertexBuffer[Vertices[1]] = InputVertex(1.0f, 0.0f, -1.0f, 0, 1, 0);
	VertexBuffer[Vertices[2]] = InputVertex(0.0f, 1.0f, -1.0f, 1, 0, 0);
	VertexBuffer[Vertices[3]] = InputVertex(1.0f, 1.0f, -1.0f, 1, 1, 1);

	AllocateIndexIndex(Vertices[0]);
	AllocateIndexIndex(Vertices[1]);
	AllocateIndexIndex(Vertices[2]);

	AllocateIndexIndex(Vertices[1]);
	AllocateIndexIndex(Vertices[2]);
	AllocateIndexIndex(Vertices[3]);

	CommitScene();
}

void Geo3DViewForm::PrintCurrentCoord(void)
{
	POINT Coord = { (LONG)floor(CameraPosition.x), (LONG)floor(CameraPosition.y) };

	cout << Coord.x << ", " << Coord.y << " | " << CameraPosition.z << endl;
}

bool Geo3DViewForm::HandleKeyPress(int Key)
{
	switch (Key) {
	case  VK_ESCAPE:
		PostQuitMessage(0);
		break;
	case 'U':
		PrintCurrentCoord();
		break;
	case 'V':
		UseVSync = !UseVSync;
		break;
	default:
		return false;
	}

	return true;
}

bool TestNSWE(int NSWE, int Mask) {

	return ((NSWE & Mask) == Mask);
}

int DumpBMP32(uint8_t* pixels, int width, int height, const char* filePath) {

	FILE *bmpFile = NULL;
	int fileSize = 54 + 4 * width * height;

	unsigned char bmpfileheader[14] = { 'B','M', 0,0,0,0, 0,0, 0,0, 54,0,0,0 };
	unsigned char bmpinfoheader[40] = { 40,0,0,0, 0,0,0,0, 0,0,0,0, 1,0, 32,0 };
	unsigned char bmppad[3] = { 0,0,0 };

	bmpfileheader[2] = (unsigned char)(fileSize);
	bmpfileheader[3] = (unsigned char)(fileSize >> 8);
	bmpfileheader[4] = (unsigned char)(fileSize >> 16);
	bmpfileheader[5] = (unsigned char)(fileSize >> 24);

	bmpinfoheader[4] = (unsigned char)(width);
	bmpinfoheader[5] = (unsigned char)(width >> 8);
	bmpinfoheader[6] = (unsigned char)(width >> 16);
	bmpinfoheader[7] = (unsigned char)(width >> 24);
	bmpinfoheader[8] = (unsigned char)(height);
	bmpinfoheader[9] = (unsigned char)(height >> 8);
	bmpinfoheader[10] = (unsigned char)(height >> 16);
	bmpinfoheader[11] = (unsigned char)(height >> 24);

	fopen_s(&bmpFile, filePath, "wb");
	fwrite(bmpfileheader, 1, sizeof(bmpfileheader), bmpFile);
	fwrite(bmpinfoheader, 1, sizeof(bmpinfoheader), bmpFile);
	for (int i = 0; i<height; i++)
		fwrite(pixels + (width * (height - i - 1) * 4), 4, (size_t)width, bmpFile);

	fclose(bmpFile);

	return 1;
}

#define NSWE_TEX_WIDTH 16
#define NSWE_TEX_HEIGHT 16 * NSWE_TEX_WIDTH

void Geo3DViewForm::GenerateNSWETexture(void)
{
	HRESULT res;

	uint32_t Pixels[NSWE_TEX_HEIGHT][NSWE_TEX_WIDTH];
	memset(&Pixels[0][0], 0xFF, sizeof(Pixels));

	for (int X = 0; X < NSWE_TEX_WIDTH; X++)
		for (int Y = 0; Y < NSWE_TEX_HEIGHT; Y++) {

			int CellX = X % NSWE_TEX_WIDTH;
			int CellY = Y % NSWE_TEX_WIDTH;

			if (CellX == 0 || CellX == NSWE_TEX_WIDTH - 1 || CellY == 0 || CellY == NSWE_TEX_WIDTH - 1)
				Pixels[Y][X] = 0xFF00FF00;
		}


	for (int NSWE = 0; NSWE <= 15; NSWE++) {

		int Y = (15 - NSWE) * NSWE_TEX_WIDTH;

		if (!TestNSWE(NSWE, L2Geodata::EAST)) {

			int X = NSWE_TEX_WIDTH - 1;

			for (int OffsetY = 1; OffsetY < NSWE_TEX_WIDTH - 1; OffsetY++)
				Pixels[Y + OffsetY][X + 0] = 0xFFFF0000;
		}

		if (!TestNSWE(NSWE, L2Geodata::WEST)) {

			int X = 0;

			for (int OffsetY = 1; OffsetY < NSWE_TEX_WIDTH - 1; OffsetY++)
				Pixels[Y + OffsetY][X + 0] = 0xFFFF0000;
		}

		if (!TestNSWE(NSWE, L2Geodata::NORTH)) {

			for (int XOffset = 1; XOffset < NSWE_TEX_WIDTH - 1; XOffset++)
				Pixels[Y + 0][0 + XOffset] = 0xFFFF0000;
		}

		if (!TestNSWE(NSWE, L2Geodata::SOUTH)) {

			for (int OffsetX = 1; OffsetX < NSWE_TEX_WIDTH - 1; OffsetX++)
				Pixels[Y + NSWE_TEX_WIDTH - 1][0 + OffsetX] = 0xFFFF0000;
		}
	}

	D3D11_SUBRESOURCE_DATA InitialData = {};
	InitialData.pSysMem = &Pixels[0][0];
	InitialData.SysMemPitch = sizeof(Pixels[0]);

	D3D11_TEXTURE2D_DESC TextureDesc = {};
	TextureDesc.Width = NSWE_TEX_WIDTH;
	TextureDesc.Height = NSWE_TEX_HEIGHT;
	TextureDesc.MipLevels = 1;
	TextureDesc.ArraySize = 1;
	TextureDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	TextureDesc.SampleDesc.Count = 1;
	TextureDesc.SampleDesc.Quality = 0;
	TextureDesc.Usage = D3D11_USAGE_DEFAULT;
	TextureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	TextureDesc.CPUAccessFlags = 0;

	res = DirectDevice->CreateTexture2D(&TextureDesc, &InitialData, &NSWETexture);
	if (FAILED(res))
		throw new runtime_error("Couldn't create NSWE texture");

	res = DirectDevice->CreateShaderResourceView(NSWETexture, NULL, &NSWEView);
	if (FAILED(res))
		throw new runtime_error("Couldn't create NSWE texture");

	DirectDeviceCtx->PSSetShaderResources(0, 1, &NSWEView);

	// DumpBMP32((uint8_t*)&Pixels[0][0], NSWE_TEX_WIDTH, NSWE_TEX_HEIGHT, "H:\\test.bmp");
}

void Geo3DViewForm::GenerateDebugGeodataScene(void)
{
	srand(11);

	for (int X = -10; X <= 10; X++)
		for (int Y = -10; Y <= 10; Y++) {

			int Offset = abs(X) | abs(Y);

			L2Geodata::SetSubBlocks(13100 + X * 16, 140572 + Y * 16, 1, MAKE_SUBBLOCK(-2796, 0));
		}

	L2Geodata::SetSubBlocks(13100 + 16 * 1, 140572 + 16 * 1, 1, MAKE_SUBBLOCK(-2796 + 16 * 1, 0));
	L2Geodata::SetSubBlocks(13100 + 16 * 2, 140572 + 16 * 2, 1, MAKE_SUBBLOCK(-2796 + 16 * 1, 0));

	// L2Geodata::SetSubBlocks(13100 + 16 * 1, 140572 + 16 * 2, 1, MAKE_SUBBLOCK(-2796 + 16 * 4, 0));
	// L2Geodata::SetSubBlocks(13100 + 16 * 1, 140572 + 16 * 1, 2, MAKE_SUBBLOCK(-2796 + 16 * 0, 0), MAKE_SUBBLOCK(-2796 + 16 * 5, 0));

	/*
	L2Geodata::SetSubBlocks(13100 + 16 * 2, 140572 + 16 * 1, 1, MAKE_SUBBLOCK(-2796 + 16 * 5, 0));
	L2Geodata::SetSubBlocks(13100 + 16 * 1, 140572 + 16 * 1, 1, MAKE_SUBBLOCK(-2796 + 16 * 4, 0));
	L2Geodata::SetSubBlocks(13100 + 16 * 3, 140572 + 16 * 1, 1, MAKE_SUBBLOCK(-2796 + 16 * 3, 0));
	
	L2Geodata::SetSubBlocks(13100 + 16 * 1, 140572 + 16 * 2, 1, MAKE_SUBBLOCK(-2796 + 16 * 2, 0));
	L2Geodata::SetSubBlocks(13100 + 16 * 3, 140572 + 16 * 2, 1, MAKE_SUBBLOCK(-2796 + 16 * 1, 0));
	 */

	/*
	L2Geodata::SetSubBlocks(13100 + 16 * 2, 140572 + 16 * 1, (-2796 + (16 * 1)) << 1);
	L2Geodata::SetSubBlocks(13100 + 16 * 3, 140572 + 16 * 2, (-2796 + (16 * 1)) << 1);
	 */

	GenerateGeodataScene(13100, 140572, 5 * 16, 5 * 16);
}

void Geo3DViewForm::BuildViewMatrix(void)
{
	float x = sinf(CameraAngle.y) * sinf(CameraAngle.x);
	float y = sinf(CameraAngle.y) * cosf(CameraAngle.x);
	float z = cosf(CameraAngle.y);

	TargetVector = XMVectorSet(x, y, z, 1);

	XMVECTOR Position = XMVectorSet(CameraPosition.x, CameraPosition.y, CameraPosition.z, 1);
	XMVECTOR Target = Position + TargetVector;

	ViewMatrix = XMMatrixLookAtLH(Position, Target, Up);
}

void Geo3DViewForm::BuildWorldMatrix(void)
{
	XMVECTOR zero = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
	XMVECTOR one = XMVectorSet(1.0f, 1.0f, 1.0f, 1.0f);
	XMVECTOR position = XMVectorSet(CameraPosition.x, CameraPosition.y, CameraPosition.z, 0.0f);

	WorldMatrix = XMMatrixAffineTransformation(one, zero, zero, position);
}

void Geo3DViewForm::BuildShaderMatrix(void)
{
	XMMATRIX FinalMatrix = XMMatrixTranspose(ViewMatrix * ProjectionMatrix);

	DirectDeviceCtx->UpdateSubresource(ShaderMatrixRef, 0, NULL, &FinalMatrix, 0, 0);
}

uint32_t Geo3DViewForm::AllocateVertexIndex(void)
{
	if (NextVertexIndex >= MAX_VERTICES)
		throw new std::runtime_error("Vertices limit is reached");

	uint32_t Ret = NextVertexIndex;
	NextVertexIndex++;

	return Ret;
}

uint32_t Geo3DViewForm::AllocateIndexIndex(uint32_t Index)
{
	if (NextIndexIndex >= MAX_INDEXES)
		throw new std::runtime_error("Indexes limit is reached");

	uint32_t Ret = NextIndexIndex;
	NextIndexIndex++;

	IndexBuffer[Ret] = Index;

	return Ret;
}

void Geo3DViewForm::ResetScene(void)
{
	NextVertexIndex = 0;
	NextIndexIndex = 0;
}

#ifdef USE_TOOTLE
void DisplayTootleErrorMessage(TootleResult Result, string OperationName)
{
	cout << "Tootle returned error from " + OperationName + ": " << endl;

	switch (Result)
	{
	case NA_TOOTLE_RESULT:
		cout << " NA_TOOTLE_RESULT" << endl;
		break;

	case TOOTLE_OK:
		break;

	case TOOTLE_INVALID_ARGS:
		cout << " TOOTLE_INVALID_ARGS" << endl;
		break;

	case TOOTLE_OUT_OF_MEMORY:
		cout << " TOOTLE_OUT_OF_MEMORY" << endl;
		break;

	case TOOTLE_3D_API_ERROR:
		cout << " TOOTLE_3D_API_ERROR" << endl;
		break;

	case TOOTLE_INTERNAL_ERROR:
		cout << " TOOTLE_INTERNAL_ERROR" << endl;
		break;

	case TOOTLE_NOT_INITIALIZED:
		cout << " TOOTLE_NOT_INITIALIZED" << endl;
		break;
	}
}

void CheckTootleResult(TootleResult Result, string OperationName) {
	if (Result != TOOTLE_OK) {
		DisplayTootleErrorMessage(Result, OperationName);
		throw new runtime_error(OperationName + " failed");
	}
}

#endif

size_t GetHash(uint32_t *Data, int Count) {

	hash<uint32_t> Hasher;

	size_t Result = 0;

	for (int Index = 0; Index < Count; Index++)
		Result = Result * 31 + Hasher(Data[Index]);

	return Result;
}

void Geo3DViewForm::ApplyTootle(void)
{
#ifdef USE_TOOTLE
	std::cout << "Start Tootling stuff" << endl;

	int FaceCount = NextIndexIndex / 3;

	/*
	std::vector<unsigned int> FaceClusters;
	FaceClusters.resize(FaceCount + 1);
	unsigned int ClustersCount;

	cout << "TootleClusterMesh start" << endl;
	CheckTootleResult(TootleClusterMesh(VertexBuffer, IndexBuffer, NextVertexIndex, FaceCount, sizeof(InputVertex), 0, IndexBuffer, &FaceClusters[0],
		NULL), "ClusterMesh");

	cout << "TootleVCacheClusters start" << endl;
	CheckTootleResult(TootleVCacheClusters(IndexBuffer, FaceCount, NextVertexIndex, TOOTLE_DEFAULT_VCACHE_SIZE, &FaceClusters[0], IndexBuffer, NULL,
		TOOTLE_VCACHE_DIRECT3D), "VCacheClusters");

	cout << "TootleOptimizeOverdraw start" << endl;
	CheckTootleResult(TootleOptimizeOverdraw(VertexBuffer, IndexBuffer, NextVertexIndex, FaceCount, sizeof(InputVertex), NULL, 0, TOOTLE_CCW,
		&FaceClusters[0], IndexBuffer, NULL, TOOTLE_OVERDRAW_FAST), "OptimizeOverdraw");
	*/

	
	std::cout << "TootleFastOptimize start" << endl;
	CheckTootleResult(TootleFastOptimize(VertexBuffer, IndexBuffer, NextVertexIndex, FaceCount, sizeof(InputVertex), TOOTLE_DEFAULT_VCACHE_SIZE,
		TOOTLE_CCW, IndexBuffer, NULL, TOOTLE_DEFAULT_ALPHA), "FastOptimize");
	
	/*
	cout << "TootleFastOptimizeVCacheAndClusterMesh start" << endl;
	CheckTootleResult(TootleFastOptimizeVCacheAndClusterMesh(IndexBuffer, FaceCount, NextVertexIndex, TOOTLE_DEFAULT_VCACHE_SIZE, IndexBuffer,
		&FaceClusters[0], &ClustersCount, TOOTLE_DEFAULT_ALPHA), "FastOptimizeVCacheAndClusterMesh");

	cout << "TootleOptimizeOverdraw start" << endl;
	CheckTootleResult(TootleOptimizeOverdraw(VertexBuffer, IndexBuffer, NextVertexIndex, FaceCount, sizeof(InputVertex), NULL, 0,
		TOOTLE_CCW, &FaceClusters[0], IndexBuffer, NULL, TOOTLE_OVERDRAW_FAST), "OptimizeOverdraw");
	*/

	std::cout << "TootleOptimizeVertexMemory start" << endl;
	CheckTootleResult(TootleOptimizeVertexMemory(VertexBuffer, IndexBuffer, NextVertexIndex, FaceCount, sizeof(InputVertex), VertexBuffer, IndexBuffer,
		NULL), "OptimizeVertexMemory");

	// cout << "Start hash: " << StartHash << ", final hash: " << FinalHash << endl;
#endif
}

void Geo3DViewForm::CommitScene(void) {

#ifdef USE_TOOTLE
	ApplyTootle();
#endif

	D3D11_BOX Region = { };
	Region.bottom = 1;
	Region.back = 1;

	Region.right = NextVertexIndex * sizeof(*VertexBuffer);
	DirectDeviceCtx->UpdateSubresource(SceneVertexBuffer, 0, &Region, VertexBuffer, 0, 0);

	Region.right = NextIndexIndex * sizeof(*IndexBuffer);
	DirectDeviceCtx->UpdateSubresource(SceneIndexBuffer, 0, &Region, IndexBuffer, 0, 0);
}

void Geo3DViewForm::GetGeoLayers(int32_t GridX, int32_t GridY, int16_t& LayersCount, int16_t*& Layers)
{
	int32_t X = GridWorldX + GridX * L2Geodata::GEO_COORDS_IN_WORLD_COORDS;
	int32_t Y = GridWorldY + GridY * L2Geodata::GEO_COORDS_IN_WORLD_COORDS;

	Layers = L2Geodata::GetSubBlocks(X, Y, LayersCount);
}

void Geo3DViewForm::FindCorrespondingLayer(int16_t LayersCount, int16_t* Layers, int16_t LayerIndex, int16_t OtherLayersCount, int16_t* OtherLayers,
	int16_t& CorrespondingLayerIndex, int16_t& CorrespondingHeight)
{
	int16_t Height = GET_GEO_HEIGHT(Layers[LayerIndex]);

	uint16_t MinDiff = MAXUINT16;

	CorrespondingLayerIndex = -1;
	CorrespondingHeight = -1;

	for (int16_t OtherLayerIndex = 0; OtherLayerIndex < OtherLayersCount; OtherLayerIndex++) {

		int16_t OtherHeight = GET_GEO_HEIGHT(OtherLayers[OtherLayerIndex]);

		uint16_t Diff = abs((int32_t)OtherHeight - (int32_t)Height);

		if (Diff < MinDiff) {
			CorrespondingLayerIndex = OtherLayerIndex;
			MinDiff = Diff;
		}
	}

	if (CorrespondingLayerIndex == -1) {
		assert(CorrespondingHeight == -1);
		return;
	}

	CorrespondingHeight = GET_GEO_HEIGHT(OtherLayers[CorrespondingLayerIndex]);

	// if some of our layers have better correspondence than ourselves then we do not have any correspondance at all
	for (int16_t Layer2Index = 0; Layer2Index < LayersCount; Layer2Index++) {

		if (Layer2Index == LayerIndex)
			continue;

		int16_t Layer2Height = GET_GEO_HEIGHT(Layers[Layer2Index]);

		uint16_t Diff = abs((int32_t)CorrespondingHeight - (int32_t)Layer2Height);
		if (Diff < MinDiff) {
			CorrespondingLayerIndex = -1;
			CorrespondingHeight = -1;
			return;
		}
	}
}

struct NeighborInfo {
	int16_t LayerIndex, Height;
};

void Geo3DViewForm::GetNeighbors(int GridX, int GridY, int16_t LayersCount, int16_t* Layers, int16_t LayerIndex, NeighborInfo Neighbors[3][3]) {

	for (int NeighborX = -1; NeighborX <= 1; NeighborX++)
		for (int NeighborY = -1; NeighborY <= 1; NeighborY++) {

			if (NeighborX == 0 && NeighborY == 0) {
				Neighbors[NeighborX + 1][NeighborY + 1] = { LayerIndex, GET_GEO_HEIGHT(Layers[LayerIndex]) };
				continue;
			}

			int16_t NeighborLayerCount;
			int16_t *NeighborLayers;

			GetGeoLayers(GridX + NeighborX, GridY + NeighborY, NeighborLayerCount, NeighborLayers);

			int16_t NeighborLayerIndex, NeighborHeight;
			FindCorrespondingLayer(LayersCount, Layers, LayerIndex, NeighborLayerCount, NeighborLayers, NeighborLayerIndex, NeighborHeight);

			Neighbors[NeighborX + 1][NeighborY + 1] = { NeighborLayerIndex, NeighborHeight };
		}
}

XMFLOAT3 Geo3DViewForm::BakeLightColor(XMVECTOR TextureColor, XMVECTOR Normal)
{
	XMVECTOR AmbientColor = { 0.125f, 0.125f, 0.75f, 1.0f };
	XMVECTOR DiffuseColor = { 0.5f, 0.5f, 75.0f, 1.0f };

	XMVECTOR Color = AmbientColor;

	float LightIntensity = min(max(0.0f, XMVector3Dot(Normal, LightDirection).m128_f32[0]), 1.0f);

	if (LightIntensity > 0.0f)
	{
		// Determine the final diffuse color based on the diffuse color and the amount of light intensity.
		Color += DiffuseColor * LightIntensity;
	}

	Color = XMVectorClamp(Color, { 0, 0, 0, 0 }, { 1, 1, 1, 1 });

	Color *= TextureColor;

	XMFLOAT3 Result;
	XMStoreFloat3(&Result, Color);

	return Result;
}

float GetRandomFloat(void) {
	return (float)rand() / RAND_MAX;
}

float GetRandomCoord(void) {

	return GetRandomFloat() * 2.0f - 1.0f;
}

XMVECTOR GetRandomVector(void) {

	return XMVector3Normalize(XMVectorSet(GetRandomCoord(), GetRandomCoord(), GetRandomCoord(), 1.0f));
}

XMFLOAT3 GetRandomColor(void) {

	XMFLOAT3 Color;
	XMStoreFloat3(&Color, XMVectorSet(GetRandomFloat(), GetRandomFloat(), GetRandomFloat(), 1.0f));
	return Color;
}

bool Geo3DViewForm::GenerateConcaveHull(vector<Point>& Dest, int StartX, int StartY, int TurnDirection)
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

static vector<Point> Hull;

bool Geo3DViewForm::GenerateConcaveHullAndHolesFromUsageMap(void)
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

bool Geo3DViewForm::GenerateModelFromUsageMap(void)
{
	if (!GenerateConcaveHullAndHolesFromUsageMap()) {
		assert(false);
		return false;
	}

	Indices = mapbox::earcut<uint32_t>(SeparatedPoints);

	return true;
}

void Geo3DViewForm::SetupFloodFillStack(int StackSize)
{
	FloodFillStackSize = StackSize;
	FloodFillStack = (POINT*)malloc(FloodFillStackSize * sizeof(POINT));
	FloodFillStackIndex = -1;
}

void Geo3DViewForm::ResetFloodFillStack(void)
{
	FloodFillStackIndex = -1;
}

void Geo3DViewForm::PushGridCell(int GridX, int GridY)
{
	if ((uint32_t)(FloodFillStackIndex + 1) >= FloodFillStackSize)
		throw new runtime_error("Stack overflow");

	FloodFillStackIndex++;
	FloodFillStack[FloodFillStackIndex] = { GridX, GridY };
}

void Geo3DViewForm::PushGridNeighbors(int GridX, int GridY)
{
	PushGridCell(GridX + 1, GridY + 0);
	PushGridCell(GridX - 1, GridY + 0);
	PushGridCell(GridX + 0, GridY + 1);
	PushGridCell(GridX + 0, GridY - 1);
}

void Geo3DViewForm::PushHeightRangeNeighborsOneSide(int GridX, int GridY, int OffsetX, int OffsetY, POINT Direction, HeightRange* Range)
{
	bool DynamicX = Direction.x != 0;

	POINT NeighborPoint = AddPoint({ GridX, GridY } , Direction);

	HeightRange NeighboRanges[MAX_HEIGHT_RANGES_COUNT];
	int NeighboRangesCount;
	
	GetHeightRanges(NeighborPoint.x, NeighborPoint.y, OffsetX, OffsetY, NeighboRanges, NeighboRangesCount);

	for (int NeighborHeightIndex = 0; NeighborHeightIndex < NeighboRangesCount; NeighborHeightIndex++) {

		HeightRange* NeighborRange = &NeighboRanges[NeighborHeightIndex];

		if (Range->OverlapTestWith(NeighborRange))
			PushGridCell(DynamicX ? NeighborPoint.x : NeighborPoint.y, NeighborHeightIndex);
	}
}

void Geo3DViewForm::PushHeightRangeNeighborsBothSides(int GridX, int GridY, int OffsetX, int OffsetY, POINT Direction, HeightRange* Range)
{
	PushHeightRangeNeighborsOneSide(GridX, GridY, OffsetX, OffsetY, Direction, Range);
	PushHeightRangeNeighborsOneSide(GridX, GridY, OffsetX, OffsetY, NegatePoint(Direction), Range);
}

bool Geo3DViewForm::PopStackPoint(POINT & Point)
{
	if (FloodFillStackIndex < 0)
		return false;

	Point = FloodFillStack[FloodFillStackIndex];
	FloodFillStackIndex--;

	return true;
}

void Geo3DViewForm::FinalizeFloodFillStack(void)
{
	free(FloodFillStack);
	FloodFillStack = NULL;

	FloodFillStackSize = 0;
	FloodFillStackIndex = -1;
}

void Geo3DViewForm::SetupPointUsageMap(int MapWidth, int MapHeight)
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

void Geo3DViewForm::ResetPointUsageMap(int CenterGridX, int CenterGridY, bool CenterX, bool CenterY)
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

	PointUsageMapGridX = CenterX ? CenterGridX - (PointUsageMapWidth  / 2) : CenterGridX;
	PointUsageMapGridY = CenterY ? CenterGridY - (PointUsageMapHeight / 2) : CenterGridY;

	PointUsageMapUsedBoundBox = { MAXLONG32, MAXLONG32, MINLONG32, MINLONG32 };
}

void Geo3DViewForm::ApplyGridCellToPointUsageMap(int GridX, int GridY)
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
	PointUsageMapUsedBoundBox.left   = min(PointUsageMapUsedBoundBox.left,   GridX);
	PointUsageMapUsedBoundBox.top    = min(PointUsageMapUsedBoundBox.top,    GridY);
	PointUsageMapUsedBoundBox.right  = max(PointUsageMapUsedBoundBox.right,  GridX + 1 + 1);
	PointUsageMapUsedBoundBox.bottom = max(PointUsageMapUsedBoundBox.bottom, GridY + 1 + 1);
}

inline int GetGridZ(int16_t Height) {

	return Height / L2Geodata::HEIGHT_RESOLUTION;
}

inline int16_t GridZToHeight(int GridZ) {

	return GridZ * L2Geodata::HEIGHT_RESOLUTION;
}

bool Geo3DViewForm::ApplyHeightRangeToPointUsageMap(int GridX, HeightRange* Range)
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

UsageInfo Geo3DViewForm::GetPointUsage(int GridX, int GridY)
{
	assert(IsPointInUsageBound(GridX, GridY));

	int MapX = GridX - PointUsageMapGridX;
	int MapY = GridY - PointUsageMapGridY;
	int Index = GET_USAGE_MAP_INDEX;

	return PointUsageMap[Index];
}

void Geo3DViewForm::SetPointUsageCount(int GridX, int GridY, uint8_t UsageCount)
{
	assert(IsPointInUsageBound(GridX, GridY));
	assert(UsageCount <= 1);

	int MapX = GridX - PointUsageMapGridX;
	int MapY = GridY - PointUsageMapGridY;
	int Index = GET_USAGE_MAP_INDEX;

	PointUsageMap[Index].UsageCount = UsageCount;
}

bool Geo3DViewForm::IsCellInUsageBound(int GridX, int GridY)
{
	int MapX = GridX - PointUsageMapGridX;
	int MapY = GridY - PointUsageMapGridY;

	return (MapX >= 0 && MapX + 1 < (int32_t)PointUsageMapWidth && MapY >= 0 && MapY + 1 < (int32_t)PointUsageMapHeight);
}

bool Geo3DViewForm::IsPointInUsageBound(int GridX, int GridY)
{
	int MapX = GridX - PointUsageMapGridX;
	int MapY = GridY - PointUsageMapGridY;

	return (MapX >= 0 && MapX < (int32_t)PointUsageMapWidth && MapY >= 0 && MapY < (int32_t)PointUsageMapHeight);
}

bool Geo3DViewForm::FindPointByUsageCount(uint8_t UsageCount, POINT& Point)
{
	for (int GridX = PointUsageMapUsedBoundBox.left; GridX < PointUsageMapUsedBoundBox.right; GridX++)
		for (int GridY = PointUsageMapUsedBoundBox.top; GridY < PointUsageMapUsedBoundBox.bottom; GridY++) 
			if (GetPointUsage(GridX, GridY).UsageCount == UsageCount) {
				Point = { GridX, GridY };
				return true;
			}

	return false;
}

void Geo3DViewForm::FinalizePointUsageMap(void)
{
	free(PointUsageMap);
	PointUsageMap = NULL;

	PointUsageMapWidth = 0;
	PointUsageMapHeight = 0;

	PointUsageMapGridX = 0;
	PointUsageMapGridY = 0;
}

static float Epsilon = 10e-5f;

void Geo3DViewForm::AddTopPlaneModel(int16_t SubBlock, int Direction)
{
	XMFLOAT3 Color = BakeLightColor({ 1, 1, 1 }, { 0, 0, (float)Direction });

	int NSWE = GET_GEO_NSWE(SubBlock);
	float TexOffsetY = (float)(15 - NSWE) / 16.0f;

	uint32_t VertexOffset = NextVertexIndex;
	for (int Index = 0; Index < Points.size(); Index++) {

		Point P = Points[Index];

		uint32_t VertexIndex = AllocateVertexIndex();

		InputVertex *Vertex = &VertexBuffer[VertexIndex];

		Vertex->Pos = { (float)P[0], (float)P[1], (float)GET_GEO_HEIGHT(SubBlock) * 0.1f + (float)Direction * Epsilon };
#ifdef DEBUG_USE_RANDOM_COLORS
		Vertex->Color = GetRandomColor();
#else
		Vertex->Color = Color;
#endif
		Vertex->Tex = { (float)P[0], (float)P[1], TexOffsetY };
	}

	if (Direction == 1) {
		for (int Index = 0; Index < Indices.size(); Index++)
			AllocateIndexIndex(VertexOffset + Indices[Index]);
	}
	else {
		for (int Index = (int) Indices.size() - 1; Index >= 0; Index--)
			AllocateIndexIndex(VertexOffset + Indices[Index]);
	}
}

void Geo3DViewForm::GenerateTopPlanes(int GridX, int GridY)
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

		AddTopPlaneModel(SubBlock,  1);
		AddTopPlaneModel(SubBlock, -1);
	}
}

int OffsetToNSWE(int OffsetX, int OffsetY) {

	int Result = 0;

	if (OffsetX ==  1)
		Result |= L2Geodata::EAST;
	else
	if (OffsetX == -1)
		Result |= L2Geodata::WEST;

	if (OffsetY ==  1)
		Result |= L2Geodata::SOUTH;
	else
	if (OffsetY == -1)
		Result |= L2Geodata::NORTH;

	return Result;
}

int32_t GetHeightDiff(int16_t From, int16_t To) {
	return (int32_t)To - (int32_t)From;
}

void Geo3DViewForm::GetLowAndHighLayers(int16_t SubBlock, int16_t* Layers, int16_t LayersCount, int16_t& LowLayerIndex, int16_t& HighLayerIndex) {

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

bool Geo3DViewForm::CanGoInThisDirection(int16_t SubBlock, int DirectionX, int DirectionY)
{
	int NSWE = GET_GEO_NSWE(SubBlock);

	return TestNSWE(NSWE, OffsetToNSWE(DirectionX, DirectionY));
}

bool Geo3DViewForm::CanGoUnderneath(int16_t SubBlock, int16_t HigherSubBlock)
{
	int16_t Height = GET_GEO_HEIGHT(SubBlock);
	int16_t HigherHeight = GET_GEO_HEIGHT(HigherSubBlock);

	return GetHeightDiff(Height, HigherHeight) > L2Geodata::MIN_LAYER_DIFF;
}

bool Geo3DViewForm::GetDestSubBlock(int16_t SubBlock, int OffsetX, int OffsetY, int16_t* Layers, int16_t LayersCount, int16_t& DestSubBlock)
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

			DestSubBlock = HigherSubBlock;
			return true;
		}
	}

	// at this moment we already not struggling with higher subblock
	if (LowLayerIndex != -1) {

		DestSubBlock = Layers[LowLayerIndex];
		return true;
	}

	// or else we cannot go on higher block and don't even have lower block so we kinda stuck
	return false;
}

bool Geo3DViewForm::GetWallDestHeight(int16_t SubBlock, int OffsetX, int OffsetY, int16_t* Layers, int16_t LayersCount, int16_t& DestHeight)
{
	int16_t LowLayerIndex, HighLayerIndex;
	GetLowAndHighLayers(SubBlock, Layers, LayersCount, LowLayerIndex, HighLayerIndex);
	if (HighLayerIndex == -1)
		return false;

	DestHeight = GET_GEO_HEIGHT(Layers[HighLayerIndex]);

	bool CantGoInThisDirection = !CanGoInThisDirection(SubBlock, OffsetX, OffsetY);
	bool CantGoUnderneathHigherLayer = !CanGoUnderneath(SubBlock, Layers[HighLayerIndex]);
	bool IsNothingUnderHigherLayer = LowLayerIndex == -1;

	bool WallRequired = CantGoInThisDirection || CantGoUnderneathHigherLayer || IsNothingUnderHigherLayer;

	return WallRequired;
}

void Geo3DViewForm::GetHeightRanges(int GridX, int GridY, int OffsetX, int OffsetY, HeightRange *DestHeightRanges, int& DestHeightRangesCount)
{
	DestHeightRangesCount = 0;

	int16_t LayersCount, DestLayersCount;
	int16_t *Layers, *DestLayers;

	GetGeoLayers(GridX, GridY, LayersCount, Layers);
	GetGeoLayers(GridX + OffsetX, GridY + OffsetY, DestLayersCount, DestLayers);

	HeightRange HeightRanges[MAX_HEIGHT_RANGES_COUNT];
	int HeightRangesIndex = 0;

	for (int LayerIndex = 0; LayerIndex < LayersCount; LayerIndex++) {

		int16_t DestHeight;

		if (!GetWallDestHeight(Layers[LayerIndex], OffsetX, OffsetY, DestLayers, DestLayersCount, DestHeight))
			continue;

		HeightRanges[HeightRangesIndex++] = HeightRange(GET_GEO_HEIGHT(Layers[LayerIndex]), DestHeight);
		HeightRanges[HeightRangesIndex++] = HeightRange(DestHeight, GET_GEO_HEIGHT(Layers[LayerIndex]));
	}

	for (int DestLayerIndex = 0; DestLayerIndex < DestLayersCount; DestLayerIndex++) {

		int16_t DestHeight;

		if (!GetWallDestHeight(DestLayers[DestLayerIndex], -OffsetX, -OffsetY, Layers, LayersCount, DestHeight))
			continue;

		HeightRanges[HeightRangesIndex++] = HeightRange(DestHeight, GET_GEO_HEIGHT(DestLayers[DestLayerIndex]));
		HeightRanges[HeightRangesIndex++] = HeightRange(GET_GEO_HEIGHT(DestLayers[DestLayerIndex]), DestHeight);
	}

	if (HeightRangesIndex > 0) {
		sort(begin(HeightRanges), begin(HeightRanges) + HeightRangesIndex);

		HeightRange* CurrentRange = &DestHeightRanges[DestHeightRangesCount++];
		*CurrentRange = HeightRanges[0];

		for (int Index = 1; Index < HeightRangesIndex; Index++) {

			HeightRange* NextRange = &HeightRanges[Index];
			if (CurrentRange->SortedOverlapTestWith(NextRange))
				CurrentRange->MergeWith(NextRange);
			else {
				CurrentRange = &DestHeightRanges[DestHeightRangesCount++];
				*CurrentRange = *NextRange;
			}
		}
	}
}

void Geo3DViewForm::GenerateSidePlanes(int GridX, int GridY, int OffsetX, int OffsetY)
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
			int16_t HeightIndex = (int16_t) P.y;
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

		XMFLOAT3 Color = BakeLightColor({ 1, 1, 1 }, { (float)(OffsetX * -PlaneDirection), (float)(OffsetY * -PlaneDirection), 0 });

		if (!GenerateModelFromUsageMap())
			throw new runtime_error("Couldn't generate model for side plane");

		uint32_t VertexOffset = NextVertexIndex;
		for (int Index = 0; Index < Points.size(); Index++) {

			Point P = Points[Index];

			uint32_t VertexIndex = AllocateVertexIndex();

			InputVertex *Vertex = &VertexBuffer[VertexIndex];

			// this epsilon thing is for fixing z buffer fight
			Vertex->Pos = { (float)(DynamicX ? P[0] : (float)(GridX + 1) + (float)PlaneDirection * Epsilon), (float)(!DynamicX ? P[0] : (float)(GridY + 1) + (float)PlaneDirection * Epsilon), (float)GridZToHeight(P[1]) * 0.1f };
#ifdef DEBUG_USE_RANDOM_COLORS
			Vertex->Color = GetRandomColor();
#else
			Vertex->Color = Color;
#endif
			Vertex->Tex = { 0, 0, -1 };
		}

		if (PlaneDirection == (DynamicX ? 1 : -1)) {
			for (int Index = 0; Index < Indices.size(); Index++)
				AllocateIndexIndex(VertexOffset + Indices[Index]);
		}
		else {
			for (int Index = (int) Indices.size() - 1; Index >= 0; Index--)
				AllocateIndexIndex(VertexOffset + Indices[Index]);
		}
		
		/*
		for (int X = PointUsageMapUsedBoundBox.left; X < PointUsageMapUsedBoundBox.right - 1; X++)
			for (int Z = PointUsageMapUsedBoundBox.top; Z < PointUsageMapUsedBoundBox.bottom - 1; Z++)
				if (GetPointUsage(X, Z).IsSet)
					AddSidePlane(DynamicX ? X : GridX, !DynamicX ? X : GridY, GridZToHeight(Z), GridZToHeight(Z + 1), OffsetX, OffsetY, PlaneDirection, Color);		 
		*/
	}
}

void Geo3DViewForm::SetupGenerationGrid(int32_t WorldX, int32_t WorldY, uint32_t Width, uint32_t Height)
{
	GridWorldX = WorldX;
	GridWorldY = WorldY;

	GridWidth = Width / L2Geodata::GEO_COORDS_IN_WORLD_COORDS;
	GridHeight = Height / L2Geodata::GEO_COORDS_IN_WORLD_COORDS;

	GridUsageMap = (uint8_t*) calloc((GridWidth * GridHeight * 3 * MAX_HEIGHT_RANGES_COUNT + 7) / 8, 1);

	// int MaxCellPerPolygon = (int)(sqrt(GridWidth * GridHeight) / 2); // 50%

	SetupPointUsageMap(400 + 1, 4096 + 1);

	SetupFloodFillStack(1000 * 1000);
}

bool Geo3DViewForm::IsInGridBound(int GridX, int GridY)
{
	return (GridX >= 0 && GridX < (int32_t)GridWidth && GridY >= 0 && GridY < (int32_t)GridHeight);
}

#define GET_GRID_BIT_INDEX \
GridX * GridHeight * 3 * MAX_HEIGHT_RANGES_COUNT + \
GridY * 3 * MAX_HEIGHT_RANGES_COUNT + \
Side * MAX_HEIGHT_RANGES_COUNT + \
LayerOrHeightIndex

bool Geo3DViewForm::GetGridUsage(int GridX, int GridY, int Side, int16_t LayerOrHeightIndex)
{
	assert(IsInGridBound(GridX, GridY));

	int Index = GET_GRID_BIT_INDEX;

	return (GridUsageMap[Index / 8] >> (Index % 8)) & 1;
}

void Geo3DViewForm::SetGridSideUsage(int GridX, int GridY, int Side, int16_t LayerOrHeightIndex)
{
	assert(IsInGridBound(GridX, GridY));

	int Index = GET_GRID_BIT_INDEX;

	GridUsageMap[Index / 8] |= 1 << (Index % 8);
}

void Geo3DViewForm::FinalizeGenerationGrid(void)
{
	free(GridUsageMap);
	GridUsageMap = NULL;

	GridWorldX = 0;
	GridWorldY = 0;

	GridWidth = 0;
	GridHeight = 0;

	FinalizeFloodFillStack();
	FinalizePointUsageMap();
}

void Geo3DViewForm::GenerateGeodataScene(int32_t WorldX, int32_t WorldY, uint32_t Width, uint32_t Height)
{
	LONGLONG StartTime = GetTime();

	SetupGenerationGrid(WorldX, WorldY, Width, Height);

	ResetScene();

	for (uint32_t GridX = 0; GridX < GridWidth; GridX++)
		for (uint32_t GridY = 0; GridY < GridHeight; GridY++) {

			GenerateTopPlanes(GridX, GridY);
			GenerateSidePlanes(GridX, GridY, 1, 0);
			GenerateSidePlanes(GridX, GridY, 0, 1);
		}

	CommitScene();

	FinalizeGenerationGrid();

	LONGLONG EndTime = GetTime();

	cout << NextVertexIndex << " verticies was used" << endl;
	cout << NextIndexIndex << " indices was used" << endl;
	cout << "Scene generated for " << TimeToMs(EndTime - StartTime) << " ms" << endl;
}

void Geo3DViewForm::ProcessWindowState(void)
{
	bool IsInFocusNow = GetForegroundWindow() == WindowHandle;

	if (IsInFocusNow != IsInFocus) {

		IsInFocus = IsInFocusNow;

		if (IsInFocus) {
			RECT Rect;
			POINT Center;

			GetWindowRect(WindowHandle, &Rect);
			Center = { Rect.left + (Rect.right - Rect.left) / 2, Rect.top + (Rect.bottom - Rect.top) / 2 };
			Rect = { Center.x, Center.y, Center.x + 1, Center.y + 1 };
			ClipCursor(&Rect);
			ShowCursor(false);
		}
		else {
			ShowCursor(true);
			ClipCursor(NULL);
		}
	}
}

void Geo3DViewForm::ProcessMouseInput(LONG dx, LONG dy) {

	CameraAngle.x -= (float)dx / 300.0f;
	CameraAngle.y += (float)dy / 300.0f;

	CameraAngle.x /= (float)M_PI * 2.0f;
	CameraAngle.x -= (float)(long)CameraAngle.x;
	CameraAngle.x *= (float)M_PI * 2.0f;

	CameraAngle.y = min(max(0.01f, CameraAngle.y), 3.14f);

	BuildViewMatrix();
	BuildShaderMatrix();
}

LRESULT Geo3DViewForm::WndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	switch (Msg)
	{
	case WM_INPUT:
		UINT RimType;
		HRAWINPUT RawHandle;
		RAWINPUT RawInput;
		UINT RawInputSize, Res;

		RimType = GET_RAWINPUT_CODE_WPARAM(wParam);
		RawHandle = HRAWINPUT(lParam);

		RawInputSize = sizeof(RawInput);
		Res = GetRawInputData(RawHandle, RID_INPUT, &RawInput, &RawInputSize, sizeof(RawInput.header));

		if (Res != (sizeof(RAWINPUTHEADER) + sizeof(RAWMOUSE)))
			break;

		if ((RawInput.data.mouse.usFlags & 1) != MOUSE_MOVE_RELATIVE)
			break;

		if (IsInFocus)
			ProcessMouseInput(RawInput.data.mouse.lLastX, RawInput.data.mouse.lLastY);

		break;
	case WM_KEYDOWN: {
		int Key = (int)wParam;

		if (!HandleKeyPress(Key) && Key < KEYS_COUNT)
			PressedKeys[Key] = true;
		break;
	}
	case WM_KEYUP:
		if (wParam < KEYS_COUNT)
			PressedKeys[wParam] = false;
		break;
	case WM_RBUTTONDOWN:
		DrawTrianglesAsLines = !DrawTrianglesAsLines;

		if (DrawTrianglesAsLines)
			DirectDeviceCtx->RSSetState(WireFrameMode);
		else
			DirectDeviceCtx->RSSetState(SolidMode);

		break;
	case WM_MOUSEWHEEL: {

		float Mul = Sign(GET_WHEEL_DELTA_WPARAM(wParam)) == 1 ? 1.1f : 1.0f / 1.1f;

		MoveSpeed = max(1.0f, MoveSpeed * Mul);
		break;
	}
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hWnd, Msg, wParam, lParam);
	}
	return 0;
}

void Geo3DViewForm::Show(void) {

	ShowWindow(WindowHandle, SW_SHOW);
	// UpdateWindow(WindowHandle);
}

void Geo3DViewForm::ProcessKeyboardInput(double dt)
{
	XMVECTOR SideVector = XMVector3Normalize(XMVector3Cross(-TargetVector, Up));
	XMVECTOR ForwardVector = XMVector3Cross(SideVector, Up);

	XMVECTOR Displacement = {};

	if (PressedKeys['D'])
		Displacement += SideVector;
	if (PressedKeys['A'])
		Displacement -= SideVector;
	if (PressedKeys['W'])
		Displacement += ForwardVector;
	if (PressedKeys['S'])
		Displacement -= ForwardVector;
	if (PressedKeys[VK_SHIFT])
		Displacement -= Up;
	if (PressedKeys[VK_SPACE] || PressedKeys[VK_CONTROL])
		Displacement += Up;

	Displacement = XMVector3Normalize(Displacement) * (float)(MoveSpeed * dt);

	if (XMVector3LengthSq(Displacement).m128_f32[0] <= 0.0)
		return;

	XMVECTOR Position = XMLoadFloat3(&CameraPosition);
	Position += Displacement;
	XMStoreFloat3(&CameraPosition, Position);

	BuildViewMatrix();
	BuildShaderMatrix();
}

void Geo3DViewForm::WaitForNextFrame(void)
{
	HRESULT res;

	if (SyncQuery == NULL) {
		D3D11_QUERY_DESC QueryDesc = {};
		QueryDesc.Query = D3D11_QUERY_EVENT;

		res = DirectDevice->CreateQuery(&QueryDesc, &SyncQuery);
		if (FAILED(res))
			throw new runtime_error("Couldn't create sync query");
	}
	else {

		BOOL QueryResult;
		do {
			res = DirectDeviceCtx->GetData(SyncQuery, &QueryResult, sizeof(QueryResult), 0);
			if (res == S_FALSE)
				QueryResult = false;
			else
				if (res != S_OK)
					throw new runtime_error("Sync query wait failed");
		} while (!QueryResult);
	}
}

void Geo3DViewForm::DrawScene(void)
{
	

	// clear the back buffer to a deep blue
	float color[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	DirectDeviceCtx->ClearRenderTargetView(RenderTargetView, color);
	DirectDeviceCtx->ClearDepthStencilView(DepthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	DirectDeviceCtx->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	DirectDeviceCtx->DrawIndexed(NextIndexIndex, 0, 0);

	if (SyncQuery != NULL) 
		DirectDeviceCtx->End(SyncQuery);

	// switch the back buffer and the front buffer
	SwapChain->Present(UseVSync ? 1 : 0, 0);
}

void Geo3DViewForm::Tick(double dt) {

	ProcessWindowState();
	ProcessKeyboardInput(dt);
	DrawScene();
}