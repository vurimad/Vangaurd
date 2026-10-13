/*
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"

#include "../../../common/core/include/commandlet.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../common/redContainers/include/string/stringUtils.h"

#include "../../../common/redCore/include/absolutePath.h"
#include "../../../common/redReflection/include/resourcePath.h"
#include "../../../common/resource/include/resourceEditorDepotImpl.h"

#include "../../../common/resourceMaterial/include/bitmapTexture.h"
#include "../../../common/resourceMaterial/include/sourceTextureData.h"

#include "../../../common/redJobs2/include/jobRunner.h"

#include "../../backendEngine/include/resourceMetadata.h"
#include "../../backendEngine/include/resourceImportMetadata.h"
#include "../../backendEngine/include/commandletUtils.h"

//------------------------------------------------------------------------------

class ImageMetadataScanCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE( ImageMetadataScanCommandlet );

public:
	ImageMetadataScanCommandlet();
	~ImageMetadataScanCommandlet();

	virtual const char* GetOneLiner() const override;
	virtual void PrintHelp() const override;
	virtual bool Execute( const red::CommandLine& options ) override;

private:
	red::DynArray< res::ResourcePath > ScanResourcePathsForImages( const red::CommandLine& options );

	struct ImageInfo
	{
		res::ResourcePath path;

		Uint32 width;
		Uint32 height;
		Uint32 depth;
		STextureGroupSetup textureSetup;

		Uint32 sourceWidth;
		Uint32 sourceHeight;
		Uint32 sourceDepth;
		ETextureRawFormat sourceRawFormat;

		THandle< res::ResourceImportMetadata > importMetadata;

		bool hasSourceTexture;
		bool successful;
	};
};

RTTI_BEGIN_TYPE( ImageMetadataScanCommandlet )
	RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE()

ImageMetadataScanCommandlet::ImageMetadataScanCommandlet()
	: red::ICommandlet( RED_NAME_CONSTEXPR( "scan-image-metadata" ) )
{
}

ImageMetadataScanCommandlet::~ImageMetadataScanCommandlet()
{
}

const char* ImageMetadataScanCommandlet::GetOneLiner() const
{
	return "Scans image files in the engine and dumps their metadata";
}

void ImageMetadataScanCommandlet::PrintHelp() const
{
}

//------------------------------------------------------------------------------

red::DynArray< res::ResourcePath > ImageMetadataScanCommandlet::ScanResourcePathsForImages( const red::CommandLine& options )
{
	const auto* depot = commandlet::GetEditorDepot();
	RED_FATAL_ASSERT( depot != nullptr );

	constexpr const char imagePattern[] = "*.xbm";

	red::DynArray< res::ResourcePath > result;

	if ( options.HasOption( "dir" ) )
	{
		auto directories = options.GetValues( "dir" );
		for ( const auto& dir : directories )
		{
			result.PushBack( commandlet::ScanResourceDepotDirectories( *depot, dir, imagePattern ) );
		}
	}
	else if ( options.HasOption( "file" ) )
	{
		auto files = options.GetValues( "file" );
		for ( const auto& file : files )
		{
			if ( red::GlobMatch( file, imagePattern ) )
			{
				auto resPath = res::ResourcePath::Build( file );
				if ( depot->ResourceExists( resPath ) )
				{
					result.PushBack( resPath );
				}
			}
			else
			{
				RED_LOG_ERROR( "File %hs is not an image!", file.AsChar() );
			}
		}
	}
	else
	{
		result = commandlet::ScanResourceDepotDirectories(*depot, "", imagePattern );
	}

	red::alg::RemoveDuplicates( result );

	return result;
}

namespace
{

const char* GetCompressionString( ETextureCompression value )
{
	static const rtti::EnumType* texEnum = GetRttiSystem().FindEnum( RED_NAME_CONSTEXPR_NOREG( "ETextureCompression" ) );

	CName outName;
	if ( texEnum && texEnum->FindName( value, outName ) )
	{
		return outName.AsChar();
	}

	return "";
}

const char* GetRawFormatString( ETextureRawFormat value )
{
	const rtti::EnumType* texEnum = GetRttiSystem().FindEnum( RED_NAME_CONSTEXPR_NOREG( "ETextureRawFormat" ) );

	CName outName;
	if ( texEnum && texEnum->FindName( value, outName ) )
	{
		return outName.AsChar();
	}

	return "";
}

}

bool ImageMetadataScanCommandlet::Execute( const red::CommandLine& options )
{
	red::DynArray< res::ResourcePath > imagePaths = ScanResourcePathsForImages( options );
	red::DynArray< ImageInfo > imageInfos{ imagePaths.Size() };
	
	for ( Uint32 i : imagePaths.Indices() )
	{
		const res::ResourcePath& imagePath = imagePaths[i];
		ImageInfo& imageInfo = imageInfos[i];

		imageInfo.path = imagePath;
		imageInfo.hasSourceTexture = false;
		imageInfo.successful = false;

		res::IssueLoadingRequestParameter loadParam;
		loadParam.path = imagePath;
		loadParam.priority = io::eAsyncPriority_Normal;
		loadParam.skipBuffers = true;
		loadParam.skipPostLoad = true;
		loadParam.postLoadFlags.skipUploadRenderData = true;
		auto token = GResourceLoader->IssueLoadingRequest( loadParam );

		THandle< CBitmapTexture > texture = Cast< CBitmapTexture >( token->WaitUntilLoaded() );
		if ( token->IsLoaded() )
		{
			auto sizeDesc = texture->GetSizeDesc();

			imageInfo.width		= sizeDesc.width;
			imageInfo.height	= sizeDesc.height;
			imageInfo.depth		= sizeDesc.depth;
			imageInfo.textureSetup = texture->GetTextureSetup();

			THandle< CSourceTexture > sourceTexture = texture->GetSourceData();
			if ( sourceTexture )
			{
				imageInfo.sourceWidth = sourceTexture->GetWidth();
				imageInfo.sourceHeight = sourceTexture->GetHeight();
				imageInfo.sourceDepth = sourceTexture->GetDepth();
				imageInfo.sourceRawFormat = sourceTexture->GetFormat();
				imageInfo.hasSourceTexture = true;
			}

			THandle< res::ResourceMetadata > metadata = res::ResourceMetadata::Get( texture );
			if ( metadata )
			{
				imageInfo.importMetadata = metadata->GetImportData();
			}

			imageInfo.successful = true;
		}
	}

	// Setup output
	String param;
	FILE* output = stdout;
	if ( options.GetFirstOption( "output", param ) )
	{
		output = fopen( param.AsChar(), "w" );
		if ( output == nullptr )
		{
			output = stdout;
		}
	}

	fprintf( output, 
		"Resource Path\t"
		"Width\tHeight\tDepth\t"
		"Group\tCompression\tRawFormat\t"
		"Streamable\tHasMipMap\tIsGamma\tStretchHisto\t"
		"SrcWidth\tSrcHeight\tSrcDepth\tSrcRawFormat\t"
		//"InterHost\tInterPath\tInterAuthor\t"
		"AssetHost\tAssetPath\t"
		//"VCPath\tVCRevision\tVCHash\t"
		"\n" );
	for ( const auto& info : imageInfos )
	{
		fprintf( output, "%hs", info.path.ToDebugString() );
		if ( info.successful )
		{
			fprintf( output, "\t%u\t%u\t%u",
				info.width,
				info.height,
				info.depth
			);
			fprintf( output, "\t%hs\t%hs\t%hs", 
				GpuWrapApi::GetTextureGroupName( info.textureSetup.m_group ),
				GetCompressionString( info.textureSetup.m_compression ),
				GetRawFormatString( info.textureSetup.m_rawFormat )
			);
			fprintf( output, "\t%s\t%s\t%s\t%s",
				info.textureSetup.m_isStreamable ? "t" : "f",
				info.textureSetup.m_hasMipchain ? "t" : "f",
				info.textureSetup.m_isGamma ? "t" : "f",
				info.textureSetup.m_stretchHistogram ? "t" : "f"
			);

			if ( info.hasSourceTexture )
			{
				fprintf( output, "\t%u\t%u\t%u\t%hs",
					info.sourceWidth,
					info.sourceHeight,
					info.sourceDepth,
					GetRawFormatString( info.sourceRawFormat )
				);
			}
			else
			{
				fprintf( output, "\t\t\t\t" );
			}

			// Some sections are commented out here because they should be being printed out
			// but no data actually specifies these fields therefore they are always blank
			// TODO: Fix importers so they record version control path, revision, and hash - then uncomment some of these
			if ( info.importMetadata )
			{
				const auto& importData = *info.importMetadata;
				//fprintf( output, "\t%s\t%s\t%s",
				//	importData.GetIntermediateHost().AsChar(),
				//	importData.GetIntermediatePath().AsChar(),
				//	importData.GetIntermediateAuthor().AsChar()
				//);
				fprintf( output, "\t%s\t%s",
					importData.GetAssetHost().AsChar(),
					importData.GetAssetPath().AsChar()
				);
				//using LocalMD5 = Uint64[2];
				//const auto& md5 = reinterpret_cast< const LocalMD5& >( importData.GetAssetChecksum() );
				//fprintf( output, "\t%s\t%d\t%016llX%016llX",
				//	importData.GetAssetVersionControlPath().AsChar(),
				//	importData.GetAssetVersionControlRevision(),
				//	md5[0], md5[1]
				//);
			}
			else
			{
				fprintf( output, 
					//"\t\t\t"
					"\t\t"
					//"\t\t\t"
				);
			}

			fprintf( output, "\n" );
		}
		else
		{
			fprintf( output, "ERROR\n" );
		}
	}

	fflush( output );

	if ( output != stdout )
	{
		fclose( output );
	}

	return true;
}
