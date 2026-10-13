/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */
#include "build.h"

#include "../../backendEngine/include/thumbnailGenerator.h"
#include "../../backendEngine/include/resourceMetadata.h"
#include "../../backendEngine/include/environmentPresetsManager.h"
#include "../../backendEngine/include/commandletUtils.h"
#include "../../backendEngine/include/commandletRenderInstance.h"
#include "../../backendEngine/include/commandletEngineInstance.h"

#include "../../../common/core/include/commandlet.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../common/redReflection/include/resource.h"
#include "../../../common/redReflection/include/serializationSaver.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/redReflection/include/resourceUtils.h"
#include "../../../common/resource/include/resourceSaver.h"
#include "../../../common/resource/include/backendResourceDepot.h"
#include "../../../common/redImage/include/image.h"
#include "../../../common/sound/include/soundSystem.h"
#include "../../backendEngine/include/streamingManagerInstance.h"

class UpdateThumbnailCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE( UpdateThumbnailCommandlet );

public:
	UpdateThumbnailCommandlet();
	~UpdateThumbnailCommandlet();

	// ICommandlet interface
	virtual bool Execute( const red::CommandLine& options ) override;
	virtual const char* GetOneLiner() const override;
	virtual void PrintHelp() const override;

	virtual Bool InitializeEnvironment() override;
	virtual void TeardownEnvironment() override;

private:
	bool UpdateThumbnail( const res::ResourcePath& resourcePath, const Uint32 thumbnailSize );

	struct Settings
	{
		red::DynArray< res::ResourcePath > resourcesToProcess;
		bool updateCacheImages;
		bool forceGeneration;

		Settings();
		bool ParseOptions( const red::CommandLine& options );
	};

	Settings m_settings;

	red::UniquePtr< tools::EnvironmentPresetsManager > m_envPresetsManager;
	commandlet::EngineInstance m_engine;
	commandlet::RenderInstance m_render;
	res::StreamingManagerInstance m_streamingManager;
};

RTTI_BEGIN_TYPE( UpdateThumbnailCommandlet )
	RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE()

UpdateThumbnailCommandlet::UpdateThumbnailCommandlet()
	: ICommandlet( RED_NAME_CONSTEXPR( "update-thumb" ) )
{
}

UpdateThumbnailCommandlet::~UpdateThumbnailCommandlet()
{
}

const char* UpdateThumbnailCommandlet::GetOneLiner() const
{
	return "Updates the thumbnail contained within in the resource metadata.";
}

void UpdateThumbnailCommandlet::PrintHelp() const
{
	RED_LOG( "Usage:" );
	RED_LOG( " update-thumb <options>" );
	RED_LOG( "Options:" );
	RED_LOG( "  -path=<path>  - Update the thumbnail for a single resource" );
	RED_LOG( "  -dir=<path>   - Update the thumbnails recursively for an entire directory tree" );
	RED_LOG( "  -skipcache    - Do not update the thumbnail cache" );
	RED_LOG( "  -force        - Forcefully generate and update thumbnails" );
}

Bool UpdateThumbnailCommandlet::InitializeEnvironment()
{
	if ( !m_engine.InitialiseWithSoundSystem() )
		return false;

	if ( !m_render.Initialise() )
		return false;

	m_envPresetsManager = red::CreateUniquePtr< tools::EnvironmentPresetsManager >();
	m_streamingManager.Initialize();
	return true;
}

void UpdateThumbnailCommandlet::TeardownEnvironment()
{
	m_envPresetsManager.Reset();
	m_streamingManager.Shutdown();

	m_render.Shutdown();

	m_engine.Shutdown();
}

UpdateThumbnailCommandlet::Settings::Settings()
	: updateCacheImages( false )
{
}

bool UpdateThumbnailCommandlet::Settings::ParseOptions( const red::CommandLine& options )
{
	String basePath;
	if ( options.GetFirstOption( "path", basePath ) )
	{
		resourcesToProcess.PushBack( res::ResourcePath::Build( basePath ) );
	}
	else if ( options.GetFirstOption( "dir", basePath ) )
	{
		resourcesToProcess = commandlet::ScanResourceDepotDirectories( *commandlet::GetEditorDepot(), basePath );
	}
	else
	{
		RED_LOG_ERROR( "Missing input parameter, requires either '-path' or '-dir'" );
		return false;
	}

	updateCacheImages = ! options.HasOption( "skipcache" );
	forceGeneration = options.HasOption( "force" );

	return true;
}

bool UpdateThumbnailCommandlet::UpdateThumbnail( const res::ResourcePath& resourcePath, const Uint32 thumbnailSize )
{
	if ( !res::CanGenerateThumbnail( resourcePath ) )
	{
		RED_LOG_ERROR( "Cannot generate thumbnail for: %hs", resourcePath.ToDebugString() );
		return false;
	}

	if ( !m_settings.forceGeneration )
	{
		if ( res::IsThumbnailInCacheUpToDate( resourcePath, CName::NONE() ) )
		{
			RED_LOG( "Thumbnail is up-to-date: %hs", resourcePath.ToDebugString() );
			return true;
		}
	}

	TResRef< CResource > resRef{ resourcePath };
	THandle< CResource > resource = resRef.Get();
	if ( !resource )
	{
		RED_LOG_ERROR( "Failed to load the resource to update %hs", resourcePath.ToDebugString() );
		return false;
	}

	img::ImagePtr image = res::prv::GenerateThumbnail( resourcePath, resource, *m_envPresetsManager );
	if ( !image || image->GetPixelBufferSize() == 0 )
	{
		RED_LOG_ERROR( "Failed to generate the thumbnail image for %hs", resourcePath.ToDebugString() );
		return false;
	}

	res::ResourceMetadata::SetThumbnail( resource, image );

	bool result = true;
	res::ResourceSavingOptions options;
	options.m_targetPath = resourcePath;
	if ( !res::SaveResource( resource, options ) )
	{
		RED_LOG_ERROR( "Failed to save the thumbnail in the resource %hs. Make sure it is not read only!", resourcePath.ToDebugString() );
		result = false;
	}

	if ( m_settings.updateCacheImages )
	{
		red::AbsolutePath thumbnailPath = res::GetThumbnailPath( resourcePath, CName::NONE() );
		const auto& dirPath = ::red::utils::ParentAbsolutePath( thumbnailPath );
		if (!GFileManager->CreatePath(dirPath))
		{
			RED_LOG_ERROR( "Failed to create the thumbnail destination directory: %hs", dirPath.ToDebugString() );
			return false;
		}

		if ( !res::prv::SaveThumbnailToCache( image, thumbnailPath ) )
		{
			RED_LOG_ERROR( "Failed to save cached thumbnail iamge for %hs to %hs", resourcePath.ToDebugString(), thumbnailPath.ToDebugString() );
		}
	}

	return result;
}

bool UpdateThumbnailCommandlet::Execute( const red::CommandLine& options )
{
	if ( !m_settings.ParseOptions( options ) )
	{
		return false;
	}

	// output image
	if ( m_settings.updateCacheImages && !res::prv::EnsureThumbnailCacheDirExists() )
	{
		RED_LOG_ERROR("Failed to create the hidden cache thumbnail directory");
		return false;
	}

	const Uint32 thumbnailSize = res::ThumbnailSize();
	const Uint32 totalResources = m_settings.resourcesToProcess.Size();
	Uint32 processedResourceCount = 0;
	Uint32 thumbnailsGenerated = 0;

	for ( const auto& resourcePath : m_settings.resourcesToProcess )
	{
		processedResourceCount += 1;
		RED_LOG( "Status: Processing: %d / %d ", processedResourceCount, totalResources );
		if ( UpdateThumbnail( resourcePath, thumbnailSize ) )
		{
			thumbnailsGenerated++;
		}
	}

	if ( thumbnailsGenerated != totalResources )
	{
		RED_LOG_ERROR( "Failed to generate all of the resources. Completed %d / %d successfully.", thumbnailsGenerated, totalResources );
		return false;
	}

	RED_LOG("Thumbnail generation done completed (%d done)", thumbnailsGenerated);
	return true;
}

