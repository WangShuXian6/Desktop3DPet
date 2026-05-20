#include "pch.h"
#include "FbxMeshLoader.h"
#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <zlib.h>
#include <array>
#include <unordered_map>
#include <unordered_set>
#include <limits>

using namespace DirectX;

namespace
{
    constexpr size_t FbxHeaderSize = 27;
    constexpr size_t FbxVersionOffset = 23;
    constexpr uint32_t FbxLargeNodeHeaderVersion = 7500;
    constexpr double FbxTimeUnitsPerSecond = 46186158000.0;

    struct Node
    {
        size_t endOffset{};
        uint64_t propertyCount{};
        size_t propertyBytes{};
        std::string name;
        size_t propertyOffset{};
        size_t childOffset{};
    };

    struct FbxReader
    {
        explicit FbxReader(std::vector<uint8_t> bytes, uint32_t version) :
            data(std::move(bytes)),
            fbxVersion(version),
            nodeHeaderSize(version >= FbxLargeNodeHeaderVersion ? 25 : 13)
        {
        }

        std::vector<uint8_t> data;
        uint32_t fbxVersion{};
        size_t nodeHeaderSize{};

        uint32_t ReadU32(size_t offset) const
        {
            if (offset + sizeof(uint32_t) > data.size()) return 0;
            uint32_t value{};
            std::memcpy(&value, data.data() + offset, sizeof(value));
            return value;
        }

        uint64_t ReadU64(size_t offset) const
        {
            if (offset + sizeof(uint64_t) > data.size()) return 0;
            uint64_t value{};
            std::memcpy(&value, data.data() + offset, sizeof(value));
            return value;
        }

        int32_t ReadI32(size_t offset) const
        {
            int32_t value{};
            if (offset + sizeof(value) <= data.size())
            {
                std::memcpy(&value, data.data() + offset, sizeof(value));
            }
            return value;
        }

        int64_t ReadI64(size_t offset) const
        {
            int64_t value{};
            if (offset + sizeof(value) <= data.size())
            {
                std::memcpy(&value, data.data() + offset, sizeof(value));
            }
            return value;
        }

        float ReadF32(size_t offset) const
        {
            float value{};
            if (offset + sizeof(value) <= data.size())
            {
                std::memcpy(&value, data.data() + offset, sizeof(value));
            }
            return value;
        }

        double ReadF64(size_t offset) const
        {
            double value{};
            if (offset + sizeof(value) <= data.size())
            {
                std::memcpy(&value, data.data() + offset, sizeof(value));
            }
            return value;
        }

        std::string ReadString(size_t offset, size_t length) const
        {
            if (offset + length > data.size()) return {};
            return std::string(reinterpret_cast<char const*>(data.data() + offset), length);
        }

        bool HasNodeHeader(size_t offset, size_t limit) const
        {
            return offset + nodeHeaderSize <= limit;
        }

        bool IsNullRecord(size_t offset, size_t limit) const
        {
            if (!HasNodeHeader(offset, limit)) return true;
            for (size_t i = 0; i < nodeHeaderSize; ++i)
            {
                if (data[offset + i] != 0) return false;
            }
            return true;
        }

        bool ReadNode(size_t offset, size_t limit, Node& node) const
        {
            if (!HasNodeHeader(offset, limit) || IsNullRecord(offset, limit)) return false;

            uint64_t endOffset{};
            uint64_t propertyBytes{};
            if (fbxVersion >= FbxLargeNodeHeaderVersion)
            {
                endOffset = ReadU64(offset);
                node.propertyCount = ReadU64(offset + 8);
                propertyBytes = ReadU64(offset + 16);
            }
            else
            {
                endOffset = ReadU32(offset);
                node.propertyCount = ReadU32(offset + 4);
                propertyBytes = ReadU32(offset + 8);
            }

            if (endOffset > static_cast<uint64_t>(limit) ||
                propertyBytes > static_cast<uint64_t>(limit))
            {
                return false;
            }

            node.endOffset = static_cast<size_t>(endOffset);
            node.propertyBytes = static_cast<size_t>(propertyBytes);
            const uint8_t nameLength = data[offset + nodeHeaderSize - 1];

            if (node.endOffset <= offset || offset + nodeHeaderSize + nameLength > node.endOffset)
            {
                return false;
            }

            node.name = ReadString(offset + nodeHeaderSize, nameLength);
            node.propertyOffset = offset + nodeHeaderSize + nameLength;
            node.childOffset = node.propertyOffset + node.propertyBytes;
            return node.childOffset <= node.endOffset;
        }

        size_t SkipProperty(size_t offset) const
        {
            if (offset >= data.size()) return data.size();

            const char type = static_cast<char>(data[offset++]);
            switch (type)
            {
            case 'Y': return offset + 2;
            case 'C': return offset + 1;
            case 'I': return offset + 4;
            case 'F': return offset + 4;
            case 'D': return offset + 8;
            case 'L': return offset + 8;
            case 'R':
            case 'S':
            {
                const uint32_t length = ReadU32(offset);
                return offset + 4 + length;
            }
            case 'f':
            case 'd':
            case 'i':
            case 'l':
            case 'b':
            case 'c':
            {
                const uint32_t compressedLength = ReadU32(offset + 8);
                return offset + 12 + compressedLength;
            }
            default:
                return data.size();
            }
        }

        size_t PropertyOffset(Node const& node, uint32_t propertyIndex) const
        {
            size_t offset = node.propertyOffset;
            for (uint32_t i = 0; i < propertyIndex && offset < node.childOffset; ++i)
            {
                offset = SkipProperty(offset);
            }
            return offset;
        }

        std::string ReadStringProperty(size_t offset) const
        {
            if (offset + 5 > data.size() || static_cast<char>(data[offset]) != 'S') return {};
            const uint32_t length = ReadU32(offset + 1);
            return ReadString(offset + 5, length);
        }

        int64_t ReadIntegerProperty(size_t offset) const
        {
            if (offset >= data.size()) return 0;
            switch (static_cast<char>(data[offset]))
            {
            case 'Y': return static_cast<int16_t>(ReadU32(offset + 1) & 0xffff);
            case 'I': return ReadI32(offset + 1);
            case 'L': return ReadI64(offset + 1);
            default: return 0;
            }
        }

        double ReadNumericProperty(size_t offset) const
        {
            if (offset >= data.size()) return 0.0;
            switch (static_cast<char>(data[offset]))
            {
            case 'Y': return static_cast<int16_t>(ReadU32(offset + 1) & 0xffff);
            case 'I': return ReadI32(offset + 1);
            case 'L': return static_cast<double>(ReadI64(offset + 1));
            case 'F': return ReadF32(offset + 1);
            case 'D': return ReadF64(offset + 1);
            default: return 0.0;
            }
        }

        template <typename T>
        bool ReadArrayProperty(size_t offset, char expectedType, std::vector<T>& values, std::wstring& error) const
        {
            if (offset + 13 > data.size() || static_cast<char>(data[offset]) != expectedType)
            {
                error = L"FBX array property type mismatch.";
                return false;
            }

            const uint32_t count = ReadU32(offset + 1);
            const uint32_t encoding = ReadU32(offset + 5);
            const uint32_t sourceLength = ReadU32(offset + 9);
            const size_t sourceOffset = offset + 13;
            const size_t targetBytes = static_cast<size_t>(count) * sizeof(T);
            if (sourceOffset + sourceLength > data.size())
            {
                error = L"FBX compressed array is outside the file bounds.";
                return false;
            }

            values.resize(count);
            if (targetBytes == 0) return true;

            if (encoding == 0)
            {
                if (sourceLength < targetBytes)
                {
                    error = L"FBX raw array is shorter than expected.";
                    return false;
                }

                std::memcpy(values.data(), data.data() + sourceOffset, targetBytes);
                return true;
            }

            if (encoding != 1)
            {
                error = L"Unsupported FBX array compression encoding.";
                return false;
            }

            uLongf decompressedBytes = static_cast<uLongf>(targetBytes);
            const int result = uncompress(
                reinterpret_cast<Bytef*>(values.data()),
                &decompressedBytes,
                reinterpret_cast<Bytef const*>(data.data() + sourceOffset),
                static_cast<uLong>(sourceLength));

            if (result != Z_OK || decompressedBytes != targetBytes)
            {
                error = L"Could not decompress FBX array data.";
                return false;
            }

            return true;
        }
    };

    struct RawMesh
    {
        int64_t geometryId{};
        std::vector<double> vertices;
        std::vector<int32_t> polygonIndices;
        std::vector<double> normals;
        std::vector<double> uvs;
        std::vector<int32_t> uvIndices;
        std::vector<int32_t> materialIndices;
    };

    struct ModelInfo
    {
        int64_t id{};
        int64_t parentId{};
        std::string name;
        std::string type;
        XMFLOAT3 translation{};
        XMFLOAT3 rotation{};
        XMFLOAT3 scaling{ 1.0f, 1.0f, 1.0f };
    };

    struct ClusterInfo
    {
        int64_t id{};
        int64_t boneModelId{};
        std::vector<int32_t> indices;
        std::vector<double> weights;
        std::array<double, 16> transform{};
        std::array<double, 16> transformLink{};
        bool hasTransform{};
        bool hasTransformLink{};
    };

    struct SkinInfo
    {
        int64_t id{};
    };

    struct AnimationCurveInfo
    {
        int64_t id{};
        std::vector<int64_t> times;
        std::vector<float> values;
    };

    struct AnimationCurveNodeInfo
    {
        int64_t id{};
    };

    struct AnimationChannel
    {
        AnimationCurveInfo const* curve{};
        char axis{};
    };

    struct ModelAnimation
    {
        std::vector<AnimationChannel> translation;
        std::vector<AnimationChannel> rotation;
        std::vector<AnimationChannel> scaling;
    };

    struct BoundCluster
    {
        ClusterInfo const* cluster{};
        int64_t boneModelId{};
    };

    using VertexInfluences = std::vector<std::vector<std::pair<size_t, float>>>;

    struct MaterialInfo
    {
        int64_t id{};
        XMFLOAT4 diffuse{ 0.85f, 0.85f, 0.85f, 1.0f };
    };

    struct TextureInfo
    {
        int64_t id{};
        std::string fileName;
        std::string relativeFileName;
    };

    struct ConnectionInfo
    {
        std::string type;
        int64_t child{};
        int64_t parent{};
        std::string property;
    };

    struct SceneData
    {
        RawMesh mesh;
        bool foundMesh{};
        std::vector<ModelInfo> models;
        std::vector<SkinInfo> skins;
        std::vector<ClusterInfo> clusters;
        std::vector<MaterialInfo> materials;
        std::vector<TextureInfo> textures;
        std::vector<AnimationCurveInfo> curves;
        std::vector<AnimationCurveNodeInfo> curveNodes;
        std::vector<ConnectionInfo> connections;
    };

    bool ParseGeometryChildren(FbxReader const& reader, size_t offset, size_t limit, RawMesh& mesh, std::wstring& error)
    {
        size_t cursor = offset;
        while (reader.HasNodeHeader(cursor, limit) && !reader.IsNullRecord(cursor, limit))
        {
            Node child{};
            if (!reader.ReadNode(cursor, limit, child)) return false;

            if (child.name == "Vertices" && child.propertyCount > 0)
            {
                if (!reader.ReadArrayProperty<double>(reader.PropertyOffset(child, 0), 'd', mesh.vertices, error)) return false;
            }
            else if (child.name == "PolygonVertexIndex" && child.propertyCount > 0)
            {
                if (!reader.ReadArrayProperty<int32_t>(reader.PropertyOffset(child, 0), 'i', mesh.polygonIndices, error)) return false;
            }
            else if (child.name == "Normals" && child.propertyCount > 0)
            {
                if (!reader.ReadArrayProperty<double>(reader.PropertyOffset(child, 0), 'd', mesh.normals, error)) return false;
            }
            else if (child.name == "UV" && child.propertyCount > 0)
            {
                if (!reader.ReadArrayProperty<double>(reader.PropertyOffset(child, 0), 'd', mesh.uvs, error)) return false;
            }
            else if (child.name == "UVIndex" && child.propertyCount > 0)
            {
                if (!reader.ReadArrayProperty<int32_t>(reader.PropertyOffset(child, 0), 'i', mesh.uvIndices, error)) return false;
            }
            else if (child.name == "Materials" && child.propertyCount > 0)
            {
                if (!reader.ReadArrayProperty<int32_t>(reader.PropertyOffset(child, 0), 'i', mesh.materialIndices, error)) return false;
            }
            else if (child.childOffset < child.endOffset)
            {
                if (!ParseGeometryChildren(reader, child.childOffset, child.endOffset, mesh, error)) return false;
            }

            cursor = child.endOffset;
        }

        return true;
    }

    void ParseProperties70(FbxReader const& reader, size_t offset, size_t limit, std::function<void(Node const&)> const& handleProperty)
    {
        size_t cursor = offset;
        while (reader.HasNodeHeader(cursor, limit) && !reader.IsNullRecord(cursor, limit))
        {
            Node child{};
            if (!reader.ReadNode(cursor, limit, child)) return;
            if (child.name == "P")
            {
                handleProperty(child);
            }
            cursor = child.endOffset;
        }
    }

    XMFLOAT3 ReadVectorProperty(FbxReader const& reader, Node const& property, XMFLOAT3 fallback)
    {
        if (property.propertyCount < 7)
        {
            return fallback;
        }

        return XMFLOAT3(
            static_cast<float>(reader.ReadNumericProperty(reader.PropertyOffset(property, 4))),
            static_cast<float>(reader.ReadNumericProperty(reader.PropertyOffset(property, 5))),
            static_cast<float>(reader.ReadNumericProperty(reader.PropertyOffset(property, 6))));
    }

    void ParseModelChildren(FbxReader const& reader, size_t offset, size_t limit, ModelInfo& model)
    {
        size_t cursor = offset;
        while (reader.HasNodeHeader(cursor, limit) && !reader.IsNullRecord(cursor, limit))
        {
            Node child{};
            if (!reader.ReadNode(cursor, limit, child)) return;

            if (child.name == "Properties70")
            {
                ParseProperties70(reader, child.childOffset, child.endOffset, [&](Node const& property)
                {
                    const std::string name = reader.ReadStringProperty(reader.PropertyOffset(property, 0));
                    if (name == "Lcl Translation")
                    {
                        model.translation = ReadVectorProperty(reader, property, model.translation);
                    }
                    else if (name == "Lcl Rotation")
                    {
                        model.rotation = ReadVectorProperty(reader, property, model.rotation);
                    }
                    else if (name == "Lcl Scaling")
                    {
                        model.scaling = ReadVectorProperty(reader, property, model.scaling);
                    }
                });
            }

            cursor = child.endOffset;
        }
    }

    void ParseClusterChildren(FbxReader const& reader, size_t offset, size_t limit, ClusterInfo& cluster, std::wstring& error)
    {
        size_t cursor = offset;
        while (reader.HasNodeHeader(cursor, limit) && !reader.IsNullRecord(cursor, limit))
        {
            Node child{};
            if (!reader.ReadNode(cursor, limit, child)) return;

            if (child.name == "Indexes" && child.propertyCount > 0)
            {
                reader.ReadArrayProperty<int32_t>(reader.PropertyOffset(child, 0), 'i', cluster.indices, error);
            }
            else if (child.name == "Weights" && child.propertyCount > 0)
            {
                reader.ReadArrayProperty<double>(reader.PropertyOffset(child, 0), 'd', cluster.weights, error);
            }
            else if (child.name == "Transform" && child.propertyCount > 0)
            {
                std::vector<double> values;
                if (reader.ReadArrayProperty<double>(reader.PropertyOffset(child, 0), 'd', values, error) && values.size() >= 16)
                {
                    std::copy_n(values.begin(), 16, cluster.transform.begin());
                    cluster.hasTransform = true;
                }
            }
            else if (child.name == "TransformLink" && child.propertyCount > 0)
            {
                std::vector<double> values;
                if (reader.ReadArrayProperty<double>(reader.PropertyOffset(child, 0), 'd', values, error) && values.size() >= 16)
                {
                    std::copy_n(values.begin(), 16, cluster.transformLink.begin());
                    cluster.hasTransformLink = true;
                }
            }

            cursor = child.endOffset;
        }
    }

    void ParseAnimationCurveChildren(FbxReader const& reader, size_t offset, size_t limit, AnimationCurveInfo& curve, std::wstring& error)
    {
        size_t cursor = offset;
        while (reader.HasNodeHeader(cursor, limit) && !reader.IsNullRecord(cursor, limit))
        {
            Node child{};
            if (!reader.ReadNode(cursor, limit, child)) return;

            if (child.name == "KeyTime" && child.propertyCount > 0)
            {
                reader.ReadArrayProperty<int64_t>(reader.PropertyOffset(child, 0), 'l', curve.times, error);
            }
            else if (child.name == "KeyValueFloat" && child.propertyCount > 0)
            {
                reader.ReadArrayProperty<float>(reader.PropertyOffset(child, 0), 'f', curve.values, error);
            }

            cursor = child.endOffset;
        }
    }

    void ParseMaterialChildren(FbxReader const& reader, size_t offset, size_t limit, MaterialInfo& material)
    {
        size_t cursor = offset;
        while (reader.HasNodeHeader(cursor, limit) && !reader.IsNullRecord(cursor, limit))
        {
            Node child{};
            if (!reader.ReadNode(cursor, limit, child)) return;

            if (child.name == "Properties70")
            {
                ParseProperties70(reader, child.childOffset, child.endOffset, [&](Node const& property)
                {
                    if (property.propertyCount >= 7 &&
                        reader.ReadStringProperty(reader.PropertyOffset(property, 0)) == "DiffuseColor")
                    {
                        material.diffuse = XMFLOAT4(
                            static_cast<float>(reader.ReadNumericProperty(reader.PropertyOffset(property, 4))),
                            static_cast<float>(reader.ReadNumericProperty(reader.PropertyOffset(property, 5))),
                            static_cast<float>(reader.ReadNumericProperty(reader.PropertyOffset(property, 6))),
                            1.0f);
                    }
                });
            }

            cursor = child.endOffset;
        }
    }

    void ParseTextureChildren(FbxReader const& reader, size_t offset, size_t limit, TextureInfo& texture)
    {
        size_t cursor = offset;
        while (reader.HasNodeHeader(cursor, limit) && !reader.IsNullRecord(cursor, limit))
        {
            Node child{};
            if (!reader.ReadNode(cursor, limit, child)) return;

            if ((child.name == "FileName" || child.name == "RelativeFilename") && child.propertyCount > 0)
            {
                std::string value = reader.ReadStringProperty(reader.PropertyOffset(child, 0));
                if (child.name == "FileName") texture.fileName = value;
                else texture.relativeFileName = value;
            }
            else if (child.name == "Properties70")
            {
                ParseProperties70(reader, child.childOffset, child.endOffset, [&](Node const& property)
                {
                    if (property.propertyCount < 5) return;

                    const std::string name = reader.ReadStringProperty(reader.PropertyOffset(property, 0));
                    if (name == "FileName")
                    {
                        texture.fileName = reader.ReadStringProperty(reader.PropertyOffset(property, 4));
                    }
                    else if (name == "RelativeFilename")
                    {
                        texture.relativeFileName = reader.ReadStringProperty(reader.PropertyOffset(property, 4));
                    }
                });
            }

            cursor = child.endOffset;
        }
    }

    void TraverseScene(FbxReader const& reader, size_t offset, size_t limit, SceneData& scene, std::wstring& error)
    {
        size_t cursor = offset;
        while (reader.HasNodeHeader(cursor, limit) && !reader.IsNullRecord(cursor, limit))
        {
            Node node{};
            if (!reader.ReadNode(cursor, limit, node)) return;

            if (node.name == "Geometry" && node.propertyCount >= 3)
            {
                const int64_t geometryId = reader.ReadIntegerProperty(reader.PropertyOffset(node, 0));
                const std::string geometryType = reader.ReadStringProperty(reader.PropertyOffset(node, 2));
                if (!scene.foundMesh && geometryType == "Mesh")
                {
                    RawMesh candidate{};
                    candidate.geometryId = geometryId;
                    if (!ParseGeometryChildren(reader, node.childOffset, node.endOffset, candidate, error)) return;
                    if (!candidate.vertices.empty() && !candidate.polygonIndices.empty())
                    {
                        scene.mesh = std::move(candidate);
                        scene.foundMesh = true;
                    }
                }
            }
            else if (node.name == "Model" && node.propertyCount >= 3)
            {
                ModelInfo model{};
                model.id = reader.ReadIntegerProperty(reader.PropertyOffset(node, 0));
                model.name = reader.ReadStringProperty(reader.PropertyOffset(node, 1));
                model.type = reader.ReadStringProperty(reader.PropertyOffset(node, 2));
                ParseModelChildren(reader, node.childOffset, node.endOffset, model);
                scene.models.push_back(model);
            }
            else if (node.name == "Deformer" && node.propertyCount >= 3)
            {
                const std::string deformerType = reader.ReadStringProperty(reader.PropertyOffset(node, 2));
                if (deformerType == "Skin")
                {
                    SkinInfo skin{};
                    skin.id = reader.ReadIntegerProperty(reader.PropertyOffset(node, 0));
                    scene.skins.push_back(skin);
                }
                else if (deformerType == "Cluster")
                {
                    ClusterInfo cluster{};
                    cluster.id = reader.ReadIntegerProperty(reader.PropertyOffset(node, 0));
                    ParseClusterChildren(reader, node.childOffset, node.endOffset, cluster, error);
                    scene.clusters.push_back(std::move(cluster));
                }
            }
            else if (node.name == "Material" && node.propertyCount > 0)
            {
                MaterialInfo material{};
                material.id = reader.ReadIntegerProperty(reader.PropertyOffset(node, 0));
                ParseMaterialChildren(reader, node.childOffset, node.endOffset, material);
                scene.materials.push_back(material);
            }
            else if (node.name == "Texture" && node.propertyCount > 0)
            {
                TextureInfo texture{};
                texture.id = reader.ReadIntegerProperty(reader.PropertyOffset(node, 0));
                ParseTextureChildren(reader, node.childOffset, node.endOffset, texture);
                scene.textures.push_back(texture);
            }
            else if (node.name == "AnimationCurve" && node.propertyCount > 0)
            {
                AnimationCurveInfo curve{};
                curve.id = reader.ReadIntegerProperty(reader.PropertyOffset(node, 0));
                ParseAnimationCurveChildren(reader, node.childOffset, node.endOffset, curve, error);
                if (!curve.times.empty() && !curve.values.empty())
                {
                    scene.curves.push_back(std::move(curve));
                }
            }
            else if (node.name == "AnimationCurveNode" && node.propertyCount > 0)
            {
                AnimationCurveNodeInfo curveNode{};
                curveNode.id = reader.ReadIntegerProperty(reader.PropertyOffset(node, 0));
                scene.curveNodes.push_back(curveNode);
            }
            else if (node.name == "C" && node.propertyCount >= 3)
            {
                ConnectionInfo connection{};
                connection.type = reader.ReadStringProperty(reader.PropertyOffset(node, 0));
                connection.child = reader.ReadIntegerProperty(reader.PropertyOffset(node, 1));
                connection.parent = reader.ReadIntegerProperty(reader.PropertyOffset(node, 2));
                if (node.propertyCount > 3)
                {
                    connection.property = reader.ReadStringProperty(reader.PropertyOffset(node, 3));
                }
                scene.connections.push_back(connection);
            }

            if (node.childOffset < node.endOffset)
            {
                TraverseScene(reader, node.childOffset, node.endOffset, scene, error);
            }

            cursor = node.endOffset;
        }
    }

    XMFLOAT3 Normalize(XMFLOAT3 value)
    {
        XMVECTOR vector = XMLoadFloat3(&value);
        vector = XMVector3Normalize(vector);
        XMFLOAT3 result{};
        XMStoreFloat3(&result, vector);
        return result;
    }

    XMFLOAT3 FaceNormal(XMFLOAT3 const& a, XMFLOAT3 const& b, XMFLOAT3 const& c)
    {
        const XMVECTOR av = XMLoadFloat3(&a);
        const XMVECTOR bv = XMLoadFloat3(&b);
        const XMVECTOR cv = XMLoadFloat3(&c);
        XMVECTOR normal = XMVector3Cross(bv - av, cv - av);
        normal = XMVector3Normalize(normal);

        XMFLOAT3 result{};
        XMStoreFloat3(&result, normal);
        return result;
    }

    void AppendVertex(
        std::vector<LoadedMeshVertex>& output,
        std::vector<XMFLOAT3> const& positions,
        std::vector<XMFLOAT3> const& normals,
        std::vector<XMFLOAT2> const& texcoords,
        VertexInfluences const* influences,
        size_t vertexIndex,
        size_t polygonVertexIndex,
        XMFLOAT4 const& color,
        XMFLOAT3 const& fallbackNormal)
    {
        if (vertexIndex >= positions.size()) return;

        LoadedMeshVertex vertex{};
        vertex.position = positions[vertexIndex];
        vertex.normal = polygonVertexIndex < normals.size() ? normals[polygonVertexIndex] : fallbackNormal;
        vertex.color = color;
        if (polygonVertexIndex < texcoords.size())
        {
            vertex.texcoord = texcoords[polygonVertexIndex];
        }
        if (influences && vertexIndex < influences->size())
        {
            auto const& vertexInfluences = (*influences)[vertexIndex];
            const size_t count = std::min<size_t>(4, vertexInfluences.size());
            uint32_t indices[4]{};
            float weights[4]{};
            for (size_t i = 0; i < count; ++i)
            {
                indices[i] = static_cast<uint32_t>(vertexInfluences[i].first);
                weights[i] = vertexInfluences[i].second;
            }

            vertex.boneIndices = XMUINT4(indices[0], indices[1], indices[2], indices[3]);
            vertex.boneWeights = XMFLOAT4(weights[0], weights[1], weights[2], weights[3]);
        }
        output.push_back(vertex);
    }

    MaterialInfo const* FindMaterial(std::vector<MaterialInfo> const& materials, int64_t id)
    {
        for (auto const& material : materials)
        {
            if (material.id == id) return &material;
        }
        return nullptr;
    }

    TextureInfo const* FindTexture(std::vector<TextureInfo> const& textures, int64_t id)
    {
        for (auto const& texture : textures)
        {
            if (texture.id == id) return &texture;
        }
        return nullptr;
    }

    std::vector<MaterialInfo const*> MaterialSlotsForGeometry(SceneData const& scene)
    {
        std::vector<MaterialInfo const*> slots;
        for (auto const& connection : scene.connections)
        {
            if (connection.parent == scene.mesh.geometryId && (connection.type.empty() || connection.type == "OO"))
            {
                if (auto material = FindMaterial(scene.materials, connection.child))
                {
                    slots.push_back(material);
                }
            }
        }

        if (slots.empty())
        {
            for (auto const& material : scene.materials)
            {
                slots.push_back(&material);
            }
        }

        return slots;
    }

    XMFLOAT4 MaterialColorForPolygon(
        RawMesh const& mesh,
        std::vector<MaterialInfo const*> const& materialSlots,
        size_t polygonIndex)
    {
        if (materialSlots.empty())
        {
            return XMFLOAT4(0.85f, 0.85f, 0.85f, 1.0f);
        }

        int materialIndex = 0;
        if (polygonIndex < mesh.materialIndices.size())
        {
            materialIndex = mesh.materialIndices[polygonIndex];
        }

        if (materialIndex < 0 || static_cast<size_t>(materialIndex) >= materialSlots.size())
        {
            materialIndex = 0;
        }

        return materialSlots[materialIndex]->diffuse;
    }

    std::string TexturePathForScene(SceneData const& scene)
    {
        std::vector<int64_t> materialIds;
        for (auto const& material : MaterialSlotsForGeometry(scene))
        {
            materialIds.push_back(material->id);
        }

        for (int64_t materialId : materialIds)
        {
            for (auto const& connection : scene.connections)
            {
                if (connection.parent != materialId || connection.type != "OP")
                {
                    continue;
                }

                if (!connection.property.empty() && connection.property != "DiffuseColor")
                {
                    continue;
                }

                if (auto texture = FindTexture(scene.textures, connection.child))
                {
                    return !texture->relativeFileName.empty() ? texture->relativeFileName : texture->fileName;
                }
            }
        }

        for (auto const& texture : scene.textures)
        {
            if (!texture.relativeFileName.empty()) return texture.relativeFileName;
            if (!texture.fileName.empty()) return texture.fileName;
        }

        return {};
    }

    std::filesystem::path PathFromFbxString(std::string const& value)
    {
        if (value.empty()) return {};

        std::wstring converted;
        converted.reserve(value.size());
        for (char ch : value)
        {
            converted.push_back(static_cast<unsigned char>(ch));
        }

        return std::filesystem::path(converted);
    }

    ModelInfo const* FindModel(std::vector<ModelInfo> const& models, int64_t id)
    {
        for (auto const& model : models)
        {
            if (model.id == id) return &model;
        }
        return nullptr;
    }

    ClusterInfo const* FindCluster(std::vector<ClusterInfo> const& clusters, int64_t id)
    {
        for (auto const& cluster : clusters)
        {
            if (cluster.id == id) return &cluster;
        }
        return nullptr;
    }

    AnimationCurveInfo const* FindCurve(std::vector<AnimationCurveInfo> const& curves, int64_t id)
    {
        for (auto const& curve : curves)
        {
            if (curve.id == id) return &curve;
        }
        return nullptr;
    }

    XMMATRIX MatrixFromFbxArray(std::array<double, 16> const& values)
    {
        return XMMATRIX(
            static_cast<float>(values[0]), static_cast<float>(values[1]), static_cast<float>(values[2]), static_cast<float>(values[3]),
            static_cast<float>(values[4]), static_cast<float>(values[5]), static_cast<float>(values[6]), static_cast<float>(values[7]),
            static_cast<float>(values[8]), static_cast<float>(values[9]), static_cast<float>(values[10]), static_cast<float>(values[11]),
            static_cast<float>(values[12]), static_cast<float>(values[13]), static_cast<float>(values[14]), static_cast<float>(values[15]));
    }

    XMMATRIX LocalMatrix(XMFLOAT3 const& translation, XMFLOAT3 const& rotation, XMFLOAT3 const& scaling)
    {
        return XMMatrixScaling(scaling.x, scaling.y, scaling.z) *
            XMMatrixRotationX(XMConvertToRadians(rotation.x)) *
            XMMatrixRotationY(XMConvertToRadians(rotation.y)) *
            XMMatrixRotationZ(XMConvertToRadians(rotation.z)) *
            XMMatrixTranslation(translation.x, translation.y, translation.z);
    }

    float EvaluateCurve(AnimationCurveInfo const& curve, int64_t time, float fallback)
    {
        if (curve.times.empty() || curve.values.empty())
        {
            return fallback;
        }

        const size_t count = std::min(curve.times.size(), curve.values.size());
        if (time <= curve.times.front()) return curve.values.front();
        if (time >= curve.times[count - 1]) return curve.values[count - 1];

        for (size_t i = 1; i < count; ++i)
        {
            if (time <= curve.times[i])
            {
                const int64_t t0 = curve.times[i - 1];
                const int64_t t1 = curve.times[i];
                const float v0 = curve.values[i - 1];
                const float v1 = curve.values[i];
                const float alpha = t1 != t0 ? static_cast<float>(time - t0) / static_cast<float>(t1 - t0) : 0.0f;
                return v0 + (v1 - v0) * alpha;
            }
        }

        return curve.values[count - 1];
    }

    void ApplyChannels(std::vector<AnimationChannel> const& channels, int64_t time, XMFLOAT3& value)
    {
        for (auto const& channel : channels)
        {
            if (!channel.curve) continue;

            float* target = nullptr;
            switch (channel.axis)
            {
            case 'X': target = &value.x; break;
            case 'Y': target = &value.y; break;
            case 'Z': target = &value.z; break;
            default: break;
            }

            if (target)
            {
                *target = EvaluateCurve(*channel.curve, time, *target);
            }
        }
    }

    std::unordered_map<int64_t, ModelAnimation> BuildModelAnimations(SceneData const& scene)
    {
        std::unordered_map<int64_t, ModelAnimation> animations;
        struct CurveNodeBinding
        {
            int64_t modelId{};
            std::string property;
        };

        std::unordered_map<int64_t, std::vector<AnimationChannel>> channelsByCurveNode;
        std::unordered_map<int64_t, CurveNodeBinding> bindingByCurveNode;

        for (auto const& connection : scene.connections)
        {
            if (connection.type != "OP") continue;

            if (auto curve = FindCurve(scene.curves, connection.child))
            {
                char axis = 0;
                if (connection.property.find('X') != std::string::npos) axis = 'X';
                else if (connection.property.find('Y') != std::string::npos) axis = 'Y';
                else if (connection.property.find('Z') != std::string::npos) axis = 'Z';
                if (axis)
                {
                    channelsByCurveNode[connection.parent].push_back(AnimationChannel{ curve, axis });
                }
            }
            else if (FindModel(scene.models, connection.parent))
            {
                bindingByCurveNode[connection.child] = CurveNodeBinding{ connection.parent, connection.property };
            }
        }

        for (auto const& item : channelsByCurveNode)
        {
            auto binding = bindingByCurveNode.find(item.first);
            if (binding == bindingByCurveNode.end()) continue;

            ModelAnimation& animation = animations[binding->second.modelId];
            if (binding->second.property == "Lcl Translation")
            {
                animation.translation.insert(animation.translation.end(), item.second.begin(), item.second.end());
            }
            else if (binding->second.property == "Lcl Rotation")
            {
                animation.rotation.insert(animation.rotation.end(), item.second.begin(), item.second.end());
            }
            else if (binding->second.property == "Lcl Scaling")
            {
                animation.scaling.insert(animation.scaling.end(), item.second.begin(), item.second.end());
            }
        }

        return animations;
    }

    void ResolveModelParents(SceneData& scene)
    {
        for (auto& model : scene.models)
        {
            model.parentId = 0;
            for (auto const& connection : scene.connections)
            {
                if (connection.type == "OO" && connection.child == model.id && FindModel(scene.models, connection.parent))
                {
                    model.parentId = connection.parent;
                    break;
                }
            }
        }
    }

    XMMATRIX ModelGlobalMatrix(
        SceneData const& scene,
        std::unordered_map<int64_t, ModelAnimation> const& animations,
        int64_t modelId,
        int64_t time,
        std::unordered_map<int64_t, XMMATRIX>& cache,
        std::unordered_set<int64_t>& visiting)
    {
        auto cached = cache.find(modelId);
        if (cached != cache.end()) return cached->second;

        ModelInfo const* model = FindModel(scene.models, modelId);
        if (!model || visiting.count(modelId))
        {
            return XMMatrixIdentity();
        }

        visiting.insert(modelId);
        XMFLOAT3 translation = model->translation;
        XMFLOAT3 rotation = model->rotation;
        XMFLOAT3 scaling = model->scaling;

        auto animation = animations.find(modelId);
        if (animation != animations.end())
        {
            ApplyChannels(animation->second.translation, time, translation);
            ApplyChannels(animation->second.rotation, time, rotation);
            ApplyChannels(animation->second.scaling, time, scaling);
        }

        XMMATRIX global = LocalMatrix(translation, rotation, scaling);
        if (model->parentId != 0)
        {
            global = global * ModelGlobalMatrix(scene, animations, model->parentId, time, cache, visiting);
        }

        visiting.erase(modelId);
        cache[modelId] = global;
        return global;
    }

    std::vector<BoundCluster> ClustersForMesh(SceneData const& scene)
    {
        std::vector<int64_t> skinIds;
        for (auto const& connection : scene.connections)
        {
            if (connection.type == "OO" && connection.parent == scene.mesh.geometryId)
            {
                for (auto const& skin : scene.skins)
                {
                    if (skin.id == connection.child)
                    {
                        skinIds.push_back(skin.id);
                        break;
                    }
                }
            }
        }

        std::vector<BoundCluster> clusters;
        for (int64_t skinId : skinIds)
        {
            for (auto const& connection : scene.connections)
            {
                if (connection.type == "OO" && connection.parent == skinId)
                {
                    if (auto cluster = FindCluster(scene.clusters, connection.child))
                    {
                        BoundCluster bound{};
                        bound.cluster = cluster;
                        clusters.push_back(bound);
                    }
                }
            }
        }

        for (auto& bound : clusters)
        {
            for (auto const& connection : scene.connections)
            {
                if (connection.type == "OO" && connection.parent == bound.cluster->id && FindModel(scene.models, connection.child))
                {
                    bound.boneModelId = connection.child;
                    break;
                }
            }
        }

        return clusters;
    }

    int64_t MeshModelIdForGeometry(SceneData const& scene)
    {
        for (auto const& connection : scene.connections)
        {
            if (connection.type == "OO" &&
                connection.child == scene.mesh.geometryId &&
                FindModel(scene.models, connection.parent))
            {
                return connection.parent;
            }
        }

        return 0;
    }

    VertexInfluences BuildInfluences(RawMesh const& mesh, std::vector<BoundCluster> const& clusters)
    {
        const size_t controlPointCount = mesh.vertices.size() / 3;
        VertexInfluences influences(controlPointCount);

        for (size_t clusterIndex = 0; clusterIndex < clusters.size(); ++clusterIndex)
        {
            ClusterInfo const& cluster = *clusters[clusterIndex].cluster;
            const size_t count = std::min(cluster.indices.size(), cluster.weights.size());
            for (size_t i = 0; i < count; ++i)
            {
                const int32_t controlPoint = cluster.indices[i];
                if (controlPoint >= 0 && static_cast<size_t>(controlPoint) < influences.size())
                {
                    influences[static_cast<size_t>(controlPoint)].push_back({ clusterIndex, static_cast<float>(cluster.weights[i]) });
                }
            }
        }

        for (auto& item : influences)
        {
            if (item.size() > 4)
            {
                std::sort(item.begin(), item.end(), [](auto const& a, auto const& b) { return a.second > b.second; });
                item.resize(4);
            }

            float total = 0.0f;
            for (auto const& influence : item) total += influence.second;
            if (total > 0.0001f)
            {
                for (auto& influence : item) influence.second /= total;
            }
        }

        return influences;
    }

    std::vector<XMFLOAT3> MeshPositions(RawMesh const& mesh, XMFLOAT3& center, float& modelScale)
    {
        std::vector<XMFLOAT3> positions;
        positions.reserve(mesh.vertices.size() / 3);

        XMFLOAT3 minBounds{ FLT_MAX, FLT_MAX, FLT_MAX };
        XMFLOAT3 maxBounds{ -FLT_MAX, -FLT_MAX, -FLT_MAX };
        for (size_t i = 0; i + 2 < mesh.vertices.size(); i += 3)
        {
            XMFLOAT3 position{
                static_cast<float>(mesh.vertices[i]),
                static_cast<float>(mesh.vertices[i + 1]),
                static_cast<float>(mesh.vertices[i + 2])
            };

            minBounds.x = std::min(minBounds.x, position.x);
            minBounds.y = std::min(minBounds.y, position.y);
            minBounds.z = std::min(minBounds.z, position.z);
            maxBounds.x = std::max(maxBounds.x, position.x);
            maxBounds.y = std::max(maxBounds.y, position.y);
            maxBounds.z = std::max(maxBounds.z, position.z);
            positions.push_back(position);
        }

        center = XMFLOAT3(
            (minBounds.x + maxBounds.x) * 0.5f,
            (minBounds.y + maxBounds.y) * 0.5f,
            (minBounds.z + maxBounds.z) * 0.5f);
        const float extentX = maxBounds.x - minBounds.x;
        const float extentY = maxBounds.y - minBounds.y;
        const float extentZ = maxBounds.z - minBounds.z;
        const float largestExtent = std::max({ extentX, extentY, extentZ, 1.0f });
        modelScale = 1.65f / largestExtent;
        return positions;
    }

    std::vector<XMFLOAT3> MeshNormals(RawMesh const& mesh)
    {
        std::vector<XMFLOAT3> normals;
        normals.reserve(mesh.normals.size() / 3);
        for (size_t i = 0; i + 2 < mesh.normals.size(); i += 3)
        {
            normals.push_back(Normalize(XMFLOAT3{
                static_cast<float>(mesh.normals[i]),
                static_cast<float>(mesh.normals[i + 1]),
                static_cast<float>(mesh.normals[i + 2])
            }));
        }
        return normals;
    }

    void NormalizeMeshPositions(std::vector<XMFLOAT3>& positions, XMFLOAT3 const& center, float modelScale)
    {
        for (auto& position : positions)
        {
            position.x = (position.x - center.x) * modelScale;
            position.y = (position.y - center.y) * modelScale;
            position.z = (position.z - center.z) * modelScale;
        }
    }

    bool BuildRenderableMeshFromPositions(
        SceneData const& scene,
        std::vector<XMFLOAT3> const& positions,
        std::vector<XMFLOAT3> const& normals,
        VertexInfluences const* influences,
        std::vector<LoadedMeshVertex>& output,
        std::wstring& error)
    {
        RawMesh const& mesh = scene.mesh;
        output.clear();

        std::vector<XMFLOAT2> texcoords;
        texcoords.reserve(mesh.uvIndices.empty() ? mesh.uvs.size() / 2 : mesh.uvIndices.size());
        if (!mesh.uvs.empty())
        {
            if (!mesh.uvIndices.empty())
            {
                for (int32_t uvIndex : mesh.uvIndices)
                {
                    if (uvIndex >= 0 && static_cast<size_t>(uvIndex) * 2 + 1 < mesh.uvs.size())
                    {
                        texcoords.push_back(XMFLOAT2(
                            static_cast<float>(mesh.uvs[static_cast<size_t>(uvIndex) * 2]),
                            1.0f - static_cast<float>(mesh.uvs[static_cast<size_t>(uvIndex) * 2 + 1])));
                    }
                    else
                    {
                        texcoords.push_back(XMFLOAT2(0.0f, 0.0f));
                    }
                }
            }
            else
            {
                for (size_t i = 0; i + 1 < mesh.uvs.size(); i += 2)
                {
                    texcoords.push_back(XMFLOAT2(
                        static_cast<float>(mesh.uvs[i]),
                        1.0f - static_cast<float>(mesh.uvs[i + 1])));
                }
            }
        }

        const auto materialSlots = MaterialSlotsForGeometry(scene);
        std::vector<size_t> polygonVertices;
        std::vector<size_t> polygonVertexSequence;
        size_t sequence = 0;
        size_t polygonIndex = 0;

        for (int32_t encodedIndex : mesh.polygonIndices)
        {
            const bool endsPolygon = encodedIndex < 0;
            const int32_t decoded = endsPolygon ? -encodedIndex - 1 : encodedIndex;
            if (decoded < 0)
            {
                error = L"FBX polygon index is invalid.";
                return false;
            }
            if (static_cast<size_t>(decoded) >= positions.size())
            {
                error = L"FBX polygon index points outside the vertex array.";
                return false;
            }

            polygonVertices.push_back(static_cast<size_t>(decoded));
            polygonVertexSequence.push_back(sequence++);

            if (endsPolygon)
            {
                if (polygonVertices.size() >= 3)
                {
                    const XMFLOAT4 color = MaterialColorForPolygon(mesh, materialSlots, polygonIndex);
                    const XMFLOAT3 fallback = FaceNormal(
                        positions[polygonVertices[0]],
                        positions[polygonVertices[1]],
                        positions[polygonVertices[2]]);

                    for (size_t i = 1; i + 1 < polygonVertices.size(); ++i)
                    {
                        AppendVertex(output, positions, normals, texcoords, influences, polygonVertices[0], polygonVertexSequence[0], color, fallback);
                        AppendVertex(output, positions, normals, texcoords, influences, polygonVertices[i], polygonVertexSequence[i], color, fallback);
                        AppendVertex(output, positions, normals, texcoords, influences, polygonVertices[i + 1], polygonVertexSequence[i + 1], color, fallback);
                    }
                }

                polygonVertices.clear();
                polygonVertexSequence.clear();
                ++polygonIndex;
            }
        }

        if (output.empty())
        {
            error = L"FBX mesh did not produce any renderable triangles.";
            return false;
        }

        return true;
    }

    bool BuildRenderableMesh(
        SceneData const& scene,
        std::vector<LoadedMeshVertex>& output,
        std::wstring& error,
        VertexInfluences const* influences = nullptr)
    {
        RawMesh const& mesh = scene.mesh;
        if (mesh.vertices.size() < 9 || mesh.vertices.size() % 3 != 0 || mesh.polygonIndices.empty())
        {
            error = L"FBX mesh does not contain usable vertices or polygons.";
            return false;
        }

        XMFLOAT3 center{};
        float modelScale{};
        auto positions = MeshPositions(mesh, center, modelScale);
        NormalizeMeshPositions(positions, center, modelScale);
        return BuildRenderableMeshFromPositions(scene, positions, MeshNormals(mesh), influences, output, error);
    }

    bool BakeAnimationFrames(SceneData& scene, LoadedMeshAnimation& animation, std::wstring& error)
    {
        animation.frames.clear();
        animation.boneFrames.clear();
        animation.durationSeconds = 0.0f;
        animation.boneCount = 0;

        ResolveModelParents(scene);
        const auto clusters = ClustersForMesh(scene);
        if (clusters.empty())
        {
            return false;
        }
        const int64_t meshModelId = MeshModelIdForGeometry(scene);

        const auto modelAnimations = BuildModelAnimations(scene);
        if (modelAnimations.empty())
        {
            return false;
        }

        int64_t minTime = std::numeric_limits<int64_t>::max();
        int64_t maxTime = std::numeric_limits<int64_t>::min();
        for (auto const& curve : scene.curves)
        {
            if (curve.times.empty()) continue;
            minTime = std::min(minTime, curve.times.front());
            maxTime = std::max(maxTime, curve.times.back());
        }

        if (minTime == std::numeric_limits<int64_t>::max() || maxTime <= minTime)
        {
            return false;
        }

        XMFLOAT3 center{};
        float modelScale{};
        const auto basePositions = MeshPositions(scene.mesh, center, modelScale);
        const auto normals = MeshNormals(scene.mesh);
        const auto influences = BuildInfluences(scene.mesh, clusters);
        if (basePositions.empty() || influences.empty())
        {
            return false;
        }

        const double durationSeconds = static_cast<double>(maxTime - minTime) / FbxTimeUnitsPerSecond;
        const size_t frameCount = std::clamp(static_cast<size_t>(std::ceil(durationSeconds * 30.0)) + 1, size_t(2), size_t(180));
        animation.durationSeconds = static_cast<float>(durationSeconds);
        animation.boneCount = static_cast<uint32_t>(clusters.size());
        animation.frames.reserve(frameCount);
        animation.boneFrames.reserve(frameCount);

        const XMMATRIX rawToNormalized =
            XMMatrixTranslation(-center.x, -center.y, -center.z) *
            XMMatrixScaling(modelScale, modelScale, modelScale);
        const XMMATRIX normalizedToRaw =
            XMMatrixScaling(1.0f / modelScale, 1.0f / modelScale, 1.0f / modelScale) *
            XMMatrixTranslation(center.x, center.y, center.z);

        std::vector<XMFLOAT3> skinnedPositions(basePositions.size());
        for (size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex)
        {
            const double alpha = frameCount > 1 ? static_cast<double>(frameIndex) / static_cast<double>(frameCount - 1) : 0.0;
            const int64_t time = minTime + static_cast<int64_t>((maxTime - minTime) * alpha);
            std::unordered_map<int64_t, XMMATRIX> globalCache;
            std::unordered_set<int64_t> visiting;
            XMMATRIX currentMesh = XMMatrixIdentity();
            if (meshModelId != 0)
            {
                currentMesh = ModelGlobalMatrix(scene, modelAnimations, meshModelId, time, globalCache, visiting);
            }
            XMVECTOR currentMeshDeterminant{};
            XMMATRIX inverseCurrentMesh = XMMatrixInverse(&currentMeshDeterminant, currentMesh);
            std::vector<XMMATRIX> skinMatrices(clusters.size(), XMMatrixIdentity());
            std::vector<bool> validSkinMatrices(clusters.size(), false);
            std::vector<XMFLOAT4X4> boneFrame(clusters.size());

            for (size_t clusterIndex = 0; clusterIndex < clusters.size(); ++clusterIndex)
            {
                BoundCluster const& bound = clusters[clusterIndex];
                ClusterInfo const& cluster = *bound.cluster;
                XMMATRIX skinMatrix = XMMatrixIdentity();
                if (bound.boneModelId != 0 && cluster.hasTransformLink)
                {
                    XMMATRIX meshBind = cluster.hasTransform ? MatrixFromFbxArray(cluster.transform) : XMMatrixIdentity();
                    XMMATRIX boneBind = MatrixFromFbxArray(cluster.transformLink);
                    XMMATRIX currentBone = ModelGlobalMatrix(scene, modelAnimations, bound.boneModelId, time, globalCache, visiting);
                    XMVECTOR determinant{};
                    skinMatrix = meshBind * XMMatrixInverse(&determinant, boneBind) * currentBone * inverseCurrentMesh;
                    skinMatrices[clusterIndex] = skinMatrix;
                    validSkinMatrices[clusterIndex] = true;
                }

                XMStoreFloat4x4(&boneFrame[clusterIndex], normalizedToRaw * skinMatrix * rawToNormalized);
            }
            animation.boneFrames.push_back(std::move(boneFrame));

            for (size_t vertexIndex = 0; vertexIndex < basePositions.size(); ++vertexIndex)
            {
                XMVECTOR result = XMVectorZero();
                float totalWeight = 0.0f;
                XMVECTOR base = XMLoadFloat3(&basePositions[vertexIndex]);

                for (auto const& influence : influences[vertexIndex])
                {
                    if (influence.first >= clusters.size()) continue;
                    if (!validSkinMatrices[influence.first])
                    {
                        continue;
                    }

                    result += XMVector3TransformCoord(base, skinMatrices[influence.first]) * influence.second;
                    totalWeight += influence.second;
                }

                if (totalWeight <= 0.0001f)
                {
                    result = base;
                }
                else if (totalWeight < 0.999f)
                {
                    result += base * (1.0f - totalWeight);
                }

                XMStoreFloat3(&skinnedPositions[vertexIndex], result);
            }

            NormalizeMeshPositions(skinnedPositions, center, modelScale);

            std::vector<LoadedMeshVertex> frame;
            if (!BuildRenderableMeshFromPositions(scene, skinnedPositions, normals, nullptr, frame, error))
            {
                animation.frames.clear();
                animation.boneFrames.clear();
                animation.durationSeconds = 0.0f;
                animation.boneCount = 0;
                return false;
            }

            animation.frames.push_back(std::move(frame));
        }

        return animation.frames.size() > 1;
    }

    struct AssimpBoneBinding
    {
        std::string name;
        aiMatrix4x4 offset;
    };

    struct AssimpInfluence
    {
        uint32_t boneIndex{};
        float weight{};
    };

    struct AssimpMeshRuntime
    {
        aiMesh const* mesh{};
        aiMatrix4x4 bindGlobal;
        XMFLOAT4 diffuse{ 0.85f, 0.85f, 0.85f, 1.0f };
        std::vector<AssimpBoneBinding> bones;
        std::vector<std::vector<AssimpInfluence>> influences;
    };

    aiVector3D TransformPoint(aiMatrix4x4 const& matrix, aiVector3D const& value)
    {
        return aiVector3D(
            matrix.a1 * value.x + matrix.a2 * value.y + matrix.a3 * value.z + matrix.a4,
            matrix.b1 * value.x + matrix.b2 * value.y + matrix.b3 * value.z + matrix.b4,
            matrix.c1 * value.x + matrix.c2 * value.y + matrix.c3 * value.z + matrix.c4);
    }

    aiVector3D TransformVector(aiMatrix4x4 const& matrix, aiVector3D const& value)
    {
        aiVector3D result(
            matrix.a1 * value.x + matrix.a2 * value.y + matrix.a3 * value.z,
            matrix.b1 * value.x + matrix.b2 * value.y + matrix.b3 * value.z,
            matrix.c1 * value.x + matrix.c2 * value.y + matrix.c3 * value.z);
        result.NormalizeSafe();
        return result;
    }

    XMFLOAT3 ToFloat3(aiVector3D const& value)
    {
        return XMFLOAT3(value.x, value.y, value.z);
    }

    aiNodeAnim const* FindAssimpChannel(aiAnimation const* animation, char const* nodeName)
    {
        if (!animation || !nodeName) return nullptr;

        for (unsigned i = 0; i < animation->mNumChannels; ++i)
        {
            aiNodeAnim const* channel = animation->mChannels[i];
            if (channel && std::strcmp(channel->mNodeName.C_Str(), nodeName) == 0)
            {
                return channel;
            }
        }

        return nullptr;
    }

    aiVector3D InterpolateAssimpVector(
        aiVectorKey const* keys,
        unsigned count,
        double time,
        aiVector3D const& fallback)
    {
        if (!keys || count == 0) return fallback;
        if (count == 1 || time <= keys[0].mTime) return keys[0].mValue;
        if (time >= keys[count - 1].mTime) return keys[count - 1].mValue;

        for (unsigned i = 1; i < count; ++i)
        {
            if (time <= keys[i].mTime)
            {
                const double t0 = keys[i - 1].mTime;
                const double t1 = keys[i].mTime;
                const float alpha = t1 != t0 ? static_cast<float>((time - t0) / (t1 - t0)) : 0.0f;
                return keys[i - 1].mValue + (keys[i].mValue - keys[i - 1].mValue) * alpha;
            }
        }

        return keys[count - 1].mValue;
    }

    aiQuaternion InterpolateAssimpRotation(
        aiQuatKey const* keys,
        unsigned count,
        double time,
        aiQuaternion const& fallback)
    {
        if (!keys || count == 0) return fallback;
        if (count == 1 || time <= keys[0].mTime) return keys[0].mValue;
        if (time >= keys[count - 1].mTime) return keys[count - 1].mValue;

        for (unsigned i = 1; i < count; ++i)
        {
            if (time <= keys[i].mTime)
            {
                const double t0 = keys[i - 1].mTime;
                const double t1 = keys[i].mTime;
                const double alpha = t1 != t0 ? (time - t0) / (t1 - t0) : 0.0;
                aiQuaternion result;
                aiQuaternion::Interpolate(result, keys[i - 1].mValue, keys[i].mValue, static_cast<ai_real>(alpha));
                result.Normalize();
                return result;
            }
        }

        return keys[count - 1].mValue;
    }

    aiMatrix4x4 AssimpNodeLocalTransform(aiNode const* node, aiAnimation const* animation, double time)
    {
        aiMatrix4x4 local = node->mTransformation;
        aiNodeAnim const* channel = FindAssimpChannel(animation, node->mName.C_Str());
        if (!channel)
        {
            return local;
        }

        aiVector3D fallbackScale;
        aiQuaternion fallbackRotation;
        aiVector3D fallbackTranslation;
        local.Decompose(fallbackScale, fallbackRotation, fallbackTranslation);

        const aiVector3D scale = InterpolateAssimpVector(channel->mScalingKeys, channel->mNumScalingKeys, time, fallbackScale);
        const aiQuaternion rotation = InterpolateAssimpRotation(channel->mRotationKeys, channel->mNumRotationKeys, time, fallbackRotation);
        const aiVector3D translation = InterpolateAssimpVector(channel->mPositionKeys, channel->mNumPositionKeys, time, fallbackTranslation);

        aiMatrix4x4 scaleMatrix;
        aiMatrix4x4 rotationMatrix(rotation.GetMatrix());
        aiMatrix4x4 translationMatrix;
        aiMatrix4x4::Scaling(scale, scaleMatrix);
        aiMatrix4x4::Translation(translation, translationMatrix);
        return translationMatrix * rotationMatrix * scaleMatrix;
    }

    void BuildAssimpGlobals(
        aiNode const* node,
        aiMatrix4x4 const& parent,
        aiAnimation const* animation,
        double time,
        std::unordered_map<std::string, aiMatrix4x4>& globals)
    {
        if (!node) return;

        const aiMatrix4x4 global = parent * AssimpNodeLocalTransform(node, animation, time);
        globals[node->mName.C_Str()] = global;
        for (unsigned i = 0; i < node->mNumChildren; ++i)
        {
            BuildAssimpGlobals(node->mChildren[i], global, animation, time, globals);
        }
    }

    XMFLOAT4 AssimpMaterialColor(aiScene const* scene, aiMesh const* mesh)
    {
        if (!scene || !mesh || mesh->mMaterialIndex >= scene->mNumMaterials)
        {
            return XMFLOAT4(0.85f, 0.85f, 0.85f, 0.0f);
        }

        bool usesDiffuseTexture = false;
        aiString texturePath;
        if (AI_SUCCESS == scene->mMaterials[mesh->mMaterialIndex]->GetTexture(aiTextureType_DIFFUSE, 0, &texturePath))
        {
            std::string value = texturePath.C_Str();
            usesDiffuseTexture = !value.empty() && value[0] != '*';
        }
        if (usesDiffuseTexture)
        {
            return XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
        }

        aiColor4D color;
        if (AI_SUCCESS == aiGetMaterialColor(scene->mMaterials[mesh->mMaterialIndex], AI_MATKEY_COLOR_DIFFUSE, &color))
        {
            return XMFLOAT4(color.r, color.g, color.b, 0.0f);
        }

        return XMFLOAT4(0.85f, 0.85f, 0.85f, 0.0f);
    }

    std::filesystem::path AssimpTexturePath(aiScene const* scene)
    {
        if (!scene) return {};

        for (unsigned i = 0; i < scene->mNumMaterials; ++i)
        {
            aiString texturePath;
            if (AI_SUCCESS == scene->mMaterials[i]->GetTexture(aiTextureType_DIFFUSE, 0, &texturePath))
            {
                std::string value = texturePath.C_Str();
                if (!value.empty() && value[0] != '*')
                {
                    return PathFromFbxString(value);
                }
            }
        }

        return {};
    }

    std::vector<std::vector<AssimpInfluence>> BuildAssimpInfluences(aiMesh const* mesh, std::vector<AssimpBoneBinding>& bones)
    {
        std::vector<std::vector<AssimpInfluence>> influences(mesh ? mesh->mNumVertices : 0);
        if (!mesh)
        {
            return influences;
        }

        bones.clear();
        bones.reserve(mesh->mNumBones);
        for (unsigned boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex)
        {
            aiBone const* bone = mesh->mBones[boneIndex];
            if (!bone) continue;

            bones.push_back(AssimpBoneBinding{ bone->mName.C_Str(), bone->mOffsetMatrix });
            const uint32_t storedBoneIndex = static_cast<uint32_t>(bones.size() - 1);
            for (unsigned weightIndex = 0; weightIndex < bone->mNumWeights; ++weightIndex)
            {
                aiVertexWeight const& weight = bone->mWeights[weightIndex];
                if (weight.mVertexId < influences.size() && weight.mWeight > 0.0f)
                {
                    influences[weight.mVertexId].push_back(AssimpInfluence{ storedBoneIndex, weight.mWeight });
                }
            }
        }

        for (auto& vertexInfluences : influences)
        {
            if (vertexInfluences.size() > 4)
            {
                std::sort(vertexInfluences.begin(), vertexInfluences.end(), [](auto const& a, auto const& b)
                {
                    return a.weight > b.weight;
                });
                vertexInfluences.resize(4);
            }

            float total = 0.0f;
            for (auto const& influence : vertexInfluences) total += influence.weight;
            if (total > 0.0001f)
            {
                for (auto& influence : vertexInfluences) influence.weight /= total;
            }
        }

        return influences;
    }

    void GatherAssimpMeshes(
        aiScene const* scene,
        aiNode const* node,
        aiMatrix4x4 const& parent,
        std::vector<AssimpMeshRuntime>& meshes)
    {
        if (!scene || !node) return;

        const aiMatrix4x4 global = parent * node->mTransformation;
        for (unsigned i = 0; i < node->mNumMeshes; ++i)
        {
            const unsigned meshIndex = node->mMeshes[i];
            if (meshIndex >= scene->mNumMeshes) continue;

            AssimpMeshRuntime runtime{};
            runtime.mesh = scene->mMeshes[meshIndex];
            runtime.bindGlobal = global;
            runtime.diffuse = AssimpMaterialColor(scene, runtime.mesh);
            runtime.influences = BuildAssimpInfluences(runtime.mesh, runtime.bones);
            meshes.push_back(std::move(runtime));
        }

        for (unsigned i = 0; i < node->mNumChildren; ++i)
        {
            GatherAssimpMeshes(scene, node->mChildren[i], global, meshes);
        }
    }

    void AssimpSkinnedVertex(
        AssimpMeshRuntime const& runtime,
        size_t vertexIndex,
        std::unordered_map<std::string, aiMatrix4x4> const& globals,
        aiMatrix4x4 const& rootInverse,
        aiVector3D& position,
        aiVector3D& normal)
    {
        aiMesh const* mesh = runtime.mesh;
        if (!mesh || vertexIndex >= mesh->mNumVertices)
        {
            position = {};
            normal = aiVector3D(0.0f, 1.0f, 0.0f);
            return;
        }

        aiVector3D const& basePosition = mesh->mVertices[vertexIndex];
        aiVector3D const baseNormal = mesh->HasNormals() ? mesh->mNormals[vertexIndex] : aiVector3D(0.0f, 1.0f, 0.0f);
        aiMatrix4x4 const unskinnedMatrix = rootInverse * runtime.bindGlobal;
        aiVector3D unskinnedPosition = TransformPoint(unskinnedMatrix, basePosition);
        aiVector3D unskinnedNormal = TransformVector(unskinnedMatrix, baseNormal);

        if (vertexIndex >= runtime.influences.size() || runtime.influences[vertexIndex].empty())
        {
            position = unskinnedPosition;
            normal = unskinnedNormal;
            return;
        }

        aiVector3D skinnedPosition{};
        aiVector3D skinnedNormal{};
        float totalWeight = 0.0f;
        for (auto const& influence : runtime.influences[vertexIndex])
        {
            if (influence.boneIndex >= runtime.bones.size()) continue;

            auto const& bone = runtime.bones[influence.boneIndex];
            auto found = globals.find(bone.name);
            if (found == globals.end()) continue;

            const aiMatrix4x4 finalMatrix = rootInverse * found->second * bone.offset * runtime.bindGlobal;
            skinnedPosition += TransformPoint(finalMatrix, basePosition) * influence.weight;
            skinnedNormal += TransformVector(finalMatrix, baseNormal) * influence.weight;
            totalWeight += influence.weight;
        }

        if (totalWeight <= 0.0001f)
        {
            position = unskinnedPosition;
            normal = unskinnedNormal;
            return;
        }

        if (totalWeight < 0.999f)
        {
            skinnedPosition += unskinnedPosition * (1.0f - totalWeight);
            skinnedNormal += unskinnedNormal * (1.0f - totalWeight);
        }

        skinnedNormal.NormalizeSafe();
        position = skinnedPosition;
        normal = skinnedNormal;
    }

    bool ComputeAssimpBounds(
        std::vector<AssimpMeshRuntime> const& meshes,
        std::unordered_map<std::string, aiMatrix4x4> const& globals,
        aiMatrix4x4 const& rootInverse,
        XMFLOAT3& center,
        float& modelScale)
    {
        XMFLOAT3 minBounds{ FLT_MAX, FLT_MAX, FLT_MAX };
        XMFLOAT3 maxBounds{ -FLT_MAX, -FLT_MAX, -FLT_MAX };
        bool found = false;

        for (auto const& runtime : meshes)
        {
            aiMesh const* mesh = runtime.mesh;
            if (!mesh) continue;

            for (unsigned i = 0; i < mesh->mNumVertices; ++i)
            {
                aiVector3D position;
                aiVector3D normal;
                AssimpSkinnedVertex(runtime, i, globals, rootInverse, position, normal);

                minBounds.x = std::min(minBounds.x, position.x);
                minBounds.y = std::min(minBounds.y, position.y);
                minBounds.z = std::min(minBounds.z, position.z);
                maxBounds.x = std::max(maxBounds.x, position.x);
                maxBounds.y = std::max(maxBounds.y, position.y);
                maxBounds.z = std::max(maxBounds.z, position.z);
                found = true;
            }
        }

        if (!found)
        {
            return false;
        }

        center = XMFLOAT3(
            (minBounds.x + maxBounds.x) * 0.5f,
            (minBounds.y + maxBounds.y) * 0.5f,
            (minBounds.z + maxBounds.z) * 0.5f);
        const float largestExtent = std::max({
            maxBounds.x - minBounds.x,
            maxBounds.y - minBounds.y,
            maxBounds.z - minBounds.z,
            1.0f });
        modelScale = 1.65f / largestExtent;
        return true;
    }

    bool BuildAssimpVertices(
        std::vector<AssimpMeshRuntime> const& meshes,
        std::unordered_map<std::string, aiMatrix4x4> const& globals,
        aiMatrix4x4 const& rootInverse,
        XMFLOAT3 const& center,
        float modelScale,
        std::vector<LoadedMeshVertex>& output)
    {
        output.clear();

        for (auto const& runtime : meshes)
        {
            aiMesh const* mesh = runtime.mesh;
            if (!mesh) continue;

            for (unsigned faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex)
            {
                aiFace const& face = mesh->mFaces[faceIndex];
                if (face.mNumIndices != 3) continue;

                for (unsigned corner = 0; corner < 3; ++corner)
                {
                    const unsigned vertexIndex = face.mIndices[corner];
                    if (vertexIndex >= mesh->mNumVertices) continue;

                    aiVector3D position;
                    aiVector3D normal;
                    AssimpSkinnedVertex(runtime, vertexIndex, globals, rootInverse, position, normal);

                    LoadedMeshVertex vertex{};
                    vertex.position = XMFLOAT3(
                        (position.x - center.x) * modelScale,
                        (position.y - center.y) * modelScale,
                        (position.z - center.z) * modelScale);
                    vertex.normal = Normalize(ToFloat3(normal));
                    vertex.color = runtime.diffuse;
                    if (mesh->HasTextureCoords(0))
                    {
                        aiVector3D const& uv = mesh->mTextureCoords[0][vertexIndex];
                        vertex.texcoord = XMFLOAT2(uv.x, uv.y);
                    }
                    output.push_back(vertex);
                }
            }
        }

        return !output.empty();
    }

    bool LoadFbxMeshWithAssimp(
        std::filesystem::path const& path,
        std::vector<LoadedMeshVertex>& vertices,
        std::wstring& error,
        std::filesystem::path* diffuseTexturePath,
        LoadedMeshAnimation* animation)
    {
        auto utf8Path = path.u8string();
        std::string importPath(reinterpret_cast<char const*>(utf8Path.c_str()), utf8Path.size());

        Assimp::Importer importer;
        aiScene const* scene = importer.ReadFile(
            importPath,
            aiProcess_Triangulate |
            aiProcess_GenSmoothNormals |
            aiProcess_LimitBoneWeights |
            aiProcess_ImproveCacheLocality |
            aiProcess_ConvertToLeftHanded |
            aiProcess_ValidateDataStructure);

        if (!scene || !scene->mRootNode || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE))
        {
            std::string message = importer.GetErrorString();
            if (message.empty()) message = "Assimp could not import this FBX.";
            error.assign(message.begin(), message.end());
            return false;
        }

        std::vector<AssimpMeshRuntime> meshes;
        GatherAssimpMeshes(scene, scene->mRootNode, aiMatrix4x4(), meshes);
        if (meshes.empty())
        {
            error = L"Assimp imported the FBX but found no renderable meshes.";
            return false;
        }

        aiMatrix4x4 rootInverse = scene->mRootNode->mTransformation;
        rootInverse.Inverse();

        std::unordered_map<std::string, aiMatrix4x4> bindGlobals;
        BuildAssimpGlobals(scene->mRootNode, aiMatrix4x4(), nullptr, 0.0, bindGlobals);

        XMFLOAT3 center{};
        float modelScale{};
        if (!ComputeAssimpBounds(meshes, bindGlobals, rootInverse, center, modelScale))
        {
            error = L"Assimp imported the FBX but could not compute mesh bounds.";
            return false;
        }

        if (!BuildAssimpVertices(meshes, bindGlobals, rootInverse, center, modelScale, vertices))
        {
            error = L"Assimp imported the FBX but produced no triangles.";
            return false;
        }

        if (diffuseTexturePath)
        {
            *diffuseTexturePath = AssimpTexturePath(scene);
        }

        if (animation)
        {
            animation->frames.clear();
            animation->boneFrames.clear();
            animation->durationSeconds = 0.0f;
            animation->boneCount = 0;

            aiAnimation const* clip = nullptr;
            for (unsigned i = 0; i < scene->mNumAnimations; ++i)
            {
                if (scene->mAnimations[i] && scene->mAnimations[i]->mDuration > 0.0)
                {
                    clip = scene->mAnimations[i];
                    break;
                }
            }

            if (clip)
            {
                const double ticksPerSecond = clip->mTicksPerSecond > 0.0 ? clip->mTicksPerSecond : 25.0;
                const double durationSeconds = clip->mDuration / ticksPerSecond;
                if (durationSeconds > 0.0)
                {
                    const size_t frameCount = std::clamp(
                        static_cast<size_t>(std::ceil(durationSeconds * 30.0)) + 1,
                        size_t(2),
                        size_t(180));
                    animation->durationSeconds = static_cast<float>(durationSeconds);
                    animation->frames.reserve(frameCount);

                    for (size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex)
                    {
                        const double alpha = frameCount > 1 ? static_cast<double>(frameIndex) / static_cast<double>(frameCount - 1) : 0.0;
                        const double time = clip->mDuration * alpha;
                        std::unordered_map<std::string, aiMatrix4x4> globals;
                        BuildAssimpGlobals(scene->mRootNode, aiMatrix4x4(), clip, time, globals);

                        std::vector<LoadedMeshVertex> frame;
                        if (BuildAssimpVertices(meshes, globals, rootInverse, center, modelScale, frame) &&
                            frame.size() == vertices.size())
                        {
                            animation->frames.push_back(std::move(frame));
                        }
                    }

                    if (animation->frames.size() <= 1)
                    {
                        animation->frames.clear();
                        animation->durationSeconds = 0.0f;
                    }
                }
            }
        }

        return true;
    }
}

bool LoadFbxMesh(
    std::filesystem::path const& path,
    std::vector<LoadedMeshVertex>& vertices,
    std::wstring& error,
    std::filesystem::path* diffuseTexturePath,
    LoadedMeshAnimation* animation)
{
    vertices.clear();
    if (diffuseTexturePath)
    {
        diffuseTexturePath->clear();
    }
    if (animation)
    {
        animation->frames.clear();
        animation->boneFrames.clear();
        animation->durationSeconds = 0.0f;
        animation->boneCount = 0;
    }

    std::wstring assimpError;
    if (LoadFbxMeshWithAssimp(path, vertices, assimpError, diffuseTexturePath, animation))
    {
        return true;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        error = L"Could not open FBX file: " + path.wstring();
        return false;
    }

    std::vector<uint8_t> bytes(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());

    if (bytes.size() <= FbxHeaderSize ||
        std::memcmp(bytes.data(), "Kaydara FBX Binary  ", 20) != 0)
    {
        error = L"File is not a binary FBX file.";
        return false;
    }

    uint32_t version{};
    std::memcpy(&version, bytes.data() + FbxVersionOffset, sizeof(version));

    FbxReader reader{ std::move(bytes), version };
    SceneData scene{};
    TraverseScene(reader, FbxHeaderSize, reader.data.size(), scene, error);
    if (!scene.foundMesh)
    {
        if (error.empty()) error = L"Could not find a mesh Geometry node in the FBX file.";
        return false;
    }

    if (diffuseTexturePath)
    {
        *diffuseTexturePath = PathFromFbxString(TexturePathForScene(scene));
    }

    ResolveModelParents(scene);
    const auto clusters = ClustersForMesh(scene);
    const auto influences = BuildInfluences(scene.mesh, clusters);
    if (!BuildRenderableMesh(scene, vertices, error, &influences))
    {
        return false;
    }

    if (animation)
    {
        std::wstring animationError;
        BakeAnimationFrames(scene, *animation, animationError);
    }

    return true;
}
