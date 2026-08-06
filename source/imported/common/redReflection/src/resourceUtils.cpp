#include "build.h"
#include "resourceUtils.h"
#include "resourceDepot.h"
#include "../../redContainers/include/string/stringBuilder.h"
#include "../../redFileSystem/include/fileSys.h"
#include "../../redFileSystem/include/filePaths.h"

namespace res
{
	red::AbsolutePath GetThumbnailPath( const ResourcePath& resourcePath, CName appearanceName )
	{
		const Uint64 resourceHash = resourcePath.GetHash();

		String filePath = String::Printf( "thumbs\\%.2x\\%.2x\\%.16llx_%hs.png",
			static_cast<Uint8>( resourceHash >> 56 ), static_cast<Uint8>( resourceHash >> 48 ), resourceHash, appearanceName.AsChar() );

		return GFileManager->GetCacheDirectory().AddFilePath( filePath );
	}

	red::AbsolutePath GetBackupPath( const ResourcePath& resourcePath )
	{
		const Uint64 resourceHash = resourcePath.GetHash();

		String extension = red::utils::ExtractExtension( resourcePath.ToStringView() );
		String filePath = String::Printf( "autosave\\%.2x\\%.2x\\%.16llx.%hs",
			static_cast<Uint8>( resourceHash >> 56 ), static_cast<Uint8>( resourceHash >> 48 ), resourceHash, extension.AsChar() );

		return GFileManager->GetCacheDirectory().AddFilePath( filePath );
	}

	void CreateJSONFilepath( const String& filename, red::AbsolutePath& folder )
	{
		if ( !GFileManager->FileExist( folder ) )
		{
			GFileManager->CreatePath( folder );
		}

		if ( !filename.Empty() )
		{
			String newName = filename;
			if ( !filename.EndsWith( ".json" ) )
				newName += ".json";

			folder.AppendFilePath( newName );
		}
	}

	red::AbsolutePath GetPreDefinedFilterFilePath( const String& filename )
	{
		red::AbsolutePath engineRoot = GFileManager->GetEngineRoot();
		red::AbsolutePath filterFolder = engineRoot.AppendDirPath( "editor\\debugFilters\\" );

		CreateJSONFilepath( filename, filterFolder );

		return filterFolder;
	}

	red::AbsolutePath GetDebugFilterDescriptionsFilePath( const String& filename )
	{
		red::AbsolutePath engineRoot = GFileManager->GetEngineRoot();
		red::AbsolutePath descriptionFilterFolder = engineRoot.AppendDirPath( "editor\\debugFilters\\descriptions\\" );

		CreateJSONFilepath( filename, descriptionFilterFolder );

		return descriptionFilterFolder;
	}

	red::AbsolutePath GetUserDefinedFilterFilePath( const String& filename )
	{
		red::AbsolutePath userFilterFolder = red::paths::GetUserEditorDirectory().AddDirPath( "debugFilters\\" );

		CreateJSONFilepath( filename, userFilterFolder );

		return userFilterFolder;
	}

	red::AbsolutePath GetUserDefinedFilterShortcutsFilePath( const String& filename )
	{
		red::AbsolutePath userFilterFolder = red::paths::GetUserEditorDirectory().AddDirPath( "shortcuts\\" );

		CreateJSONFilepath( filename, userFilterFolder );

		return userFilterFolder;
	}
}