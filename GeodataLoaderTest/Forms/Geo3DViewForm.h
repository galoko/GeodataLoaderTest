#include "stdafx.h"

#include <inttypes.h>
#define _USE_MATH_DEFINES
#include <cmath>
#include <array>

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

	XMMATRIX WorldMatrix;
	XMMATRIX ViewMatrix;
	XMMATRIX ProjectionMatrix;

	struct ShaderMatricesStruct {
		XMFLOAT4X4 WorldMatrix;
		XMFLOAT4X4 FinalMatrix;
	};

	ShaderMatricesStruct ShaderMatrices;
	ID3D11Buffer *ShaderMatricesRef;

	struct LightOptionsStruct {

		XMFLOAT3 AmbientColor;
		float Padding1;

		XMFLOAT3 DiffuseColor;
		float Padding2;

		XMFLOAT3 LightDirection;
		float Padding3;
	};

	LightOptionsStruct LightOptions;
	ID3D11Buffer *LightOptionsRef;

	ID3D11Texture2D *NSWETexture;
	ID3D11ShaderResourceView *NSWEView;

	static const int MAX_VERTICES = 700 * 1000 * 22;
	GeodataVertex *VertexBuffer;
	uint32_t VertexBufferSize;
	ID3D11Buffer *SceneVertexBuffer;

	static const int MAX_INDEXES = MAX_VERTICES * 2;
	uint32_t *IndexBuffer;
	uint32_t IndexBufferSize;
	ID3D11Buffer *SceneIndexBuffer;

	ID3D11SamplerState *PointSampler;
	ID3D11SamplerState *SmoothSampler;

	ID3D11RasterizerState *SolidMode;
	ID3D11RasterizerState *WireFrameMode;

	// for input lag reduction
	ID3D11Query *SyncQuery;

	// DirectX Scene definition

	// GUI

	XMFLOAT3 CameraPosition;
	XMFLOAT2 CameraAngle;
	XMVECTOR TargetVector;
	XMVECTOR Up;

	bool DrawTrianglesAsLines;
	bool UseVSync = true;
	bool IsInFocus;

#define KEYS_COUNT 256
	bool PressedKeys[KEYS_COUNT];

	float MoveSpeed = 30.0f;

	void BuildViewMatrix(void);
	void BuildWorldMatrix(void);
	void UpdateShaderVariables(void);

	void CommitGeodataScene(void);

	void GenerateNSWETexture(void);
	void GenerateGeodataScene(int32_t WorldX, int32_t WorldY, uint32_t Width, uint32_t Height);

	void PrintCurrentCoord(void);
	bool HandleKeyPress(int Key);

	void ProcessWindowState(void);
	void ProcessMouseInput(LONG dx, LONG dy);
	void ProcessKeyboardInput(double dt);

	void DrawScene(void);

	LRESULT WndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
	static LRESULT CALLBACK WndProcCallback(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
public:
	void Init(unsigned int Width, unsigned int Height, WCHAR *WindowClass, WCHAR *Title, HINSTANCE hInstance);
	void Show(void);
	void WaitForNextFrame(void);
	void Tick(double dt);
};