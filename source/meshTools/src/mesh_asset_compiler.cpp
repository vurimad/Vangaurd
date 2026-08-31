#include <vanguard/mesh_tools/mesh_asset_compiler.hpp>

#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/memory/pool.hpp>

#include <cmath>
#include <cstring>

namespace
{
    using namespace vanguard;
    namespace assets = vanguard::assets;
    namespace tools = vanguard::mesh_tools;

    constexpr u32 SettingsMagic = vanguard::serialization::MakeFourCC('V', 'M', 'C', 'B');
    constexpr u16 SettingsVersion = 1;
    constexpr u32 MaximumSettingsBytes = 1024u * 1024u;
    constexpr u32 MaximumPathBytes = 1024;
    constexpr u32 MaximumFormatBytes = 16;
    constexpr char CompilerName[] = "vanguard.mesh.compiler";
    constexpr char CompilerToolPath[] = "tools/vanguard-mesh-compiler";
    constexpr char CompilerPolicy[] = "VanguardMeshCompiler;Assimp6;VMesh1;FixedFunctionVertexInput";

    [[nodiscard]] u32 StringLength(const char* value, const u32 maximum) noexcept
    {
        if (value == nullptr)
            return maximum;
        u32 length = 0;
        while (length < maximum && value[length] != '\0')
            ++length;
        return length;
    }

    void AppendU8(containers::DynamicArray<u8>& output, const u8 value)
    {
        output.PushBack(value);
    }
    void AppendU16(containers::DynamicArray<u8>& output, const u16 value)
    {
        AppendU8(output, static_cast<u8>(value));
        AppendU8(output, static_cast<u8>(value >> 8u));
    }
    void AppendU32(containers::DynamicArray<u8>& output, const u32 value)
    {
        AppendU16(output, static_cast<u16>(value));
        AppendU16(output, static_cast<u16>(value >> 16u));
    }
    void AppendU64(containers::DynamicArray<u8>& output, const u64 value)
    {
        AppendU32(output, static_cast<u32>(value));
        AppendU32(output, static_cast<u32>(value >> 32u));
    }
    void AppendF32(containers::DynamicArray<u8>& output, const f32 value)
    {
        u32 bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        AppendU32(output, bits);
    }
    void AppendString(containers::DynamicArray<u8>& output, const char* value, const u16 size)
    {
        AppendU16(output, size);
        for (u16 index = 0; index < size; ++index)
            AppendU8(output, static_cast<u8>(value[index]));
    }
    void AppendReference(containers::DynamicArray<u8>& output, const resources::ResourceReference value)
    {
        AppendU64(output, value.GetPath().Id());
        AppendU32(output, value.ExpectedType());
    }

    class SettingsReader final
    {
    public:
        explicit SettingsReader(const containers::ArraySpan<const u8> bytes) noexcept : m_bytes(bytes) {}
        [[nodiscard]] bool U8(u8& value) noexcept
        {
            if (m_offset >= m_bytes.Size())
                return false;
            value = m_bytes[m_offset++];
            return true;
        }
        [[nodiscard]] bool U16(u16& value) noexcept
        {
            u8 low = 0, high = 0;
            if (!U8(low) || !U8(high))
                return false;
            value = static_cast<u16>(low | static_cast<u16>(high) << 8u);
            return true;
        }
        [[nodiscard]] bool U32(u32& value) noexcept
        {
            u16 low = 0, high = 0;
            if (!U16(low) || !U16(high))
                return false;
            value = static_cast<u32>(low) | static_cast<u32>(high) << 16u;
            return true;
        }
        [[nodiscard]] bool U64(u64& value) noexcept
        {
            u32 low = 0, high = 0;
            if (!U32(low) || !U32(high))
                return false;
            value = static_cast<u64>(low) | static_cast<u64>(high) << 32u;
            return true;
        }
        [[nodiscard]] bool F32(f32& value) noexcept
        {
            u32 bits = 0;
            if (!U32(bits))
                return false;
            std::memcpy(&value, &bits, sizeof(value));
            return std::isfinite(value);
        }
        [[nodiscard]] bool String(containers::DynamicArray<char>& storage, const u32 maximum, const char*& value) noexcept
        {
            u16 size = 0;
            if (!U16(size) || size == 0 || size >= maximum)
                return false;
            const u32 offset = storage.Size();
            storage.Resize(offset + size + 1u);
            for (u16 index = 0; index < size; ++index)
            {
                u8 byte = 0;
                if (!U8(byte) || byte == 0)
                    return false;
                storage[offset + index] = static_cast<char>(byte);
            }
            storage[offset + size] = '\0';
            value = storage.TypedData() + offset;
            return true;
        }
        [[nodiscard]] bool Reference(resources::ResourceReference& value) noexcept
        {
            u64 path = 0;
            u32 type = 0;
            if (!U64(path) || !U32(type))
                return false;
            value = resources::ResourceReference(resources::ResourcePath::FromId(path), type);
            return (path == resources::InvalidResourceId && type == resources::InvalidResourceTypeId) || (value.IsValid() && value.IsTyped());
        }
        [[nodiscard]] bool Finished() const noexcept
        {
            return m_offset == m_bytes.Size();
        }

    private:
        containers::ArraySpan<const u8> m_bytes;
        u32 m_offset = 0;
    };

    struct ParsedSettings final
    {
        ParsedSettings() noexcept : characters(memory::pools::Tools::GetInstance()), lods(memory::pools::Tools::GetInstance()) {}
        containers::DynamicArray<char> characters;
        containers::DynamicArray<tools::LodLevelSettings> lods;
        const char* sourceName = nullptr;
        const char* formatHint = nullptr;
        tools::MeshImportSettings import;
        tools::CookSettings cook;
    };

    [[nodiscard]] bool ParseSettings(const containers::ArraySpan<const u8> bytes, ParsedSettings& output) noexcept
    {
        if (bytes.Empty() || bytes.Size() > MaximumSettingsBytes)
            return false;
        output.characters.Reserve(bytes.Size());
        if (output.characters.Capacity() < bytes.Size())
            return false;
        SettingsReader reader(bytes);
        u32 magic = 0, lodCount = 0;
        u16 version = 0, reserved16 = 0;
        u8 importFlags = 0, cookFlags = 0, axis = 0, reserved8 = 0;
        if (!reader.U32(magic) || !reader.U16(version) || !reader.U16(reserved16) || magic != SettingsMagic || version != SettingsVersion || reserved16 != 0 || !reader.U8(importFlags) ||
            !reader.U8(cookFlags) || !reader.U8(axis) || !reader.U8(reserved8) || reserved8 != 0 || (importFlags & ~0x1fu) != 0 || (cookFlags & ~0x1fu) != 0 ||
            axis > static_cast<u8>(tools::SourceUpAxis::Z) || !reader.F32(output.import.uniformScale) || !reader.F32(output.import.smoothingAngleDegrees) ||
            !reader.U32(output.import.maximumSubmeshes) || !reader.U32(output.import.maximumVerticesPerSubmesh) || !reader.U32(output.import.maximumIndicesPerSubmesh) ||
            !reader.U32(output.import.maximumJoints) || !reader.Reference(output.import.defaultMaterial) || !reader.Reference(output.import.skeleton) ||
            !reader.U64(output.cook.meshCookingProfile) || !reader.F32(output.cook.overdrawThreshold) || !reader.U32(output.cook.maximumSubmeshes) ||
            !reader.U32(output.cook.maximumVertexStreamsPerSubmesh) || !reader.U32(output.cook.maximumVerticesPerSubmesh) || !reader.U32(output.cook.maximumIndicesPerSubmesh) ||
            !reader.U32(output.cook.maximumLodLevels) || !reader.F32(output.cook.lodAttributeWeights.normal) || !reader.F32(output.cook.lodAttributeWeights.tangent) ||
            !reader.F32(output.cook.lodAttributeWeights.texCoord) || !reader.F32(output.cook.lodAttributeWeights.color) || !reader.F32(output.cook.lodAttributeWeights.jointWeights) ||
            !reader.F32(output.cook.lodAttributeWeights.morphPosition) || !reader.U32(lodCount) || lodCount > output.cook.maximumLodLevels ||
            !reader.String(output.characters, MaximumPathBytes, output.sourceName) || !reader.String(output.characters, MaximumFormatBytes, output.formatHint))
            return false;
        output.import.generateNormals = (importFlags & 1u) != 0;
        output.import.generateTangents = (importFlags & 2u) != 0;
        output.import.flipUVs = (importFlags & 4u) != 0;
        output.import.flipWinding = (importFlags & 8u) != 0;
        output.import.strictDependencies = (importFlags & 16u) != 0;
        output.import.sourceUpAxis = static_cast<tools::SourceUpAxis>(axis);
        output.cook.optimizeVertexCache = (cookFlags & 1u) != 0;
        output.cook.optimizeOverdraw = (cookFlags & 2u) != 0;
        output.cook.optimizeVertexFetch = (cookFlags & 4u) != 0;
        output.cook.lockLodBorders = (cookFlags & 8u) != 0;
        output.cook.regularizeLodTriangles = (cookFlags & 16u) != 0;
        output.lods.Resize(lodCount);
        for (u32 index = 0; index < lodCount; ++index)
            if (!reader.F32(output.lods[index].triangleRatio) || !reader.F32(output.lods[index].maximumNormalizedError) || !reader.F32(output.lods[index].minimumScreenCoverage))
                return false;
        output.cook.lodLevels = {output.lods.TypedData(), output.lods.Size()};
        return reader.Finished() && resources::ResourcePath::FromString(output.sourceName).IsValid() && tools::FindMeshCookingProfile(output.cook.meshCookingProfile) != nullptr;
    }

    [[nodiscard]] crypto::Digest256 ToolFingerprint(const tools::MeshCookingProfile& profile) noexcept
    {
        crypto::Sha256Builder builder;
        static_cast<void>(builder.Update(CompilerPolicy, sizeof(CompilerPolicy) - 1u));
        static_cast<void>(builder.Update(&profile.id, sizeof(profile.id)));
        static_cast<void>(builder.Update(&profile.version, sizeof(profile.version)));
        crypto::Digest256 result;
        static_cast<void>(builder.Finalize(result));
        return result;
    }

    [[nodiscard]] bool ResolveAndImport(const assets::BuildRequest& request, const ParsedSettings& settings, const tools::MeshAssetCompilerConfig& config, tools::ImportedMesh& mesh,
                                        tools::MeshImportReport& report) noexcept
    {
        filesystem::AbsolutePath sourceFile;
        if (!config.resolveSourceFile(request.source.identity, sourceFile, config.resolveSourceFileUserData))
            return false;
        const tools::MeshImportResult result = tools::ImportMeshFile(sourceFile, settings.import, mesh, &report);
        return result == tools::MeshImportResult::Success && mesh.sourceFingerprint == crypto::Sha256(request.source.content.Data(), request.source.content.SizeInBytes());
    }

    [[nodiscard]] bool DiscoverImportedDependencies(const assets::BuildRequest& request, const tools::ImportedMesh& mesh, const tools::MeshAssetCompilerConfig& config,
                                                    assets::DependencyCollector& dependencies) noexcept
    {
        u32 added = 0;
        for (const containers::String& path : mesh.dependencies)
        {
            if (config.resolveDependency == nullptr)
                return false;
            tools::MeshDependencySource dependency;
            if (!config.resolveDependency(path.AsChar(), dependency, config.resolveDependencyUserData) || !dependency.identity.IsValid() || !dependency.identity.IsTyped() ||
                dependency.content.IsEmpty())
                return false;
            if (dependency.identity == request.source.identity)
                continue;
            if (++added > config.maximumDependencies ||
                dependencies.Add({dependency.identity, dependency.content, assets::DependencyRole::Source, assets::DependencyRequirement::Required}) != assets::Result::Success)
                return false;
        }
        return true;
    }

    [[nodiscard]] bool VerifyImportedDependencies(const assets::BuildRequest& request, const tools::ImportedMesh& mesh, const tools::MeshAssetCompilerConfig& config,
                                                  const containers::ArraySpan<const assets::BuildDependency> planned) noexcept
    {
        u32 resolvedCount = 0;
        for (const containers::String& path : mesh.dependencies)
        {
            if (config.resolveDependency == nullptr)
                return false;
            tools::MeshDependencySource dependency;
            if (!config.resolveDependency(path.AsChar(), dependency, config.resolveDependencyUserData))
                return false;
            if (dependency.identity == request.source.identity)
                continue;
            bool found = false;
            for (const assets::BuildDependency& expected : planned)
                if (expected.role == assets::DependencyRole::Source && expected.identity == dependency.identity && expected.content == dependency.content)
                    found = true;
            if (!found || ++resolvedCount > config.maximumDependencies)
                return false;
        }
        u32 plannedCount = 0;
        for (const assets::BuildDependency& dependency : planned)
            if (dependency.role == assets::DependencyRole::Source)
                ++plannedCount;
        return resolvedCount == plannedCount;
    }
} // namespace

namespace vanguard::mesh_tools
{
    struct MeshAssetCompiler::Impl final
    {
        MeshAssetCompilerConfig config;
        assets::BuildSystem* buildSystem = nullptr;
        assets::CompilerId compilerId = assets::InvalidCompilerId;
    };

    MeshBuildSettingsResult EncodeMeshBuildSettings(const MeshBuildDescription& description, containers::DynamicArray<u8>& output) noexcept
    {
        output.Clear();
        const u32 sourceLength = StringLength(description.sourceName, MaximumPathBytes);
        const u32 formatLength = StringLength(description.formatHint, MaximumFormatBytes);
        if (sourceLength == 0 || sourceLength >= MaximumPathBytes || formatLength == 0 || formatLength >= MaximumFormatBytes ||
            !resources::ResourcePath::FromString(description.sourceName).IsValid() || description.import.sourceUpAxis > SourceUpAxis::Z || !std::isfinite(description.import.uniformScale) ||
            description.import.uniformScale <= 0.0f || !std::isfinite(description.import.smoothingAngleDegrees) || description.import.smoothingAngleDegrees <= 0.0f ||
            description.import.smoothingAngleDegrees > 175.0f || description.import.maximumSubmeshes == 0 || description.import.maximumVerticesPerSubmesh == 0 ||
            description.import.maximumIndicesPerSubmesh < 3 || description.import.maximumJoints == 0 || description.import.maximumJoints > 255 || description.cook.maximumLodLevels == 0 ||
            description.cook.lodLevels.Size() > description.cook.maximumLodLevels || FindMeshCookingProfile(description.cook.meshCookingProfile) == nullptr)
            return MeshBuildSettingsResult::InvalidArgument;
        const u64 required = 128ull + sourceLength + formatLength + static_cast<u64>(description.cook.lodLevels.Size()) * 12ull;
        if (required > MaximumSettingsBytes)
            return MeshBuildSettingsResult::LimitExceeded;
        output.Reserve(static_cast<u32>(required));
        if (output.Capacity() < required)
            return MeshBuildSettingsResult::LimitExceeded;
        AppendU32(output, SettingsMagic);
        AppendU16(output, SettingsVersion);
        AppendU16(output, 0);
        AppendU8(output, static_cast<u8>((description.import.generateNormals ? 1u : 0u) | (description.import.generateTangents ? 2u : 0u) | (description.import.flipUVs ? 4u : 0u) |
                                         (description.import.flipWinding ? 8u : 0u) | (description.import.strictDependencies ? 16u : 0u)));
        AppendU8(output, static_cast<u8>((description.cook.optimizeVertexCache ? 1u : 0u) | (description.cook.optimizeOverdraw ? 2u : 0u) | (description.cook.optimizeVertexFetch ? 4u : 0u) |
                                         (description.cook.lockLodBorders ? 8u : 0u) | (description.cook.regularizeLodTriangles ? 16u : 0u)));
        AppendU8(output, static_cast<u8>(description.import.sourceUpAxis));
        AppendU8(output, 0);
        AppendF32(output, description.import.uniformScale);
        AppendF32(output, description.import.smoothingAngleDegrees);
        AppendU32(output, description.import.maximumSubmeshes);
        AppendU32(output, description.import.maximumVerticesPerSubmesh);
        AppendU32(output, description.import.maximumIndicesPerSubmesh);
        AppendU32(output, description.import.maximumJoints);
        AppendReference(output, description.import.defaultMaterial);
        AppendReference(output, description.import.skeleton);
        AppendU64(output, description.cook.meshCookingProfile);
        AppendF32(output, description.cook.overdrawThreshold);
        AppendU32(output, description.cook.maximumSubmeshes);
        AppendU32(output, description.cook.maximumVertexStreamsPerSubmesh);
        AppendU32(output, description.cook.maximumVerticesPerSubmesh);
        AppendU32(output, description.cook.maximumIndicesPerSubmesh);
        AppendU32(output, description.cook.maximumLodLevels);
        AppendF32(output, description.cook.lodAttributeWeights.normal);
        AppendF32(output, description.cook.lodAttributeWeights.tangent);
        AppendF32(output, description.cook.lodAttributeWeights.texCoord);
        AppendF32(output, description.cook.lodAttributeWeights.color);
        AppendF32(output, description.cook.lodAttributeWeights.jointWeights);
        AppendF32(output, description.cook.lodAttributeWeights.morphPosition);
        AppendU32(output, description.cook.lodLevels.Size());
        AppendString(output, description.sourceName, static_cast<u16>(sourceLength));
        AppendString(output, description.formatHint, static_cast<u16>(formatLength));
        for (const LodLevelSettings& lod : description.cook.lodLevels)
        {
            AppendF32(output, lod.triangleRatio);
            AppendF32(output, lod.maximumNormalizedError);
            AppendF32(output, lod.minimumScreenCoverage);
        }
        return MeshBuildSettingsResult::Success;
    }

    namespace
    {
        [[nodiscard]] bool Discover(const assets::BuildRequest& request, assets::DependencyCollector& dependencies, void* userData) noexcept
        {
            MeshAssetCompiler::Impl& implementation = *static_cast<MeshAssetCompiler::Impl*>(userData);
            ParsedSettings settings;
            if (!ParseSettings(request.settings, settings) || request.source.identity.GetPath() != resources::ResourcePath::FromString(settings.sourceName))
                return false;
            const MeshCookingProfile* profile = FindMeshCookingProfile(settings.cook.meshCookingProfile);
            const resources::ResourceReference tool(resources::ResourcePath::FromString(CompilerToolPath), MeshCompilerToolResourceType);
            if (profile == nullptr || dependencies.Add({tool, ToolFingerprint(*profile), assets::DependencyRole::Tool, assets::DependencyRequirement::Required}) != assets::Result::Success)
                return false;
            if (settings.import.defaultMaterial.IsValid() &&
                dependencies.Add({settings.import.defaultMaterial, {}, assets::DependencyRole::Generated, assets::DependencyRequirement::Required}) != assets::Result::Success)
                return false;
            if (settings.import.skeleton.IsValid() &&
                dependencies.Add({settings.import.skeleton, {}, assets::DependencyRole::Generated, assets::DependencyRequirement::Required}) != assets::Result::Success)
                return false;
            ImportedMesh mesh;
            MeshImportReport report;
            return ResolveAndImport(request, settings, implementation.config, mesh, report) && DiscoverImportedDependencies(request, mesh, implementation.config, dependencies);
        }

        [[nodiscard]] bool CompileAsset(const assets::CompileContext& context, assets::ArtifactWriter& artifacts, void* userData) noexcept
        {
            MeshAssetCompiler::Impl& implementation = *static_cast<MeshAssetCompiler::Impl*>(userData);
            if (context.IsCancellationRequested())
                return false;
            ParsedSettings settings;
            if (!ParseSettings(context.request.settings, settings) || context.request.source.identity.GetPath() != resources::ResourcePath::FromString(settings.sourceName))
                return false;
            ImportedMesh mesh;
            MeshImportReport importReport;
            if (!ResolveAndImport(context.request, settings, implementation.config, mesh, importReport) ||
                !VerifyImportedDependencies(context.request, mesh, implementation.config, context.dependencies))
            {
                VG_LOG_ERROR(diagnostics::Category::DataBuild, "Mesh import failed for %s: %s", settings.sourceName, importReport.diagnostic.AsChar());
                return false;
            }
            if (context.IsCancellationRequested())
                return false;
            containers::DynamicArray<u8> bytes(memory::pools::Assets::GetInstance());
            filesystem::MemoryFileWriter writer(bytes);
            CookReport cookReport;
            const Result result = CookMesh(mesh.BuildSourceView(), writer, settings.cook, &cookReport);
            if (result != Result::Success)
            {
                VG_LOG_ERROR(diagnostics::Category::DataBuild, "Mesh cook failed for %s: %s", settings.sourceName, ToString(result));
                return false;
            }
            filesystem::MemoryFileReader reader(bytes, 0);
            meshes::MeshFile cooked;
            containers::DynamicArray<meshes::StorageSegment> segments(memory::pools::Assets::GetInstance());
            if (context.IsCancellationRequested() || cooked.Open(reader) != meshes::Result::Success ||
                meshes::BuildStorageSegments(cooked, bytes.Size(), segments) != meshes::Result::Success)
                return false;
            for (u32 segmentIndex = 0; segmentIndex < segments.Size(); ++segmentIndex)
            {
                const meshes::StorageSegment& segment = segments[segmentIndex];
                assets::ArtifactFlags flags = assets::ArtifactFlags::Streamable;
                if (meshes::HasFlag(segment.flags, meshes::StorageSegmentFlags::Metadata))
                    flags = assets::ArtifactFlags::Primary | assets::ArtifactFlags::MemoryResident;
                else if (meshes::HasFlag(segment.flags, meshes::StorageSegmentFlags::RequiredForLowestLod))
                    flags = flags | assets::ArtifactFlags::MemoryResident;
                if (segment.offset > bytes.Size() || segment.byteSize > bytes.Size() - segment.offset ||
                    artifacts.Add(context.request.output, segmentIndex, flags, segment.alignmentLog2, bytes.TypedData() + segment.offset, static_cast<usize>(segment.byteSize)) !=
                        assets::Result::Success)
                    return false;
            }
            return !context.IsCancellationRequested();
        }
    } // namespace

    MeshAssetCompiler::~MeshAssetCompiler()
    {
        static_cast<void>(Shutdown());
    }

    bool MeshAssetCompiler::Initialize(const MeshAssetCompilerConfig& config) noexcept
    {
        if (m_impl != nullptr || !vanguard::mesh_tools::IsInitialized() || config.resolveSourceFile == nullptr || config.resolveDependency == nullptr || config.maximumDependencies == 0)
            return false;
        Impl* const implementation = VANGUARD_NEW(Impl, memory::pools::Tools);
        if (implementation == nullptr)
            return false;
        implementation->config = config;
        implementation->compilerId = assets::HashCompilerName(CompilerName);
        m_impl = implementation;
        return true;
    }

    bool MeshAssetCompiler::Shutdown() noexcept
    {
        if (m_impl == nullptr)
            return true;
        if (m_impl->buildSystem != nullptr && Unregister() != assets::Result::Success)
            return false;
        VANGUARD_DELETE(m_impl);
        m_impl = nullptr;
        return true;
    }

    bool MeshAssetCompiler::IsInitialized() const noexcept
    {
        return m_impl != nullptr;
    }

    assets::CompilerDescriptor MeshAssetCompiler::GetDescriptor() noexcept
    {
        if (m_impl == nullptr)
            return {};
        return {m_impl->compilerId, CompilerName, MeshAssetCompilerVersion, MeshSourceResourceType, meshes::MeshResourceType, Discover, CompileAsset, m_impl};
    }

    assets::Result MeshAssetCompiler::Register(assets::BuildSystem& buildSystem) noexcept
    {
        if (m_impl == nullptr || m_impl->buildSystem != nullptr)
            return assets::Result::InvalidState;
        const assets::Result result = buildSystem.RegisterCompiler(GetDescriptor());
        if (result == assets::Result::Success)
            m_impl->buildSystem = &buildSystem;
        return result;
    }

    assets::Result MeshAssetCompiler::Unregister() noexcept
    {
        if (m_impl == nullptr || m_impl->buildSystem == nullptr)
            return assets::Result::InvalidState;
        const assets::Result result = m_impl->buildSystem->UnregisterCompiler(m_impl->compilerId);
        if (result == assets::Result::Success)
            m_impl->buildSystem = nullptr;
        return result;
    }
} // namespace vanguard::mesh_tools
