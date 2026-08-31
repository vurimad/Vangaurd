#include <vanguard/mesh_tools/mesh_import.hpp>

#include <vanguard/serialization/serialization.hpp>

#include "../../../external/assimp/vanguard/assimp_vanguard_io.hpp"

#include <assimp/Importer.hpp>
#include <assimp/config.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
    namespace containers = vanguard::containers;
    namespace crypto = vanguard::crypto;
    namespace filesystem = vanguard::filesystem;
    namespace memory = vanguard::memory;
    namespace meshes = vanguard::meshes;
    namespace serialization = vanguard::serialization;
    namespace tools = vanguard::mesh_tools;
    using vanguard::f32;
    using vanguard::u32;
    using vanguard::u64;
    using vanguard::u8;
    using vanguard::usize;

    constexpr u32 MaximumNodeDepth = 1024;

    struct ImportContext
    {
        const tools::MeshImportSettings& settings;
        tools::ImportedMesh& output;
        tools::MeshImportReport* report = nullptr;
        vanguard::filesystem::AbsolutePath sourceDirectory;
        bool resolveDependencies = false;
    };

    u64 StableHash(const char* const text, const usize length, const u64 seed = 0) noexcept
    {
        u64 result = serialization::Crc64(text, length, seed);
        return result != 0 ? result : 1;
    }

    u64 StableHash(const containers::String& text, const u64 seed = 0) noexcept
    {
        return StableHash(text.AsChar(), text.Length(), seed);
    }

    containers::String SafeName(const aiString& value, const char* const prefix, const u32 index)
    {
        return value.length != 0 ? containers::String(value.C_Str()) : containers::String::Printf("%s_%u", prefix, index);
    }

    void SetDiagnostic(tools::MeshImportReport* const report, const char* const text)
    {
        if (report != nullptr)
        {
            report->diagnostic = text;
        }
    }

    void SetDiagnostic(tools::MeshImportReport* const report, const containers::String& text)
    {
        if (report != nullptr)
        {
            report->diagnostic = text;
        }
    }

    bool AddUniqueDependency(tools::ImportedMesh& output, const char* const path)
    {
        for (const containers::String& existing : output.dependencies)
        {
            if (existing == path)
            {
                return true;
            }
        }
        output.dependencies.PushBack(containers::String(path));
        return true;
    }

    void RecordOpenedFile(const char* const path, void* const userData) noexcept
    {
        AddUniqueDependency(*static_cast<tools::ImportedMesh*>(userData), path);
    }

    bool ValidateSettings(const tools::MeshImportSettings& settings) noexcept
    {
        return std::isfinite(settings.uniformScale) && settings.uniformScale > 0.0f &&
               std::isfinite(settings.smoothingAngleDegrees) && settings.smoothingAngleDegrees > 0.0f &&
               settings.smoothingAngleDegrees <= 175.0f && settings.sourceUpAxis <= tools::SourceUpAxis::Z &&
               settings.maximumSubmeshes != 0 && settings.maximumVerticesPerSubmesh != 0 &&
               settings.maximumIndicesPerSubmesh >= 3 && settings.maximumJoints != 0 && settings.maximumJoints <= 255;
    }

    aiVector3D ConvertPosition(const aiVector3D& value, const tools::MeshImportSettings& settings) noexcept
    {
        const f32 scale = settings.uniformScale;
        return settings.sourceUpAxis == tools::SourceUpAxis::Y ? aiVector3D(value.x * scale, -value.z * scale, value.y * scale)
                                                               : aiVector3D(value.x * scale, value.y * scale, value.z * scale);
    }

    aiVector3D ConvertDirection(const aiVector3D& value, const tools::SourceUpAxis axis) noexcept
    {
        aiVector3D converted = axis == tools::SourceUpAxis::Y ? aiVector3D(value.x, -value.z, value.y) : value;
        const f32 lengthSquared = converted.SquareLength();
        if (lengthSquared > 1.0e-20f)
        {
            converted /= std::sqrt(lengthSquared);
        }
        return converted;
    }

    tools::ImportedVertexStream& AddFloatStream(tools::ImportedSubmesh& submesh, const meshes::VertexSemantic semantic,
                                                 const u8 semanticIndex, const meshes::VertexFormat format, const u32 components)
    {
        tools::ImportedVertexStream& stream = submesh.vertexStreams.EmplaceBack();
        stream.semantic = semantic;
        stream.semanticIndex = semanticIndex;
        stream.format = format;
        stream.stride = components * sizeof(f32);
        stream.floatValues.Reserve(submesh.vertexCount * components);
        return stream;
    }

    void Append3(tools::ImportedVertexStream& stream, const aiVector3D& value)
    {
        stream.floatValues.PushBack(value.x);
        stream.floatValues.PushBack(value.y);
        stream.floatValues.PushBack(value.z);
    }

    void Append4(tools::ImportedVertexStream& stream, const f32 x, const f32 y, const f32 z, const f32 w)
    {
        stream.floatValues.PushBack(x);
        stream.floatValues.PushBack(y);
        stream.floatValues.PushBack(z);
        stream.floatValues.PushBack(w);
    }

    u32 FindJoint(const tools::ImportedMesh& output, const char* const name) noexcept
    {
        for (u32 index = 0; index < output.joints.Size(); ++index)
        {
            if (output.joints[index].name == name)
            {
                return index;
            }
        }
        return ~u32{0};
    }

    void CopyMatrix(const aiMatrix4x4& matrix, f32 (&destination)[16]) noexcept
    {
        destination[0] = matrix.a1;
        destination[1] = matrix.a2;
        destination[2] = matrix.a3;
        destination[3] = matrix.a4;
        destination[4] = matrix.b1;
        destination[5] = matrix.b2;
        destination[6] = matrix.b3;
        destination[7] = matrix.b4;
        destination[8] = matrix.c1;
        destination[9] = matrix.c2;
        destination[10] = matrix.c3;
        destination[11] = matrix.c4;
        destination[12] = matrix.d1;
        destination[13] = matrix.d2;
        destination[14] = matrix.d3;
        destination[15] = matrix.d4;
    }

    tools::MeshImportResult AddSkinning(const aiMesh& source, ImportContext& context, tools::ImportedSubmesh& submesh)
    {
        if (!source.HasBones())
        {
            return tools::MeshImportResult::Success;
        }

        tools::ImportedVertexStream& indices = submesh.vertexStreams.EmplaceBack();
        indices.semantic = meshes::VertexSemantic::JointIndices;
        indices.format = meshes::VertexFormat::R8G8B8A8UInt;
        indices.stride = 4;
        indices.integerValues.Resize(submesh.vertexCount * 4);

        tools::ImportedVertexStream& weights = AddFloatStream(
            submesh, meshes::VertexSemantic::JointWeights, 0, meshes::VertexFormat::R32G32B32A32Float, 4);
        weights.floatValues.Resize(submesh.vertexCount * 4);

        for (u32 boneIndex = 0; boneIndex < source.mNumBones; ++boneIndex)
        {
            const aiBone& bone = *source.mBones[boneIndex];
            u32 jointIndex = FindJoint(context.output, bone.mName.C_Str());
            if (jointIndex == ~u32{0})
            {
                if (context.output.joints.Size() >= context.settings.maximumJoints)
                {
                    return tools::MeshImportResult::TooManyJoints;
                }
                tools::ImportedJoint& joint = context.output.joints.EmplaceBack();
                joint.name = SafeName(bone.mName, "joint", context.output.joints.Size() - 1);
                CopyMatrix(bone.mOffsetMatrix, joint.inverseBind);
                jointIndex = context.output.joints.Size() - 1;
            }

            for (u32 influenceIndex = 0; influenceIndex < bone.mNumWeights; ++influenceIndex)
            {
                const aiVertexWeight& influence = bone.mWeights[influenceIndex];
                if (influence.mVertexId >= submesh.vertexCount || !std::isfinite(influence.mWeight) || influence.mWeight < 0.0f)
                {
                    return tools::MeshImportResult::InvalidGeometry;
                }
                const u32 base = influence.mVertexId * 4;
                for (u32 slot = 0; slot < 4; ++slot)
                {
                    if (influence.mWeight > weights.floatValues[base + slot])
                    {
                        for (u32 move = 3; move > slot; --move)
                        {
                            weights.floatValues[base + move] = weights.floatValues[base + move - 1];
                            indices.integerValues[base + move] = indices.integerValues[base + move - 1];
                        }
                        weights.floatValues[base + slot] = influence.mWeight;
                        indices.integerValues[base + slot] = static_cast<u8>(jointIndex);
                        break;
                    }
                }
            }
        }

        for (u32 vertex = 0; vertex < submesh.vertexCount; ++vertex)
        {
            const u32 base = vertex * 4;
            const f32 total = weights.floatValues[base] + weights.floatValues[base + 1] + weights.floatValues[base + 2] + weights.floatValues[base + 3];
            if (!(total > 0.0f) || !std::isfinite(total))
            {
                return tools::MeshImportResult::InvalidGeometry;
            }
            for (u32 slot = 0; slot < 4; ++slot)
            {
                weights.floatValues[base + slot] /= total;
            }
        }
        return tools::MeshImportResult::Success;
    }

    tools::ImportedTextureSemantic TextureSemantic(const aiTextureType type) noexcept
    {
        switch (type)
        {
        case aiTextureType_BASE_COLOR:
        case aiTextureType_DIFFUSE:
            return tools::ImportedTextureSemantic::BaseColor;
        case aiTextureType_NORMALS:
        case aiTextureType_HEIGHT:
            return tools::ImportedTextureSemantic::Normal;
        case aiTextureType_METALNESS:
            return tools::ImportedTextureSemantic::Metalness;
        case aiTextureType_DIFFUSE_ROUGHNESS:
            return tools::ImportedTextureSemantic::Roughness;
        case aiTextureType_SPECULAR:
            return tools::ImportedTextureSemantic::Specular;
        case aiTextureType_EMISSIVE:
            return tools::ImportedTextureSemantic::Emissive;
        case aiTextureType_LIGHTMAP:
            return tools::ImportedTextureSemantic::Occlusion;
        case aiTextureType_OPACITY:
            return tools::ImportedTextureSemantic::Opacity;
        default:
            return tools::ImportedTextureSemantic::Unknown;
        }
    }

    tools::MeshImportResult AddMaterialTextures(const aiScene& scene, const aiMaterial& source, const aiTextureType type,
                                                ImportContext& context, tools::ImportedMaterial& destination)
    {
        const u32 count = source.GetTextureCount(type);
        for (u32 index = 0; index < count; ++index)
        {
            aiString path;
            u32 uvSet = 0;
            if (source.GetTexture(type, index, &path, nullptr, &uvSet) != AI_SUCCESS)
            {
                return tools::MeshImportResult::ImportFailure;
            }
            tools::ImportedTextureReference& texture = destination.textures.EmplaceBack();
            texture.semantic = TextureSemantic(type);
            texture.uvSet = uvSet;
            texture.sourcePath = path.C_Str();

            const aiTexture* embedded = scene.GetEmbeddedTexture(path.C_Str());
            if (embedded != nullptr)
            {
                texture.storage = tools::ImportedTextureStorage::Embedded;
                for (u32 embeddedIndex = 0; embeddedIndex < scene.mNumTextures; ++embeddedIndex)
                {
                    if (scene.mTextures[embeddedIndex] == embedded)
                    {
                        texture.embeddedTexture = embeddedIndex;
                        break;
                    }
                }
                continue;
            }

            if (context.resolveDependencies)
            {
                containers::String normalized(path.C_Str());
                for (u32 byte = 0; byte < normalized.Length(); ++byte)
                {
                    if (normalized[byte] == '\\')
                    {
                        normalized[byte] = '/';
                    }
                }
                const containers::StringView normalizedView(normalized);
                const filesystem::AbsolutePath resolved = filesystem::paths::IsAbsolutePath(normalizedView)
                                                             ? filesystem::AbsolutePath::ParseFilePath(normalizedView)
                                                             : context.sourceDirectory.AddFilePath(normalizedView);
                const bool exists = filesystem::SystemIO::FileExist(resolved.AsChar());
                if (!exists && context.settings.strictDependencies)
                {
                    SetDiagnostic(context.report, containers::String::Printf("Missing mesh texture dependency '%s'.", resolved.AsChar()));
                    return tools::MeshImportResult::MissingDependency;
                }
                if (exists)
                {
                    AddUniqueDependency(context.output, resolved.AsChar());
                    texture.sourcePath = resolved.AsString();
                }
            }
        }
        return tools::MeshImportResult::Success;
    }

    tools::MeshImportResult ExtractMaterials(const aiScene& scene, ImportContext& context)
    {
        context.output.materials.Reserve(scene.mNumMaterials > 1u ? scene.mNumMaterials : 1u);
        static constexpr aiTextureType TextureTypes[] = {
            aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE, aiTextureType_NORMALS, aiTextureType_HEIGHT,
            aiTextureType_METALNESS, aiTextureType_DIFFUSE_ROUGHNESS, aiTextureType_SPECULAR, aiTextureType_EMISSIVE,
            aiTextureType_LIGHTMAP, aiTextureType_OPACITY};

        for (u32 materialIndex = 0; materialIndex < scene.mNumMaterials; ++materialIndex)
        {
            const aiMaterial& source = *scene.mMaterials[materialIndex];
            tools::ImportedMaterial& material = context.output.materials.EmplaceBack();
            aiString name;
            if (source.Get(AI_MATKEY_NAME, name) == AI_SUCCESS && name.length != 0)
            {
                material.name = name.C_Str();
            }
            else
            {
                material.name = containers::String::Printf("material_%u", materialIndex);
            }

            aiColor4D color{1.0f, 1.0f, 1.0f, 1.0f};
            if (aiGetMaterialColor(&source, AI_MATKEY_BASE_COLOR, &color) != AI_SUCCESS)
            {
                aiGetMaterialColor(&source, AI_MATKEY_COLOR_DIFFUSE, &color);
            }
            material.baseColor[0] = color.r;
            material.baseColor[1] = color.g;
            material.baseColor[2] = color.b;
            material.baseColor[3] = color.a;
            if (aiGetMaterialColor(&source, AI_MATKEY_COLOR_EMISSIVE, &color) == AI_SUCCESS)
            {
                material.emissiveColor[0] = color.r;
                material.emissiveColor[1] = color.g;
                material.emissiveColor[2] = color.b;
                material.emissiveColor[3] = color.a;
            }
            if (aiGetMaterialColor(&source, AI_MATKEY_COLOR_SPECULAR, &color) == AI_SUCCESS)
            {
                material.specularColor[0] = color.r;
                material.specularColor[1] = color.g;
                material.specularColor[2] = color.b;
                material.specularColor[3] = color.a;
            }
            aiGetMaterialFloat(&source, AI_MATKEY_METALLIC_FACTOR, &material.metallic);
            aiGetMaterialFloat(&source, AI_MATKEY_ROUGHNESS_FACTOR, &material.roughness);
            aiGetMaterialFloat(&source, AI_MATKEY_OPACITY, &material.opacity);
            int twoSided = 0;
            unsigned int valueCount = 1;
            if (aiGetMaterialIntegerArray(&source, AI_MATKEY_TWOSIDED, &twoSided, &valueCount) == AI_SUCCESS)
            {
                material.twoSided = twoSided != 0;
            }

            for (const aiTextureType type : TextureTypes)
            {
                if ((type == aiTextureType_DIFFUSE && source.GetTextureCount(aiTextureType_BASE_COLOR) != 0) ||
                    (type == aiTextureType_HEIGHT && source.GetTextureCount(aiTextureType_NORMALS) != 0))
                {
                    continue;
                }
                const tools::MeshImportResult result = AddMaterialTextures(scene, source, type, context, material);
                if (result != tools::MeshImportResult::Success)
                {
                    return result;
                }
            }
        }

        if (context.output.materials.Empty())
        {
            context.output.materials.EmplaceBack().name = "default";
        }
        return tools::MeshImportResult::Success;
    }

    tools::MeshImportResult CopyEmbeddedTextures(const aiScene& scene, tools::ImportedMesh& output)
    {
        output.embeddedTextures.Reserve(scene.mNumTextures);
        for (u32 index = 0; index < scene.mNumTextures; ++index)
        {
            const aiTexture& source = *scene.mTextures[index];
            tools::ImportedEmbeddedTexture& texture = output.embeddedTextures.EmplaceBack();
            texture.name = SafeName(source.mFilename, "embedded", index);
            texture.formatHint = source.achFormatHint;
            texture.width = source.mWidth;
            texture.height = source.mHeight;
            texture.compressed = source.mHeight == 0;
            const u64 byteCount = texture.compressed ? source.mWidth : static_cast<u64>(source.mWidth) * source.mHeight * sizeof(aiTexel);
            if (byteCount > ~u32{0})
            {
                return tools::MeshImportResult::LimitExceeded;
            }
            texture.bytes.Resize(static_cast<u32>(byteCount));
            if (byteCount != 0)
            {
                std::memcpy(texture.bytes.TypedData(), source.pcData, static_cast<usize>(byteCount));
            }
        }
        return tools::MeshImportResult::Success;
    }

    containers::String NodePath(const containers::String& parent, const aiNode& node, const u32 siblingIndex)
    {
        const containers::String name = SafeName(node.mName, "node", siblingIndex);
        return parent.Empty() ? name : containers::String::Printf("%s/%s", parent.AsChar(), name.AsChar());
    }

    tools::MeshImportResult AppendNodeGeometry(const aiScene& scene, const aiNode& node, const aiMatrix4x4& parentTransform,
                                               const containers::String& parentPath, const u32 siblingIndex, const u32 depth,
                                               ImportContext& context)
    {
        if (depth > MaximumNodeDepth)
        {
            return tools::MeshImportResult::LimitExceeded;
        }
        const aiMatrix4x4 transform = parentTransform * node.mTransformation;
        aiMatrix3x3 normalTransform(transform);
        const ai_real determinant = normalTransform.Determinant();
        normalTransform.Inverse().Transpose();
        const containers::String nodePath = NodePath(parentPath, node, siblingIndex);

        for (u32 nodeMeshIndex = 0; nodeMeshIndex < node.mNumMeshes; ++nodeMeshIndex)
        {
            if (context.output.submeshes.Size() >= context.settings.maximumSubmeshes)
            {
                return tools::MeshImportResult::LimitExceeded;
            }
            const u32 sceneMeshIndex = node.mMeshes[nodeMeshIndex];
            if (sceneMeshIndex >= scene.mNumMeshes)
            {
                return tools::MeshImportResult::InvalidGeometry;
            }
            const aiMesh& source = *scene.mMeshes[sceneMeshIndex];
            if (source.mNumVertices == 0 || source.mNumFaces == 0)
            {
                continue;
            }
            const u64 indexCount = static_cast<u64>(source.mNumFaces) * 3;
            if (source.mNumVertices > context.settings.maximumVerticesPerSubmesh || indexCount > context.settings.maximumIndicesPerSubmesh)
            {
                return tools::MeshImportResult::LimitExceeded;
            }

            tools::ImportedSubmesh& submesh = context.output.submeshes.EmplaceBack();
            submesh.vertexCount = source.mNumVertices;
            submesh.displayName = SafeName(source.mName, "primitive", sceneMeshIndex);
            submesh.sourceNodePath = nodePath;
            submesh.name = StableHash(submesh.displayName);
            submesh.stableId = StableHash(nodePath, submesh.name ^ sceneMeshIndex);
            const u32 materialIndex = source.mMaterialIndex < context.output.materials.Size() ? source.mMaterialIndex : 0;
            submesh.materialName = StableHash(context.output.materials[materialIndex].name);
            submesh.material = context.settings.defaultMaterial;
            if (context.output.materials[materialIndex].twoSided)
            {
                submesh.flags = submesh.flags | meshes::SubmeshFlags::TwoSided;
            }

            u32 streamCapacity = 3 + (source.HasBones() ? 2u : 0u);
            for (u32 channel = 0; channel < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++channel)
            {
                streamCapacity += source.HasTextureCoords(channel) ? 1u : 0u;
            }
            for (u32 channel = 0; channel < AI_MAX_NUMBER_OF_COLOR_SETS; ++channel)
            {
                streamCapacity += source.HasVertexColors(channel) ? 1u : 0u;
            }
            submesh.vertexStreams.Reserve(streamCapacity);

            tools::ImportedVertexStream& positions = AddFloatStream(
                submesh, meshes::VertexSemantic::Position, 0, meshes::VertexFormat::R32G32B32Float, 3);
            tools::ImportedVertexStream* normals = source.HasNormals()
                                                       ? &AddFloatStream(submesh, meshes::VertexSemantic::Normal, 0,
                                                                         meshes::VertexFormat::R32G32B32Float, 3)
                                                       : nullptr;
            tools::ImportedVertexStream* tangents = source.HasTangentsAndBitangents()
                                                        ? &AddFloatStream(submesh, meshes::VertexSemantic::Tangent, 0,
                                                                          meshes::VertexFormat::R32G32B32A32Float, 4)
                                                        : nullptr;

            tools::ImportedVertexStream* texCoords[AI_MAX_NUMBER_OF_TEXTURECOORDS]{};
            for (u32 channel = 0; channel < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++channel)
            {
                if (source.HasTextureCoords(channel))
                {
                    texCoords[channel] = &AddFloatStream(submesh, meshes::VertexSemantic::TexCoord, static_cast<u8>(channel),
                                                         meshes::VertexFormat::R32G32Float, 2);
                }
            }
            tools::ImportedVertexStream* colors[AI_MAX_NUMBER_OF_COLOR_SETS]{};
            for (u32 channel = 0; channel < AI_MAX_NUMBER_OF_COLOR_SETS; ++channel)
            {
                if (source.HasVertexColors(channel))
                {
                    colors[channel] = &AddFloatStream(submesh, meshes::VertexSemantic::Color, static_cast<u8>(channel),
                                                      meshes::VertexFormat::R32G32B32A32Float, 4);
                }
            }

            for (u32 vertexIndex = 0; vertexIndex < source.mNumVertices; ++vertexIndex)
            {
                Append3(positions, ConvertPosition(transform * source.mVertices[vertexIndex], context.settings));
                aiVector3D normal;
                if (normals != nullptr)
                {
                    normal = ConvertDirection(normalTransform * source.mNormals[vertexIndex], context.settings.sourceUpAxis);
                    Append3(*normals, normal);
                }
                if (tangents != nullptr)
                {
                    aiVector3D tangent = ConvertDirection(normalTransform * source.mTangents[vertexIndex], context.settings.sourceUpAxis);
                    tangent -= normal * (normal * tangent);
                    if (tangent.SquareLength() > 1.0e-20f)
                    {
                        tangent.Normalize();
                    }
                    const aiVector3D bitangent = ConvertDirection(normalTransform * source.mBitangents[vertexIndex], context.settings.sourceUpAxis);
                    const f32 handedness = (normal ^ tangent) * bitangent < 0.0f ? -1.0f : 1.0f;
                    Append4(*tangents, tangent.x, tangent.y, tangent.z, handedness);
                }
                for (u32 channel = 0; channel < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++channel)
                {
                    if (texCoords[channel] != nullptr)
                    {
                        texCoords[channel]->floatValues.PushBack(source.mTextureCoords[channel][vertexIndex].x);
                        texCoords[channel]->floatValues.PushBack(source.mTextureCoords[channel][vertexIndex].y);
                    }
                }
                for (u32 channel = 0; channel < AI_MAX_NUMBER_OF_COLOR_SETS; ++channel)
                {
                    if (colors[channel] != nullptr)
                    {
                        const aiColor4D& color = source.mColors[channel][vertexIndex];
                        Append4(*colors[channel], color.r, color.g, color.b, color.a);
                    }
                }
            }

            const tools::MeshImportResult skinResult = AddSkinning(source, context, submesh);
            if (skinResult != tools::MeshImportResult::Success)
            {
                return skinResult;
            }

            const bool reverseWinding = context.settings.flipWinding != (determinant < 0.0f);
            submesh.indices.Reserve(static_cast<u32>(indexCount));
            for (u32 faceIndex = 0; faceIndex < source.mNumFaces; ++faceIndex)
            {
                const aiFace& face = source.mFaces[faceIndex];
                if (face.mNumIndices != 3 || face.mIndices[0] >= source.mNumVertices || face.mIndices[1] >= source.mNumVertices ||
                    face.mIndices[2] >= source.mNumVertices)
                {
                    return tools::MeshImportResult::InvalidGeometry;
                }
                submesh.indices.PushBack(reverseWinding ? face.mIndices[2] : face.mIndices[0]);
                submesh.indices.PushBack(face.mIndices[1]);
                submesh.indices.PushBack(reverseWinding ? face.mIndices[0] : face.mIndices[2]);
            }
        }

        for (u32 childIndex = 0; childIndex < node.mNumChildren; ++childIndex)
        {
            const tools::MeshImportResult result = AppendNodeGeometry(
                scene, *node.mChildren[childIndex], transform, nodePath, childIndex, depth + 1, context);
            if (result != tools::MeshImportResult::Success)
            {
                return result;
            }
        }
        return tools::MeshImportResult::Success;
    }

    tools::MeshImportResult ConvertScene(const aiScene& scene, const containers::StringView sourceName, ImportContext& context)
    {
        if (scene.mRootNode == nullptr || scene.mNumMeshes == 0)
        {
            return tools::MeshImportResult::NoGeometry;
        }
        const tools::MeshImportResult embeddedResult = CopyEmbeddedTextures(scene, context.output);
        if (embeddedResult != tools::MeshImportResult::Success)
        {
            return embeddedResult;
        }
        const tools::MeshImportResult materialResult = ExtractMaterials(scene, context);
        if (materialResult != tools::MeshImportResult::Success)
        {
            return materialResult;
        }

        aiMatrix4x4 identity;
        const tools::MeshImportResult geometryResult = AppendNodeGeometry(scene, *scene.mRootNode, identity, {}, 0, 0, context);
        if (geometryResult != tools::MeshImportResult::Success)
        {
            return geometryResult;
        }
        if (context.output.submeshes.Empty())
        {
            return tools::MeshImportResult::NoGeometry;
        }
        context.output.name = StableHash(sourceName.Data(), sourceName.Length());
        context.output.skeleton = context.settings.skeleton;
        return tools::MeshImportResult::Success;
    }

    unsigned int ImportFlags(const tools::MeshImportSettings& settings) noexcept
    {
        unsigned int flags = static_cast<unsigned int>(aiProcess_Triangulate | aiProcess_JoinIdenticalVertices |
                                                       aiProcess_ValidateDataStructure | aiProcess_FindInvalidData |
                                                       aiProcess_FindDegenerates | aiProcess_SortByPType | aiProcess_GenBoundingBoxes);
        if (settings.generateNormals)
        {
            flags |= aiProcess_GenSmoothNormals;
        }
        if (settings.generateTangents)
        {
            flags |= aiProcess_CalcTangentSpace;
        }
        if (settings.flipUVs)
        {
            flags |= aiProcess_FlipUVs;
        }
        return flags;
    }

    void ConfigureImporter(Assimp::Importer& importer, const tools::MeshImportSettings& settings)
    {
        importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_POINT | aiPrimitiveType_LINE);
        importer.SetPropertyFloat(AI_CONFIG_PP_GSN_MAX_SMOOTHING_ANGLE, settings.smoothingAngleDegrees);
        importer.SetPropertyInteger(AI_CONFIG_PP_LBW_MAX_WEIGHTS, 4);
    }

    void FinishReport(const tools::ImportedMesh& output, tools::MeshImportReport* const report)
    {
        if (report == nullptr)
        {
            return;
        }
        report->submeshCount = output.submeshes.Size();
        report->materialCount = output.materials.Size();
        report->externalDependencyCount = output.dependencies.Size();
        report->embeddedTextureCount = output.embeddedTextures.Size();
        report->jointCount = output.joints.Size();
        for (const tools::ImportedSubmesh& submesh : output.submeshes)
        {
            report->vertexCount += submesh.vertexCount;
            report->indexCount += submesh.indices.Size();
        }
    }

    bool HashFile(const char* const path, crypto::Digest256& digest) noexcept
    {
        std::FILE* file = nullptr;
        if (fopen_s(&file, path, "rb") != 0 || file == nullptr)
        {
            return false;
        }
        crypto::Sha256Builder hash;
        u8 bytes[64 * 1024];
        bool success = true;
        for (;;)
        {
            const usize read = std::fread(bytes, 1, sizeof(bytes), file);
            if (read != 0 && !hash.Update(bytes, read))
            {
                success = false;
                break;
            }
            if (read != sizeof(bytes))
            {
                success = std::feof(file) != 0 && std::ferror(file) == 0;
                break;
            }
        }
        std::fclose(file);
        return success && hash.Finalize(digest);
    }
} // namespace

namespace vanguard::mesh_tools
{
    const char* ToString(const MeshImportResult result) noexcept
    {
        switch (result)
        {
        case MeshImportResult::Success:
            return "Success";
        case MeshImportResult::InvalidArgument:
            return "InvalidArgument";
        case MeshImportResult::UnsupportedSource:
            return "UnsupportedSource";
        case MeshImportResult::SourceNotFound:
            return "SourceNotFound";
        case MeshImportResult::SourceTooLarge:
            return "SourceTooLarge";
        case MeshImportResult::ImportFailure:
            return "ImportFailure";
        case MeshImportResult::NoGeometry:
            return "NoGeometry";
        case MeshImportResult::LimitExceeded:
            return "LimitExceeded";
        case MeshImportResult::InvalidGeometry:
            return "InvalidGeometry";
        case MeshImportResult::MissingDependency:
            return "MissingDependency";
        case MeshImportResult::TooManyJoints:
            return "TooManyJoints";
        }
        return "Unknown";
    }

    const void* ImportedVertexStream::Data() const noexcept
    {
        return !floatValues.Empty() ? static_cast<const void*>(floatValues.TypedData()) : static_cast<const void*>(integerValues.TypedData());
    }

    void ImportedMesh::Clear() noexcept
    {
        name = 0;
        sourceFingerprint = {};
        skeleton = {};
        materials.Clear();
        embeddedTextures.Clear();
        dependencies.Clear();
        joints.Clear();
        submeshes.Clear();
        sourceSubmeshes.Clear();
    }

    SourceMesh ImportedMesh::BuildSourceView() noexcept
    {
        sourceSubmeshes.Resize(submeshes.Size());
        for (u32 submeshIndex = 0; submeshIndex < submeshes.Size(); ++submeshIndex)
        {
            ImportedSubmesh& imported = submeshes[submeshIndex];
            imported.sourceStreams.Resize(imported.vertexStreams.Size());
            for (u32 streamIndex = 0; streamIndex < imported.vertexStreams.Size(); ++streamIndex)
            {
                const ImportedVertexStream& stream = imported.vertexStreams[streamIndex];
                imported.sourceStreams[streamIndex] = {
                    stream.semantic, stream.semanticIndex, stream.format, stream.Data(), imported.vertexCount, stream.stride};
            }
            sourceSubmeshes[submeshIndex] = {
                imported.stableId,
                imported.name,
                imported.materialName,
                imported.material,
                imported.flags,
                {imported.sourceStreams.TypedData(), imported.sourceStreams.Size()},
                {imported.indices.TypedData(), imported.indices.Size()}};
        }
        return {name, sourceFingerprint, skeleton, {sourceSubmeshes.TypedData(), sourceSubmeshes.Size()}};
    }

    MeshImportResult ImportMeshFile(const filesystem::AbsolutePath& source, const MeshImportSettings& settings, ImportedMesh& output,
                                    MeshImportReport* const report) noexcept
    {
        output.Clear();
        if (report != nullptr)
        {
            *report = {};
        }
        if (!ValidateSettings(settings) || source.AsChar() == nullptr || source.AsChar()[0] == '\0')
        {
            return MeshImportResult::InvalidArgument;
        }
        if (!filesystem::SystemIO::FileExist(source.AsChar()))
        {
            SetDiagnostic(report, "Mesh source file does not exist.");
            return MeshImportResult::SourceNotFound;
        }

        const filesystem::AbsolutePath sourceDirectory = filesystem::paths::ParentAbsolutePath(source);
        ImportContext context{settings, output, report, sourceDirectory, true};
        Assimp::Importer importer;
        importer.SetIOHandler(vanguard_assimp::CreateTrackingIOSystem(sourceDirectory.AsChar(), RecordOpenedFile, &output));
        ConfigureImporter(importer, settings);
        const aiScene* scene = importer.ReadFile(source.AsChar(), ImportFlags(settings));
        if (scene == nullptr)
        {
            SetDiagnostic(report, containers::String::Printf("Assimp failed to import '%s': %s", source.AsChar(), importer.GetErrorString()));
            return MeshImportResult::ImportFailure;
        }
        if (!HashFile(source.AsChar(), output.sourceFingerprint))
        {
            SetDiagnostic(report, "Failed to fingerprint mesh source file.");
            return MeshImportResult::ImportFailure;
        }

        const MeshImportResult result = ConvertScene(*scene, filesystem::paths::GetFileName(source), context);
        if (result != MeshImportResult::Success)
        {
            if (report != nullptr && report->diagnostic.Empty())
            {
                report->diagnostic = ToString(result);
            }
            output.Clear();
            return result;
        }
        FinishReport(output, report);
        return MeshImportResult::Success;
    }

    MeshImportResult ImportMeshMemory(const void* const sourceData, const usize sourceSize, const containers::StringView formatHint,
                                      const containers::StringView sourceName, const MeshImportSettings& settings, ImportedMesh& output,
                                      MeshImportReport* const report) noexcept
    {
        output.Clear();
        if (report != nullptr)
        {
            *report = {};
        }
        if (!ValidateSettings(settings) || sourceData == nullptr || sourceSize == 0 || formatHint.Empty() || sourceName.Empty() ||
            sourceSize > static_cast<usize>(~u32{0}))
        {
            return MeshImportResult::InvalidArgument;
        }

        const containers::String hint(formatHint.Data(), static_cast<u32>(formatHint.Length()));
        Assimp::Importer importer;
        ConfigureImporter(importer, settings);
        const aiScene* scene = importer.ReadFileFromMemory(sourceData, sourceSize, ImportFlags(settings), hint.AsChar());
        if (scene == nullptr)
        {
            const containers::String name(sourceName.Data(), static_cast<u32>(sourceName.Length()));
            SetDiagnostic(report, containers::String::Printf("Assimp failed to import '%s': %s", name.AsChar(), importer.GetErrorString()));
            return MeshImportResult::ImportFailure;
        }

        output.sourceFingerprint = crypto::Sha256(sourceData, sourceSize);
        ImportContext context{settings, output, report, {}, false};
        const MeshImportResult result = ConvertScene(*scene, sourceName, context);
        if (result != MeshImportResult::Success)
        {
            if (report != nullptr && report->diagnostic.Empty())
            {
                report->diagnostic = ToString(result);
            }
            output.Clear();
            return result;
        }
        FinishReport(output, report);
        return MeshImportResult::Success;
    }
} // namespace vanguard::mesh_tools
