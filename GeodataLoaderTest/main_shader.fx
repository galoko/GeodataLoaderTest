cbuffer ShaderVariables : register(b0)
{
    float4x4 WorldMatrix;
    float4x4 FinalMatrix;
};

cbuffer LightOptions : register(b1)
{
    float3 AmbientColor;
    float3 DiffuseColor;
    float3 LightDirection;
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
    float3 Tex1   : TEXCOORD1;
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
    output.Pos = mul(float4(input.Pos, 1.0f), FinalMatrix);
    output.Tex0 = input.Tex0;
    output.Tex1 = input.Tex1;
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
    if (input.Tex0.x >= 0.0)
        Color = MainTexture.Sample(SmoothSampler, input.Tex0);
    else
        Color = float4(1.0f, 1.0f, 1.0f, 1.0f);
        
    // then light
    Color = ApplyLight(Color, input.Normal);
    
    // then NSWE texture if any
    if (input.Tex1.z >= 0.0) {
        
        float2 NSWE_Tex;
        NSWE_Tex.x = input.Tex1.x;
        NSWE_Tex.y = input.Tex1.z + (input.Tex1.y % 1.0f) / 16.0f;
        
        float4 NSWE_Color = NSWETexture.Sample(PointSampler, NSWE_Tex);
        
        if (NSWE_Color.a == 1.0f)
            Color = NSWE_Color;
    }

    return Color;
}
