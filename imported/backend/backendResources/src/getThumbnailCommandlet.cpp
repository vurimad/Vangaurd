/**
 * Copyright c 2017 CD Projekt Red. All Rights Reserved.
 */
#include "build.h"

#include "../../backendEngine/include/thumbnailGenerator.h"
#include "../../backendEngine/include/resourceMetadata.h"
#include "../../backendEngine/include/commandletUtils.h"

#include "../../../common/core/include/commandlet.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../common/redReflection/include/resourceDepot.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/redReflection/include/resourceUtils.h"


/// Commandlet that updates the cached resource thumbnail on disk from the resource file
class GetThumbnailCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE( GetThumbnailCommandlet );

public:
	GetThumbnailCommandlet();
	~GetThumbnailCommandlet();

	virtual bool Execute( const red::CommandLine& options ) override;

	virtual const char* GetOneLiner() const override;
	virtual void PrintHelp() const override;

private:
	struct Settings
	{
		red::DynArray< res::ResourcePath > resourcesToProcess;

		bool ParseOptions(const red::CommandLine& options);
	};

	bool ExtractThumbnail(const res::ResourcePath& resourcePath, const Uint32 thumbnailSize);

	Settings m_settings;
};


RTTI_BEGIN_TYPE( GetThumbnailCommandlet )
	RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE()


GetThumbnailCommandlet::GetThumbnailCommandlet()
	: ICommandlet( RED_NAME_CONSTEXPR( "get-thumb" ) )
{
}

GetThumbnailCommandlet::~GetThumbnailCommandlet()
{
}

const char* GetThumbnailCommandlet::GetOneLiner() const
{
	return "Extracts a thumbnail from a resource";
}

void GetThumbnailCommandlet::PrintHelp() const
{
	RED_LOG( "Usage:"  );
	RED_LOG( "    get-thumb <options>" );
	RED_LOG( "Options:" );
	RED_LOG( "  -path=<path>    - Extract the thumbnail from a single resource" );
	RED_LOG( "  -dir=<path>     - Extract the thumbnails recursively from the given directory tree" );
}

bool GetThumbnailCommandlet::Settings::ParseOptions( const red::CommandLine& options )
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

	return true;
}	

bool GetThumbnailCommandlet::ExtractThumbnail(const res::ResourcePath& resourcePath, const Uint32 thumbnailSize)
{
	if ( res::IsThumbnailInCacheUpToDate( resourcePath, CName::NONE() ) )
	{
		// thumbnail dump file exists for current resource version
		RED_LOG("Thumbnail is up-to-date: %hs", resourcePath.ToString().AsChar());
		return true;
	}

	// check if thumbnail is stored in resource metadata
	THandle<res::ResourceMetadata> meta = res::ResourceMetadata::Get(resourcePath);
	if (!meta || !meta->HasThumbnail())
	{
		RED_LOG_ERROR("No thumbnail found inside: %hs", resourcePath.ToString().AsChar());
		return false;
	}

	img::ImagePtr image = res::prv::BuildThumbnailImage(meta->GetImageData(), thumbnailSize, thumbnailSize);
	if (!image || image->GetPixelBufferSize() == 0)
	{
		RED_LOG_ERROR("Failed to get thumbnail data: %hs", resourcePath.ToString().AsChar());
		return false;
	}

	red::AbsolutePath thumbnailPath = res::GetThumbnailPath(resourcePath, CName::NONE());
	const auto& dirPath = ::red::utils::ParentAbsolutePath(thumbnailPath);
	if (!GFileManager->CreatePath(dirPath))
	{
		RED_LOG_ERROR("Failed to create the thumbnail destination directory: %hs", dirPath.AsChar());
		return false;
	}

	return res::prv::SaveThumbnailToCache( image, thumbnailPath );
}


bool GetThumbnailCommandlet::Execute( const red::CommandLine& options )
{
	if ( !m_settings.ParseOptions( options ) )
	{
		return false;
	}

	// output image
	if ( !res::prv::EnsureThumbnailCacheDirExists() )
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
		if ( ExtractThumbnail( resourcePath, thumbnailSize ) )
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


