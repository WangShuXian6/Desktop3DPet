#pragma once
#include <cstddef>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include "../anim/AnimationController.h"
#include "FbxMeshLoader.h"

class D3D11Renderer
{
public:
    bool Initialize(HWND hwnd, int width, int height);
    void Resize(int width, int height);
    void Tick(float dt);
    void Render(PoseParams const& pose);
    bool SetModelPath(std::filesystem::path const& path);
    void SetModelRotation(float pitchDegrees, float yawDegrees, float rollDegrees);
    void SetMaterialSettings(
        float brightness,
        float contrast,
        float saturation,
        float ambient,
        float directLight,
        float specular,
        float roughness);
    void Destroy();

private:
    static constexpr size_t MaxGpuSkinBones = 64;

    struct Vertex
    {
        DirectX::XMFLOAT3 position;
        DirectX::XMFLOAT3 normal;
        DirectX::XMFLOAT4 color;
        DirectX::XMFLOAT2 texcoord;
        DirectX::XMUINT4 boneIndices;
        DirectX::XMFLOAT4 boneWeights;
    };

    struct ConstantBuffer
    {
        DirectX::XMMATRIX worldViewProjection;
        DirectX::XMMATRIX world;
        DirectX::XMFLOAT4 params;
        DirectX::XMFLOAT4 materialAdjust;
        DirectX::XMFLOAT4 lightingAdjust;
    };

    struct BoneBuffer
    {
        DirectX::XMFLOAT4X4 bones[MaxGpuSkinBones];
    };

    bool CreateDeviceAndSwapChain(HWND hwnd, int width, int height);
    bool CreateRenderTarget();
    bool CreateDepthStencil();
    bool CreateRasterizerState();
    bool CreateShaders();
    bool CreateBoneBuffer();
    bool CreateGeometry();
    bool CreateModelGeometry(std::filesystem::path const& path);
    bool CreateFallbackGeometry();
    bool CreateVertexBuffer(std::vector<Vertex> const& vertices, bool dynamic);
    void UploadAnimationFrame(size_t frameIndex);
    void UploadBoneFrame(size_t frameIndex);
    void UploadIdentityBones();
    bool CreateTextureFromFile(std::filesystem::path const& path);
    bool CreateSamplerState();
    std::filesystem::path ResolveTexturePath(std::filesystem::path const& modelPath, std::filesystem::path const& texturePath) const;
    std::filesystem::path FindModelPath() const;

    int m_width{ 1 };
    int m_height{ 1 };
    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
    Microsoft::WRL::ComPtr<IDXGISwapChain> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_renderTarget;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_depthStencil;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_depthStencilView;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_rasterizerState;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_pixelShader;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_inputLayout;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_constantBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_boneBuffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_diffuseTexture;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_samplerState;
    std::filesystem::path m_modelPath{ L"1.fbx" };
    DirectX::XMFLOAT3 m_modelRotationDegrees{};
    DirectX::XMFLOAT4 m_materialAdjust{ 1.0f, 1.0f, 1.0f, 0.22f };
    DirectX::XMFLOAT4 m_lightingAdjust{ 0.78f, 0.0f, 0.45f, 0.0f };
    std::vector<std::vector<Vertex>> m_animationFrames;
    std::vector<std::vector<DirectX::XMFLOAT4X4>> m_gpuAnimationFrames;
    float m_animationDurationSeconds{};
    float m_animationTimeSeconds{};
    size_t m_currentAnimationFrame{};
    uint32_t m_gpuBoneCount{};
    UINT m_vertexCount{ 0 };
    bool m_hasDiffuseTexture{ false };
    bool m_vertexBufferDynamic{ false };
};
