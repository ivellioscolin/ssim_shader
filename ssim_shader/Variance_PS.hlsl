//--------------------------------------------------------------------------------------
// Constant Buffer Variables
//--------------------------------------------------------------------------------------
Texture2D txDiffuse : register( t0 );
SamplerState samLinear : register( s0 );

cbuffer cbAverageSingle : register(b0)
{
    float average;
    float pad[3];
};

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------

struct PS_INPUT
{
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD0;
};

float PS( PS_INPUT input) : SV_Target
{
    float pixel = txDiffuse.Sample(samLinear, input.Tex).x;
    float output = (pixel - average) * (pixel - average);
    return output;
}
