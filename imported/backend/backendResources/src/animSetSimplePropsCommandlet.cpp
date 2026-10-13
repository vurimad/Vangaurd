/**
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "resaveCommandlet.h"

#include <fstream>

#include "../../backendEngine/include/commandletUtils.h"
#include "../../../common/animation/include/eventsContainer.h"
#include "../../../common/game/include/gameItemActions.h"
#include "../../../common/gameTweakDB/include/records/tweakDBItem.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../common/resource/include/resourceEditorDepotImpl.h"

class AnimSetSimplePropsCommandlet : public ResaveCommandlet
{
	RTTI_DECLARE_TYPE( AnimSetSimplePropsCommandlet );
	RED_BASE_CLASS( ResaveCommandlet );

public:
	AnimSetSimplePropsCommandlet();
	virtual ~AnimSetSimplePropsCommandlet();

	virtual const AnsiChar* GetOneLiner() const override final;
	virtual Bool Execute( const red::CommandLine& options ) override final;
	virtual void PrintHelp() const override final;

private:
	virtual Bool OnResourcePreSave( CResource& resource ) override final;
	Bool TryToReplaceProp( game::data::RecordID& recordId ) const;

	Bool m_doPerforceCheckout;
	red::HashMap< game::data::RecordID, game::data::RecordID > m_propIdToSimplePropIdMap;
	red::DynArray< String > m_skippedResourcesPaths;
	red::DynArray< String > m_modifiedResourcesPaths;
	red::DynArray< String > m_failedResourcesPaths;
};

RTTI_BEGIN_TYPE( AnimSetSimplePropsCommandlet )
	RTTI_PARENT_TYPE( ResaveCommandlet )
RTTI_END_TYPE()

AnimSetSimplePropsCommandlet::AnimSetSimplePropsCommandlet()
	: TBaseClass( RED_NAME_CONSTEXPR( "animSetSimpleProps" ) )
	, m_propIdToSimplePropIdMap( red::PoolBackend() )
	, m_skippedResourcesPaths( red::PoolBackend() )
	, m_modifiedResourcesPaths( red::PoolBackend() )
	, m_failedResourcesPaths( red::PoolBackend() )
{
}

AnimSetSimplePropsCommandlet::~AnimSetSimplePropsCommandlet() = default;

const AnsiChar* AnimSetSimplePropsCommandlet::GetOneLiner() const
{
	return "Anim set simple props.";
}

void AnimSetSimplePropsCommandlet::PrintHelp() const 
{
	RED_LOG( "Usage:" );
	RED_LOG( " animSetSimpleProps" );
	RED_LOG( "Will modify Workspot Item events in animsets in depot in a way that previously used props are replaced with their simpler counterparts (without animated components)." );
	RED_LOG( "Example: animSetSimpleProps -dir=base -glob=*.anims -perforceCheckout 1 -tty -cleanlog -force > Z:\\log.txt" );
}

Bool DoPropRecordsMatch( const game::data::Item_Record& propRecord, const game::data::Item_Record& otherRecord )
{
	constexpr const AnsiChar* simplePropPrefix = "crowd_";
	constexpr const AnsiChar* alternativeSimplePropPrefix = "crowd__"; // Alternative prefix was added to fix cases when CName hash collision happens
	return simplePropPrefix + propRecord.friendlyName() == otherRecord.friendlyName()
		|| alternativeSimplePropPrefix + propRecord.friendlyName() == otherRecord.friendlyName();
}

Bool AnimSetSimplePropsCommandlet::Execute( const red::CommandLine& options )
{
	m_doPerforceCheckout = true;
	constexpr const AnsiChar* perforceCheckoutOptionName = "perforceCheckout";
	if ( options.HasOption( perforceCheckoutOptionName ) )
	{
		options.GetFirstParam( perforceCheckoutOptionName, m_doPerforceCheckout );
	}

	m_propIdToSimplePropIdMap.Clear();
	m_skippedResourcesPaths.Clear();
	m_modifiedResourcesPaths.Clear();
	m_failedResourcesPaths.Clear();

	red::DynArray< THandle< game::data::Item_Record > > propRecords{ red::PoolBackend() };
	game::data::GetTweakDB().ReloadOnce(false);
	const red::DynArray< THandle< game::data::TweakDBRecord > >& records = game::data::GetRecords( ClassID< game::data::Item_Record >() );
	for ( const THandle< game::data::TweakDBRecord>& record : records )
	{
		THandle< game::data::Item_Record > itemRecord = Cast< game::data::Item_Record >( record );
		if ( itemRecord )
		{
			const Bool isPropItem = itemRecord->tags().Exist( RED_NAME_CONSTEXPR( "Prop" ) );
			if ( isPropItem )
			{
				propRecords.PushBack( itemRecord );
			}
		}
	}

	for ( const THandle< game::data::Item_Record >& propRecord : propRecords )
	{
		auto iterator = std::find_if( propRecords.Begin(), propRecords.End(), [ propRecord ]( const THandle< game::data::Item_Record >& otherRecord )
		{
			return DoPropRecordsMatch( *propRecord, *otherRecord );
		} );
		if ( iterator != propRecords.End() )
		{
			RED_ASSERT( !m_propIdToSimplePropIdMap.KeyExist( propRecord->GetID() ) );
			m_propIdToSimplePropIdMap[ propRecord->GetID() ] = (*iterator)->GetID();
		}
	}

	RED_LOG( "\n\tANIM SET SIMPLE PROPS COMMANDLET STARTS...\n" );
	const Bool result = TBaseClass::Execute( options );
	std::sort( m_skippedResourcesPaths.Begin(), m_skippedResourcesPaths.End() );
	std::sort( m_modifiedResourcesPaths.Begin(), m_modifiedResourcesPaths.End() );
	std::sort( m_failedResourcesPaths.Begin(), m_failedResourcesPaths.End() );

	RED_LOG( "\n\tANIM SET SIMPLE PROPS COMMANDLET SUMMARY..." );

	RED_LOG( "\nSkipped anim sets (%u)", m_skippedResourcesPaths.Size() );
	for ( const String& resourcePath : m_skippedResourcesPaths )
	{
		RED_LOG( "%hs", resourcePath.AsChar() );
	}

	RED_LOG( "\nModified anim sets (%u)", m_modifiedResourcesPaths.Size() );
	for ( const String& resourcePath : m_modifiedResourcesPaths )
	{
		RED_LOG( "%hs", resourcePath.AsChar() );
	}

	RED_LOG( "\nFailed (e.g. p4 checkout fail) anim sets (%u)", m_failedResourcesPaths.Size() );
	for ( const String& resourcePath : m_failedResourcesPaths )
	{
		RED_LOG( "%hs", resourcePath.AsChar() );
	}

	RED_LOG( "\nFound simple props (%u)", m_propIdToSimplePropIdMap.Size() );
	for ( const auto& mapEntry : m_propIdToSimplePropIdMap )
	{
		RED_LOG( "%hs", mapEntry.Value().ToString().AsChar() );
	}

	RED_LOG( "\n\tANIM SET SIMPLE PROPS COMMANDLET ENDS...\n" );
	return result;
}

Bool AnimSetSimplePropsCommandlet::TryToReplaceProp( game::data::RecordID& recordId ) const
{
	if ( const game::data::RecordID* newRecordId = m_propIdToSimplePropIdMap.FindPtr( recordId ) )
	{
		recordId = *newRecordId;
		return true;
	}
	else
	{
		return false;
	}
}

Bool AnimSetSimplePropsCommandlet::OnResourcePreSave( CResource& resource )
{
	const res::ResourcePath& resourcePath = resource.GetPath();
	if ( !resource.IsA< anim::AnimSet >() )
	{
		RED_LOG_ERROR( "Resource '%hs' is not an anim set.", resourcePath.ToDebugString() );
		return false;
	}

	Uint32 modifiedEventsCount = 0;
	anim::AnimSet& animSet = static_cast< anim::AnimSet& >( resource );
	for ( const THandle< anim::AnimSetEntry >& animSetEntry : animSet.EDITOR_GetAnimations() )
	{
		red::DynArray< THandle< anim::AnimEvent > >& events = animSetEntry->EDITOR_GetRegularEvents()->EDITOR_GetEvents();
		for ( const THandle< anim::AnimEvent > event : events )
		{
			if ( THandle< anim::AnimEvent_WorkspotItem > workspotItemEvent = Cast< anim::AnimEvent_WorkspotItem >( event ) )
			{
				for ( const THandle< work::IWorkspotItemAction >& action : workspotItemEvent->m_actions )
				{
					// EquipPropToSlotAction, UnequipPropAction, UnequipFromSlotAction actions are skipped as they do not contain item record id
					if ( THandle< work::EquipInventoryWeaponAction > specificAction = Cast< work::EquipInventoryWeaponAction >( action ) )
					{
						modifiedEventsCount += TryToReplaceProp( specificAction->m_fallbackItem );
					}
					else if ( THandle< work::EquipItemToSlotAction > specificAction = Cast< work::EquipItemToSlotAction >( action ) )
					{
						modifiedEventsCount += TryToReplaceProp( specificAction->m_item );
					}
					else if ( THandle< work::UnequipItemAction > specificAction = Cast< work::UnequipItemAction >( action ) )
					{
						modifiedEventsCount += TryToReplaceProp( specificAction->m_item );
					}
				}
			}
		}
	}

	if ( modifiedEventsCount == 0 )
	{
		RED_LOG( "Resource '%hs' is not resaved as it does not have workspot item events that have to be modified.", resourcePath.ToDebugString() );
		m_skippedResourcesPaths.PushBack( resourcePath.ToString() );
		return false;
	}

	if ( m_doPerforceCheckout )
	{
		const AnsiChar* tempFilePath = "animSetSimplePropsTemp.txt";
		const res::ResourceEditorDepot* depot = commandlet::GetEditorDepot();
		const String p4CheckoutCommandStr = String::Printf( "p4 -s edit %hs > %hs", depot->TranslateResourcePath( resourcePath ).ToDebugString(), tempFilePath );
		std::system( p4CheckoutCommandStr.AsChar() );
		std::ifstream tempFileStream = std::ifstream( tempFilePath );
		std::string tempFileContentString( ( std::istreambuf_iterator< AnsiChar >( tempFileStream ) ), std::istreambuf_iterator< AnsiChar >() );
		if ( tempFileContentString.find( "warning:" ) != std::string::npos || tempFileContentString.find( "error:" ) != std::string::npos )
		{
			RED_LOG_ERROR( "Failed to execute command '%hs'. Full message: %hs.", p4CheckoutCommandStr.AsChar(), tempFileContentString.data() );
			m_failedResourcesPaths.PushBack( resourcePath.ToString() );
			return false;
		}
	}

	RED_LOG( "Resource '%hs' and its %u workspot item events are processed successfully.", resourcePath.ToDebugString(), modifiedEventsCount );
	m_modifiedResourcesPaths.PushBack( resourcePath.ToString() );
	return true;
}
