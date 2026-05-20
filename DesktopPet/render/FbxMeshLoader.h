#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>
#include <DirectXMath.h>

struct LoadedMeshVertex
{
    DirectX::XMFLOAT3 position{};
    DirectX::XMFLOAT3 normal{};
    DirectX::XMFLOAT4 color{ 0.85f, 0.85f, 0.85f, 1.0f };
    DirectX::XMFLOAT2 texcoord{};
    DirectX::XMUINT4 boneIndices{};
    DirectX::XMFLOAT4 boneWeights{};
};

struct LoadedMeshAnimation
{
    std::vector<std::vector<LoadedMeshVertex>> frames;
    std::vector<std::vector<DirectX::XMFLOAT4X4>> boneFrames;
    float durationSeconds{};
    uint32_t boneCount{};
};

bool LoadFbxMesh(
    std::filesystem::path const& path,
    std::vector<LoadedMeshVertex>& vertices,
    std::wstring& error,
    std::filesystem::path* diffuseTexturePath = nullptr,
    LoadedMeshAnimation* animation = nullptr);
