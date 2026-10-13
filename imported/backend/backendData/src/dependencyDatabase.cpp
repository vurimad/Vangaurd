/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "dependencyDatabase.h"
#if !defined( VANGUARD_ASSET_HOST )
#include "dependencyLooseFileDatabase.h"
#endif
#include "dependencyBinaryDatabase.h"

#include "../../../common/redFileSystem/include/fileVersionList.h"

namespace dependency
{

red::UniquePtr< Database > Database::Create( const red::AbsolutePath& path, Uint32 settingsHash )
{
	RED_FATAL_ASSERT( !path.Empty(), "Cannot initialise with an empty path!" );

	red::UniquePtr< Database > result;
	if ( path.IsFilePath() )
	{
		result = red::CreateUniquePtr< BinaryDatabase >( path, settingsHash );
	}
#if !defined( VANGUARD_ASSET_HOST )
	else if ( path.IsDirectoryPath() )
	{
		result = red::CreateUniquePtr< LooseFileDatabase >( path, settingsHash );
	}
#endif
	return result;
}

red::AbsolutePath Database::ComputeDefaultDatabasePath( const red::AbsolutePath& basePath )
{
	// Be default we are using the binary dependencies database
	return basePath.AddFilePath( BinaryDatabase::DefaultDatabaseName() );
}

Database::~Database() = default;

} // dependency
