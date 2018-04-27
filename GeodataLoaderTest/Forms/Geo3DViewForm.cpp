#include "stdafx.h"

#include "Geo3DViewForm.h"

#include "FormsUtils.h"
#include "TimeUtils.h"
#include "MathUtils.h"

#include <algorithm>
#include <stdexcept>
#include <iostream>
#include <D3Dcompiler.h>

#pragma comment (lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

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

	res = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, 0, D3D11_CREATE_DEVICE_SINGLETHREADED | D3D11_CREATE_DEVICE_DEBUG, NULL, 0,
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
		throw new runtime_error("Couldn't compile pixel shader");
	}

	res = DirectDevice->CreatePixelShader(PSBlob->GetBufferPointer(), PSBlob->GetBufferSize(), NULL, &PixelShader);
	if (FAILED(res))
		throw new runtime_error("Couldn't create pixel shader");

	// creating input layout
	static const D3D11_INPUT_ELEMENT_DESC VertexLayoutDesc[4] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 1, DXGI_FORMAT_R32G32B32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 }
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

	// creating buffers
	D3D11_BUFFER_DESC SceneVertexBufferDesc = {};
	SceneVertexBufferDesc.ByteWidth = sizeof(GeodataVertex) * MAX_VERTICES;
	SceneVertexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	SceneVertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	res = DirectDevice->CreateBuffer(&SceneVertexBufferDesc, NULL, &SceneVertexBuffer);
	if (FAILED(res))
		throw new runtime_error("Couldn't create vertex buffer");

	D3D11_BUFFER_DESC SceneIndexBufferDesc = {};
	SceneIndexBufferDesc.ByteWidth = sizeof(uint32_t) * MAX_INDEXES;
	SceneIndexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	SceneIndexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	res = DirectDevice->CreateBuffer(&SceneIndexBufferDesc, NULL, &SceneIndexBuffer);
	if (FAILED(res))
		throw new runtime_error("Couldn't create index buffer");

	// create shader variables references

	D3D11_BUFFER_DESC ShaderVariablesDesc = { };
	ShaderVariablesDesc.Usage = D3D11_USAGE_DEFAULT;
	ShaderVariablesDesc.ByteWidth = sizeof(ShaderMatrices);
	ShaderVariablesDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	res = DirectDevice->CreateBuffer(&ShaderVariablesDesc, NULL, &ShaderMatricesRef);
	if (FAILED(res))
		throw new runtime_error("Couldn't create shader matrices ref");

	DirectDeviceCtx->VSSetConstantBuffers(0, 1, &ShaderMatricesRef);

	D3D11_BUFFER_DESC LightOptionsDesc = { };
	LightOptionsDesc.Usage = D3D11_USAGE_DEFAULT;
	LightOptionsDesc.ByteWidth = sizeof(LightOptions);
	LightOptionsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	res = DirectDevice->CreateBuffer(&LightOptionsDesc, NULL, &LightOptionsRef);
	if (FAILED(res))
		throw new runtime_error("Couldn't create shader light options ref");

	DirectDeviceCtx->PSSetConstantBuffers(1, 1, &LightOptionsRef);

	// setup frame
	DirectDeviceCtx->OMSetRenderTargets(1, &RenderTargetView, DepthStencilView);

	DirectDeviceCtx->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	DirectDeviceCtx->IASetInputLayout(VertexLayout);

	DirectDeviceCtx->VSSetShader(VertexShader, 0, 0);
	DirectDeviceCtx->PSSetShader(PixelShader, 0, 0);

	UINT stride = sizeof(GeodataVertex);
	UINT offset = 0;
	DirectDeviceCtx->IASetVertexBuffers(0, 1, &SceneVertexBuffer, &stride, &offset);

	DirectDeviceCtx->IASetIndexBuffer(SceneIndexBuffer, DXGI_FORMAT_R32_UINT, 0);

	// register raw input

	RAWINPUTDEVICE Rid[1] = {};

	Rid[0].usUsagePage = 0x01;
	Rid[0].usUsage = 0x02;
	Rid[0].dwFlags = RIDEV_INPUTSINK;   // adds HID mouse and also ignores legacy mouse messages
	Rid[0].hwndTarget = WindowHandle;

	if (!RegisterRawInputDevices(Rid, 1, sizeof(Rid[0])))
		throw new runtime_error("Couldn't register raw devices");

	// init buffers

	VertexBuffer = new GeodataVertex[MAX_VERTICES];
	IndexBuffer = new uint32_t[MAX_INDEXES];

	// setup camera

	CameraAngle.y = 3;
	CameraAngle.x = 0;

	CameraPosition.z = -269.5f;

	Up = XMVectorSet(0, 0, 1, 1);

	// setup matrices

	ProjectionMatrix = XMMatrixPerspectiveFovLH(0.5f * (float)M_PI, (float)Width / (float)Height, 0.1f, 4500.0f);

	BuildViewMatrix();
	BuildWorldMatrix();
	UpdateShaderVariables();

	// setup light

	LightOptions = {};
	LightOptions.AmbientColor = { 0.125f, 0.125f, 0.75f };
	LightOptions.DiffuseColor = { 0.5f, 0.5f, 75.0f };
	XMStoreFloat3(&LightOptions.LightDirection, XMVector3Normalize(XMVectorSet(-0.512651205f, 0.189535633f, -0.837415695f, 1.0f)));

	DirectDeviceCtx->UpdateSubresource(LightOptionsRef, 0, NULL, &LightOptions, 0, 0);

	// load resources

	GenerateNSWETexture();

	// scene init

	GenerateGeodataScene(13100, 140572, 16 * 512 * 1, 16 * 512 * 1);
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

void Geo3DViewForm::GenerateNSWETexture(void)
{
	HRESULT res;

	uint32_t Pixels[L2GeodataModelGenerator::NSWE_TEX_HEIGHT][L2GeodataModelGenerator::NSWE_TEX_WIDTH];

	L2GeodataModelGenerator::GenerateNSWETexture(Pixels);

	D3D11_SUBRESOURCE_DATA InitialData = {};
	InitialData.pSysMem = &Pixels[0][0];
	InitialData.SysMemPitch = sizeof(Pixels[0]);

	D3D11_TEXTURE2D_DESC TextureDesc = {};
	TextureDesc.Width = L2GeodataModelGenerator::NSWE_TEX_WIDTH;
	TextureDesc.Height = L2GeodataModelGenerator::NSWE_TEX_HEIGHT;
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

	DirectDeviceCtx->PSSetShaderResources(1, 1, &NSWEView);

	// DumpBMP32((uint8_t*)&Pixels[0][0], NSWE_TEX_WIDTH, NSWE_TEX_HEIGHT, "H:\\test.bmp");
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
	WorldMatrix = XMMatrixIdentity();
}

void Geo3DViewForm::UpdateShaderVariables(void)
{
	XMMATRIX InvWorldMatrix = XMMatrixTranspose(WorldMatrix);
	XMStoreFloat4x4(&ShaderMatrices.WorldMatrix, InvWorldMatrix);

	XMMATRIX FinalMatrix = XMMatrixTranspose(WorldMatrix * ViewMatrix * ProjectionMatrix);
	XMStoreFloat4x4(&ShaderMatrices.FinalMatrix, FinalMatrix);

	DirectDeviceCtx->UpdateSubresource(ShaderMatricesRef, 0, NULL, &ShaderMatrices, 0, 0);
}

size_t GetHash(uint32_t *Data, int Count) {

	hash<uint32_t> Hasher;

	size_t Result = 0;

	for (int Index = 0; Index < Count; Index++)
		Result = Result * 31 + Hasher(Data[Index]);

	return Result;
}

void Geo3DViewForm::CommitGeodataScene(void) {

	D3D11_BOX Region = { };
	Region.bottom = 1;
	Region.back = 1;
	
	Region.right = VertexBufferSize * sizeof(*VertexBuffer);
	DirectDeviceCtx->UpdateSubresource(SceneVertexBuffer, 0, &Region, VertexBuffer, 0, 0);

	Region.right = IndexBufferSize * sizeof(*IndexBuffer);
	DirectDeviceCtx->UpdateSubresource(SceneIndexBuffer, 0, &Region, IndexBuffer, 0, 0);
}

void Geo3DViewForm::GenerateGeodataScene(int32_t WorldX, int32_t WorldY, uint32_t Width, uint32_t Height)
{
	LONGLONG StartTime = GetTime();

	L2GeodataModelGenerator Generator;

	VertexBufferSize = MAX_VERTICES;
	IndexBufferSize = MAX_INDEXES;
	Generator.GenerateGeodataScene(WorldX, WorldY, Width, Height, VertexBuffer, VertexBufferSize, IndexBuffer, IndexBufferSize);

	CommitGeodataScene();

	LONGLONG EndTime = GetTime();

	cout << VertexBufferSize << " verticies was used" << endl;
	cout << IndexBufferSize << " indices was used" << endl;
	cout << VertexBufferSize * sizeof(*VertexBuffer) + IndexBufferSize * sizeof(*IndexBuffer) << " bytes was used" << endl;
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
	UpdateShaderVariables();
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

LRESULT Geo3DViewForm::WndProcCallback(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	return Geo3DViewForm::GetInstance().WndProc(hWnd, Msg, wParam, lParam);
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
	UpdateShaderVariables();
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

	DirectDeviceCtx->DrawIndexed(IndexBufferSize, 0, 0);

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