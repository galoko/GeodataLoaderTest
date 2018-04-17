#include "stdafx.h"

#include "Geo3DViewForm.h"

#include "Geodata\L2Geodata.h"
#include "FormsUtils.h"

#include <stdexcept>
#include <iostream>
#include <D3Dcompiler.h>

#pragma comment (lib, "d3d11.lib")
#pragma comment(lib,"d3dcompiler.lib")

struct InputVertex
{
	XMFLOAT3 Pos;
	XMFLOAT3 Color;
	XMFLOAT3 Normal;

	InputVertex() {
	}

	InputVertex(float x, float y, float z, float r, float g, float b) : Pos(x, y, z), Color(r, g, b) {
	}
};

static unsigned int Width, Height;

static HWND WindowHandle;

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
static ID3D11SamplerState* Sampler;

struct ShaderOptions {
	float LigthEnabled;
	float Padding[3];
};

static ID3D11Buffer *ShaderOptionsRef;
static ShaderOptions Options;

static ID3D11Buffer *SceneVertexBuffer;
static ID3D11Buffer *SceneIndexBuffer;

static XMFLOAT3 CameraPosition;
static XMFLOAT2 CameraAngle;
static XMVECTOR TargetVector;
static XMVECTOR Up;

static int32_t* GeoGrid;
static uint32_t GeoGridWorldX;
static uint32_t GeoGridWorldY;
static uint32_t GeoGridWidth;
static uint32_t GeoGridHeight;
static uint32_t GeoGridLayersCount;

#define MAX_VERTICES (30 * 1000 * 1000)
static InputVertex *VertexBuffer;
static uint32_t NextVertexIndex;

#define MAX_INDEXES (MAX_VERTICES * 5)
static uint32_t *IndexBuffer;
static uint32_t NextIndexIndex;
static uint32_t LineStartIndex;

#define KEYS_COUNT 256
static bool PressedKeys[KEYS_COUNT];
static bool InFocus;
static float MoveSpeed = 30.0f;

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
	res = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, 0, D3D11_CREATE_DEVICE_SINGLETHREADED | D3D11_CREATE_DEVICE_DEBUG, NULL, 0,
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
	{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 }
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

	// creating sampler for textures
	D3D11_SAMPLER_DESC SamplerDesc = {};
	SamplerDesc.Filter = D3D11_FILTER_ANISOTROPIC;
	SamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	SamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	SamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	SamplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	SamplerDesc.MinLOD = 0;
	SamplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
	res = DirectDevice->CreateSamplerState(&SamplerDesc, &Sampler);
	if (FAILED(res))
		throw new std::runtime_error("Couldn't create sampler");

	// setup cull mode
	ID3D11RasterizerState *CullMode;

	D3D11_RASTERIZER_DESC CullModeDesc = {};
	CullModeDesc.FillMode = D3D11_FILL_SOLID;
	CullModeDesc.CullMode = D3D11_CULL_NONE;
	CullModeDesc.FrontCounterClockwise = true;
	res = DirectDevice->CreateRasterizerState(&CullModeDesc, &CullMode);
	if (FAILED(res))
		throw new std::runtime_error("Couldn't create cullmode");

	DirectDeviceCtx->RSSetState(CullMode);

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

	D3D11_BUFFER_DESC OptionsDesc = {};
	OptionsDesc.Usage = D3D11_USAGE_DEFAULT;
	OptionsDesc.ByteWidth = sizeof(ShaderOptions);
	OptionsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	res = DirectDevice->CreateBuffer(&OptionsDesc, NULL, &ShaderOptionsRef);
	if (FAILED(res))
		throw new std::runtime_error("Couldn't create shader options buffer");

	DirectDeviceCtx->PSSetConstantBuffers(1, 1, &ShaderOptionsRef);

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

	CameraPosition.z = -269.5;

	Up = XMVectorSet(0, 0, 1, 1);

	// setup matrices

	ProjectionMatrix = XMMatrixPerspectiveFovLH(0.5f * (float)M_PI, (float)Width / (float)Height, 0.1f, 1000.0f);

	BuildViewMatrix();
	BuildWorldMatrix();

	BuildShaderMatrix();

	VertexBuffer = new InputVertex[MAX_VERTICES];
	IndexBuffer = new uint32_t[MAX_INDEXES];

	// GenerateDebugStaticScene();


	int16_t Count;
	for (int X = -10; X <= 10; X++)
		for (int Y = -10; Y <= 10; Y++) {

			int Offset = abs(X) | abs(Y);

			L2Geodata::GetLayeredSubBlocks(13100 + X * 16, 140572 + Y * 16, Count)[0] = -2732 << 1;
		}

	L2Geodata::GetLayeredSubBlocks(13100 + 16, 140572 + 16, Count)[0] = -2724 << 1;
	L2Geodata::GetLayeredSubBlocks(13100 + 16, 140572 + 32, Count)[0] = -2716 << 1;
	// L2Geodata::GetLayeredSubBlocks(13100 + 32, 140572 + 16, Count)[0] = -2716 << 1;
	// L2Geodata::GetLayeredSubBlocks(13100 + 32, 140572 + 32, Count)[0] = -2708 << 1;
	/*
	L2Geodata::GetLayeredSubBlocks(13100 + 16, 140572, Count)[0] = -2692 << 1;
	L2Geodata::GetLayeredSubBlocks(13100, 140572 + 16, Count)[0] = -2700 << 1;
	L2Geodata::GetLayeredSubBlocks(13100 + 16, 140572 + 16, Count)[0] = -2684 << 1;
	*/
	
	GenerateGeodataScene(13100, 140572, 64, 64);

	// register raw input
	RAWINPUTDEVICE Rid[1] = {};

	Rid[0].usUsagePage = 0x01;
	Rid[0].usUsage = 0x02;
	Rid[0].dwFlags = RIDEV_INPUTSINK;   // adds HID mouse and also ignores legacy mouse messages
	Rid[0].hwndTarget = WindowHandle;

	if (!RegisterRawInputDevices(Rid, 1, sizeof(Rid[0])))
		throw new std::runtime_error("Couldn't register raw devices");
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
	LineStartIndex = 0;
}

void Geo3DViewForm::CommitScene(void) {

	D3D11_BOX Region = {};
	Region.bottom = 1;
	Region.back = 1;

	Region.right = NextVertexIndex * sizeof(*VertexBuffer);
	DirectDeviceCtx->UpdateSubresource(SceneVertexBuffer, 0, &Region, VertexBuffer, 0, 0);

	Region.right = NextIndexIndex * sizeof(*IndexBuffer);
	DirectDeviceCtx->UpdateSubresource(SceneIndexBuffer, 0, &Region, IndexBuffer, 0, 0);
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

void Geo3DViewForm::AllocateGrid(int32_t GridWorldX, int32_t GridWorldY, uint32_t GridWidth, uint32_t GridHeight, uint32_t MaxLayersCount)
{
	int GridSize = GridWidth * GridHeight * MaxLayersCount * 2 * 2;
	int GridByteSize = GridSize * sizeof(*GeoGrid);

	GeoGrid = new int32_t[GridSize];
	memset(GeoGrid, 0xFF, GridByteSize);

	GeoGridWorldX = GridWorldX;
	GeoGridWorldY = GridWorldY;
	GeoGridWidth = GridWidth;
	GeoGridHeight = GridHeight;
	GeoGridLayersCount = MaxLayersCount;
}

int32_t * Geo3DViewForm::GetGridPtr(uint32_t GridX, uint32_t GridY, uint32_t LayerIndex, uint32_t VertexX, uint32_t VertexY)
{
	if (GridX >= GeoGridWidth || GridY >= GeoGridHeight || LayerIndex >= GeoGridLayersCount || VertexX >= 2 || VertexY >= 2)
		throw new std::runtime_error("Grid indexes out of bound");

	uint32_t Index =
		GridX * GeoGridHeight * GeoGridLayersCount * 2 * 2 +
		GridY * GeoGridLayersCount * 2 * 2 +
		LayerIndex * 2 * 2 +
		VertexX * 2 +
		VertexY;

	return &GeoGrid[Index];
}

int32_t Geo3DViewForm::GetGridVertexIndex(uint32_t GridX, uint32_t GridY, uint32_t LayerIndex, uint32_t VertexX, uint32_t VertexY) {

	int32_t* Index = GetGridPtr(GridX, GridY, LayerIndex, VertexX, VertexY);

	int32_t Ret = *Index;

	if (Ret == -1) {

		Ret = AllocateVertexIndex();
		*Index = Ret;
	}

	return Ret;
}

void Geo3DViewForm::GetGeoLayers(int32_t GridX, int32_t GridY, int16_t& LayersCount, int16_t*& Layers)
{
	int32_t X = GeoGridWorldX + GridX * L2Geodata::GEO_COORDS_IN_WORLD_COORDS;
	int32_t Y = GeoGridWorldY + GridY * L2Geodata::GEO_COORDS_IN_WORLD_COORDS;

	Layers = L2Geodata::GetLayeredSubBlocks(X, Y, LayersCount);
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

void Geo3DViewForm::FreeGrid(void)
{
	delete[] GeoGrid;

	GeoGrid = NULL;
	GeoGridWidth = 0;
	GeoGridHeight = 0;
	GeoGridLayersCount = 0;
}

struct NeighborInfo {
	int16_t LayerIndex, Height;
};

template <typename T> int Sign(T val) {
	return (T(0) < val) - (val < T(0));
}

float GetRandomCoord(void) {

	float t = (float)rand() / RAND_MAX;
	t = t * 2.0f - 1.0f;

	return t;
}

XMVECTOR GetRandomVector(void) {

	return XMVector3Normalize(XMVectorSet(GetRandomCoord(), GetRandomCoord(), GetRandomCoord(), 1.0f));
}

#define NOT_DIR(x) (1 - abs(x))

void Geo3DViewForm::GenerateGeodataScene(int32_t WorldX, int32_t WorldY, uint32_t Width, uint32_t Height)
{
	ResetScene();

	int GridWidth = Width / L2Geodata::GEO_COORDS_IN_WORLD_COORDS;
	int GridHeight = Height / L2Geodata::GEO_COORDS_IN_WORLD_COORDS;

	AllocateGrid(WorldX, WorldY, GridWidth, GridHeight, L2Geodata::LAYERS_PER_SUBBLOCK_LIMIT);

	for (int GridX = 0; GridX < GridWidth; GridX++)
		for (int GridY = 0; GridY < GridHeight; GridY++) {

			int16_t LayerCount;
			int16_t *Layers;

			GetGeoLayers(GridX, GridY, LayerCount, Layers);

			for (int LayerIndex = 0; LayerIndex < LayerCount; LayerIndex++) {

				NeighborInfo Neighbors[3][3];

				for (int NeighborX = -1; NeighborX <= 1; NeighborX++)
					for (int NeighborY = -1; NeighborY <= 1; NeighborY++) {

						if (NeighborX == 0 && NeighborY == 0) {
							Neighbors[NeighborX + 1][NeighborY + 1] = { -1, -1 };
							continue;
						}

						int16_t NeighborLayerCount;
						int16_t *NeighborLayers;

						GetGeoLayers(GridX + NeighborX, GridY + NeighborY, NeighborLayerCount, NeighborLayers);

						int16_t NeighborLayerIndex, NeighborHeight;
						FindCorrespondingLayer(LayerCount, Layers, LayerIndex, NeighborLayerCount, NeighborLayers, NeighborLayerIndex, NeighborHeight);

						Neighbors[NeighborX + 1][NeighborY + 1] = { NeighborLayerIndex, NeighborHeight };
					}

				int16_t Height = GET_GEO_HEIGHT(Layers[LayerIndex]);

				int32_t VerticesIndexes[2][2];

				// calculate vertices
				for (int VertexX = 0; VertexX < 2; VertexX++)
					for (int VertexY = 0; VertexY < 2; VertexY++) {

						InputVertex Vertex;

						Vertex.Pos = { (float)(GridX + VertexX), (float)(GridY + VertexY), (float)Height * 0.1f };
						Vertex.Color = { 1.0f, 1.0f, 1.0f };
						// Vertex.Color = { (float)rand() / RAND_MAX, (float)rand() / RAND_MAX, (float)rand() / RAND_MAX };
						Vertex.Normal = { 0, 0, 0 };

						// if (GridX == 0 && GridY == 1 && VertexX == 1 && VertexY == 0) {

						// calculate normals

						int NX = VertexX == 0 ? -1 : 1;
						int NY = VertexY == 0 ? -1 : 1;

						XMVECTOR Normal = { 0, 0, 1 };

						NeighborInfo *NX_Entry, *NY_Entry, *NXY_Entry;

						NX_Entry = &Neighbors[NX + 1][0 + 1];
						int NtoX_D, NtoY_D, NtoXY_D, NXtoXY_D, NYtoXY_D;

						if (NX_Entry->LayerIndex != -1)
							NtoX_D = Sign((int32_t)Height - (int32_t)NX_Entry->Height);
						else
							NtoX_D = 0;

						NY_Entry = &Neighbors[0 + 1][NY + 1];
						if (NY_Entry->LayerIndex != -1)
							NtoY_D = Sign((int32_t)Height - (int32_t)NY_Entry->Height);
						else
							NtoY_D = 0;

						NXY_Entry = &Neighbors[NX + 1][NY + 1];
						if (NXY_Entry->LayerIndex != -1)
							NtoXY_D = Sign((int32_t)Height - (int32_t)NXY_Entry->Height);
						else
							NtoXY_D = 0;

						if (NX_Entry->LayerIndex != -1 && NXY_Entry->LayerIndex != -1)
							NXtoXY_D = Sign((int32_t)NX_Entry->Height - (int32_t)NXY_Entry->Height);
						else
							NXtoXY_D = 0;

						if (NY_Entry->LayerIndex != -1 && NXY_Entry->LayerIndex != -1)
							NYtoXY_D = Sign((int32_t)NY_Entry->Height - (int32_t)NXY_Entry->Height);
						else
							NYtoXY_D = 0;

						
						Normal += { (float)(NtoX_D * NX), 0, (float)NOT_DIR(NtoX_D) };
						Normal += { 0, (float)(NtoY_D * NY), (float)NOT_DIR(NtoY_D) };
						/*
						Normal += { (float)(NtoX_D * NX), 0, 0 };
						Normal += { 0, (float)(NtoY_D * NY), 0 };
						*/

						if (abs(NtoY_D + NtoX_D) < 2) {
							Normal += { 0, 0, (float)NOT_DIR(NtoXY_D) };
							if (NtoXY_D != NtoY_D)
								Normal += { (float)(NYtoXY_D * NX), 0, 0 };
							if (NtoXY_D != NtoX_D)
								Normal += { 0, (float)(NXtoXY_D * NY), 0 };
						}

						// Normal += GetRandomVector() * 1.5f;

						Normal = XMVector3Normalize(Normal);

						XMStoreFloat3(&Vertex.Normal, Normal);
						// }

						int32_t Index = GetGridVertexIndex(GridX, GridY, LayerIndex, VertexX, VertexY);

						VertexBuffer[Index] = Vertex;
						VerticesIndexes[VertexX][VertexY] = Index;
					}

				/*
				vertices
				0 2
				1 3
				*/

				// setup top part of quad

				AllocateIndexIndex(VerticesIndexes[0][0]);
				AllocateIndexIndex(VerticesIndexes[1][0]);
				AllocateIndexIndex(VerticesIndexes[0][1]);

				AllocateIndexIndex(VerticesIndexes[1][0]);
				AllocateIndexIndex(VerticesIndexes[0][1]);
				AllocateIndexIndex(VerticesIndexes[1][1]);

				// now setup two walls
				if (GridX + 1 < GeoGridWidth) {

					NeighborInfo* N = &Neighbors[1 + 1][0 + 1];

					if (N->LayerIndex != -1 && N->Height != Height) {

						AllocateIndexIndex(VerticesIndexes[1][0]);
						AllocateIndexIndex(VerticesIndexes[1][1]);
						AllocateIndexIndex(GetGridVertexIndex(GridX + 1, GridY, N->LayerIndex, 0, 0));

						AllocateIndexIndex(VerticesIndexes[1][1]);
						AllocateIndexIndex(GetGridVertexIndex(GridX + 1, GridY, N->LayerIndex, 0, 0));
						AllocateIndexIndex(GetGridVertexIndex(GridX + 1, GridY, N->LayerIndex, 0, 1));
					}
				}

				if (GridY + 1 < GeoGridHeight) {

					NeighborInfo* N = &Neighbors[0 + 1][1 + 1];

					if (N->LayerIndex != -1 && N->Height != Height) {

						AllocateIndexIndex(VerticesIndexes[0][1]);
						AllocateIndexIndex(VerticesIndexes[1][1]);
						AllocateIndexIndex(GetGridVertexIndex(GridX, GridY + 1, N->LayerIndex, 0, 0));

						AllocateIndexIndex(VerticesIndexes[1][1]);
						AllocateIndexIndex(GetGridVertexIndex(GridX, GridY + 1, N->LayerIndex, 0, 0));
						AllocateIndexIndex(GetGridVertexIndex(GridX, GridY + 1, N->LayerIndex, 1, 0));
					}
				}
			}
		}

	FreeGrid();

	LineStartIndex = NextVertexIndex;
	for (int VertexIndex = 0; VertexIndex < LineStartIndex; VertexIndex++) {

		InputVertex* Vertex = &VertexBuffer[VertexIndex];

		int32_t L1 = AllocateVertexIndex();
		int32_t L2 = AllocateVertexIndex();

		XMVECTOR Position = XMLoadFloat3(&Vertex->Pos);
		XMVECTOR Normal = XMLoadFloat3(&Vertex->Normal);

		XMStoreFloat3(&VertexBuffer[L1].Pos, Position);
		VertexBuffer[L1].Color = Vertex->Normal;
		VertexBuffer[L1].Normal = { 0, 0, 1 };

		Position += Normal * 0.5;

		XMStoreFloat3(&VertexBuffer[L2].Pos, Position);
		VertexBuffer[L2].Color = Vertex->Normal;
		VertexBuffer[L2].Normal = { 0, 0, 1 };
	}

	CommitScene();
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
	case WM_SETFOCUS:
		RECT Rect;
		POINT Center;

		GetWindowRect(WindowHandle, &Rect);
		Center = { Rect.left + (Rect.right - Rect.left) / 2, Rect.top + (Rect.bottom - Rect.top) / 2 };
		Rect = { Center.x, Center.y, Center.x + 1, Center.y + 1 };
		ClipCursor(&Rect);
		InFocus = true;
		break;
	case WM_KILLFOCUS:
		InFocus = false;
		break;
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

		if (InFocus)
			ProcessMouseInput(RawInput.data.mouse.lLastX, RawInput.data.mouse.lLastY);

		break;
	case WM_SETCURSOR:
		SetCursor(0);
		break;
	case WM_KEYDOWN:

		if (wParam == VK_ESCAPE)
			PostQuitMessage(0);
		else
			if (wParam < KEYS_COUNT)
				PressedKeys[wParam] = true;
		break;
	case WM_KEYUP:
		if (wParam < KEYS_COUNT)
			PressedKeys[wParam] = false;
		break;
	case WM_MOUSEWHEEL:
		MoveSpeed = max(1.0f, MoveSpeed + GET_WHEEL_DELTA_WPARAM(wParam) / 120.0f * 1.5f);
		break;
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

void Geo3DViewForm::DrawScene(void)
{
	// clear the back buffer to a deep blue
	float color[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	DirectDeviceCtx->ClearRenderTargetView(RenderTargetView, color);
	DirectDeviceCtx->ClearDepthStencilView(DepthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	Options.LigthEnabled = 1;
	DirectDeviceCtx->UpdateSubresource(ShaderOptionsRef, 0, NULL, &Options, 0, 0);

	DirectDeviceCtx->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	DirectDeviceCtx->DrawIndexed(NextIndexIndex, 0, 0);

	if (LineStartIndex > 0) {

		Options.LigthEnabled = 0;
		DirectDeviceCtx->UpdateSubresource(ShaderOptionsRef, 0, NULL, &Options, 0, 0);

		DirectDeviceCtx->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
		DirectDeviceCtx->Draw(NextVertexIndex - LineStartIndex, LineStartIndex);
	}

	// switch the back buffer and the front buffer
	SwapChain->Present(0, 0);
}

void Geo3DViewForm::Tick(double dt) {

	ProcessKeyboardInput(dt);
	DrawScene();
}