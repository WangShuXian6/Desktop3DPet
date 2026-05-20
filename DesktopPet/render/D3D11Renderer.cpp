#include "pch.h"
#include "D3D11Renderer.h"
#include <array>
#include <cfloat>
#include <wincodec.h>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

static const char* kShaderSource = R"HLSL(
cbuffer Cb : register(b0)
{
    matrix worldViewProjection;
    matrix world;
    float4 params;
    float4 materialAdjust;
    float4 lightingAdjust;
};

cbuffer Bones : register(b1)
{
    matrix boneMatrices[64];
};

Texture2D diffuseTexture : register(t0);
SamplerState diffuseSampler : register(s0);

struct VSInput
{
    float3 pos : POSITION;
    float3 normal : NORMAL0;
    float4 color : COLOR0;
    float2 texcoord : TEXCOORD0;
    uint4 boneIndices : BLENDINDICES0;
    float4 boneWeights : BLENDWEIGHT0;
};

struct PSInput
{
    float4 pos : SV_POSITION;
    float3 normal : NORMAL0;
    float4 color : COLOR0;
    float2 texcoord : TEXCOORD0;
};

PSInput VSMain(VSInput input)
{
    PSInput output;
    float4 localPos = float4(input.pos, 1.0f);
    float3 localNormal = input.normal;
    float weightSum = input.boneWeights.x + input.boneWeights.y + input.boneWeights.z + input.boneWeights.w;
    if (weightSum > 0.0001f)
    {
        float4 skinnedPos =
            mul(localPos, boneMatrices[input.boneIndices.x]) * input.boneWeights.x +
            mul(localPos, boneMatrices[input.boneIndices.y]) * input.boneWeights.y +
            mul(localPos, boneMatrices[input.boneIndices.z]) * input.boneWeights.z +
            mul(localPos, boneMatrices[input.boneIndices.w]) * input.boneWeights.w;
        float3 skinnedNormal =
            mul(float4(input.normal, 0.0f), boneMatrices[input.boneIndices.x]).xyz * input.boneWeights.x +
            mul(float4(input.normal, 0.0f), boneMatrices[input.boneIndices.y]).xyz * input.boneWeights.y +
            mul(float4(input.normal, 0.0f), boneMatrices[input.boneIndices.z]).xyz * input.boneWeights.z +
            mul(float4(input.normal, 0.0f), boneMatrices[input.boneIndices.w]).xyz * input.boneWeights.w;

        localPos = lerp(localPos, skinnedPos, saturate(weightSum));
        localNormal = normalize(lerp(input.normal, skinnedNormal, saturate(weightSum)));
    }

    output.pos = mul(localPos, worldViewProjection);
    output.normal = normalize(mul(float4(localNormal, 0.0f), world).xyz);
    output.color = input.color;
    output.texcoord = input.texcoord;
    return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float3 lightDir = normalize(float3(-0.35f, 0.75f, -0.55f));
    float3 normal = normalize(input.normal);
    float lit = saturate(dot(normal, lightDir));
    float textureUse = input.color.a;
    float4 material = float4(input.color.rgb, 1.0f);
    if (params.z > 0.5f && textureUse > 0.5f)
    {
        material *= diffuseTexture.Sample(diffuseSampler, input.texcoord);
    }

    float3 base = material.rgb;
    float luminance = dot(base, float3(0.2126f, 0.7152f, 0.0722f));
    base = lerp(float3(luminance, luminance, luminance), base, materialAdjust.z);
    base = ((base - 0.5f) * materialAdjust.y) + 0.5f;
    base *= materialAdjust.x;
    base = saturate(base);

    float3 viewDir = normalize(float3(0.0f, 0.12f, -1.0f));
    float3 halfDir = normalize(lightDir + viewDir);
    float roughness = saturate(lightingAdjust.z);
    float specularPower = lerp(128.0f, 8.0f, roughness);
    float specular = pow(saturate(dot(normal, halfDir)), specularPower) * lightingAdjust.y;
    float3 color = base * (materialAdjust.w + lit * lightingAdjust.x) + float3(specular, specular, specular);
    return float4(color, 1.0f);
}
)HLSL";

bool D3D11Renderer::Initialize(HWND hwnd, int width, int height)
{
    m_width = std::max(1, width);
    m_height = std::max(1, height);
    if (!CreateDeviceAndSwapChain(hwnd, m_width, m_height)) return false;
    if (!CreateRenderTarget()) return false;
    if (!CreateDepthStencil()) return false;
    if (!CreateRasterizerState()) return false;
    if (!CreateShaders()) return false;
    if (!CreateSamplerState()) return false;
    if (!CreateGeometry()) return false;
    return true;
}

bool D3D11Renderer::CreateDeviceAndSwapChain(HWND hwnd, int width, int height)
{
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 2;
    desc.BufferDesc.Width = width;
    desc.BufferDesc.Height = height;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferDesc.RefreshRate.Numerator = 60;
    desc.BufferDesc.RefreshRate.Denominator = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = hwnd;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT flags = 0;
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevels[] =
    {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    D3D_FEATURE_LEVEL actual{};

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        flags,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &desc,
        &m_swapChain,
        &m_device,
        &actual,
        &m_context);

#if defined(_DEBUG)
    if (FAILED(hr))
    {
        flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            flags,
            featureLevels,
            ARRAYSIZE(featureLevels),
            D3D11_SDK_VERSION,
            &desc,
            &m_swapChain,
            &m_device,
            &actual,
            &m_context);
    }
#endif

    return SUCCEEDED(hr);
}

bool D3D11Renderer::CreateRenderTarget()
{
    ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr)) return false;
    hr = m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, &m_renderTarget);
    return SUCCEEDED(hr);
}

bool D3D11Renderer::CreateDepthStencil()
{
    if (!m_device) return false;

    D3D11_TEXTURE2D_DESC textureDesc{};
    textureDesc.Width = static_cast<UINT>(m_width);
    textureDesc.Height = static_cast<UINT>(m_height);
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    HRESULT hr = m_device->CreateTexture2D(&textureDesc, nullptr, &m_depthStencil);
    if (FAILED(hr)) return false;

    hr = m_device->CreateDepthStencilView(m_depthStencil.Get(), nullptr, &m_depthStencilView);
    return SUCCEEDED(hr);
}

bool D3D11Renderer::CreateRasterizerState()
{
    D3D11_RASTERIZER_DESC desc{};
    desc.FillMode = D3D11_FILL_SOLID;
    desc.CullMode = D3D11_CULL_NONE;
    desc.DepthClipEnable = TRUE;

    HRESULT hr = m_device->CreateRasterizerState(&desc, &m_rasterizerState);
    return SUCCEEDED(hr);
}

bool D3D11Renderer::CreateShaders()
{
    ComPtr<ID3DBlob> vsBlob;
    ComPtr<ID3DBlob> psBlob;
    ComPtr<ID3DBlob> errorBlob;

    HRESULT hr = D3DCompile(
        kShaderSource,
        std::strlen(kShaderSource),
        nullptr,
        nullptr,
        nullptr,
        "VSMain",
        "vs_4_0",
        0,
        0,
        &vsBlob,
        &errorBlob);
    if (FAILED(hr)) return false;

    hr = D3DCompile(
        kShaderSource,
        std::strlen(kShaderSource),
        nullptr,
        nullptr,
        nullptr,
        "PSMain",
        "ps_4_0",
        0,
        0,
        &psBlob,
        &errorBlob);
    if (FAILED(hr)) return false;

    hr = m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_vertexShader);
    if (FAILED(hr)) return false;
    hr = m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_pixelShader);
    if (FAILED(hr)) return false;

    D3D11_INPUT_ELEMENT_DESC layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    hr = m_device->CreateInputLayout(layout, ARRAYSIZE(layout), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &m_inputLayout);
    if (FAILED(hr)) return false;

    D3D11_BUFFER_DESC cbDesc{};
    cbDesc.ByteWidth = sizeof(ConstantBuffer);
    cbDesc.Usage = D3D11_USAGE_DEFAULT;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    hr = m_device->CreateBuffer(&cbDesc, nullptr, &m_constantBuffer);
    if (FAILED(hr)) return false;

    return CreateBoneBuffer();
}

bool D3D11Renderer::CreateBoneBuffer()
{
    D3D11_BUFFER_DESC boneDesc{};
    boneDesc.ByteWidth = sizeof(BoneBuffer);
    boneDesc.Usage = D3D11_USAGE_DEFAULT;
    boneDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    HRESULT hr = m_device->CreateBuffer(&boneDesc, nullptr, &m_boneBuffer);
    if (FAILED(hr)) return false;

    UploadIdentityBones();
    return true;
}

void D3D11Renderer::UploadIdentityBones()
{
    if (!m_context || !m_boneBuffer)
    {
        return;
    }

    BoneBuffer buffer{};
    for (size_t i = 0; i < MaxGpuSkinBones; ++i)
    {
        XMStoreFloat4x4(&buffer.bones[i], XMMatrixTranspose(XMMatrixIdentity()));
    }
    m_context->UpdateSubresource(m_boneBuffer.Get(), 0, nullptr, &buffer, 0, 0);
}

bool D3D11Renderer::CreateSamplerState()
{
    D3D11_SAMPLER_DESC desc{};
    desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    desc.MinLOD = 0;
    desc.MaxLOD = D3D11_FLOAT32_MAX;

    HRESULT hr = m_device->CreateSamplerState(&desc, &m_samplerState);
    return SUCCEEDED(hr);
}

bool D3D11Renderer::CreateTextureFromFile(std::filesystem::path const& path)
{
    if (!m_device || path.empty()) return false;

    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return false;

    ComPtr<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromFilename(
        path.c_str(),
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnLoad,
        &decoder);
    if (FAILED(hr)) return false;

    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) return false;

    UINT width{};
    UINT height{};
    hr = frame->GetSize(&width, &height);
    if (FAILED(hr) || width == 0 || height == 0) return false;

    ComPtr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr)) return false;

    hr = converter->Initialize(
        frame.Get(),
        GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0,
        WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) return false;

    const UINT stride = width * 4;
    std::vector<uint8_t> pixels(static_cast<size_t>(stride) * height);
    hr = converter->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size()), pixels.data());
    if (FAILED(hr)) return false;

    D3D11_TEXTURE2D_DESC textureDesc{};
    textureDesc.Width = width;
    textureDesc.Height = height;
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Usage = D3D11_USAGE_DEFAULT;
    textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = pixels.data();
    init.SysMemPitch = stride;

    ComPtr<ID3D11Texture2D> texture;
    hr = m_device->CreateTexture2D(&textureDesc, &init, &texture);
    if (FAILED(hr)) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = textureDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    hr = m_device->CreateShaderResourceView(texture.Get(), &srvDesc, &m_diffuseTexture);
    return SUCCEEDED(hr);
}

std::filesystem::path D3D11Renderer::ResolveTexturePath(
    std::filesystem::path const& modelPath,
    std::filesystem::path const& texturePath) const
{
    if (texturePath.empty()) return {};

    std::array<std::filesystem::path, 8> candidates{
        texturePath,
        modelPath.parent_path() / texturePath,
        std::filesystem::current_path() / texturePath,
        std::filesystem::current_path() / L"assets" / L"pets" / texturePath,
        modelPath.parent_path() / texturePath.filename(),
        modelPath.parent_path() / L"Texture" / texturePath.filename(),
        std::filesystem::current_path() / texturePath.filename(),
        std::filesystem::current_path() / L"assets" / L"pets" / texturePath.filename(),
    };

    for (auto const& candidate : candidates)
    {
        std::error_code ignored;
        if (!candidate.empty() && std::filesystem::exists(candidate, ignored))
        {
            return candidate;
        }
    }

    return {};
}

bool D3D11Renderer::CreateGeometry()
{
    if (CreateModelGeometry(FindModelPath()))
    {
        return true;
    }

    return CreateFallbackGeometry();
}

bool D3D11Renderer::CreateModelGeometry(std::filesystem::path const& path)
{
    std::vector<LoadedMeshVertex> mesh;
    std::wstring error;
    std::filesystem::path texturePath;
    LoadedMeshAnimation animation;
    if (!LoadFbxMesh(path, mesh, error, &texturePath, &animation))
    {
        return false;
    }

    std::vector<Vertex> vertices;
    vertices.reserve(mesh.size());
    for (auto const& item : mesh)
    {
        vertices.push_back({ item.position, item.normal, item.color, item.texcoord, item.boneIndices, item.boneWeights });
    }

    m_animationFrames.clear();
    m_gpuAnimationFrames.clear();
    m_gpuBoneCount = 0;
    m_animationDurationSeconds = animation.durationSeconds;
    m_animationTimeSeconds = 0.0f;
    m_currentAnimationFrame = 0;
    const bool useGpuSkinning =
        animation.durationSeconds > 0.0f &&
        animation.boneCount > 0 &&
        animation.boneCount <= MaxGpuSkinBones &&
        animation.boneFrames.size() > 1;

    if (useGpuSkinning)
    {
        m_gpuAnimationFrames = animation.boneFrames;
        m_gpuBoneCount = animation.boneCount;
        UploadBoneFrame(0);
    }
    else if (!animation.frames.empty())
    {
        m_animationFrames.reserve(animation.frames.size());
        for (auto const& frame : animation.frames)
        {
            std::vector<Vertex> converted;
            converted.reserve(frame.size());
            for (auto const& item : frame)
            {
                converted.push_back({ item.position, item.normal, item.color, item.texcoord, item.boneIndices, item.boneWeights });
            }
            if (converted.size() == vertices.size())
            {
                m_animationFrames.push_back(std::move(converted));
            }
        }
    }

    if (!m_animationFrames.empty())
    {
        vertices = m_animationFrames.front();
    }

    if (!CreateVertexBuffer(vertices, !useGpuSkinning && m_animationFrames.size() > 1))
    {
        return false;
    }

    m_diffuseTexture.Reset();
    m_hasDiffuseTexture = false;
    const auto resolvedTexture = ResolveTexturePath(path, texturePath);
    if (!resolvedTexture.empty())
    {
        m_hasDiffuseTexture = CreateTextureFromFile(resolvedTexture);
    }

    return true;
}

bool D3D11Renderer::CreateVertexBuffer(std::vector<Vertex> const& vertices, bool dynamic)
{
    if (vertices.empty()) return false;

    m_vertexCount = static_cast<UINT>(vertices.size());
    m_vertexBufferDynamic = dynamic;

    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = static_cast<UINT>(sizeof(Vertex) * vertices.size());
    desc.Usage = dynamic ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    desc.CPUAccessFlags = dynamic ? D3D11_CPU_ACCESS_WRITE : 0;

    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = vertices.data();

    HRESULT hr = m_device->CreateBuffer(&desc, &init, &m_vertexBuffer);
    return SUCCEEDED(hr);
}

bool D3D11Renderer::CreateFallbackGeometry()
{
    const XMFLOAT4 body{ 0.35f, 0.85f, 1.00f, 1.0f };
    const XMFLOAT4 face{ 1.00f, 0.90f, 0.50f, 1.0f };
    const XMFLOAT4 ear{ 0.25f, 0.65f, 0.95f, 1.0f };
    const XMFLOAT4 eye{ 0.05f, 0.05f, 0.10f, 1.0f };
    const XMFLOAT4 blush{ 1.00f, 0.45f, 0.60f, 1.0f };
    const XMFLOAT4 tail{ 0.70f, 0.95f, 1.00f, 1.0f };

    std::vector<Vertex> v;
    auto tri = [&](float ax, float ay, float bx, float by, float cx, float cy, XMFLOAT4 color)
    {
        const XMFLOAT3 normal{ 0.0f, 0.0f, -1.0f };
        v.push_back({ XMFLOAT3(ax, ay, 0.0f), normal, color, XMFLOAT2(0.0f, 0.0f) });
        v.push_back({ XMFLOAT3(bx, by, 0.0f), normal, color, XMFLOAT2(1.0f, 0.0f) });
        v.push_back({ XMFLOAT3(cx, cy, 0.0f), normal, color, XMFLOAT2(1.0f, 1.0f) });
    };
    auto rect = [&](float x0, float y0, float x1, float y1, XMFLOAT4 c)
    {
        tri(x0, y0, x1, y0, x1, y1, c);
        tri(x0, y0, x1, y1, x0, y1, c);
    };

    // body and face
    rect(-0.38f, -0.42f, 0.38f, 0.18f, body);
    rect(-0.34f, -0.05f, 0.34f, 0.48f, face);

    // ears
    tri(-0.34f, 0.36f, -0.20f, 0.74f, -0.05f, 0.36f, ear);
    tri(0.05f, 0.36f, 0.20f, 0.74f, 0.34f, 0.36f, ear);

    // eyes
    rect(-0.22f, 0.12f, -0.14f, 0.22f, eye);
    rect(0.14f, 0.12f, 0.22f, 0.22f, eye);

    // mouth
    tri(-0.04f, 0.02f, 0.04f, 0.02f, 0.0f, -0.07f, eye);

    // blush
    rect(-0.32f, -0.02f, -0.22f, 0.06f, blush);
    rect(0.22f, -0.02f, 0.32f, 0.06f, blush);

    // paws
    rect(-0.30f, -0.62f, -0.12f, -0.42f, face);
    rect(0.12f, -0.62f, 0.30f, -0.42f, face);

    // tail
    tri(0.34f, -0.16f, 0.70f, 0.00f, 0.38f, 0.10f, tail);
    tri(0.44f, 0.08f, 0.76f, 0.26f, 0.48f, 0.30f, tail);

    const bool created = CreateVertexBuffer(v, false);
    m_animationFrames.clear();
    m_gpuAnimationFrames.clear();
    m_animationDurationSeconds = 0.0f;
    m_animationTimeSeconds = 0.0f;
    m_currentAnimationFrame = 0;
    m_gpuBoneCount = 0;
    UploadIdentityBones();
    m_diffuseTexture.Reset();
    m_hasDiffuseTexture = false;
    return created;
}

std::filesystem::path D3D11Renderer::FindModelPath() const
{
    std::filesystem::path requested = m_modelPath.empty() ? std::filesystem::path(L"1.fbx") : m_modelPath;
    if (requested.is_absolute())
    {
        return requested;
    }

    const auto cwd = std::filesystem::current_path();
    const auto fileName = requested.filename();
    std::array<std::filesystem::path, 10> candidates{
        cwd / requested,
        cwd / L"assets" / L"pets" / requested,
        cwd / fileName,
        cwd / L"assets" / L"pets" / fileName,
        cwd / L"..\\..\\" / requested,
        cwd / L"..\\..\\..\\" / requested,
        cwd / L"..\\..\\..\\..\\" / requested,
        cwd / L"..\\..\\" / fileName,
        cwd / L"..\\..\\..\\" / fileName,
        cwd / L"..\\..\\..\\..\\" / fileName,
    };

    for (auto const& path : candidates)
    {
        std::error_code ignored;
        if (std::filesystem::exists(path, ignored))
        {
            return path;
        }
    }

    return candidates.front();
}

bool D3D11Renderer::SetModelPath(std::filesystem::path const& path)
{
    m_modelPath = path.empty() ? std::filesystem::path(L"1.fbx") : path;
    if (!m_device)
    {
        return true;
    }

    m_vertexBuffer.Reset();
    m_vertexCount = 0;

    if (CreateModelGeometry(FindModelPath()))
    {
        return true;
    }

    CreateFallbackGeometry();
    return false;
}

void D3D11Renderer::SetModelRotation(float pitchDegrees, float yawDegrees, float rollDegrees)
{
    m_modelRotationDegrees = XMFLOAT3(pitchDegrees, yawDegrees, rollDegrees);
}

void D3D11Renderer::SetMaterialSettings(
    float brightness,
    float contrast,
    float saturation,
    float ambient,
    float directLight,
    float specular,
    float roughness)
{
    m_materialAdjust = XMFLOAT4(
        std::clamp(brightness, 0.0f, 4.0f),
        std::clamp(contrast, 0.0f, 3.0f),
        std::clamp(saturation, 0.0f, 3.0f),
        std::clamp(ambient, 0.0f, 2.0f));
    m_lightingAdjust = XMFLOAT4(
        std::clamp(directLight, 0.0f, 3.0f),
        std::clamp(specular, 0.0f, 3.0f),
        std::clamp(roughness, 0.0f, 1.0f),
        0.0f);
}

void D3D11Renderer::Tick(float dt)
{
    const bool useGpuSkinning = m_gpuAnimationFrames.size() > 1;
    const size_t frameCount = useGpuSkinning ? m_gpuAnimationFrames.size() : m_animationFrames.size();
    if (frameCount <= 1 || m_animationDurationSeconds <= 0.0f)
    {
        return;
    }

    m_animationTimeSeconds += std::max(0.0f, dt);
    while (m_animationTimeSeconds >= m_animationDurationSeconds)
    {
        m_animationTimeSeconds -= m_animationDurationSeconds;
    }

    const float normalized = m_animationTimeSeconds / m_animationDurationSeconds;
    const size_t frameIndex = std::min(
        frameCount - 1,
        static_cast<size_t>(normalized * static_cast<float>(frameCount)));
    if (frameIndex != m_currentAnimationFrame)
    {
        if (useGpuSkinning)
        {
            UploadBoneFrame(frameIndex);
        }
        else
        {
            UploadAnimationFrame(frameIndex);
        }
    }
}

void D3D11Renderer::UploadAnimationFrame(size_t frameIndex)
{
    if (!m_context || !m_vertexBuffer || !m_vertexBufferDynamic || frameIndex >= m_animationFrames.size())
    {
        return;
    }

    auto const& frame = m_animationFrames[frameIndex];
    if (frame.empty() || frame.size() != m_vertexCount)
    {
        return;
    }

    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT hr = m_context->Map(m_vertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr))
    {
        return;
    }

    std::memcpy(mapped.pData, frame.data(), frame.size() * sizeof(Vertex));
    m_context->Unmap(m_vertexBuffer.Get(), 0);
    m_currentAnimationFrame = frameIndex;
}

void D3D11Renderer::UploadBoneFrame(size_t frameIndex)
{
    if (!m_context || !m_boneBuffer || frameIndex >= m_gpuAnimationFrames.size())
    {
        return;
    }

    BoneBuffer buffer{};
    for (size_t i = 0; i < MaxGpuSkinBones; ++i)
    {
        XMStoreFloat4x4(&buffer.bones[i], XMMatrixTranspose(XMMatrixIdentity()));
    }

    auto const& frame = m_gpuAnimationFrames[frameIndex];
    const size_t count = std::min({ frame.size(), MaxGpuSkinBones, static_cast<size_t>(m_gpuBoneCount) });
    for (size_t i = 0; i < count; ++i)
    {
        XMStoreFloat4x4(&buffer.bones[i], XMMatrixTranspose(XMLoadFloat4x4(&frame[i])));
    }

    m_context->UpdateSubresource(m_boneBuffer.Get(), 0, nullptr, &buffer, 0, 0);
    m_currentAnimationFrame = frameIndex;
}

void D3D11Renderer::Resize(int width, int height)
{
    if (!m_swapChain || width <= 0 || height <= 0) return;
    m_width = width;
    m_height = height;
    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    m_renderTarget.Reset();
    m_depthStencilView.Reset();
    m_depthStencil.Reset();
    m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    CreateRenderTarget();
    CreateDepthStencil();
}

void D3D11Renderer::Render(PoseParams const& pose)
{
    if (!m_context || !m_renderTarget) return;

    const float clear[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    m_context->ClearRenderTargetView(m_renderTarget.Get(), clear);

    D3D11_VIEWPORT vp{};
    vp.Width = static_cast<float>(m_width);
    vp.Height = static_cast<float>(m_height);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &vp);

    m_context->ClearDepthStencilView(m_depthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    m_context->OMSetRenderTargets(1, m_renderTarget.GetAddressOf(), m_depthStencilView.Get());
    m_context->RSSetState(m_rasterizerState.Get());

    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    m_context->IASetInputLayout(m_inputLayout.Get());
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
    m_context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    m_context->PSSetShader(m_pixelShader.Get(), nullptr, 0);
    ID3D11ShaderResourceView* diffuseSrv = m_hasDiffuseTexture ? m_diffuseTexture.Get() : nullptr;
    m_context->PSSetShaderResources(0, 1, &diffuseSrv);
    m_context->PSSetSamplers(0, 1, m_samplerState.GetAddressOf());

    const float aspect = m_height > 0 ? static_cast<float>(m_width) / static_cast<float>(m_height) : 1.0f;
    const float s = pose.scale * (1.0f + pose.bounce * 0.025f);
    XMMATRIX modelRotation =
        XMMatrixRotationX(XMConvertToRadians(m_modelRotationDegrees.x)) *
        XMMatrixRotationY(XMConvertToRadians(m_modelRotationDegrees.y)) *
        XMMatrixRotationZ(XMConvertToRadians(m_modelRotationDegrees.z));
    XMMATRIX world =
        XMMatrixScaling(s, s, s) *
        modelRotation *
        XMMatrixRotationY(0.32f + pose.wave * 0.12f) *
        XMMatrixRotationZ(pose.rotationRad) *
        XMMatrixTranslation(0.0f, pose.bounce * 0.035f - 0.02f, 0.0f);
    XMMATRIX view = XMMatrixLookAtLH(
        XMVectorSet(0.0f, 0.04f, -2.75f, 0.0f),
        XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f),
        XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
    XMMATRIX projection = XMMatrixPerspectiveFovLH(XMConvertToRadians(38.0f), aspect, 0.05f, 10.0f);

    ConstantBuffer cb{};
    cb.worldViewProjection = XMMatrixTranspose(world * view * projection);
    cb.world = XMMatrixTranspose(world);
    cb.params = XMFLOAT4(pose.wave, pose.mood, m_hasDiffuseTexture ? 1.0f : 0.0f, 0.0f);
    cb.materialAdjust = m_materialAdjust;
    cb.lightingAdjust = m_lightingAdjust;
    m_context->UpdateSubresource(m_constantBuffer.Get(), 0, nullptr, &cb, 0, 0);
    m_context->VSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
    m_context->VSSetConstantBuffers(1, 1, m_boneBuffer.GetAddressOf());
    m_context->PSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());

    m_context->Draw(m_vertexCount, 0);
    m_swapChain->Present(1, 0);
}

void D3D11Renderer::Destroy()
{
    m_diffuseTexture.Reset();
    m_samplerState.Reset();
    m_boneBuffer.Reset();
    m_constantBuffer.Reset();
    m_vertexBuffer.Reset();
    m_inputLayout.Reset();
    m_pixelShader.Reset();
    m_vertexShader.Reset();
    m_rasterizerState.Reset();
    m_depthStencilView.Reset();
    m_depthStencil.Reset();
    m_renderTarget.Reset();
    m_swapChain.Reset();
    m_context.Reset();
    m_device.Reset();
}
