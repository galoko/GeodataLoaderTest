cbuffer Transformation : register( b0 )
{
    matrix FinalMatrix;
};

SamplerState Sampler : register( s0 );
Texture2D Texture : register( t0 );

//--------------------------------------------------------------------------------------
struct VS_INPUT
{
    float3 Pos   : POSITION;
    float3 Color : COLOR;
    float3 Tex   : TEXCOORD;
};

struct PS_INPUT
{
    float4 Pos   : SV_POSITION;
    float4 Color : COLOR;
    float3 Tex   : TEXCOORD;
};


//--------------------------------------------------------------------------------------
// Vertex Shader
//--------------------------------------------------------------------------------------
PS_INPUT VS( VS_INPUT input )
{
    PS_INPUT output;
    output.Pos = mul(float4(input.Pos, 1.0f), FinalMatrix);
    output.Color = float4(input.Color, 1.0f);
    output.Tex = input.Tex;

    return output;
}


//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float4 PS( PS_INPUT input) : SV_Target
{    
    float2 Tex;
    Tex.x = input.Tex.x;
    Tex.y = input.Tex.z + (input.Tex.y % 1.0f) / 16.0f;

    return saturate(input.Color - (1.0f - Texture.Sample(Sampler, Tex)));
}
