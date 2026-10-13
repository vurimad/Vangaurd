/**
* Copyright (c) 2016-2020 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../../common/redReflection/include/backendData.h"
#include "../../../common/generalEditorBackend/include/timelineProxy.h"

namespace scnb
{
	class SolutionResave;
} // namespace scnb

namespace tools
{
	typedef Uint64 TimelineId;
	const TimelineId TimelineInvalidId = static_cast< TimelineId >( -1 );

	using TimelineSnapPoint = proxy::TimelineSnapPoint;

	class BACKEND_ENGINE_API TimeLineDescription : public IBackendData
	{
		RTTI_DECLARE_TYPE( TimeLineDescription )
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		TimeLineDescription();
	};

	// Basic supported types. It is possible to add new tracks types.
	struct BACKEND_ENGINE_API TimeLineTracks
	{
		static const String Track;
		static const String TracksGroup;
		static const String TracksRoot;
	};

	enum class BACKEND_ENGINE_API TimelineResizeMode
	{
		StartTime,
		EndTime
	};

	class BACKEND_ENGINE_API TimeLineItemDescription : public IBackendData
	{
		RTTI_DECLARE_TYPE( TimeLineItemDescription )

		// ???
		friend class scnb::SolutionResave;
		// ???
	public:
		TimeLineItemDescription();
		TimeLineItemDescription( TimelineId id, const String& type, const String& visualType );

		TimelineId GetId() const { return m_id; }
		const String& GetType() const { return m_type; }
		void SetType( const String& type ) { m_type = type; }
		const String& GetVisualType() const { return m_visualType; }
		void SetVisualType( const String& visualType ) { m_visualType = visualType; }
		const String& GetCaption() const { return m_caption; }

		void SetCaption( const String& caption ) { m_caption = caption; }
		void SetForegroundColor( const Color& color ) { m_foregroundColor = color; }
		const Color& GetForegroundColor() const { return m_foregroundColor; }
		void SetBackgroundColor( const Color& color ) { m_backgroundColor = color; }
		const Color& GetBackGroundColor() const { return m_backgroundColor; }
		Bool IsMute() const { return m_isMute; }
		virtual void SetMute( Bool isMuted );
		void SetMuteWellbehaved( Bool isMuted );

		Bool Restore() { return OnItemRestored(); }
		Bool Place( TimelineId oldEventId, TimelineId newParentId ) { return OnItemPlace( oldEventId, newParentId ); }

		// Don't use Reset functions at all. They are not exist at all...
		void ResetId( TimelineId id ) { m_id = id; }
		void ResetType( const String& type ) { m_type = type; }

		virtual void OnIntialized() { }

	protected:
		virtual Bool OnItemRestored() { return false; }
		virtual Bool OnItemPlace( TimelineId oldEventId, TimelineId newParentId ) { return false; }

		void InitValues( TimelineId id, const String& type, const String& visualType );
		void ChangeVisualType( const String& newType ) { m_visualType = newType; }

	private:
		TimelineId m_id;

		String m_type;
		String m_visualType;

		String m_caption;
		Color m_foregroundColor;
		Color m_backgroundColor;

		Bool m_isMute;
	};

	class BACKEND_ENGINE_API TimeLineTrackDescription : public TimeLineItemDescription
	{
		RTTI_DECLARE_TYPE( TimeLineTrackDescription )
	public:
		TimeLineTrackDescription();
		TimeLineTrackDescription( TimelineId id, const String& trackType, const String& visualType );

		virtual EditorObjectID GetEditorObjectID() const override;

		TimelineId GetParentId() const { return m_parentId; }
		void SetParentId( TimelineId parentId ) { m_parentId = parentId; }

		Uint32 GetConfiguration() const { return m_configuration; }

		Bool IsSelfVisible() const { return m_isVisible; }
		void SetSelfVisible( Bool visible ) { m_isVisible = visible; }

		Bool IsPinned() const { return m_isPinned; }
		void SetIsPinned( Bool isPinned ) { m_isPinned = isPinned; }

		Bool IsExpanded() const { return m_isExpanded; }
		void SetExpanded( Bool isExpanded ) { m_isExpanded = isExpanded; }

		Bool IsLocked() const { return m_isLocked; }
		void SetLocked( Bool isLocked ) { m_isLocked = isLocked; }

		// Locks track and events modifications
		void SetReadOnlyMode( Bool value );
		// Locks track for modifications
		void SetTrackReadOnlyMode( Bool value );
		// Locks track's events for modifications
		void SetEventsReadOnlyMode( Bool value );

		void SetCanBeRemoved( Bool value );
		void SetCanBeRenamed( Bool value );
		// Enables or disables if we can reparent track
		void SetCanBeReparented( Bool value );
		// Enables or disables possibility to pin/unpin track
		void SetCanBePinned( Bool value );

		void SetCanCreateEvents( Bool value );
		void SetCanDeleteEvents( Bool value );
		void SetCanRenameEvents( Bool value );
		void SetCanModifyEventProperties( Bool value );
		void SetCanResizeEvents( Bool value );
		void SetCanAddEventsByReparenting( Bool value );
		void SetCanRemoveEventsByReparenting( Bool value );
		void SetCanPasteEvents( Bool value );
		void SetCanCopyEvents( Bool value );
		void SetCanGroupEvents(Bool value);

		Bool CanTrackModifyEventProperties() const { return !IsLocked() && ( m_configuration & ConfigurationMasks::CanModifyEventsProperties ) != 0; }
		Bool CanTrackDeleteEvents() const { return !IsLocked() && ( m_configuration & ConfigurationMasks::CanDeleteEvents ) != 0; }

	protected:
		// Track configuration mask (possible actions that can be done by user with this track object)
		// TracksGroups cannot have events by definition. All settings here related to the events will be ignored
		enum ConfigurationMasks : Uint32
		{
			CanBeRemoved = RED_FLAG( 0 ),
			CanBeRenamed = RED_FLAG( 1 ),
			CanBeReparented = RED_FLAG( 2 ),
			CanBePinned = RED_FLAG( 3 ),

			CanModifyTrack = 0x0000ffff,

			// some configuration space

			CanCreateEvents = RED_FLAG( 16 ),	// Right-click -> Create ...
			CanDeleteEvents = RED_FLAG( 17 ),	// Del button, Cut
			CanRenameEvents = RED_FLAG( 18 ),
			CanModifyEventsProperties = RED_FLAG( 19 ),
			CanResizeEvents = RED_FLAG( 20 ),
			CanPasteEvents = RED_FLAG( 21 ),	// Not the same as Create!
			CanCopyEvents = RED_FLAG( 22 ), 
			// CanCutEvents -> We don't need this, we use CanDeleteEvents (if user cuts but doesn't paste is a _hacky_ delete).
			CanAddEventsByReparentin = RED_FLAG( 23 ),
			CanRemoveEventsByReparenting = RED_FLAG( 24 ),
			CanGroupEvents = RED_FLAG( 25 ),

			CanModifyEvents = 0xffff0000,

			CanModifyAll = CanModifyTrack | CanModifyEvents
		};

		void ModifyConfiguration( Bool enable, Uint32 mask );

		TimelineId m_parentId;

		Uint32 m_configuration;	// Policy defined by the programmers, shouldn't be changed by any frontend operation.
		Bool m_isVisible;
		Bool m_isPinned;
		Bool m_isExpanded;
		Bool m_isLocked;
	};

	// Basic supported types
	struct BACKEND_ENGINE_API TimeLineEvents
	{
		static const String DurationEvent;
		static const String PointEvent;
		static const String RangeMarkerEvent;
		static const String PointMarkerEvent;
	};

	// We need to store the ids in a structure because if we try to sync the value of an array using ed::InteropPropertyContainer it will just send the size, not the contents.
	struct TimelineEventGroupIds
	{
		RTTI_DECLARE_TYPE( TimelineEventGroupIds );
		red::DynArray< TimelineId > m_ids{ red::PoolBackend() };
	};

	class BACKEND_ENGINE_API TimeLineEventDescription : public TimeLineItemDescription
	{
		RTTI_DECLARE_TYPE( TimeLineEventDescription )
	public:
		TimeLineEventDescription();
		TimeLineEventDescription( TimelineId id, const String& eventType, const String& visualType );

		virtual EditorObjectID GetEditorObjectID() const override;
		// TODO: remove me
		virtual Bool IsEventVisible() const { return false; }

		Float GetStartTime() const { return m_startTime; }
		Float GetDurationTime() const { return m_durationTime; }
		Float GetEndTime() const { return m_startTime + m_durationTime; }
		const String& GetState() const { return m_state; }

		void SetStartTime( Float time );
		void SetDurationTime( Float time );
		Bool SetState( const String& state );

		void SetIconName( const red::String& name ) { m_iconName = name; }
		const String& GetIconName() const { return m_iconName; }
		virtual void SetMute( Bool isMuted ) override;

		Bool IsLocked() const { return m_isLocked; }
		void SetLocked( const Bool isLocked ) { m_isLocked = isLocked; }

		void OverrideTime( Float startTime, Float durationTime );
		void OverrideDuration( Float durationTime );

		void MoveEventByOffset( Float timeOffset );
		void ResizeEventByOffset( TimelineResizeMode mode, Float timeOffset );

		void BindToSectorId( TimelineId sectorId ) { m_sectorId = sectorId; }
		void BindToTrackId( TimelineId trackId ) { m_trackId = trackId; }
		void BindToEventsGroup( TimelineId id );
		void UnbindFromEventsGroup( TimelineId id );
		void OverrideBoundGroupIds( red::DynArray< TimelineId > newIds );

		TimelineId GetBoundTrackId() const;
		TimelineId GetBoundSectorId() const;
		const red::DynArray< TimelineId >& GetBoundEventsGroupIds() const;
		Bool IsGrouped() const;

		virtual void SyncRuntimeEvent();

	protected:
		Float CorrectTimeValue( Float value );

		String m_iconName;
		String m_state;

		// where this event is bound
		TimelineId m_trackId;
		TimelineId m_sectorId;
		TimelineEventGroupIds m_eventsGroupIds;

		Float m_startTime;
		Float m_durationTime;
		Bool m_isLocked;
	};

	struct BACKEND_ENGINE_API TimeLineSectors
	{
		static const String Sector;
	};

	class BACKEND_ENGINE_API TimeLineSectorDescription : public TimeLineItemDescription
	{
		RTTI_DECLARE_TYPE( TimeLineSectorDescription )
	public:
		TimeLineSectorDescription();
		TimeLineSectorDescription( TimelineId id, const String& type, const String& visualType );

		Float GetStartTime() const { return m_startTime; }
		Float GetDurationTime() const { return m_startTime; }
		Float GetEndTime() const { return GetStartTime() + GetDurationTime(); }

		void SetStartTime( Float time );
		void SetDurationTime( Float time );

	protected:
		virtual void SyncRuntimeEvent() { }

		Float m_startTime;
		Float m_durationTime;
	};

	class BACKEND_ENGINE_API TimeLineEventsGroupDescription: public TimeLineItemDescription
	{
		RTTI_DECLARE_TYPE( TimeLineEventsGroupDescription )
		RED_BASE_CLASS( TimeLineItemDescription )
		RED_USE_MEMORY_POOL( red::PoolBackend );
	public:
		TimeLineEventsGroupDescription(){}
		TimeLineEventsGroupDescription( TimelineId id, const String& type, const String& visualType )
		: TimeLineItemDescription( id, type, visualType)
		{}

		virtual EditorObjectID GetEditorObjectID() const override;
	};
}