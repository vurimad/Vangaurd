#include "../../../imported/backend/backendData/src/build.h"

#include "../../../imported/backend/backendData/include/dataPlatform.h"
#include "../../../imported/backend/backendData/include/dependencyDatabase.h"
#include "../../../imported/backend/backendEngine/include/backendEngineApi.h"
#include "../../../imported/backend/backendEngine/include/resourceUtils.h"

#include "../../../imported/common/redCore/include/absolutePath.h"
#include "../../../imported/common/redReflection/include/resourcePath.h"

RTTI_BEGIN_ENUM_IN_NAMESPACE( TargetPlatform, data );
RTTI_ENUM_OPTION( NONE );
RTTI_ENUM_OPTION( PC );
RTTI_ENUM_OPTION( XBOX1 );
RTTI_ENUM_OPTION( PS4 );
RTTI_ENUM_OPTION( WINDOWS_SERVER );
RTTI_ENUM_OPTION( LINUX_SERVER );
RTTI_END_ENUM();

bool IsCsvResource( const res::ResourcePath& path )
{
    const red::StringView extension = red::paths::GetExtension( path.ToStringView() );
    return extension == "csv";
}

bool IsJsonResource( const res::ResourcePath& path )
{
    static const red::StringView jsonResourceExts[] = {
        "json",
        "widget", "widgetbrush", "hudentries",
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
