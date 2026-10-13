/*
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "censorshipSwapList.h"
#include "2darray.h"
#include "../../redReflection/include/censorshipSystem.h"
#include "../../redReflection/include/resourceSwapList.h"

namespace res
{

res::ResourcePath GetCensorshipSwapListPath()
{
	return RED_CONST_RESOURCEPATH( "base\\censorship_swap_list.csv" );
}

namespace prv
{

static THandle< C2dArray > LoadCensorshipCsvSwapList()
{
	auto csvCensorshipList = GetCensorshipSwapListPath();
	auto token = GResourceLoader->IssueLoadingRequest( csvCensorshipList );
	job::FlushCounter( token->GetWaitCounter() );

	if ( !token->HasFailed() )
	{
		return Cast< C2dArray >( token->GetResource() );
	}

	return {};
}

static Uint32 CensorFlagsFromString( const String& str )
{
	Uint32 flags = 0;
	if ( str.ContainsNoCase( "nudity" ) )
	{
		flags |= Censor_Nudity;
	}
	if ( str.ContainsNoCase( "sexualized" ) )
	{
		flags |= Censor_OverSexualised;
	}
	if ( str.ContainsNoCase( "suggestive" ) )
	{
		flags |= Censor_Suggestive;
	}
	if ( str.ContainsNoCase( "homosexuality" ) )
	{
		flags |= Censor_Homosexuality;
	}
	if ( str.ContainsNoCase( "gore" ) )
	{
		flags |= Censor_Gore;
	}
	if ( str.ContainsNoCase( "drugs" ) )
	{
		flags |= Censor_Drugs;
	}
	if ( str.ContainsNoCase( "religion" ) )
	{
		flags |= Censor_Religion;
	}
	if ( str.ContainsNoCase( "streameronly" ) || str.ContainsNoCase( "chinese" ) )
	{
		flags |= Censor_WinnieThePooh;
	}
	return flags;
}

static red::UniquePtr< res::ResourceSwapList > LoadCensorSwapList( const Uint32 censorFlags )
{
	red::UniquePtr< res::ResourceSwapList > result;

	if ( censorFlags != 0 )
	{
		auto csvResource = LoadCensorshipCsvSwapList();
		if ( csvResource )
		{
			const Uint32 rowCount = csvResource->GetNumberOfRows();
			red::Map< res::ResourcePath, res::ResourcePath > filterMap{ rowCount, red::PoolEngine() };

			// Add entries from the censor swap list CSV into the map
			for ( Uint32 i = 0; i < csvResource->GetNumberOfRows(); ++i )
			{
				Uint32 rowFlags = prv::CensorFlagsFromString( csvResource->GetValueRef( 2, i ) );
				if ( (rowFlags & censorFlags) != 0 )
				{
					auto fromPath = res::ResourcePath::Build( csvResource->GetValueRef( 0, i ) );
					auto toPath = res::ResourcePath::Build( csvResource->GetValueRef( 1, i ) );
					filterMap.InsertUnsorted( fromPath, toPath );
				}
			}

			if ( !filterMap.Empty() )
			{
				filterMap.MakeClean();

				// We might have situations where a resource is censored for a given attribute, but then its replacement
				// is also censored for another attribute. So we need to either resolve this at runtime or do it here when loading.
				// Currently this applies only for the Chinese streamer censorship where certain extra assets are censored
				// but those assets are used as targets for existing resource swaps.
				// For now we will only replace the targets once during execution, so effectively we will do only follow a single link in this chain
				// NOTE: This algorithm isn't a perfect 1 pass swap
				for ( auto filterIter : filterMap )
				{
					auto& toPath = filterIter.Value();

					auto iter = filterMap.Find( toPath );
					if ( iter != filterMap.End() )
					{
						toPath = iter.Value();
					}
				}

				result = red::CreateUniquePtr< res::ResourceSwapList >();
				result->Internal_SetSwappedResources( filterMap );
#ifndef RED_CONFIGURATION_FINAL
				RED_LOG( "CensorshipSwapList: Swap List for flags %08X", censorFlags );
				for ( const auto iter : filterMap )
				{
					RED_LOG( "CensorshipSwapList: Swapping '%hs' to '%hs'", iter.Key().ToDebugString(), iter.Value().ToDebugString() );
				}
#endif
			}
			else
			{
				RED_LOG( "CensorshipSwapList: Swap List for flags %08X is empty!", censorFlags );
			}
		}
	}

	return result;
}

} // prv


void UpdateCensorshipSwapListToResourceLoader()
{
	static Uint32 s_swaplistCensorFlags = 0;
	const Uint32 censorFlags = red::GCensorshipSystem.GetCensorFlags();
	if ( censorFlags != s_swaplistCensorFlags )
	{
		auto censorSwapList = prv::LoadCensorSwapList( censorFlags );
		s_swaplistCensorFlags = censorFlags;
		GResourceLoader->ApplyCensorship( std::move( censorSwapList ) );
	}
}

} // res

