#include <vanguard/shader_tools/shader_asset_compiler.hpp>

#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/memory/pool.hpp>

namespace
{
    using namespace vanguard;
    namespace assets = vanguard::assets;
    namespace tools = vanguard::shader_tools;

    constexpr u32 SettingsMagic = vanguard::serialization::MakeFourCC('V', 'S', 'C', 'B');
    constexpr u16 SettingsVersion = 1;
    constexpr u32 MaximumEntries = 32;
    constexpr u32 MaximumDefines = 1024;
    constexpr u32 MaximumPathBytes = 1024;
    constexpr u32 MaximumProfileBytes = 64;
    constexpr u32 MaximumDefineNameBytes = 256;
    constexpr u32 MaximumDefineValueBytes = 1024;
    constexpr char CompilerName[] = "vanguard.shader.compiler";
    constexpr char CompilerToolPath[] = "tools/vanguard-shader-compiler";

    [[nodiscard]] u32 StringLength(const char* value, const u32 maximum) noexcept
    {
        if (value == nullptr)
            return maximum;
        u32 length = 0;
        while (length < maximum && value[length] != '\0')
            ++length;
        return length;
    }

    void AppendU8(containers::DynamicArray<u8>& output, const u8 value) noexcept
    {
        output.PushBack(value);
    }
    void AppendU16(containers::DynamicArray<u8>& output, const u16 value) noexcept
    {
        AppendU8(output, static_cast<u8>(value));
        AppendU8(output, static_cast<u8>(value >> 8u));
    }
    void AppendU32(containers::DynamicArray<u8>& output, const u32 value) noexcept
    {
        AppendU16(output, static_cast<u16>(value));
        AppendU16(output, static_cast<u16>(value >> 16u));
    }
    void AppendU64(containers::DynamicArray<u8>& output, const u64 value) noexcept
    {
        AppendU32(output, static_cast<u32>(value));
        AppendU32(output, static_cast<u32>(value >> 32u));
    }
    void AppendBytes(containers::DynamicArray<u8>& output, const void* data, const u32 size) noexcept
    {
        const u8* const bytes = static_cast<const u8*>(data);
        for (u32 index = 0; index < size; ++index)
            output.PushBack(bytes[index]);
    }
    void AppendString(containers::DynamicArray<u8>& output, const char* value, const u16 size) noexcept
    {
        AppendU16(output, size);
        AppendBytes(output, value, size);
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
        [[nodiscard]] bool Bytes(void* destination, const u32 size) noexcept
        {
            if (size > m_bytes.Size() - m_offset)
                return false;
            u8* const output = static_cast<u8*>(destination);
            for (u32 index = 0; index < size; ++index)
                output[index] = m_bytes[m_offset + index];
            m_offset += size;
            return true;
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
        ParsedSettings() noexcept
            : characters(memory::pools::Tools::GetInstance()), entries(memory::pools::Tools::GetInstance()), defines(memory::pools::Tools::GetInstance())
        {
        }

        containers::DynamicArray<char> characters;
        containers::DynamicArray<tools::EntryPoint> entries;
        containers::DynamicArray<tools::Define> defines;
        tools::CompileSettings compile;
        crypto::Digest256 permutation;
        const char* sourceName = nullptr;
        u64 program = 0;
    };

    [[nodiscard]] bool ReadString(SettingsReader& reader, containers::DynamicArray<char>& storage, const u32 maximum, const bool allowEmpty,
                                  const char*& output) noexcept
    {
        u16 size = 0;
        if (!reader.U16(size) || size >= maximum || (!allowEmpty && size == 0))
            return false;
        if (size == 0)
        {
            output = nullptr;
            return true;
        }
        const u32 offset = storage.Size();
        for (u32 index = 0; index <= size; ++index)
            storage.PushBack('\0');
        if (!reader.Bytes(storage.TypedData() + offset, size))
            return false;
        output = storage.TypedData() + offset;
        return true;
    }

    [[nodiscard]] bool ParseSettings(const containers::ArraySpan<const u8> bytes, const assets::TargetPlatform target, ParsedSettings& output) noexcept
    {
        if (bytes.Empty() || bytes.Size() > 1024u * 1024u)
            return false;
        output.characters.Reserve(bytes.Size());
        if (output.characters.Capacity() < bytes.Size())
            return false;
        SettingsReader reader(bytes);
        u32 magic = 0, entryCount = 0, defineCount = 0;
        u16 version = 0, reserved16 = 0;
        u8 optimization = 0, debugInformation = 0, flags = 0, reserved8 = 0;
        if (!reader.U32(magic) || !reader.U16(version) || !reader.U16(reserved16) || !reader.U64(output.program) ||
            !reader.Bytes(output.permutation.bytes, crypto::Digest256::ByteCount) || !reader.U8(optimization) || !reader.U8(debugInformation) ||
            !reader.U8(flags) || !reader.U8(reserved8) || !reader.U32(entryCount) || !reader.U32(defineCount) || magic != SettingsMagic ||
            version != SettingsVersion || reserved16 != 0 || reserved8 != 0 || output.program == 0 ||
            optimization > static_cast<u8>(tools::Optimization::Maximum) || debugInformation > static_cast<u8>(tools::DebugInformation::Maximum) ||
            (flags & ~3u) != 0 || entryCount == 0 || entryCount > MaximumEntries || defineCount > MaximumDefines ||
            !ReadString(reader, output.characters, MaximumPathBytes, false, output.sourceName) ||
            !ReadString(reader, output.characters, MaximumProfileBytes, false, output.compile.profile))
            return false;
        output.compile.target = target == assets::TargetPlatform::WindowsD3D12 ? tools::Target::D3D12Dxil : tools::Target::VulkanSpirV;
        output.compile.optimization = static_cast<tools::Optimization>(optimization);
        output.compile.debugInformation = static_cast<tools::DebugInformation>(debugInformation);
        output.compile.warningsAsErrors = (flags & 1u) != 0;
        output.compile.preciseFloatingPoint = (flags & 2u) != 0;
        output.entries.Reserve(entryCount);
        output.defines.Reserve(defineCount);
        for (u32 index = 0; index < entryCount; ++index)
        {
            u8 stage = 0, entryReserved = 0;
            const char* name = nullptr;
            if (!reader.U8(stage) || !reader.U8(entryReserved) || entryReserved != 0 || stage >= static_cast<u8>(shaders::ShaderStage::Count) ||
                !ReadString(reader, output.characters, shaders::MaximumEntryPointLength, false, name))
                return false;
            output.entries.PushBack({name, static_cast<shaders::ShaderStage>(stage)});
        }
        for (u32 index = 0; index < defineCount; ++index)
        {
            const char* name = nullptr;
            const char* value = nullptr;
            if (!ReadString(reader, output.characters, MaximumDefineNameBytes, false, name) ||
                !ReadString(reader, output.characters, MaximumDefineValueBytes, true, value))
                return false;
            output.defines.PushBack({name, value});
        }
        return reader.Finished();
    }

    [[nodiscard]] bool NormalizePath(const char* basePath, const char* includePath, const bool relative, char (&output)[MaximumPathBytes]) noexcept
    {
        char combined[MaximumPathBytes]{};
        u32 length = 0;
        if (relative && basePath != nullptr)
        {
            const u32 baseLength = StringLength(basePath, MaximumPathBytes);
            if (baseLength >= MaximumPathBytes)
                return false;
            u32 separator = baseLength;
            while (separator != 0 && basePath[separator - 1u] != '/' && basePath[separator - 1u] != '\\')
                --separator;
            if (separator >= MaximumPathBytes)
                return false;
            for (u32 index = 0; index < separator; ++index)
                combined[length++] = basePath[index];
        }
        const u32 includeLength = StringLength(includePath, MaximumPathBytes);
        if (includeLength == 0 || includeLength >= MaximumPathBytes || length + includeLength >= MaximumPathBytes)
            return false;
        for (u32 index = 0; index < includeLength; ++index)
            combined[length++] = includePath[index];

        u32 segmentStarts[128]{};
        u32 segmentCount = 0;
        u32 written = 0;
        u32 cursor = 0;
        while (cursor < length)
        {
            while (cursor < length && (combined[cursor] == '/' || combined[cursor] == '\\'))
                ++cursor;
            const u32 begin = cursor;
            while (cursor < length && combined[cursor] != '/' && combined[cursor] != '\\')
                ++cursor;
            const u32 size = cursor - begin;
            if (size == 0 || (size == 1 && combined[begin] == '.'))
                continue;
            if (size == 2 && combined[begin] == '.' && combined[begin + 1u] == '.')
            {
                if (segmentCount == 0)
                    return false;
                written = segmentStarts[--segmentCount];
                continue;
            }
            if (segmentCount >= 128 || written + size + (written != 0 ? 1u : 0u) >= MaximumPathBytes)
                return false;
            segmentStarts[segmentCount++] = written;
            if (written != 0)
                output[written++] = '/';
            for (u32 index = 0; index < size; ++index)
            {
                char value = combined[begin + index];
                if (value >= 'A' && value <= 'Z')
                    value = static_cast<char>(value + ('a' - 'A'));
                output[written++] = value;
            }
        }
        if (written == 0)
            return false;
        output[written] = '\0';
        return resources::ResourcePath::FromString(output).IsValid();
    }

    struct ResolvedInclude
    {
        char path[MaximumPathBytes]{};
        resources::ResourceReference identity;
        crypto::Digest256 content;
    };

    struct DiscoveryContext
    {
        explicit DiscoveryContext(const tools::ShaderAssetCompilerConfig& value) noexcept : config(value), includes(memory::pools::Tools::GetInstance()) {}
        const tools::ShaderAssetCompilerConfig& config;
        containers::DynamicArray<ResolvedInclude> includes;
    };

    [[nodiscard]] bool FindInclude(const DiscoveryContext& context, const resources::ResourceReference identity) noexcept
    {
        for (const ResolvedInclude& include : context.includes)
            if (include.identity == identity)
                return true;
        return false;
    }

    [[nodiscard]] bool ScanIncludes(const char* sourceName, const containers::ArraySpan<const u8> source, const u32 depth, DiscoveryContext& context,
                                    assets::DependencyCollector& dependencies) noexcept
    {
        if (depth > context.config.maximumIncludeDepth)
            return false;
        u32 cursor = 0;
        while (cursor < source.Size())
        {
            const u32 lineBegin = cursor;
            while (cursor < source.Size() && source[cursor] != '\n' && source[cursor] != '\r')
                ++cursor;
            const u32 lineEnd = cursor;
            while (cursor < source.Size() && (source[cursor] == '\n' || source[cursor] == '\r'))
                ++cursor;
            u32 token = lineBegin;
            while (token < lineEnd && (source[token] == ' ' || source[token] == '\t'))
                ++token;
            if (token >= lineEnd || source[token++] != '#')
                continue;
            while (token < lineEnd && (source[token] == ' ' || source[token] == '\t'))
                ++token;
            constexpr char IncludeKeyword[] = "include";
            bool includeDirective = true;
            for (u32 index = 0; index < sizeof(IncludeKeyword) - 1u; ++index)
                if (token + index >= lineEnd || source[token + index] != IncludeKeyword[index])
                    includeDirective = false;
            if (!includeDirective)
                continue;
            token += sizeof(IncludeKeyword) - 1u;
            while (token < lineEnd && (source[token] == ' ' || source[token] == '\t'))
                ++token;
            if (token >= lineEnd || (source[token] != '"' && source[token] != '<'))
                return false;
            const u8 opener = source[token++];
            const u8 closer = opener == '"' ? '"' : '>';
            const u32 pathBegin = token;
            while (token < lineEnd && source[token] != closer)
                ++token;
            if (token == pathBegin || token >= lineEnd || token - pathBegin >= MaximumPathBytes)
                return false;
            char includeText[MaximumPathBytes]{};
            for (u32 index = 0; index < token - pathBegin; ++index)
                includeText[index] = static_cast<char>(source[pathBegin + index]);
            char canonicalPath[MaximumPathBytes]{};
            if (!NormalizePath(sourceName, includeText, opener == '"', canonicalPath))
                return false;
            tools::ShaderSource resolved;
            if (!context.config.loadSource(canonicalPath, resolved, context.config.loadSourceUserData) || resolved.canonicalPath == nullptr ||
                resolved.content.Empty() || resolved.content.Data() == nullptr || resolved.identity.ExpectedType() != tools::ShaderSourceResourceType ||
                resolved.identity.GetPath() != resources::ResourcePath::FromString(canonicalPath))
                return false;
            if (FindInclude(context, resolved.identity))
                continue;
            if (context.includes.Size() >= context.config.maximumIncludeFiles)
                return false;
            ResolvedInclude include;
            if (!NormalizePath(nullptr, resolved.canonicalPath, false, include.path) ||
                resources::ResourcePath::FromString(include.path) != resolved.identity.GetPath())
                return false;
            include.identity = resolved.identity;
            include.content = crypto::Sha256(resolved.content.Data(), resolved.content.SizeInBytes());
            context.includes.PushBack(include);
            if (dependencies.Add({include.identity, include.content, assets::DependencyRole::Source, assets::DependencyRequirement::Required}) !=
                assets::Result::Success)
                return false;
            containers::DynamicArray<u8> snapshot(memory::pools::Tools::GetInstance());
            snapshot.Reserve(resolved.content.Size());
            if (snapshot.Capacity() < resolved.content.Size())
                return false;
            for (const u8 value : resolved.content)
                snapshot.PushBack(value);
            if (!ScanIncludes(include.path, snapshot, depth + 1u, context, dependencies))
                return false;
        }
        return true;
    }

    struct CompileSourceContext
    {
        const assets::CompileContext& build;
        const tools::ShaderAssetCompilerConfig& config;
    };

    [[nodiscard]] bool LoadCompileSource(const char* requestedPath, tools::LoadedSource& output, void* userData) noexcept
    {
        CompileSourceContext& context = *static_cast<CompileSourceContext*>(userData);
        char canonicalPath[MaximumPathBytes]{};
        if (!NormalizePath(nullptr, requestedPath, false, canonicalPath))
            return false;
        tools::ShaderSource source;
        if (!context.config.loadSource(canonicalPath, source, context.config.loadSourceUserData) || source.content.Empty() || source.canonicalPath == nullptr ||
            source.identity.ExpectedType() != tools::ShaderSourceResourceType || source.identity.GetPath() != resources::ResourcePath::FromString(canonicalPath))
            return false;
        char returnedPath[MaximumPathBytes]{};
        if (!NormalizePath(nullptr, source.canonicalPath, false, returnedPath) || resources::ResourcePath::FromString(returnedPath) != source.identity.GetPath())
            return false;
        const crypto::Digest256 digest = crypto::Sha256(source.content.Data(), source.content.SizeInBytes());
        bool verified = false;
        for (const assets::BuildDependency& dependency : context.build.dependencies)
            if (dependency.role == assets::DependencyRole::Source && dependency.identity == source.identity && dependency.content == digest)
                verified = true;
        if (!verified)
            return false;
        output.content = source.content;
        return true;
    }
} // namespace

namespace vanguard::shader_tools
{
    struct ShaderAssetCompiler::Impl final
    {
        ShaderCompiler compiler;
        ShaderAssetCompilerConfig config;
        assets::BuildSystem* buildSystem = nullptr;
        assets::CompilerId compilerId = assets::InvalidCompilerId;
    };

    BuildSettingsResult EncodeShaderBuildSettings(const ShaderBuildDescription& description, containers::DynamicArray<u8>& output) noexcept
    {
        output.Clear();
        const u32 sourceLength = StringLength(description.sourceName, MaximumPathBytes);
        const u32 profileLength = StringLength(description.profile, MaximumProfileBytes);
        if (description.program == 0 || sourceLength == 0 || sourceLength >= MaximumPathBytes || profileLength == 0 || profileLength >= MaximumProfileBytes ||
            description.entryPoints.Empty() || description.entryPoints.Size() > MaximumEntries || description.defines.Size() > MaximumDefines ||
            description.optimization > Optimization::Maximum || description.debugInformation > DebugInformation::Maximum ||
            !resources::ResourcePath::FromString(description.sourceName).IsValid())
            return BuildSettingsResult::InvalidArgument;
        char canonicalSource[MaximumPathBytes]{};
        usize canonicalSourceLength = 0;
        if (resources::CanonicalizePath(description.sourceName, canonicalSource, MaximumPathBytes, canonicalSourceLength) != resources::Result::Success ||
            canonicalSourceLength == 0)
            return BuildSettingsResult::InvalidArgument;
        u64 required = 64u + sourceLength + profileLength;
        for (const EntryPoint& entry : description.entryPoints)
        {
            const u32 length = StringLength(entry.name, shaders::MaximumEntryPointLength);
            if (length == 0 || length >= shaders::MaximumEntryPointLength || entry.stage >= shaders::ShaderStage::Count)
                return BuildSettingsResult::InvalidArgument;
            required += 4u + length;
        }
        for (const Define& define : description.defines)
        {
            const u32 nameLength = StringLength(define.name, MaximumDefineNameBytes);
            const u32 valueLength = define.value != nullptr ? StringLength(define.value, MaximumDefineValueBytes) : 0;
            if (nameLength == 0 || nameLength >= MaximumDefineNameBytes || valueLength >= MaximumDefineValueBytes)
                return BuildSettingsResult::InvalidArgument;
            required += 4u + nameLength + valueLength;
        }
        if (required > 1024u * 1024u)
            return BuildSettingsResult::LimitExceeded;
        output.Reserve(static_cast<u32>(required));
        if (output.Capacity() < required)
            return BuildSettingsResult::LimitExceeded;
        AppendU32(output, SettingsMagic);
        AppendU16(output, SettingsVersion);
        AppendU16(output, 0);
        AppendU64(output, description.program);
        AppendBytes(output, description.permutation.bytes, crypto::Digest256::ByteCount);
        AppendU8(output, static_cast<u8>(description.optimization));
        AppendU8(output, static_cast<u8>(description.debugInformation));
        AppendU8(output, static_cast<u8>((description.warningsAsErrors ? 1u : 0u) | (description.preciseFloatingPoint ? 2u : 0u)));
        AppendU8(output, 0);
        AppendU32(output, description.entryPoints.Size());
        AppendU32(output, description.defines.Size());
        AppendString(output, canonicalSource, static_cast<u16>(canonicalSourceLength));
        AppendString(output, description.profile, static_cast<u16>(profileLength));
        for (const EntryPoint& entry : description.entryPoints)
        {
            AppendU8(output, static_cast<u8>(entry.stage));
            AppendU8(output, 0);
            AppendString(output, entry.name, static_cast<u16>(StringLength(entry.name, shaders::MaximumEntryPointLength)));
        }
        for (const Define& define : description.defines)
        {
            AppendString(output, define.name, static_cast<u16>(StringLength(define.name, MaximumDefineNameBytes)));
            AppendString(output, define.value, static_cast<u16>(define.value != nullptr ? StringLength(define.value, MaximumDefineValueBytes) : 0));
        }
        return BuildSettingsResult::Success;
    }

    namespace
    {
        [[nodiscard]] bool Discover(const assets::BuildRequest& request, assets::DependencyCollector& dependencies, void* userData) noexcept
        {
            ShaderAssetCompiler::Impl& implementation = *static_cast<ShaderAssetCompiler::Impl*>(userData);
            ParsedSettings settings;
            if (!ParseSettings(request.settings, request.target, settings) ||
                request.source.identity.GetPath() != resources::ResourcePath::FromString(settings.sourceName))
                return false;
            const resources::ResourceReference tool(resources::ResourcePath::FromString(CompilerToolPath), ShaderCompilerToolResourceType);
            if (dependencies.Add({tool, implementation.compiler.CompilerFingerprint(), assets::DependencyRole::Tool,
                                  assets::DependencyRequirement::Required}) != assets::Result::Success)
                return false;
            DiscoveryContext context(implementation.config);
            return ScanIncludes(settings.sourceName, request.source.content, 0, context, dependencies);
        }

        [[nodiscard]] bool CompileAsset(const assets::CompileContext& context, assets::ArtifactWriter& artifacts, void* userData) noexcept
        {
            ShaderAssetCompiler::Impl& implementation = *static_cast<ShaderAssetCompiler::Impl*>(userData);
            if (context.IsCancellationRequested())
                return false;
            ParsedSettings settings;
            if (!ParseSettings(context.request.settings, context.request.target, settings) ||
                context.request.source.identity.GetPath() != resources::ResourcePath::FromString(settings.sourceName))
                return false;
            CompileSourceContext sourceContext{context, implementation.config};
            CompileRequest request;
            request.sourceName = settings.sourceName;
            request.moduleName = "vanguard_shader_asset";
            request.source = context.request.source.content;
            request.entryPoints = settings.entries;
            request.defines = settings.defines;
            request.loadSource = LoadCompileSource;
            request.loadSourceUserData = &sourceContext;
            request.settings = settings.compile;
            CompileOutput compiled;
            const Result result = implementation.compiler.Compile(request, compiled);
            if (result != Result::Success)
            {
                VG_LOG_ERROR(diagnostics::Category::Rendering, "Shader build failed for %s: %s\n%s", settings.sourceName, ToString(result),
                             compiled.GetDiagnostics());
                return false;
            }
            if (context.IsCancellationRequested())
                return false;
            containers::DynamicArray<u8> bytes(memory::pools::Assets::GetInstance());
            filesystem::MemoryFileWriter writer(bytes);
            if (compiled.WriteShader(writer, settings.program, settings.permutation) != Result::Success)
                return false;
            return artifacts.Add(context.request.output, 0, assets::ArtifactFlags::Primary | assets::ArtifactFlags::MemoryResident, 4, bytes.Data(),
                                 bytes.Size()) == assets::Result::Success;
        }
    } // namespace

    ShaderAssetCompiler::~ShaderAssetCompiler()
    {
        static_cast<void>(Shutdown());
    }

    bool ShaderAssetCompiler::Initialize(const ShaderAssetCompilerConfig& config) noexcept
    {
        if (m_impl != nullptr || config.loadSource == nullptr || config.maximumIncludeFiles == 0 || config.maximumIncludeDepth == 0)
            return false;
        Impl* const implementation = VANGUARD_NEW(Impl, memory::pools::Tools);
        if (implementation == nullptr || implementation->compiler.Initialize() != Result::Success)
        {
            if (implementation != nullptr)
                VANGUARD_DELETE(implementation);
            return false;
        }
        implementation->config = config;
        implementation->compilerId = assets::HashCompilerName(CompilerName);
        m_impl = implementation;
        return true;
    }

    bool ShaderAssetCompiler::Shutdown() noexcept
    {
        if (m_impl == nullptr)
            return true;
        if (m_impl->buildSystem != nullptr && Unregister() != assets::Result::Success)
            return false;
        m_impl->compiler.Shutdown();
        VANGUARD_DELETE(m_impl);
        m_impl = nullptr;
        return true;
    }

    bool ShaderAssetCompiler::IsInitialized() const noexcept
    {
        return m_impl != nullptr;
    }

    assets::CompilerDescriptor ShaderAssetCompiler::GetDescriptor() noexcept
    {
        if (m_impl == nullptr)
            return {};
        return {m_impl->compilerId, CompilerName, ShaderAssetCompilerVersion, ShaderSourceResourceType, shaders::ShaderResourceType, Discover,
                CompileAsset,       m_impl};
    }

    assets::Result ShaderAssetCompiler::Register(assets::BuildSystem& buildSystem) noexcept
    {
        if (m_impl == nullptr || m_impl->buildSystem != nullptr)
            return assets::Result::InvalidState;
        const assets::Result result = buildSystem.RegisterCompiler(GetDescriptor());
        if (result == assets::Result::Success)
            m_impl->buildSystem = &buildSystem;
        return result;
    }

    assets::Result ShaderAssetCompiler::Unregister() noexcept
    {
        if (m_impl == nullptr || m_impl->buildSystem == nullptr)
            return assets::Result::InvalidState;
        const assets::Result result = m_impl->buildSystem->UnregisterCompiler(m_impl->compilerId);
        if (result == assets::Result::Success)
            m_impl->buildSystem = nullptr;
        return result;
    }
} // namespace vanguard::shader_tools
