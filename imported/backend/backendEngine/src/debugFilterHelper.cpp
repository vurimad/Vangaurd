#include "build.h"
#include "debugFilterHelper.h"

#include "validator.h"

#include "../../../common/redConfig/include/configVarSystem.h"
#include "../../../common/redReflection/include/resource.h"
#include "../../../common/renderData/include/debugFilterMask.h"
#include "../../../common/renderData/include/renderSettingsVars.h"

void DebugFilterHelper::SetupInterop( DebugFilterHelper* ptr, ::interop::utils::Object& op )
{
	op.RegisterFunctor<Uint32>( "GetFilterMaskCount", [ptr]( interop::IContext& ctx )
	{
		return rend::DebugFilterMask::GetFilterMaskMapSize();
	});

	op.RegisterFunctor<String, Uint32>( "GetFilterNameForIndex", [ptr]( interop::IContext& ctx, Uint32 index )
	{
			red::DynArray< String > names;
			rend::DebugFilterMask::GetFilterMaskMapKeys( names );
			return index < names.Size() ? names[index] : String::EMPTY();
	} );

	op.RegisterFunctor<String, Uint32>( "GetFilterValueForIndex", [ptr]( interop::IContext& ctx, Uint32 index )
	{
		red::DynArray< rend::DebugFilterMask > masks;
		rend::DebugFilterMask::GetFilterMaskMapValues( masks );
		// #tbd: CYB-23937 - filters are super hacky and have problems when shutting down the editor before the data is available
		RED_ERROR( index < masks.Size(), "GetFilterValueForIndex index out of range: index=%u, masks_size=%u", index, masks.Size() );
		return index < masks.Size() ? rend::DebugFilterMask::ConvertToString( masks[index] ) : 0;
	} );

	op.RegisterFunctor<String, String>( "GetFilterHierarchyAsString", []( interop::IContext& ctx, String maskName )
	{
		return rend::DebugFilterMask::FilterHierarchyAsString( maskName );
	} );

	op.RegisterFunctor<void, String, String>( "SetFilterHierarchyFromString", [ptr]( interop::IContext& ctx, String maskName, String processedHierarchy )
	{
		return ptr->SetFilterHierarchyFromString( maskName, processedHierarchy );
	} );

	op.RegisterFunctor<void, String, String>( "SetFilter", [ptr]( interop::IContext& ctx, String name, String value )
	{
		rend::DebugFilterMask::GetFilterMask( name ) = rend::DebugFilterMask::BuildFromString( value );
	} );

	op.RegisterFunctor<void>("SaveGameFilterMask", []( interop::IContext& ctx )
	{
		String gameMaskAsString = rend::DebugFilterMask::FilterHierarchyAsString( rend::GetGameFilterName() );
		Config::GetConfigSystem().SetValue( "DebugFilters", rend::GetGameFilterName(), gameMaskAsString );
		Config::GetConfigSystem().Save();
	} );

	op.RegisterFunctor<void>( "LoadGameFilterMask", [ptr]( interop::IContext& ctx )
	{
		String gameMaskAsString;
		const char* gameFilterName = rend::GetGameFilterName();
		if (Config::GetConfigSystem().GetValue( "DebugFilters", gameFilterName, gameMaskAsString ))
		{
			ptr->SetFilterHierarchyFromString( gameFilterName, gameMaskAsString );
		}
	} );

	op.RegisterFunctor<void>( "ReloadGameFilterMask", [ptr]( interop::IContext& ctx )
	{
		Config::GetConfigSystem().Reload();

		String gameMaskAsString;
		const char* gameFilterName = rend::GetGameFilterName();
		if ( Config::GetConfigSystem().GetValue( "DebugFilters", gameFilterName, gameMaskAsString ) )
		{
			rend::DebugFilterMask::GetFilterMask( gameFilterName ) = rend::GetDefaultGameFilters();
			if ( !gameMaskAsString.Empty() )
			{
				ptr->SetFilterHierarchyFromString( gameFilterName, gameMaskAsString );
			}
		}
	});

	op.RegisterNotifier( "_onDebugFilterChanged", ptr->m_debugFilterChangedNotifier );
}

DebugFilterHelper::DebugFilterHelper()
{
}

DebugFilterHelper::~DebugFilterHelper()
{
}

void DebugFilterHelper::SetFilterHierarchyFromString( const String &maskName, const String &processedHierarchy )
{
	rend::DebugFilterMask::SetFilterMaskFromHierarchyString( maskName, processedHierarchy );
}

void DebugFilterHelper::DebugFilterChangedNotify()
{
	m_debugFilterChangedNotifier.Notify();
}

//////////////////////////////////////////////////////////////////////////

QualityLevelHelper::QualityLevelHelper()
{

}

QualityLevelHelper::~QualityLevelHelper()
{

}

void QualityLevelHelper::SetQualityLevel( int option )
{
	Config::SelectQualityLevel( option );
}

void QualityLevelHelper::SetTextureQualityLevel( int option )
{
	Config::SelectTextureQualityLevel( option );
}

void QualityLevelHelper::SetupInterop( QualityLevelHelper* ptr, ::interop::utils::Object& op )
{
	op.RegisterFunctor<void, int>( "SetQualityLevel", [ptr]( interop::IContext& ctx, int option )
	{
		ptr->SetQualityLevel( option );
	} );
	op.RegisterFunctor<void, int>( "SetTextureQualityLevel", [ptr]( interop::IContext& ctx, int option )
	{
		ptr->SetTextureQualityLevel( option );
	} );
}
