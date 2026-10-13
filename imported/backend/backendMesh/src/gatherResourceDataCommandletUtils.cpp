/**
* Copyright (C)2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "gatherResourceDataCommandletUtils.h"
#include "../../../common/resource/include/resourceEditorDepotImpl.h"
#include "../../../common/redReflection/include/resource.h"


namespace tools
{
	void FindSubdirectories( const String& directoryPath, red::DynArray< String >& subDirectories )
	{
		red::DynArray< String > subs;
		res::GetEditorDepot()->FindDepotSubDirectories( directoryPath, subs );
		for( const auto& sub : subs )
		{
			const String foundDirectory = directoryPath + sub + "\\";
			subDirectories.PushBack( foundDirectory );
			FindSubdirectories( foundDirectory, subDirectories );
		}
	}

	void FindResourcesByExtensionRecursive( const String& directoryPath, const red::HashSet< String >& supportedExtensions, red::DynArray< res::ResourcePath >& outResources )
	{
		red::DynArray < String > subDirectories{ red::PoolBackend() };
		subDirectories.PushBack( directoryPath );
		FindSubdirectories( directoryPath, subDirectories );

		for( const auto& subDirectory : subDirectories )
		{
			red::DynArray< String > filePaths{ red::PoolBackend() };
			res::GetEditorDepot()->FindDepotFiles( subDirectory, "*", filePaths );
			for( const auto& filePath : filePaths )
			{
				if( supportedExtensions.Exist( red::paths::ExtractExtension( filePath ) ) )
				{
					outResources.PushBack( res::ResourcePath::Build( subDirectory + filePath ) );
				}
			}
		}
	}

	void GatherSupportedExtensions( const rtti::ClassType* resourceClass, red::HashSet< String >& outExtensions )
	{
		if( !resourceClass->IsAbstract() )
		{
			auto resource = resourceClass->CreateHandle<CResource>();
			outExtensions.Insert( resource->GetExtension() );
			outExtensions.Insert( resource->GetDeprecatedExtension() );
		}

		red::DynArray< const rtti::ClassType* > childClasses;
		rtti::ITypeSystem::GetInstance().EnumClasses( resourceClass, childClasses );
		for( const rtti::ClassType* child : childClasses )
		{
			if( child != resourceClass )
			{
				GatherSupportedExtensions( child, outExtensions );
			}
		}
	}
}
