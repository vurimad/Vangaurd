/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "fileSyncResourceEditorDepot.h"

#if defined( RED_FILE_SYNC_SERVICE_ENABLED )

#include "../../redFileSystem/include/fileSys.h"
#include "../../redReflection/include/serializationAsyncSource.h"
#include "../../redFileSystem/include/fileSyncService.h"
#include "../../redFileSystem/include/fileSyncFileManager.h"
#include "../../redIO/include/redIOAsyncIO.h"
#include "serializationAsyncSourceImpl.h"

namespace res
{
namespace prv
{

FileSyncResourceEditorDepot::FileSyncResourceEditorDepot( const DepotInitInfo& initInfo )
	: ResourceEditorDepot( initInfo )
{
	RED_ASSERT( FileSyncService::IsClient() );
	SetDebugName( "FileSync" );
}

red::SharedPtr< serialization::IAsyncSource > FileSyncResourceEditorDepot::CreateResourceAsyncSource( const res::ResourcePath resourcePath, const res::CreateResourceAsyncSourceParams& params )
{
	const red::AbsolutePath absolutePath = TranslateExistingResourcePath_FollowLinks( resourcePath.ToStringView() );

	// Translate the path

	AnsiChar translatedAbsolutePath[ io::REDIO_MAX_PATH_LENGTH ];
	red::Strcpy( translatedAbsolutePath, absolutePath.AsChar(), io::REDIO_MAX_PATH_LENGTH );
	if ( !FileSyncFileManager::TranslateFilePath( translatedAbsolutePath, io::REDIO_MAX_PATH_LENGTH ) )
	{
		return nullptr;
	}

	// Access file under translated path

	serialization::AsyncSourcePtr result;
	io::TFileHandle fh = io::GAsyncIO.OpenFile( translatedAbsolutePath, io::eAsyncFlag_TryCloseFileWhenNotUsed );
	if ( fh != io::INVALID_FILE_HANDLE )
	{
		result = serialization::CreateFileAsyncSource( resourcePath, fh );
		io::GAsyncIO.ReleaseFile( fh ); // close additional reference, now the AsyncSource has the reference to the file
	}

	return result;
}

} // prv
} // res

#else

RED_NO_EMPTY_FILE()

#endif // RED_FILE_SYNC_SERVICE_ENABLED
