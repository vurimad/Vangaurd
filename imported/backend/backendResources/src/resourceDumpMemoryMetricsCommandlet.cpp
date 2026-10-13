#include "build.h"
#include "../../../common/core/include/commandlet.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../common/redSystem/include/log.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/resource/include/backendResourceDepot.h"
#include "../../../common/redReflection/include/serializationFileTables.h"
#include "../../../common/redReflection/include/serializationLoader.h"
#include "../../../common/redReflection/include/serializationBinaryRuntimeTables.h"
#include "../../../common/redFileSystem/include/memoryFileReader.h"
#include "../../backendEngine/include/commandletEngineInstance.h"
#include "../../backendEngine/include/commandletRenderInstance.h"
#include "../../backendEngine/include/commandletUtils.h"
#include "../../backendEngine/include/resourceUtils.h"

namespace
{
	struct Settings
	{
		res::IBackendResourceDepot* depot;
		res::ResourceLoader* loader;

		Bool skipImports;
		Bool skipPostLoad;
	};

	struct SingleResourceMetrics
	{
		red::memory::RuntimeMetrics globalMetrics;
	};

	void ComputeFinalMemoryMetricsForResource( SingleResourceMetrics& memoryMetrics, const SingleResourceMetrics& initialMemoryMetrics, Uint32 renderBlobSize )
	{
		RED_ASSERT( memoryMetrics.globalMetrics.cpuBytesAllocated >= initialMemoryMetrics.globalMetrics.cpuBytesAllocated );
		memoryMetrics.globalMetrics.cpuBytesAllocated -= initialMemoryMetrics.globalMetrics.cpuBytesAllocated;
		RED_ASSERT( memoryMetrics.globalMetrics.gpuBytesAllocated >= initialMemoryMetrics.globalMetrics.gpuBytesAllocated );
		memoryMetrics.globalMetrics.gpuBytesAllocated -= initialMemoryMetrics.globalMetrics.gpuBytesAllocated;
		memoryMetrics.globalMetrics.gpuBytesAllocated += renderBlobSize;
		RED_ASSERT( memoryMetrics.globalMetrics.totalBytesAllocated >= initialMemoryMetrics.globalMetrics.totalBytesAllocated );
		memoryMetrics.globalMetrics.totalBytesAllocated -= initialMemoryMetrics.globalMetrics.totalBytesAllocated;
		memoryMetrics.globalMetrics.totalBytesAllocated += renderBlobSize;
	}

	Bool IsValidResource( const res::ResourcePath& resourcePath )
	{
		if ( IsNotResource( resourcePath ) )
		{
			return false;
		}

		const red::StringView resourceExtension = red::paths::GetExtension( resourcePath.ToStringView() );
		if ( resourceExtension == "link" ||
			 resourceExtension == "tweak" )
		{
			return false;
		}

		if ( resourceExtension == "sa" || resourceExtension == "ss" )
		{
			return false;
		}

		return true;
	}

	THandle< CResource > LoadResource( const res::ResourcePath& resPath, res::IBackendResourceDepot* depot, red::DynArray< Uint8 >& fileData, Bool skipImports, Bool skipPostLoad )
	{
		// read whole file into memory in one go
		{
			red::SharedPtr< IFile > filePtr = depot->CreateResourceReader( resPath, false );
			if ( !filePtr )
			{
				return nullptr;
			}
			filePtr->SetCooker( true );

			RED_FATAL_ASSERT( filePtr->GetSize() < UINT32_MAX );
			fileData.Resize( static_cast<Uint32>( filePtr->GetSize() ) );
			filePtr->Serialize( fileData.Data(), filePtr->GetSize() );
		}

		CMemoryFileReader memoryFileReader( fileData, 0 );

		Uint32 version = 0;
		serialization::FileTables fileTables;
		if ( !fileTables.Load( memoryFileReader, version ) )
		{
			return nullptr;
		}
		memoryFileReader.m_version = version;

		serialization::LoadingContext context;
		context.m_skipImports = skipImports;
		context.m_skipPostLoad = skipPostLoad;

		serialization::RuntimeTables runtimeTables{ serialization::RuntimeTables::eLoadExportsFromFile };
		runtimeTables.Resolve( context, fileTables );

		runtimeTables.CreateExports( context, fileTables );

		// Unused since no asyncSource
		job::Counter nullCounter;

		serialization::LoadingResult result;
		runtimeTables.LoadExports( 0, memoryFileReader, nullCounter, context, fileTables, result );

		if ( result.m_loadedRootObjects.Empty() )
		{
			return nullptr;
		}

		return Cast< CResource >( result.m_loadedRootObjects.Front() );
	}

	class OutputWriter
	{
	public:
		OutputWriter( const red::AbsolutePath& outputPath )
			: m_output( stdout )
		{
			if ( !outputPath.Empty() )
			{
				FILE* stream = fopen( outputPath.AsChar(), "w" );
				if ( stream != nullptr )
				{
					m_output = stream;
				}
			}

			fprintf( m_output, "ResourcePath;SkipImports;SkipPostLoad;TotalBytesAllocated;CPUBytesAllocated;GPUBytesAllocated\n" );
		}

		~OutputWriter()
		{
			fflush( m_output );

			if ( m_output != stdout )
			{
				fclose( m_output );
			}
		}

		void OutputResourceMemoryMetrics( const res::ResourcePath resPath, const Settings& settings, const SingleResourceMetrics& memoryMetrics )
		{
			fprintf(m_output, "%hs;%u;%u;%llu;%llu;%llu\n",
				resPath.ToDebugString(),
				settings.skipImports,
				settings.skipPostLoad,
				memoryMetrics.globalMetrics.totalBytesAllocated,
				memoryMetrics.globalMetrics.cpuBytesAllocated,
				memoryMetrics.globalMetrics.gpuBytesAllocated );
		}

	protected:
		FILE* m_output;
	};
}

//------------------------------------------------------------------------------

// Commandlet to dump resource memory metrics
class ResourceDumpMemoryMetricsCommandlet final : public red::ICommandlet
{
	RTTI_DECLARE_TYPE( ResourceDumpMemoryMetricsCommandlet );

public:
	ResourceDumpMemoryMetricsCommandlet();
	~ResourceDumpMemoryMetricsCommandlet();

	virtual Bool Execute( const red::CommandLine& options ) override;
	virtual const char* GetOneLiner() const override;
	virtual void PrintHelp() const override;

	virtual Bool InitializeEnvironment() override;
	virtual void TeardownEnvironment() override;

private:
	Bool CollectResourceMemoryMetrics( const res::ResourcePath& resPath, SingleResourceMetrics& memoryMetrics, const Settings& settings );

	red::UniquePtr< OutputWriter > m_output;

	commandlet::EngineInstance m_engineInstance;
	commandlet::RenderInstance m_renderInstance;
};

RTTI_BEGIN_TYPE( ResourceDumpMemoryMetricsCommandlet );
	RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE();

//------------------------------------------------------------------------------

ResourceDumpMemoryMetricsCommandlet::ResourceDumpMemoryMetricsCommandlet()
	: red::ICommandlet( RED_NAME_CONSTEXPR( "resourceDumpMemoryMetrics" ) )
{}

ResourceDumpMemoryMetricsCommandlet::~ResourceDumpMemoryMetricsCommandlet()
{}

Bool ResourceDumpMemoryMetricsCommandlet::Execute( const red::CommandLine& commandLine )
{
	commandlet::BackendDepotHolder depotHolder( commandLine );

	Settings settings;
	settings.skipImports = true;
	settings.skipPostLoad = true;
	settings.depot = depotHolder.GetDepot();
	settings.loader = depotHolder.GetLoader();

	red::AbsolutePath outputPath;

	String param;
	if ( commandLine.GetFirstOption( "output" , param ) )
	{
		outputPath = red::AbsolutePath::ParseFilePath( param );
	}

	if ( commandLine.HasOption( "file" ) )
	{
		settings.skipImports = commandLine.HasOption( "skipImports" );
		settings.skipPostLoad = commandLine.HasOption( "skipPostLoad" );
	}

	red::DynArray< res::ResourcePath > resources = commandlet::ParseResourcePathsFromCommandLine( *depotHolder.GetDepot(), commandLine );
	if ( resources.Empty() )
	{
		if ( commandLine.HasOption( "production" ) )
		{
			resources.PushBack( commandlet::ScanResourceDepotDirectories( *depotHolder.GetDepot(), "engine\\" ) );
			resources.PushBack( commandlet::ScanResourceDepotDirectories( *depotHolder.GetDepot(), "base\\" ) );
		}
	}

	OutputWriter writer{ outputPath };

	for ( Uint32 i = 0; i < resources.Size(); ++i )
	{
		const auto& resourcePath = resources[i];

		if ( !IsValidResource( resourcePath ) )
		{
			continue;
		}

		RED_LOG_INFO( "[%u/%u] Processing %hs\n", i + 1, resources.Size(), resourcePath.ToDebugString() );
		RED_LOG_FLUSH_AND_WAIT();

		SingleResourceMetrics resourceMemoryMetrics;
		red::Memzero( &resourceMemoryMetrics, sizeof( SingleResourceMetrics ) );

		if ( !CollectResourceMemoryMetrics( resourcePath, resourceMemoryMetrics, settings ) )
		{
			RED_LOG_ERROR( "Failed to collect memory metrics for resource '%hs'", resourcePath.ToDebugString() );
		}
		else
		{
			writer.OutputResourceMemoryMetrics( resourcePath, settings, resourceMemoryMetrics );
		}
	}

	return true;
}

const char* ResourceDumpMemoryMetricsCommandlet::GetOneLiner() const
{
	return "Dump memory metrics for requested resources";
}

void ResourceDumpMemoryMetricsCommandlet::PrintHelp() const
{
	RED_LOG( "Usage: " );
	RED_LOG( " resourceDumpMemoryMetrics -output=<file> (-all|-dir=<path>|-file=<path>) [-skipImports][-skipPostLoad]" );
	RED_LOG( "Parameters: " );
	RED_LOG( "  -output=<file>         - output path to dump memory metrics to" );
	RED_LOG( "  -all                   - collect memory metrics for all resources from the archive" );
	RED_LOG( "  -production            - collect memory metrics for all resources in the production depot" );
	RED_LOG( "  -dir                   - collect memory metrics for all resources from the specified directory" );
	RED_LOG( "  -file=<path>           - collect memory metrics for specified resource" );
	RED_LOG( "  -skipImports           - skip loading of the resource imports. Works only with -file option. For -all and -dir imports are always skipped." );
	RED_LOG( "  -skipPostLoad          - skip execution of the resource post load job. Works only with -file option. For -all and -dir post loads are always skipped." );
}

Bool ResourceDumpMemoryMetricsCommandlet::InitializeEnvironment()
{
	return m_engineInstance.Initialise() && m_renderInstance.Initialise();
}

void ResourceDumpMemoryMetricsCommandlet::TeardownEnvironment()
{
	m_renderInstance.Shutdown();

	m_engineInstance.Shutdown();
}

Bool ResourceDumpMemoryMetricsCommandlet::CollectResourceMemoryMetrics( const res::ResourcePath& resPath, SingleResourceMetrics& memoryMetrics, const Settings& settings )
{
	if ( GResourceLoader->IsResourceLoaded( resPath ) )
	{
		RED_LOG_ERROR( "Resource '%hs' is already loaded", resPath.ToDebugString() );
		return false;
	}

	if ( IsNotResource( resPath ) )
	{
		return false;
	}

	SingleResourceMetrics initialMemoryMetrics;
	red::Memzero( &initialMemoryMetrics, sizeof( SingleResourceMetrics ) );
	red::memory::GetRuntimeMetrics( initialMemoryMetrics.globalMetrics );

	red::DynArray< Uint8 > fileData;

	THandle< CResource > resource = LoadResource( resPath, settings.depot, fileData, settings.skipImports, settings.skipPostLoad );
	if ( !resource )
	{
		RED_LOG_ERROR( "Failed to load '%hs'", resPath.ToDebugString() );
		return false;
	}

	red::memory::GetRuntimeMetrics( memoryMetrics.globalMetrics );

	ComputeFinalMemoryMetricsForResource( memoryMetrics, initialMemoryMetrics, resource->GetRenderResourceSize() );

	return true;
}