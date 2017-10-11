// ssim_shader.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <Windows.h>
#include <d3d11.h>
#include <DirectXMath.h>
#include <Shlwapi.h>
#include <vector>
#include <string>
#include "Passthrough_VS.h"
#include "Average_PS.h"
#include "Variance_PS.h"
#include "Covariance_PS.h"

template <class T> inline void SafeRelease(T*& pT)
{
    if (pT != nullptr)
    {
        pT->Release();
        pT = nullptr;
    }
}

inline void SafeCloseHandle(HANDLE& h)
{
    if (h != nullptr)
    {
        ::CloseHandle(h);
        h = nullptr;
    }
}

template <class T> inline void SafeFree(T*& pT)
{
    if (pT != nullptr)
    {
        ::free(pT);
        pT = nullptr;
    }
}

typedef enum _STEREO_TYPE
{
    STEREO_TYPE_2D,
    STEREO_TYPE_3D_SBS,
    STEREO_TYPE_3D_TB,
}STEREO_TYPE, *PSTEREO_TYPE;

const PCHAR STEREO_TYPE_NAME[] = {
    "2D", 
    "3D - SBS",
    "3D - TB",
};

#define VALIDATE_PASS_MSG "Stereo mode validation result: PASS"
#define VALIDATE_FAIL_MSG "Stereo mode validation result: FAIL"

typedef enum _STEREO_EYE
{
    STEREO_EYE_LEFT = 0,
    STEREO_EYE_RIGHT = 1,
    STEREO_EYE_COUNT = 2,
}STEREO_EYE, *PSTEREO_EYE;

using namespace DirectX;

typedef struct _VERTEX
{
    XMFLOAT3 Pos;
    XMFLOAT2 Tex;
}VERTEX;

struct CBAverageSingle
{
    float average;
    float pad[3];
};

struct CBAveragePair
{
    float average1;
    float average2;
    float pad[2];
};

void ProcessCapture(ID3D11Device* pDev, ID3D11DeviceContext* pDevCtx, ID3D11Texture2D *pData)
{
    BOOL isSuccess = TRUE;
    HRESULT hr = S_OK;

    BYTE* pBits = NULL;
    DWORD width = 0;
    DWORD height = 0;
    DWORD pitch = 0;

    D3D11_TEXTURE2D_DESC Desc;
    pData->GetDesc(&Desc);

    ID3D11Texture2D* pCaptureTex = nullptr;

    D3D11_TEXTURE2D_DESC DescCapture;
    DescCapture = Desc;
    DescCapture.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    DescCapture.Usage = D3D11_USAGE_STAGING;
    DescCapture.MiscFlags = 0;
    DescCapture.BindFlags = 0;
    hr = pDev->CreateTexture2D(&DescCapture, nullptr, &pCaptureTex);

    SYSTEMTIME curTime = { 0 };
    GetLocalTime(&curTime);
    wchar_t path[MAX_PATH * 2];
    ZeroMemory(path, MAX_PATH * 2 * sizeof(wchar_t));
    GetCurrentDirectory(MAX_PATH * 2, path);

    wchar_t buf[MAX_PATH * 2];
    ZeroMemory(buf, MAX_PATH * 2 * sizeof(wchar_t));

    pDevCtx->CopyResource(pCaptureTex, pData);

    for (UINT resIdx = 0; (SUCCEEDED(hr)) && (resIdx < Desc.MipLevels * Desc.ArraySize); resIdx++)
    {
        UINT mipSlice = resIdx % Desc.MipLevels;
        UINT arraySlice = resIdx / Desc.MipLevels;
        UINT idx = D3D11CalcSubresource(mipSlice, arraySlice, Desc.MipLevels);

        D3D11_MAPPED_SUBRESOURCE mappedResource = { 0 };
        hr = pDevCtx->Map(pCaptureTex, idx, D3D11_MAP_READ, 0, &mappedResource);
        pBits = (PBYTE)mappedResource.pData;
        pitch = mappedResource.RowPitch;
        width = Desc.Width >> (1 * mipSlice);
        height = Desc.Height >> (1 * mipSlice);

        FILE *pFile = NULL;
        swprintf_s(buf, MAX_PATH * 2, L"%s\\%04d%02d%02d_%02d%02d%02d-%03d_array%d_mip%d.y",
            path, curTime.wYear, curTime.wMonth, curTime.wDay, curTime.wHour, curTime.wMinute, curTime.wSecond, curTime.wMilliseconds, arraySlice, mipSlice);
        if (_wfopen_s(&pFile, buf, L"wb") == 0)
        {
            fwrite(pBits, pitch * height, 1, pFile);
            fclose(pFile);
        }

        pDevCtx->Unmap(pCaptureTex, idx);
    }
    if (pCaptureTex)
    {
        pCaptureTex->Release();
    }
}

HRESULT AdjustStereoVertexBuffer(VERTEX *pVB, BOOL isRightEye, STEREO_TYPE sType)
{
    HRESULT hr = S_OK;
    VERTEX vertices[] =
    {
        { XMFLOAT3(-1.0f, -1.0f, 0.0f), XMFLOAT2(0.0f, 1.0f) },
        { XMFLOAT3(-1.0f, 1.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) },
        { XMFLOAT3(1.0f, 1.0f, 0.0f), XMFLOAT2(1.0f, 0.0f) },
        { XMFLOAT3(1.0f, -1.0f, 0.0f), XMFLOAT2(1.0f, 1.0f) },
    };
    VERTEX verticesSBS_Left[] =
    {
        { XMFLOAT3(-1.0f, -1.0f, 0.0f), XMFLOAT2(0.0f, 1.0f) },
        { XMFLOAT3(-1.0f, 1.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) },
        { XMFLOAT3(1.0f, 1.0f, 0.0f), XMFLOAT2(0.5f, 0.0f) },
        { XMFLOAT3(1.0f, -1.0f, 0.0f), XMFLOAT2(0.5f, 1.0f) },
    };
    VERTEX verticesSBS_Right[] =
    {
        { XMFLOAT3(-1.0f, -1.0f, 0.0f), XMFLOAT2(0.5f, 1.0f) },
        { XMFLOAT3(-1.0f, 1.0f, 0.0f), XMFLOAT2(0.5f, 0.0f) },
        { XMFLOAT3(1.0f, 1.0f, 0.0f), XMFLOAT2(1.0f, 0.0f) },
        { XMFLOAT3(1.0f, -1.0f, 0.0f), XMFLOAT2(1.0f, 1.0f) },
    };
    VERTEX verticesTB_Top[] =
    {
        { XMFLOAT3(-1.0f, -1.0f, 0.0f), XMFLOAT2(0.0f, 0.5f) },
        { XMFLOAT3(-1.0f, 1.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) },
        { XMFLOAT3(1.0f, 1.0f, 0.0f), XMFLOAT2(1.0f, 0.0f) },
        { XMFLOAT3(1.0f, -1.0f, 0.0f), XMFLOAT2(1.0f, 0.5f) },
    };
    VERTEX verticesTB_Bottom[] =
    {
        { XMFLOAT3(-1.0f, -1.0f, 0.0f), XMFLOAT2(0.0f, 1.0f) },
        { XMFLOAT3(-1.0f, 1.0f, 0.0f), XMFLOAT2(0.0f, 0.5f) },
        { XMFLOAT3(1.0f, 1.0f, 0.0f), XMFLOAT2(1.0f, 0.5f) },
        { XMFLOAT3(1.0f, -1.0f, 0.0f), XMFLOAT2(1.0f, 1.0f) },
    };
    switch (sType)
    {
    case STEREO_TYPE_2D:
    {
        RtlCopyMemory(pVB, vertices, sizeof(vertices));
        break;
    }
    case STEREO_TYPE_3D_SBS:
    {
        if (isRightEye)
        {
            RtlCopyMemory(pVB, verticesSBS_Right, sizeof(verticesSBS_Right));
        }
        else
        {
            RtlCopyMemory(pVB, verticesSBS_Left, sizeof(verticesSBS_Left));
        }
        break;
    }
    case STEREO_TYPE_3D_TB:
    {
        if (isRightEye)
        {
            RtlCopyMemory(pVB, verticesTB_Bottom, sizeof(verticesTB_Bottom));
        }
        else
        {
            RtlCopyMemory(pVB, verticesTB_Top, sizeof(verticesTB_Top));
        }
        break;
    }
    default:
        hr = E_INVALIDARG;
        break;
    }

    return hr;
}

UINT GetMip1Value(ID3D11Device* pDev, ID3D11DeviceContext* pDevCtx, ID3D11Texture2D *pData)
{
    UINT pixVal = 0;
    HRESULT hr = S_OK;

    D3D11_TEXTURE2D_DESC Desc;
    pData->GetDesc(&Desc);

    ID3D11Texture2D* pCaptureTex = nullptr;

    D3D11_TEXTURE2D_DESC DescCapture;
    DescCapture = Desc;
    DescCapture.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    DescCapture.Usage = D3D11_USAGE_STAGING;
    DescCapture.MiscFlags = 0;
    DescCapture.BindFlags = 0;
    hr = pDev->CreateTexture2D(&DescCapture, nullptr, &pCaptureTex);
    if(SUCCEEDED(hr))
    {
        pDevCtx->CopyResource(pCaptureTex, pData);
        UINT mipSlice = (Desc.MipLevels * Desc.ArraySize - 1) % Desc.MipLevels;
        UINT arraySlice = (Desc.MipLevels * Desc.ArraySize - 1) / Desc.MipLevels;
        UINT idx = D3D11CalcSubresource(mipSlice, arraySlice, Desc.MipLevels);
        D3D11_MAPPED_SUBRESOURCE mappedResource = { 0 };
        hr = pDevCtx->Map(pCaptureTex, idx, D3D11_MAP_READ, 0, &mappedResource);
        if (SUCCEEDED(hr))
        {
            pixVal = *((UINT16*)mappedResource.pData);
        }
        pDevCtx->Unmap(pCaptureTex, idx);
    }
    SafeRelease(pCaptureTex);
    return pixVal;
}

void ValidateStereoFormat(CONST PWCHAR pFileName, UINT32 width, UINT32 height, STEREO_TYPE sType, BOOL &isHighCl, double &ssim)
{
    HRESULT hr = S_OK;
    D3D_DRIVER_TYPE driverType = D3D_DRIVER_TYPE_NULL;
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    ID3D11Device *pDx11Dev = NULL;
    ID3D11DeviceContext *pDx11DevCtx = NULL;
    ID3D11VertexShader *pVSPassThrough = NULL;
    ID3D11InputLayout *pInputLayout = NULL;
    ID3D11PixelShader *pPSAverage = NULL;
    ID3D11PixelShader *pPSVariance = NULL;
    ID3D11PixelShader *pPSCovariance = NULL;
    ID3D11Buffer *pIB = NULL;
    ID3D11SamplerState *pSamplerLinear = NULL;
    D3D11_VIEWPORT viewport = { 0 };
    UINT side = 1024;// 2^10
    viewport.Width = (FLOAT)side;
    viewport.Height = (FLOAT)side;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;

    UINT createDeviceFlags = 0;
#ifdef _DEBUG
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_DRIVER_TYPE driverTypes[] =
    {
        D3D_DRIVER_TYPE_HARDWARE,
        D3D_DRIVER_TYPE_WARP,
        D3D_DRIVER_TYPE_REFERENCE,
    };
    UINT numDriverTypes = ARRAYSIZE(driverTypes);

    D3D_FEATURE_LEVEL featureLevels[] =
    {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };
    UINT numFeatureLevels = ARRAYSIZE(featureLevels);

    if (SUCCEEDED(hr))
    {
        for (UINT driverTypeIndex = 0; driverTypeIndex < numDriverTypes; driverTypeIndex++)
        {
            driverType = driverTypes[driverTypeIndex];
            hr = D3D11CreateDevice(NULL, driverType, NULL, createDeviceFlags, featureLevels, numFeatureLevels,
                D3D11_SDK_VERSION, &pDx11Dev, &featureLevel, &pDx11DevCtx);
            if (SUCCEEDED(hr))
                break;
        }
    }

    if (SUCCEEDED(hr))
    {
        hr = pDx11Dev->CreateVertexShader(g_Passthrough_VS, ARRAYSIZE(g_Passthrough_VS), nullptr, &pVSPassThrough);
    }

    if (SUCCEEDED(hr))
    {
        D3D11_INPUT_ELEMENT_DESC Layout[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        UINT NumElements = ARRAYSIZE(Layout);
        hr = pDx11Dev->CreateInputLayout(Layout, NumElements, g_Passthrough_VS, ARRAYSIZE(g_Passthrough_VS), &pInputLayout);
    }

    if (SUCCEEDED(hr))
    {
        hr = pDx11Dev->CreatePixelShader(g_Average_PS, ARRAYSIZE(g_Average_PS), nullptr, &pPSAverage);
    }
    if (SUCCEEDED(hr))
    {
        hr = pDx11Dev->CreatePixelShader(g_Variance_PS, ARRAYSIZE(g_Variance_PS), nullptr, &pPSVariance);
    }
    if (SUCCEEDED(hr))
    {
        hr = pDx11Dev->CreatePixelShader(g_Covariance_PS, ARRAYSIZE(g_Covariance_PS), nullptr, &pPSCovariance);
    }

    WORD indices[] =
    {
        0, 1, 3,
        3, 1, 2,
    };

    D3D11_BUFFER_DESC BufferDesc;
    ZeroMemory(&BufferDesc, sizeof(BufferDesc));
    BufferDesc.ByteWidth = sizeof(WORD) * ARRAYSIZE(indices);
    BufferDesc.Usage = D3D11_USAGE_DEFAULT;
    BufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    BufferDesc.CPUAccessFlags = 0;
    D3D11_SUBRESOURCE_DATA InitData;
    ZeroMemory(&InitData, sizeof(InitData));
    InitData.pSysMem = indices;
    if (SUCCEEDED(hr))
    {
        hr = pDx11Dev->CreateBuffer(&BufferDesc, &InitData, &pIB);
    }

    if (SUCCEEDED(hr))
    {
        D3D11_SAMPLER_DESC SampDesc;
        RtlZeroMemory(&SampDesc, sizeof(SampDesc));
        SampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        SampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        SampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        SampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        SampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        SampDesc.MinLOD = 0;
        SampDesc.MaxLOD = D3D11_FLOAT32_MAX;
        hr = pDx11Dev->CreateSamplerState(&SampDesc, &pSamplerLinear);
    }

    {
        float average[STEREO_EYE_COUNT] = { 0.0f };
        float stdDeviation[STEREO_EYE_COUNT] = { 0.0f };
        float covariance = 0.0f;

        HANDLE hYuvFile = NULL;
        LARGE_INTEGER fileSize = { 0 };
        hYuvFile = CreateFile(pFileName, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hYuvFile)
        {
            if (!GetFileSizeEx(hYuvFile, &fileSize))
            {
                hr = E_INVALIDARG;
            }
        }
        else
        {
            hr = E_INVALIDARG;
        }

        PBYTE pYuvBuf = (PBYTE)malloc(fileSize.LowPart);
        DWORD bytesRead = 0;
        if (SUCCEEDED(hr))
        {
            if (!ReadFile(hYuvFile, pYuvBuf, fileSize.LowPart, &bytesRead, NULL))
            {
                hr = E_INVALIDARG;
            }
            else if (fileSize.LowPart != bytesRead)
            {
                hr = E_FAIL;
            }
        }
        SafeCloseHandle(hYuvFile);

        ID3D11Texture2D *pTexYUV = NULL;
        ID3D11ShaderResourceView *pSrvYUV = NULL;
        D3D11_TEXTURE2D_DESC texYUVDesc = { 0 };
        texYUVDesc.Width = width;
        texYUVDesc.Height = height;
        texYUVDesc.MipLevels = 1;
        texYUVDesc.ArraySize = 1;
        texYUVDesc.Format = DXGI_FORMAT_NV12;
        texYUVDesc.SampleDesc.Count = 1;
        texYUVDesc.SampleDesc.Quality = 0;
        texYUVDesc.Usage = D3D11_USAGE_DEFAULT;
        texYUVDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        texYUVDesc.CPUAccessFlags = 0;
        texYUVDesc.MiscFlags = 0;

        D3D11_SUBRESOURCE_DATA initData = { 0 };
        RtlZeroMemory(&initData, sizeof(initData));
        initData.pSysMem = pYuvBuf;
        initData.SysMemPitch = texYUVDesc.Width;
        initData.SysMemSlicePitch = 0;
        if (SUCCEEDED(hr))
        {
            hr = pDx11Dev->CreateTexture2D(&texYUVDesc, &initData, &pTexYUV);
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvYUVDesc = { DXGI_FORMAT_UNKNOWN, D3D_SRV_DIMENSION_UNKNOWN,{ 0 } };
        srvYUVDesc.Format = DXGI_FORMAT_R8_UNORM;
        srvYUVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvYUVDesc.Texture2D.MostDetailedMip = 0;
        srvYUVDesc.Texture2D.MipLevels = texYUVDesc.MipLevels;
        if (SUCCEEDED(hr))
        {
            hr = pDx11Dev->CreateShaderResourceView(pTexYUV, &srvYUVDesc, &pSrvYUV);
        }

        VERTEX vertices[] =
        {
            { XMFLOAT3(-1.0f, -1.0f, 0.0f), XMFLOAT2(0.0f, 1.0f) },
            { XMFLOAT3(-1.0f, 1.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) },
            { XMFLOAT3(1.0f, 1.0f, 0.0f), XMFLOAT2(1.0f, 0.0f) },
            { XMFLOAT3(1.0f, -1.0f, 0.0f), XMFLOAT2(1.0f, 1.0f) },
        };
        float ClearColor[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
        UINT stride = sizeof(VERTEX);
        UINT offset = 0;
        D3D11_SUBRESOURCE_DATA InitData;
        ZeroMemory(&InitData, sizeof(InitData));
        InitData.pSysMem = vertices;

        D3D11_TEXTURE2D_DESC ssimTexDesc = { 0 };
        D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = { DXGI_FORMAT_UNKNOWN, D3D11_RTV_DIMENSION_UNKNOWN,{ 0 } };
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = { DXGI_FORMAT_UNKNOWN, D3D_SRV_DIMENSION_UNKNOWN,{ 0 } };
        ID3D11Texture2D *pSSIMTex[STEREO_EYE_COUNT] = { NULL, NULL };
        ID3D11RenderTargetView *pSSIMTexRtv[STEREO_EYE_COUNT] = { NULL, NULL };
        ID3D11ShaderResourceView *pSSIMTexSrv[STEREO_EYE_COUNT] = { NULL, NULL };
        ID3D11Texture2D *pSSIMTexVariance[STEREO_EYE_COUNT] = { NULL, NULL };
        ID3D11RenderTargetView *pSSIMTexVarianceRtv[STEREO_EYE_COUNT] = { NULL, NULL };
        ID3D11ShaderResourceView *pSSIMTexVarianceSrv[STEREO_EYE_COUNT] = { NULL, NULL };
        ID3D11Buffer *pVB[STEREO_EYE_COUNT] = { NULL, NULL };
        ID3D11Buffer *pCBAverageSingle[STEREO_EYE_COUNT] = { NULL, NULL };
        ID3D11Buffer *pCBAveragePair = NULL;
        ID3D11Texture2D *pSSIMTexCovariance = NULL;
        ID3D11RenderTargetView *pSSIMTexCovarianceRtv = NULL;
        ID3D11ShaderResourceView *pSSIMTexCovarianceSrv = NULL;
        ID3D11Buffer *pVBCovariance = NULL;
        if (SUCCEEDED(hr))
        {
            RtlCopyMemory(&ssimTexDesc, &texYUVDesc, sizeof(texYUVDesc));
            //ssimTexDesc.Format = DXGI_FORMAT_R8_UNORM;
            ssimTexDesc.Format = DXGI_FORMAT_R16_UNORM;// To reduce accuracy lost during calculation
            ssimTexDesc.Width = side;
            ssimTexDesc.Height = side;
            ssimTexDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            ssimTexDesc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
            ssimTexDesc.MipLevels = 0;

            rtvDesc.Format = ssimTexDesc.Format;
            rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            rtvDesc.Texture2DArray.MipSlice = 0;

            srvDesc.Format = ssimTexDesc.Format;
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MostDetailedMip = 0;
            srvDesc.Texture2D.MipLevels = -1;

            for (UINT eyeIdx = 0; eyeIdx < STEREO_EYE_COUNT; eyeIdx++)
            {
                // Prepare resources
                hr = pDx11Dev->CreateTexture2D(&ssimTexDesc, NULL, &pSSIMTex[eyeIdx]);
                if (FAILED(hr))
                {
                    break;
                }
                hr = pDx11Dev->CreateTexture2D(&ssimTexDesc, NULL, &pSSIMTexVariance[eyeIdx]);
                if (FAILED(hr))
                {
                    break;
                }
                hr = pDx11Dev->CreateRenderTargetView(pSSIMTex[eyeIdx], &rtvDesc, &pSSIMTexRtv[eyeIdx]);
                if (FAILED(hr))
                {
                    break;
                }
                hr = pDx11Dev->CreateRenderTargetView(pSSIMTexVariance[eyeIdx], &rtvDesc, &pSSIMTexVarianceRtv[eyeIdx]);
                if (FAILED(hr))
                {
                    break;
                }
                hr = pDx11Dev->CreateShaderResourceView(pSSIMTex[eyeIdx], &srvDesc, &pSSIMTexSrv[eyeIdx]);
                if (FAILED(hr))
                {
                    break;
                }
                hr = pDx11Dev->CreateShaderResourceView(pSSIMTexVariance[eyeIdx], &srvDesc, &pSSIMTexVarianceSrv[eyeIdx]);
                if (FAILED(hr))
                {
                    break;
                }

                hr = AdjustStereoVertexBuffer(vertices, eyeIdx, sType);
                if (FAILED(hr))
                {
                    break;
                }

                ZeroMemory(&BufferDesc, sizeof(BufferDesc));
                BufferDesc.ByteWidth = sizeof(VERTEX) * ARRAYSIZE(vertices);
                BufferDesc.Usage = D3D11_USAGE_DEFAULT;
                BufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
                BufferDesc.CPUAccessFlags = 0;
                hr = pDx11Dev->CreateBuffer(&BufferDesc, &InitData, &pVB[eyeIdx]);
                if (FAILED(hr))
                {
                    break;
                }

                // Now draw NV12 to Y only
                pDx11DevCtx->ClearRenderTargetView(pSSIMTexRtv[eyeIdx], ClearColor);
                pDx11DevCtx->OMSetRenderTargets(1, &pSSIMTexRtv[eyeIdx], NULL);
                pDx11DevCtx->RSSetViewports(1, &viewport);
                pDx11DevCtx->IASetInputLayout(pInputLayout);
                pDx11DevCtx->IASetVertexBuffers(0, 1, &pVB[eyeIdx], &stride, &offset);
                pDx11DevCtx->IASetIndexBuffer(pIB, DXGI_FORMAT_R16_UINT, 0);
                pDx11DevCtx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                pDx11DevCtx->VSSetShader(pVSPassThrough, NULL, 0);
                pDx11DevCtx->PSSetShader(pPSAverage, NULL, 0);
                pDx11DevCtx->PSSetSamplers(0, 1, &pSamplerLinear);
                pDx11DevCtx->PSSetShaderResources(0, 1, &pSrvYUV);
                pDx11DevCtx->DrawIndexed(ARRAYSIZE(indices), 0, 0);

                // Generate all mips
                pDx11DevCtx->GenerateMips(pSSIMTexSrv[eyeIdx]);
                //ProcessCapture(pDx11Dev, pDx11DevCtx, pSSIMTex[eyeIdx]);

                // Get average
                average[eyeIdx] = (float)GetMip1Value(pDx11Dev, pDx11DevCtx, pSSIMTex[eyeIdx]) / 65535.0f * 255.0f;

                ZeroMemory(&BufferDesc, sizeof(BufferDesc));
                BufferDesc.ByteWidth = sizeof(CBAverageSingle);
                BufferDesc.Usage = D3D11_USAGE_DEFAULT;
                BufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
                hr = pDx11Dev->CreateBuffer(&BufferDesc, NULL, &pCBAverageSingle[eyeIdx]);
                if (FAILED(hr))
                {
                    break;
                }

                CBAverageSingle cbAverage = { 0 };
                cbAverage.average = average[eyeIdx] / 255.0f;
                pDx11DevCtx->UpdateSubresource(pCBAverageSingle[eyeIdx], 0, NULL, &cbAverage, 0, 0);

                pDx11DevCtx->ClearRenderTargetView(pSSIMTexVarianceRtv[eyeIdx], ClearColor);
                pDx11DevCtx->OMSetRenderTargets(1, &pSSIMTexVarianceRtv[eyeIdx], NULL);
                pDx11DevCtx->RSSetViewports(1, &viewport);
                pDx11DevCtx->IASetInputLayout(pInputLayout);
                pDx11DevCtx->IASetVertexBuffers(0, 1, &pVB[eyeIdx], &stride, &offset);
                pDx11DevCtx->IASetIndexBuffer(pIB, DXGI_FORMAT_R16_UINT, 0);
                pDx11DevCtx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                pDx11DevCtx->PSSetConstantBuffers(0, 1, &pCBAverageSingle[eyeIdx]);
                pDx11DevCtx->VSSetShader(pVSPassThrough, NULL, 0);
                pDx11DevCtx->PSSetShader(pPSVariance, NULL, 0);
                pDx11DevCtx->PSSetSamplers(0, 1, &pSamplerLinear);
                pDx11DevCtx->PSSetShaderResources(0, 1, &pSrvYUV);
                pDx11DevCtx->DrawIndexed(ARRAYSIZE(indices), 0, 0);

                // Generate all mips
                pDx11DevCtx->GenerateMips(pSSIMTexVarianceSrv[eyeIdx]);
                //ProcessCapture(pDx11Dev, pDx11DevCtx, pSSIMTexVariance[eyeIdx]);

                // Get standard deviation
                stdDeviation[eyeIdx] = sqrt(GetMip1Value(pDx11Dev, pDx11DevCtx, pSSIMTexVariance[eyeIdx]) * (float)side * (float)side / ((float)side * (float)side - 1.0f));
            }
        }

        if (SUCCEEDED(hr))
        {
            hr = pDx11Dev->CreateTexture2D(&ssimTexDesc, NULL, &pSSIMTexCovariance);
        }
        if (SUCCEEDED(hr))
        {
            hr = pDx11Dev->CreateRenderTargetView(pSSIMTexCovariance, &rtvDesc, &pSSIMTexCovarianceRtv);
        }
        if (SUCCEEDED(hr))
        {
            hr = pDx11Dev->CreateShaderResourceView(pSSIMTexCovariance, &srvDesc, &pSSIMTexCovarianceSrv);
        }
        if (SUCCEEDED(hr))
        {
            hr = AdjustStereoVertexBuffer(vertices, 0, STEREO_TYPE_2D);
        }

        ZeroMemory(&BufferDesc, sizeof(BufferDesc));
        BufferDesc.ByteWidth = sizeof(VERTEX) * ARRAYSIZE(vertices);
        BufferDesc.Usage = D3D11_USAGE_DEFAULT;
        BufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        BufferDesc.CPUAccessFlags = 0;
        if (SUCCEEDED(hr))
        {
            hr = pDx11Dev->CreateBuffer(&BufferDesc, &InitData, &pVBCovariance);
        }

        ZeroMemory(&BufferDesc, sizeof(BufferDesc));
        BufferDesc.ByteWidth = sizeof(CBAveragePair);
        BufferDesc.Usage = D3D11_USAGE_DEFAULT;
        BufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (SUCCEEDED(hr))
        {
            hr = pDx11Dev->CreateBuffer(&BufferDesc, NULL, &pCBAveragePair);
        }

        CBAveragePair cbAveragePair = { 0 };
        cbAveragePair.average1 = average[STEREO_EYE_LEFT] / 255.0f;
        cbAveragePair.average2 = average[STEREO_EYE_RIGHT] / 255.0f;
        pDx11DevCtx->UpdateSubresource(pCBAveragePair, 0, NULL, &cbAveragePair, 0, 0);

        pDx11DevCtx->ClearRenderTargetView(pSSIMTexCovarianceRtv, ClearColor);
        pDx11DevCtx->OMSetRenderTargets(1, &pSSIMTexCovarianceRtv, NULL);
        pDx11DevCtx->RSSetViewports(1, &viewport);
        pDx11DevCtx->IASetInputLayout(pInputLayout);
        pDx11DevCtx->IASetVertexBuffers(0, 1, &pVBCovariance, &stride, &offset);
        pDx11DevCtx->IASetIndexBuffer(pIB, DXGI_FORMAT_R16_UINT, 0);
        pDx11DevCtx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        pDx11DevCtx->PSSetConstantBuffers(0, 1, &pCBAveragePair);
        pDx11DevCtx->VSSetShader(pVSPassThrough, NULL, 0);
        pDx11DevCtx->PSSetShader(pPSCovariance, NULL, 0);
        pDx11DevCtx->PSSetSamplers(0, 1, &pSamplerLinear);
        pDx11DevCtx->PSSetShaderResources(STEREO_EYE_LEFT, 1, &pSSIMTexSrv[STEREO_EYE_LEFT]);
        pDx11DevCtx->PSSetShaderResources(STEREO_EYE_RIGHT, 1, &pSSIMTexSrv[STEREO_EYE_RIGHT]);
        pDx11DevCtx->DrawIndexed(ARRAYSIZE(indices), 0, 0);

        pDx11DevCtx->GenerateMips(pSSIMTexCovarianceSrv);
        //ProcessCapture(pDx11Dev, pDx11DevCtx, pSSIMTexCovariance);
        covariance = GetMip1Value(pDx11Dev, pDx11DevCtx, pSSIMTexCovariance) * (float)side * (float)side / ((float)side * (float)side - 1.0f);

        // Calculate SSIM
        float k1 = 0.01f;
        float k2 = 0.03f;
        float L = 255.0f;
        float c1 = pow((k1*L), 2.0f);
        float c2 = pow((k2*L), 2.0f);
        float c3 = c2 / 2;
        double ssimNumerator = (2 * average[STEREO_EYE_LEFT] * average[STEREO_EYE_RIGHT] + c1) * (2 * covariance + c2);
        double ssimDenominator = (pow(average[STEREO_EYE_LEFT], 2.0f) + pow(average[STEREO_EYE_RIGHT], 2.0f) + c1) * (pow(stdDeviation[STEREO_EYE_LEFT], 2.0f) + pow(stdDeviation[STEREO_EYE_RIGHT], 2.0f) + c2);
        ssim = ssimNumerator / ssimDenominator;

        // Cleanup
        for (UINT eyeIdx = 0; eyeIdx < STEREO_EYE_COUNT; eyeIdx++)
        {
            SafeRelease(pCBAverageSingle[eyeIdx]);
            SafeRelease(pVB[eyeIdx]);
            SafeRelease(pSSIMTexVarianceSrv[eyeIdx]);
            SafeRelease(pSSIMTexVarianceRtv[eyeIdx]);
            SafeRelease(pSSIMTexVariance[eyeIdx]);
            SafeRelease(pSSIMTexSrv[eyeIdx]);
            SafeRelease(pSSIMTexRtv[eyeIdx]);
            SafeRelease(pSSIMTex[eyeIdx]);
        }
        SafeRelease(pVBCovariance);
        SafeRelease(pSSIMTexCovarianceSrv);
        SafeRelease(pSSIMTexCovarianceRtv);
        SafeRelease(pSSIMTexCovariance);
        SafeRelease(pCBAveragePair);
        SafeRelease(pSrvYUV);
        SafeRelease(pTexYUV);
        SafeFree(pYuvBuf);
    }
    SafeRelease(pVSPassThrough);
    SafeRelease(pInputLayout);
    SafeRelease(pPSCovariance);
    SafeRelease(pPSVariance);
    SafeRelease(pPSAverage);
    SafeRelease(pIB);
    SafeRelease(pSamplerLinear);
    SafeRelease(pDx11DevCtx);
    SafeRelease(pDx11Dev);

    // All frame should has SSIM no less than 0.8, given specificy stereo type
    if (ssim < 0.8)
    {
        isHighCl = FALSE;
    }
    else
    {
        isHighCl = TRUE;
    }
}

void ShowHelp()
{
    printf("******************************************************\n");
    printf("Usage:\n");
    printf("ssim_shader <filename> <width> <height> <stereo_type>\n");
    printf("\nStereo Type :\n");
    for (UINT idx = 0; idx < ARRAYSIZE(STEREO_TYPE_NAME); idx++)
    {
        printf("  %d: %s\n", idx, STEREO_TYPE_NAME[idx]);
    }
    printf("******************************************************\n");
}

int wmain(int argc, wchar_t *argv[], wchar_t *envp[])
{
    if (argc != 5)
    {
        printf("Invalid number of parameters!\n");
        ShowHelp();
        return -1;
    }
    if (!PathFileExists(argv[1]))
    {
        printf("Input file doesn't exists!\n");
        ShowHelp();
        return -1;
    }
    INT width = _wtoi(argv[2]);
    INT height = _wtoi(argv[3]);
    if ((width < 0) || (height < 0))
    {
        printf("Width and height must be positive!\n");
        return -1;
    }
    STEREO_TYPE sType = (STEREO_TYPE)_wtoi(argv[4]);

    LARGE_INTEGER qpfFreq;
    double qpfPeroid;
    QueryPerformanceFrequency(&qpfFreq);
    qpfPeroid = 1 / ((double)qpfFreq.QuadPart);
    qpfPeroid *= 1000000.0;

    LARGE_INTEGER measureStart = { 0 };
    LARGE_INTEGER measureEnd = { 0 };
    LARGE_INTEGER ElapsedMicroseconds = { 0 };

    BOOL highConfidenceLevel = FALSE;
    double ssim = 0.0f;

    QueryPerformanceCounter(&measureStart);
    ValidateStereoFormat(argv[1], (UINT)width, (UINT)height, sType, highConfidenceLevel, ssim);
    QueryPerformanceCounter(&measureEnd);
    ElapsedMicroseconds.QuadPart = measureEnd.QuadPart - measureStart.QuadPart;
    ElapsedMicroseconds.QuadPart = (LONGLONG)(ElapsedMicroseconds.QuadPart * qpfPeroid);
    printf("******************************************************\n");
    printf("Result: \n");
    printf("Selected stereo mode: %s\n", STEREO_TYPE_NAME[sType]);
    printf("SSIM: %f\n", ssim);
    printf("Time elapsed: %lluus\n", ElapsedMicroseconds.QuadPart);
    printf("%s\n", highConfidenceLevel ? VALIDATE_PASS_MSG : VALIDATE_FAIL_MSG);
    printf("******************************************************\n");

    return 0;
}

