/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"

#include "resourceUtils.h"

#include "../../../common/redReflection/include/resource.h"
#include "../../../common/resourceMaterial/include/bitmapTexture.h"
#include "../../../common/resourceMaterial/include/cubeTexture.h"
#include "../../../common/resourceMaterial/include/multilayerMask.h"
#include "../../../common/resourceMaterial/include/textureArray.h"
#include "../../../common/resource/include/binkResource.h"

//
// NOTE / TODO
// Merge these into a single configuration object so for various backend engine actions
// like cooking, validating, packing into archives, resaving, <insert more here>
// we know how to process every extension that could be present in the depot
//

bool IsNotResource( const res::ResourcePath& path )
{
	static const red::Set< red::StringView > notResourceExts( {
		"bmp", "png", "jpg", "tif", "tiff", "tga", "dds",
		"fx", "fxmeta",
		"txt", "lua", "py",
		"re", "refab", "ma", "max", "mb", "fbx",
		"swf", "srt",
		"otf", "ttf",
		"xls", "xlsm", "xlsx", "xlam",
		"pdf", "rtf",
		"spp",
		"bk2", "mp4",
		"lnk", "bat",
		"tmp", "bak",
		"p4ignore",
		"scnlocjson", "res", "wem"
		}, red::PoolBackend() );

	const red::StringView extension = red::paths::GetExtension( path.ToStringView() );
	return notResourceExts.Exist( extension );
}

namespace
{

static red::DynArray< String > GetResourceExtensions()
{
	red::DynArray< const rtti::ClassType* > klasses;
	GetRttiSystem().EnumClasses( CResource::GetStaticClass(), klasses );

	red::DynArray< String > result;
	result.Reserve( klasses.Size() * 2 );

	for ( const rtti::ClassType* klass : klasses )
	{
		auto resource = klass->GetDefaultObject< CResource >();
		result.PushBack( resource->GetExtension() );
		if ( red::StrcmpNC( resource->GetExtension(), resource->GetDeprecatedExtension() ) != 0 )
		{
			result.PushBack( resource->GetDeprecatedExtension() );
		}
	}

	return result;
}

}

bool IsResource( const res::ResourcePath& path )
{
	static const red::DynArray< String > resourceExtensions = GetResourceExtensions();

	auto iter = std::find( resourceExtensions.Begin(), resourceExtensions.End(), red::paths::ExtractExtension( path.ToStringView() ) );
	return iter != resourceExtensions.End();
}

bool CouldBeResource( const res::ResourcePath& path )
{
	return !IsNotResource( path );
}

bool IsGeneratedResource( const res::ResourcePath& path )
{
	static const red::StringView generatedResourceExts[] = {
		"streamingsector", "streamingarea", "streamingworld"
	};

	static const red::StringView generatedResourcesDirectiories[] = {
		"\\_generated\\", "\\_external\\", "\\_compiled\\", "\\_global\\"
	};

	const red::StringView pathStringView = path.ToStringView();
	const red::StringView extension = red::paths::GetExtension( pathStringView );
	for ( const auto ext : generatedResourceExts )
	{
		if ( extension == ext )
		{
			return true;
		}
	}

	for ( const auto& directory : generatedResourcesDirectiories )
	{
		if ( pathStringView.Find( directory ) != red::StringView::npos )
		{
			return true;
		}
	}

	return false;
}

bool IsBinaryResource( const res::ResourcePath& path )
{
	if ( IsTextResource( path ) )
	{
		return false;
	}

	if ( IsJsonResource( path ) )
	{
		return false;
	}

	if ( IsNotResource( path ) )
	{
		return false;
	}

	return IsResource( path );
}

bool CanBinaryScanResource( const res::ResourcePath& path )
{
	if ( IsTextResource( path ) )
	{
		return false;
	}

	if ( IsJsonResource( path ) )
	{
		return false;
	}

	if ( IsBinkResource( path ) )
	{
		return false;
	}

	if ( IsICUResource( path ) )
	{
		return false;
	}

	if ( IsAudioResource( path ) )
	{
		return false;
	}

	return true;
}

bool IsTextResource( const res::ResourcePath& path )
{
	static const red::StringView textResourceExts[] = {
		"csv", "script", "xml", "tweak"
	};

	const red::StringView extension = red::paths::GetExtension( path.ToStringView() );
	for ( const auto ext : textResourceExts )
	{
		if ( extension == ext )
		{
			return true;
		}
	}

	return false;
}

bool IsCsvResource( const res::ResourcePath& path )
{
	const red::StringView extension = red::paths::GetExtension( path.ToStringView() );
	return extension == "csv";
}

// In JSON serialisation format and a CResource
bool IsJsonResource( const res::ResourcePath& path )
{
	static const red::StringView jsonResourceExts[] = {
		"json", // Note included both here and below because we have json files which are both CResource and not
		"widget", "widgetbrush", "hudentries",	// deprecated
		"inkgamesettings", "inkbrush", "inkatlas", "inkstyle", "inkhud", "inkmenu", "inklayers", "inkshapecollection", "inkfontfamily", "inktypography", "inkfullscreencomposition",
		"mlsetup", "mltemplate", "inkcharcustomization", "terrainsetup", "garmentlayerparams"
	};

	const red::StringView extension = red::paths::GetExtension( path.ToStringView() );
	for ( const auto ext : jsonResourceExts )
	{
		if ( extension == ext )
		{
			return true;
		}
	}

	return false;
}

bool IsBinkResource( const res::ResourcePath& path )
{
	const red::StringView extension = red::paths::GetExtension( path.ToStringView() );
	return extension == "bk2";
}

bool IsICUResource( const res::ResourcePath& path )
{
	static const res::ResourcePath icuDataFile = res::ResourcePath::Build( "engine\\i18n\\icudt65l.dat" );
	return icuDataFile == path;
}

bool IsAudioResource( const res::ResourcePath& path )
{
	const red::StringView extension = red::paths::GetExtension( path.ToStringView() );
	return extension == "opusinfo" || extension == "opuswem" || extension == "wem" || extension == "bnk" || extension == "opuspak";
}

// Need to know if a file is in the json serialisation format but is not a CResource
bool IsJsonNotResource( const res::ResourcePath& path )
{
	static const red::StringView jsonNotResourceExts[] = {
		"json", // Note included both here and above because we have json files which are both CResource and not
		"scnlocjson"
	};

	const red::StringView extension = red::paths::GetExtension( path.ToStringView() );
	for ( const auto ext : jsonNotResourceExts )
	{
		if ( extension == ext )
		{
			return true;
		}
	}

	return false;
}

bool IsLinkResource( const res::ResourcePath& path )
{
	return red::paths::GetExtension( path.ToStringView() ) == "link";
}

bool IsNotServerResource( const res::ResourcePath& path )
{
#define LIST_RESOURCE_EXTENSIONS( resourceType ) resourceType::GetFileExtension(), resourceType::GetDeprecatedFileExtension()
	static const char* notServerExtensions[] =
	{
		LIST_RESOURCE_EXTENSIONS( CBitmapTexture ),
		LIST_RESOURCE_EXTENSIONS( CCubeTexture ),
		LIST_RESOURCE_EXTENSIONS( Multilayer_Mask ),
		LIST_RESOURCE_EXTENSIONS( CTextureArray ),
		LIST_RESOURCE_EXTENSIONS( Bink )
	};
#undef LIST_RESOURCE_EXTENSIONS

	red::StringView extension = red::paths::GetExtension( path.ToStringView() );
	if( extension == "link" )
	{
		extension = red::paths::GetExtension( red::paths::GetFileStem( path.ToStringView() ) );
	}
    for ( const auto ext : notServerExtensions )
	{
		if ( extension == ext )
		{
			return true;
		}
	}

	return false;
}

res::ResourcePath ComputeLinkResourcePath( const res::ResourcePath& path )
{
	if ( IsLinkResource( path ) )
	{
		return path;
	}
	String pathStr = path.ToString();
	pathStr += ".link";
	return res::ResourcePath::Build( pathStr );
}

res::ResourcePath ComputeNonLinkResourcePath( const res::ResourcePath& path )
{
	if ( !IsLinkResource( path ) )
	{
		return path;
	}
	return res::ResourcePath::Build( red::paths::RemoveExtension( path.ToStringView() ) );
}
