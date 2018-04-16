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
static ID3D11Buffer *ShaderMatrix;
static ID3D11SamplerState* Sampler;

static ID3D11Buffer *SceneVertexBuffer;
static ID3D11Buffer *SceneIndexBuffer;

static XMFLOAT3 CameraPosition;
static XMFLOAT2 CameraAngle;
static XMVECTOR TargetVector;
static XMVECTOR Up;

static uint32_t* GeoGrid;
static uint32_t GeoGridWorldX;
static uint32_t GeoGridWorldY;
static uint32_t GeoGridWidth;
static uint32_t GeoGridHeight;
static uint32_t GeoGridLayersCount;

#define MAX_VERTICES (10 * 1000)
static InputVertex VertexBuffer[MAX_VERTICES];
static uint32_t NextVertexIndex;

#define MAX_INDEXES (MAX_VERTICES * 2)
static uint32_t IndexBuffer[MAX_INDEXES];
static uint32_t NextIndexIndex;

#define KEYS_COUNT 256
static bool PressedKeys[KEYS_COUNT];

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

	DXGI_SWAP_CHAIN_DESC sd = { };
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
	D3D11_VIEWPORT vp = { };
	vp.Width = (float) Width;
	vp.Height = (float) Height;
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
	res = D3DCompile(ShaderData, ShaderSize, NULL, NULL, NULL, "VS", "vs_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &VSBlob, NULL);
	if (FAILED(res))
		throw new std::runtime_error("Couldn't compile vertex shader");

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
	res = SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*) &BackBuffer);
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
	D3D11_SAMPLER_DESC SamplerDesc = { };
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

	D3D11_BUFFER_DESC SceneVertexBufferDesc = { };
	SceneVertexBufferDesc.ByteWidth = sizeof(InputVertex) * 4;
	SceneVertexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	SceneVertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	res = DirectDevice->CreateBuffer(&SceneVertexBufferDesc, NULL, &SceneVertexBuffer);
	if (FAILED(res))
		throw new std::runtime_error("Couldn't create vertex buffer");

	D3D11_BUFFER_DESC SceneIndexBufferDesc = { };
	SceneIndexBufferDesc.ByteWidth = sizeof(uint32_t) * 6;
	SceneIndexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	SceneIndexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	res = DirectDevice->CreateBuffer(&SceneIndexBufferDesc, NULL, &SceneIndexBuffer);
	if (FAILED(res))
		throw new std::runtime_error("Couldn't create index buffer");

	// create shader variable reference
	D3D11_BUFFER_DESC MatrixDesc = { };
	MatrixDesc.Usage = D3D11_USAGE_DEFAULT;
	MatrixDesc.ByteWidth = sizeof(XMFLOAT4X4);
	MatrixDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	res = DirectDevice->CreateBuffer(&MatrixDesc, NULL, &ShaderMatrix);
	if (FAILED(res))
		throw new std::runtime_error("Couldn't create shader matrix buffer");

	DirectDeviceCtx->VSSetConstantBuffers(0, 1, &ShaderMatrix);

	// setup frame
	DirectDeviceCtx->OMSetRenderTargets(1, &RenderTargetView, DepthStencilView);

	DirectDeviceCtx->IASetInputLayout(VertexLayout);

	DirectDeviceCtx->VSSetShader(VertexShader, 0, 0);
	DirectDeviceCtx->PSSetShader(PixelShader, 0, 0);

	UINT stride = sizeof(InputVertex);
	UINT offset = 0;
	DirectDeviceCtx->IASetVertexBuffers(0, 1, &SceneVertexBuffer, &stride, &offset);

	DirectDeviceCtx->IASetIndexBuffer(SceneIndexBuffer, DXGI_FORMAT_R32_UINT, 0);
	DirectDeviceCtx->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// setup camera

	CameraAngle.y = 3;
	CameraAngle.x = 0;

	CameraPosition.y = 0;

	Up = XMVectorSet(0, 0, 1, 1);

	// setup matrices

	ProjectionMatrix = XMMatrixPerspectiveFovRH(0.25f * (float)M_PI, (float)Width / (float)Height, 1.0f, 1000.0f);

	BuildViewMatrix();
	BuildWorldMatrix();

	BuildShaderMatrix();

	GenerateDebugStaticScene();
	// GenerateGeodataScene(13000, 140572, 16000, 16000);

	// register raw input
	RAWINPUTDEVICE Rid[1] = { };

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

	ViewMatrix = XMMatrixLookAtRH(Position, Target, Up);
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

	DirectDeviceCtx->UpdateSubresource(ShaderMatrix, 0, NULL, &FinalMatrix, 0, 0);
}

uint32_t Geo3DViewForm::AllocateVertexIndex(InputVertex Vertex)
{
	if (NextVertexIndex >= MAX_VERTICES)
		throw new std::runtime_error("Vertices limit is reached");

	uint32_t Ret = NextVertexIndex;
	NextVertexIndex++;

	VertexBuffer[Ret] = Vertex;

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
	Vertices[0] = AllocateVertexIndex(InputVertex(0.0f, 0.0f, -1.0f, 0, 0, 0));
	Vertices[1] = AllocateVertexIndex(InputVertex(1.0f, 0.0f, -1.0f, 0, 1, 0));
	Vertices[2] = AllocateVertexIndex(InputVertex(0.0f, 1.0f, -1.0f, 1, 0, 0));
	Vertices[3] = AllocateVertexIndex(InputVertex(1.0f, 1.0f, -1.0f, 1, 1, 1));

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
	GeoGrid = new uint32_t[GridWidth * GridHeight * MaxLayersCount * 2 * 2];
	GeoGridWorldX = GridWorldX;
	GeoGridWorldY = GridWorldY;
	GeoGridWidth = GridWidth;
	GeoGridHeight = GridHeight;
	GeoGridLayersCount = MaxLayersCount;
}

uint32_t * Geo3DViewForm::GetGridPtr(uint32_t GridX, uint32_t GridY, uint32_t LayerIndex, uint32_t VertexX, uint32_t VertexY)
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

void Geo3DViewForm::GenerateGeodataScene(int32_t WorldX, int32_t WorldY, uint32_t Width, uint32_t Height)
{
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

				InputVertex Vertices[4];

				// calculate normals
				for (int VertexX = 0; VertexX < 2; VertexX++) 
					for (int VertexY = 0; VertexY < 2; VertexY++) {

						int VertexIndex = VertexX * 2 + VertexY;

						int NX = VertexX == 0 ? -1 : 1;
						int NY = VertexY == 0 ? -1 : 1;

						XMVECTOR Normal = { 0, 0, 1 };

						NeighborInfo* N;
						
						N = &Neighbors[NX + 1][0 + 1];
						if (N->LayerIndex != -1) {
							int NDirection = Sign((int32_t)Height - (int32_t)N->Height);

							XMVECTOR FaceNormal = { NDirection * NX, 0, 1 - abs(NDirection) };

							Normal += FaceNormal;
						}

						N = &Neighbors[0 + 1][NY + 1];
						if (N->LayerIndex != -1) {
							int NDirection = Sign((int32_t)Height - (int32_t)N->Height);

							XMVECTOR FaceNormal = { 0, NDirection * NY, 1 - abs(NDirection) };

							Normal += FaceNormal;
						}

						N = &Neighbors[NX + 1][NY + 1];
						if (N->LayerIndex != -1 && Height == N->Height) {

							XMVECTOR FaceNormal = { 0, 0, 1 };

							Normal += FaceNormal;
						}

						Normal = XMVector3Normalize(Normal);

						XMStoreFloat3(&Vertices[VertexIndex].Normal, Normal);

						Vertices[VertexIndex].Color = { 0, 1, 0 };
					}
			}
		}

	FreeGrid();
}

void Geo3DViewForm::ProcessMouseInput(LONG dx, LONG dy) {

	CameraAngle.x += (float)dx / 300.0f;
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
		GetWindowRect(WindowHandle, &Rect);
		ClipCursor(&Rect);
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

		ProcessMouseInput(RawInput.data.mouse.lLastX, RawInput.data.mouse.lLastY);
		
		break;
	case WM_SETCURSOR:
		SetCursor(0);
		break;
	case WM_KEYDOWN:
		if (wParam < KEYS_COUNT)
			PressedKeys[wParam] = true;
		break;
	case WM_KEYUP:
		if (wParam < KEYS_COUNT)
			PressedKeys[wParam] = false;
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
	float MoveSpeed = 2.5f;

	XMVECTOR SideVector = XMVector3Normalize(XMVector3Cross(TargetVector, Up));
	XMVECTOR ForwardVector = XMVector3Cross(-SideVector, Up);

	XMVECTOR Displacement = { };

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

	Displacement = XMVector3Normalize(Displacement) * MoveSpeed * dt;

	XMVECTOR Position = XMLoadFloat3(&CameraPosition);
	Position += Displacement;
	XMStoreFloat3(&CameraPosition, Position);

	BuildViewMatrix();
	BuildShaderMatrix();
}

void Geo3DViewForm::DrawScene(void)
{
	// clear the back buffer to a deep blue
	float color[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
	DirectDeviceCtx->ClearRenderTargetView(RenderTargetView, color);
	DirectDeviceCtx->ClearDepthStencilView(DepthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	DirectDeviceCtx->DrawIndexed(NextIndexIndex, 0, 0);

	// switch the back buffer and the front buffer
	SwapChain->Present(0, 0);
}

void Geo3DViewForm::Tick(double dt) {

	ProcessKeyboardInput(dt);
	DrawScene();
}