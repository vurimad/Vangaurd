/**
* Copyright (c) 2016-2020 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "timeLine.h"
#include "timeLineItemCollection.h"
#include "timeLineBaseItem.h"
#include "../../../common/engine/include/ruidGenerator.h"

// #scnb_todo_szi Remove this.
namespace scnb
{
	class Timeline;
}

namespace tools
{
	class TimeLineBase;

	class BACKEND_ENGINE_API TimeLineBaseDescriptor : public TimeLineDescription
	{
		RTTI_DECLARE_TYPE( TimeLineBaseDescriptor )

		friend TimeLineBase;
		friend scnb::Timeline; // #scnb_todo_szi Remove this.

	public:
		TimeLineBaseDescriptor();
		TimeLineBaseDescriptor( const THandle< CResource >& ownerResource );

		virtual void Initialize( TimeLineBase* timeline );
		virtual Bool OnPropertyMissing( CName propertyName, const rtti::Variant& readValue ) override;

		TimelineId GetRootTrackId() const { return m_tracksRootId; }
		TimelineId GenerateTimelineId() { return m_lastUsedId++; }

		const THandle< CResource >& GetParentResource() const { return m_parentResource; }
		void SetParentResource( const THandle< CResource >& parentResource ) { m_parentResource = parentResource; }

		template < typename T>
		T* GetTrack( TimelineId id ) const
		{
			return m_tracks.Find< T >( id );
		}

		template < typename T>
		T* GetEvent( TimelineId id ) const
		{
			return m_events.Find< T >( id );
		}

		const red::ArraySpan< const THandle< tools::TimeLineItemDescription > > GetEvents() const { return m_events.GetItems(); }
		const red::ArraySpan< const THandle< tools::TimeLineItemDescription > > GetTracks() const { return m_tracks.GetItems(); }

	protected:
		TimeLineItemCollection m_tracks;
		TimeLineItemCollection m_events;
		TimeLineItemCollection m_sectors;
		TimeLineItemCollection m_eventsGroups;

		TimelineId m_lastUsedId;
		TimelineId m_tracksRootId;

		THandle< CResource > m_parentResource;
	};

	//
	// Basic implementation of Timeline interface. It fully runtime implementation. It is not serializable
	// object by design! Currently, there is no use case for fully serializable timeline.
	//
	// Features:
	// - It manages struct of tracks and events
	// - Returns nullptr for runtime data.
	// 
	// All timeline items (description) are ISerializable so you can handle serialization in derived class or
	// in your custom implementation of ITimeLine.
	//

	class TrackDescriptor;

	class BACKEND_ENGINE_API TimelineItemDataHolder : public IBackendData
	{
		RTTI_DECLARE_TYPE( TimelineItemDataHolder );
		RED_USE_MEMORY_POOL( red::PoolBackend );
	public:
		TimelineItemDataHolder() = default;
		TimelineItemDataHolder( const THandle< ISerializable >& item, const THandle< TimeLineTrackDescription >& parentTrack, const red::DynArray< Uint64 >& groups = {} )
			: m_item( item )
			, m_parentTrack( parentTrack )
			, m_groups( groups )
		{
		}

		ISerializable* GetItem() { return m_item.Get(); }
		THandle< ISerializable > GetItemHandle() const { return m_item; }

		THandle< TimeLineTrackDescription > GetParentHandle() const { return m_parentTrack; }

		virtual tools::EditorObjectID GetEditorObjectID() const override
		{
			return m_item ? m_item->GetEditorObjectID() : ISerializable::GetEditorObjectID();
		}

		const red::DynArray< Uint64 >& GetGroups() { return m_groups; }

		Bool IsGrouped() const { return !m_groups.Empty(); }

	private:
		THandle< ISerializable > m_item = nullptr;
		THandle< TimeLineTrackDescription > m_parentTrack = nullptr;
		red::DynArray< Uint64 > m_groups;
	};

	class BACKEND_ENGINE_API TimeLineBase : public ITimeLine, red::NonCopyable
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
	public:
		TimeLineBase( THandle< TimeLineBaseDescriptor > descriptor );

		virtual Bool Initialize();

		virtual void AttachTimeLineListener( ITimeLineListener* listener ) override;
		virtual void DetachTimeLineListener( ITimeLineListener* listener ) override;

		// Remove timeline content
		void ClearTimeline();
		// Enable/Disable inputs on ui side
		virtual void SetEnableControl( Bool enable ) override;
		// Rebinds all events to tracks (Resave code)
		void RebindEvents();

		void MarkModified();

		///
		/// TimeLine generic interface
		///

		// Called when UI changed objects selection
		virtual void OnSelectionChanged( red::DynArray< TimelineId > selectedId ) override { }
		// Called when UI objects deselected
		virtual void OnDeselectionChanged() override { }
		// Called when UI modified runtime data
		virtual void OnDataItemChanged( TimelineId id, const rtti::AccessPath& path ) override;
		// Called when UI modified track data
		virtual void OnTrackDataChanged( TimelineId id, const rtti::AccessPath& path ) override {}

		virtual THandle< ISerializable > GetItemData( TimelineId id ) const override;
		virtual THandle< TimelineItemDataHolder > CreateItemDataholder( TimelineId id ) const override;

		virtual void InvalidateTimeline() override;
		virtual void OnInvalidateDescription( TimelineId id ) override;
		virtual void OnItemDescriptionPropertyChange( TimelineId id, const rtti::AccessPath& path ) override;
		virtual void OnEventComponentChanged( TimelineId id, Uint64 componentId ) override;

		///
		/// Tracks interface
		///

		// Returns root of Tracks tree
		virtual TimelineId GetTracksRootId() const override { return m_descriptor->m_tracksRootId; }
		virtual void GetAvailableLayers( red::DynArray< String >& layers ) const override {}
		virtual void ToggleLayer( const String& layer ) override;
		virtual void ActivateLayer( const String& layer ) override;
		virtual void DeactivateLayer( const String& layer ) override;
		// Returns unordered array of all Tracks that TimeLine has.
		virtual void GetTracks( red::DynArray< TimelineId >& tracksIds ) const override { m_descriptor->m_tracks.GetItems( tracksIds ); }
		// Returns ordered array of all Tracks that TimeLine has.
		virtual void GetTracksInOrder( red::DynArray< TimelineId >& tracksIds ) const override;
		// Returns user data attached to this Track object. User data can be accessed directly on UI side
		virtual THandle< ISerializable > GetTrackData( TimelineId trackId ) const override;
		// Returns track parent
		virtual TimelineId GetTrackParentId( TimelineId trackId ) const override;
		// Returns track type
		virtual void GetTrackType( TimelineId trackId, String& outType ) const override;
		// Inactive track will be not visible on timeline
		virtual Bool IsTrackVisible( TimelineId trackId ) const override;
		// Returns Track Description
		virtual THandle< TimeLineTrackDescription > GetTrackDescription( TimelineId trackId ) const override;
		// Fills default event type (event that will be created on double click or ALT + double click in timeline frontend)
		virtual void GetDefaultEventTypes( const TimelineId trackId, String& outType, String& outAlternativeType ) const override;
		// Fills array of supported types (Some tracks types can support different Track object types)
		virtual void GetSupportedTrackTypes( TimelineId trackId, red::DynArray< String >& outTypes ) const override;
		// Fills array of supported events types (Some tracks types can suppTimeLineEventsGroupItemrt different Events object types)
		virtual void GetSupportedEventTypes( TimelineId trackId, red::DynArray< String >& outTypes) const override;
		
		// Below, tracks hierarchy management methods:
		// Fills array of attached events to the track. Only "Track" object can have attached events.
		virtual void GetTrackEvents( TimelineId trackId, red::DynArray< TimelineId >& outEvents ) override;
		// Fills array of sub tracksTimeLineEventsGroupItem
		virtual void GetTrackSubtracks( TimelineId trackId, red::DynArray< TimelineId >& outTracks ) override;
		// Reparent Track.
		virtual void ReparentTrack( TimelineId trackId, TimelineId newParentId ) override;

		virtual TimelineId CreateTrack( TimelineId parentTrackId, const String& type, const String& caption, const TimelineId previousId = TimelineInvalidId, const String& visualType = "default" ) override;
		virtual void RemoveTrack( TimelineId trackId ) override;
		virtual Bool RestoreTrack( const THandle< TimeLineTrackBaseItem >& track ) override;

		virtual void ReparentEvents( TimelineId trackId, const red::DynArray< TimelineId >& eventsId ) override;
		virtual void MoveTrackBefore( TimelineId trackId, TimelineId destinationTrackId ) override;
		virtual void MoveTrackAfter( TimelineId trackId, TimelineId destinationTrackId ) override;

		virtual void SetTargetTrack( TimelineId trackId ) override { m_targetTrack = trackId; }
		virtual TimelineId GetTargetTrack() const override { return m_targetTrack; }
		virtual void SetSmartTrackSelection( Bool enable ) override;
		virtual Bool IsSmartTrackSelectionEnabled()  const override;

		THandle< TimeLineTrackBaseItem > GetTrack( TimelineId trackId ) const { return HandleFromPtr( m_descriptor->m_tracks.Find< TimeLineTrackBaseItem >( trackId ) ); }
		THandle< TimeLineEventsGroupItem > GetEventsGroup( TimelineId id ) const { return HandleFromPtr( m_descriptor->m_eventsGroups.Find< TimeLineEventsGroupItem >( id ) ); }
		THandle< TimeLineEventItem > GetEvent( TimelineId id ) const { return HandleFromPtr( m_descriptor->m_events.Find< TimeLineEventItem >( id ) ); }
		THandle< TimeLineSectorItem > GetSector( TimelineId id ) const { return HandleFromPtr( m_descriptor->m_sectors.Find< TimeLineSectorItem >( id ) ); }

		///
		/// Events interface
		///

		virtual Bool IsEvent( TimelineId itemId ) const override;

		// Returns array of all Events that TimeLine has.
		virtual void GetEvents( red::DynArray< TimelineId >& events ) const override { m_descriptor->m_events.GetItems( events ); }
		// Return data attached to the Event
		virtual THandle< ISerializable > GetEventData( TimelineId eventId ) const override;
		virtual void GetEventCurveData( TimelineId eventId, red::DynArray< String >& data ) const override;
		virtual void GetEventEvaluatorsPath( TimelineId eventId, red::DynArray< String >& evaluators ) const override;
		virtual void GetEventSnapPoints( TimelineId eventId, red::DynArray< TimelineSnapPoint >& snapPoints ) const override;
		virtual void RefreshSnapPoints( TimelineId eventId ) override;
		// Returns event type
		virtual void GetEventType( TimelineId eventId, String& type ) const override;
		// Inactive event will be not visible on timeline
		virtual Bool IsEventVisible( TimelineId eventId ) const override;
		// Returns event's track id
		virtual TimelineId GetEventTrack( TimelineId eventId ) const override;
		// Returns Event Description
		virtual THandle< TimeLineEventDescription > GetEventDescription( TimelineId eventId ) const override;

		// Select events
		virtual void Select( const red::DynArray< TimelineId >& eventIds ) override;
		// Create an event and attach it to the track pointed by trackId
		virtual TimelineId CreateTimelineEvent( TimelineId parentTrackId, const String& type, Float initialTime, const String& caption ) override;
		// Remove an event from timeline.
		virtual void RemoveEvent( TimelineId eventId ) override;
		virtual Bool CanRemoveEvent( TimelineId eventId ) const override;
		// Bind/Move an event to a track
		virtual Bool SetEventTrack( TimelineId eventId, TimelineId trackId ) override;
		// Bind/Move an event to a sector
		virtual void SetEventSector( TimelineId eventId, TimelineId sectorId ) override;
		// Set state of event.
		void SetEventState( TimelineId eventId, const String& state ) override;

		// reaction to restoring this event by undo/redo
		virtual Bool RestoreEvent( const THandle< TimeLineEventDescription >& event ) override;
		// reaction to clipboard action (paste)
		virtual Bool CanPlaceEventObject( const THandle< ISerializable >& object ) const override;
		virtual Bool PlaceEvent( const THandle< TimeLineEventDescription >& clone ) override;

		virtual void MoveEvent( TimelineId eventId, Float timeOffset ) override;
		virtual void ResizeEvent( TimelineId eventId, TimelineResizeMode mode, Float timeOffset ) override;
		virtual void ClipEvent( TimelineId eventId, TimelineResizeMode mode, Float timeOffset ) override;
		virtual void MuteEvent( TimelineId eventId, const Bool isMuted ) override;
		virtual Bool IsEventMuted( TimelineId eventId ) const override;
		virtual Float GetEventsEndTime() const override;

		///
		/// Events constraints and events groups interface
		///

		virtual void GetEventsGroups( red::DynArray< TimelineId >& eventsGroups ) const override;
		virtual void GetEventsGroupType( TimelineId eventsGroupId, String& type ) const override;
		virtual TimelineId CreateEventsGroup( const String& type, const red::DynArray< TimelineId >& events ) override;
		virtual THandle< TimeLineEventsGroupDescription > GetEventsGroupDescription( TimelineId eventsGroupId ) const override;
		virtual void RemoveEventsGroup( TimelineId eventsGroupId ) override;
		virtual Bool CanRemoveEventsGroup( TimelineId eventsGroupId ) const override;
		virtual Bool RestoreEventsGroup( const THandle< TimeLineEventsGroupDescription >& eventsGroup ) override;
		virtual void AddToEventsGroup( TimelineId eventsGroupId, const red::DynArray< TimelineId >& events ) override;
		virtual void RemFromEventsGroup( TimelineId eventsGroupId, const red::DynArray< TimelineId >& events ) override;

		///
		/// Sectors
		///

		virtual void GetSectors( red::DynArray< TimelineId >& sectors ) const override { m_descriptor->m_sectors.GetItems( sectors ); }
		virtual void GetSectorEvents( TimelineId sectorId, red::DynArray< TimelineId >& outEvents ) override;

		// Returns Sector type
		virtual void GetSectorType( TimelineId sectorId, String& outType ) const override;
		virtual THandle< ISerializable > GetSectorData( TimelineId sectorId ) const override { return nullptr; }
		// Returns Sector Description
		virtual THandle< TimeLineSectorDescription > GetSectorDescription( TimelineId sectorId ) const override;

		virtual void GetSupportedSectorTypes( red::DynArray< String >& outTypes ) const override;
		virtual TimelineId CreateSector( const String& type, Float initialPosition, const String& caption ) override;
		virtual void RemoveSector( TimelineId sectorId ) override;

		void CreateRootTrack();

		// Internal function to create a track without of checking supporting type
		virtual TimelineId InternalCreateTrack( TimelineId parentTrackId, const String& type, const String& caption, const TimelineId previousId = TimelineInvalidId, const String& visualType = "default" );

	protected:
		virtual THandle< TimeLineTrackBaseItem > CreateTrackDescriptorItem( const String& type, const String& visualType = "default" );
		virtual THandle< TimeLineSectorItem > CreateSectorItem( const String& type );
		virtual THandle< TimeLineEventItem > CreateEventDescriptorItem( const String& type );
		virtual THandle< TimeLineEventsGroupItem > CreateEventsGroupDescriptorItem( const String& type );

		virtual void OnTimelineCleared(); // done, removed

		virtual void OnEventCreated( THandle< TimeLineEventItem > item ) { }
		virtual void OnEventRemoved( THandle< TimeLineEventItem > item ) { }
		virtual void OnEventRestored( THandle< TimeLineEventItem > item ) { }
		virtual void OnSectorCreated( THandle< TimeLineSectorItem > item ) { }
		virtual void OnEventsGroupCreated( THandle< TimeLineEventsGroupItem > item ) { }
		virtual void OnEventsGroupRemoved( THandle< TimeLineEventsGroupItem > item ) { }
		virtual void OnEventsGroupRestored( THandle< TimeLineEventsGroupItem > item ) { }
		void OnLayersChanged(); // done, obsoleted

		virtual void ResetEventId( THandle< TimeLineEventItem > item );

		// A validation function when a timeline try to move events between tracks
		virtual Bool CanEventBindToTrack( TimelineId eventId, TimelineId destinationTrackId ) const { return true; }

		TimelineId GenerateTimelineId() const { return m_descriptor->GenerateTimelineId(); }

		// For situation when you create an event on backend side and you need put it into the timeline
		Bool RegisterEvent( THandle< TimeLineEventItem > event );
		Bool RegisterTrack( THandle< TimeLineTrackBaseItem > track );
		Bool RegisterEventsGroup( THandle< TimeLineEventsGroupItem > eventsGroup );

		void SetEventTrack( THandle< TimeLineEventItem > event, TimelineId trackId );
		void RemoveEventFromTrack( TimelineId trackId, TimelineId eventId );
		void AddEventToTrack( TimeLineEventItem& event, TimelineId trackId );

		THandle< TimeLineBaseDescriptor > m_descriptor;
		red::DynArray< ITimeLineListener* > m_timelineListeners;

	private:
		red::DynArray< String > m_activeLayers;
		RUIDGenerator m_ruidGenerator;

		TimelineId m_targetTrack;

		Bool m_smartTrackSelection;
	};
}