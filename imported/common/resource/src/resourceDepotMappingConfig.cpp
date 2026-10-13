/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */
#include "build.h"
#include "resourceDepotMappingConfig.h"


#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_LINUX )

#include "../../redFileSystem/include/fileSys.h"
#include "rapidXml.h"

namespace res
{
namespace prv
{

const char XML_MAPPINGS_NAME[] = "mappings";
const char XML_MOUNT_NAME[] = "mount";
const char XML_DISKPATH_NAME[] = "diskPath";
const char XML_RESPATH_NAME[] = "resPath";
const char XML_LEVEL_NAME[] = "level";

bool LoadMappingsFromFile( const red::AbsolutePath& configFile, const red::AbsolutePath& basePath, red::DynArray< DepotMapping >& mappings )
{
	String buffer;
	if ( !red::LoadFileToString( configFile, buffer ) )
	{
		RED_LOG_ERROR( "Failed to open the mapping configuration file" );
		return false;
	}

	using namespace rapidxml;

	xml_document<> xmlDoc;
	rapidxml::ResetErrors();
	xmlDoc.parse<0>( buffer.AsChar() );
	if ( rapidxml::HasError() )
	{
		RED_LOG_ERROR( "Failed to parse the mapping configuration file" );
		return false;
	}

	xml_node<>* mappingsNode = xmlDoc.first_node( XML_MAPPINGS_NAME );
	if ( mappingsNode == nullptr )
	{
		RED_LOG_ERROR( "Failed to find mapping node in mapping configuration file" );
		return false;
	}

	for ( xml_node<>* mountNode = mappingsNode->first_node( XML_MOUNT_NAME ); mountNode != nullptr; mountNode = mountNode->next_sibling( XML_MOUNT_NAME ) )
	{
		xml_node<>* diskPathNode = mountNode->first_node( XML_DISKPATH_NAME );
		xml_node<>* resPathNode = mountNode->first_node( XML_RESPATH_NAME );
		xml_node<>* levelNode = mountNode->first_node( XML_LEVEL_NAME );
		if ( diskPathNode == nullptr || resPathNode == nullptr )
		{
			RED_LOG_ERROR( "Invalid element found in mapping configuration file, requires both a %hs and %hs node specified", XML_DISKPATH_NAME, XML_RESPATH_NAME );
			continue;
		}

		const red::StringView diskPath( diskPathNode->value(), diskPathNode->value_size() );

		red::String resPath( resPathNode->value(), static_cast<Uint32>( resPathNode->value_size() ) );
		// Make sure path is appropriately conditioned
		resPath.ToLower();
		resPath.ReplaceAll( '/', '\\' );
		if ( !resPath.Empty() && resPath.Back() != '\\' )
		{
			resPath.Append( '\\' );
		}

		Uint32 level;
		if ( levelNode != nullptr )
		{
			level = atoi( levelNode->value() );
		}
		else
		{
			RED_LOG_ERROR( "No level found for element in mapping configuration file, setting to maximum" );
			level = std::numeric_limits<Uint32>::max();
		}

		mappings.PushBack( { basePath.AddDirPath( diskPath ), resPath, level } );
		RED_LOG_INFO( "Added mount path %hs -> %hs", mappings.Back().diskPath.AsChar(), mappings.Back().mountPath.AsChar() );
	}

	return true;
}

red::AbsolutePath GetMappingsFileName( const red::AbsolutePath& gameRoot )
{
	return gameRoot.AddFilePath( "mapping.xml" );
}

void InitializeResourceDepotMappings( const red::AbsolutePath& engineDepotRoot, const red::AbsolutePath& gameDepotRoot, const red::AbsolutePath& customDepotMapping, red::DynArray< DepotMapping >& mappings )
{
	// Add in the engine depot always
	mappings.PushBack( { engineDepotRoot, "engine\\", 0 } );

	const red::AbsolutePath configPath = customDepotMapping.Empty() ? GetMappingsFileName( gameDepotRoot ) : customDepotMapping;
	if ( !LoadMappingsFromFile( configPath, gameDepotRoot, mappings ) )
	{
		RED_LOG_ERROR( "Could not read mappings from config file %hs", configPath.AsChar() );
		// If no mappings file exists then just add the hard coded assumed path
		mappings.PushBack( { gameDepotRoot.AddDirPath( "base" ), "base\\", 1 } );
	}
}

} // namespace prv
} // namespace res

#else // ! RED_PLATFORM_WINPC

namespace res
{
namespace prv
{

void InitializeResourceDepotMappings( const red::AbsolutePath& engineDepotRoot, const red::AbsolutePath& gameDepotRoot, const red::AbsolutePath& customDepotMapping, red::DynArray< DepotMapping >& mappings )
{
	RED_LOG_ERROR( "Should not be using the editor depot or mappings on consoles!" );
	// This is a simple hack to make sure that the editor resource depot still compiles and 
	// kind of runs on consoles, even though it is not officially supported.
	// This may end up being useful in testing and other things so it is being kept alive.
	mappings.PushBack( { engineDepotRoot, "engine\\", 0 } );
	mappings.PushBack( { gameDepotRoot.AddDirPath( "base" ), "base\\", 1 } );
}

} // namespace prv
} // namespace res

#endif // RED_PLATFORM_WINPC