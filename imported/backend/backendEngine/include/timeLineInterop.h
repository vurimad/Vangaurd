/**
* Copyright (c) 2016-2020 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "objectHandler.h"
#include "timeLine.h"

#include "../../../common/redReflection/include/serializableValueNotifier.h"

namespace proxy
{
	class TimelineData;
}

namespace tools
{
	class IResourceEditor;
	class TimelineBaseInterop;
	class IEditActionHistory;
	class ResourceBackend;

	struct BACKEND_ENGINE_API TimeLineInteropContext
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

		TimeLineInteropContext();
		TimeLineInteropContext( IResourceEditor* editor );

		IEditActionHistory* m_editHistory;
		ResourceBackend* m_resourceBackend;
	};

	// same enum as in frontend
	enum class CommandType : Uint32
	{
		CT_Timeline,
		CT_Event,
		CT_Track,
		CT_Sector,
		CT_EventsGroup
	};

	class BACKEND_ENGINE_API TimeLineInterop : public ITimeLineListener
	{
		friend TimelineBaseInterop;
		DECLARE_INTEROP( TimeLineInterop );
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:

		TimeLineInterop( const TimeLineInteropContext& timeLineContext, ITimeLine* timeline, ObjectCollection* selectionObject, THandle<ISerializable> userContext = nullptr );
		virtual ~TimeLineInterop();

		ITimeLine* GetTimeLine() { return m_timeLine; }
		ObjectHandler* GetCurrentHandler() { return m_handler; }

		Bool IsSelected( TimelineId eventId ) const;

		///
		/// TimeLineListener implementation
		///
		virtual void OnInvalidateTimeline() override;
		virtual void OnInvalidateTimelineDescriptionProperty( const red::String& propertyName ) override;

		virtual void OnTimelineActiveLayersChanged( const red::DynArray< String >& activeLayers ) override;

		// Generic invalidate Description functions
		virtual void OnInvalidateDescription( TimelineId itemId ) override;
		virtual void OnInvalidateDescription( const red::DynArray< TimelineId >& itemIds ) override;
		virtual void OnInvalidateDescriptionProperty( const red::DynArray< TimelineId >& itemIds, const red::String& propertyName ) override;
		virtual void OnInvalidateDescriptionProperty( TimelineId itemId, const red::String& propertyName ) override;

		/// Events
		virtual void OnEventsSelected( const red::DynArray< TimelineId >& eventIds ) override;
		virtual void OnEventCreated( TimelineId eventId, TimelineId parentTrackId, const red::String& type, Bool includeInLoadingPhase = false ) override;
		virtual void OnEventDeleted( TimelineId eventId ) override;
		virtual void OnEventReparent( TimelineId eventId, TimelineId trackId ) override;
		virtual void OnEventComponentChanged( TimelineId eventId, Uint64 componentId ) override;

		/// Tracks
		virtual void OnTrackCreated( TimelineId trackId, TimelineId parentTrackId, const red::String& type, Bool includeInLoadingPhase = false ) override;
		virtual void OnTrackDeleted( TimelineId trackId, TimelineId parentTrackId ) override;
		virtual void OnTrackReparent( TimelineId trackId, TimelineId newParentId ) override;
		virtual void OnTrackReordered( TimelineId trackId, Uint32 newIndex ) override;

		/// Sectors
		virtual void OnSectorCreated( TimelineId sectorId, const red::String& type, Bool includeInLoadingPhase = false ) override;
		virtual void OnSectorDeleted( TimelineId sectorId ) override;

		/// EventsGroups
		virtual void OnEventsGroupCreated( TimelineId eventsGroupId ) override;
		virtual void OnEventsGroupDeleted( TimelineId eventsGroupId ) override;

		virtual void StepOut() {}

		void ClearSelection();
		void RefreshSelection();
		void ShowEventProperties( const TimelineId id );
		void ToggleLayer( const String& layer );
		void ActivateLayer( const String& layer );
		void DeactivateLayer( const String& layer );

	private:
		typedef Uint32 InstanceId;

		void OnNotifyPropertyChange( const SerializableID& id, const rtti::AccessPath& path );

		void UpdateSelectionCache( const SelectionStatePtr& selection );

		ObjectCollection* GetDataWrapper( TimelineId id );
		void RemoveDataWrapper( TimelineId id );

		void QueueCommand( const String& cmd, Bool includeInLoadingPhase );
		void FlushPendingCommands( red::DynArray< String >& outCommands );
		void GetAvailableLayers( red::DynArray< String >& outLayers ) const;

		Bool IsCommandValid( InstanceId commandInstnace );

		void Send( const CommandType commandType, const String& command, Bool includeInLoadingPhase = false );
		void Send( const CommandType commandType, const AnsiChar* command, const red::DynArray< TimelineId >& ids, Bool includeInLoadingPhase = false );
		void Send( const CommandType commandType, const AnsiChar* command, Bool includeInLoadingPhase = false );
		void Send( const CommandType commandType, const AnsiChar* command, const red::String& additonalParameters, Bool includeInLoadingPhase = false );
		void Send( const CommandType commandType, const AnsiChar* command, TimelineId id, Bool includeInLoadingPhase = false );
		void Send( const CommandType commandType, const AnsiChar* command, TimelineId id, const red::String& additonalParameters, Bool includeInLoadingPhase = false );
		void Send( const CommandType commandType, const AnsiChar* command, TimelineId firstId, TimelineId secondId, Bool includeInLoadingPhase = false );
		void Send( const CommandType commandType, const AnsiChar* command, TimelineId firstId, TimelineId secondId, const red::String& additonalParameters, Bool includeInLoadingPhase = false );

		void SendEventSelectionChange( const red::DynArray< TimelineId >& selection );

		void Fetch( InstanceId cmdInstance, proxy::TimelineData& outTimelineData );

		ITimeLine* m_timeLine;
		IEditActionHistory* m_editHistory;
		red::UniquePtr< IObjectResolver > m_objectResolver;
		ObjectHandler* m_handler;
		THandle< ISerializable > m_userContext;

		InstanceId m_instanceId; // each time after invalidation the counter is bump by one. This enforce that both ui and backend work with the same data

		ObjectCollection* m_selectionDataObjects;
		red::SharedPtr< ObjectCollection > m_selectionDescriptionObjects;
		red::SharedPtr< ObjectCollection > m_descriptorWrapper;

		red::HashMap< TimelineId, red::SharedPtr< ObjectCollection > > m_dataWrappers;

		red::HashMap< SerializableID, TimelineId > m_selectionDataMapping;
		red::DynArray< TimelineId > m_selectedEvents;
		red::DynArray< String > m_pendingCommands;

		interop::ObjectPtr m_tracks;
		interop::ObjectPtr m_events;
		interop::ObjectPtr m_sectors;
		interop::ObjectPtr m_eventsGroups;

		// Notifies about changes of internal state of timeline
		interop::InteropNotifier m_onTimeLineChanged;
		interop::InteropNotifier m_onPendingCommands;
		interop::InteropNotifier m_onLayersChanged;

		SerializableValueNotifier m_serializableNotifier;
		Bool m_isLoading;

		// commands postponed until we finish loading phase
		red::DynArray< String > m_postponedCommands;
	};
}