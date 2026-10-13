#include "../../../imported/backend/backendData/src/build.h"

#include <vanguard/asset_host/durable_resource_depot.hpp>
#include <vanguard/asset_streaming/platform_directory_observer.hpp>

#include <vanguard/containers/containers.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/io/io.hpp>
#include <vanguard/jobs/jobs.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/reflection/reflection.hpp>

#include "../../../imported/backend/backendData/include/cookerTask.h"
#include "../../../imported/backend/backendData/include/cooker.h"
#include "../../../imported/backend/backendData/include/dataErrorHandlerImpl.h"
#include "../../../imported/backend/backendData/include/dataSystemInstance.h"
#include "../../../imported/backend/backendData/include/dataTaskReporter.h"
#include "../../../imported/backend/backendData/include/dataTaskSystem.h"
#include "../../../imported/backend/backendData/include/dataTypesImpl.h"
#include "../../../imported/backend/backendData/include/dependencyBinaryDatabase.h"
#include "../../../imported/backend/backendData/include/dependencyCache.h"
#include "../../../imported/backend/backendData/include/dependencyScannerImpl.h"
#include "../../../imported/backend/backendData/include/resourceDatabase.h"
#include "../../../imported/backend/backendData/src/fileSystemFilter.h"
#include "../../../imported/backend/backendEngine/include/importer.h"
#include "../../../imported/backend/backendMesh/include/meshOptimizer.h"
#include "../../../imported/common/core/include/coreApi.h"
#include "../../../imported/backend/backendEngine/include/resourceMetadata.h"
#include "../../../imported/backend/backendEngine/include/resourceImportMetadata.h"
#include "../../../imported/common/resource/include/backendResourceDepot.h"
#include "../../../imported/common/resource/include/2darray.h"
#include "../../../imported/common/resource/include/resourceSaver.h"
#include "../../../imported/common/renderData/include/renderObjectPtr.h"
#include "../../../imported/common/renderData/include/renderResource.h"
#include "../../../imported/common/renderData/include/renderSimpleTypes.h"
#include "../../../imported/common/resourceMaterial/include/resourceMaterialPublic.h"
#include "../../../imported/common/resourceMaterial/include/bitmapTexture.h"
#include "../../../imported/common/resourceMaterial/include/materialInstance.h"
#include "../../../imported/common/resourceMaterial/include/materialTemplate.h"
#include "../../../imported/common/resourceMesh/include/resourceMeshApi.h"
#include "../../../imported/common/resourceMesh/include/mesh.h"
#include "../../../imported/common/resourceMesh/include/meshDataViewRO.h"
#include "../../../imported/common/redReflection/include/resourceClassLookup.h"
#include "../../../imported/common/redReflection/include/resourceDepot.h"
#include "../../../imported/common/redReflection/include/resourceLoader.h"
#include "../../../imported/common/redReflection/include/resourceToken.h"
#include "../../../imported/common/redReflection/include/rttiProperty.h"
#include "../../../imported/common/redReflection/include/serializationSaver.h"

#include <cstring>
#include <cstdio>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <utility>

extern void Register2DArraySaver();
extern void Register2DArrayLoader();
extern "C" Uint32 VanguardAssetHost_GetHardwareBC7CompressionCount();
extern "C" Bool VanguardAssetHost_SetBitmapTextureImportCookingPlatform(Uint32 platform);

namespace img::memory
{
    void InitializeMemoryPools();
}

namespace rend
{
    void InitializeMemoryPools();
}

namespace
{
    constexpr data::TargetPlatform kAssetHostTargetPlatform = data::TargetPlatform::PC;

    struct TextureShadowBinding
    {
        res::ResourcePath resourcePath;
        red::AbsolutePath sourcePath;
    };

    struct TextureShadowWatchState
    {
        const red::DynArray<TextureShadowBinding>* bindings = nullptr;
        std::atomic<Uint64> pendingMask{0};
        std::atomic<bool> overflow{false};
    };

    void OnTextureShadowSourceObservation(const vanguard::filesystem::DirectoryObservation& observation, void* userData) noexcept
    {
        auto* const state = static_cast<TextureShadowWatchState*>(userData);
        if (!state || !state->bindings)
            return;
        if (observation.kind == vanguard::filesystem::DirectoryObservationKind::Overflow)
        {
            state->overflow.store(true, std::memory_order_release);
            return;
        }

        const auto& bindings = *state->bindings;
        const Uint32 count = static_cast<Uint32>(Min<Uint64>(bindings.Size(), 64u));
        for (Uint32 index = 0; index < count; ++index)
        {
            if (observation.path == bindings[index].sourcePath)
            {
                state->pendingMask.fetch_or(Uint64{1} << index, std::memory_order_release);
            }
        }
    }

    bool BuildTextureShadowBindings(resdb::Database& database, res::IBackendResourceDepot& depot,
                                    const red::AbsolutePath& sourceRoot, red::DynArray<TextureShadowBinding>& outBindings)
    {
        outBindings.Clear();
        for (const res::ResourcePath& path : database.GetResourcePaths())
        {
            if (red::paths::GetExtension(path.ToStringView()) != "xbm")
                continue;

            const THandle<res::ResourceMetadata> metadata = res::ResourceMetadata::Get(path, false, &depot);
            if (!metadata || !metadata->GetImportData() || metadata->GetImportData()->GetAssetPath().Empty())
                continue;

            const red::AbsolutePath sourcePath = red::AbsolutePath::CreateFilePath(metadata->GetImportData()->GetAssetPath());
            if (!sourcePath.AsStringView().StartsWithIgnoreCase(sourceRoot.AsStringView()))
                continue;
            if (outBindings.Size() >= 64u)
                return false;

            TextureShadowBinding binding;
            binding.resourcePath = path;
            binding.sourcePath = sourcePath;
            outBindings.PushBack(std::move(binding));
        }
        return !outBindings.Empty();
    }

    bool IsStage12DummyProjectRoot(const char* root)
    {
        if (!root || !*root)
            return false;

        std::string projectPath(root);
        if (!projectPath.empty() && projectPath.back() != '\\' && projectPath.back() != '/')
            projectPath.push_back('\\');
        projectPath += "red-asset-test.vproject";

        std::ifstream file(projectPath, std::ios::binary);
        if (!file)
            return false;

        char text[4097]{};
        file.read(text, 4096);
        const std::streamsize bytesRead = file.gcount();
        if (bytesRead <= 0 || bytesRead == 4096)
            return false;
        text[bytesRead] = '\0';

        return std::strstr(text, "vproject 1.0") != nullptr &&
               std::strstr(text, "project.id = \"3c50d9df-febd-4dd0-a171-024655bc426a\"") != nullptr &&
               std::strstr(text, "project.name = \"RED Asset Test\"") != nullptr &&
               std::strstr(text, "project.technicalName = \"red-asset-test\"") != nullptr;
    }

    class Gate0RejectingSaver final : public serialization::ISaver
    {
    public:
        Bool SaveObjects(IFile&, const serialization::SavingContext&) const override { return false; }
    };

    class ScratchDepot final : public res::IBackendResourceDepot
    {
    public:
        ScratchDepot( red::AbsolutePath root, const char* debugName )
            : m_root( std::move( root ) )
            , m_debugName( debugName )
            , m_dlcRoots( red::PoolBackend() )
        {
        }

        red::SharedPtr<serialization::IAsyncSource> CreateResourceAsyncSource(
            const res::ResourcePath, const res::CreateResourceAsyncSourceParams& ) override
        {
            return {};
        }

        bool ResourceExists( const res::ResourcePath path ) const override
        {
            return GFileManager->FileExist( TranslateResourcePath( path ) );
        }

        bool ResourceIsReadOnly( const res::ResourcePath path ) const override
        {
            return GFileManager->IsFileReadOnly( TranslateResourcePath( path ) );
        }

        Uint64 GetResourceTimestamp( const res::ResourcePath path ) const override
        {
            return GFileManager->GetFileTimeStamp( TranslateResourcePath( path ) );
        }

        res::ResourcePath ResolveResourcePath( const res::ResourcePath path ) const override
        {
            return ResourceExists( path ) ? path : res::ResourcePath{};
        }

        const char* GetDebugName() const override
        {
            return m_debugName;
        }

        Uint32 Debug_GetResourceTotalSizeOnDisk( const res::ResourcePath ) const override
        {
            return 0;
        }

        Uint32 Debug_GetResourceObjectSizeOnDisk( const res::ResourcePath ) const override
        {
            return 0;
        }

        bool IsLanguageVoicePackAvailable( const CName ) const override
        {
            return false;
        }

        bool IsLanguageVoicePackInstalled( const CName ) const override
        {
            return false;
        }

        const red::DynArray<red::String>& GetDLCRootPaths() const override
        {
            return m_dlcRoots;
        }

        red::SharedPtr<IFile> CreateResourceReader( const res::ResourcePath path, const bool ) override
        {
            red::SharedPtr<IFile> result;
            result = GFileManager->CreateFileReader( TranslateResourcePath( path ), FOF_AbsolutePath );
            return result;
        }

        red::SharedPtr<IFile> CreateResourceWriter( const res::ResourcePath path ) override
        {
            ++m_writerCreations;
            red::SharedPtr<IFile> result;
            result = GFileManager->CreateFileWriter( TranslateResourcePath( path ), FOF_AbsolutePath );
            return result;
        }

        Uint32 WriterCreations() const
        {
            return m_writerCreations;
        }

        void FindDepotSubDirectories( const String&, red::DynArray<String>& ) const override
        {
        }

        void FindDepotFiles( const String&, const char*, red::DynArray<String>& ) const override
        {
        }

        bool RemoveResource( const res::ResourcePath path ) override
        {
            return GFileManager->DeleteFile( TranslateResourcePath( path ) );
        }

        red::AbsolutePath TranslateResourcePath( const res::ResourcePath path ) const override
        {
            return m_root.AddFilePath( path.ToStringView() );
        }

        res::ResourcePath TranslateAbsolutePath( const red::AbsolutePath& absolutePath ) const override
        {
            red::StringView path = absolutePath.AsStringView();
            const red::StringView root = m_root.AsStringView();
            if ( !path.StartsWith( root ) )
            {
                return {};
            }
            path.TrimFront( root.Length() );
            return res::ResourcePath::Build( path );
        }

        bool ResourceIsLink( const res::ResourcePath ) const override
        {
            return false;
        }

        void SetDLCRootPaths( const red::DynArray<String>& rootPaths ) override
        {
            m_dlcRoots = rootPaths;
        }

        void ScanForDLCRootPaths() override
        {
        }

    private:
        red::AbsolutePath m_root;
        const char* m_debugName;
        red::DynArray<red::String> m_dlcRoots;
        Uint32 m_writerCreations = 0;
    };

    class HostSystemContext final : public data::SystemContext
    {
    public:
        HostSystemContext( data::ResourceDepot& sourceDepot, data::ResourceDepot& cookedDepot,
                           dependency::Cache& sourceCache, dependency::Cache& cookedCache,
                           dependency::Database& database, data::ErrorHandler& errorHandler,
                           resdb::Database* sourceResourceDatabase = nullptr )
        {
            m_sourceDepot = &sourceDepot;
            m_cookedDepot = &cookedDepot;
            m_sourceLoader = nullptr;
            m_cookedLoader = nullptr;
            m_sourceDepotDatabase = sourceResourceDatabase;
            m_outputCollector = nullptr;
            m_errorHandler = &errorHandler;
            m_workerTaskAccumulator = nullptr;
            m_targetPlatform = data::TargetPlatform::PC;
            m_isServer = false;
            m_isWorker = false;
            m_sourceDepsCache = &sourceCache;
            m_cookedDepsCache = &cookedCache;
            m_depsDatabase = &database;
            m_dataCache = nullptr;
            m_force = false;
            m_collectDebugInfo = false;
        }
    };

    class HostReporter final : public data::TaskReporter
    {
    public:
        void SetTasksCount( Uint32 ) override {}
        void UpdateSystemStatus( const red::String& ) override {}
        void UpdateTaskProgress( Uint32, String&& ) override {}
        void ClearTaskProgress( Uint32 ) override {}
    };

    struct RuntimeState final
    {
        bool diagnostics = false;
        bool io = false;
        bool filesystem = false;
        bool jobs = false;
    };

    void ShutdownRuntime( RuntimeState& state )
    {
        if ( state.jobs && vanguard::jobs::IsInitialized() )
        {
            static_cast<void>( vanguard::jobs::Shutdown() );
            state.jobs = false;
        }
        if ( state.filesystem && vanguard::filesystem::IsInitialized() )
        {
            vanguard::filesystem::Shutdown();
            state.filesystem = false;
        }
        if ( state.io && vanguard::io::IsInitialized() )
        {
            vanguard::io::Shutdown();
            state.io = false;
        }
        if ( state.diagnostics && vanguard::diagnostics::IsInitialized() )
        {
            vanguard::diagnostics::Shutdown();
            state.diagnostics = false;
        }
    }

    bool InitializeRuntime( const red::AbsolutePath& scratchRoot, RuntimeState& state,
                            const red::AbsolutePath& cacheRoot = red::AbsolutePath() )
    {
        if ( !vanguard::diagnostics::Initialize( vanguard::diagnostics::Mode::Synchronous, "VanguardAssetHost" ) )
        {
            return false;
        }
        state.diagnostics = true;

        if ( !vanguard::io::Initialize() )
        {
            return false;
        }
        state.io = true;

        if ( !vanguard::reflection::IsInitialized() && !vanguard::reflection::Initialize() )
        {
            return false;
        }

        const red::AbsolutePath& effectiveCacheRoot = cacheRoot.Empty() ? scratchRoot : cacheRoot;
        const vanguard::filesystem::Config filesystemConfig{ scratchRoot, scratchRoot, effectiveCacheRoot };
        if ( !vanguard::filesystem::Initialize( filesystemConfig ) )
        {
            return false;
        }
        state.filesystem = true;

        if ( !vanguard::filesystem::GetManager().CreatePath( effectiveCacheRoot ) )
        {
            return false;
        }

        if ( !vanguard::jobs::Initialize( vanguard::jobs::ToolConfig() ) )
        {
            return false;
        }
        state.jobs = true;

        RED_INITIALIZE_MEMORY_POOL( data::PoolDataSystem, red::PoolBackend, red::memory::AcquireDefaultAllocator(), 0 );
        return true;
    }

    void InitializeTextureDomainRuntime()
    {
        // Initialize only the CPU pools required by texture import/cook work; no live render scene is created here.
        // Keep them out of the earlier CSV-only proof modes so Stage 1.1/1.2 retain their narrow startup surface.
        img::memory::InitializeMemoryPools();
        rend::InitializeMemoryPools();
    }

    void InitializeMeshDomainRuntime()
    {
        InitializeTextureDomainRuntime();
        mesh::InitializeMeshOptimizerLibrary();
    }

    void ConfigureTextureImportTarget(const data::Context& context)
    {
        const Bool configured = VanguardAssetHost_SetBitmapTextureImportCookingPlatform(
            static_cast<Uint32>(data::CookingPlatformFromTargetPlatform(context.TargetPlatform())));
        ALWAYSENABLED_RED_FATAL_ASSERT(configured, "Unsupported AssetHost bitmap texture target platform");
    }

    int RunOneCook( const red::AbsolutePath& scratchRoot )
    {
        auto& files = vanguard::filesystem::GetManager();
        const red::AbsolutePath sourceRoot = scratchRoot.AddDirPath( "source" );
        const red::AbsolutePath cookedRoot = scratchRoot.AddDirPath( "cooked" );
        const red::AbsolutePath cacheRoot = scratchRoot.AddDirPath( "cache" );
        if ( !files.CreatePath( sourceRoot ) || !files.CreatePath( cookedRoot ) || !files.CreatePath( cacheRoot ) )
        {
            std::fprintf( stderr, "ERROR stage=directories\n" );
            return 10;
        }

        const res::ResourcePath resourcePath = res::ResourcePath::Build( "probe.csv" );
        ScratchDepot sourceDepot( sourceRoot, "AssetHostSource" );
        ScratchDepot cookedDepot( cookedRoot, "AssetHostCooked" );
        dependency::Cache sourceCache( &sourceDepot );
        dependency::Cache cookedCache( &cookedDepot );

        const red::AbsolutePath sourceCachePath = cacheRoot.AddFilePath( "source.DependencyFileHashCache.cache" );
        const red::AbsolutePath cookedCachePath = cacheRoot.AddFilePath( "cooked.DependencyFileHashCache.cache" );
        const red::AbsolutePath databasePath = cacheRoot.AddFilePath( dependency::BinaryDatabase::DefaultDatabaseName() );

        const bool sourceCacheExisted = files.FileExist( sourceCachePath );
        const bool cookedCacheExisted = files.FileExist( cookedCachePath );
        const bool databaseExisted = files.FileExist( databasePath );
        if ( sourceCacheExisted && !sourceCache.Load( sourceCachePath ) )
        {
            std::fprintf( stderr, "ERROR stage=load-source-cache\n" );
            return 11;
        }
        if ( cookedCacheExisted && !cookedCache.Load( cookedCachePath ) )
        {
            std::fprintf( stderr, "ERROR stage=load-cooked-cache\n" );
            return 12;
        }

        dependency::BinaryDatabase database( databasePath, 0 );
        if ( databaseExisted && !database.Load() )
        {
            std::fprintf( stderr, "ERROR stage=load-dependency-db\n" );
            return 13;
        }

        data::GlobalErrorHandler errorHandler;
        HostSystemContext context( sourceDepot, cookedDepot, sourceCache, cookedCache, database, errorHandler );
        HostReporter reporter;
        data::TaskSystem taskSystem( context, &cooker::CreateTask, reporter, 1 );

        red::DynArray<res::ResourcePath> resources{ red::PoolBackend() };
        resources.PushBack( resourcePath );
        taskSystem.Build( resources );

        const auto tasks = taskSystem.GetAllTasksInOrderOfExecution();
        if ( tasks.Size() != 1 || !tasks[0] )
        {
            std::fprintf( stderr, "ERROR stage=task-count count=%u\n", tasks.Size() );
            return 14;
        }

        const auto& task = *tasks[0];
        const auto& metrics = task.GetMetrics();
        const Uint32 buildReason = task.GetBuildReason();
        const bool success = task.Succeeded();

        bool persisted = true;
        if ( database.HasChanged() )
        {
            persisted = database.Save() && persisted;
        }
        persisted = sourceCache.Save( sourceCachePath ) && persisted;
        persisted = cookedCache.Save( cookedCachePath ) && persisted;

        std::printf(
            "RESULT success=%u cook_writes=%u task_files_built=%u build_reason=0x%08X database_preexisting=%u source_cache_preexisting=%u cooked_cache_preexisting=%u persisted=%u\n",
            success ? 1u : 0u,
            cookedDepot.WriterCreations(),
            metrics.filesBuilt,
            buildReason,
            databaseExisted ? 1u : 0u,
            sourceCacheExisted ? 1u : 0u,
            cookedCacheExisted ? 1u : 0u,
            persisted ? 1u : 0u );

        if ( !success )
        {
            std::fprintf( stderr, "ERROR stage=task failure_reason=%u\n", static_cast<Uint32>( task.GetFailureReason() ) );
            return 15;
        }
        if ( !persisted )
        {
            return 16;
        }
        return 0;
    }

    bool WriteExactFile(vanguard::filesystem::Manager& files, const red::AbsolutePath& path,
                        const char* const bytes, const Uint32 byteCount)
    {
        auto writer = files.CreateFileWriter(path);
        if (!writer)
            return false;
        if (byteCount != 0u)
            writer->Serialize(const_cast<char*>(bytes), byteCount);
        writer->Flush();
        return !writer->HasErrors();
    }

    bool FileMatches(vanguard::filesystem::Manager& files, const red::AbsolutePath& path,
                     const char* const expected, const Uint32 expectedSize)
    {
        auto reader = files.CreateFileReader(path, FOF_Buffered);
        if (!reader || reader->GetSize() != expectedSize)
            return false;
        red::DynArray<Uint8> bytes{red::PoolBackend()};
        bytes.Resize(expectedSize);
        if (bytes.Size() != expectedSize)
            return false;
        if (expectedSize != 0u)
            reader->Serialize(bytes.Data(), expectedSize);
        return !reader->HasErrors() &&
               (expectedSize == 0u || std::memcmp(bytes.Data(), expected, expectedSize) == 0);
    }

    bool FileMatches(vanguard::filesystem::Manager& files, const red::AbsolutePath& path,
                     const red::DynArray<Uint8>& expected)
    {
        return FileMatches(files, path, reinterpret_cast<const char*>(expected.TypedData()), expected.Size());
    }

    bool WriteExactFile(vanguard::filesystem::Manager& files, const red::AbsolutePath& path,
                        const red::DynArray<Uint8>& bytes)
    {
        return WriteExactFile(files, path, reinterpret_cast<const char*>(bytes.TypedData()), bytes.Size());
    }

    bool DatabaseEntryMatchesSize(const resdb::Database& database, const res::ResourcePath path, const Uint32 size)
    {
        const resdb::Resource* const resource = database.LookupResource(path);
        return resource != nullptr && resource->timestamp != 0ull && resource->totalSize == size;
    }

    bool DatabaseEntriesReadyForBuild(const resdb::Database& database,
                                      const red::DynArray<res::ResourcePath>& paths)
    {
        for (const res::ResourcePath path : paths)
        {
            const resdb::Resource* const resource = database.LookupResource(path);
            if (resource == nullptr || resource->timestamp == 0ull ||
                resource->status != dependency::prv::ScanResult::OK)
            {
                return false;
            }
        }
        return true;
    }

    bool LogicalAssetCommitSucceeded(const vanguard::assets::AssetFileTransactionResult result)
    {
        return result == vanguard::assets::AssetFileTransactionResult::Success ||
               result == vanguard::assets::AssetFileTransactionResult::CommittedCleanupPending;
    }

    bool SerializeCsvImage(const res::ResourcePath path, const char* const text, red::DynArray<Uint8>& outBytes)
    {
        auto resource = CreateHandle<C2dArray>();
        resource->ParseData(text);
        res::ResourceSavingOptions options;
        options.m_targetPath = path;
        options.m_loadDeferredDataBuffers = false;
        return res::SerializeResource(resource, path, options, outBytes);
    }

    void AddDepotMapping(red::DynArray<res::prv::DepotMapping>& mappings, const red::AbsolutePath& diskRoot,
                         const char* const mountPath, const Uint32 level = 0u)
    {
        res::prv::DepotMapping mapping;
        mapping.diskPath = diskRoot;
        mapping.mountPath = mountPath;
        mapping.level = level;
        mappings.PushBack(std::move(mapping));
    }

    void QueueCommittedPathsToWatcher(
        void* const context, const vanguard::containers::ArraySpan<const res::ResourcePath> paths) noexcept
    {
        auto* const watcher = static_cast<resdb::FileSystemWatcher*>(context);
        if (watcher == nullptr)
            return;
        for (const res::ResourcePath path : paths)
            watcher->PushResourcePath(path);
    }

    void QueueCommittedPathsToDatabase(
        void* const context, const vanguard::containers::ArraySpan<const res::ResourcePath> paths) noexcept
    {
        auto* const database = static_cast<resdb::Database*>(context);
        if (database == nullptr || paths.Empty())
            return;
        database->EnqueueCommittedPaths(red::ArraySpan<const res::ResourcePath>(paths.Data(), paths.Size()));
    }

    int RunGate0Proof(const red::AbsolutePath& scratchRoot)
    {
        auto& files = vanguard::filesystem::GetManager();
        const red::AbsolutePath authoredRoot = scratchRoot.AddDirPath("authored");
        if (!files.CreatePath(authoredRoot))
        {
            std::fprintf(stderr, "ERROR gate0=directories\n");
            return 20;
        }

        red::DynArray<res::prv::DepotMapping> mappings{red::PoolEngine()};
        AddDepotMapping(mappings, authoredRoot, "");
        vanguard::asset_host::DurableResourceDepot durableDepot(mappings);
        vanguard::assets::AssetFileRecoveryReport recovery;
        const red::AbsolutePath journalPath = scratchRoot.AddFilePath("authored-resource-save.vtxn");
        if (durableDepot.Initialize(files, authoredRoot, journalPath, &recovery) !=
            vanguard::assets::AssetFileTransactionResult::Success)
        {
            std::fprintf(stderr, "ERROR gate0=initialize result=%u\n",
                         static_cast<Uint32>(durableDepot.LastTransactionResult()));
            return 21;
        }

        Register2DArraySaver();
        serialization::ISaver::RegisterCustomFactory<Gate0RejectingSaver>("gate0fail");

        static constexpr char GuardBytes[] = "known-good-authored-resource\n";
        const res::ResourcePath guardPath = res::ResourcePath::Build("guard.gate0fail");
        const red::AbsolutePath guardAbsolute = durableDepot.ResolveWritableResourcePath(guardPath);
        if (!WriteExactFile(files, guardAbsolute, GuardBytes, static_cast<Uint32>(sizeof(GuardBytes) - 1u)))
        {
            std::fprintf(stderr, "ERROR gate0=guard-fixture\n");
            return 22;
        }

        auto guardResource = CreateHandle<C2dArray>();
        guardResource->ParseData("name;value\nold;1\n");
        guardResource->Internal_SetPath(guardPath);
        guardResource->MarkModified();
        res::ResourceSavingOptions guardOptions;
        guardOptions.m_depot = &durableDepot;
        guardOptions.m_targetPath = guardPath;
        guardOptions.m_loadDeferredDataBuffers = false;
        const bool rejectedUnsupported = !res::SaveResource(guardResource, guardOptions);
        const bool guardPreserved =
            FileMatches(files, guardAbsolute, GuardBytes, static_cast<Uint32>(sizeof(GuardBytes) - 1u));
        const bool failedSaveRemainedModified = guardResource->IsModified();

        const res::ResourcePath resourcePath = res::ResourcePath::Build("gate0.csv");
        auto first = CreateHandle<C2dArray>();
        first->ParseData("name;value\nalpha;1\n");
        first->Internal_SetPath(resourcePath);
        first->MarkModified();
        res::ResourceSavingOptions firstOptions;
        firstOptions.m_depot = &durableDepot;
        firstOptions.m_targetPath = resourcePath;
        firstOptions.m_loadDeferredDataBuffers = false;
        const bool firstSaved = res::SaveResource(first, firstOptions);
        const bool firstUnmodified = !first->IsModified();

        auto replacement = CreateHandle<C2dArray>();
        replacement->ParseData("name;value\nalpha;2\nbeta;3\n");
        replacement->Internal_SetPath(resourcePath);
        replacement->MarkModified();
        res::ResourceSavingOptions replacementOptions;
        replacementOptions.m_depot = &durableDepot;
        replacementOptions.m_targetPath = resourcePath;
        replacementOptions.m_loadDeferredDataBuffers = false;
        const bool replacementSaved = res::SaveResource(replacement, replacementOptions);
        const bool replacementUnmodified = !replacement->IsModified();

        static constexpr char ExpectedReplacement[] = "name;value\nalpha;2\nbeta;3\n";
        const red::AbsolutePath resourceAbsolute = durableDepot.ResolveWritableResourcePath(resourcePath);
        const bool exactReplacement = FileMatches(files, resourceAbsolute, ExpectedReplacement,
                                                  static_cast<Uint32>(sizeof(ExpectedReplacement) - 1u));
        const red::AbsolutePath stagePath = red::AbsolutePath::CreateFilePath(
            red::String(resourceAbsolute.AsChar()) + vanguard::assets::AssetTransactionStageSuffix);
        const red::AbsolutePath backupPath = red::AbsolutePath::CreateFilePath(
            red::String(resourceAbsolute.AsChar()) + vanguard::assets::AssetTransactionBackupSuffix);
        const bool artifactsAbsent = !files.FileExist(stagePath) && !files.FileExist(backupPath) && !files.FileExist(journalPath);

        const auto shutdown = durableDepot.Shutdown();
        const bool recoveryHealthy = !recovery.journalFound || recovery.rolledBack || recovery.completedCleanup;
        const bool success = rejectedUnsupported && guardPreserved && failedSaveRemainedModified && firstSaved && firstUnmodified &&
                             replacementSaved && replacementUnmodified && exactReplacement && artifactsAbsent &&
                             recoveryHealthy && shutdown == vanguard::assets::AssetFileTransactionResult::Success;
        std::printf(
            "GATE0_RESULT success=%u rejected_save_preserved=%u failed_save_modified=%u first_save=%u replace_save=%u exact_replacement=%u artifacts_absent=%u recovery_found=%u recovery_rolled_back=%u\n",
            success ? 1u : 0u,
            (rejectedUnsupported && guardPreserved) ? 1u : 0u,
            failedSaveRemainedModified ? 1u : 0u,
            (firstSaved && firstUnmodified) ? 1u : 0u,
            (replacementSaved && replacementUnmodified) ? 1u : 0u,
            exactReplacement ? 1u : 0u,
            artifactsAbsent ? 1u : 0u,
            recovery.journalFound ? 1u : 0u,
            recovery.rolledBack ? 1u : 0u);
        return success ? 0 : 23;
    }

    int ArmGate0Recovery(const red::AbsolutePath& scratchRoot)
    {
        auto& files = vanguard::filesystem::GetManager();
        const red::AbsolutePath authoredRoot = scratchRoot.AddDirPath("authored");
        const red::AbsolutePath target = authoredRoot.AddFilePath("gate0.csv");
        const red::AbsolutePath journal = scratchRoot.AddFilePath("authored-resource-save.vtxn");
        if (!files.FileExist(target))
        {
            std::fprintf(stderr, "ERROR gate0-arm=missing-canonical\n");
            return 30;
        }

        const red::AbsolutePath roots[] = {authoredRoot};
        vanguard::assets::AssetFileTransactionConfig config;
        config.journalPath = journal;
        config.writableRoots = {roots, 1u};
        config.assertionRoots = {roots, 1u};
        vanguard::assets::AssetFileRecoveryReport recovery;
        vanguard::assets::AssetFileTransaction transaction;
        if (transaction.Initialize(files, config, &recovery) != vanguard::assets::AssetFileTransactionResult::Success)
        {
            std::fprintf(stderr, "ERROR gate0-arm=initialize\n");
            return 31;
        }

        static constexpr char InterruptedBytes[] = "interrupted-gate0-publication\n";
        const auto interrupted = vanguard::containers::ArraySpan<const vanguard::u8>(
            reinterpret_cast<const vanguard::u8*>(InterruptedBytes), static_cast<vanguard::u32>(sizeof(InterruptedBytes) - 1u));
        const bool applied = transaction.Begin() == vanguard::assets::AssetFileTransactionResult::Success &&
                             transaction.StageWrite(target, interrupted) == vanguard::assets::AssetFileTransactionResult::Success &&
                             transaction.Prepare() == vanguard::assets::AssetFileTransactionResult::Success &&
                             transaction.ApplyPrepared() == vanguard::assets::AssetFileTransactionResult::Success;
        const red::AbsolutePath stage = red::AbsolutePath::CreateFilePath(
            red::String(target.AsChar()) + vanguard::assets::AssetTransactionStageSuffix);
        const red::AbsolutePath backup = red::AbsolutePath::CreateFilePath(
            red::String(target.AsChar()) + vanguard::assets::AssetTransactionBackupSuffix);
        const bool armed = applied && FileMatches(files, target, InterruptedBytes, static_cast<Uint32>(sizeof(InterruptedBytes) - 1u)) &&
                           !files.FileExist(stage) && files.FileExist(backup) && files.FileExist(journal);
        std::printf("GATE0_ARM_RESULT success=%u applying_target=%u backup_present=%u journal_present=%u prior_recovery=%u\n",
                    armed ? 1u : 0u,
                    FileMatches(files, target, InterruptedBytes, static_cast<Uint32>(sizeof(InterruptedBytes) - 1u)) ? 1u : 0u,
                    files.FileExist(backup) ? 1u : 0u,
                    files.FileExist(journal) ? 1u : 0u,
                    recovery.journalFound ? 1u : 0u);
        return armed ? 0 : 32;
    }

    int RunGate0CohortProof(const red::AbsolutePath& scratchRoot)
    {
        auto& files = vanguard::filesystem::GetManager();
        const red::AbsolutePath authoredRoot = scratchRoot.AddDirPath("cohort-authored");
        if (!files.CreatePath(authoredRoot))
        {
            std::fprintf(stderr, "ERROR gate0-cohort=directories\n");
            return 40;
        }

        Register2DArraySaver();
        const res::ResourcePath pathA = res::ResourcePath::Build("cohort_a.csv");
        const res::ResourcePath pathB = res::ResourcePath::Build("cohort_b.csv");
        red::DynArray<Uint8> oldA{red::PoolBackend()};
        red::DynArray<Uint8> oldB{red::PoolBackend()};
        red::DynArray<Uint8> stableA{red::PoolBackend()};
        red::DynArray<Uint8> stableB{red::PoolBackend()};
        if (!SerializeCsvImage(pathA, "name;value\nold-a;1\n", oldA) ||
            !SerializeCsvImage(pathB, "name;value\nold-b;2\n", oldB) ||
            !SerializeCsvImage(pathA, "name;value\nstable-a;10\n", stableA) ||
            !SerializeCsvImage(pathB, "name;value\nstable-b;20\n", stableB))
        {
            std::fprintf(stderr, "ERROR gate0-cohort=serialize\n");
            return 41;
        }

        red::DynArray<res::prv::DepotMapping> mappings{red::PoolEngine()};
        AddDepotMapping(mappings, authoredRoot, "");
        vanguard::asset_host::DurableResourceDepot durableDepot(mappings);
        const red::AbsolutePath absoluteA = durableDepot.ResolveWritableResourcePath(pathA);
        const red::AbsolutePath absoluteB = durableDepot.ResolveWritableResourcePath(pathB);
        const red::AbsolutePath journal = scratchRoot.AddFilePath("authored-cohort-save.vtxn");
        vanguard::assets::AssetFileRecoveryReport recovery;
        if (durableDepot.Initialize(files, authoredRoot, journal, &recovery) !=
            vanguard::assets::AssetFileTransactionResult::Success)
        {
            std::fprintf(stderr, "ERROR gate0-cohort=initialize result=%u\n",
                         static_cast<Uint32>(durableDepot.LastTransactionResult()));
            return 42;
        }
        resdb::FileSystemWatcher watcher(&files, &durableDepot);
        durableDepot.SetCommittedPathSink(&watcher, &QueueCommittedPathsToWatcher);

        const bool recoveryPreimage = !recovery.journalFound ||
                                      (recovery.rolledBack && FileMatches(files, absoluteA, stableA) && FileMatches(files, absoluteB, stableB));
        if (!files.FileExist(absoluteA) && !WriteExactFile(files, absoluteA, oldA))
            return 43;
        if (!files.FileExist(absoluteB) && !WriteExactFile(files, absoluteB, oldB))
            return 44;

        const bool beforeAOld = FileMatches(files, absoluteA, oldA);
        const bool beforeBOld = FileMatches(files, absoluteB, oldB);
        const bool beforeAStable = FileMatches(files, absoluteA, stableA);
        const bool beforeBStable = FileMatches(files, absoluteB, stableB);
        const bool staged = durableDepot.BeginCohort() == vanguard::assets::AssetFileTransactionResult::Success &&
                            durableDepot.StageCohortWrite(pathA, {stableA.TypedData(), stableA.Size()}) ==
                                vanguard::assets::AssetFileTransactionResult::Success &&
                            durableDepot.StageCohortWrite(pathB, {stableB.TypedData(), stableB.Size()}) ==
                                vanguard::assets::AssetFileTransactionResult::Success &&
                            durableDepot.PrepareCohort() == vanguard::assets::AssetFileTransactionResult::Success;
        const bool preparedOldVisible = staged &&
                                        ((beforeAOld && FileMatches(files, absoluteA, oldA)) ||
                                         (beforeAStable && FileMatches(files, absoluteA, stableA))) &&
                                        ((beforeBOld && FileMatches(files, absoluteB, oldB)) ||
                                         (beforeBStable && FileMatches(files, absoluteB, stableB)));
        const bool noPathsBeforeCommit = watcher.GetChangedFiles().Empty();

        const bool applied = staged && durableDepot.ApplyCohort() == vanguard::assets::AssetFileTransactionResult::Success;
        std::atomic<bool> readerStarted{false};
        std::atomic<bool> readerFinished{false};
        std::atomic<bool> readerSawA{false};
        std::thread reader;
        bool readerBlocked = false;
        if (applied)
        {
            reader = std::thread([&]()
            {
                readerStarted.store(true, std::memory_order_release);
                readerSawA.store(durableDepot.ResourceExists(pathA), std::memory_order_release);
                readerFinished.store(true, std::memory_order_release);
            });
            while (!readerStarted.load(std::memory_order_acquire))
                std::this_thread::yield();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            readerBlocked = !readerFinished.load(std::memory_order_acquire);
        }

        const bool published = applied && durableDepot.PublishCohort() == vanguard::assets::AssetFileTransactionResult::Success;
        const auto committedPaths = watcher.GetChangedFiles();
        const bool committedPathsExact = committedPaths.Size() == 2u && committedPaths[0] == pathA && committedPaths[1] == pathB;
        if (reader.joinable())
            reader.join();
        const bool readerReleased = readerFinished.load(std::memory_order_acquire) && readerSawA.load(std::memory_order_acquire);
        const bool completed = published && durableDepot.CompleteCohort() == vanguard::assets::AssetFileTransactionResult::Success;
        const bool exactStable = FileMatches(files, absoluteA, stableA) && FileMatches(files, absoluteB, stableB);

        const red::AbsolutePath stageA = red::AbsolutePath::CreateFilePath(
            red::String(absoluteA.AsChar()) + vanguard::assets::AssetTransactionStageSuffix);
        const red::AbsolutePath stageB = red::AbsolutePath::CreateFilePath(
            red::String(absoluteB.AsChar()) + vanguard::assets::AssetTransactionStageSuffix);
        const red::AbsolutePath backupA = red::AbsolutePath::CreateFilePath(
            red::String(absoluteA.AsChar()) + vanguard::assets::AssetTransactionBackupSuffix);
        const red::AbsolutePath backupB = red::AbsolutePath::CreateFilePath(
            red::String(absoluteB.AsChar()) + vanguard::assets::AssetTransactionBackupSuffix);
        const bool artifactsAbsent = !files.FileExist(stageA) && !files.FileExist(stageB) &&
                                     !files.FileExist(backupA) && !files.FileExist(backupB) && !files.FileExist(journal);
        watcher.PushResourcePath(res::ResourcePath::Build("cohort_a.csv.vtxn-stage"));
        watcher.PushResourcePath(res::ResourcePath::Build("cohort_b.csv.vtxn-backup"));
        watcher.PushResourcePath(res::ResourcePath::Build("authored-cohort-save.vtxn"));
        watcher.PushResourcePath(res::ResourcePath::Build("authored-cohort-save.vtxn.stage"));
        const auto artifactPaths = watcher.GetChangedFiles();
        const bool watcherFiltered = artifactPaths.Empty() &&
                                     resdb::IsTransactionArtifactPath("x.csv.vtxn-stage") &&
                                     resdb::IsTransactionArtifactPath("x.csv.vtxn-backup") &&
                                     resdb::IsTransactionArtifactPath("x.vtxn") &&
                                     resdb::IsTransactionArtifactPath("x.vtxn.stage") &&
                                     !resdb::IsTransactionArtifactPath("x.csv");
        const auto shutdown = durableDepot.Shutdown();
        const bool success = recoveryPreimage && staged && preparedOldVisible && noPathsBeforeCommit && applied && readerBlocked &&
                             published && committedPathsExact && readerReleased && completed && exactStable && artifactsAbsent && watcherFiltered &&
                             shutdown == vanguard::assets::AssetFileTransactionResult::Success;
        std::printf(
            "GATE0_COHORT_RESULT success=%u staged=%u prepared_old_visible=%u no_paths_before_commit=%u reader_blocked=%u committed_paths=%u reader_released=%u exact_cohort=%u artifacts_absent=%u watcher_filtered=%u recovery_found=%u recovery_rolled_back=%u recovery_preimage=%u\n",
            success ? 1u : 0u,
            staged ? 1u : 0u,
            preparedOldVisible ? 1u : 0u,
            noPathsBeforeCommit ? 1u : 0u,
            readerBlocked ? 1u : 0u,
            committedPathsExact ? 1u : 0u,
            readerReleased ? 1u : 0u,
            exactStable ? 1u : 0u,
            artifactsAbsent ? 1u : 0u,
            watcherFiltered ? 1u : 0u,
            recovery.journalFound ? 1u : 0u,
            recovery.rolledBack ? 1u : 0u,
            recoveryPreimage ? 1u : 0u);
        return success ? 0 : 45;
    }

    int ArmGate0CohortRecovery(const red::AbsolutePath& scratchRoot)
    {
        auto& files = vanguard::filesystem::GetManager();
        const red::AbsolutePath authoredRoot = scratchRoot.AddDirPath("cohort-authored");
        const res::ResourcePath pathA = res::ResourcePath::Build("cohort_a.csv");
        const res::ResourcePath pathB = res::ResourcePath::Build("cohort_b.csv");
        red::DynArray<res::prv::DepotMapping> mappings{red::PoolEngine()};
        AddDepotMapping(mappings, authoredRoot, "");
        vanguard::asset_host::DurableResourceDepot durableDepot(mappings);
        const red::AbsolutePath absoluteA = durableDepot.ResolveWritableResourcePath(pathA);
        const red::AbsolutePath absoluteB = durableDepot.ResolveWritableResourcePath(pathB);
        if (!files.FileExist(absoluteA) || !files.FileExist(absoluteB))
        {
            std::fprintf(stderr, "ERROR gate0-cohort-arm=missing-canonical\n");
            return 50;
        }

        Register2DArraySaver();
        red::DynArray<Uint8> interruptedA{red::PoolBackend()};
        red::DynArray<Uint8> interruptedB{red::PoolBackend()};
        if (!SerializeCsvImage(pathA, "name;value\ninterrupted-a;100\n", interruptedA) ||
            !SerializeCsvImage(pathB, "name;value\ninterrupted-b;200\n", interruptedB))
            return 51;

        const red::AbsolutePath journal = scratchRoot.AddFilePath("authored-cohort-save.vtxn");
        vanguard::assets::AssetFileRecoveryReport recovery;
        if (durableDepot.Initialize(files, authoredRoot, journal, &recovery) !=
            vanguard::assets::AssetFileTransactionResult::Success || recovery.journalFound)
            return 52;

        const bool applied = durableDepot.BeginCohort() == vanguard::assets::AssetFileTransactionResult::Success &&
                             durableDepot.StageCohortWrite(pathA, {interruptedA.TypedData(), interruptedA.Size()}) ==
                                 vanguard::assets::AssetFileTransactionResult::Success &&
                             durableDepot.StageCohortWrite(pathB, {interruptedB.TypedData(), interruptedB.Size()}) ==
                                 vanguard::assets::AssetFileTransactionResult::Success &&
                             durableDepot.PrepareCohort() == vanguard::assets::AssetFileTransactionResult::Success &&
                             durableDepot.ApplyCohort() == vanguard::assets::AssetFileTransactionResult::Success;
        const red::AbsolutePath backupA = red::AbsolutePath::CreateFilePath(
            red::String(absoluteA.AsChar()) + vanguard::assets::AssetTransactionBackupSuffix);
        const red::AbsolutePath backupB = red::AbsolutePath::CreateFilePath(
            red::String(absoluteB.AsChar()) + vanguard::assets::AssetTransactionBackupSuffix);
        const bool armed = applied && FileMatches(files, absoluteA, interruptedA) && FileMatches(files, absoluteB, interruptedB) &&
                           files.FileExist(backupA) && files.FileExist(backupB) && files.FileExist(journal);
        std::printf("GATE0_COHORT_ARM_RESULT success=%u applying_a=%u applying_b=%u backups_present=%u journal_present=%u\n",
                    armed ? 1u : 0u,
                    FileMatches(files, absoluteA, interruptedA) ? 1u : 0u,
                    FileMatches(files, absoluteB, interruptedB) ? 1u : 0u,
                    (files.FileExist(backupA) && files.FileExist(backupB)) ? 1u : 0u,
                    files.FileExist(journal) ? 1u : 0u);
        std::fflush(stdout);
        if (armed)
            std::_Exit(0);
        return 53;
    }

    int RunGate0MappedDepotProof(const red::AbsolutePath& scratchRoot)
    {
        auto& files = vanguard::filesystem::GetManager();
        const red::AbsolutePath firstRoot = scratchRoot.AddDirPath("mapped-first");
        const red::AbsolutePath secondRoot = scratchRoot.AddDirPath("mapped-second");
        if (!files.CreatePath(firstRoot) || !files.CreatePath(secondRoot))
            return 60;

        Register2DArraySaver();
        const res::ResourcePath ownedPath = res::ResourcePath::Build("base\\owned.csv");
        const res::ResourcePath newPath = res::ResourcePath::Build("base\\new.csv");
        red::DynArray<Uint8> oldOwned{red::PoolBackend()};
        red::DynArray<Uint8> replacementOwned{red::PoolBackend()};
        red::DynArray<Uint8> createdNew{red::PoolBackend()};
        if (!SerializeCsvImage(ownedPath, "name;value\nold-owned;1\n", oldOwned) ||
            !SerializeCsvImage(ownedPath, "name;value\nreplacement-owned;2\n", replacementOwned) ||
            !SerializeCsvImage(newPath, "name;value\ncreated-new;3\n", createdNew))
            return 61;

        const red::AbsolutePath firstOwned = firstRoot.AddFilePath("owned.csv");
        const red::AbsolutePath secondOwned = secondRoot.AddFilePath("owned.csv");
        const red::AbsolutePath firstNew = firstRoot.AddFilePath("new.csv");
        const red::AbsolutePath secondNew = secondRoot.AddFilePath("new.csv");
        if (!WriteExactFile(files, firstOwned, oldOwned))
            return 62;

        red::DynArray<res::prv::DepotMapping> mappings{red::PoolEngine()};
        AddDepotMapping(mappings, firstRoot, "base\\", 1u);
        AddDepotMapping(mappings, secondRoot, "base\\", 2u);
        vanguard::asset_host::DurableResourceDepot depot(mappings);
        const red::AbsolutePath roots[] = {firstRoot, secondRoot};
        const red::AbsolutePath journal = scratchRoot.AddFilePath("mapped-authored-save.vtxn");
        vanguard::assets::AssetFileRecoveryReport recovery;
        if (depot.Initialize(files, {roots, 2u}, journal, &recovery) != vanguard::assets::AssetFileTransactionResult::Success ||
            recovery.journalFound)
            return 63;

        const bool genericWouldChooseLast = depot.TranslateResourcePath(ownedPath) == secondOwned;
        const bool existingWritesToOwner = depot.ResolveWritableResourcePath(ownedPath) == firstOwned;
        const bool newWritesToLast = depot.ResolveWritableResourcePath(newPath) == secondNew;

        res::SetEditorDepot(&depot);
        const bool globalInstalled = res::GetEditorDepot() == &depot;

        auto replacement = CreateHandle<C2dArray>();
        replacement->ParseData("name;value\nreplacement-owned;2\n");
        replacement->Internal_SetPath(ownedPath);
        replacement->MarkModified();
        res::ResourceSavingOptions replacementOptions;
        replacementOptions.m_loadDeferredDataBuffers = false;
        const bool replacementDefaultDepot = replacementOptions.m_depot == &depot;
        const bool replacementSaved = res::SaveResource(replacement, replacementOptions);

        auto created = CreateHandle<C2dArray>();
        created->ParseData("name;value\ncreated-new;3\n");
        created->Internal_SetPath(newPath);
        created->MarkModified();
        res::ResourceSavingOptions createOptions;
        createOptions.m_loadDeferredDataBuffers = false;
        const bool createDefaultDepot = createOptions.m_depot == &depot;
        const bool createSaved = res::SaveResource(created, createOptions);

        res::SetEditorDepot(nullptr);
        const bool exactExistingOwner = FileMatches(files, firstOwned, replacementOwned) && !files.FileExist(secondOwned);
        const bool exactNewLast = FileMatches(files, secondNew, createdNew) && !files.FileExist(firstNew);
        const bool unmodifiedAfterPublish = !replacement->IsModified() && !created->IsModified();
        const bool artifactsAbsent = !files.FileExist(journal) &&
                                     !files.FileExist(red::AbsolutePath::CreateFilePath(red::String(firstOwned.AsChar()) +
                                                                                       vanguard::assets::AssetTransactionStageSuffix)) &&
                                     !files.FileExist(red::AbsolutePath::CreateFilePath(red::String(secondNew.AsChar()) +
                                                                                       vanguard::assets::AssetTransactionStageSuffix));
        const auto shutdown = depot.Shutdown();
        const bool success = genericWouldChooseLast && existingWritesToOwner && newWritesToLast && globalInstalled &&
                             replacementDefaultDepot && createDefaultDepot && replacementSaved && createSaved &&
                             exactExistingOwner && exactNewLast && unmodifiedAfterPublish && artifactsAbsent &&
                             shutdown == vanguard::assets::AssetFileTransactionResult::Success;
        std::printf(
            "GATE0_MAPPED_RESULT success=%u generic_last=%u existing_owner=%u new_last=%u global_durable=%u default_depot=%u exact_existing=%u exact_new=%u unmodified=%u artifacts_absent=%u\n",
            success ? 1u : 0u,
            genericWouldChooseLast ? 1u : 0u,
            existingWritesToOwner ? 1u : 0u,
            newWritesToLast ? 1u : 0u,
            globalInstalled ? 1u : 0u,
            (replacementDefaultDepot && createDefaultDepot) ? 1u : 0u,
            exactExistingOwner ? 1u : 0u,
            exactNewLast ? 1u : 0u,
            unmodifiedAfterPublish ? 1u : 0u,
            artifactsAbsent ? 1u : 0u);
        return success ? 0 : 64;
    }

    int RunStage11DatabaseProof(const red::AbsolutePath& scratchRoot)
    {
        auto& files = vanguard::filesystem::GetManager();
        const red::AbsolutePath authoredRoot = scratchRoot.AddDirPath("stage11-authored");
        const red::AbsolutePath cookedRoot = scratchRoot.AddDirPath("stage11-cooked");
        const red::AbsolutePath cacheRoot = scratchRoot.AddDirPath("stage11-cache");
        if (!files.CreatePath(authoredRoot) || !files.CreatePath(cookedRoot) || !files.CreatePath(cacheRoot))
            return 70;

        Register2DArraySaver();
        const res::ResourcePath pathA = res::ResourcePath::Build("stage11_a.csv");
        const res::ResourcePath pathB = res::ResourcePath::Build("stage11_b.csv");
        const res::ResourcePath tweakPath = res::ResourcePath::Build("stage11_dependency.tweak");
        const res::ResourcePath unsupportedCsvPath = res::ResourcePath::Build("stage11_unsupported.csv");
        const res::ResourcePath oldRecursiveDependency = res::ResourcePath::Build("base\\stage11_dep_b.csv");
        const res::ResourcePath newRecursiveDependency = res::ResourcePath::Build("base\\stage11_dep_c.csv");
        const res::ResourcePath tweakDependency = res::ResourcePath::Build("base\\stage11_tweak_dep.csv");
        red::DynArray<Uint8> baseA{red::PoolBackend()};
        red::DynArray<Uint8> baseB{red::PoolBackend()};
        red::DynArray<Uint8> nextA{red::PoolBackend()};
        red::DynArray<Uint8> nextB{red::PoolBackend()};
        if (!SerializeCsvImage(pathA, "name;value\ndep;base\\stage11_dep_b.csv\nbase-a;1\n", baseA) ||
            !SerializeCsvImage(pathB, "name;value\nbase-b;2\n", baseB) ||
            !SerializeCsvImage(pathA, "name;value\ndep;base\\stage11_dep_c.csv\nnext-a-longer;100\nextra;101\n", nextA) ||
            !SerializeCsvImage(pathB, "name;value\nnext-b-longer;200\nextra;201\nextra2;202\n", nextB))
            return 71;

        red::DynArray<res::prv::DepotMapping> mappings{red::PoolEngine()};
        AddDepotMapping(mappings, authoredRoot, "");
        vanguard::asset_host::DurableResourceDepot durableDepot(mappings);
        const red::AbsolutePath absoluteA = authoredRoot.AddFilePath("stage11_a.csv");
        const red::AbsolutePath absoluteB = authoredRoot.AddFilePath("stage11_b.csv");
        const red::AbsolutePath journal = scratchRoot.AddFilePath("stage11-resource-database.vtxn");
        vanguard::assets::AssetFileRecoveryReport recovery;
        if (durableDepot.Initialize(files, authoredRoot, journal, &recovery) !=
            vanguard::assets::AssetFileTransactionResult::Success)
            return 72;

        const bool recoveryBeforeDatabase = !recovery.journalFound ||
                                            (recovery.rolledBack && FileMatches(files, absoluteA, baseA) &&
                                             FileMatches(files, absoluteB, baseB));
        if (!recoveryBeforeDatabase)
        {
            static_cast<void>(durableDepot.Shutdown());
            return 73;
        }
        if (!recovery.journalFound && (!WriteExactFile(files, absoluteA, baseA) || !WriteExactFile(files, absoluteB, baseB)))
        {
            static_cast<void>(durableDepot.Shutdown());
            return 74;
        }

        static constexpr char TweakBytes[] = "setting = \"base\\\\stage11_tweak_dep.csv\"\n";
        static constexpr char UnsupportedCr2wCsv[] = {'C', 'R', '2', 'W', 0, 0, 0, 0};
        if (!WriteExactFile(files, authoredRoot.AddFilePath(tweakPath.ToStringView()), TweakBytes,
                            static_cast<Uint32>(sizeof(TweakBytes) - 1u)) ||
            !WriteExactFile(files, authoredRoot.AddFilePath(unsupportedCsvPath.ToStringView()), UnsupportedCr2wCsv,
                            static_cast<Uint32>(sizeof(UnsupportedCr2wCsv))))
        {
            static_cast<void>(durableDepot.Shutdown());
            return 75;
        }

        const res::ResourcePath artifactPaths[] = {
            res::ResourcePath::Build("ignored.csv.vtxn-stage"),
            res::ResourcePath::Build("ignored.csv.vtxn-backup"),
            res::ResourcePath::Build("ignored.vtxn"),
            res::ResourcePath::Build("ignored.vtxn.stage")};
        static constexpr char ArtifactBytes[] = "transaction-artifact\n";
        for (const res::ResourcePath artifactPath : artifactPaths)
        {
            if (!WriteExactFile(files, authoredRoot.AddFilePath(artifactPath.ToStringView()), ArtifactBytes,
                                static_cast<Uint32>(sizeof(ArtifactBytes) - 1u)))
            {
                static_cast<void>(durableDepot.Shutdown());
                return 75;
            }
        }

        resdb::Database database;
        const red::AbsolutePath databasePath = cacheRoot.AddFilePath(resdb::Database::GetDefaultFilename());
        database.Initialise(durableDepot, databasePath);
        const bool initialDatabaseExact = DatabaseEntryMatchesSize(database, pathA, baseA.Size()) &&
                                          DatabaseEntryMatchesSize(database, pathB, baseB.Size());
        const auto& initialRecursive = database.GetRecursiveForwardPaths(pathA);
        const bool initialRecursiveExact = initialRecursive.Exist(oldRecursiveDependency) &&
                                           !initialRecursive.Exist(newRecursiveDependency);
        const auto& tweakRecursive = database.GetRecursiveForwardPaths(tweakPath);
        const bool tweakScanExact = tweakRecursive.Exist(tweakDependency);
        const resdb::Resource* const unsupportedCsv = database.LookupResource(unsupportedCsvPath);
        const bool unsupportedCsvFailedClosed = unsupportedCsv != nullptr &&
                                                unsupportedCsv->status == dependency::prv::ScanResult::DATA_ERROR;
        bool initialArtifactsFiltered = true;
        for (const res::ResourcePath artifactPath : artifactPaths)
            initialArtifactsFiltered = initialArtifactsFiltered && database.LookupResource(artifactPath) == nullptr;

        database.InitFileSystemWatcher();
        durableDepot.SetCommittedPathSink(&database, &QueueCommittedPathsToDatabase);
        for (const res::ResourcePath artifactPath : artifactPaths)
            database.EnqueueCommittedPaths(red::ArraySpan<const res::ResourcePath>(&artifactPath, 1u));
        database.Update();
        bool incrementalArtifactsFiltered = true;
        for (const res::ResourcePath artifactPath : artifactPaths)
            incrementalArtifactsFiltered = incrementalArtifactsFiltered && database.LookupResource(artifactPath) == nullptr;

        const bool staged = durableDepot.BeginCohort() == vanguard::assets::AssetFileTransactionResult::Success &&
                            durableDepot.StageCohortWrite(pathA, {nextA.TypedData(), nextA.Size()}) ==
                                vanguard::assets::AssetFileTransactionResult::Success &&
                            durableDepot.StageCohortWrite(pathB, {nextB.TypedData(), nextB.Size()}) ==
                                vanguard::assets::AssetFileTransactionResult::Success &&
                            durableDepot.PrepareCohort() == vanguard::assets::AssetFileTransactionResult::Success;
        const bool applied = staged && durableDepot.ApplyCohort() == vanguard::assets::AssetFileTransactionResult::Success;
        const bool published = applied && durableDepot.PublishCohort() == vanguard::assets::AssetFileTransactionResult::Success;
        const bool databaseStillPrecommit = published && DatabaseEntryMatchesSize(database, pathA, baseA.Size()) &&
                                            DatabaseEntryMatchesSize(database, pathB, baseB.Size()) &&
                                            files.GetFileSize(absoluteA) == nextA.Size() && files.GetFileSize(absoluteB) == nextB.Size();

        const auto completionResult = published ? durableDepot.CompleteCohort()
                                                : vanguard::assets::AssetFileTransactionResult::InvalidState;
        const bool completed = published && LogicalAssetCommitSucceeded(completionResult);
        if (completed)
            database.Update();
        const bool databaseConverged = completed && DatabaseEntryMatchesSize(database, pathA, nextA.Size()) &&
                                       DatabaseEntryMatchesSize(database, pathB, nextB.Size());
        const auto& updatedRecursive = database.GetRecursiveForwardPaths(pathA);
        const bool recursiveConverged = databaseConverged && !updatedRecursive.Exist(oldRecursiveDependency) &&
                                        updatedRecursive.Exist(newRecursiveDependency);

        red::DynArray<res::ResourcePath> admitted{red::PoolBackend()};
        admitted.PushBack(pathA);
        admitted.PushBack(pathB);
        red::DynArray<res::ResourcePath> rejected{red::PoolBackend()};
        rejected.PushBack(unsupportedCsvPath);
        const bool failedScanRejected = !DatabaseEntriesReadyForBuild(database, rejected);
        const bool taskAdmissionAllowed = databaseConverged && recursiveConverged && completed &&
                                          DatabaseEntriesReadyForBuild(database, admitted);
        bool tasksSucceeded = false;
        {
            ScratchDepot cookedDepot(cookedRoot, "Stage11Cooked");
            dependency::Cache sourceCache(&durableDepot);
            dependency::Cache cookedCache(&cookedDepot);
            dependency::BinaryDatabase dependencyDatabase(cacheRoot.AddFilePath("Stage11Dependencies.cache"), 0);
            data::GlobalErrorHandler errorHandler;
            HostSystemContext context(durableDepot, cookedDepot, sourceCache, cookedCache, dependencyDatabase,
                                      errorHandler, &database);
            HostReporter reporter;
            data::TaskSystem taskSystem(context, &cooker::CreateTask, reporter, 1);
            if (taskAdmissionAllowed)
                taskSystem.Build(admitted);
            const auto tasks = taskSystem.GetAllTasksInOrderOfExecution();
            tasksSucceeded = taskAdmissionAllowed && tasks.Size() == 2u;
            for (const auto& task : tasks)
                tasksSucceeded = tasksSucceeded && task && task->Succeeded();
        }

        bool artifactsStillFiltered = true;
        for (const res::ResourcePath artifactPath : artifactPaths)
            artifactsStillFiltered = artifactsStillFiltered && database.LookupResource(artifactPath) == nullptr;

        durableDepot.SetCommittedPathSink(nullptr, nullptr);
        database.ShutFileSystemWatcher();
        database.Shutdown();
        static_cast<void>(files.DeleteFile(authoredRoot.AddFilePath(tweakPath.ToStringView())));
        static_cast<void>(files.DeleteFile(authoredRoot.AddFilePath(unsupportedCsvPath.ToStringView())));
        for (const res::ResourcePath artifactPath : artifactPaths)
            static_cast<void>(files.DeleteFile(authoredRoot.AddFilePath(artifactPath.ToStringView())));
        const auto depotShutdown = durableDepot.Shutdown();

        const bool exactFiles = FileMatches(files, absoluteA, nextA) && FileMatches(files, absoluteB, nextB);
        const bool artifactsAbsent = !files.FileExist(journal) &&
                                     !files.FileExist(red::AbsolutePath::CreateFilePath(red::String(absoluteA.AsChar()) +
                                                                                       vanguard::assets::AssetTransactionStageSuffix)) &&
                                     !files.FileExist(red::AbsolutePath::CreateFilePath(red::String(absoluteB.AsChar()) +
                                                                                       vanguard::assets::AssetTransactionStageSuffix));
        const bool success = recoveryBeforeDatabase && initialDatabaseExact && initialRecursiveExact && tweakScanExact &&
                             unsupportedCsvFailedClosed && failedScanRejected && initialArtifactsFiltered &&
                             incrementalArtifactsFiltered && staged && applied && published && databaseStillPrecommit &&
                             databaseConverged && recursiveConverged && completed && taskAdmissionAllowed && tasksSucceeded &&
                             artifactsStillFiltered && exactFiles && artifactsAbsent &&
                             depotShutdown == vanguard::assets::AssetFileTransactionResult::Success;
        std::printf(
            "STAGE11_DB_RESULT success=%u recovery_before_db=%u initial_db=%u recursive_initial=%u tweak_scan=%u binary_csv_failed_closed=%u failed_scan_rejected=%u fullscan_artifacts_filtered=%u incremental_artifacts_filtered=%u durable_commit=%u db_stale_before_update=%u db_converged=%u recursive_converged=%u task_admitted_after_db=%u tasks_succeeded=%u exact_files=%u artifacts_absent=%u recovery_found=%u recovery_rolled_back=%u\n",
            success ? 1u : 0u,
            recoveryBeforeDatabase ? 1u : 0u,
            initialDatabaseExact ? 1u : 0u,
            initialRecursiveExact ? 1u : 0u,
            tweakScanExact ? 1u : 0u,
            unsupportedCsvFailedClosed ? 1u : 0u,
            failedScanRejected ? 1u : 0u,
            initialArtifactsFiltered ? 1u : 0u,
            incrementalArtifactsFiltered ? 1u : 0u,
            (staged && applied && published && completed) ? 1u : 0u,
            databaseStillPrecommit ? 1u : 0u,
            databaseConverged ? 1u : 0u,
            recursiveConverged ? 1u : 0u,
            taskAdmissionAllowed ? 1u : 0u,
            tasksSucceeded ? 1u : 0u,
            exactFiles ? 1u : 0u,
            artifactsAbsent ? 1u : 0u,
            recovery.journalFound ? 1u : 0u,
            recovery.rolledBack ? 1u : 0u);
        return success ? 0 : 76;
    }

    int RunStage12SystemProof(const red::AbsolutePath& projectRoot)
    {
        auto& files = vanguard::filesystem::GetManager();
        const red::AbsolutePath authoredRoot = projectRoot.AddDirPath("Assets\\REDAssetHost");
        const red::AbsolutePath derivedRoot = projectRoot.AddDirPath("DerivedData\\REDAssetHost");
        const red::AbsolutePath cookedRoot = derivedRoot.AddDirPath("Cooked");
        const red::AbsolutePath cookedCacheRoot = derivedRoot.AddDirPath("CookedCache");
        const red::AbsolutePath intermediateRoot = projectRoot.AddDirPath("Intermediate\\REDAssetHost");
        if (!files.CreatePath(authoredRoot) || !files.CreatePath(derivedRoot) || !files.CreatePath(cookedRoot) ||
            !files.CreatePath(cookedCacheRoot) || !files.CreatePath(intermediateRoot))
        {
            std::fprintf(stderr, "ERROR stage12=directories\n");
            return 90;
        }

        Register2DArraySaver();
        Register2DArrayLoader();

        red::DynArray<res::prv::DepotMapping> mappings{red::PoolEngine()};
        AddDepotMapping(mappings, authoredRoot, "");
        vanguard::asset_host::DurableResourceDepot durableDepot(mappings);
        vanguard::assets::AssetFileRecoveryReport recovery;
        const red::AbsolutePath journalPath = intermediateRoot.AddFilePath("stage12-resource-save.vtxn");
        if (durableDepot.Initialize(files, authoredRoot, journalPath, &recovery) !=
            vanguard::assets::AssetFileTransactionResult::Success)
        {
            std::fprintf(stderr, "ERROR stage12=durable-depot result=%u\n",
                         static_cast<Uint32>(durableDepot.LastTransactionResult()));
            return 91;
        }

        // Finish transaction recovery before the database or loader can observe the authored tree.
        const bool recoveryBeforeSystem = !recovery.journalFound || recovery.rolledBack || recovery.completedCleanup;
        if (!recoveryBeforeSystem)
        {
            static_cast<void>(durableDepot.Shutdown());
            return 92;
        }

        res::SetEditorDepot(&durableDepot);
        SetResourceDepot(&durableDepot);
        auto sourceLoader = res::CreateResourceLoader(&durableDepot, false);
        if (!sourceLoader)
        {
            SetResourceDepot(nullptr);
            res::SetEditorDepot(nullptr);
            static_cast<void>(durableDepot.Shutdown());
            return 93;
        }
        GResourceLoader = sourceLoader.Get();

        auto system = red::CreateUniquePtr<data::SystemInstance, red::PoolBackend>();
        system->SetTargetPlatform(kAssetHostTargetPlatform);
        system->InitSourceDepot();
        system->InitCookedDepot(cookedRoot, cookedCacheRoot);
        system->WaitUntilInitialised();
        data::SystemContext& context = system->CreateContext();
        resdb::Database& database = context.SourceResourceDatabase();
        durableDepot.SetCommittedPathSink(&database, &QueueCommittedPathsToDatabase);

        const bool realSourceWiring = &context.SourceDepot() == static_cast<data::ResourceDepot*>(&durableDepot) &&
                                      &context.SourceLoader() == sourceLoader.Get() && GResourceLoader == sourceLoader.Get();
        const res::ResourcePath resourcePath = res::ResourcePath::Build("stage12_probe.csv");
        const red::AbsolutePath authoredPath = authoredRoot.AddFilePath(resourcePath.ToStringView());
        const red::AbsolutePath cookedPath = cookedRoot.AddFilePath(resourcePath.ToStringView());
        const bool authoredPreexisting = files.FileExist(authoredPath);
        const bool cookedPreexisting = files.FileExist(cookedPath);
        const Uint64 cookedTimestampBefore = cookedPreexisting ? files.GetFileTimeStamp(cookedPath) : 0ull;

        bool authoredReady = authoredPreexisting;
        if (!authoredPreexisting)
        {
            auto authored = CreateHandle<C2dArray>();
            authored->ParseData("name;value\nalpha;42\nbeta;84\n");
            authored->Internal_SetPath(resourcePath);
            authored->MarkModified();
            res::ResourceSavingOptions options;
            options.m_depot = &durableDepot;
            options.m_targetPath = resourcePath;
            options.m_loadDeferredDataBuffers = false;
            authoredReady = res::SaveResource(authored, options) && !authored->IsModified();
            if (authoredReady)
                database.Update();
        }

        red::DynArray<res::ResourcePath> admitted{red::PoolBackend()};
        admitted.PushBack(resourcePath);
        const bool databaseReady = authoredReady && DatabaseEntriesReadyForBuild(database, admitted);

        bool loaderLoaded = false;
        if (databaseReady)
        {
            auto token = context.SourceLoader().IssueLoadingRequest(resourcePath);
            if (token)
            {
                const THandle<C2dArray> loaded = token->WaitUntilLoaded<C2dArray>();
                Uint32 columns = 0u;
                Uint32 rows = 0u;
                if (loaded)
                {
                    loaded->GetSize(columns, rows);
                    loaderLoaded = columns == 2u && rows == 2u && loaded->GetValue("name", 0u) == "alpha" &&
                                   loaded->GetValue("value", 1u) == "84";
                }
            }
        }

        bool taskSucceeded = false;
        Uint32 buildReason = 0u;
        Uint32 filesBuilt = 0u;
        {
            HostReporter reporter;
            data::TaskSystem taskSystem(context, &cooker::CreateTask, reporter, 1);
            if (databaseReady && loaderLoaded)
                taskSystem.Build(admitted);
            const auto tasks = taskSystem.GetAllTasksInOrderOfExecution();
            if (tasks.Size() == 1u && tasks[0])
            {
                taskSucceeded = tasks[0]->Succeeded();
                buildReason = tasks[0]->GetBuildReason();
                filesBuilt = tasks[0]->GetMetrics().filesBuilt;
            }
        }

        const bool cookedExists = files.FileExist(cookedPath);
        const Uint64 cookedTimestampAfter = cookedExists ? files.GetFileTimeStamp(cookedPath) : 0ull;
        const bool restartReuse = !authoredPreexisting ||
                                  (cookedPreexisting && buildReason == 0u && cookedTimestampAfter == cookedTimestampBefore);

        durableDepot.SetCommittedPathSink(nullptr, nullptr);
        system->Shutdown();
        system.Reset();
        GResourceLoader = nullptr;
        sourceLoader->Shutdown();
        sourceLoader.Reset();
        SetResourceDepot(nullptr);
        res::SetEditorDepot(nullptr);
        const auto depotShutdown = durableDepot.Shutdown();

        const bool globalsCleared = GResourceLoader == nullptr && GetResourceDepot() == nullptr &&
                                    res::GetEditorDepot() == nullptr;
        const bool success = recoveryBeforeSystem && realSourceWiring && authoredReady && databaseReady && loaderLoaded &&
                             taskSucceeded && cookedExists && restartReuse && globalsCleared &&
                             depotShutdown == vanguard::assets::AssetFileTransactionResult::Success;
        std::printf(
            "STAGE12_SYSTEM_RESULT success=%u recovery_before_system=%u real_source_wiring=%u authored_preexisting=%u database_ready=%u loader_loaded=%u task_succeeded=%u cooked_preexisting=%u build_reason=0x%08X files_built=%u restart_reuse=%u globals_cleared=%u\n",
            success ? 1u : 0u,
            recoveryBeforeSystem ? 1u : 0u,
            realSourceWiring ? 1u : 0u,
            authoredPreexisting ? 1u : 0u,
            databaseReady ? 1u : 0u,
            loaderLoaded ? 1u : 0u,
            taskSucceeded ? 1u : 0u,
            cookedPreexisting ? 1u : 0u,
            buildReason,
            filesBuilt,
            restartReuse ? 1u : 0u,
            globalsCleared ? 1u : 0u);
        return success ? 0 : 94;
    }

    int RunStage13TextureProof(const red::AbsolutePath& projectRoot, const bool forceReimport)
    {
        auto& files = vanguard::filesystem::GetManager();
        const red::AbsolutePath authoredRoot = projectRoot.AddDirPath("Assets\\REDAssetHost");
        const red::AbsolutePath sourceFixtureRoot = projectRoot.AddDirPath("SourceAssets\\REDAssetHost");
        const red::AbsolutePath derivedRoot = projectRoot.AddDirPath("DerivedData\\REDAssetHost");
        const red::AbsolutePath cookedRoot = derivedRoot.AddDirPath("Cooked");
        const red::AbsolutePath cookedCacheRoot = derivedRoot.AddDirPath("CookedCache");
        const red::AbsolutePath intermediateRoot = projectRoot.AddDirPath("Intermediate\\REDAssetHost");
        if (!files.CreatePath(authoredRoot) || !files.CreatePath(sourceFixtureRoot) || !files.CreatePath(derivedRoot) ||
            !files.CreatePath(cookedRoot) || !files.CreatePath(cookedCacheRoot) || !files.CreatePath(intermediateRoot))
        {
            std::fprintf(stderr, "ERROR stage13=directories\n");
            return 95;
        }

        const red::AbsolutePath sourcePath = sourceFixtureRoot.AddFilePath("Brick.png");
        if (!files.FileExist(sourcePath))
        {
            std::fprintf(stderr, "ERROR stage13=fixture-missing path=%hs\n", sourcePath.AsChar());
            return 96;
        }

        const res::ResourcePath texturePath = res::ResourcePath::Build("textures\\brick.xbm");
        const rtti::ClassType* const textureClass = res::GetResourceClassForFile(texturePath);
        IImporter* const importer = textureClass != nullptr ? IImporter::FindImporter(textureClass, "png") : nullptr;
        auto cooker = ICooker::Create(texturePath);
        const bool cookerReady = cooker != nullptr && cooker->CanCook(texturePath) && cooker->GetVersion() == 8u;
        const bool importerReady = textureClass != nullptr && importer != nullptr && importer->SupportsResource(textureClass) &&
                                   importer->SupportsFormat("png");
        if (!importerReady || !cookerReady)
        {
            std::printf("STAGE13_TEXTURE_RESULT success=0 importer_ready=%u cooker_ready=%u cooker_version=%u\n",
                        importerReady ? 1u : 0u,
                        cookerReady ? 1u : 0u,
                        cooker ? cooker->GetVersion() : 0u);
            return 97;
        }

        red::DynArray<res::prv::DepotMapping> mappings{red::PoolEngine()};
        AddDepotMapping(mappings, authoredRoot, "");
        vanguard::asset_host::DurableResourceDepot durableDepot(mappings);
        vanguard::assets::AssetFileRecoveryReport recovery;
        const red::AbsolutePath journalPath = intermediateRoot.AddFilePath("stage13-texture-save.vtxn");
        if (durableDepot.Initialize(files, authoredRoot, journalPath, &recovery) !=
            vanguard::assets::AssetFileTransactionResult::Success)
        {
            std::fprintf(stderr, "ERROR stage13=durable-depot result=%u\n",
                         static_cast<Uint32>(durableDepot.LastTransactionResult()));
            return 98;
        }

        const bool recoveryBeforeSystem = !recovery.journalFound || recovery.rolledBack || recovery.completedCleanup;
        if (!recoveryBeforeSystem)
        {
            static_cast<void>(durableDepot.Shutdown());
            return 99;
        }

        res::SetEditorDepot(&durableDepot);
        SetResourceDepot(&durableDepot);
        auto sourceLoader = res::CreateResourceLoader(&durableDepot, false);
        if (!sourceLoader)
        {
            SetResourceDepot(nullptr);
            res::SetEditorDepot(nullptr);
            static_cast<void>(durableDepot.Shutdown());
            return 100;
        }
        GResourceLoader = sourceLoader.Get();

        auto system = red::CreateUniquePtr<data::SystemInstance, red::PoolBackend>();
        system->SetTargetPlatform(kAssetHostTargetPlatform);
        system->InitSourceDepot();
        system->InitCookedDepot(cookedRoot, cookedCacheRoot);
        system->WaitUntilInitialised();
        data::SystemContext& context = system->CreateContext();
        ConfigureTextureImportTarget(context);
        resdb::Database& database = context.SourceResourceDatabase();
        durableDepot.SetCommittedPathSink(&database, &QueueCommittedPathsToDatabase);

        const bool realSourceWiring = &context.SourceDepot() == static_cast<data::ResourceDepot*>(&durableDepot) &&
                                      &context.SourceLoader() == sourceLoader.Get() && GResourceLoader == sourceLoader.Get();
        const red::AbsolutePath authoredPath = authoredRoot.AddFilePath(texturePath.ToStringView());
        const red::AbsolutePath cookedPath = cookedRoot.AddFilePath(texturePath.ToStringView());
        const bool authoredPreexisting = files.FileExist(authoredPath);
        const bool cookedPreexisting = files.FileExist(cookedPath);
        const Uint64 cookedTimestampBefore = cookedPreexisting ? files.GetFileTimeStamp(cookedPath) : 0ull;

        bool importedThisRun = false;
        bool hardwareBC7Observed = false;
        bool authoredReady = authoredPreexisting;
        if (!authoredPreexisting || forceReimport)
        {
            THandle<ResourceImportOptions> options = importer->CreateOptions(sourcePath);
            THandle<CResource> importTarget = textureClass->CreateHandle<CResource>();
            if (options && importTarget)
            {
                options->SetSourcePath(sourcePath);
                options->SetResourcePath(texturePath);
                importTarget->Internal_SetPath(texturePath);
                const Uint32 hardwareBC7Before = VanguardAssetHost_GetHardwareBC7CompressionCount();
                ImportResult importResult = importer->DoImport(importTarget, options);
                const Uint32 hardwareBC7After = VanguardAssetHost_GetHardwareBC7CompressionCount();
                hardwareBC7Observed = hardwareBC7After > hardwareBC7Before;
                if (importResult.IsSuccessful() && importResult.GetResources().Size() == 1u &&
                    importResult.GetResources()[0] && importResult.GetResources()[0]->GetClass() == textureClass)
                {
                    THandle<CResource> imported = importResult.GetResources()[0];
                    imported->Internal_SetPath(texturePath);
                    imported->MarkModified();
                    res::ResourceSavingOptions optionsForSave;
                    optionsForSave.m_depot = &durableDepot;
                    optionsForSave.m_targetPath = texturePath;
                    authoredReady = res::SaveResource(imported, optionsForSave) && !imported->IsModified();
                    importedThisRun = authoredReady;
                    if (authoredReady)
                        database.Update();
                }
                else if (importResult.HasMessage())
                {
                    std::fprintf(stderr, "ERROR stage13=import message=%hs\n", importResult.GetErrorMessage().AsChar());
                }
            }
        }

        red::DynArray<res::ResourcePath> admitted{red::PoolBackend()};
        admitted.PushBack(texturePath);
        const bool databaseReady = authoredReady && DatabaseEntriesReadyForBuild(database, admitted);

        bool loaderLoaded = false;
        Uint32 loadedGpuBytes = 0u;
        if (databaseReady)
        {
            res::IssueLoadingRequestParameter load(texturePath);
            load.postLoadFlags.skipUploadRenderData = true;
            auto token = context.SourceLoader().IssueLoadingRequest(load);
            if (token)
            {
                const THandle<CResource> loaded = token->WaitUntilLoaded();
                if (loaded)
                {
                    loadedGpuBytes = loaded->GetGPUSize();
                    loaderLoaded = loaded->GetClass() == textureClass && loadedGpuBytes != 0u;
                }
            }
        }

        bool taskSucceeded = false;
        Uint32 buildReason = 0u;
        Uint32 filesBuilt = 0u;
        {
            HostReporter reporter;
            data::TaskSystem taskSystem(context, &cooker::CreateTask, reporter, 1);
            if (databaseReady && loaderLoaded)
                taskSystem.Build(admitted);
            const auto tasks = taskSystem.GetAllTasksInOrderOfExecution();
            if (tasks.Size() == 1u && tasks[0])
            {
                taskSucceeded = tasks[0]->Succeeded();
                buildReason = tasks[0]->GetBuildReason();
                filesBuilt = tasks[0]->GetMetrics().filesBuilt;
            }
        }

        const bool cookedExists = files.FileExist(cookedPath);
        const Uint64 cookedTimestampAfter = cookedExists ? files.GetFileTimeStamp(cookedPath) : 0ull;
        const bool restartReuse = forceReimport || !authoredPreexisting ||
                                  (cookedPreexisting && buildReason == 0u && cookedTimestampAfter == cookedTimestampBefore);
        const bool incrementalRecook = !forceReimport ||
                                       (authoredPreexisting && importedThisRun && cookedPreexisting &&
                                        buildReason != 0u && cookedTimestampAfter != cookedTimestampBefore);
        const bool compressionProof = (!forceReimport && authoredPreexisting) || hardwareBC7Observed;

        durableDepot.SetCommittedPathSink(nullptr, nullptr);
        system->Shutdown();
        system.Reset();
        GResourceLoader = nullptr;
        sourceLoader->Shutdown();
        sourceLoader.Reset();
        SetResourceDepot(nullptr);
        res::SetEditorDepot(nullptr);
        const auto depotShutdown = durableDepot.Shutdown();

        const bool globalsCleared = GResourceLoader == nullptr && GetResourceDepot() == nullptr &&
                                    res::GetEditorDepot() == nullptr;
        const bool success = recoveryBeforeSystem && realSourceWiring && importerReady && cookerReady && authoredReady &&
                             databaseReady && loaderLoaded && taskSucceeded && cookedExists && restartReuse && incrementalRecook && compressionProof &&
                             globalsCleared && depotShutdown == vanguard::assets::AssetFileTransactionResult::Success;
        std::printf(
            "STAGE13_TEXTURE_RESULT success=%u force_reimport=%u importer_ready=%u cooker_ready=%u cooker_version=%u authored_preexisting=%u imported_this_run=%u hardware_bc7_observed=%u database_ready=%u loader_loaded=%u loaded_gpu_bytes=%u task_succeeded=%u cooked_preexisting=%u build_reason=0x%08X files_built=%u restart_reuse=%u incremental_recook=%u globals_cleared=%u\n",
            success ? 1u : 0u,
            forceReimport ? 1u : 0u,
            importerReady ? 1u : 0u,
            cookerReady ? 1u : 0u,
            cooker ? cooker->GetVersion() : 0u,
            authoredPreexisting ? 1u : 0u,
            importedThisRun ? 1u : 0u,
            hardwareBC7Observed ? 1u : 0u,
            databaseReady ? 1u : 0u,
            loaderLoaded ? 1u : 0u,
            loadedGpuBytes,
            taskSucceeded ? 1u : 0u,
            cookedPreexisting ? 1u : 0u,
            buildReason,
            filesBuilt,
            restartReuse ? 1u : 0u,
            incrementalRecook ? 1u : 0u,
            globalsCleared ? 1u : 0u);
        return success ? 0 : 101;
    }

    bool ReimportShadowTexture(const TextureShadowBinding& binding, data::SystemContext& context,
                               vanguard::asset_host::DurableResourceDepot& depot, bool& hardwareBC7Observed)
    {
        auto& files = vanguard::filesystem::GetManager();
        if (!files.FileExist(binding.sourcePath))
            return false;

        res::IssueLoadingRequestParameter load(binding.resourcePath);
        load.postLoadFlags.skipUploadRenderData = true;
        const auto token = context.SourceLoader().IssueLoadingRequest(load);
        const THandle<CResource> current = token ? token->WaitUntilLoaded() : nullptr;
        if (!current)
            return false;

        const String sourceExtension = red::utils::ExtractExtension(binding.sourcePath);
        IImporter* const importer = IImporter::FindImporter(current->GetClass(), sourceExtension);
        if (!importer)
            return false;

        THandle<ResourceImportOptions> options = importer->CreateOptions(binding.sourcePath);
        if (!options)
            return false;

        const THandle<res::ResourceMetadata> metadata = res::ResourceMetadata::Get(current);
        if (metadata && metadata->GetImportData())
        {
            const THandle<ResourceImportOptions>& suggested = metadata->GetImportData()->GetImportOptions();
            if (suggested && suggested->GetClass() == options->GetClass() && !options->m_skipSuggestedOptions)
                suggested->GetClass()->Copy(options.Get(), suggested.Get());
        }
        options->SetResourcePath(binding.resourcePath);
        options->SetSourcePath(binding.sourcePath);

        const Uint32 hardwareBC7Before = VanguardAssetHost_GetHardwareBC7CompressionCount();
        ImportResult importResult = importer->DoImport(current, options);
        const Uint32 hardwareBC7After = VanguardAssetHost_GetHardwareBC7CompressionCount();
        hardwareBC7Observed = hardwareBC7Observed || hardwareBC7After > hardwareBC7Before;
        if (!importResult.IsSuccessful() || importResult.GetResources().Size() != 1u || !importResult.GetResources()[0])
            return false;

        THandle<CResource> imported = importResult.GetResources()[0];
        imported->Internal_SetPath(binding.resourcePath);
        res::ResourceMetadata::SetImportOptions(imported, options);
        imported->MarkModified();
        res::ResourceSavingOptions saving;
        saving.m_depot = &depot;
        saving.m_targetPath = binding.resourcePath;
        return res::SaveResource(imported, saving) && !imported->IsModified();
    }

    bool ImportNewShadowTexture(const red::AbsolutePath& sourcePath, const res::ResourcePath& resourcePath,
                                vanguard::asset_host::DurableResourceDepot& depot, bool& hardwareBC7Observed)
    {
        const rtti::ClassType* const resourceClass = res::GetResourceClassForFile(resourcePath);
        if (!resourceClass)
            return false;
        const String sourceExtension = red::utils::ExtractExtension(sourcePath);
        IImporter* const importer = IImporter::FindImporter(resourceClass, sourceExtension);
        if (!importer)
            return false;

        THandle<ResourceImportOptions> options = importer->CreateOptions(sourcePath);
        THandle<CResource> target = resourceClass->CreateHandle<CResource>();
        if (!options || !target)
            return false;
        options->SetSourcePath(sourcePath);
        options->SetResourcePath(resourcePath);
        target->Internal_SetPath(resourcePath);

        const Uint32 hardwareBC7Before = VanguardAssetHost_GetHardwareBC7CompressionCount();
        ImportResult importResult = importer->DoImport(target, options);
        const Uint32 hardwareBC7After = VanguardAssetHost_GetHardwareBC7CompressionCount();
        hardwareBC7Observed = hardwareBC7Observed || hardwareBC7After > hardwareBC7Before;
        if (!importResult.IsSuccessful() || importResult.GetResources().Size() != 1u || !importResult.GetResources()[0])
            return false;

        THandle<CResource> imported = importResult.GetResources()[0];
        imported->Internal_SetPath(resourcePath);
        res::ResourceMetadata::SetImportOptions(imported, options);
        imported->MarkModified();
        res::ResourceSavingOptions saving;
        saving.m_depot = &depot;
        saving.m_targetPath = resourcePath;
        return res::SaveResource(imported, saving) && !imported->IsModified();
    }

    int RunStage21TextureShadowProof(const red::AbsolutePath& projectRoot)
    {
        auto& files = vanguard::filesystem::GetManager();
        const red::AbsolutePath authoredRoot = projectRoot.AddDirPath("Assets\\REDAssetHost");
        const red::AbsolutePath sourceRoot = projectRoot.AddDirPath("SourceAssets\\REDAssetHost");
        const red::AbsolutePath derivedRoot = projectRoot.AddDirPath("DerivedData\\REDAssetHost");
        const red::AbsolutePath cookedRoot = derivedRoot.AddDirPath("Cooked");
        const red::AbsolutePath cookedCacheRoot = derivedRoot.AddDirPath("CookedCache");
        const red::AbsolutePath intermediateRoot = projectRoot.AddDirPath("Intermediate\\REDAssetHost");
        if (!files.CreatePath(authoredRoot) || !files.CreatePath(sourceRoot) || !files.CreatePath(derivedRoot) ||
            !files.CreatePath(cookedRoot) || !files.CreatePath(cookedCacheRoot) || !files.CreatePath(intermediateRoot))
        {
            std::fprintf(stderr, "ERROR stage21=directories\n");
            return 102;
        }

        red::DynArray<res::prv::DepotMapping> mappings{red::PoolEngine()};
        AddDepotMapping(mappings, authoredRoot, "");
        vanguard::asset_host::DurableResourceDepot durableDepot(mappings);
        vanguard::assets::AssetFileRecoveryReport recovery;
        const red::AbsolutePath journalPath = intermediateRoot.AddFilePath("stage21-texture-shadow.vtxn");
        if (durableDepot.Initialize(files, authoredRoot, journalPath, &recovery) !=
            vanguard::assets::AssetFileTransactionResult::Success)
            return 103;

        const bool recoveryBeforeSystem = !recovery.journalFound || recovery.rolledBack || recovery.completedCleanup;
        if (!recoveryBeforeSystem)
        {
            static_cast<void>(durableDepot.Shutdown());
            return 104;
        }

        res::SetEditorDepot(&durableDepot);
        SetResourceDepot(&durableDepot);
        auto sourceLoader = res::CreateResourceLoader(&durableDepot, false);
        if (!sourceLoader)
        {
            SetResourceDepot(nullptr);
            res::SetEditorDepot(nullptr);
            static_cast<void>(durableDepot.Shutdown());
            return 105;
        }
        GResourceLoader = sourceLoader.Get();

        auto system = red::CreateUniquePtr<data::SystemInstance, red::PoolBackend>();
        system->SetTargetPlatform(kAssetHostTargetPlatform);
        system->InitSourceDepot();
        system->InitCookedDepot(cookedRoot, cookedCacheRoot);
        system->WaitUntilInitialised();
        data::SystemContext& context = system->CreateContext();
        ConfigureTextureImportTarget(context);
        resdb::Database& database = context.SourceResourceDatabase();
        durableDepot.SetCommittedPathSink(&database, &QueueCommittedPathsToDatabase);

        struct ShadowFixture
        {
            const char* sourceName;
            const char* resourceName;
        };
        static constexpr ShadowFixture fixtures[] = {
            {"Brick.png", "textures\\brick.xbm"},
            {"Color2048.png", "textures\\color2048.xbm"},
            {"Color4096.png", "textures\\color4096.xbm"},
            {"Normal2048_n.png", "textures\\normal2048.xbm"},
        };

        red::DynArray<res::ResourcePath> seeded{red::PoolBackend()};
        bool seedHardwareBC7Observed = false;
        bool seedSucceeded = true;
        const auto seedStart = std::chrono::steady_clock::now();
        for (const ShadowFixture& fixture : fixtures)
        {
            const red::AbsolutePath sourcePath = sourceRoot.AddFilePath(fixture.sourceName);
            const res::ResourcePath resourcePath = res::ResourcePath::Build(fixture.resourceName);
            const red::AbsolutePath authoredPath = authoredRoot.AddFilePath(resourcePath.ToStringView());
            if (!files.FileExist(sourcePath) || files.FileExist(authoredPath))
                continue;
            if (!ImportNewShadowTexture(sourcePath, resourcePath, durableDepot, seedHardwareBC7Observed))
            {
                seedSucceeded = false;
                break;
            }
            seeded.PushBack(resourcePath);
        }

        bool seedTasksSucceeded = seedSucceeded;
        Uint32 seedNonzeroBuildReasons = 0u;
        if (seedSucceeded && !seeded.Empty())
        {
            database.Update();
            seedTasksSucceeded = DatabaseEntriesReadyForBuild(database, seeded);
            HostReporter reporter;
            data::TaskSystem taskSystem(context, &cooker::CreateTask, reporter, 1);
            if (seedTasksSucceeded)
                taskSystem.Build(seeded);
            const auto tasks = taskSystem.GetAllTasksInOrderOfExecution();
            seedTasksSucceeded = seedTasksSucceeded && tasks.Size() == seeded.Size();
            for (const auto& task : tasks)
            {
                if (!task || !task->Succeeded())
                    seedTasksSucceeded = false;
                if (task && task->GetBuildReason() != 0u)
                    ++seedNonzeroBuildReasons;
            }
        }
        const auto seedElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - seedStart).count();
        std::printf("STAGE21_TEXTURE_SEED success=%u imported=%u build_reasons=%u hardware_bc7=%u elapsed_ms=%lld\n",
                    seedTasksSucceeded ? 1u : 0u,
                    seeded.Size(),
                    seedNonzeroBuildReasons,
                    seedHardwareBC7Observed ? 1u : 0u,
                    static_cast<long long>(seedElapsedMs));
        std::fflush(stdout);

        if (!seedTasksSucceeded)
        {
            durableDepot.SetCommittedPathSink(nullptr, nullptr);
            system->Shutdown();
            system.Reset();
            GResourceLoader = nullptr;
            sourceLoader->Shutdown();
            sourceLoader.Reset();
            SetResourceDepot(nullptr);
            res::SetEditorDepot(nullptr);
            static_cast<void>(durableDepot.Shutdown());
            return 107;
        }

        bool fixtureEvidenceReady = true;
        bool normalBC5Ready = false;
        for (const ShadowFixture& fixture : fixtures)
        {
            const res::ResourcePath resourcePath = res::ResourcePath::Build(fixture.resourceName);
            res::IssueLoadingRequestParameter load(resourcePath);
            load.postLoadFlags.skipUploadRenderData = true;
            const auto token = context.SourceLoader().IssueLoadingRequest(load);
            const THandle<CResource> loaded = token ? token->WaitUntilLoaded() : nullptr;
            const THandle<CBitmapTexture> texture = loaded ? Cast<CBitmapTexture>(loaded) : nullptr;
            if (!texture)
            {
                fixtureEvidenceReady = false;
                std::printf("STAGE21_TEXTURE_FIXTURE resource=%s loaded=0\n", fixture.resourceName);
                continue;
            }

            const ITexture::SizeDesc size = texture->GetSizeDesc();
            const STextureGroupSetup& setup = texture->GetTextureSetup();
            const bool normalFixture = std::strcmp(fixture.sourceName, "Normal2048_n.png") == 0;
            if (normalFixture)
            {
                normalBC5Ready = setup.m_group == GpuWrapApi::TEXG_Generic_Normal &&
                                 setup.m_compression == TCM_Normalmap;
            }
            std::printf(
                "STAGE21_TEXTURE_FIXTURE resource=%s loaded=1 width=%u height=%u mips=%u group=%u compression=%d raw_format=%d gamma=%u\n",
                fixture.resourceName,
                static_cast<Uint32>(size.width),
                static_cast<Uint32>(size.height),
                static_cast<Uint32>(size.numMips),
                static_cast<Uint32>(setup.m_group),
                static_cast<Int32>(setup.m_compression),
                static_cast<Int32>(setup.m_rawFormat),
                setup.m_isGamma ? 1u : 0u);
        }
        std::fflush(stdout);

        red::DynArray<TextureShadowBinding> bindings{red::PoolBackend()};
        const bool bindingsReady = BuildTextureShadowBindings(database, durableDepot, sourceRoot, bindings);
        TextureShadowWatchState watchState;
        watchState.bindings = &bindings;

        vanguard::asset_streaming::PlatformDirectoryObserver observer;
        const vanguard::filesystem::AbsolutePath observedRoots[] = {sourceRoot};
        const bool observerStarted = bindingsReady && observer.Start({observedRoots, 1u}, &OnTextureShadowSourceObservation, &watchState, {});
        std::printf("STAGE21_TEXTURE_SHADOW_READY success=%u bindings=%u observer=%u\n",
                    observerStarted ? 1u : 0u, bindings.Size(), observerStarted ? 1u : 0u);
        std::fflush(stdout);

        bool observationReceived = false;
        if (observerStarted)
        {
            for (Uint32 spin = 0; spin < 3000u; ++spin)
            {
                if (watchState.pendingMask.load(std::memory_order_acquire) != 0u || watchState.overflow.load(std::memory_order_acquire))
                {
                    observationReceived = true;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        static_cast<void>(observer.Stop());

        Uint64 pendingMask = watchState.pendingMask.load(std::memory_order_acquire);
        const bool overflow = watchState.overflow.load(std::memory_order_acquire);
        if (overflow && bindings.Size() != 0u)
            pendingMask = bindings.Size() == 64u ? ~Uint64{0} : ((Uint64{1} << bindings.Size()) - 1u);

        red::DynArray<res::ResourcePath> admitted{red::PoolBackend()};
        Uint32 pendingBindings = 0u;
        Uint32 attemptedReimports = 0u;
        Uint32 reimported = 0u;
        Uint32 reimportFailures = 0u;
        Uint32 missingSources = 0u;
        bool hardwareBC7Observed = false;
        const auto reimportStart = std::chrono::steady_clock::now();
        for (Uint32 index = 0; index < bindings.Size(); ++index)
        {
            if ((pendingMask & (Uint64{1} << index)) == 0u)
                continue;
            ++pendingBindings;
            if (!files.FileExist(bindings[index].sourcePath))
            {
                ++missingSources;
                continue;
            }
            ++attemptedReimports;
            if (ReimportShadowTexture(bindings[index], context, durableDepot, hardwareBC7Observed))
            {
                admitted.PushBack(bindings[index].resourcePath);
                ++reimported;
            }
            else
            {
                ++reimportFailures;
            }
        }
        const auto reimportElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - reimportStart).count();

        const auto databaseStart = std::chrono::steady_clock::now();
        if (!admitted.Empty())
            database.Update();
        const bool databaseReady = admitted.Empty() || DatabaseEntriesReadyForBuild(database, admitted);
        const auto databaseElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - databaseStart).count();

        bool tasksSucceeded = admitted.Empty();
        Uint32 nonzeroBuildReasons = 0u;
        Uint32 filesBuilt = 0u;
        const auto taskStart = std::chrono::steady_clock::now();
        if (!admitted.Empty())
        {
            HostReporter reporter;
            data::TaskSystem taskSystem(context, &cooker::CreateTask, reporter, 1);
            if (databaseReady)
                taskSystem.Build(admitted);
            const auto tasks = taskSystem.GetAllTasksInOrderOfExecution();
            tasksSucceeded = databaseReady && tasks.Size() == admitted.Size();
            for (const auto& task : tasks)
            {
                if (!task || !task->Succeeded())
                    tasksSucceeded = false;
                if (task && task->GetBuildReason() != 0u)
                    ++nonzeroBuildReasons;
                if (task)
                    filesBuilt += task->GetMetrics().filesBuilt;
            }
        }
        const auto taskElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - taskStart).count();

        durableDepot.SetCommittedPathSink(nullptr, nullptr);
        system->Shutdown();
        system.Reset();
        GResourceLoader = nullptr;
        sourceLoader->Shutdown();
        sourceLoader.Reset();
        SetResourceDepot(nullptr);
        res::SetEditorDepot(nullptr);
        const auto depotShutdown = durableDepot.Shutdown();

        const bool globalsCleared = GResourceLoader == nullptr && GetResourceDepot() == nullptr && res::GetEditorDepot() == nullptr;
        const bool success = recoveryBeforeSystem && fixtureEvidenceReady && normalBC5Ready && bindingsReady && observerStarted && observationReceived && pendingMask != 0u &&
                             pendingBindings == attemptedReimports + missingSources && reimportFailures == 0u &&
                             reimported == attemptedReimports && reimported == admitted.Size() && databaseReady && tasksSucceeded && globalsCleared &&
                             depotShutdown == vanguard::assets::AssetFileTransactionResult::Success;
        std::printf(
            "STAGE21_TEXTURE_SHADOW_RESULT success=%u bindings=%u observation=%u overflow=%u pending_mask=0x%016llX pending_bindings=%u attempted_reimports=%u reimported=%u reimport_failures=%u orphaned_sources=%u hardware_bc7_observed=%u fixture_evidence=%u normal_bc5=%u database_ready=%u tasks_succeeded=%u nonzero_build_reasons=%u files_built=%u reimport_ms=%lld database_ms=%lld task_ms=%lld globals_cleared=%u\n",
            success ? 1u : 0u,
            bindings.Size(),
            observationReceived ? 1u : 0u,
            overflow ? 1u : 0u,
            static_cast<unsigned long long>(pendingMask),
            pendingBindings,
            attemptedReimports,
            reimported,
            reimportFailures,
            missingSources,
            hardwareBC7Observed ? 1u : 0u,
            fixtureEvidenceReady ? 1u : 0u,
            normalBC5Ready ? 1u : 0u,
            databaseReady ? 1u : 0u,
            tasksSucceeded ? 1u : 0u,
            nonzeroBuildReasons,
            filesBuilt,
            static_cast<long long>(reimportElapsedMs),
            static_cast<long long>(databaseElapsedMs),
            static_cast<long long>(taskElapsedMs),
            globalsCleared ? 1u : 0u);
        return success ? 0 : 106;
    }

    int RunStage22MaterialShadowProof(const red::AbsolutePath& projectRoot, bool semanticEdit)
    {
        auto& files = vanguard::filesystem::GetManager();
        const red::AbsolutePath authoredRoot = projectRoot.AddDirPath("Assets\\REDAssetHost");
        const red::AbsolutePath sourceRoot = projectRoot.AddDirPath("SourceAssets\\REDAssetHost");
        const red::AbsolutePath derivedRoot = projectRoot.AddDirPath("DerivedData\\REDAssetHost");
        const red::AbsolutePath cookedRoot = derivedRoot.AddDirPath("Cooked");
        const red::AbsolutePath cookedCacheRoot = derivedRoot.AddDirPath("CookedCache");
        const red::AbsolutePath intermediateRoot = projectRoot.AddDirPath("Intermediate\\REDAssetHost");
        if (!files.CreatePath(authoredRoot) || !files.CreatePath(sourceRoot) || !files.CreatePath(derivedRoot) ||
            !files.CreatePath(cookedRoot) || !files.CreatePath(cookedCacheRoot) || !files.CreatePath(intermediateRoot))
        {
            std::fprintf(stderr, "ERROR stage22=directories\n");
            return 120;
        }

        red::DynArray<res::prv::DepotMapping> mappings{red::PoolEngine()};
        AddDepotMapping(mappings, authoredRoot, "");
        vanguard::asset_host::DurableResourceDepot durableDepot(mappings);
        vanguard::assets::AssetFileRecoveryReport recovery;
        const red::AbsolutePath journalPath = intermediateRoot.AddFilePath("stage22-material-shadow.vtxn");
        if (durableDepot.Initialize(files, authoredRoot, journalPath, &recovery) !=
            vanguard::assets::AssetFileTransactionResult::Success)
            return 121;

        const bool recoveryBeforeSystem = !recovery.journalFound || recovery.rolledBack || recovery.completedCleanup;
        if (!recoveryBeforeSystem)
        {
            static_cast<void>(durableDepot.Shutdown());
            return 122;
        }

        res::SetEditorDepot(&durableDepot);
        SetResourceDepot(&durableDepot);
        auto sourceLoader = res::CreateResourceLoader(&durableDepot, false);
        if (!sourceLoader)
        {
            SetResourceDepot(nullptr);
            res::SetEditorDepot(nullptr);
            static_cast<void>(durableDepot.Shutdown());
            return 123;
        }
        GResourceLoader = sourceLoader.Get();

        auto system = red::CreateUniquePtr<data::SystemInstance, red::PoolBackend>();
        system->SetTargetPlatform(kAssetHostTargetPlatform);
        system->InitSourceDepot();
        system->InitCookedDepot(cookedRoot, cookedCacheRoot);
        system->WaitUntilInitialised();
        data::SystemContext& context = system->CreateContext();
        ConfigureTextureImportTarget(context);
        resdb::Database& database = context.SourceResourceDatabase();
        durableDepot.SetCommittedPathSink(&database, &QueueCommittedPathsToDatabase);

        const res::ResourcePath texturePath = res::ResourcePath::Build(
            semanticEdit ? "textures\\color2048.xbm" : "textures\\brick.xbm");
        const res::ResourcePath templatePath = res::ResourcePath::Build("materials\\stage22_probe.mt");
        const res::ResourcePath instancePath = res::ResourcePath::Build("materials\\stage22_probe.mi");
        const red::AbsolutePath textureAbsolute = authoredRoot.AddFilePath(texturePath.ToStringView());
        const red::AbsolutePath templateAbsolute = authoredRoot.AddFilePath(templatePath.ToStringView());
        const red::AbsolutePath instanceAbsolute = authoredRoot.AddFilePath(instancePath.ToStringView());
        const red::AbsolutePath cookedTemplateAbsolute = cookedRoot.AddFilePath(templatePath.ToStringView());
        const red::AbsolutePath cookedInstanceAbsolute = cookedRoot.AddFilePath(instancePath.ToStringView());

        bool textureSeeded = false;
        if (!files.FileExist(textureAbsolute))
        {
            bool hardwareBC7Observed = false;
            const red::AbsolutePath textureSource = sourceRoot.AddFilePath(semanticEdit ? "Color2048.png" : "Brick.png");
            if (!files.FileExist(textureSource) ||
                !ImportNewShadowTexture(textureSource, texturePath, durableDepot, hardwareBC7Observed))
            {
                durableDepot.SetCommittedPathSink(nullptr, nullptr);
                system->Shutdown();
                system.Reset();
                GResourceLoader = nullptr;
                sourceLoader->Shutdown();
                sourceLoader.Reset();
                SetResourceDepot(nullptr);
                res::SetEditorDepot(nullptr);
                static_cast<void>(durableDepot.Shutdown());
                std::fprintf(stderr, "ERROR stage22=texture-fixture\n");
                return 124;
            }
            textureSeeded = true;
            database.Update();
        }

        const bool templatePreexisting = files.FileExist(templateAbsolute);
        const bool instancePreexisting = files.FileExist(instanceAbsolute);
        bool templateSaved = templatePreexisting;
        bool instanceSaved = instancePreexisting;
        bool fixtureBindingReady = false;
        bool bindingUpdated = false;

        if (!templatePreexisting || !instancePreexisting)
        {
            auto materialTemplate = CreateHandle<CMaterialTemplate>();
            materialTemplate->Internal_SetPath(templatePath);
            materialTemplate->SetName(RED_NAME_CONSTEXPR("Stage22Probe"));
            materialTemplate->SetIsValid(true);

            MaterialMetaDefinition metadata(RED_NAME_CONSTEXPR("Stage22Probe"));
            MaterialMetaDefinition::Param textureParam;
            textureParam.m_type = IMaterialDataProviderDesc::PT_Texture;
            textureParam.m_register = 0u;
            textureParam.m_textureGroup = GpuWrapApi::TEXG_Generic_Color;
            textureParam.m_name = "ProbeTexture";
            metadata.m_parameters[MSH_PixelShader].PushBack(textureParam);
            const bool parameterCreated = materialTemplate->UpdateParametersList(metadata);
            materialTemplate->MarkModified();

            res::ResourceSavingOptions templateSaving;
            templateSaving.m_depot = &durableDepot;
            templateSaving.m_targetPath = templatePath;
            templateSaved = parameterCreated && res::SaveResource(materialTemplate, templateSaving) && !materialTemplate->IsModified();

            auto materialInstance = CreateHandle<CMaterialInstance>();
            materialInstance->Internal_SetPath(instancePath);
            materialInstance->SetBaseMaterial(materialTemplate);
            const TResRef<ITexture> textureReference(texturePath);
            const bool parameterWritten = materialInstance->WriteParameter(
                RED_NAME_CONSTEXPR("ProbeTexture"), textureReference, false);
            materialInstance->MarkModified();

            res::ResourceSavingOptions instanceSaving;
            instanceSaving.m_depot = &durableDepot;
            instanceSaving.m_targetPath = instancePath;
            instanceSaved = parameterWritten && res::SaveResource(materialInstance, instanceSaving) && !materialInstance->IsModified();

            if (templateSaved && instanceSaved)
            {
                fixtureBindingReady = true;
                database.Update();
            }
        }

        if (templatePreexisting && instancePreexisting)
        {
            res::IssueLoadingRequestParameter editLoad(instancePath);
            editLoad.postLoadFlags.skipUploadRenderData = true;
            const auto editToken = context.SourceLoader().IssueLoadingRequest(editLoad);
            const THandle<CMaterialInstance> editableInstance = editToken ? editToken->WaitUntilLoaded<CMaterialInstance>() : nullptr;
            if (editableInstance)
            {
                TResRef<ITexture> currentTexture;
                const bool parameterRead = editableInstance->ReadParameter(RED_NAME_CONSTEXPR("ProbeTexture"), currentTexture);
                if (parameterRead && currentTexture.GetPath() == texturePath)
                {
                    fixtureBindingReady = true;
                }
                else if (parameterRead)
                {
                    const TResRef<ITexture> editedTexture(texturePath);
                    const bool parameterWritten = editableInstance->WriteParameter(
                        RED_NAME_CONSTEXPR("ProbeTexture"), editedTexture, false);
                    if (parameterWritten)
                    {
                        editableInstance->MarkModified();
                        res::ResourceSavingOptions editSaving;
                        editSaving.m_depot = &durableDepot;
                        editSaving.m_targetPath = instancePath;
                        instanceSaved = res::SaveResource(editableInstance, editSaving) && !editableInstance->IsModified();
                        fixtureBindingReady = instanceSaved;
                        bindingUpdated = instanceSaved;
                        if (instanceSaved)
                            database.Update();
                    }
                }
            }
        }

        red::DynArray<res::ResourcePath> required{red::PoolBackend()};
        required.PushBack(texturePath);
        required.PushBack(templatePath);
        required.PushBack(instancePath);
        const bool databaseReady = templateSaved && instanceSaved && DatabaseEntriesReadyForBuild(database, required);

        bool templateLoaded = false;
        bool instanceLoaded = false;
        bool fixtureReferencesReady = false;
        if (databaseReady)
        {
            res::IssueLoadingRequestParameter templateLoad(templatePath);
            templateLoad.postLoadFlags.skipUploadRenderData = true;
            const auto templateToken = context.SourceLoader().IssueLoadingRequest(templateLoad);
            const THandle<CMaterialTemplate> loadedTemplate = templateToken ? templateToken->WaitUntilLoaded<CMaterialTemplate>() : nullptr;
            templateLoaded = loadedTemplate && loadedTemplate->FindParameter(RED_NAME_CONSTEXPR("ProbeTexture")) != nullptr;

            res::IssueLoadingRequestParameter instanceLoad(instancePath);
            instanceLoad.postLoadFlags.skipUploadRenderData = true;
            const auto instanceToken = context.SourceLoader().IssueLoadingRequest(instanceLoad);
            const THandle<CMaterialInstance> loadedInstance = instanceToken ? instanceToken->WaitUntilLoaded<CMaterialInstance>() : nullptr;
            instanceLoaded = loadedInstance != nullptr;
            if (loadedInstance)
            {
                TResRef<ITexture> loadedTexture;
                const bool parameterRead = loadedInstance->ReadParameter(RED_NAME_CONSTEXPR("ProbeTexture"), loadedTexture);
                fixtureReferencesReady = parameterRead && loadedTexture.GetPath() == texturePath &&
                                         loadedInstance->GetBaseMaterialPath() == templatePath;
            }
        }

        bool tasksSucceeded = false;
        Uint32 taskCount = 0u;
        Uint32 materialReason = 0u;
        Uint32 templateReason = 0u;
        Uint32 textureReason = 0u;
        Uint32 nonzeroReasons = 0u;
        Uint32 filesBuilt = 0u;
        {
            HostReporter reporter;
            data::TaskSystem taskSystem(context, &cooker::CreateTask, reporter, 1);
            red::DynArray<res::ResourcePath> roots{red::PoolBackend()};
            roots.PushBack(instancePath);
            if (databaseReady && templateLoaded && instanceLoaded && fixtureReferencesReady)
                taskSystem.Build(roots);
            const auto tasks = taskSystem.GetAllTasksInOrderOfExecution();
            taskCount = tasks.Size();
            tasksSucceeded = !tasks.Empty();
            bool sawMaterial = false;
            for (const auto& task : tasks)
            {
                if (!task || !task->Succeeded())
                    tasksSucceeded = false;
                if (!task)
                    continue;
                const Uint32 reason = task->GetBuildReason();
                if (reason != 0u)
                    ++nonzeroReasons;
                filesBuilt += task->GetMetrics().filesBuilt;
                if (task->GetPath() == instancePath)
                {
                    sawMaterial = true;
                    materialReason = reason;
                }
                else if (task->GetPath() == templatePath)
                {
                    templateReason = reason;
                }
                else if (task->GetPath() == texturePath)
                {
                    textureReason = reason;
                }
            }
            tasksSucceeded = tasksSucceeded && sawMaterial;
        }

        const bool cookedTemplatePresent = files.FileExist(cookedTemplateAbsolute);
        const bool cookedInstancePresent = files.FileExist(cookedInstanceAbsolute);

        durableDepot.SetCommittedPathSink(nullptr, nullptr);
        system->Shutdown();
        system.Reset();
        GResourceLoader = nullptr;
        sourceLoader->Shutdown();
        sourceLoader.Reset();
        SetResourceDepot(nullptr);
        res::SetEditorDepot(nullptr);
        const auto depotShutdown = durableDepot.Shutdown();
        const bool globalsCleared = GResourceLoader == nullptr && GetResourceDepot() == nullptr && res::GetEditorDepot() == nullptr;

        const bool success = recoveryBeforeSystem && templateSaved && instanceSaved && fixtureBindingReady && databaseReady && templateLoaded && instanceLoaded &&
                             fixtureReferencesReady && tasksSucceeded && cookedTemplatePresent && cookedInstancePresent && globalsCleared &&
                             depotShutdown == vanguard::assets::AssetFileTransactionResult::Success;
        std::printf(
            "STAGE22_MATERIAL_RESULT success=%u edit_mode=%u binding_updated=%u texture_seeded=%u template_preexisting=%u instance_preexisting=%u database_ready=%u template_loaded=%u instance_loaded=%u references_ready=%u tasks=%u material_reason=0x%08X template_reason=0x%08X texture_reason=0x%08X nonzero_reasons=%u files_built=%u cooked_template=%u cooked_instance=%u globals_cleared=%u\n",
            success ? 1u : 0u,
            semanticEdit ? 1u : 0u,
            bindingUpdated ? 1u : 0u,
            textureSeeded ? 1u : 0u,
            templatePreexisting ? 1u : 0u,
            instancePreexisting ? 1u : 0u,
            databaseReady ? 1u : 0u,
            templateLoaded ? 1u : 0u,
            instanceLoaded ? 1u : 0u,
            fixtureReferencesReady ? 1u : 0u,
            taskCount,
            materialReason,
            templateReason,
            textureReason,
            nonzeroReasons,
            filesBuilt,
            cookedTemplatePresent ? 1u : 0u,
            cookedInstancePresent ? 1u : 0u,
            globalsCleared ? 1u : 0u);
        return success ? 0 : 125;
    }

    int RunStage23MeshShadowProof(const red::AbsolutePath& projectRoot, bool skinnedEdit)
    {
        auto& files = vanguard::filesystem::GetManager();
        const red::AbsolutePath authoredRoot = projectRoot.AddDirPath("Assets\\REDAssetHost");
        const red::AbsolutePath sourceRoot = projectRoot.AddDirPath("SourceAssets\\REDAssetHost");
        const red::AbsolutePath sourceModelRoot = sourceRoot.AddDirPath("models");
        const red::AbsolutePath derivedRoot = projectRoot.AddDirPath("DerivedData\\REDAssetHost");
        const red::AbsolutePath cookedRoot = derivedRoot.AddDirPath("Cooked");
        const red::AbsolutePath cookedCacheRoot = derivedRoot.AddDirPath("CookedCache");
        const red::AbsolutePath intermediateRoot = projectRoot.AddDirPath("Intermediate\\REDAssetHost");
        if (!files.CreatePath(authoredRoot) || !files.CreatePath(sourceRoot) || !files.CreatePath(sourceModelRoot) ||
            !files.CreatePath(derivedRoot) || !files.CreatePath(cookedRoot) || !files.CreatePath(cookedCacheRoot) ||
            !files.CreatePath(intermediateRoot))
        {
            std::fprintf(stderr, "ERROR stage23=directories\n");
            return 126;
        }

        const red::AbsolutePath sourcePath = sourceModelRoot.AddFilePath("lieutenantHead.fbx");
        bool sourceSeeded = false;
        if (!files.FileExist(sourcePath))
        {
            constexpr const char* kSourceFixture = "C:\\Users\\Ark\\Desktop\\Model 2\\lieutenantHead.fbx";
            std::ifstream source(kSourceFixture, std::ios::binary);
            std::ofstream destination(sourcePath.AsChar(), std::ios::binary | std::ios::trunc);
            if (!source || !destination)
            {
                std::fprintf(stderr, "ERROR stage23=source-copy-open\n");
                return 127;
            }
            destination << source.rdbuf();
            destination.flush();
            sourceSeeded = source.good() || source.eof();
            sourceSeeded = sourceSeeded && destination.good();
            destination.close();
            source.close();
            sourceSeeded = sourceSeeded && files.FileExist(sourcePath);
            if (!sourceSeeded)
            {
                std::fprintf(stderr, "ERROR stage23=source-copy\n");
                return 128;
            }
        }

        const res::ResourcePath meshPath = res::ResourcePath::Build("models\\stage23_lieutenant_head.w2mesh");
        const res::ResourcePath fallbackMaterialPath = res::ResourcePath::Build("engine\\materials\\metal_base.remt");
        const rtti::ClassType* const meshClass = res::GetResourceClassForFile(meshPath);
        IImporter* const importer = meshClass ? IImporter::FindImporter(meshClass, "fbx") : nullptr;
        auto cooker = ICooker::Create(meshPath);
        const bool importerReady = meshClass != nullptr && importer != nullptr && importer->SupportsResource(meshClass) &&
                                   importer->SupportsFormat("fbx");
        const bool cookerReady = cooker != nullptr && cooker->CanCook(meshPath) && cooker->GetVersion() == 23u;
        if (!importerReady || !cookerReady)
        {
            std::printf("STAGE23_MESH_RESULT success=0 source_seeded=%u importer_ready=%u cooker_ready=%u cooker_version=%u\n",
                        sourceSeeded ? 1u : 0u,
                        importerReady ? 1u : 0u,
                        cookerReady ? 1u : 0u,
                        cooker ? cooker->GetVersion() : 0u);
            return 129;
        }

        red::DynArray<res::prv::DepotMapping> mappings{red::PoolEngine()};
        AddDepotMapping(mappings, authoredRoot, "");
        vanguard::asset_host::DurableResourceDepot durableDepot(mappings);
        vanguard::assets::AssetFileRecoveryReport recovery;
        const red::AbsolutePath journalPath = intermediateRoot.AddFilePath("stage23-mesh-shadow.vtxn");
        if (durableDepot.Initialize(files, authoredRoot, journalPath, &recovery) !=
            vanguard::assets::AssetFileTransactionResult::Success)
        {
            return 130;
        }

        const bool recoveryBeforeSystem = !recovery.journalFound || recovery.rolledBack || recovery.completedCleanup;
        if (!recoveryBeforeSystem)
        {
            static_cast<void>(durableDepot.Shutdown());
            return 131;
        }

        res::SetEditorDepot(&durableDepot);
        SetResourceDepot(&durableDepot);
        auto sourceLoader = res::CreateResourceLoader(&durableDepot, false);
        if (!sourceLoader)
        {
            SetResourceDepot(nullptr);
            res::SetEditorDepot(nullptr);
            static_cast<void>(durableDepot.Shutdown());
            return 132;
        }
        GResourceLoader = sourceLoader.Get();

        auto system = red::CreateUniquePtr<data::SystemInstance, red::PoolBackend>();
        system->SetTargetPlatform(kAssetHostTargetPlatform);
        system->InitSourceDepot();
        system->InitCookedDepot(cookedRoot, cookedCacheRoot);
        system->WaitUntilInitialised();
        data::SystemContext& context = system->CreateContext();
        resdb::Database& database = context.SourceResourceDatabase();
        durableDepot.SetCommittedPathSink(&database, &QueueCommittedPathsToDatabase);

        const red::AbsolutePath fallbackMaterialAbsolute = authoredRoot.AddFilePath(fallbackMaterialPath.ToStringView());
        bool materialSeeded = false;
        bool materialSaved = files.FileExist(fallbackMaterialAbsolute);
        if (!materialSaved)
        {
            auto materialTemplate = CreateHandle<CMaterialTemplate>();
            materialTemplate->Internal_SetPath(fallbackMaterialPath);
            materialTemplate->SetName(RED_NAME_CONSTEXPR("Stage23MetalBase"));
            materialTemplate->SetIsValid(true);
            MaterialMetaDefinition materialMetadata(RED_NAME_CONSTEXPR("Stage23MetalBase"));
            static_cast<void>(materialTemplate->UpdateParametersList(materialMetadata));
            materialTemplate->MarkModified();

            res::ResourceSavingOptions materialSaving;
            materialSaving.m_depot = &durableDepot;
            materialSaving.m_targetPath = fallbackMaterialPath;
            materialSaved = res::SaveResource(materialTemplate, materialSaving) && !materialTemplate->IsModified();
            materialSeeded = materialSaved;
            if (materialSaved)
                database.Update();
        }

        bool materialLoaded = false;
        if (materialSaved)
        {
            res::IssueLoadingRequestParameter materialLoad(fallbackMaterialPath);
            materialLoad.postLoadFlags.skipUploadRenderData = true;
            const auto materialToken = context.SourceLoader().IssueLoadingRequest(materialLoad);
            const THandle<CMaterialTemplate> fallbackMaterial =
                materialToken ? materialToken->WaitUntilLoaded<CMaterialTemplate>() : nullptr;
            materialLoaded = fallbackMaterial != nullptr && fallbackMaterial->IsValid();
        }

        const red::AbsolutePath authoredMeshAbsolute = authoredRoot.AddFilePath(meshPath.ToStringView());
        const red::AbsolutePath cookedMeshAbsolute = cookedRoot.AddFilePath(meshPath.ToStringView());
        const bool authoredPreexisting = files.FileExist(authoredMeshAbsolute);
        const bool cookedPreexisting = files.FileExist(cookedMeshAbsolute);
        const Uint64 cookedTimestampBefore = cookedPreexisting ? files.GetFileTimeStamp(cookedMeshAbsolute) : 0ull;

        bool importedThisRun = false;
        bool reimportedThisRun = false;
        bool skinnedOptionUpdated = false;
        bool authoredReady = authoredPreexisting;
        if (materialLoaded)
        {
            THandle<ResourceImportOptions> options = importer->CreateOptions(sourcePath);
            THandle<CResource> target;
            bool shouldImport = !authoredPreexisting;
            if (authoredPreexisting)
            {
                res::IssueLoadingRequestParameter currentLoad(meshPath);
                currentLoad.postLoadFlags.skipUploadRenderData = true;
                const auto currentToken = context.SourceLoader().IssueLoadingRequest(currentLoad);
                target = currentToken ? currentToken->WaitUntilLoaded() : nullptr;
                if (target && options)
                {
                    bool copiedSuggestedOptions = false;
                    const THandle<res::ResourceMetadata> currentMetadata = res::ResourceMetadata::Get(target);
                    if (currentMetadata && currentMetadata->GetImportData())
                    {
                        const THandle<ResourceImportOptions>& suggested =
                            currentMetadata->GetImportData()->GetImportOptions();
                        if (suggested && suggested->GetClass() == options->GetClass() && !options->m_skipSuggestedOptions)
                        {
                            suggested->GetClass()->Copy(options.Get(), suggested.Get());
                            copiedSuggestedOptions = true;
                        }
                    }
                    if (!copiedSuggestedOptions)
                        shouldImport = true;
                }
            }
            else
            {
                target = meshClass->CreateHandle<CResource>();
            }

            if (options && target)
            {
                const rtti::Property* const skinnedProperty =
                    options->GetClass()->FindProperty(RED_NAME_CONSTEXPR_NOREG("importAsSkinnedMesh"));
                if (!skinnedProperty)
                {
                    authoredReady = false;
                }
                else
                {
                    Bool currentSkinned = false;
                    skinnedProperty->Get(options.Get(), &currentSkinned);
                    if (currentSkinned != static_cast<Bool>(skinnedEdit))
                    {
                        shouldImport = true;
                        const Bool desiredSkinned = skinnedEdit;
                        skinnedProperty->Set(options.Get(), &desiredSkinned);
                        skinnedOptionUpdated = true;
                    }
                }

                options->SetSourcePath(sourcePath);
                options->SetResourcePath(meshPath);
                target->Internal_SetPath(meshPath);
                if (shouldImport && skinnedProperty)
                {
                    ImportResult importResult = importer->DoImport(target, options);
                    if (importResult.IsSuccessful() && importResult.GetResources().Size() == 1u &&
                        importResult.GetResources()[0] && importResult.GetResources()[0]->GetClass() == meshClass)
                    {
                        THandle<CResource> imported = importResult.GetResources()[0];
                        imported->Internal_SetPath(meshPath);
                        res::ResourceMetadata::SetImportOptions(imported, options);
                        imported->MarkModified();
                        res::ResourceSavingOptions meshSaving;
                        meshSaving.m_depot = &durableDepot;
                        meshSaving.m_targetPath = meshPath;
                        authoredReady = res::SaveResource(imported, meshSaving) && !imported->IsModified();
                        importedThisRun = authoredReady;
                        reimportedThisRun = authoredPreexisting && authoredReady;
                        if (authoredReady)
                            database.Update();
                    }
                    else if (importResult.HasMessage())
                    {
                        authoredReady = false;
                        std::fprintf(stderr, "ERROR stage23=import message=%hs\n", importResult.GetErrorMessage().AsChar());
                    }
                }
            }
        }

        red::DynArray<res::ResourcePath> required{red::PoolBackend()};
        required.PushBack(meshPath);
        required.PushBack(fallbackMaterialPath);
        const bool databaseReady = authoredReady && materialSaved && DatabaseEntriesReadyForBuild(database, required);

        bool meshLoaded = false;
        bool renderBlobReady = false;
        bool importMetadataReady = false;
        bool skinnedOptionReady = false;
        Uint32 lodCount = 0u;
        Uint32 chunkCount = 0u;
        Uint32 skinnedChunkCount = 0u;
        Uint32 materialCount = 0u;
        Uint32 boneCount = 0u;
        Uint32 appearanceCount = 0u;
        if (databaseReady)
        {
            res::IssueLoadingRequestParameter meshLoad(meshPath);
            meshLoad.postLoadFlags.skipUploadRenderData = true;
            const auto meshToken = context.SourceLoader().IssueLoadingRequest(meshLoad);
            const THandle<CMesh> loadedMesh = meshToken ? meshToken->WaitUntilLoaded<CMesh>() : nullptr;
            if (loadedMesh)
            {
                mesh::DataViewRO view(*loadedMesh);
                lodCount = view.GetNumLODs();
                chunkCount = view.GetNumChunks();
                materialCount = view.GetNumMaterials();
                boneCount = view.GetNumBones();
                appearanceCount = view.GetNumAppearances();
                for (Uint32 chunkIndex = 0u; chunkIndex < chunkCount; ++chunkIndex)
                {
                    if (MeshUtilities::HasSkinning(view.GetChunkVertexType(static_cast<mesh::ChunkIndex>(chunkIndex))))
                        ++skinnedChunkCount;
                }
                renderBlobReady = loadedMesh->GetRenderResourceBlob() != nullptr;
                meshLoaded = lodCount > 0u && chunkCount > 0u;

                const auto materialNames = view.GetMaterialNames();
                for (const CName name : materialNames)
                    std::printf("STAGE23_MESH_MATERIAL name=%hs\n", name.AsChar());

                const THandle<res::ResourceMetadata> resourceMetadata = res::ResourceMetadata::Get(loadedMesh);
                const THandle<res::ResourceImportMetadata> importMetadata =
                    resourceMetadata ? resourceMetadata->GetImportData() : nullptr;
                const THandle<ResourceImportOptions> importOptions =
                    importMetadata ? importMetadata->GetImportOptions() : nullptr;
                importMetadataReady = importOptions && importOptions->GetSourcePath() == sourcePath &&
                                      importOptions->GetResourcePath() == meshPath;
                if (importMetadataReady)
                {
                    const rtti::Property* const skinnedProperty =
                        importOptions->GetClass()->FindProperty(RED_NAME_CONSTEXPR_NOREG("importAsSkinnedMesh"));
                    if (skinnedProperty)
                    {
                        Bool storedSkinned = false;
                        skinnedProperty->Get(importOptions.Get(), &storedSkinned);
                        skinnedOptionReady = storedSkinned == static_cast<Bool>(skinnedEdit);
                    }
                }
            }
        }

        bool tasksSucceeded = false;
        bool sawMeshTask = false;
        Uint32 taskCount = 0u;
        Uint32 meshReason = 0u;
        Uint32 materialReason = 0u;
        Uint32 nonzeroReasons = 0u;
        Uint32 filesBuilt = 0u;
        {
            HostReporter reporter;
            data::TaskSystem taskSystem(context, &cooker::CreateTask, reporter, 1);
            red::DynArray<res::ResourcePath> roots{red::PoolBackend()};
            roots.PushBack(meshPath);
            if (databaseReady && meshLoaded && renderBlobReady && importMetadataReady && skinnedOptionReady)
                taskSystem.Build(roots);
            const auto tasks = taskSystem.GetAllTasksInOrderOfExecution();
            taskCount = tasks.Size();
            tasksSucceeded = !tasks.Empty();
            for (const auto& task : tasks)
            {
                if (!task || !task->Succeeded())
                    tasksSucceeded = false;
                if (!task)
                    continue;
                const Uint32 reason = task->GetBuildReason();
                if (reason != 0u)
                    ++nonzeroReasons;
                filesBuilt += task->GetMetrics().filesBuilt;
                if (task->GetPath() == meshPath)
                {
                    sawMeshTask = true;
                    meshReason = reason;
                }
                else if (task->GetPath() == fallbackMaterialPath)
                {
                    materialReason = reason;
                }
            }
            tasksSucceeded = tasksSucceeded && sawMeshTask;
        }

        const bool cookedMeshPresent = files.FileExist(cookedMeshAbsolute);
        const Uint64 cookedTimestampAfter = cookedMeshPresent ? files.GetFileTimeStamp(cookedMeshAbsolute) : 0ull;
        const bool restartReuse = authoredPreexisting && cookedPreexisting && meshReason == 0u &&
                                  cookedTimestampAfter == cookedTimestampBefore;

        durableDepot.SetCommittedPathSink(nullptr, nullptr);
        system->Shutdown();
        system.Reset();
        GResourceLoader = nullptr;
        sourceLoader->Shutdown();
        sourceLoader.Reset();
        SetResourceDepot(nullptr);
        res::SetEditorDepot(nullptr);
        const auto depotShutdown = durableDepot.Shutdown();
        const bool globalsCleared =
            GResourceLoader == nullptr && GetResourceDepot() == nullptr && res::GetEditorDepot() == nullptr;

        const bool sourceReady = files.FileExist(sourcePath);
        const bool fullSuccess = recoveryBeforeSystem && sourceReady && importerReady && cookerReady && materialSaved && materialLoaded &&
                                 authoredReady && databaseReady && meshLoaded && renderBlobReady &&
                                 importMetadataReady && skinnedOptionReady && tasksSucceeded && cookedMeshPresent && globalsCleared &&
                                 depotShutdown == vanguard::assets::AssetFileTransactionResult::Success;
        std::printf(
            "STAGE23_MESH_RESULT success=%u skinned_edit=%u source_seeded=%u importer_ready=%u cooker_ready=%u cooker_version=%u material_seeded=%u material_loaded=%u authored_preexisting=%u imported_this_run=%u reimported_this_run=%u option_updated=%u database_ready=%u mesh_loaded=%u lods=%u chunks=%u skinned_chunks=%u materials=%u bones=%u appearances=%u render_blob=%u import_metadata=%u option_ready=%u tasks=%u mesh_reason=0x%08X material_reason=0x%08X nonzero_reasons=%u files_built=%u cooked_mesh=%u restart_reuse=%u globals_cleared=%u\n",
            fullSuccess ? 1u : 0u,
            skinnedEdit ? 1u : 0u,
            sourceSeeded ? 1u : 0u,
            importerReady ? 1u : 0u,
            cookerReady ? 1u : 0u,
            cooker ? cooker->GetVersion() : 0u,
            materialSeeded ? 1u : 0u,
            materialLoaded ? 1u : 0u,
            authoredPreexisting ? 1u : 0u,
            importedThisRun ? 1u : 0u,
            reimportedThisRun ? 1u : 0u,
            skinnedOptionUpdated ? 1u : 0u,
            databaseReady ? 1u : 0u,
            meshLoaded ? 1u : 0u,
            lodCount,
            chunkCount,
            skinnedChunkCount,
            materialCount,
            boneCount,
            appearanceCount,
            renderBlobReady ? 1u : 0u,
            importMetadataReady ? 1u : 0u,
            skinnedOptionReady ? 1u : 0u,
            taskCount,
            meshReason,
            materialReason,
            nonzeroReasons,
            filesBuilt,
            cookedMeshPresent ? 1u : 0u,
            restartReuse ? 1u : 0u,
            globalsCleared ? 1u : 0u);
        return fullSuccess ? 0 : 133;
    }

    int ArmStage11DatabaseRecovery(const red::AbsolutePath& scratchRoot)
    {
        auto& files = vanguard::filesystem::GetManager();
        const red::AbsolutePath authoredRoot = scratchRoot.AddDirPath("stage11-authored");
        if (!files.CreatePath(authoredRoot))
            return 80;

        Register2DArraySaver();
        const res::ResourcePath pathA = res::ResourcePath::Build("stage11_a.csv");
        const res::ResourcePath pathB = res::ResourcePath::Build("stage11_b.csv");
        red::DynArray<Uint8> baseA{red::PoolBackend()};
        red::DynArray<Uint8> baseB{red::PoolBackend()};
        red::DynArray<Uint8> interruptedA{red::PoolBackend()};
        red::DynArray<Uint8> interruptedB{red::PoolBackend()};
        if (!SerializeCsvImage(pathA, "name;value\ndep;base\\stage11_dep_b.csv\nbase-a;1\n", baseA) ||
            !SerializeCsvImage(pathB, "name;value\nbase-b;2\n", baseB) ||
            !SerializeCsvImage(pathA, "name;value\ninterrupted-a;999\n", interruptedA) ||
            !SerializeCsvImage(pathB, "name;value\ninterrupted-b;999\n", interruptedB))
            return 81;

        const red::AbsolutePath absoluteA = authoredRoot.AddFilePath("stage11_a.csv");
        const red::AbsolutePath absoluteB = authoredRoot.AddFilePath("stage11_b.csv");
        if (!WriteExactFile(files, absoluteA, baseA) || !WriteExactFile(files, absoluteB, baseB))
            return 82;

        const red::AbsolutePath roots[] = {authoredRoot};
        vanguard::assets::AssetFileTransactionConfig config;
        config.journalPath = scratchRoot.AddFilePath("stage11-resource-database.vtxn");
        config.writableRoots = {roots, 1u};
        config.assertionRoots = {roots, 1u};
        vanguard::assets::AssetFileRecoveryReport recovery;
        vanguard::assets::AssetFileTransaction transaction;
        if (transaction.Initialize(files, config, &recovery) != vanguard::assets::AssetFileTransactionResult::Success)
            return 83;

        const bool armed = transaction.Begin() == vanguard::assets::AssetFileTransactionResult::Success &&
                           transaction.StageWrite(absoluteA, {interruptedA.TypedData(), interruptedA.Size()}) ==
                               vanguard::assets::AssetFileTransactionResult::Success &&
                           transaction.StageWrite(absoluteB, {interruptedB.TypedData(), interruptedB.Size()}) ==
                               vanguard::assets::AssetFileTransactionResult::Success &&
                           transaction.Prepare() == vanguard::assets::AssetFileTransactionResult::Success &&
                           transaction.ApplyPrepared() == vanguard::assets::AssetFileTransactionResult::Success &&
                           FileMatches(files, absoluteA, interruptedA) && FileMatches(files, absoluteB, interruptedB) &&
                           files.FileExist(config.journalPath);
        std::printf("STAGE11_DB_ARM_RESULT success=%u applying_a=%u applying_b=%u journal_present=%u\n",
                    armed ? 1u : 0u,
                    FileMatches(files, absoluteA, interruptedA) ? 1u : 0u,
                    FileMatches(files, absoluteB, interruptedB) ? 1u : 0u,
                    files.FileExist(config.journalPath) ? 1u : 0u);
        std::fflush(stdout);
        if (armed)
            std::_Exit(0);
        return 84;
    }
}

int main( const int argc, const char* const* const argv )
{
    const bool gate0 = argc == 3 && std::strcmp(argv[1], "--gate0") == 0;
    const bool gate0ArmRecovery = argc == 3 && std::strcmp(argv[1], "--gate0-arm-recovery") == 0;
    const bool gate0Cohort = argc == 3 && std::strcmp(argv[1], "--gate0-cohort") == 0;
    const bool gate0CohortArmRecovery = argc == 3 && std::strcmp(argv[1], "--gate0-cohort-arm-recovery") == 0;
    const bool gate0Mapped = argc == 3 && std::strcmp(argv[1], "--gate0-mapped") == 0;
    const bool stage11Database = argc == 3 && std::strcmp(argv[1], "--stage1-db") == 0;
    const bool stage11DatabaseArmRecovery = argc == 3 && std::strcmp(argv[1], "--stage1-db-arm-recovery") == 0;
    const bool stage12System = argc == 3 && std::strcmp(argv[1], "--stage1-system") == 0;
    const bool stage13Texture = argc == 3 && std::strcmp(argv[1], "--stage1-texture") == 0;
    const bool stage13TextureReimport = argc == 3 && std::strcmp(argv[1], "--stage1-texture-reimport") == 0;
    const bool stage21TextureShadow = argc == 3 && std::strcmp(argv[1], "--stage2-texture-shadow") == 0;
    const bool stage22MaterialShadow = argc == 3 && std::strcmp(argv[1], "--stage2-material-shadow") == 0;
    const bool stage22MaterialShadowEdit = argc == 3 && std::strcmp(argv[1], "--stage2-material-shadow-edit") == 0;
    const bool stage23MeshShadow = argc == 3 && std::strcmp(argv[1], "--stage2-mesh-shadow") == 0;
    const bool stage23MeshShadowSkinned = argc == 3 && std::strcmp(argv[1], "--stage2-mesh-shadow-skinned") == 0;
    if ( argc != 2 && !gate0 && !gate0ArmRecovery && !gate0Cohort && !gate0CohortArmRecovery && !gate0Mapped &&
         !stage11Database && !stage11DatabaseArmRecovery && !stage12System && !stage13Texture && !stage13TextureReimport &&
         !stage21TextureShadow && !stage22MaterialShadow && !stage22MaterialShadowEdit && !stage23MeshShadow && !stage23MeshShadowSkinned )
    {
        std::fprintf( stderr, "Usage: VanguardAssetHost [--gate0|--gate0-arm-recovery|--gate0-cohort|--gate0-cohort-arm-recovery|--gate0-mapped|--stage1-db|--stage1-db-arm-recovery|--stage1-system|--stage1-texture|--stage1-texture-reimport|--stage2-texture-shadow|--stage2-material-shadow|--stage2-material-shadow-edit|--stage2-mesh-shadow|--stage2-mesh-shadow-skinned] <absolute-root>\n" );
        return 2;
    }

    if ( !vanguard::memory::Initialize() || !vanguard::containers::Initialize() )
    {
        return 3;
    }

    const bool gatedMode = gate0 || gate0ArmRecovery || gate0Cohort || gate0CohortArmRecovery || gate0Mapped ||
                           stage11Database || stage11DatabaseArmRecovery || stage12System || stage13Texture || stage13TextureReimport ||
                           stage21TextureShadow || stage22MaterialShadow || stage22MaterialShadowEdit || stage23MeshShadow || stage23MeshShadowSkinned;
    const red::StringView scratchText( argv[gatedMode ? 2 : 1] );
    if ( !red::AbsolutePath::IsValidPath( scratchText ) )
    {
        std::fprintf( stderr, "ERROR stage=path-invalid\n" );
        return 4;
    }
    const red::AbsolutePath scratchRoot = red::AbsolutePath::CreateDirPath( scratchText );
    if ( stage12System && !IsStage12DummyProjectRoot( argv[2] ) )
    {
        std::fprintf( stderr, "ERROR stage12=project-identity-mismatch\n" );
        return 6;
    }
    if ( stage13Texture && !IsStage12DummyProjectRoot( argv[2] ) )
    {
        std::fprintf( stderr, "ERROR stage13=project-identity-mismatch\n" );
        return 6;
    }
    if ( stage13TextureReimport && !IsStage12DummyProjectRoot( argv[2] ) )
    {
        std::fprintf( stderr, "ERROR stage13=project-identity-mismatch\n" );
        return 6;
    }
    if ( stage21TextureShadow && !IsStage12DummyProjectRoot( argv[2] ) )
    {
        std::fprintf( stderr, "ERROR stage21=project-identity-mismatch\n" );
        return 6;
    }
    if ( (stage22MaterialShadow || stage22MaterialShadowEdit) && !IsStage12DummyProjectRoot( argv[2] ) )
    {
        std::fprintf( stderr, "ERROR stage22=project-identity-mismatch\n" );
        return 6;
    }
    if ( (stage23MeshShadow || stage23MeshShadowSkinned) && !IsStage12DummyProjectRoot( argv[2] ) )
    {
        std::fprintf( stderr, "ERROR stage23=project-identity-mismatch\n" );
        return 6;
    }
    const red::AbsolutePath runtimeCacheRoot = (stage12System || stage13Texture || stage13TextureReimport || stage21TextureShadow || stage22MaterialShadow || stage22MaterialShadowEdit || stage23MeshShadow || stage23MeshShadowSkinned)
        ? scratchRoot.AddDirPath("DerivedData\\REDAssetHost\\SourceCache")
        : scratchRoot;

    RuntimeState runtime;
    if ( !InitializeRuntime( scratchRoot, runtime, runtimeCacheRoot ) )
    {
        ShutdownRuntime( runtime );
        return 5;
    }

    if ( stage13Texture || stage13TextureReimport || stage21TextureShadow || stage22MaterialShadow || stage22MaterialShadowEdit )
        InitializeTextureDomainRuntime();
    else if ( stage23MeshShadow || stage23MeshShadowSkinned )
        InitializeMeshDomainRuntime();

    const int result = gate0 ? RunGate0Proof(scratchRoot)
                             : (gate0ArmRecovery ? ArmGate0Recovery(scratchRoot)
                                : (gate0Cohort ? RunGate0CohortProof(scratchRoot)
                                   : (gate0CohortArmRecovery ? ArmGate0CohortRecovery(scratchRoot)
                                      : (gate0Mapped ? RunGate0MappedDepotProof(scratchRoot)
                                         : (stage11Database ? RunStage11DatabaseProof(scratchRoot)
                                            : (stage11DatabaseArmRecovery ? ArmStage11DatabaseRecovery(scratchRoot)
                                               : (stage12System ? RunStage12SystemProof(scratchRoot)
                                                  : (stage13Texture ? RunStage13TextureProof(scratchRoot, false)
                                                     : (stage13TextureReimport ? RunStage13TextureProof(scratchRoot, true)
                                                        : (stage21TextureShadow ? RunStage21TextureShadowProof(scratchRoot)
                                                           : (stage22MaterialShadow ? RunStage22MaterialShadowProof(scratchRoot, false)
                                                              : (stage22MaterialShadowEdit ? RunStage22MaterialShadowProof(scratchRoot, true)
                                                                 : (stage23MeshShadow ? RunStage23MeshShadowProof(scratchRoot, false)
                                                                    : (stage23MeshShadowSkinned ? RunStage23MeshShadowProof(scratchRoot, true)
                                                                       : RunOneCook(scratchRoot)))))))))))))));
    ShutdownRuntime( runtime );
    return result;
}
