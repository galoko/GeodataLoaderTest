#include "stdafx.h"

#include "Geo3DViewForm.h"

#include "FormsUtils.h"
#include "TimeUtils.h"
#include "MathUtils.h"
#include "ColorUtils.h"

#include <algorithm>
#include <stdexcept>
#include <iostream>
#include <D3Dcompiler.h>
#include "WICTextureLoader.h"

#pragma comment (lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

// Init

void Geo3DViewForm::Init(unsigned int Width, unsigned int Height, WCHAR *WindowClass, WCHAR *Title,
	HINSTANCE hInstance) {

	// creating window

	RegisterClass(WindowClass, WndProcCallback, hInstance);

	POINT WindowPos;
	WindowPos.x = (GetSystemMetrics(SM_CXSCREEN) - Width) / 2;
	WindowPos.y = (GetSystemMetrics(SM_CYSCREEN) - Height) / 2;

	WindowHandle = CreateWindowW(WindowClass, Title, WS_POPUP, WindowPos.x, WindowPos.y, Width, Height, nullptr, nullptr, hInstance, nullptr);
	if (WindowHandle == 0)
		throw new runtime_error("Couldn't create window");

	// DirectX 11

	HRESULT res;

	// creating device and swap chain

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

	res = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, 0, 0/* | D3D11_CREATE_DEVICE_DEBUG*/, NULL, 0,
		D3D11_SDK_VERSION, &sd, &SwapChain, &DirectDevice, NULL, &DirectDeviceCtx);
	if (FAILED(res))
		throw new runtime_error("Couldn't create DX device and swap chain");

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
	HRSRC hShaderRes = FindResource(0, L"main_shader", RT_RCDATA);
	HANDLE hShaderResGlobal = LoadResource(0, hShaderRes);
	LPVOID ShaderData = LockResource(hShaderResGlobal);
	DWORD ShaderSize = SizeofResource(0, hShaderRes);

	// compiling and creating vertex shader
	ID3DBlob *Errors;

	ID3DBlob *VSBlob;
	res = D3DCompile(ShaderData, ShaderSize, NULL, NULL, NULL, "VS", "vs_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &VSBlob, &Errors);
	if (FAILED(res)) {
		string ErrorText = string((const char *)Errors->GetBufferPointer(), Errors->GetBufferSize());
		cout << ErrorText << endl;
		throw new runtime_error("Couldn't compile vertex shader");
	}

	res = DirectDevice->CreateVertexShader(VSBlob->GetBufferPointer(), VSBlob->GetBufferSize(), NULL, &VertexShader);
	if (FAILED(res))
		throw new runtime_error("Couldn't create vertex shader");

	// compiling and creating pixel shader
	ID3DBlob *PSBlob;
	res = D3DCompile(ShaderData, ShaderSize, NULL, NULL, NULL, "PS", "ps_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &PSBlob, &Errors);
	if (FAILED(res)) {
		string ErrorText = string((const char *)Errors->GetBufferPointer(), Errors->GetBufferSize());
		cout << ErrorText << endl;
		throw new runtime_error("Couldn't compile pixel shader");
	}

	res = DirectDevice->CreatePixelShader(PSBlob->GetBufferPointer(), PSBlob->GetBufferSize(), NULL, &PixelShader);
	if (FAILED(res))
		throw new runtime_error("Couldn't create pixel shader");

	// creating input layout
	static const D3D11_INPUT_ELEMENT_DESC VertexLayoutDesc[4] = {
		{ "POSITION",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD",  0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "NSWECOORD", 0, DXGI_FORMAT_R32_FLOAT,       0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "NORMAL",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 }
	};

	res = DirectDevice->CreateInputLayout(VertexLayoutDesc, sizeof(VertexLayoutDesc) / sizeof(*VertexLayoutDesc),
		VSBlob->GetBufferPointer(), VSBlob->GetBufferSize(), &VertexLayout);
	if (FAILED(res))
		throw new runtime_error("Couldn't create input layout");

	// Create a render target view
	ID3D11Texture2D *BackBuffer;
	res = SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&BackBuffer);
	if (FAILED(res))
		throw new runtime_error("Couldn't get back buffer");

	res = DirectDevice->CreateRenderTargetView(BackBuffer, NULL, &RenderTargetView);
	if (FAILED(res))
		throw new runtime_error("Couldn't create render target");

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
		throw new runtime_error("Couldn't create depth stencil buffer");

	res = DirectDevice->CreateDepthStencilView(DepthStencilBuffer, 0, &DepthStencilView);
	if (FAILED(res))
		throw new runtime_error("Couldn't create depth stencil view");

	// creating depth stencil states

	D3D11_DEPTH_STENCIL_DESC DepthStencilStateDesc;
	DepthStencilStateDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	DepthStencilStateDesc.DepthFunc = D3D11_COMPARISON_LESS;
	DepthStencilStateDesc.StencilEnable = FALSE;
	DepthStencilStateDesc.StencilReadMask = 0xFF;
	DepthStencilStateDesc.StencilWriteMask = 0xFF;

	// Stencil operations if pixel is front-facing
	DepthStencilStateDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
	DepthStencilStateDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_INCR;
	DepthStencilStateDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
	DepthStencilStateDesc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;

	// Stencil operations if pixel is back-facing
	DepthStencilStateDesc.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
	DepthStencilStateDesc.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_DECR;
	DepthStencilStateDesc.BackFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
	DepthStencilStateDesc.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;

	DepthStencilStateDesc.DepthEnable = TRUE;
	res = DirectDevice->CreateDepthStencilState(&DepthStencilStateDesc, &DepthStencilState3D);
	if (FAILED(res))
		throw new runtime_error("Couldn't create depth stencil state");

	DepthStencilStateDesc.DepthEnable = FALSE;
	res = DirectDevice->CreateDepthStencilState(&DepthStencilStateDesc, &DepthStencilState2D);
	if (FAILED(res))
		throw new runtime_error("Couldn't create depth stencil state");

	// setup cull modes
	D3D11_RASTERIZER_DESC CullModeDesc = { };
	CullModeDesc.CullMode = D3D11_CULL_FRONT;
	CullModeDesc.FrontCounterClockwise = true;

	CullModeDesc.FillMode = D3D11_FILL_SOLID;
	res = DirectDevice->CreateRasterizerState(&CullModeDesc, &SolidMode);
	if (FAILED(res))
		throw new runtime_error("Couldn't create solid cullmode");

	CullModeDesc.FillMode = D3D11_FILL_WIREFRAME;
	res = DirectDevice->CreateRasterizerState(&CullModeDesc, &WireFrameMode);
	if (FAILED(res))
		throw new runtime_error("Couldn't create wireframe cullmode");

	DirectDeviceCtx->RSSetState(SolidMode);

	// creating samplers
	D3D11_SAMPLER_DESC SamplerDesc = {};
	SamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	SamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	SamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	SamplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	SamplerDesc.MinLOD = 0;
	SamplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

	SamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	res = DirectDevice->CreateSamplerState(&SamplerDesc, &SmoothSampler);
	if (FAILED(res))
		throw new runtime_error("Couldn't create smooth sampler");

	DirectDeviceCtx->PSSetSamplers(0, 1, &SmoothSampler);

	SamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	res = DirectDevice->CreateSamplerState(&SamplerDesc, &PointSampler);
	if (FAILED(res))
		throw new runtime_error("Couldn't create point sampler");

	DirectDeviceCtx->PSSetSamplers(1, 1, &PointSampler);

	// create shader variables references

	D3D11_BUFFER_DESC PersistentVertexVariablesDesc = { };
	PersistentVertexVariablesDesc.Usage = D3D11_USAGE_DEFAULT;
	PersistentVertexVariablesDesc.ByteWidth = sizeof(PersistentVertexVariables);
	PersistentVertexVariablesDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	res = DirectDevice->CreateBuffer(&PersistentVertexVariablesDesc, NULL, &PersistentVertexVariablesRef);
	if (FAILED(res))
		throw new runtime_error("Couldn't create persistent vertex variables ref");

	DirectDeviceCtx->VSSetConstantBuffers(0, 1, &PersistentVertexVariablesRef);

	D3D11_BUFFER_DESC PerDrawVertexVariablesDesc = {};
	PerDrawVertexVariablesDesc.Usage = D3D11_USAGE_DEFAULT;
	PerDrawVertexVariablesDesc.ByteWidth = sizeof(PerDrawVertexVariables);
	PerDrawVertexVariablesDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	res = DirectDevice->CreateBuffer(&PerDrawVertexVariablesDesc, NULL, &PerDrawVertexVariablesRef);
	if (FAILED(res))
		throw new runtime_error("Couldn't create per draw vertex variables ref");

	DirectDeviceCtx->VSSetConstantBuffers(1, 1, &PerDrawVertexVariablesRef);

	D3D11_BUFFER_DESC PersistentPixelVariablesDesc = { };
	PersistentPixelVariablesDesc.Usage = D3D11_USAGE_DEFAULT;
	PersistentPixelVariablesDesc.ByteWidth = sizeof(PersistentPixelVariables);
	PersistentPixelVariablesDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	res = DirectDevice->CreateBuffer(&PersistentPixelVariablesDesc, NULL, &PersistentPixelVariablesRef);
	if (FAILED(res))
		throw new runtime_error("Couldn't create persistent pixel variables ref");

	DirectDeviceCtx->PSSetConstantBuffers(0, 1, &PersistentPixelVariablesRef);

	D3D11_BUFFER_DESC PerDrawPixelVariablesDesc = {};
	PerDrawPixelVariablesDesc.Usage = D3D11_USAGE_DEFAULT;
	PerDrawPixelVariablesDesc.ByteWidth = sizeof(PerDrawPixelVariables);
	PerDrawPixelVariablesDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	res = DirectDevice->CreateBuffer(&PerDrawPixelVariablesDesc, NULL, &PerDrawPixelVariablesRef);
	if (FAILED(res))
		throw new runtime_error("Couldn't create PerDraw pixel variables ref");

	DirectDeviceCtx->PSSetConstantBuffers(1, 1, &PerDrawPixelVariablesRef);

	// Scene loader
	
	for (int BufferNum = 1; BufferNum <= BUFFERS_COUNT; BufferNum++) {

		ModelBuffer Buffer = { };
		Buffer.VertexBuffer = (GeodataVertex*)malloc(MAX_VERTICES * sizeof(GeodataVertex));
		Buffer.IndexBuffer = (uint32_t*)malloc(MAX_INDEXES * sizeof(uint32_t));

		BufferPool.push(Buffer);
	}

	AvailableTexturesLoadCount = BUFFERS_COUNT;

	ExecutionPool = CreateThreadpool(NULL);
	SetThreadpoolThreadMinimum(ExecutionPool, THREADS_COUNT);
	SetThreadpoolThreadMaximum(ExecutionPool, THREADS_COUNT);

	// setup frame
	DirectDeviceCtx->OMSetRenderTargets(1, &RenderTargetView, DepthStencilView);

	DirectDeviceCtx->IASetInputLayout(VertexLayout);

	DirectDeviceCtx->VSSetShader(VertexShader, 0, 0);
	DirectDeviceCtx->PSSetShader(PixelShader, 0, 0);

	// register raw input

	RAWINPUTDEVICE Rid[1] = {};

	Rid[0].usUsagePage = 0x01;
	Rid[0].usUsage = 0x02;
	Rid[0].dwFlags = RIDEV_INPUTSINK;   // adds HID mouse and also ignores legacy mouse messages
	Rid[0].hwndTarget = WindowHandle;

	if (!RegisterRawInputDevices(Rid, 1, sizeof(Rid[0])))
		throw new runtime_error("Couldn't register raw devices");

	// setup light

	PersistentPixelVariables = {};
	PersistentPixelVariables.AmbientColor = { 0.125f, 0.125f, 0.75f };
	PersistentPixelVariables.DiffuseColor = { 0.5f, 0.5f, 75.0f };
	XMStoreFloat3(&PersistentPixelVariables.LightDirection, XMVector3Normalize(XMVectorSet(-0.512651205f, 0.189535633f, -0.837415695f, 1.0f)));

	DirectDeviceCtx->UpdateSubresource(PersistentPixelVariablesRef, 0, NULL, &PersistentPixelVariables, 0, 0);

	// load resources

	GenerateNSWETexture();
	LoadL2Map(Width, Height);
	GeneratePathFindMarkerModel();

	// Scale

	PersistentVertexVariables.ScaleWorld = 100.0f;

	ScaleWorld = 1.0f / PersistentVertexVariables.ScaleWorld;
	ScaleWorldZ = ScaleWorld / 10.0f;

	// setup matrices

	ProjectionMatrix = XMMatrixPerspectiveFovLH(0.5f * (float)M_PI, (float)Width / (float)Height, ToScene(1.0f), ToScene(1500.0f));

	BuildOrthogonalMatrix(Width, Height);

	// path finding

	StartMarker = -1;
	FinishMarker = -1;

	// setup camera

	Up = XMVectorSet(0, 0, 1, 1);

	LoadState();
	/*
	// TODO save camera and position to a file
	CameraAngle.y = 3;
	CameraAngle.x = 0;

	// initial position
	TeleportTo(13100, 140572, -2695);
	// TeleportTo(115143, -179224, 8932);

	// SaveState();
	*/
}

// Window and Input events processing

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

	CameraAngle.y = min(max(0.1f, CameraAngle.y), 3.13f);

	// cout << CameraAngle.x << " " << CameraAngle.y << endl;

	BuildViewMatrix();
	UpdatePersistentVertexVariables();
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

	Displacement = XMVector3Normalize(Displacement) * (float)(ToScene(MoveSpeed) * dt);

	if (XMVector3LengthSq(Displacement).m128_f32[0] <= 0.0)
		return;

	XMVECTOR Position = XMLoadFloat3(&CameraPosition);
	Position += Displacement;
	XMStoreFloat3(&CameraPosition, Position);

	CalcL2MapPosition();
	BuildViewMatrix();
	UpdatePersistentVertexVariables();

	CheckRegions(false);
}

void Geo3DViewForm::Show(void) {

	ShowWindow(WindowHandle, SW_SHOW);
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
	case 'K':
		SwitchNSWETextureMode();
		break;
	case 'V':
		UseVSync = !UseVSync;
		break;
	case 'I':
		SetPathFindStart();
		break;
	case 'O':
		SetPathFindFinish();
		break;
	case 'P':
		FindPath();
		break;
	case 'M':
		DrawMap = !DrawMap;
		break;
	case VK_F5:
		SaveState();
		break;
	case VK_F9:
		LoadState();
		break;
	default:
		return false;
	}

	return true;
}

void Geo3DViewForm::PrintCurrentCoord(void)
{
	int32_t WorldX, WorldY, WorldZ;

	ToWorld(CameraPosition.x, CameraPosition.y, CameraPosition.z, WorldX, WorldY, WorldZ);

	cout << WorldX << " " << WorldY << " " << WorldZ << endl;
}

void Geo3DViewForm::SwitchNSWETextureMode(void)
{
	NSWETextureEnabled = !NSWETextureEnabled;
}

// Window Callback

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
	case WM_SCHEDULED_RESULT: {

		int ID = (int)wParam;

		switch (ID) {
		case ID_MODEL_GENERATION: {
			ReadModelGenerationResult((ModelGenerationRequest*)lParam);
			break;
		}
		case ID_TEXTURE_LOAD: {
			ReadTextureLoadResult((TextureLoadRequest*)lParam);
			break;
		}
		case ID_PATH_FIND: {
			ReadPathFindResult((PathFindRequest*)lParam);
			break;
		}
		case ID_PATH_FIND_DEBUG: {
			ReadPathFindDebug((L2GeodataPathFind*)lParam);
			break;
		}
		default:
			cout << "Unknown scheduled result ID: " << ID << endl;
			break;
		}

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

LRESULT Geo3DViewForm::WndProcCallback(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	return Geo3DViewForm::GetInstance().WndProc(hWnd, Msg, wParam, lParam);
}

// 3D Utils

void Geo3DViewForm::CalcL2MapPosition(void)
{
	int32_t WorldX, WorldY, WorldZ;

	ToWorld(CameraPosition.x, CameraPosition.y, CameraPosition.z, WorldX, WorldY, WorldZ);

	float X = (float)(WorldX - L2Geodata::MAP_MIN_X) / L2Geodata::GEO_COORDS_IN_WORLD_COORDS / L2Geodata::GEO_REGION_SIZE;
	float Y = (float)(WorldY - L2Geodata::MAP_MIN_Y) / L2Geodata::GEO_COORDS_IN_WORLD_COORDS / L2Geodata::GEO_REGION_SIZE;

	X -= 5.0f;

	X /= 11.0f;
	Y /= 16.0f;

	L2MapPlayerPosition = XMVectorSet(X, -Y, 0.0f, 1.0f);
}

void Geo3DViewForm::UpdateAllPerDrawVariables(void)
{
	XMMATRIX InvWorldMatrix = XMMatrixTranspose(WorldMatrix);
	XMStoreFloat4x4(&PerDrawVertexVariables.WorldMatrix, InvWorldMatrix);

	DirectDeviceCtx->UpdateSubresource(PerDrawVertexVariablesRef, 0, NULL, &PerDrawVertexVariables, 0, 0);

	DirectDeviceCtx->UpdateSubresource(PerDrawPixelVariablesRef, 0, NULL, &PerDrawPixelVariables, 0, 0);
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

void Geo3DViewForm::BuildOrthogonalMatrix(unsigned int Width, unsigned int Height)
{
	OrthogonalMatrix = XMMatrixOrthographicLH((float)Width, (float)Height, 0.0, 0.5);
}

void Geo3DViewForm::UpdatePersistentVertexVariables(void)
{
	XMMATRIX FinalMatrix = XMMatrixTranspose(ViewMatrix * ProjectionMatrix);
	XMStoreFloat4x4(&PersistentVertexVariables.FinalMatrix, FinalMatrix);

	XMMATRIX InvOrthogonalMatrix = XMMatrixTranspose(OrthogonalMatrix);
	XMStoreFloat4x4(&PersistentVertexVariables.OrthogonalMatrix, InvOrthogonalMatrix);

	DirectDeviceCtx->UpdateSubresource(PersistentVertexVariablesRef, 0, NULL, &PersistentVertexVariables, 0, 0);
}

void Geo3DViewForm::GenerateNSWETexture(void)
{
	static const int MipMapCount = 1;

	HRESULT res;

	D3D11_TEXTURE2D_DESC TextureDesc = {};
	TextureDesc.Width = L2GeodataModelGenerator::NSWE_TEX_WIDTH;
	TextureDesc.Height = L2GeodataModelGenerator::NSWE_TEX_HEIGHT;
	TextureDesc.MipLevels = MipMapCount;
	TextureDesc.ArraySize = 1;
	TextureDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	TextureDesc.SampleDesc.Count = 1;
	TextureDesc.SampleDesc.Quality = 0;
	TextureDesc.Usage = D3D11_USAGE_DEFAULT;
	TextureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	TextureDesc.CPUAccessFlags = 0;

	res = DirectDevice->CreateTexture2D(&TextureDesc, NULL, &NSWETexture);
	if (FAILED(res))
		throw new runtime_error("Couldn't create NSWE texture");

	res = DirectDevice->CreateShaderResourceView(NSWETexture, NULL, &NSWEView);
	if (FAILED(res))
		throw new runtime_error("Couldn't create NSWE shader view");

	int Width = L2GeodataModelGenerator::NSWE_TEX_WIDTH;
	int Height = L2GeodataModelGenerator::NSWE_TEX_HEIGHT;

	for (int MipMapIndex = 0; MipMapIndex < MipMapCount; MipMapIndex++) {

		uint32_t* Pixels = (uint32_t*) malloc(Width * Height * sizeof(uint32_t));

		if (MipMapCount < 2 || MipMapIndex < MipMapCount - 1)
			L2GeodataModelGenerator::GenerateNSWETexture(Pixels, Width, Height);
		else
			memset(Pixels, 0, Width * Height * sizeof(uint32_t));

		// DumpBMP32((uint8_t*)Pixels, Width, Height, ("H:\\test_" + to_string(MipMapIndex) + ".bmp").c_str());

		DirectDeviceCtx->UpdateSubresource(NSWETexture, MipMapIndex, NULL, Pixels, Width * sizeof(uint32_t), 0);

		free(Pixels);

		Width = Width / 2;
		Height = Height / 2;
	}

	DirectDeviceCtx->PSSetShaderResources(1, 1, &NSWEView);
}

void Geo3DViewForm::LoadL2Map(unsigned int Width, unsigned int Height)
{
	CoInitialize(NULL);

	HRESULT res;

	res = CreateWICTextureFromFile(DirectDevice, L"..\\data\\l2worldmap.bmp", (ID3D11Resource**)&L2MapTexture, &L2MapTextureView);
	if (FAILED(res)) {
		cout << "Couldn't load L2 Map Texture, check if ..\\data\\l2worldmap.jpg exists " << res << endl;
		return;
	}

	D3D11_TEXTURE2D_DESC Desc;
	L2MapTexture->GetDesc(&Desc);

	L2MapScreenPoint = XMVectorSet((float)Width / 2 - Desc.Width, -((float)Height / 2 - Desc.Height), 0.0f, 1.0f);
	L2MapScale = XMVectorSet((float)Desc.Width, (float)Desc.Height, 1.0f, 1.0f);

	GeodataVertex Rect[4 + 3] = { };

	Rect[0].Pos = { 0,  0, 0.0f };
	Rect[1].Pos = { 1,  0, 0.0f };
	Rect[2].Pos = { 0, -1, 0.0f };
	Rect[3].Pos = { 1, -1, 0.0f };

	Rect[0].Tex = { 0, 0 };
	Rect[1].Tex = { 1, 0 };
	Rect[2].Tex = { 0, 1 };
	Rect[3].Tex = { 1, 1 };

	double Angle = M_PI / 2;
	for (int Index = 6; Index >= 4; Index--) {

		float Len = Index == 6 ? 1.5f : 1.0f;

		Rect[Index].Pos = { (float)cos(Angle) * Len, (float)sin(Angle) * Len, 0.0f };
		Rect[Index].Tex = { 0, 0 };

		Angle += M_PI / 1.5;
	}
	
	D3D11_SUBRESOURCE_DATA InitialData = { };

	// creating buffers
	D3D11_BUFFER_DESC VertexBufferDesc = { };
	VertexBufferDesc.ByteWidth = sizeof(Rect);
	VertexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	VertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	InitialData.pSysMem = &Rect;
	res = DirectDevice->CreateBuffer(&VertexBufferDesc, &InitialData, &L2MapVertices);
	if (FAILED(res))
		throw new runtime_error("Couldn't create l2 map rect vertex buffer");
}

void Geo3DViewForm::GeneratePathFindMarkerModel(void)
{
	HRESULT res;

	GeodataVertex Rect[4] = { };

	Rect[0].Pos = { 0,  0, 0.0f };
	Rect[1].Pos = { 1,  0, 0.0f };
	Rect[2].Pos = { 0,  1, 0.0f };
	Rect[3].Pos = { 1,  1, 0.0f };

	Rect[0].Tex = { 0, 0 };
	Rect[1].Tex = { 0, 0 };
	Rect[2].Tex = { 0, 0 };
	Rect[3].Tex = { 0, 0 };

	D3D11_SUBRESOURCE_DATA InitialData = {};

	// creating buffers
	D3D11_BUFFER_DESC VertexBufferDesc = {};
	VertexBufferDesc.ByteWidth = sizeof(Rect);
	VertexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	VertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	InitialData.pSysMem = &Rect;
	res = DirectDevice->CreateBuffer(&VertexBufferDesc, &InitialData, &PathFindMarkerBuffer);
	if (FAILED(res))
		throw new runtime_error("Couldn't create path find marker vertex buffer");
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

float BoolToFloat(bool B) {
	
	return B ? 1.0f : 0.0f;
}

void Geo3DViewForm::DrawScene(void)
{
	// clear the back buffer to a deep blue
	float color[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	DirectDeviceCtx->ClearRenderTargetView(RenderTargetView, color);
	DirectDeviceCtx->ClearDepthStencilView(DepthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	DirectDeviceCtx->PSSetShaderResources(1, 1, &NSWEView);

	WorldMatrix = XMMatrixIdentity();
	PerDrawVertexVariables.IsOrthogonal = BoolToFloat(false);
	PerDrawPixelVariables.IsOrthogonal = BoolToFloat(false);
	PerDrawPixelVariables.LightEnabled = BoolToFloat(NSWETextureEnabled);
	PerDrawPixelVariables.UseNSWE = BoolToFloat(NSWETextureEnabled);
	PerDrawPixelVariables.UseStaticColor = BoolToFloat(NSWETextureEnabled);
	PerDrawPixelVariables.StaticColor = { 1, 1, 1 };
	UpdateAllPerDrawVariables();

	uint32_t TotalVertexCount = 0;

	DirectDeviceCtx->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	DirectDeviceCtx->OMSetDepthStencilState(DepthStencilState3D, 0);
	for (int X = -1; X <= 1; X++)
		for (int Y = -1; Y <= 1; Y++) {

			RegionModel* Model = &Regions[1 + X][1 + Y];

			if (Model->IsModelDone && Model->IndexCount > 0) {

				if (!NSWETextureEnabled && Model->TextureView != NULL) {

					DirectDeviceCtx->PSSetShaderResources(0, 1, &Model->TextureView);

					if (PerDrawPixelVariables.UseStaticColor != BoolToFloat(NSWETextureEnabled) || PerDrawPixelVariables.LightEnabled != BoolToFloat(NSWETextureEnabled)) {
						PerDrawPixelVariables.UseStaticColor = BoolToFloat(NSWETextureEnabled);
						PerDrawPixelVariables.LightEnabled = BoolToFloat(NSWETextureEnabled);
						UpdateAllPerDrawVariables();
					}
				}
				else {

					if (PerDrawPixelVariables.UseStaticColor != BoolToFloat(true) || PerDrawPixelVariables.LightEnabled != BoolToFloat(true)) {
						PerDrawPixelVariables.UseStaticColor = BoolToFloat(true);
						PerDrawPixelVariables.LightEnabled = BoolToFloat(true);
						UpdateAllPerDrawVariables();
					}
				}

				UINT stride = sizeof(GeodataVertex);
				UINT offset = 0;
				DirectDeviceCtx->IASetVertexBuffers(0, 1, &Model->VertexBuffer, &stride, &offset);

				DirectDeviceCtx->IASetIndexBuffer(Model->IndexBuffer, DXGI_FORMAT_R32_UINT, 0);

				DirectDeviceCtx->DrawIndexed(Model->IndexCount, 0, 0);

				TotalVertexCount += Model->VertexCount;
			}
		}

	if (PathFindMarkers.size() > 0) {
		PerDrawVertexVariables.IsOrthogonal = BoolToFloat(false);
		PerDrawPixelVariables.IsOrthogonal = BoolToFloat(false);
		PerDrawPixelVariables.LightEnabled = BoolToFloat(false);
		PerDrawPixelVariables.UseNSWE = BoolToFloat(false);

		DirectDeviceCtx->OMSetDepthStencilState(DepthStencilState3D, 0);
		DirectDeviceCtx->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		UINT stride = sizeof(GeodataVertex);
		UINT offset = 0;
		DirectDeviceCtx->IASetVertexBuffers(0, 1, &PathFindMarkerBuffer, &stride, &offset);

		PerDrawPixelVariables.UseStaticColor = BoolToFloat(true);

		for (PathFindMarker& Marker : PathFindMarkers) {

			PerDrawPixelVariables.StaticColor = Marker.Color;
			WorldMatrix = Marker.WorldMatrix;
			UpdateAllPerDrawVariables();

			DirectDeviceCtx->Draw(4, 0);
		}
	}

	if (DrawMap) {

		PerDrawVertexVariables.IsOrthogonal = BoolToFloat(true);
		PerDrawPixelVariables.IsOrthogonal = BoolToFloat(true);
		PerDrawPixelVariables.LightEnabled = BoolToFloat(false);
		PerDrawPixelVariables.UseNSWE = BoolToFloat(false);

		DirectDeviceCtx->OMSetDepthStencilState(DepthStencilState2D, 0);
		DirectDeviceCtx->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		UINT stride = sizeof(GeodataVertex);
		UINT offset = 0;
		DirectDeviceCtx->IASetVertexBuffers(0, 1, &L2MapVertices, &stride, &offset);

		PerDrawPixelVariables.UseStaticColor = BoolToFloat(false);
		WorldMatrix = XMMatrixAffineTransformation2D(L2MapScale, XMVectorSet(0, 0, 0, 1), 0, L2MapScreenPoint);
		UpdateAllPerDrawVariables();

		DirectDeviceCtx->PSSetShaderResources(0, 1, &L2MapTextureView);
		DirectDeviceCtx->Draw(4, 0);

		PerDrawPixelVariables.UseStaticColor = BoolToFloat(true);
		PerDrawPixelVariables.StaticColor = { 1, 0, 0 };
		WorldMatrix = XMMatrixAffineTransformation2D(XMVectorSet(10, 10, 1, 1), XMVectorSet(0, 0, 0, 1), CameraAngle.x - (float)M_PI, L2MapScreenPoint + (L2MapPlayerPosition * L2MapScale));
		UpdateAllPerDrawVariables();

		DirectDeviceCtx->Draw(3, 4);
	}

	if (SyncQuery != NULL)
		DirectDeviceCtx->End(SyncQuery);

	// switch the back buffer and the front buffer
	SwapChain->Present(UseVSync ? 2 : 0, 0);
}

void Geo3DViewForm::Tick(double dt) {

	ProcessWindowState();
	ProcessKeyboardInput(dt);
	DrawScene();
}

// Scene loader

float Geo3DViewForm::ToScene(float F)
{
	return F * ScaleWorld;
}

void Geo3DViewForm::ToScene(int X, int Y, int Z, float & FX, float & FY, float & FZ)
{
	FX = (float)X * ScaleWorld;
	FY = (float)Y * ScaleWorld;
	FZ = (float)Z * ScaleWorldZ;
}

void Geo3DViewForm::ToWorld(float FX, float FY, float FZ, int32_t& X, int32_t& Y, int32_t& Z)
{
	X = (int)floor(FX / ScaleWorld * L2Geodata::GEO_COORDS_IN_WORLD_COORDS);
	Y = (int)floor(FY / ScaleWorld * L2Geodata::GEO_COORDS_IN_WORLD_COORDS);
	Z = (int)floor(FZ / ScaleWorldZ);
}

void Geo3DViewForm::TeleportTo(int32_t WorldX, int32_t WorldY, int32_t WorldZ)
{
	ToScene(WorldX / L2Geodata::GEO_COORDS_IN_WORLD_COORDS, WorldY / L2Geodata::GEO_COORDS_IN_WORLD_COORDS, WorldZ,
		CameraPosition.x, CameraPosition.y, CameraPosition.z);

	CalcL2MapPosition();
	BuildViewMatrix();
	UpdatePersistentVertexVariables();

	CheckRegions(true);
}

// Path finding

int32_t Geo3DViewForm::SetPathFindMarker(int32_t ExistingMarker, int32_t WorldX, int32_t WorldY, int32_t WorldZ, uint8_t R, uint8_t G, uint8_t B)
{
	float X, Y, Z;
	ToScene(WorldX / L2Geodata::GEO_COORDS_IN_WORLD_COORDS, WorldY / L2Geodata::GEO_COORDS_IN_WORLD_COORDS, WorldZ,
		X, Y, Z);

	const static float Epsilon = 1.0f * ScaleWorldZ;

	PathFindMarker Marker;
	Marker.WorldMatrix = XMMatrixScaling(ScaleWorld, ScaleWorld, 1.0f) * XMMatrixTranslation(X, Y, Z + Epsilon);
	Marker.Color = { R / 255.0f, G / 255.0f, B / 255.0f };

	if (ExistingMarker == -1) {
		PathFindMarkers.push_back(Marker);
		ExistingMarker = (int32_t)PathFindMarkers.size() - 1;
	}
	else
		PathFindMarkers[ExistingMarker] = Marker;

	return ExistingMarker;
}

void Geo3DViewForm::DeletePathFindMarker(int32_t& Marker)
{
	if (Marker != -1) {
		PathFindMarkers.erase(PathFindMarkers.begin() + Marker);
		Marker = -1;
	}
}

void Geo3DViewForm::UpdatePathFindMarkers(void)
{
	PathFindMarkers.clear();
	StartMarker = -1;
	FinishMarker = -1;

	if (!PathFindInProgress) {
		if (HaveStart)
			StartMarker = SetPathFindMarker(StartMarker, Start.x, Start.y, Start.z, 0, 0, 255);

		if (HaveFinish)
			FinishMarker = SetPathFindMarker(FinishMarker, Finish.x, Finish.y, Finish.z, 255, 0, 0);

		if (Path.size() > 2)
			for (int Index = 0; Index < Path.size() - 1; Index++) {

				XMINT3* Point = &Path[Index];

				SetPathFindMarker(-1, Point->x, Point->y, Point->z, 228, 124, 255);
			}
	}
	else {
		for (XMINT3& Point : PointsToCheck)
			SetPathFindMarker(-1, Point.x, Point.y, Point.z, 0, 255, 0);

		for (int Index = 0; Index < CheckedPoints.size(); Index++) {

			XMINT3* Point = &CheckedPoints[Index];

			SetPathFindMarker(-1, Point->x, Point->y, Point->z, 255, 255, 0);
		}
	}
}

bool Geo3DViewForm::GetCurrentGroundCoords(int32_t& WorldX, int32_t& WorldY, int32_t& WorldZ)
{
	ToWorld(CameraPosition.x, CameraPosition.y, CameraPosition.z, WorldX, WorldY, WorldZ);

	int16_t GroundSubBlock, GroundLayerIndex;
	if (!L2Geodata::GetGroundSubBlock(WorldX, WorldY, WorldZ, GroundSubBlock, GroundLayerIndex))
		return false;

	WorldZ = GET_GEO_HEIGHT(GroundSubBlock);

	return true;
}

void Geo3DViewForm::SetPathFindStart(void)
{
	int32_t WorldX, WorldY, WorldZ;
	if (!GetCurrentGroundCoords(WorldX, WorldY, WorldZ))
		return;

	Start = { WorldX, WorldY, WorldZ };
	HaveStart = true;

	UpdatePathFindMarkers();
}

void Geo3DViewForm::SetPathFindFinish(void)
{
	int32_t WorldX, WorldY, WorldZ;
	if (!GetCurrentGroundCoords(WorldX, WorldY, WorldZ))
		return;

	Finish = { WorldX, WorldY, WorldZ };
	HaveFinish = true;

	UpdatePathFindMarkers();
}

void Geo3DViewForm::FindPath(void)
{
	if (HaveStart && HaveFinish) {

		if (!PathFindInProgress) {

			PathFindRequest* Request = new PathFindRequest();
			Request->Start = Start;
			Request->Finish = Finish;

			PTP_WORK Work = CreateThreadpoolWork(PathFindWorkCallback, (PVOID)Request, NULL);
			if (Work == NULL)
				throw new runtime_error("Couldn't create model generation work");

			SubmitThreadpoolWork(Work);
			CloseThreadpoolWork(Work);

			PathFindInProgress = true;
		}
		else
			PathFindScheduled = true;
	}
}

// State load/save

void Geo3DViewForm::LoadState(void)
{
	FILE* StateFile = NULL;
	fopen_s(&StateFile, "..\\data\\state.bin", "r");
	if (StateFile == NULL)
		return;

	// GUI
	fread(&CameraPosition, sizeof(CameraPosition), 1, StateFile);
	fread(&CameraAngle, sizeof(CameraAngle), 1, StateFile);
	fread(&MoveSpeed, sizeof(MoveSpeed), 1, StateFile);

	// path finding
	fread(&HaveStart, sizeof(HaveStart), 1, StateFile);
	fread(&Start, sizeof(Start), 1, StateFile);
	fread(&HaveFinish, sizeof(HaveFinish), 1, StateFile);
	fread(&Finish, sizeof(Finish), 1, StateFile);

	fclose(StateFile);

	UpdatePathFindMarkers();

	BuildViewMatrix();
	UpdatePersistentVertexVariables();

	CalcL2MapPosition();

	CheckRegions();
}

void Geo3DViewForm::SaveState(void)
{
	FILE* StateFile = NULL;
	fopen_s(&StateFile, "..\\data\\state.bin", "w");
	if (StateFile == NULL) {
		cout << "failed to save state" << endl;
		return;
	}

	// GUI
	fwrite(&CameraPosition, sizeof(CameraPosition), 1, StateFile);
	fwrite(&CameraAngle, sizeof(CameraAngle), 1, StateFile);
	fwrite(&MoveSpeed, sizeof(MoveSpeed), 1, StateFile);

	// path finding
	fwrite(&HaveStart, sizeof(HaveStart), 1, StateFile);
	fwrite(&Start, sizeof(Start), 1, StateFile);
	fwrite(&HaveFinish, sizeof(HaveFinish), 1, StateFile);
	fwrite(&Finish, sizeof(Finish), 1, StateFile);

	fclose(StateFile);
}

// General scene setup utils

void Geo3DViewForm::ScheduleModelGeneration(int RegionX, int RegionY, ModelBuffer Buffer)
{
	cout << RegionX << " " << RegionY << " model generation scheduled" << endl;

	ModelGenerationRequest* Request = new ModelGenerationRequest();
	Request->RegionX = RegionX;
	Request->RegionY = RegionY;
	Request->Buffer = Buffer;

	PTP_WORK Work = CreateThreadpoolWork(ModelGenerationWorkCallback, (PVOID)Request, NULL);
	if (Work == NULL)
		throw new runtime_error("Couldn't create model generation work");

	SubmitThreadpoolWork(Work);
	CloseThreadpoolWork(Work);
}

void Geo3DViewForm::ScheduleTextureLoad(int RegionX, int RegionY, int LayerIndex)
{
	TextureLoadRequest* Request = new TextureLoadRequest();
	Request->RegionX = RegionX;
	Request->RegionY = RegionY;
	Request->LayerIndex = LayerIndex;

	PTP_WORK Work = CreateThreadpoolWork(TextureLoadWorkCallback, (PVOID)Request, NULL);
	if (Work == NULL)
		throw new runtime_error("Couldn't create texture load work");

	SubmitThreadpoolWork(Work);
	CloseThreadpoolWork(Work);
}

int Geo3DViewForm::GetGenerationIndexByRegion(int RegionX, int RegionY, bool AutoCreate)
{
	POINT Region = { RegionX, RegionY };

	for (int Index = 0; Index < ScheduledRegions.size(); Index++) {

		ScheduledGeneration* Generation = &ScheduledRegions[Index];

		if (Equals(Generation->Region, Region))
			return Index;
	}

	if (!AutoCreate)
		return -1;

	ScheduledGeneration NewGeneration = { };
	NewGeneration.Region = { RegionX, RegionY };
	ScheduledRegions.push_back(NewGeneration);

	return (int)ScheduledRegions.size() - 1;
}

void Geo3DViewForm::ScheduleRegionLoad(RegionModel* Region, int RegionX, int RegionY)
{
	if (Region->IsModelDone && Region->IsTextureDone)
		return;

	int GenerationIndex = GetGenerationIndexByRegion(RegionX, RegionY, true);
	ScheduledGeneration* Generation = &ScheduledRegions[GenerationIndex];

	if (!Region->IsModelDone && !Generation->IsModelScheduled && !BufferPool.empty()) {

		ModelBuffer Buffer = BufferPool.top();
		BufferPool.pop();
		
		ScheduleModelGeneration(RegionX, RegionY, Buffer);

		Generation->IsModelDone = false;
		Generation->IsModelScheduled = true;
	}

	if (!Region->IsTextureDone && !Generation->IsTextureScheduled && AvailableTexturesLoadCount > 0) {

		AvailableTexturesLoadCount--;

		ScheduleTextureLoad(RegionX, RegionY, 0);

		Generation->IsTextureDone = false;
		Generation->IsTextureScheduled = true;
	}
}

void Geo3DViewForm::ScheduleRegions(void)
{
	const static POINT NeighborOrder[9] = {
		{ 0,  0 },
		{ 0,  1 }, {  0, -1 },
		{ 1,  0 }, { -1,  0 },
		{ 1,  1 }, { -1, -1 },
		{ 1, -1 }, { -1,  1 }
	};

	for (const POINT& Point : NeighborOrder) {

		RegionModel* Region = &Regions[1 + Point.x][1 + Point.y];
		ScheduleRegionLoad(Region, CenterRegion.x + Point.x, CenterRegion.y + Point.y);
	}
}

void Geo3DViewForm::CheckRegions(bool ForceSchedule)
{
	POINT CurrentRegion = { (int)floor(CameraPosition.x / ScaleWorld / REGION_SIZE), (int)floor(CameraPosition.y / ScaleWorld / REGION_SIZE) };
	POINT Direction = SubtractPoint(CurrentRegion, CenterRegion);

	if (Direction.x != 0 || Direction.y != 0) {

		cout << "Moved in " << Direction.x << " " << Direction.y << endl;

		RegionModels NewRegions = { };

		for (int SrcX = -1; SrcX <= 1; SrcX++)
			for (int SrcY = -1; SrcY <= 1; SrcY++) {

				POINT Src = { SrcX, SrcY };
				POINT Dest = SubtractPoint(Src, Direction);

				RegionModel* SrcRegion = &Regions[1 + Src.x][1 + Src.y];
				RegionModel* DestRegion = &NewRegions[1 + Dest.x][1 + Dest.y];

				// Dest for this region is out of bound
				if (Dest.x >= -1 && Dest.x <= 1 && Dest.y >= -1 && Dest.y <= 1)
					*DestRegion = *SrcRegion;
				else
					SrcRegion->Free();
			}

		memcpy(&Regions[0][0], &NewRegions[0][0], sizeof(Regions));

		CenterRegion = CurrentRegion;
	}
	else
	if (!ForceSchedule)
		return;

	ScheduleRegions();
}

void Geo3DViewForm::CleanupGeneration(int GenerationIndex)
{
	ScheduledGeneration* Generation = &ScheduledRegions[GenerationIndex];

	if (Generation->IsModelDone && Generation->IsTextureDone)
		ScheduledRegions.erase(ScheduledRegions.begin() + GenerationIndex);
}

void Geo3DViewForm::ModelGenerationWork(ModelGenerationRequest* Request)
{
	L2GeodataModelGenerator Generator;

	static const int REGION_TO_WORLD = REGION_SIZE * L2Geodata::GEO_COORDS_IN_WORLD_COORDS;

	int32_t WorldX = Request->RegionX * REGION_TO_WORLD;
	int32_t WorldY = Request->RegionY * REGION_TO_WORLD;

	Request->VertexCount = MAX_VERTICES;
	Request->IndexCount = MAX_INDEXES;

	Generator.GenerateGeodataScene(WorldX, WorldY, 1 * REGION_TO_WORLD, 1 * REGION_TO_WORLD, ScaleWorld, ScaleWorldZ,
		Request->Buffer.VertexBuffer, Request->VertexCount, Request->Buffer.IndexBuffer, Request->IndexCount);

	if (Request->VertexCount > 0) {

		HRESULT res;

		D3D11_SUBRESOURCE_DATA InitialData = {};

		// creating buffers
		D3D11_BUFFER_DESC VertexBufferDesc = {};
		VertexBufferDesc.ByteWidth = sizeof(GeodataVertex) * Request->VertexCount;
		VertexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
		VertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		InitialData.pSysMem = Request->Buffer.VertexBuffer;
		res = DirectDevice->CreateBuffer(&VertexBufferDesc, &InitialData, &Request->VertexBuffer);
		if (FAILED(res))
			throw new runtime_error("Couldn't create vertex buffer");

		D3D11_BUFFER_DESC IndexBufferDesc = {};
		IndexBufferDesc.ByteWidth = sizeof(uint32_t) * Request->IndexCount;
		IndexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
		IndexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		InitialData.pSysMem = Request->Buffer.IndexBuffer;
		res = DirectDevice->CreateBuffer(&IndexBufferDesc, &InitialData, &Request->IndexBuffer);
		if (FAILED(res))
			throw new runtime_error("Couldn't create index buffer");
	}
	else {
		Request->VertexBuffer = NULL;
		Request->IndexBuffer = NULL;
	}

	PostMessage(WindowHandle, WM_SCHEDULED_RESULT, ID_MODEL_GENERATION, (LPARAM)Request);
}

void Geo3DViewForm::TextureLoadWork(TextureLoadRequest* Request)
{
	int32_t WorldX = (Request->RegionX * REGION_SIZE) * L2Geodata::GEO_COORDS_IN_WORLD_COORDS;
	int32_t WorldY = (Request->RegionY * REGION_SIZE) * L2Geodata::GEO_COORDS_IN_WORLD_COORDS;

	uint32_t GeoX, GeoY;

	L2Geodata::WorldToGeo(WorldX, WorldY, &GeoX, &GeoY);

	int RegionX = L2Geodata::GEO_X_FIRST + GeoX / L2Geodata::GEO_REGION_SIZE;
	int RegionY = L2Geodata::GEO_Y_FIRST + GeoY / L2Geodata::GEO_REGION_SIZE;

	int SubRegionX = (GeoX * 4 / L2Geodata::GEO_REGION_SIZE) % 4;
	int SubRegionY = (GeoY * 4 / L2Geodata::GEO_REGION_SIZE) % 4;

	wstring FileName =
		L"H:\\MAPS\\" + to_wstring(RegionX) + L"_" + to_wstring(RegionY) + L"_" + to_wstring(SubRegionX) + L"_" + to_wstring(SubRegionY) +
		(Request->LayerIndex > 0 ? to_wstring(Request->LayerIndex) : L"") + L".jpg";

	CoInitialize(NULL);

	HRESULT res;

	res = CreateWICTextureFromFile(DirectDevice, FileName.c_str(), (ID3D11Resource**) &Request->Texture, &Request->TextureView, 2048);
	if (FAILED(res)) {

		if (Request->Texture != NULL) {
			Request->Texture->Release();
			Request->Texture = NULL;
		}

		if (Request->TextureView != NULL) {
			Request->TextureView->Release();
			Request->TextureView = NULL;
		}
	}
	else {

		D3D11_TEXTURE2D_DESC Desc;

		Request->Texture->GetDesc(&Desc);

		wcout << "loaded texture " << FileName << " " << Desc.Width << " " << Desc.Height << endl;
	}

	PostMessage(WindowHandle, WM_SCHEDULED_RESULT, ID_TEXTURE_LOAD, (LPARAM)Request);
}

void Geo3DViewForm::PathFindWork(PathFindRequest* Request)
{
	L2GeodataPathFind Search;

	Request->Found = Search.FindPath(Request->Start, Request->Finish, Request->Path, Request->Weight, PathFindDebugCallback);

	PostMessage(WindowHandle, WM_SCHEDULED_RESULT, ID_PATH_FIND, (LPARAM)Request);
}

void Geo3DViewForm::ReadModelGenerationResult(ModelGenerationRequest* Request)
{
	int GenerationIndex = GetGenerationIndexByRegion(Request->RegionX, Request->RegionY, false);
	if (GenerationIndex == -1)
		throw new runtime_error("Model generation result came while there was no scheduled generation");

	ScheduledGeneration* Generation = &ScheduledRegions[GenerationIndex];

	if (!Generation->IsModelScheduled)
		throw new runtime_error("Model generation result came while there was no scheduled generation, but generation instance is present"); 
	
	if (Generation->IsModelDone)
		throw new runtime_error("Model generation result came while there was model done already");

	Generation->IsModelScheduled = false;
	Generation->IsModelDone = true;

	// trying to find corresponding active region

	POINT Region = SubtractPoint(Generation->Region, CenterRegion);
	if (Region.x >= -1 && Region.x <= 1 && Region.y >= -1 && Region.y <= 1) {

		RegionModel* Model = &Regions[1 + Region.x][1 + Region.y];

		if (Model->IsModelDone)
			throw new runtime_error("Active region already have a model, wasteful generation detected");

		Model->VertexCount = Request->VertexCount;
		Model->IndexCount = Request->IndexCount;

		Model->VertexBuffer = Request->VertexBuffer;
		Model->IndexBuffer = Request->IndexBuffer;

		Model->IsModelDone = true;

		cout << "Model generated for " << Generation->Region.x << " " << Generation->Region.y << endl;
	}
	else {
		if (Request->VertexBuffer)
			Request->VertexBuffer->Release();

		if (Request->IndexBuffer)
			Request->IndexBuffer->Release();
	}

	// put buffer back to pool
	BufferPool.push(Request->Buffer);
	
	// request is done now

	delete Request;

	CleanupGeneration(GenerationIndex);

	// there might be some regions waiting for buffer we just returned
	ScheduleRegions();
}

void Geo3DViewForm::ReadTextureLoadResult(TextureLoadRequest* Request)
{
	int GenerationIndex = GetGenerationIndexByRegion(Request->RegionX, Request->RegionY, false);
	if (GenerationIndex == -1)
		throw new runtime_error("Texture load result came while there was no scheduled load");

	ScheduledGeneration* Generation = &ScheduledRegions[GenerationIndex];

	if (!Generation->IsTextureScheduled)
		throw new runtime_error("Texture load result came while there was no scheduled load, but generation instance is present");

	if (Generation->IsTextureDone)
		throw new runtime_error("Texture load result came while there was texture loaded already");

	Generation->IsTextureScheduled = false;
	Generation->IsTextureDone = true;

	// trying to find corresponding active region

	POINT Region = SubtractPoint(Generation->Region, CenterRegion);
	if (Region.x >= -1 && Region.x <= 1 && Region.y >= -1 && Region.y <= 1) {

		RegionModel* Model = &Regions[1 + Region.x][1 + Region.y];

		if (Model->IsTextureDone)
			throw new runtime_error("Active region already have a texture, wasteful load detected");

		Model->Texture = Request->Texture;
		Model->TextureView = Request->TextureView;

		Model->IsTextureDone = true;

		cout << "Texture loaded for " << Generation->Region.x << " " << Generation->Region.y << endl;
	}
	else {
		if (Request->Texture)
			Request->Texture->Release();

		if (Request->TextureView)
			Request->TextureView->Release();
	}

	// return terxtures to pool
	AvailableTexturesLoadCount++;

	// request is done now

	delete Request;

	CleanupGeneration(GenerationIndex);

	// there might be some regions waiting for buffer we just returned
	ScheduleRegions();
}

void Geo3DViewForm::ReadPathFindResult(PathFindRequest* Request)
{
	if (!PathFindInProgress)
		throw new runtime_error("Path find result came when there was no search in progress");

	Path = Request->Path;
	if (!Request->Found)
		Path.clear();

	cout << "Path find result: " << Path.size() << " points" << endl;
	cout << "Path found: " << Request->Found << endl;
	cout << "Path weight: " << Request->Weight << endl;

	if (Path.size() >= 2) {
		
		Finish = Path[0];
		Start = Path[Path.size() - 1];
	}

	PathFindInProgress = false;

	UpdatePathFindMarkers();

	delete Request;

	if (PathFindScheduled) {
		PathFindScheduled = false;
		FindPath();
	}
}

void Geo3DViewForm::ReadPathFindDebug(L2GeodataPathFind* PathFind)
{
	PointsToCheck = PathFind->GetPointsToCheck();
	CheckedPoints = PathFind->GetCheckedPoints();

	UpdatePathFindMarkers();

	// cout << "debug call, " << PointsToCheck.size() << ", " << CheckedPoints.size() << endl;
}

uint32_t Geo3DViewForm::PathFindDebug(L2GeodataPathFind& PathFind)
{
	SendMessage(WindowHandle, WM_SCHEDULED_RESULT, ID_PATH_FIND_DEBUG, LPARAM(&PathFind));

	return 0;
}

// Loader Callbacks

VOID Geo3DViewForm::ModelGenerationWorkCallback(PTP_CALLBACK_INSTANCE Instance, PVOID Context, PTP_WORK Work)
{
	ModelGenerationRequest* Request = (ModelGenerationRequest*)Context;
	Geo3DViewForm::GetInstance().ModelGenerationWork(Request);
}

VOID Geo3DViewForm::TextureLoadWorkCallback(PTP_CALLBACK_INSTANCE Instance, PVOID Context, PTP_WORK Work)
{
	TextureLoadRequest* Request = (TextureLoadRequest*)Context;
	Geo3DViewForm::GetInstance().TextureLoadWork(Request);
}

VOID Geo3DViewForm::PathFindWorkCallback(PTP_CALLBACK_INSTANCE Instance, PVOID Context, PTP_WORK Work)
{
	PathFindRequest* Request = (PathFindRequest*)Context;
	Geo3DViewForm::GetInstance().PathFindWork(Request);
}

uint32_t Geo3DViewForm::PathFindDebugCallback(L2GeodataPathFind& PathFind)
{
	return Geo3DViewForm::GetInstance().PathFindDebug(PathFind);
}

// RegionModel

void Geo3DViewForm::RegionModel::Free(void)
{
	if (VertexBuffer)
		VertexBuffer->Release();
	
	if (IndexBuffer)
		IndexBuffer->Release();

	if (Texture)
		Texture->Release();

	if (TextureView)
		TextureView->Release();
}