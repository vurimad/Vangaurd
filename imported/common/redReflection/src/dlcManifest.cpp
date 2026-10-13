/*
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "dlcManifest.h"
#include "../../redContainers/include/string/stringUtils.h"
#include "../../redCore/include/absolutePath.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( DlcManifest, res );
RTTI_PARENT_TYPE( CResource );
RTTI_PROPERTY( m_tweakBlob ).editable();
RTTI_PROPERTY( m_quest ).editable();
RTTI_PROPERTY( m_journal ).editable();
RTTI_PROPERTY( m_factories ).editable();
RTTI_PROPERTY( m_weaponAppearances ).editable();
RTTI_PROPERTY( m_vehicleAppearances ).editable();
RTTI_PROPERTY( m_communitySpawnsets ).editable();
RTTI_END_TYPE();

namespace res
{

DlcManifest::DlcManifest()
{
}

DlcManifest::~DlcManifest()
{
}

res::ResourcePath DlcManifest::GetPathForDLC( const red::String& basePath )
{
#ifdef RED_ENABLE_DLC
	// Manifests are called index.dlc_manifest in the main directory of the DLC
	auto lastPath = red::paths::GetFileName( basePath );
	auto manifestPath = red::StrCat( basePath, "\\index.", DlcManifest::GetFileExtension() );
	return res::ResourcePath::Build( manifestPath );
#else
	return res::ResourcePath();
#endif
}

} // res
