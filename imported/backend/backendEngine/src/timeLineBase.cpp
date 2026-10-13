/**
* Copyright (c) 2016-2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "timeLineBase.h"

#include "editActionHistory.h"

#include "../../../common/redReflection/include/rttiPathParser.h"
#include "../../../common/redReflection/include/resource.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( TimeLineBaseDescriptor, tools );
	RTTI_PARENT_TYPE( TimeLineDescription );
	RTTI_PROPERTY( m_tracks );
	RTTI_PROPERTY( m_sectors );
	RTTI_PROPERTY( m_events );
	RTTI_PROPERTY( m_eventsGroups );
	RTTI_PROPERTY( m_lastUsedId );
	RTTI_PROPERTY( m_tracksRootId );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( TimelineItemDataHolder, tools );
	RTTI_PARENT_TYPE( IBackendData );
	RTTI_PROPERTY( m_item );
	RTTI_PROPERTY( m_parentTrack );
	RTTI_PROPERTY( m_groups );
RTTI_END_TYPE();

namespace tools
{
	constexpr char* s_eventsGroupIdsPropertyName = "eventsGroupIds";

	TimeLineBaseDescriptor::TimeLineBaseDescriptor()
		: TimeLineBaseDescriptor( nullptr )
	{
	}

	TimeLineBaseDescriptor::TimeLineBaseDescriptor( const THandle< CResource >& parentResource )
		: m_lastUsedId( 0 )
		, m_tracksRootId( TimelineInvalidId )
		, m_parentResource( parentResource )
	{
	}

	void TimeLineBaseDescriptor::Initialize( TimeLineBase* timeline )
	{
		if ( m_tracksRootId == TimelineInvalidId )
			timeline->CreateRootTrack();

		m_tracks.Initialize< TimeLineTrackBaseItem >( timeline );
		m_events.Initialize< TimeLineEventItem >( timeline );
		m_sectors.Initialize< TimeLineSectorItem >( timeline );
		m_eventsGroups.Initialize< TimeLineEventsGroupItem >( timeline );
	}

	Bool TimeLineBaseDescriptor::OnPropertyMissing( CName propertyName, const rtti::Variant& readValue )
	{
		if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "acks" ) )
		{
			TimeLineItemCollection tracks;
			readValue.Get< TimeLineItemCollection >( tracks );

			m_tracks = tracks;

			return true;
		}
		else if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "ctors" ) )
		{
			TimeLineItemCollection sectors;
			readValue.Get< TimeLineItemCollection >( sectors );

			m_sectors = sectors;

			return true;
		}
		else if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "ents" ) )
		{
			TimeLineItemCollection events;
			readValue.Get< TimeLineItemCollection >( events );

			m_events = events;

			return true;
		}
		else if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "stUsedId" ) )
		{
			TimelineId lastUsedId;
			readValue.Get< TimelineId >( lastUsedId );

			m_lastUsedId = lastUsedId;

			return true;
		}
		else if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "acksRootId" ) )
		{
			TimelineId tracksRootId;
			readValue.Get< TimelineId >( tracksRootId );

			m_tracksRootId = tracksRootId;

			return true;
		}

		// Pass to base class
		return TimeLineDescription::OnPropertyMissing( propertyName, readValue );
	}

	///
	///
	///

	TimeLineBase::TimeLineBase( THandle< TimeLineBaseDescriptor > descriptor )
		: m_descriptor( descriptor )
		, m_targetTrack( TimelineInvalidId )
		, m_timelineListeners{ red::PoolBackend() }
		, m_smartTrackSelection( true )
	{
	}

	Bool TimeLineBase::Initialize()
	{
		if ( !m_descriptor )
			return false;

		m_descriptor->Initialize( this );
		return true;
	}

	void TimeLineBase::SetEnableControl( Bool enable )
	{
		String msg = String::Printf( "controlEnabled %s", enable ? "true" : "false" );
		for ( ITimeLineListener* listener : m_timelineListeners )
		{
			listener->OnInvalidateTimelineDescriptionProperty( msg );
		}
	}

	void TimeLineBase::AttachTimeLineListener( ITimeLineListener* listener )
	{
		RED_ASSERT( !m_timelineListeners.Exist( listener ), "Listener already registered" );

		if ( listener && !m_timelineListeners.Exist( listener ) )
		{
			m_timelineListeners.PushBack( listener );
		}
	}

	void TimeLineBase::DetachTimeLineListener( ITimeLineListener* listener )
	{
		m_timelineListeners.Remove( listener );
	}

	void TimeLineBase::CreateRootTrack()
	{
		// Creates root of all tracks
		THandle< TimeLineTrackBaseItem > root = CreateTrackDescriptorItem( TimeLineTracks::TracksRoot );

		m_descriptor->m_tracks.Add( root );
		m_descriptor->m_tracksRootId = root->GetId();

		root->OnItemCreated( TimeLineTracks::TracksRoot, TimelineInvalidId );
	}

	void TimeLineBase::ClearTimeline()
	{
		m_descriptor->m_tracks.Clear();
		m_descriptor->m_events.Clear();
		m_descriptor->m_sectors.Clear();
		m_descriptor->m_eventsGroups.Clear();

		m_descriptor->m_tracksRootId = TimelineInvalidId;

		OnTimelineCleared();
		MarkModified();
	}

	void TimeLineBase::OnTimelineCleared()
	{
		CreateRootTrack();
	}

	void TimeLineBase::OnLayersChanged()
	{
		for ( ITimeLineListener* listener : m_timelineListeners )
		{
			listener->OnTimelineActiveLayersChanged( m_activeLayers );
		}
	}

	THandle< ISerializable > TimeLineBase::GetItemData( TimelineId id ) const
	{
		THandle< ISerializable > data = GetEventData( id );
		if ( !data )
			data = GetTrackData( id );

		return data;
	}

	THandle< TimelineItemDataHolder > TimeLineBase::CreateItemDataholder( TimelineId id ) const
	{
		// backendEngine_timeline_szi Investigate. Item is either an event, a track, an event group or a sector but this code handles only events and event groups.

		THandle< ISerializable > description = GetEventDescription( id );
		if ( description )
		{
			THandle< TimeLineEventDescription > eventDesc = Cast< TimeLineEventDescription >( description );
			THandle< TimeLineTrackDescription > parentTrack = GetTrackDescription( eventDesc->GetBoundTrackId() );
			if ( !description )
				description = GetTrackDescription( id );

			return CreateHandle< TimelineItemDataHolder >( description, parentTrack, eventDesc->GetBoundEventsGroupIds() );
		}
		else
		{
			return CreateHandle< TimelineItemDataHolder >( GetEventsGroupDescription( id ), nullptr );
		}

		RED_FATAL( "Trying to access unhandled item type!" );
		return nullptr;
	}

	void TimeLineBase::OnDataItemChanged( TimelineId id, const rtti::AccessPath& path )
	{
		THandle< ISerializable > descriptor = GetEventDescription( id );
		if ( descriptor )
		{
			THandle< TimeLineEventItem > eventDescriptor = Cast< TimeLineEventItem >( descriptor );
			eventDescriptor->OnRuntimeDataChanged( path );
		}
	}

	void TimeLineBase::InvalidateTimeline()
	{
		for ( ITimeLineListener* listener : m_timelineListeners )
		{
			listener->OnInvalidateTimeline();
		}
	}

	void TimeLineBase::OnInvalidateDescription( TimelineId id )
	{
		for( ITimeLineListener* listener : m_timelineListeners )
		{
			listener->OnInvalidateDescription( id );
		}
	}

	void TimeLineBase::OnItemDescriptionPropertyChange( TimelineId id, const rtti::AccessPath& path )
	{
		CName propName;
		if( rtti::PathParser( path ).EatName( propName ) )
		{
			for( ITimeLineListener* listener : m_timelineListeners )
			{
				listener->OnInvalidateDescriptionProperty( id, propName.AsChar() );
			}
		}
	}

	void TimeLineBase::OnEventComponentChanged( TimelineId id, Uint64 componentId )
	{
		for( ITimeLineListener* listener : m_timelineListeners )
		{
			listener->OnEventComponentChanged( id, componentId );
		}
	}

	void TimeLineBase::RebindEvents()
	{
		for ( THandle< TimeLineItemDescription > item : m_descriptor->GetEvents() )
		{
			THandle< TimeLineEventItem > event = Cast< TimeLineEventItem >( item );
			THandle< TimeLineTrackBaseItem > track = GetTrack( event->GetBoundTrackId() );

			if ( !track->HasChild( event->GetId() ) )
			{
				track->AddChild( event->GetId() );
				MarkModified();
			}
		}
	}

	void TimeLineBase::MarkModified()
	{
		const THandle< CResource >& resource = m_descriptor->GetParentResource();
		if ( resource )
		{
			resource->MarkModified();
		}
	}

	void TimeLineBase::ToggleLayer( const String& layer )
	{
		if ( m_activeLayers.Exist( layer ) )
		{
			m_activeLayers.RemoveReorder( layer );
		}
		else
		{
			m_activeLayers.PushBack( layer );
		}

		OnLayersChanged();
	}

	void TimeLineBase::ActivateLayer( const String& layer )
	{
		if ( !m_activeLayers.Exist( layer ) )
		{
			m_activeLayers.PushBack( layer );
			OnLayersChanged();
		}
	}

	void TimeLineBase::DeactivateLayer( const String& layer )
	{
		if ( m_activeLayers.Exist( layer ) )
		{
			m_activeLayers.RemoveReorder( layer );
			OnLayersChanged();
		}
	}

	void TimeLineBase::GetTracksInOrder( red::DynArray< TimelineId >& tracksIds ) const
	{
		if ( m_descriptor && TimelineInvalidId != GetTracksRootId() )
		{
			tracksIds.PushBack( GetTracksRootId() );

			THandle< TimeLineTrackDescription > description = GetTrackDescription( GetTracksRootId() );
			if ( !description )
			{
				tracksIds.Clear();
				return;
			}

			THandle< TimeLineTrackBaseItem > base = Cast< TimeLineTrackBaseItem >( description );
			if ( base )
			{
				base->RegisterTracks( tracksIds );
			}
		}
	}

	void TimeLineBase::GetTrackType( TimelineId trackId, String& type ) const
	{
		THandle< TimeLineTrackDescription > itemDescription = GetTrackDescription( trackId );
		if ( itemDescription )
		{
			type = itemDescription->GetType();
		}
	}

	Bool TimeLineBase::IsTrackVisible( TimelineId trackId ) const
	{
		if ( trackId != GetTracksRootId() )
		{
			THandle< TimeLineTrackDescription > itemDescription = GetTrackDescription( trackId );
			if ( itemDescription )
			{
				if ( !itemDescription->IsSelfVisible() )
					return false;

				return IsTrackVisible( itemDescription->GetParentId() );
			}
		}
		return true;
	}

	TimelineId TimeLineBase::GetTrackParentId( TimelineId trackId ) const
	{
		THandle< TimeLineTrackDescription > itemDescription = GetTrackDescription( trackId );
		if ( itemDescription )
		{
			return itemDescription->GetParentId();
		}
		return TimelineInvalidId;
	}

	THandle< TimeLineTrackDescription > TimeLineBase::GetTrackDescription( TimelineId trackId ) const
	{
		return HandleFromPtr( m_descriptor->m_tracks.Find< TimeLineTrackDescription >( trackId ) );
	}
	
	THandle< ISerializable > TimeLineBase::GetTrackData( TimelineId trackId ) const
	{
		THandle< TimeLineTrackBaseItem > itemDescription = Cast< TimeLineTrackBaseItem>( GetTrackDescription( trackId ) );
		if ( itemDescription )
			return itemDescription->GetRuntimeData();

		return nullptr;
	}

	void TimeLineBase::GetDefaultEventTypes( const TimelineId trackId, String& outType, String& outAlternativeType ) const
	{
		THandle< TimeLineTrackDescription > trackDescription = GetTrackDescription( trackId );
		if ( !trackDescription )
			return;

		red::DynArray< String > supportedEventTypes{ red::PoolBackend() };
		GetSupportedEventTypes( trackId, supportedEventTypes );

		if ( !supportedEventTypes.Empty() )
		{
			outType = supportedEventTypes.Front();
			outAlternativeType = outType;
		}
	}

	void TimeLineBase::GetSupportedTrackTypes( TimelineId trackId, red::DynArray< String >& outTypes ) const
	{
		outTypes.Clear();

		THandle< TimeLineTrackDescription > trackDescription = GetTrackDescription( trackId );
		if ( !trackDescription )
			return;

		if ( trackId == GetTracksRootId() )
		{
			outTypes.PushBack( TimeLineTracks::Track );
			outTypes.PushBack( TimeLineTracks::TracksGroup );
		}
		else if ( trackDescription->GetType() == TimeLineTracks::TracksGroup )
		{
			outTypes.PushBack( TimeLineTracks::Track );
		}
	}

	void TimeLineBase::GetSupportedEventTypes( TimelineId trackId, red::DynArray< String >& outTypes) const
	{
		outTypes.Clear();

		THandle< TimeLineTrackDescription > trackDescription = GetTrackDescription( trackId );
		if ( !trackDescription )
			return;

		if ( trackDescription->GetType() == TimeLineTracks::Track )
		{
			outTypes.PushBack( TimeLineEvents::PointEvent );
			outTypes.PushBack( TimeLineEvents::DurationEvent );
		}
	}
	
	void TimeLineBase::GetTrackEvents( TimelineId trackId, red::DynArray< TimelineId >& outEvents )
	{
		outEvents.Clear();

		THandle< TimeLineTrackBaseItem > trackDescription = GetTrack( trackId );
		if ( !trackDescription )
			return;

		if ( trackDescription->GetType() == TimeLineTracks::Track )
		{
			trackDescription->GetChildren( outEvents );
		}
	}

	void TimeLineBase::GetTrackSubtracks( TimelineId trackId, red::DynArray< TimelineId >& outTracks )
	{
		outTracks.Clear();

		THandle< TimeLineTrackBaseItem > trackDescription = GetTrack( trackId );
		if ( !trackDescription )
			return;

		if ( trackDescription->GetType() == TimeLineTracks::TracksGroup || trackDescription->GetType() == TimeLineTracks::TracksRoot )
		{
			trackDescription->GetChildren( outTracks );
		}
	}

	void TimeLineBase::ReparentTrack( TimelineId trackId, TimelineId newParentId )
	{
		THandle< TimeLineTrackBaseItem > track = GetTrack( trackId );
		if ( !track )
			return;

		THandle< TimeLineTrackBaseItem > newParentTrack = GetTrack( newParentId );
		if ( track->GetType() == TimeLineTracks::TracksGroup && newParentTrack->GetType() == TimeLineTracks::TracksGroup )
		{
			// groups inside groups are not supported
			return;
		}

		TimelineId oldParentId = track->GetParentId();
		if ( oldParentId != TimelineInvalidId )
		{
			THandle< TimeLineTrackBaseItem > parentTrack = GetTrack( oldParentId );

			track->SetParentId( TimelineInvalidId );
			parentTrack->RemoveChild( trackId );
		}

		if ( newParentId != TimelineInvalidId )
		{
			THandle< TimeLineTrackBaseItem > newParentTrack = GetTrack( newParentId );
			newParentTrack->AddChild( trackId );
		}

		track->SetParentId( newParentId );

		for ( ITimeLineListener* listener : m_timelineListeners )
		{
			listener->OnTrackReparent( trackId, newParentId );
		}

		MarkModified();
	}

	THandle< TimeLineTrackBaseItem > TimeLineBase::CreateTrackDescriptorItem( const String& type, const String& visualType /*= "default"*/ )
	{
		return CreateHandle< TimeLineTrackBaseItem >( this, GenerateTimelineId(), type, visualType );
	}

	TimelineId TimeLineBase::CreateTrack( TimelineId parentTrackId, const String& type, const String& caption, const TimelineId previousId, const String& visualType /*= "default"*/ )
	{
		if ( parentTrackId == TimelineInvalidId )
		{
			RED_ASSERT( "Parent Id cannot be invalid!" );
			return TimelineInvalidId;
		}
		red::DynArray< red::String > outTypes{ red::PoolBackend() };
		GetSupportedTrackTypes( parentTrackId, outTypes );

		if ( !outTypes.Exist( type ) )
		{
			// Type not supported
			return TimelineInvalidId;
		}

		return InternalCreateTrack( parentTrackId, type, caption, previousId, visualType );
	}

	TimelineId TimeLineBase::InternalCreateTrack( TimelineId parentTrackId, const String& type, const String& caption, const TimelineId previousId, const String& visualType /*= "default"*/ )
	{
		THandle<TimeLineTrackBaseItem> track = CreateTrackDescriptorItem( type, visualType );
		if ( track == nullptr )
		{
			RED_ASSERT( "Failed to create an object" );
			return TimelineInvalidId;
		}

		track->SetParentId( parentTrackId );
		track->SetCaption( caption );

		RegisterTrack( track );

		track->OnItemCreated( type, parentTrackId );

		for ( ITimeLineListener* listener : m_timelineListeners )
			listener->OnTrackCreated( track->GetId(), parentTrackId, type );

		if ( previousId != TimelineInvalidId )
		{
			MoveTrackAfter( track->GetId(), previousId );
		}

		return track->GetId();
	}

	Bool TimeLineBase::RegisterTrack( THandle< TimeLineTrackBaseItem > track )
	{
		THandle< TimeLineTrackBaseItem > oldTrack = GetTrack( track->GetId() );
		if ( oldTrack )
		{
			if ( oldTrack == track )
				return true;

			return false;
		}

		THandle< TimeLineTrackBaseItem > parent = GetTrack( track->GetParentId() );
		parent->AddChild( track->GetId() );

		m_descriptor->m_tracks.Add( track );
		MarkModified();

		return true;
	}


	Bool TimeLineBase::RegisterEventsGroup( THandle< TimeLineEventsGroupItem > eventsGroup )
	{
		THandle< TimeLineEventsGroupItem > oldEventsGroups = GetEventsGroup( eventsGroup->GetId() );
		if( oldEventsGroups )
		{
			if( oldEventsGroups == eventsGroup )
				return true;

			return false;
		}

		m_descriptor->m_eventsGroups.Add( eventsGroup );
		MarkModified();

		return true;
	}

	void TimeLineBase::RemoveTrack( TimelineId trackId )
	{
		if ( trackId == GetTracksRootId() )
			return;

		THandle< TimeLineTrackBaseItem > track = GetTrack( trackId );
		if ( track == nullptr )
			return;

		if ( track->GetType() == TimeLineTracks::Track )
		{
			// remove events binded to this track
			red::DynArray< TimelineId > events{ red::PoolBackend() };
			track->GetChildren( events );
			for ( TimelineId id : events )
			{
				RemoveEvent( id );
			}
		}
		else if ( track->GetType() == TimeLineTracks::TracksGroup )
		{
			red::DynArray< TimelineId > tracks{ red::PoolBackend() };
			track->GetChildren( tracks );
			for ( TimelineId id : tracks )
			{
				RemoveTrack( id );
			}
		}

		track->OnItemRemoved();

		THandle< TimeLineTrackBaseItem > parent = GetTrack( track->GetParentId() );
		if ( parent != nullptr )
		{
			parent->RemoveChild( trackId );
		}

		for ( ITimeLineListener* listener : m_timelineListeners )
		{
			listener->OnTrackDeleted( track->GetId(), track->GetParentId() );
		}

		m_descriptor->m_tracks.Remove( trackId );
		MarkModified();
	}

	Bool TimeLineBase::RestoreTrack( const THandle< TimeLineTrackBaseItem >& track )
	{
		RegisterTrack( track );

		track->SetTimeline( this );
		track->Restore();

		for ( ITimeLineListener* listener : m_timelineListeners )
			listener->OnTrackCreated( track->GetId(), track->GetParentId(), track->GetType() );

		return true;
	}

	void TimeLineBase::ReparentEvents( TimelineId trackId, const red::DynArray< TimelineId >& eventsId )
	{
		if ( trackId == TimelineInvalidId )
			return;

		for ( TimelineId eventId : eventsId )
		{
			if ( SetEventTrack( eventId, trackId ) )
			{
				for ( ITimeLineListener* listener : m_timelineListeners )
				{
					listener->OnEventReparent( eventId, trackId );
				}
			}
		}
	}

	void TimeLineBase::MoveTrackBefore( TimelineId trackId, TimelineId destinationTrackId )
	{
		THandle< TimeLineTrackBaseItem > destinationTrack = GetTrack( destinationTrackId );
		THandle< TimeLineTrackBaseItem > trackToMove = GetTrack( trackId );

		if ( destinationTrack->GetParentId() != trackToMove->GetParentId() )
		{
			ReparentTrack( trackId, destinationTrack->GetParentId() );
		}

		THandle< TimeLineTrackBaseItem > parentTrack = GetTrack( destinationTrack->GetParentId() );

		Uint32 newIndex = parentTrack->MoveChildBefore( trackToMove->GetId(), destinationTrack->GetId() );
		if ( newIndex != -1 )
		{
			parentTrack->OnChildItemReordered( trackId, newIndex );

			for ( ITimeLineListener* listener : m_timelineListeners )
			{
				listener->OnTrackReordered( trackId, newIndex );
			}

			MarkModified();
		}
	}

	void TimeLineBase::MoveTrackAfter( TimelineId trackId, TimelineId destinationTrackId )
	{
		THandle< TimeLineTrackBaseItem > destinationTrack = GetTrack( destinationTrackId );
		THandle< TimeLineTrackBaseItem > trackToMove = GetTrack( trackId );

		if ( destinationTrack->GetParentId() != trackToMove->GetParentId() )
		{
			ReparentTrack( trackId, destinationTrack->GetParentId() );
		}

		THandle< TimeLineTrackBaseItem > parentTrack = GetTrack( destinationTrack->GetParentId() );

		Uint32 newIndex = parentTrack->MoveChildAfter( trackToMove->GetId(), destinationTrack->GetId() );
		if ( newIndex != -1 )
		{
			parentTrack->OnChildItemReordered( trackId, newIndex );

			for ( ITimeLineListener* listener : m_timelineListeners )
			{
				listener->OnTrackReordered( trackId, newIndex );
			}

			MarkModified();
		}
	}

	void TimeLineBase::GetEventType( TimelineId eventId, String& type ) const
	{
		THandle< TimeLineEventItem > itemDescription = GetEvent( eventId );
		if ( itemDescription )
			type = itemDescription->GetType();
	}

	Bool TimeLineBase::IsEventVisible( TimelineId eventId ) const
	{
		return IsTrackVisible( GetEventTrack( eventId ) );
	}

	// Returns events id
	TimelineId TimeLineBase::GetEventTrack( TimelineId eventId ) const
	{
		THandle< TimeLineEventItem > itemDescription = GetEvent( eventId );
		if ( itemDescription )
			return itemDescription->GetBoundTrackId();

		return TimelineInvalidId;
	}

	Bool TimeLineBase::IsEvent( TimelineId itemId ) const
	{
		return m_descriptor->m_events.Find< TimeLineEventDescription >( itemId ) != nullptr;
	}

	THandle< ISerializable > TimeLineBase::GetEventData( TimelineId eventId ) const
	{
		THandle< TimeLineEventItem > itemDescription = GetEvent( eventId );
		if ( itemDescription )
			return itemDescription->GetRuntimeData();

		return nullptr;
	}

	void TimeLineBase::GetEventCurveData( TimelineId eventId, red::DynArray< String >& data ) const
	{
		THandle< TimeLineEventItem > itemDescription = GetEvent( eventId );
		if ( itemDescription )
			data = itemDescription->GetCurveData();
	}

	void TimeLineBase::GetEventEvaluatorsPath( TimelineId eventId, red::DynArray< String >& evaluators ) const
	{
		THandle< TimeLineEventItem > itemDescription = GetEvent( eventId );
		if ( itemDescription )
			evaluators = itemDescription->GetEvaluatorsPath();
	}

	void TimeLineBase::GetEventSnapPoints( TimelineId eventId, red::DynArray< TimelineSnapPoint >& snapPoints ) const
	{
	}

	void TimeLineBase::RefreshSnapPoints( TimelineId eventId )
	{
	}

	THandle< TimeLineEventDescription > TimeLineBase::GetEventDescription( TimelineId eventId ) const
	{
		return HandleFromPtr( m_descriptor->m_events.Find< TimeLineEventDescription >( eventId ) );
	}

	THandle< TimeLineEventItem > TimeLineBase::CreateEventDescriptorItem( const String& type )
	{
		return CreateHandle< TimeLineEventItem >( this, GenerateTimelineId(), type, "default" );
	}

	void TimeLineBase::Select( const red::DynArray< TimelineId >& eventIds )
	{
		for( ITimeLineListener* listener : m_timelineListeners )
		{
			listener->OnEventsSelected( eventIds );
		}
	}

	TimelineId TimeLineBase::CreateTimelineEvent( TimelineId parentTrackId, const String& type, Float initialPosition, const String& caption )
	{
		if ( parentTrackId == TimelineInvalidId )
			return TimelineInvalidId;

		red::DynArray< red::String > outTypes{ red::PoolBackend() };
		GetSupportedEventTypes( parentTrackId, outTypes );

		if ( !outTypes.Exist( type ) )
		{
			// Type not supported
			return TimelineInvalidId;
		}

		THandle<TimeLineEventItem> event = CreateEventDescriptorItem( type );
		if ( event == nullptr )
			return TimelineInvalidId;

		m_descriptor->m_events.Add( event );
		event->SetStartTime( initialPosition );
		event->OnItemCreated( type, parentTrackId, m_ruidGenerator.Next() );
		event->SetCaption( caption );

		if ( !SetEventTrack( event->GetId(), parentTrackId ) )
		{
			event->OnItemRemoved();
			m_descriptor->m_events.Remove( event->GetId() );

			return TimelineInvalidId;
		}

		OnEventCreated( event );

		event->SyncRuntimeEvent();

		for ( ITimeLineListener* listener : m_timelineListeners )
			listener->OnEventCreated( event->GetId(), parentTrackId, type );

		MarkModified();

		return event->GetId();
	}
	
	void TimeLineBase::RemoveEvent( TimelineId eventId )
	{
		THandle< TimeLineEventItem > event = GetEvent( eventId );
		if ( event == nullptr )
			return;

		event->OnItemRemoved();

		for ( ITimeLineListener* listener : m_timelineListeners )
		{
			listener->OnEventDeleted( eventId );
		}

		red::DynArray< TimelineId > eventGroups{ red::PoolBackend() };
		GetEventsGroups( eventGroups );

		for ( const TimelineId groupId : eventGroups )
		{
			THandle< TimeLineEventsGroupItem > eventsGroup = GetEventsGroup( groupId );
			eventsGroup->RemoveEvent( eventId );
		}

		RemoveEventFromTrack( event->GetBoundTrackId(), event->GetId() );
		m_descriptor->m_events.Remove( eventId );
		MarkModified();

		OnEventRemoved( event );
	}

	Bool TimeLineBase::CanRemoveEvent( TimelineId eventId ) const
	{
		THandle< TimeLineEventItem > event = GetEvent( eventId );
		if( event == nullptr )
			return false;

		if( THandle< TimeLineTrackBaseItem > track = GetTrack( event->GetBoundTrackId() ) )
		{
			const Bool canRemove = track->CanTrackDeleteEvents();
			return canRemove;
		}

		return true;
	}

	void TimeLineBase::SetEventTrack( THandle< TimeLineEventItem > event, TimelineId trackId )
	{
		const TimelineId oldTrackId = event->GetBoundTrackId();
		const TimelineId eventId = event->GetId();
		RemoveEventFromTrack( oldTrackId, eventId );

		event->BindToTrackId( TimelineInvalidId );

		AddEventToTrack( *event, trackId );

		MarkModified();
	}

	void TimeLineBase::RemoveEventFromTrack( TimelineId trackId, TimelineId eventId )
	{
		if ( trackId != TimelineInvalidId )
		{
			THandle< TimeLineTrackBaseItem > track = GetTrack( trackId );

			// Shouldn't we also unbind event from track? That would be symmetrical to what AddEventToTrack() does. As of now,
			// things will work properly only if we're in the middle of event removal process.. but we shouldn't assume it here.
			track->RemoveChild( eventId );

			MarkModified();
		}
	}

	void TimeLineBase::AddEventToTrack( TimeLineEventItem& event, TimelineId trackId )
	{
		if ( trackId != TimelineInvalidId )
		{
			THandle< TimeLineTrackBaseItem > track = GetTrack( trackId );
			if ( track != nullptr )
			{
				track->AddChild( event.GetId() );
				event.BindToTrackId( trackId );
				MarkModified();
			}
		}
	}

	Bool TimeLineBase::SetEventTrack( TimelineId eventId, TimelineId trackId )
	{
		if ( !CanEventBindToTrack( eventId, trackId ) )
			return false;

		THandle< TimeLineEventItem > event = GetEvent( eventId );
		if ( !event )
			return false;

		TimelineId oldTrackId = event->GetBoundTrackId();
		if ( oldTrackId == trackId )
			return true;

		SetEventTrack( event, trackId );

		event->OnItemAttachedToParent( oldTrackId, trackId );

		MarkModified();

		return true;
	}

	Bool TimeLineBase::RegisterEvent( THandle< TimeLineEventItem > event )
	{
		THandle< TimeLineEventItem > oldEvent = GetEvent( event->GetId() );
		if ( oldEvent )
		{
			if ( oldEvent == event )
				return true;

			return false;
		}

		m_descriptor->m_events.Add( event );
		SetEventTrack( event, event->GetBoundTrackId() );
		MarkModified();

		return true;
	}

	void TimeLineBase::SetEventSector( TimelineId eventId, TimelineId sectorId )
	{
		THandle< TimeLineEventItem > event = GetEvent( eventId );
		if ( event )
		{
			TimelineId oldSector = event->GetBoundSectorId();
			if ( oldSector != TimelineInvalidId )
			{
				THandle< TimeLineSectorItem > sector = GetSector( oldSector );
				sector->RemoveChild( eventId );
				event->BindToSectorId( TimelineInvalidId );
			}

			if ( sectorId != TimelineInvalidId )
			{
				THandle< TimeLineSectorItem > sector = GetSector( sectorId );
				if ( sector != nullptr )
				{
					event->BindToSectorId( sectorId );
					sector->AddChild( eventId );
				}
			}
			else
			{
				event->BindToSectorId( TimelineInvalidId );
			}

			MarkModified();
		}
	}

	void TimeLineBase::SetEventState( TimelineId eventId, const String& state )
	{
		THandle< TimeLineEventDescription > event = GetEventDescription( eventId );

		if( event->SetState( state ) )
		{
			OnItemDescriptionPropertyChange( eventId, rtti::AccessPath( "state" ) );
		}

		MarkModified();
	}

	Bool TimeLineBase::RestoreEvent( const THandle< TimeLineEventDescription >& event )
	{
		THandle< TimeLineEventItem > eventItem = Cast< TimeLineEventItem >( event );

		m_descriptor->m_events.Add( eventItem );
		eventItem->SetTimeline( this );

		if ( event->Restore() )
		{
			// Force binding set and attach an event to track hierarchy
			TimelineId trackId = eventItem->GetBoundTrackId();
			eventItem->BindToTrackId( TimelineInvalidId );
			SetEventTrack( eventItem, trackId );

			OnEventRestored( eventItem );

			for ( ITimeLineListener* listener : m_timelineListeners )
			{
				listener->OnEventCreated( eventItem->GetId(), eventItem->GetBoundTrackId(), eventItem->GetType() );
			}

			event->SyncRuntimeEvent();
			MarkModified();

			return true;
		}
		else
		{
			eventItem->SetTimeline( nullptr );
			m_descriptor->m_events.Remove( eventItem->GetId() );
		}

		return false;
	}

	void TimeLineBase::ResetEventId( THandle< TimeLineEventItem > item )
	{
		item->ResetId( GenerateTimelineId() );
	}

	Bool TimeLineBase::CanPlaceEventObject( const THandle< ISerializable >& object ) const
	{
		// usage: info.m_object->IsA< TimeLineEventItem >();
		return false;
	}

	Bool TimeLineBase::PlaceEvent( const THandle< TimeLineEventDescription >& event )
	{
		THandle< TimeLineEventItem > clonedEvent = Cast< TimeLineEventItem >( event );
		TimelineId targetTrack = GetTargetTrack();

		if ( targetTrack == TimelineInvalidId )
		{
			// check if we are coping an event from a different timeline...		
			TimeLineEventDescription* currentEvent = m_descriptor->GetEvent< TimeLineEventDescription >( event->GetId() );
			if ( currentEvent == nullptr )
			{			
				// How to solve the situation that we are coping an event from a different timeline? We need to know where to paste it at last.
				return false;
			}
			else
			{
				const rtti::ClassType* type = currentEvent->GetBaseObjectClass();
				if ( type->DeepCompare( currentEvent, event.Get(), 1 ) )
				{
					targetTrack = clonedEvent->GetBoundTrackId();
				}
				else
				{
					return false;
				}		
			}
		}

		m_descriptor->m_events.Add( clonedEvent );

		clonedEvent->SetTimeline( this );
		clonedEvent->BindToTrackId( targetTrack ); // overwrite without of calling any check functions. All check should be done during Place call!
		clonedEvent->OverrideBoundGroupIds( { red::PoolBackend() } );

		TimelineId oldId = clonedEvent->GetId();
		ResetEventId( clonedEvent );

		if ( event->Place( oldId, targetTrack ) )
		{
			// Force binding set and attach an event to track hierarchy
			clonedEvent->BindToTrackId( TimelineInvalidId );
			SetEventTrack( clonedEvent, targetTrack );

			OnEventCreated( clonedEvent );

			for ( ITimeLineListener* listener : m_timelineListeners )
			{
				listener->OnEventCreated( clonedEvent->GetId(), clonedEvent->GetBoundTrackId(), clonedEvent->GetType() );
			}

			clonedEvent->SyncRuntimeEvent();
			MarkModified();

			return true;
		}
		else
		{
			clonedEvent->SetTimeline( nullptr );
			m_descriptor->m_events.Remove( clonedEvent->GetId() );

			return false;
		}
	}

	void TimeLineBase::MoveEvent( TimelineId eventId, Float timeOffset )
	{
		if ( timeOffset == 0 )
			return;

		THandle< TimeLineEventItem > event = GetEvent( eventId );
		event->MoveEventByOffset( timeOffset );

		OnItemDescriptionPropertyChange( eventId, rtti::AccessPath{ "startTime" } );

		MarkModified();
	}

	void TimeLineBase::ResizeEvent( TimelineId eventId, TimelineResizeMode mode, Float timeOffset )
	{
		if ( timeOffset == 0 )
			return;

		THandle< TimeLineEventItem > event = GetEvent( eventId );
		event->ResizeEventByOffset( mode, timeOffset );

		OnItemDescriptionPropertyChange( eventId, rtti::AccessPath{ "startTime" } );
		OnItemDescriptionPropertyChange( eventId, rtti::AccessPath{ "durationTime" } );

		MarkModified();
	}

	void TimeLineBase::ClipEvent( TimelineId eventId, TimelineResizeMode mode, Float timeOffset )
	{
		// #timeline Do we want to implement this behavior in a generic way for all timelines? For the moment only SceneTimeline can handle this (both, in backend and frontend input side)
	}

	void TimeLineBase::MuteEvent( TimelineId eventId, const Bool isMuted )
	{
		THandle< TimeLineEventItem > event = GetEvent( eventId );
		event->SetMute( isMuted );

		OnItemDescriptionPropertyChange( eventId, rtti::AccessPath{ "isMute" } );

		MarkModified();
	}

	Bool TimeLineBase::IsEventMuted( TimelineId eventId ) const
	{
		THandle< TimeLineEventItem > event = GetEvent( eventId );
		return event->IsMute();
	}

	Float TimeLineBase::GetEventsEndTime() const
	{
		red::ArraySpan< const THandle< TimeLineItemDescription > > events = m_descriptor->GetEvents();

		Float eventsEndTime = 0.0f;

		for ( const THandle< TimeLineItemDescription >& event : events )
		{
			THandle< TimeLineEventDescription > eventItem = Cast< TimeLineEventDescription >( event );
			eventsEndTime = math::Max( eventsEndTime, eventItem->GetEndTime() );
		}

		return eventsEndTime;
	}

	void TimeLineBase::GetSectorEvents( TimelineId sectorId, red::DynArray< TimelineId >& outEvents )
	{
		THandle< TimeLineSectorItem > itemDescription = GetSector( sectorId );
		if ( !itemDescription )
			itemDescription->GetChildren( outEvents );
	}

	void TimeLineBase::GetSectorType( TimelineId sectorId, String& type ) const
	{
		THandle< TimeLineSectorItem > itemDescription = GetSector( sectorId );
		if ( itemDescription )
		{
			type = itemDescription->GetType();
		}
	}

	THandle< TimeLineSectorDescription > TimeLineBase::GetSectorDescription( TimelineId sectorId ) const
	{
		return HandleFromPtr( m_descriptor->m_sectors.Find< TimeLineSectorDescription >( sectorId ) );
	}

	void TimeLineBase::GetSupportedSectorTypes( red::DynArray< String >& outTypes ) const
	{
	}

	THandle< TimeLineSectorItem > TimeLineBase::CreateSectorItem( const String& type )
	{
		return CreateHandle< TimeLineSectorItem >( this, GenerateTimelineId(), type, "default" );
	}

	TimelineId TimeLineBase::CreateSector( const String& type, Float initialPosition, const String& caption )
	{
		if ( type != TimeLineSectors::Sector )
			return TimelineInvalidId;
		
		THandle< TimeLineSectorItem> sector = CreateSectorItem( type );
		if ( sector == nullptr )
			return TimelineInvalidId;
		
		m_descriptor->m_sectors.Add( sector );

		sector->SetCaption( caption );
		sector->SetStartTime( initialPosition );
		sector->OnItemCreated( type );

		OnSectorCreated( sector );

		for ( ITimeLineListener* listener : m_timelineListeners )
			listener->OnSectorCreated( sector->GetId(), type );

		MarkModified();

		return sector->GetId();
	}

	void TimeLineBase::RemoveSector( TimelineId sectorId )
	{
		THandle< TimeLineSectorItem > sector = GetSector( sectorId );
		if ( sector == nullptr )
			return;

		// remove all binded events to this sector
		red::DynArray< TimelineId > events{ red::PoolBackend() };
		GetSectorEvents( sectorId, events );

		for ( TimelineId id : events )
		{
			RemoveEvent( id );
		}

		for ( ITimeLineListener* listener : m_timelineListeners )
		{
			listener->OnSectorDeleted( sectorId );
		}

		sector->OnItemRemoved();
		m_descriptor->m_sectors.Remove( sectorId );
		MarkModified();
	}

	THandle< TimeLineEventsGroupItem > TimeLineBase::CreateEventsGroupDescriptorItem( const String& type )
	{
		return CreateHandle< TimeLineEventsGroupItem >( this, GenerateTimelineId(), type );
	}

	void TimeLineBase::GetEventsGroups( red::DynArray< TimelineId >& eventsGroups ) const
	{
		m_descriptor->m_eventsGroups.GetItems( eventsGroups );
	}

	void TimeLineBase::GetEventsGroupType( TimelineId eventsGroupId, String& type ) const
	{
		if( const TimeLineEventsGroupItem* const eventGroup = m_descriptor->m_eventsGroups.Find< TimeLineEventsGroupItem >( eventsGroupId ) )
		{
			type = eventGroup->GetType();
		}
	}

	TimelineId TimeLineBase::CreateEventsGroup( const String& type, const red::DynArray< TimelineId >& events )
	{
		THandle< TimeLineEventsGroupItem > eventsGroup = CreateEventsGroupDescriptorItem( type );
		if( eventsGroup == nullptr )
			return TimelineInvalidId;

		for( const TimelineId eventId : events )
		{
			eventsGroup->AddEvent( eventId );
		}
		m_descriptor->m_eventsGroups.Add( eventsGroup );

		eventsGroup->OnItemCreated();

		OnEventsGroupCreated( eventsGroup );

		for( ITimeLineListener* listener : m_timelineListeners )
		{
			listener->OnEventsGroupCreated( eventsGroup->GetId() );
		}

		for( const TimelineId eventId : eventsGroup->GetEvents() )
		{
			OnItemDescriptionPropertyChange( eventId, rtti::AccessPath{ s_eventsGroupIdsPropertyName } );
		}

		MarkModified();

		return eventsGroup->GetId();
	}

	THandle< TimeLineEventsGroupDescription > TimeLineBase::GetEventsGroupDescription( TimelineId eventsGroupId ) const
	{
		return HandleFromPtr( m_descriptor->m_eventsGroups.Find< TimeLineEventsGroupDescription >( eventsGroupId ) );
	}

	void TimeLineBase::RemoveEventsGroup( TimelineId eventsGroupId )
	{
		THandle< TimeLineEventsGroupItem > eventsGroup = GetEventsGroup( eventsGroupId );
		if( eventsGroup == nullptr )
			return;

		if( eventsGroup->GetEvents().Size() > 0 ) // undo will not delete events first
		{
			RemFromEventsGroup( eventsGroup->GetId(), eventsGroup->GetEvents() );
		}
		eventsGroup->OnItemRemoved();
		m_descriptor->m_eventsGroups.Remove( eventsGroupId );

		OnEventsGroupRemoved( eventsGroup );
		for( ITimeLineListener* listener : m_timelineListeners )
		{
			listener->OnEventsGroupDeleted( eventsGroupId );
		}

		MarkModified();
	}

	Bool TimeLineBase::CanRemoveEventsGroup( TimelineId eventsGroupId ) const
	{
		return static_cast< Bool >( GetEventsGroup( eventsGroupId ) );
	}

	Bool TimeLineBase::RestoreEventsGroup( const THandle< TimeLineEventsGroupDescription >& eventsGroup )
	{
		THandle< TimeLineEventsGroupItem > eventsGroupItem = Cast< TimeLineEventsGroupItem >( eventsGroup );

		m_descriptor->m_eventsGroups.Add( eventsGroupItem );
		eventsGroupItem->SetTimeline( this );

		if( eventsGroupItem->Restore() )
		{
			OnEventsGroupRestored( eventsGroupItem );

			for( ITimeLineListener* listener : m_timelineListeners )
			{
				listener->OnEventsGroupCreated( eventsGroupItem->GetId() );
			}

			for( const TimelineId eventId : eventsGroupItem->GetEvents() )
			{
				OnItemDescriptionPropertyChange( eventId, rtti::AccessPath{ s_eventsGroupIdsPropertyName } );
			}

			MarkModified();

			return true;
		}
		else
		{
			eventsGroupItem->SetTimeline( nullptr );
			m_descriptor->m_eventsGroups.Remove( eventsGroupItem->GetId() );
		}

		return false;
	}

	void TimeLineBase::AddToEventsGroup( TimelineId eventsGroupId, const red::DynArray< TimelineId >& events )
	{
		THandle< TimeLineEventsGroupItem > eventsGroup = Cast< TimeLineEventsGroupItem >( GetEventsGroupDescription( eventsGroupId ) );
		for( const TimelineId eventId : events )
		{
			eventsGroup->AddEvent( eventId );
			eventsGroup->OnEventAdded( eventId );
		}

		for( const TimelineId eventId : eventsGroup->GetEvents() )
		{
			OnItemDescriptionPropertyChange( eventId, rtti::AccessPath{ s_eventsGroupIdsPropertyName } );
		}

		MarkModified();
	}

	void TimeLineBase::RemFromEventsGroup( TimelineId eventsGroupId, const red::DynArray< TimelineId >& events )
	{
		TimeLineEventsGroupItem& eventsGroup = *SafeCast< TimeLineEventsGroupItem >( GetEventsGroupDescription( eventsGroupId ) );

		for( const TimelineId eventId : events )
		{
			if( !eventsGroup.GetEvents().Exist( eventId ) )
			{
				continue;
			}

			THandle< TimeLineEventDescription > event = GetEventDescription( eventId );
			eventsGroup.RemoveEvent( eventId );
			eventsGroup.OnEventRemoved( eventId );

			OnItemDescriptionPropertyChange( eventId, rtti::AccessPath{ s_eventsGroupIdsPropertyName } );

			MarkModified();
		}
	}

	void TimeLineBase::SetSmartTrackSelection( Bool enable )
	{
		m_smartTrackSelection = enable;
	}
	
	Bool TimeLineBase::IsSmartTrackSelectionEnabled() const
	{
		return m_smartTrackSelection;
	}
}