cbuffer Transformation : register( b0 )
{
    matrix FinalMatrix;
};

cbuffer Options : register( b1 )
{
    float LightEnabled;
};

//--------------------------------------------------------------------------------------
struct VS_INPUT
{
    float3 Pos : POSITION;
    float3 Color : COLOR;
    float3 Normal : NORMAL;
};

struct PS_INPUT
{
    float4 Pos : SV_POSITION;
    float4 Color : COLOR;
    float3 Normal : NORMAL;
};


//--------------------------------------------------------------------------------------
// Vertex Shader
//--------------------------------------------------------------------------------------
PS_INPUT VS( VS_INPUT input )
{
    PS_INPUT output = (PS_INPUT)0;
    output.Pos = mul(float4(input.Pos, 1.0f), FinalMatrix);
    output.Color = float4(input.Color, 1.0f);
    output.Normal = input.Normal;

    return output;
}


//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float4 PS( PS_INPUT input) : SV_Target
{
    if (LightEnabled < 0.5)
        return input.Color;

    float4 ambientColor = float4(0.125f, 0.125f, 0.75f, 1.0f);
    float4 diffuseColor = float4(0.5f, 0.5f, 75.0f, 1.0f);
    float3 lightDirection = float3(0.0f, 0.0f, -1.0f);

    float4 textureColor = input.Color;
    float3 lightDir;
    float lightIntensity;
    float4 color;

    // Set the default output color to the ambient light value for all pixels.
    color = ambientColor;
    
    // Invert the light direction for calculations.
    lightDir = -lightDirection;

    // Calculate the amount of light on this pixel.
    lightIntensity = saturate(dot(input.Normal, lightDir));
    
    if(lightIntensity > 0.0f)
    {
        // Determine the final diffuse color based on the diffuse color and the amount of light intensity.
        color += (diffuseColor * lightIntensity);
    }

    // Saturate the final light color.
    color = saturate(color);

    // Multiply the texture pixel and the final diffuse color to get the final pixel color result.
    color = color * textureColor;

    return color;
}
