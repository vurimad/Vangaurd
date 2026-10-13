/**
* Copyright (C)2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "../../../common/core/include/commandlet.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../common/resource/include/basicTextBuilder.h"
#include "../../../common/resource/include/resourceEditorDepotImpl.h"
#include "../../../common/resourceMaterial/include/multilayerMask.h"
#include "../../../common/resourceMaterial/include/sourceTextureData.h"
#include "../../backendData/include/resourceDatabase.h"
#include "../../backendEngine/include/commandletEngineInstance.h"
#include "../../backendEngine/include/commandletUtils.h"
#include "gatherResourceDataCommandletUtils.h"
#include "../../../common/redFileSystem/include/fileSys.h"

namespace tools
{
	struct MLMaskRowData;

	// TODO[PB]: Extract common parts with GatherM
	class GatherMLMaskDataCommandlet: public red::ICommandlet
	{
		RTTI_DECLARE_TYPE( GatherMLMaskDataCommandlet );

	public:
		GatherMLMaskDataCommandlet();
		~GatherMLMaskDataCommandlet();

		virtual Bool Execute( const red::CommandLine& options ) override;
		virtual const char* GetOneLiner() const override;
		virtual void PrintHelp() const override;

		virtual Bool InitializeEnvironment() override;
		virtual void TeardownEnvironment() override;

	private:
		struct Settings;

		Bool ParseSettings( const red::CommandLine& options, Settings& settingsFilePath );
		void InitialiseDatabase( commandlet::BackendDepotHolder& depotHolder );

		void ProcessResource( const res::ResourcePath& resourcePath, red::DynArray< MLMaskRowData >& outMLMasksData );
		Uint32 GetNumberOfReferences( const res::ResourcePath& path ) const;

		Uint32 GetMLMaskBias( THandle< Multilayer_Mask >& mlMask, ECookingPlatform platform );
		Uint64 GetResourceFileSize( const res::ResourcePath& path );

		Bool SaveMLMasksAsCSV( const red::AbsolutePath& outFilePath, const red::DynArray< MLMaskRowData >& objects ) const;

		commandlet::EngineInstance m_engineInstance;
		res::IBackendResourceDepot* m_depot;
		resdb::Database m_database;
	};

	struct GatherMLMaskDataCommandlet::Settings
	{
		res::ResourcePath m_directoryPath;
		red::AbsolutePath m_outputFile;
	};

	struct MLMaskRowData
	{
		res::ResourcePath m_path;
		Uint32 m_width = 0u;
		Uint32 m_height = 0u;
		Uint32 m_layers = 0u;
		Uint32 m_biasPC = 0u;
		Uint32 m_biasConsole = 0u;
		Uint32 m_referenced = 0u;
		Uint32 m_hddUsage = 0u;
		Uint32 m_GPUUsage = 0u;
	};


	GatherMLMaskDataCommandlet::GatherMLMaskDataCommandlet()
		: ICommandlet( RED_NAME_CONSTEXPR( "gatherMLMaskData" ) )
	{
	}

	GatherMLMaskDataCommandlet::~GatherMLMaskDataCommandlet() = default;

	Bool GatherMLMaskDataCommandlet::Execute( const red::CommandLine& options )
	{
		Settings settings;
		if( !ParseSettings( options, settings ) )
		{
			RED_LOG_ERROR( "Invalid settings" );
			return false;
		}

		commandlet::BackendDepotHolder depotHolder;
		InitialiseDatabase( depotHolder );

		red::DynArray< res::ResourcePath > resourcePaths{ red::PoolBackend() };
		red::HashSet< String > supportedExtensions{ red::PoolBackend() };
		tools::GatherSupportedExtensions( Multilayer_Mask::GetStaticClass(), supportedExtensions );
		tools::FindResourcesByExtensionRecursive( settings.m_directoryPath.ToString(), supportedExtensions, resourcePaths );

		red::DynArray< MLMaskRowData > mlMaskData{ red::PoolBackend() };
		mlMaskData.Reserve( resourcePaths.Size() );

		for( const auto& resource : resourcePaths )
		{
			ProcessResource( resource, mlMaskData );
		}
		
		Bool result = true;
		result &= SaveMLMasksAsCSV( settings.m_outputFile, mlMaskData );
		return result;
	}

	const char* GatherMLMaskDataCommandlet::GetOneLiner() const
	{
		return "Gather informations about ML Masks from a directory";
	}

	void GatherMLMaskDataCommandlet::PrintHelp() const
	{
		RED_LOG( "Usage:" );
		RED_LOG( " gatherMLMaskData -dir=<resource_path_to_scanned_directory> -out=<absolute_path_to_csv_file>" );
	}

	Bool GatherMLMaskDataCommandlet::InitializeEnvironment()
	{
		return m_engineInstance.Initialise();
	}

	void GatherMLMaskDataCommandlet::TeardownEnvironment()
	{
		m_engineInstance.Shutdown();
	}

	Bool GatherMLMaskDataCommandlet::ParseSettings( const red::CommandLine& options, Settings& settings )
	{
		String path;
		if( options.GetFirstOption( "dir", path ) )
		{
			if( !path.EndsWith( "\\" ) )
				path += "\\";
			settings.m_directoryPath = res::ResourcePath::Build( path );
		}

		if( options.GetFirstOption( "out", path ) )
		{
			settings.m_outputFile = red::AbsolutePath::CreateFilePath( red::StringView( path ) );
		}

		if( !settings.m_directoryPath.IsValid() )
			return false;

		if( !settings.m_outputFile.IsFilePath() )
			return false;

		return true;
	}

	void GatherMLMaskDataCommandlet::InitialiseDatabase( commandlet::BackendDepotHolder& depotHolder )
	{
		m_depot = depotHolder.GetDepot();

		job::Counter counter;
		red::AbsolutePath resdbCachePath = depotHolder.GetCachePath().AddFilePath( resdb::Database::GetDefaultFilename() );
		m_database.Initialise( *m_depot, resdbCachePath, counter );
		job::FlushCounter( counter );
	}

	void GatherMLMaskDataCommandlet::ProcessResource( const res::ResourcePath& resourcePath, red::DynArray< MLMaskRowData >& outMLMasksData )
	{
		res::IssueLoadingRequestParameter loadingRequest( resourcePath );
		loadingRequest.postLoadFlags.skipUploadRenderData = true;

		THandle< CResource > resource = GResourceLoader->IssueLoadingRequest( loadingRequest )->WaitUntilLoaded();
		if( !resource )
		{
			RED_LOG_ERROR( "Failed loading resource %hs", resourcePath.ToDebugString() );
			return;
		}

		if( THandle< Multilayer_Mask > mlMask = Cast< Multilayer_Mask >( resource ) )
		{
			MLMaskRowData& mlMaskData = outMLMasksData.EmplaceBack();
			mlMaskData.m_path = mlMask->GetPath();
			mlMaskData.m_layers = mlMask->GetLayerCount();

			mlMaskData.m_biasPC = GetMLMaskBias( mlMask, ECookingPlatform::PLATFORM_PC );
			mlMaskData.m_biasConsole = GetMLMaskBias( mlMask, ECookingPlatform::PLATFORM_PS4 );
			mlMaskData.m_referenced = GetNumberOfReferences( mlMask->GetPath() );
			mlMaskData.m_hddUsage = static_cast< Uint32 >( GetResourceFileSize( mlMask->GetPath() ) );
			mlMaskData.m_GPUUsage = mlMask->GetGPUSize();
		
			red::DynArray< THandle< CSourceTexture > > sourceTextures{ red::PoolBackend() };
			mlMask->GetSourceData( sourceTextures );
			for( const THandle< CSourceTexture >& texture : sourceTextures )
			{
				mlMaskData.m_width = texture->GetWidth();
				mlMaskData.m_height = texture->GetHeight();
				break; // All SourceTexture have the same size
			}
		}
		else
		{
			RED_FATAL_ASSERT( 0 != 0, "Unknow file type" );
		}
	}

	Uint32 GatherMLMaskDataCommandlet::GetNumberOfReferences( const res::ResourcePath& path ) const
	{
		return m_database.GetBackwardLinks( path ).Size();
	}

	Uint32 GatherMLMaskDataCommandlet::GetMLMaskBias( THandle< Multilayer_Mask >& mlMask, ECookingPlatform platform )
	{
		const Uint32 maxLayerIndex = mlMask->GetLayerCount();
		for( Uint32 i = 1u; i < maxLayerIndex; ++i )
		{
			if( mlMask->GetLayerBias( i, platform ) > 0u )
				return 1u;
		}
		return 0u;
	}

	Bool GatherMLMaskDataCommandlet::SaveMLMasksAsCSV( const red::AbsolutePath& outFilePath, const red::DynArray< MLMaskRowData >& objects ) const
	{
		// Header
		CBasicTextBuilder textBuilder;
		textBuilder.Write( "MLMask;Resolution;Layers;PC Biased;Console Biased;Referenced;HDD Usage [kB];GPU Usage [kB]\n" );

		// Data
		for( const MLMaskRowData& object : objects )
		{
			textBuilder.Writef( "%hs;%dx%d;%d;", object.m_path.ToString().AsChar(), object.m_width, object.m_height, object.m_layers );
			textBuilder.Writef( "%d;%d;%d;%.2f;%.2f\n", object.m_biasPC, object.m_biasConsole, object.m_referenced, object.m_hddUsage / 1024.0f, object.m_GPUUsage / 1024.0f );
		}
		return textBuilder.SaveToFile( outFilePath );
	}

	Uint64 GatherMLMaskDataCommandlet::GetResourceFileSize( const res::ResourcePath& path )
	{
		red::AbsolutePath resourceAbsolutePath = m_depot->TranslateResourcePath( path );
		return GFileManager->GetFileSize( resourceAbsolutePath );
	}

}


RTTI_BEGIN_TYPE_IN_NAMESPACE( GatherMLMaskDataCommandlet, tools )
RTTI_PARENT_TYPE( red::ICommandlet );
RTTI_END_TYPE()