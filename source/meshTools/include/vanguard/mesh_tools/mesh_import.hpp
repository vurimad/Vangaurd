#pragma once

#include <vanguard/mesh_tools/mesh_tools.hpp>

namespace vanguard::mesh_tools
{
    enum class MeshImportResult : u8
    {
        Success,
        InvalidArgument,
        UnsupportedSource,
        SourceNotFound,
        SourceTooLarge,
        ImportFailure,
        NoGeometry,
        LimitExceeded,
        InvalidGeometry,
        MissingDependency,
        TooManyJoints
    };

    [[nodiscard]] const char* ToString(MeshImportResult result) noexcept;

    enum class SourceUpAxis : u8
    {
        Y,
        Z
    };

    enum class ImportedTextureSemantic : u8
    {
        BaseColor,
        Normal,
        Metalness,
        Roughness,
        Specular,
        Emissive,
        Occlusion,
        Opacity,
        Unknown
    };

    enum class ImportedTextureStorage : u8
    {
        ExternalSource,
        Embedded
    };

    struct MeshImportSettings
    {
        f32 uniformScale = 1.0f;
        SourceUpAxis sourceUpAxis = SourceUpAxis::Z;
        bool generateNormals = true;
        bool generateTangents = true;
        bool flipUVs = false;
        bool flipWinding = true;
        bool strictDependencies = true;
        f32 smoothingAngleDegrees = 80.0f;
        u32 maximumSubmeshes = 65536;
        u32 maximumVerticesPerSubmesh = 16777216;
        u32 maximumIndicesPerSubmesh = 50331648;
        u32 maximumJoints = 255;
        resources::ResourceReference defaultMaterial;
        resources::ResourceReference skeleton;
    };

    struct ImportedTextureReference
    {
        ImportedTextureSemantic semantic = ImportedTextureSemantic::Unknown;
        ImportedTextureStorage storage = ImportedTextureStorage::ExternalSource;
        u32 uvSet = 0;
        u32 embeddedTexture = 0;
        containers::String sourcePath;
    };

    struct ImportedMaterial
    {
        ImportedMaterial() noexcept : textures(memory::pools::Assets::GetInstance()) {}

        containers::String name;
        f32 baseColor[4]{1.0f, 1.0f, 1.0f, 1.0f};
        f32 emissiveColor[4]{};
        f32 specularColor[4]{};
        f32 metallic = 0.0f;
        f32 roughness = 1.0f;
        f32 opacity = 1.0f;
        bool twoSided = false;
        containers::DynamicArray<ImportedTextureReference> textures;
    };

    struct ImportedEmbeddedTexture
    {
        ImportedEmbeddedTexture() noexcept : bytes(memory::pools::Assets::GetInstance()) {}

        containers::String name;
        containers::String formatHint;
        u32 width = 0;
        u32 height = 0;
        bool compressed = true;
        containers::DynamicArray<u8> bytes;
    };

    struct ImportedJoint
    {
        containers::String name;
        // Assimp's inverse-bind matrix in row-major order. Skeleton cooking will apply
        // Vanguard's coordinate conversion when it consumes this table.
        f32 inverseBind[16]{};
    };

    struct ImportedVertexStream
    {
        ImportedVertexStream() noexcept
            : floatValues(memory::pools::Assets::GetInstance()), integerValues(memory::pools::Assets::GetInstance())
        {
        }

        meshes::VertexSemantic semantic = meshes::VertexSemantic::Position;
        u8 semanticIndex = 0;
        meshes::VertexFormat format = meshes::VertexFormat::R32G32B32Float;
        u32 stride = 0;
        containers::DynamicArray<f32> floatValues;
        containers::DynamicArray<u8> integerValues;

        [[nodiscard]] const void* Data() const noexcept;
    };

    struct ImportedSubmesh
    {
        ImportedSubmesh() noexcept
            : vertexStreams(memory::pools::Assets::GetInstance()), indices(memory::pools::Assets::GetInstance()),
              sourceStreams(memory::pools::Assets::GetInstance())
        {
        }

        u64 stableId = 0;
        u64 name = 0;
        u64 materialName = 0;
        containers::String displayName;
        containers::String sourceNodePath;
        resources::ResourceReference material;
        meshes::SubmeshFlags flags = meshes::SubmeshFlags::CastsShadow;
        u32 vertexCount = 0;
        containers::DynamicArray<ImportedVertexStream> vertexStreams;
        containers::DynamicArray<u32> indices;
        containers::DynamicArray<SourceVertexStream> sourceStreams;
    };

    /// Owns importer output. BuildSourceView creates non-owning cooker descriptors that remain
    /// valid until this object or one of its arrays is mutated.
    struct ImportedMesh
    {
        ImportedMesh() noexcept
            : materials(memory::pools::Assets::GetInstance()), embeddedTextures(memory::pools::Assets::GetInstance()),
              dependencies(memory::pools::Assets::GetInstance()), joints(memory::pools::Assets::GetInstance()),
              submeshes(memory::pools::Assets::GetInstance()), sourceSubmeshes(memory::pools::Assets::GetInstance())
        {
        }

        void Clear() noexcept;
        [[nodiscard]] SourceMesh BuildSourceView() noexcept;

        u64 name = 0;
        crypto::Digest256 sourceFingerprint;
        resources::ResourceReference skeleton;
        containers::DynamicArray<ImportedMaterial> materials;
        containers::DynamicArray<ImportedEmbeddedTexture> embeddedTextures;
        containers::DynamicArray<containers::String> dependencies;
        containers::DynamicArray<ImportedJoint> joints;
        containers::DynamicArray<ImportedSubmesh> submeshes;
        containers::DynamicArray<SourceSubmesh> sourceSubmeshes;
    };

    struct MeshImportReport
    {
        containers::String diagnostic;
        u32 submeshCount = 0;
        u32 materialCount = 0;
        u32 vertexCount = 0;
        u32 indexCount = 0;
        u32 externalDependencyCount = 0;
        u32 embeddedTextureCount = 0;
        u32 jointCount = 0;
    };

    /// Imports a physical source file and records every file Assimp opens plus referenced
    /// external textures. Asset registration and package publication remain Phase 3 work.
    [[nodiscard]] MeshImportResult ImportMeshFile(const filesystem::AbsolutePath& source, const MeshImportSettings& settings,
                                                  ImportedMesh& output, MeshImportReport* report = nullptr) noexcept;

    /// Imports a self-contained source payload. formatHint is an extension without a required
    /// leading dot (for example "obj", "fbx", or "gltf"). External dependencies cannot be
    /// resolved from this overload.
    [[nodiscard]] MeshImportResult ImportMeshMemory(const void* sourceData, usize sourceSize, containers::StringView formatHint,
                                                    containers::StringView sourceName, const MeshImportSettings& settings,
                                                    ImportedMesh& output, MeshImportReport* report = nullptr) noexcept;
} // namespace vanguard::mesh_tools
