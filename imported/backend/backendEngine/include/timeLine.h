/**
* Copyright (c) 2016-2020 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "timeLineTypes.h"

namespace tools
{
	class TimeLineBaseObjectConstructor;
	class TimeLineTrackBaseItem;
	class TimelineItemDataHolder;

	// Capture TimeLine _backend_ state changes
	class ITimeLineListener
	{
	public:
		virtual ~ITimeLineListener() { }

		// UI side will remove all data fetch it again and rebuild the whole timeline!
		// Use this function smart! If you need to invalidate single descriptor property call OnInvalidateDescriptionProperty instead! 
		virtual void OnInvalidateTimeline() { }
		virtual void OnInvalidateTimelineDescriptionProperty( const red::String& propertyName ) { }

		virtual void OnTimelineActiveLayersChanged( const red::DynArray< String >& activeLayers ) { }

		// Generic invalidate Description functions
		virtual void OnInvalidateDescription( TimelineId itemId ) { }                                                                                           // LiveGameEditor, PreviewScenariosManager
		virtual void OnInvalidateDescription( const red::DynArray< TimelineId >& itemIds ) { }                                                                  // LiveGameEditor, PreviewScenariosManager
		virtual void OnInvalidateDescriptionProperty( const red::DynArray< TimelineId >& itemIds, const red::String& propertyName ) { }                         // LiveGameEditor, PreviewScenariosManager
		virtual void OnInvalidateDescriptionProperty( TimelineId itemId, const red::String& propertyName ) { }                                                  // LiveGameEditor, PreviewScenariosManager

		/// Events
		virtual void OnEventsSelected( const red::DynArray< TimelineId >& eventIds ) { }
		virtual void OnEventCreated( TimelineId eventId, TimelineId parentTrackId, const red::String& type, Bool includeInLoadingPhase = false ) { }            // PreviewScenariosManager
		virtual void OnEventDeleted( TimelineId eventId ) { }                                                                                                   // PreviewScenariosManager
		virtual void OnEventReparent( TimelineId eventId, TimelineId newParentId ) { }
		virtual void OnEventComponentChanged( TimelineId eventId, Uint64 componentId ) { }

		/// Tracks
		virtual void OnTrackCreated( TimelineId trackId, TimelineId parentTrackId, const red::String& type, Bool includeInLoadingPhase = false ) { }
		virtual void OnTrackDeleted( TimelineId trackId, TimelineId parentTrackId ) { }
		virtual void OnTrackReparent( TimelineId trackId, TimelineId newParentId ) { }
		virtual void OnTrackReordered( TimelineId trackId, Uint32 newIndex ) { }

		/// Sectors
		virtual void OnSectorCreated( TimelineId sectorId, const red::String& type, Bool includeInLoadingPhase = false ) { }
		virtual void OnSectorDeleted( TimelineId sectorId ) { }

		/// Event groups
		virtual void OnEventsGroupCreated( TimelineId eventsGroupId ) {}
		virtual void OnEventsGroupDeleted( TimelineId eventsGroupId ) {}
	};

	// 
	// An abstract interface for Timeline Control
	//
	// There is TimeLineBase implementation that derives from this interface. It can be reused when you 
	// will start to implement your backend. But you can also derive directly from this interface if you
	// need.
	// 
	// Basic information: Timeline has tracks and events.
	// 
	// * Tracks are organized in a tree like structure. The following object types are supported:
	//
	//   "Track" - a single track that cannot have children. Events can be attached to a Track object
	//   "TracksGroup" - a group of Track objects. 
	//		TrackGroup cannot have another TracksGroup as a child 
	//		Events cannot be attached to TracksGroup.
	// 
	// * Events can be attached to Track and Sectors object. Events can be attached to one track object and 
	//   one sector object at the same time.
	// 		
	//   Now, there are only two types of supported events:
	//   "PointEvent"
	//   "DurationEvent"
	//   
	// * Sectors helps to organize events in parts. Please refer to TimeLine design documentation for 
	//   more information.
	// 
	//	 Supported types:
	//	 "Sector"
	//
	// It is possible to add more tracks and events types when needed

	class ITimeLine
	{
	public:
		virtual ~ITimeLine() { }
		
		virtual void AttachTimeLineListener( ITimeLineListener* listener ) = 0;
		virtual void DetachTimeLineListener( ITimeLineListener* listener ) = 0;

		// Disable or enable all ui inputs to timeline
		virtual void SetEnableControl( Bool enable ) = 0;

		///
		/// TimeLine generic interface
		///

		// Called when UI changed objects selection
		virtual void OnSelectionChanged( red::DynArray< TimelineId > selectedId ) = 0;
		// Called when UI objects deselected
		virtual void OnDeselectionChanged() = 0;
		// Called when UI modified runtime data
		virtual void OnDataItemChanged( TimelineId id, const rtti::AccessPath& path ) = 0;
		// Called when UI modified track data
		virtual void OnTrackDataChanged( TimelineId id, const rtti::AccessPath& path ) = 0;

		// Generic function to get data of item
		virtual THandle< ISerializable > GetItemData( TimelineId id ) const = 0;
		virtual THandle< TimelineItemDataHolder > CreateItemDataholder( TimelineId id ) const  = 0;

		virtual void InvalidateTimeline() = 0;
		virtual void OnInvalidateDescription( TimelineId id ) = 0;
		virtual void OnItemDescriptionPropertyChange( TimelineId id, const rtti::AccessPath& path ) = 0;
		virtual void OnEventComponentChanged( TimelineId id, Uint64 componentId ) = 0;

		///
		/// Tracks interface
		///

		// Returns root of Tracks tree
		virtual TimelineId GetTracksRootId() const = 0;

		virtual void GetAvailableLayers( red::DynArray< String >& layers ) const = 0;
		virtual void ToggleLayer( const String& layer ) = 0;
		virtual void ActivateLayer( const String& layer ) = 0;
		virtual void DeactivateLayer( const String& layer ) = 0;

		// Returns array of all Tracks that TimeLine has.
		virtual void GetTracks( red::DynArray< TimelineId >& tracksIds ) const = 0;
		// Returns ordered array of all Tracks that TimeLine has.
		virtual void GetTracksInOrder( red::DynArray< TimelineId >& tracksIds ) const = 0;

		// Returns user data attached to this Track object. User data can be accessed directly on UI side
		virtual THandle< ISerializable > GetTrackData( TimelineId trackId ) const = 0;
		// Returns track parent
		virtual TimelineId GetTrackParentId( TimelineId trackId ) const = 0;
		// Returns track type
		virtual void GetTrackType( TimelineId trackId, String& type ) const = 0;
		// Not visible track will be not exposed to timeline
		virtual Bool IsTrackVisible( TimelineId trackId ) const = 0;
		// Returns Track Description
		virtual THandle< TimeLineTrackDescription > GetTrackDescription( TimelineId trackId ) const = 0;
		// Fills default event type (event that will be created on double click or ALT + double click in timeline frontend)
		virtual void GetDefaultEventTypes( const TimelineId trackId, String& outType, String& outAlternativeType ) const = 0;
		// Fills array of supported types (Some tracks types can support different Track object types)
		virtual void GetSupportedTrackTypes( TimelineId trackId, red::DynArray< String >& outTypes ) const = 0;
		// Fills array of supported events types (Some tracks types can support different Events object types)
		virtual void GetSupportedEventTypes( TimelineId trackId, red::DynArray< String >& outTypes) const = 0;
		
		// Below, tracks hierarchy management methods:
		// Fills array of attached events to the track. Only "Track" object can have attached events.
		virtual void GetTrackEvents( TimelineId trackId, red::DynArray< TimelineId >& outEvents ) = 0;
		// Fills array of sub tracks
		virtual void GetTrackSubtracks( TimelineId trackId, red::DynArray< TimelineId >& outTracks ) = 0;
		// Reparent Track.
		virtual void ReparentTrack( TimelineId trackId, TimelineId newParentId ) = 0;

		virtual void MoveTrackBefore( TimelineId destinationTrackId, TimelineId sourceTrackId ) = 0;
		virtual void MoveTrackAfter( TimelineId destinationTrackId, TimelineId sourceTrackId ) = 0;

		virtual TimelineId CreateTrack( TimelineId parentTrackId, const String& type, const String& caption, const TimelineId previousId = TimelineInvalidId, const String& visualType = "default" ) = 0;
		virtual void RemoveTrack( TimelineId trackId ) = 0;
		virtual Bool RestoreTrack( const THandle< TimeLineTrackBaseItem >& track ) = 0;

		// Try to moves events to given track
		virtual void ReparentEvents( TimelineId trackId, const red::DynArray< TimelineId >& eventsId ) = 0;

		// Set target track for event paste action. A tiny hack :(
		virtual void SetTargetTrack( TimelineId trackId ) = 0;
		virtual TimelineId GetTargetTrack() const = 0;
		// Target track for paste operation will be selected by an timeline not by user selection
		virtual void SetSmartTrackSelection( Bool enable ) = 0;
		virtual Bool IsSmartTrackSelectionEnabled()  const = 0;

		///
		/// Events interface
		///

		virtual Bool IsEvent( TimelineId itemId ) const = 0;

		// Returns array of all Events that TimeLine has.
		virtual void GetEvents( red::DynArray< TimelineId >& events ) const = 0;

		// Return data attached to the Event
		virtual THandle< ISerializable > GetEventData( TimelineId eventId ) const = 0;
		// Return paths of curves attached to the Event data
		virtual void GetEventCurveData( TimelineId eventId, red::DynArray< String >& data ) const = 0;
		// Return all evaluators path that are present in Event's data
		virtual void GetEventEvaluatorsPath( TimelineId eventId, red::DynArray< String >& evaluators ) const = 0;
		// Return all snap points
		virtual void GetEventSnapPoints( TimelineId eventId, red::DynArray< TimelineSnapPoint >& snapPoints ) const = 0;
		// Refresh snap points
		virtual void RefreshSnapPoints( TimelineId eventId ) = 0;
		// Returns event type
		virtual void GetEventType( TimelineId eventId, String& outType ) const = 0;
		// Not visible event will be not exposed to timeline
		virtual Bool IsEventVisible( TimelineId eventId ) const = 0;
		// Returns events id
		virtual TimelineId GetEventTrack( TimelineId eventId ) const = 0;
		// Returns Event Description
		virtual THandle< TimeLineEventDescription > GetEventDescription( TimelineId eventId ) const = 0;

		// Select events
		virtual void Select( const red::DynArray< TimelineId >& eventIds ) = 0;
		// Create an event and attach it to the track pointed by trackId
		virtual TimelineId CreateTimelineEvent( TimelineId parentTrackId, const String& type, Float initialTime, const String& caption ) = 0;
		// Remove an event from timeline.
		virtual void RemoveEvent( TimelineId eventId ) = 0;
		virtual Bool CanRemoveEvent( TimelineId eventId ) const = 0;
		// Bind/Move an event to a track
		virtual Bool SetEventTrack( TimelineId eventId, TimelineId trackId ) = 0;
		// Bind/Move an event to a sector
		virtual void SetEventSector( TimelineId eventId, TimelineId sectorId ) = 0;
		// Set state of event.
		virtual void SetEventState( const TimelineId id, const String& state ) = 0;

		// reaction to restoring this event by undo/redo
		virtual Bool RestoreEvent( const THandle< TimeLineEventDescription >& event ) = 0;
		// reaction to clipboard action (paste)
		virtual Bool CanPlaceEventObject( const THandle< ISerializable >& object ) const = 0;
		virtual Bool PlaceEvent( const THandle< TimeLineEventDescription >& clone ) = 0;

		virtual void MoveEvent( TimelineId eventId, Float timeOffset ) = 0;
		virtual void ResizeEvent( TimelineId eventId, TimelineResizeMode mode, Float timeOffset ) = 0;
		virtual void ClipEvent( TimelineId eventId, TimelineResizeMode mode, Float timeOffset ) = 0;
		virtual void MuteEvent( TimelineId eventId, const Bool isMuted ) = 0;
		virtual Bool IsEventMuted( TimelineId eventId ) const = 0;
		virtual Float GetEventsEndTime() const = 0;

		///
		/// Events constraints and events groups interface
		///

		virtual void GetEventsGroups( red::DynArray< TimelineId >& eventsGroups ) const = 0;
		virtual void GetEventsGroupType( TimelineId eventsGroupId, String& type ) const = 0;
		virtual TimelineId CreateEventsGroup( const String& type, const red::DynArray< TimelineId >& m_events ) = 0;
		virtual THandle< TimeLineEventsGroupDescription > GetEventsGroupDescription( TimelineId eventId ) const = 0;
		virtual void RemoveEventsGroup( TimelineId eventsGroupId ) = 0;
		virtual Bool CanRemoveEventsGroup( TimelineId eventsGroupId ) const = 0;
		virtual Bool RestoreEventsGroup( const THandle< TimeLineEventsGroupDescription >& event ) = 0;
		virtual void AddToEventsGroup( TimelineId eventsGroupId, const red::DynArray< TimelineId >& events ) = 0;
		virtual void RemFromEventsGroup( TimelineId eventsGroupId, const red::DynArray< TimelineId >& events ) = 0;

		///
		/// Sectors
		///

		virtual void GetSectors( red::DynArray< TimelineId >& sectors ) const = 0;

		// Returns Sector type
		virtual void GetSectorType( TimelineId sectorId, String& outType ) const = 0;
		// Gets runtime data bind to this sector
		virtual THandle< ISerializable > GetSectorData( TimelineId sectorId ) const = 0;
		// Returns Sector Description
		virtual THandle< TimeLineSectorDescription > GetSectorDescription( TimelineId sectorId ) const = 0;

		virtual void GetSectorEvents( TimelineId sectorId, red::DynArray< TimelineId >& outEvents ) = 0;

		// Gets supported sectors items
		virtual void GetSupportedSectorTypes( red::DynArray< String >& outTypes ) const = 0;
		virtual TimelineId CreateSector( const String& type, Float initialPosition, const String& caption ) = 0;
		virtual void RemoveSector( TimelineId sectorId ) = 0;
	};
}