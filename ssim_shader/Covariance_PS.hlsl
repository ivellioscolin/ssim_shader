//--------------------------------------------------------------------------------------
// Constant Buffer Variables
//--------------------------------------------------------------------------------------
Texture2D txDiffuse1 : register(t0);
Texture2D txDiffuse2 : register(t1);
SamplerState samLinear : register(s0);

cbuffer cbAveragePair : register(b0)
{
    float average1;
    float average2;
    float pad[2];
};

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------

struct PS_INPUT
{
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD0;
};

float PS(PS_INPUT input) : SV_Target
{
    float pixel1 = txDiffuse1.Sample(samLinear, input.Tex).x;
    float pixel2 = txDiffuse2.Sample(samLinear, input.Tex).x;
    float output = (pixel1 - average1) * (pixel2 - average2);
    return output;
}
