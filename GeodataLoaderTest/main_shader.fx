cbuffer VertexPersistent : register(b0)
{
    float4x4 FinalMatrix;
    float4x4 OrthogonalMatrix;
    float    ScaleWorld;
};

cbuffer VertexPerDraw : register(b1)
{
    float4x4 WorldMatrix;
    float    IsOrthogonal_V;
}

cbuffer PixelPersistent : register(b0)
{
    float3 AmbientColor;
    float3 DiffuseColor;
    float3 LightDirection;
}

cbuffer PixelPerDraw : register(b1)
{
    float  LightEnabled;
    float  IsOrthogonal_P;
    float  UseStaticColor;
    float  UseNSWE;
    float3 StaticColor;
}

SamplerState SmoothSampler : register(s0);
SamplerState PointSampler  : register(s1);

Texture2D MainTexture : register(t0);
Texture2D NSWETexture : register(t1);

//--------------------------------------------------------------------------------------
struct VS_INPUT
{
    float3 Pos    : POSITION;
    float2 Tex0   : TEXCOORD0;
    float  NSWE   : NSWECOORD;
    float3 Normal : NORMAL;
};

struct PS_INPUT
{
    float4 Pos    : SV_POSITION;
    float2 Tex0   : TEXCOORD0;
    float3 Tex1   : TEXCOORD1;
    float3 Normal : NORMAL;
};

//--------------------------------------------------------------------------------------
// Vertex Shader
//--------------------------------------------------------------------------------------
PS_INPUT VS(VS_INPUT input)
{
    PS_INPUT output;
    
    output.Pos = mul(float4(input.Pos, 1.0f), WorldMatrix);
    
    if (IsOrthogonal_V)
        output.Pos = mul(output.Pos, OrthogonalMatrix);
    else
        output.Pos = mul(output.Pos, FinalMatrix);
        
    output.Tex0 = input.Tex0;
    output.Tex1 = float3(input.Pos.x * ScaleWorld, input.Pos.y * ScaleWorld, input.NSWE);
    output.Normal = input.Normal;

    return output;
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------

float4 ApplyLight(float4 Color, float3 Normal) {

    float4 Result = float4(AmbientColor, 1.0f);
    
    float LightIntensity = saturate(dot(Normal, -LightDirection));
    
    if (LightIntensity > 0.0f)
        Result += float4(DiffuseColor, 1.0f) * LightIntensity;
    
    Result = saturate(Result) * Color;
    
    return Result;
}

float4 PS(PS_INPUT input) : SV_Target
{
    float4 Color;

    // texture or white color
    if (input.Tex0.x < 0.0 || UseStaticColor)
        Color = float4(StaticColor, 1.0f);
    else
        Color = MainTexture.Sample(SmoothSampler, input.Tex0);
        
    // don't have texture or light is enabled for all
    if (!IsOrthogonal_P && input.Tex0.x < 0.0 || LightEnabled)
        Color = ApplyLight(Color, input.Normal);
    
    // then NSWE texture if any
    if (input.Tex1.z >= 0.0 && UseNSWE) {
        
        float Offset = input.Tex1.z;
        if (input.Tex1.y < 0)
            Offset += 1;
        
        float2 NSWE_Tex;
        NSWE_Tex.x = input.Tex1.x;
        NSWE_Tex.y = (Offset + input.Tex1.y % 1.0f) / 16.0f;
        
        float4 NSWE_Color = NSWETexture.Sample(PointSampler, NSWE_Tex);
        
        if (NSWE_Color.a == 1.0f)
             Color = NSWE_Color;
    }

    return Color;
}