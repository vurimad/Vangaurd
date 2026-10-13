/**
* Copyright (c) 2016-2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "timeLineTypes.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( TimeLineDescription, tools );
	RTTI_PARENT_TYPE( IBackendData );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( TimeLineItemDescription, tools );
	RTTI_PARENT_TYPE( IBackendData );
	RTTI_PROPERTY( m_id ).readOnly();
	RTTI_PROPERTY( m_type ).readOnly();
	RTTI_PROPERTY( m_visualType ).readOnly();
	RTTI_PROPERTY( m_caption ).editable();
	RTTI_PROPERTY( m_foregroundColor ).editable();
	RTTI_PROPERTY( m_backgroundColor).editable();
	RTTI_PROPERTY( m_isMute ).editable();
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( TimeLineTrackDescription, tools );
	RTTI_PARENT_TYPE( TimeLineItemDescription );
	RTTI_PROPERTY( m_parentId ).readOnly();
	RTTI_PROPERTY( m_configuration ).readOnly();
	RTTI_PROPERTY( m_isVisible ).editable();
	RTTI_PROPERTY( m_isPinned );
	RTTI_PROPERTY( m_isExpanded ).notSerialized();
	RTTI_PROPERTY( m_isLocked );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( TimelineEventGroupIds, tools );
	RTTI_PROPERTY( m_ids );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( TimeLineEventDescription, tools );
	RTTI_PARENT_TYPE( TimeLineItemDescription );
	RTTI_PROPERTY( m_trackId ).readOnly();
	RTTI_PROPERTY( m_sectorId ).readOnly();
	RTTI_PROPERTY( m_eventsGroupIds ).readOnly();
	RTTI_PROPERTY( m_iconName ).editable();
	RTTI_PROPERTY( m_startTime ).editable();
	RTTI_PROPERTY( m_durationTime ).editable();
	RTTI_PROPERTY( m_state ).readOnly();
	RTTI_PROPERTY( m_isLocked ).editable();
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( TimeLineSectorDescription, tools );
	RTTI_PARENT_TYPE( TimeLineItemDescription );
	RTTI_PROPERTY( m_startTime ).editable();
	RTTI_PROPERTY( m_durationTime ).editable();
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( TimeLineEventsGroupDescription, tools );
	RTTI_PARENT_TYPE( TimeLineItemDescription );
RTTI_END_TYPE();

namespace tools
{	
	const String TimeLineTracks::Track = "Track";
	const String TimeLineTracks::TracksGroup = "TracksGroup";
	const String TimeLineTracks::TracksRoot = "TracksRoot";

	const String TimeLineEvents::DurationEvent = "DurationEvent";
	const String TimeLineEvents::PointEvent = "PointEvent";
	const String TimeLineEvents::RangeMarkerEvent = "RangeMarkerEvent";
	const String TimeLineEvents::PointMarkerEvent = "PointMarkerEvent";

	const String TimeLineSectors::Sector = "Sector";

	TimeLineDescription::TimeLineDescription()
	{
	}
	
	TimeLineItemDescription::TimeLineItemDescription()
		: m_id( TimelineInvalidId )
		, m_type( String::EMPTY() )
		, m_visualType( String::EMPTY() )
		, m_foregroundColor( Color::WHITE() )
		, m_backgroundColor( Color( 0, 108, 103 ) )
		, m_isMute( false )
	{
	}

	TimeLineItemDescription::TimeLineItemDescription( TimelineId id, const String& type, const String& visualType )
		: m_id( id )
		, m_type( type )
		, m_visualType( visualType )
		, m_foregroundColor( Color::WHITE() )
		, m_backgroundColor( Color( 0, 108, 103 ) )
		, m_isMute( false )
	{
	}

	void TimeLineItemDescription::SetMute( Bool isMuted )
	{
		if ( m_isMute != isMuted )
		{
			m_isMute = isMuted;
			NotifyPropertyChanged( rtti::AccessPath( "isMute" ) );
		}
	}

	// Note: It's well-behaved as it doesn't call neither NotifyPropertyChanged() and SyncRuntimeEvent().
	void TimeLineItemDescription::SetMuteWellbehaved( Bool isMuted )
	{
		m_isMute = isMuted;
	}

	void TimeLineItemDescription::InitValues( TimelineId id, const String& type, const String& visualType )
	{
		m_id = id;
		m_type = type;
		m_visualType = visualType;
	}

	///
	///
	///

	TimeLineTrackDescription::TimeLineTrackDescription() 
		: m_parentId( TimelineInvalidId )
		, m_configuration( ConfigurationMasks::CanModifyAll )
		, m_isVisible( true )
		, m_isPinned( false )
		, m_isExpanded( true )
	{
	}

	TimeLineTrackDescription::TimeLineTrackDescription( TimelineId id, const String& type, const String& visualType )
		: TimeLineItemDescription( id, type, visualType )
		, m_parentId( TimelineInvalidId )
		, m_configuration( ConfigurationMasks::CanModifyAll )
		, m_isVisible( true )
		, m_isPinned( false )
		, m_isExpanded( true )
	{
	}

	EditorObjectID TimeLineTrackDescription::GetEditorObjectID() const
	{
		static CName kind = RED_NAME_CONSTEXPR( "TimeLineTrackDescription" );
		return EditorObjectID::Build( GetId(), kind );
	}

	void TimeLineTrackDescription::SetReadOnlyMode( Bool readOnly )
	{
		ModifyConfiguration( !readOnly, ConfigurationMasks::CanModifyAll );
	}

	void TimeLineTrackDescription::SetTrackReadOnlyMode( Bool readOnly )
	{
		ModifyConfiguration( !readOnly, ConfigurationMasks::CanModifyTrack );
	}

	void TimeLineTrackDescription::SetEventsReadOnlyMode( Bool readOnly )
	{
		ModifyConfiguration( !readOnly, ConfigurationMasks::CanModifyEvents );
	}

	void TimeLineTrackDescription::SetCanBeRemoved( Bool value )
	{
		ModifyConfiguration( value, ConfigurationMasks::CanBeRemoved );
	}

	void TimeLineTrackDescription::SetCanBeRenamed( Bool value )
	{
		ModifyConfiguration( value, ConfigurationMasks::CanBeRenamed );
	}

	void TimeLineTrackDescription::SetCanBeReparented( Bool value )
	{
		ModifyConfiguration( value, ConfigurationMasks::CanBeReparented );
	}

	void TimeLineTrackDescription::SetCanBePinned( Bool value )
	{
		ModifyConfiguration( value, ConfigurationMasks::CanBePinned );
	}
	
	void TimeLineTrackDescription::SetCanCreateEvents( Bool value )
	{
		ModifyConfiguration( value, ConfigurationMasks::CanCreateEvents );
	}

	void TimeLineTrackDescription::SetCanDeleteEvents( Bool value )
	{
		ModifyConfiguration( value, ConfigurationMasks::CanDeleteEvents );
	}

	void TimeLineTrackDescription::SetCanRenameEvents( Bool value )
	{
		ModifyConfiguration( value, ConfigurationMasks::CanRenameEvents );
	}

	void TimeLineTrackDescription::SetCanModifyEventProperties( Bool value )
	{
		ModifyConfiguration( value, ConfigurationMasks::CanModifyEventsProperties );
	}

	void TimeLineTrackDescription::SetCanResizeEvents( Bool value )
	{
		ModifyConfiguration( value, ConfigurationMasks::CanResizeEvents );
	}

	void TimeLineTrackDescription::SetCanAddEventsByReparenting( Bool value )
	{
		ModifyConfiguration( value, ConfigurationMasks::CanAddEventsByReparentin );
	}

	void TimeLineTrackDescription::SetCanRemoveEventsByReparenting( Bool value )
	{
		ModifyConfiguration( value, ConfigurationMasks::CanRemoveEventsByReparenting );
	}

	void TimeLineTrackDescription::SetCanPasteEvents( Bool value )
	{
		ModifyConfiguration( value, ConfigurationMasks::CanPasteEvents );
	}

	void TimeLineTrackDescription::SetCanCopyEvents( Bool value )
	{
		ModifyConfiguration( value, ConfigurationMasks::CanCopyEvents );
	}

	void TimeLineTrackDescription::SetCanGroupEvents(Bool value)
	{
		ModifyConfiguration( value, ConfigurationMasks::CanGroupEvents );
	}

	void TimeLineTrackDescription::ModifyConfiguration( Bool enable, Uint32 mask )
	{
		if( enable )
		{
			m_configuration |= mask;
		}
		else
		{
			m_configuration &= ~mask;
		}
	}

	///
	///
	///

	TimeLineEventDescription::TimeLineEventDescription()
		: m_startTime( 0 )
		, m_durationTime( 10 )
		, m_trackId( TimelineInvalidId )
		, m_sectorId( TimelineInvalidId )
		, m_eventsGroupIds()
		, m_isLocked{ false }
	{
	}

	TimeLineEventDescription::TimeLineEventDescription( TimelineId id, const String& type, const String& visualType )
		: TimeLineItemDescription( id, type, visualType )
		, m_startTime( 0 )
		, m_durationTime( 10 )
		, m_trackId( TimelineInvalidId )
		, m_sectorId( TimelineInvalidId )
		, m_eventsGroupIds()
		, m_isLocked{ false }
	{
	}

	EditorObjectID TimeLineEventDescription::GetEditorObjectID() const
	{
		static CName kind = RED_NAME_CONSTEXPR( "TimeLineEventDescription" );
		return EditorObjectID::Build( GetId(), kind );
	}

	void TimeLineEventDescription::SetStartTime( Float time )
	{
		if ( m_startTime != time )
		{
			m_startTime = CorrectTimeValue( time );
			NotifyPropertyChanged( rtti::AccessPath( "startTime" ) );

			SyncRuntimeEvent();
		}
	}

	void TimeLineEventDescription::MoveEventByOffset( Float timeOffset )
	{
		if ( timeOffset != 0 )
		{
			SetStartTime( m_startTime + timeOffset );
		}
	}

	void TimeLineEventDescription::ResizeEventByOffset( TimelineResizeMode mode, Float timeOffset )
	{
		if ( timeOffset != 0 )
		{
			if ( mode == TimelineResizeMode::StartTime )
			{
				Float newDuration = GetDurationTime() + timeOffset;
				if ( newDuration >= 0.0f )
				{
					Float startTime = GetStartTime() - timeOffset;

					if ( startTime < 0 )
					{
						startTime = 0;
						newDuration = GetDurationTime() + GetStartTime();
					}

					SetStartTime( startTime );
					SetDurationTime( newDuration );
				}
			}
			else
			{
				Float newDuration = GetDurationTime() - timeOffset;
				if ( newDuration <= 0.0 )
				{
					SetDurationTime( 0.0f );
				}
				else
				{
					Float durationTime = GetDurationTime();
					SetDurationTime( durationTime - timeOffset );
				}
			}
		}
	}

	void TimeLineEventDescription::BindToEventsGroup( TimelineId id )
	{
		RED_ASSERT( !m_eventsGroupIds.m_ids.Exist( id ) );
		m_eventsGroupIds.m_ids.EmplaceBack( id );
	}
	
	void TimeLineEventDescription::UnbindFromEventsGroup( TimelineId id )
	{
		m_eventsGroupIds.m_ids.Remove( id );
	}

	void TimeLineEventDescription::OverrideBoundGroupIds( red::DynArray< TimelineId > newIds )
	{
		m_eventsGroupIds.m_ids = std::move( newIds );
	}

	tools::TimelineId TimeLineEventDescription::GetBoundTrackId() const
	{
		return m_trackId;
	}

	tools::TimelineId TimeLineEventDescription::GetBoundSectorId() const
	{
		return m_sectorId;
	}

	const red::DynArray< TimelineId >& TimeLineEventDescription::GetBoundEventsGroupIds() const
	{
		return m_eventsGroupIds.m_ids;
	}

	Bool TimeLineEventDescription::IsGrouped() const
	{
		return !m_eventsGroupIds.m_ids.Empty();
	}

	void TimeLineEventDescription::SyncRuntimeEvent()
	{
	}

	void TimeLineEventDescription::SetDurationTime( Float time )
	{
		if ( m_durationTime != time )
		{
			m_durationTime = CorrectTimeValue( time );

			NotifyPropertyChanged( rtti::AccessPath( "durationTime" ) );

			SyncRuntimeEvent();
		}
	}

	Bool TimeLineEventDescription::SetState( const String& state )
	{
		if ( m_state != state )
		{
			m_state = state;
			NotifyPropertyChanged( rtti::AccessPath( "state" ) );
			SyncRuntimeEvent();
			return true;
		}
		return false;
	}

	void TimeLineEventDescription::SetMute( Bool isMuted )
	{
		TimeLineItemDescription::SetMute( isMuted );
		SyncRuntimeEvent();
	}

	void TimeLineEventDescription::OverrideTime( Float startTime, Float durationTime )
	{
		m_startTime = CorrectTimeValue( startTime );
		m_durationTime = CorrectTimeValue( durationTime );
	}

	void TimeLineEventDescription::OverrideDuration( Float durationTime )
	{
		m_durationTime = CorrectTimeValue( durationTime );
	}

	Float TimeLineEventDescription::CorrectTimeValue( Float value )
	{
		if ( value < 0.0001 )
		{
			value = 0;
		}

		return value;
	}
	///
	///
	///

	TimeLineSectorDescription::TimeLineSectorDescription()
		: m_startTime( 0 )
		, m_durationTime( 1 )
	{
	}

	TimeLineSectorDescription::TimeLineSectorDescription( TimelineId id, const String& type, const String& visualType )
		: TimeLineItemDescription( id, type, visualType )
		, m_startTime( 0 )
		, m_durationTime( 1 )
	{
	}

	void TimeLineSectorDescription::SetStartTime( Float time )
	{
		if ( m_startTime != time )
		{
			m_startTime = time;
			SyncRuntimeEvent();
		}
	}
	
	void TimeLineSectorDescription::SetDurationTime( Float time )
	{
		if ( m_durationTime != time )
		{
			m_durationTime = time;
			SyncRuntimeEvent();
		}
	}

	EditorObjectID TimeLineEventsGroupDescription::GetEditorObjectID() const
	{
		static CName kind = RED_NAME_CONSTEXPR( "TimeLineEventsGroupDescription" );
		return EditorObjectID::Build( GetId(), kind );
	}
}
