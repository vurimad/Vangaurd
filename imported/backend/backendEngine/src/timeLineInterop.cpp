/**
* Copyright (c) 2016-2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "timeLineInterop.h"
#include "timeLineBase.h"

#include "resourceBackend.h"
#include "resourceEditor.h"
#include "selectionIterator.h"
#include "timeLineActions.h"
#include "timeLineSelectionHandler.h"
#include "../include/timeLineObjectResolver.h"
#include "../include/interopControlBase.h"

#include "../../../common/interop/include/interopUtilsRTTI.h"


namespace tools
{
	class TimelineBaseInterop : public InteropControlBase
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		TimelineBaseInterop( const AnsiChar* typeName, TimeLineInterop* timelineInterop )
			: InteropControlBase( typeName, timelineInterop->m_selectionDataObjects )
			, m_timeLine( timelineInterop->GetTimeLine() )
			, m_timeLineInterop( timelineInterop )
		{
		}

		ObjectHandler* GetHandler() const { return m_timeLineInterop->GetCurrentHandler(); }
		IEditActionHistory* GetEditHistory() { return m_timeLineInterop->m_editHistory; }

		ObjectCollection* GetSelectedDataObject() { return m_timeLineInterop->m_selectionDataObjects; }
		ObjectCollection* GetSelectedDescriptionObject() { return m_timeLineInterop->m_selectionDescriptionObjects.Get(); }
		ObjectCollection* GetDescriptorWrapper() { return m_timeLineInterop->m_descriptorWrapper.Get(); }
		ObjectCollection* GetDataWrapper( TimelineId id ) { return m_timeLineInterop->GetDataWrapper( id ); }
		void RemoveDataWrapper( TimelineId id ) { m_timeLineInterop->RemoveDataWrapper( id ); }

		THandle<ISerializable> GetUserContext() { return m_timeLineInterop->m_userContext; }

		red::HashMap< SerializableID, TimelineId >& GetSelectionMapping() { return m_timeLineInterop->m_selectionDataMapping; }

		const red::DynArray< TimelineId >& GetSelectedEvents() const { return m_timeLineInterop->m_selectedEvents; }

	protected:
		ITimeLine* m_timeLine;
		TimeLineInterop* m_timeLineInterop;
	};

	struct EventsGroupAddData
	{
		RTTI_DECLARE_TYPE( EventsGroupAddData );
		RED_USE_MEMORY_POOL( red::PoolBackend );

		TimelineId m_eventsGroupId;
		red::DynArray< TimelineId > m_events{ red::PoolBackend() };
	};

	namespace
	{
		template< typename T >
		String ArrayToString( const red::DynArray< T >& array )
		{
			if ( array.Empty() )
				return String::EMPTY();

			red::StringBuilder<String> arrayString;
			for ( const T& element : array )
				arrayString.Appendf( "%s ", ToStringDirect( element ).AsChar() );

			return arrayString.ToString();
		}

		class EventsInterop : public TimelineBaseInterop
		{
			red::DynArray< String > m_supportedEventTypes;

		public:
			EventsInterop( TimeLineInterop* timelineInterop )
				: TimelineBaseInterop( "Events", timelineInterop )
			{
				RegisterFunctor< void, String >( "createEvent", [ this ] ( ::interop::IContext& ctx, const String& data )
				{
					ObjectCreationInfo info( data );
					if ( !GetHandler()->CreateObject( info ) )
						ctx.ThrowException( "Couldn't create item with given parameters" );
				} );

				RegisterFunctor<void, TimelineId>( "removeEvent", [ this ] ( ::interop::IContext& ctx, TimelineId eventId )
				{
					THandle< TimelineItemDataHolder > eventDataholder = m_timeLine->CreateItemDataholder( eventId );

					red::DynArray< EditorObjectIDPath > paths;
					paths.PushBack( EditorObjectIDPath( eventDataholder->GetEditorObjectID() ) );

					if ( !GetHandler()->DeleteObjects( paths ) )
					{
						ctx.ThrowException( "Couldn't delete item with given parameters" );
					}
				} );

				RegisterMap( "description", [ this ] ( interop::IContext& ctx, const String& IdStr ) -> interop::ObjectPtr
				{
					TimelineId id;
					if ( !FromString( IdStr, id ) )
					{
						ctx.ThrowException( "Invalid element id - should be TimelineId" );
					}

					// TODO: Store the last Get result
					m_description = m_timeLine->GetEventDescription( id );
					if ( m_description )
					{
						GetDescriptorWrapper()->SetObject( m_description.Get() );
						return GetDescriptorWrapper()->GetRootInterop();
					}

					ctx.ThrowException( "Failed to create description object" );
					return nullptr;
				} );

				RegisterMap( "data", [ this ] ( interop::IContext& ctx, const String& IdStr ) -> interop::ObjectPtr
				{
					TimelineId id;
					if ( !FromString( IdStr, id ) )
					{
						ctx.ThrowException( "Invalid element id - should be TimelineId" );
					}

					THandle< ISerializable > data = m_timeLine->GetEventData( id );
					if ( data )
					{
						ObjectCollection* collection = GetDataWrapper( id );
						if ( !collection->HasObject( data->GetID() ) )
						{
							collection->SetObject( data.Get(), false, GetUserContext() );
						}
						return collection->GetRootInterop();
					}

					ctx.ThrowException( "Failed to create data object" );
					return nullptr;
				} );

				RegisterFunctor<void, TimelineId, TimelineId>( "setEventTrack", [ this ] ( ::interop::IContext& ctx, TimelineId eventId, TimelineId trackId )
				{
					m_timeLine->SetEventTrack( eventId, trackId );
				} );

				RegisterFunctor<void, TimelineId, TimelineId>( "setEventSector", [ this ] ( ::interop::IContext& ctx, TimelineId eventId, TimelineId trackId )
				{
					m_timeLine->SetEventSector( eventId, trackId );
				} );

				RegisterFunctor<void, red::DynArray< TimelineId > >( "select", [ this ] ( ::interop::IContext& ctx, const red::DynArray< TimelineId >& ids )
				{
					m_timeLine->Select( ids );
				} );

				RegisterFunctor<void, TimelineId >( "showEventProperties", [this]( ::interop::IContext& ctx, const TimelineId& id )
				{
					m_timeLineInterop->ShowEventProperties( id );
				} );

				RegisterFunctor<void, TimelineId, String >( "setEventState", [ this ] ( ::interop::IContext& ctx, TimelineId eventId, const String& newState )
				{
					m_timeLine->SetEventState( eventId, newState );
				} );

				RegisterFunctor<void >( "muteSelectedItems", [this]( ::interop::IContext& ctx )
				{
					GetEditHistory()->Execute( red::CreateUniquePtr< TimelineMuteSelectedEvents >( m_timeLine, GetSelectedEvents(), true ) );
				} );

				RegisterFunctor<void >( "unmuteSelectedItems", [this]( ::interop::IContext& ctx )
				{
					GetEditHistory()->Execute( red::CreateUniquePtr< TimelineMuteSelectedEvents >( m_timeLine, GetSelectedEvents(), false ) );
				} );

				RegisterFunctor<void >( "toggleMuteSelectedItems", [this]( ::interop::IContext& ctx )
				{
					GetEditHistory()->Execute( red::CreateUniquePtr< TimelineToggleMuteSelectedEvents >( m_timeLine, GetSelectedEvents() ) );
				} );

				RegisterFunctor< void, Float, red::DynArray< TimelineId > >( "move", [ this ] ( ::interop::IContext& ctx, Float offset, const red::DynArray< TimelineId >& ids )
				{
					GetEditHistory()->Execute( red::CreateUniquePtr< TimelineMoveEvents >( m_timeLine, ids, offset ) );
				} );

				RegisterFunctor< void, String, Float >( "resize", [ this ] ( ::interop::IContext& ctx, const String& resizeMode, Float offset )
				{
					TimelineResizeMode mode = resizeMode == "StartTime" ? TimelineResizeMode::StartTime : TimelineResizeMode::EndTime;
					GetEditHistory()->Execute( red::CreateUniquePtr< TimelineResizeSelectedEvents >( m_timeLine, GetSelectedEvents(), mode, offset ) );
				} );

				RegisterFunctor< void, String, Float >( "clip", [this]( ::interop::IContext& ctx, const String& clipMode, Float offset )
				{
					TimelineResizeMode mode = clipMode == "ClipFront" ? TimelineResizeMode::StartTime : TimelineResizeMode::EndTime;
					GetEditHistory()->Execute( red::CreateUniquePtr< TimelineClipSelectedEvents >( m_timeLine, GetSelectedEvents(), mode, offset ) );
				} );

				RegisterFunctor< red::DynArray< String >, TimelineId >( "getCurvesPath", [ this ] ( ::interop::IContext& ctx, TimelineId eventId )
				{
					red::DynArray< String > data;
					m_timeLine->GetEventCurveData( eventId, data );
					return data;
				} );

				RegisterFunctor< red::DynArray< String >, TimelineId >( "getEvaluatorsPath", [ this ] ( ::interop::IContext& ctx, TimelineId eventId )
				{
					red::DynArray< String > data;
					m_timeLine->GetEventEvaluatorsPath( eventId, data );
					return data;
				} );

				RegisterFunctor< red::DynArray< TimelineSnapPoint >, TimelineId >( "getSnapPoints", [ this ] ( ::interop::IContext& ctx, TimelineId eventId )
				{
					red::DynArray< TimelineSnapPoint > data;
					m_timeLine->GetEventSnapPoints( eventId, data );
					return data;
				} );

				RegisterFunctor< void, TimelineId >( "refreshSnapPoints", [ this ] ( ::interop::IContext& ctx, TimelineId eventId )
				{
					m_timeLine->RefreshSnapPoints( eventId );
				} );
			}

		private:
			THandle< ISerializable > m_description;
		};

		class TracksInterop : public TimelineBaseInterop
		{
		public:
			TracksInterop( TimeLineInterop* timelineInterop )
				: TimelineBaseInterop( "Tracks", timelineInterop )
			{
				RegisterMap( "description", [ this ] ( interop::IContext& ctx, const String& idStr ) -> interop::ObjectPtr
				{
					TimelineId id;
					if ( !FromString( idStr, id ) )
					{
						ctx.ThrowException( "Invalid element id - should be TimelineId" );
					}

					// TODO: Store the last Get result
					m_description = m_timeLine->GetTrackDescription( id );
					if ( m_description )
					{
						return interop::utils::CreateRTTIAccessFunc( ctx, &m_description, [this, id]( const rtti::AccessPath& path, const red::String& ) 
						{
							m_timeLine->OnTrackDataChanged( id, path );
						} );
					}

					ctx.ThrowException( "Failed to create description object" );
					return nullptr;
				} );

				RegisterMap( "supportedTrackTypes", [ this ] ( interop::IContext& ctx, const String& idStr ) -> interop::ObjectPtr
				{
					TimelineId id;
					if ( !FromString( idStr, id ) )
					{
						ctx.ThrowException( "Invalid element id - should be TimelineId" );
					}

					// TODO: Store the last Get result
					red::DynArray< String > types;
					m_timeLine->GetSupportedTrackTypes( id, types );

					return interop::ObjectPtr( RED_NEW( interop::utils::ConstValueObject< red::DynArray< String > > )( types ) );
				} );

				RegisterFunctor< proxy::StringPair, String >( "getDefaultEventTypes", [ this ] ( interop::IContext& ctx, const String& idStr )
				{
					TimelineId id;
					if ( !FromString( idStr, id ) )
					{
						ctx.ThrowException( "Invalid element id - should be TimelineId" );
					}

					// TODO: Store the last Get result
					String type, alternativeType;
					m_timeLine->GetDefaultEventTypes( id, type, alternativeType );

					const proxy::StringPair pair{ type, alternativeType };
					return pair;
				} );

				RegisterMap( "supportedEventTypes", [ this ] ( interop::IContext& ctx, const String& idStr ) -> interop::ObjectPtr
				{
					TimelineId id;
					if ( !FromString( idStr, id ) )
					{
						ctx.ThrowException( "Invalid element id - should be TimelineId" );
					}

					// TODO: Store the last Get result
					red::DynArray< String > types;
					m_timeLine->GetSupportedEventTypes( id, types );

					return interop::ObjectPtr( RED_NEW( interop::utils::ConstValueObject< red::DynArray< String > > )( types ) );
				} );

				RegisterMap( "attachedEvents", [ this ] ( interop::IContext& ctx, const String& trackId )
				{
					TimelineId id;
					if ( !FromString( trackId, id ) )
					{
						ctx.ThrowException( "Invalid element id - should be TimelineId" );
					}

					red::DynArray< TimelineId > children;
					m_timeLine->GetTrackEvents( id, children );

					interop::utils::Array* array = RED_NEW( interop::utils::Array )( "TimelineId", children.Size(), [ children ] ( interop::IContext& ctx, Uint32 element ) -> interop::ObjectPtr
					{
						return interop::ObjectPtr( RED_NEW( interop::utils::ConstValueObject<TimelineId> )( children[ element ] ) );
					} );

					return interop::ObjectPtr( array );
				} );

				RegisterFunctor<void, TimelineId, TimelineId>( "reparentTrack", [ this ] ( ::interop::IContext& ctx, TimelineId trackId, TimelineId newParentId  )
				{
					m_timeLine->ReparentTrack( trackId, newParentId );
				} );

				RegisterFunctor<void, TimelineId, TimelineId>( "moveTrackBefore", [ this ] ( ::interop::IContext& ctx, TimelineId trackId, TimelineId destinationTrackId )
				{
					m_timeLine->MoveTrackBefore( trackId, destinationTrackId );
				} );

				RegisterFunctor<void, TimelineId, TimelineId>( "moveTrackAfter", [ this ] ( ::interop::IContext& ctx, TimelineId trackId, TimelineId destinationTrackId )
				{
					m_timeLine->MoveTrackAfter( trackId, destinationTrackId );
				} );

				RegisterFunctor<void, TimelineId, red::DynArray< TimelineId > >( "reparentEvents", [ this ] ( ::interop::IContext& ctx, TimelineId trackId, const red::DynArray< TimelineId >& eventsId )
				{
					m_timeLine->ReparentEvents( trackId, eventsId );
				} );

				RegisterFunctor<void, String >( "createTrack", [ this ] ( ::interop::IContext& ctx, const String& data )
				{
					ObjectCreationInfo info( data );
					if ( !GetHandler()->CreateObject( info ) )
						ctx.ThrowException( "Couldn't create item with given parameters" );
				} );

				RegisterFunctor<void, TimelineId>( "removeTrack", [ this ] ( ::interop::IContext& ctx, TimelineId trackId )
				{
					red::DynArray< EditorObjectIDPath > paths{ red::PoolBackend() };
					red::DynArray< TimelineId > eventIds{ red::PoolBackend() };

					m_timeLine->GetTrackEvents( trackId, eventIds );

					// order is important here, first we will be removing events then the track
					for ( const TimelineId eventId : eventIds )
					{
						paths.PushBack( EditorObjectIDPath( m_timeLine->GetEventDescription( eventId )->GetEditorObjectID() ) );
					}

					THandle< TimeLineTrackDescription > trackDescription = m_timeLine->GetTrackDescription( trackId );
					paths.PushBack( EditorObjectIDPath( trackDescription->GetEditorObjectID() ) );

					if ( !GetHandler()->DeleteObjects( paths ) )
					{
						ctx.ThrowException( "Couldn't delete item with given parameters" );
					}
				} );

				RegisterFunctor<void, TimelineId>( "setTargetTrack", [ this ] ( ::interop::IContext& ctx, TimelineId trackId )
				{
					m_timeLine->SetTargetTrack( trackId );
				} );

				RegisterFunctor<void, TimelineId, Bool>( "setReadOnlyMode", [ this ] ( ::interop::IContext& ctx, TimelineId id, Bool value )
				{
					// TODO: Store the last Get result
					THandle< TimeLineTrackDescription > description = m_timeLine->GetTrackDescription( id );
					if ( description )
					{
						description->SetReadOnlyMode( value );
						m_timeLineInterop->OnInvalidateDescription( id );

						red::HashMap< SerializableID, TimelineId >& selectionMapping = GetSelectionMapping();

						// set readOnly for property page for selected events
						for ( auto& iter : selectionMapping )
						{
							TimelineId selectedEventId = iter.Value();
							if ( selectedEventId != TimelineInvalidId && id == m_timeLine->GetEventTrack( selectedEventId ) )
							{
								const Bool readOnly = value || !description->CanTrackModifyEventProperties();

								GetSelectedDataObject()->SetObject( m_timeLine->GetItemData( selectedEventId ).Get(), readOnly, GetUserContext() );
								GetSelectedDescriptionObject()->SetObject( m_timeLine->CreateItemDataholder( selectedEventId )->GetItem(), readOnly, GetUserContext() );
							}
						}
					}
				} );
			}

		private:
			red::DynArray< String > m_supportedTrackTypes;
			THandle< ISerializable > m_description;
		};

		class SectorsInterop : public TimelineBaseInterop
		{
		public:
			SectorsInterop( TimeLineInterop* timelineInterop )
				: TimelineBaseInterop( "Sectors", timelineInterop )
			{
				red::DynArray< TimelineId > sectors;
				m_timeLine->GetSectors( sectors );

				RegisterArray( "sectors", sectors.Size(), [ sectors ] ( interop::IContext& ctx, Uint32 element ) -> interop::ObjectPtr
				{
					return interop::ObjectPtr( RED_NEW( interop::utils::ConstValueObject<TimelineId> )( sectors[ element ] ) );
				} );

				RegisterMap( "attachedEvents", [ this ] ( interop::IContext& ctx, const String& sectorId )
				{
					TimelineId id;
					if ( !FromString( sectorId, id ) )
					{
						ctx.ThrowException( "Invalid element id - should be TimelineId" );
					}

					red::DynArray< TimelineId > children;
					m_timeLine->GetSectorEvents( id, children );

					interop::utils::Array* array = RED_NEW( interop::utils::Array )( "TimelineId", children.Size(), [ children ] ( interop::IContext& ctx, Uint32 element ) -> interop::ObjectPtr
					{
						return interop::ObjectPtr( RED_NEW( interop::utils::ConstValueObject<TimelineId> )( children[ element ] ) );
					} );

					return interop::ObjectPtr( array );
				} );

				RegisterMap( "description", [ this ] ( interop::IContext& ctx, const String& idStr ) -> interop::ObjectPtr
				{
					TimelineId id;
					if ( !FromString( idStr, id ) )
					{
						ctx.ThrowException( "Invalid element id - should be TimelineId" );
					}

					// TODO: Store the last Get result
					m_description = m_timeLine->GetSectorDescription( id );
					if ( m_description )
					{
						return interop::utils::CreateRTTIAccessFunc( ctx, &m_description );
					}

					ctx.ThrowException( "Failed to create description object" );
					return nullptr;
				} );

				RegisterFunctor < TimelineId, Float, String > ( "createSector", [ this ] ( ::interop::IContext& ctx, Float initialPosition, const String& caption )
				{
					return m_timeLine->CreateSector( TimeLineSectors::Sector, initialPosition, caption );
				} );

				RegisterFunctor<void, TimelineId>( "removeSector", [ this ] ( ::interop::IContext& ctx, TimelineId sectorId )
				{
					m_timeLine->RemoveSector( sectorId );
				} );
			}

			THandle< ISerializable > m_description;
		};

		class EventsGroupsInterop: public TimelineBaseInterop
		{
		public:
			EventsGroupsInterop( TimeLineInterop* timelineInterop )
				: TimelineBaseInterop( "EventsGroups", timelineInterop )
			{
				red::DynArray< TimelineId > eventsGroups;
				m_timeLine->GetEventsGroups( eventsGroups );

				RegisterMap( "description", [this]( interop::IContext& ctx, const String& idStr ) -> interop::ObjectPtr
				{
					TimelineId id;
					if( !FromString( idStr, id ) )
					{
						ctx.ThrowException( "Invalid element id - should be TimelineId" );
					}

					// TODO: Store the last Get result
					m_description = m_timeLine->GetEventsGroupDescription( id );
					if( m_description )
					{
						return interop::utils::CreateRTTIAccessFunc( ctx, &m_description );
					}

					ctx.ThrowException( "Failed to create description object" );
					return nullptr;
				} );

				RegisterFunctor < void, String >( "createGroup", [this]( ::interop::IContext& ctx, const String& data )
				{
					ObjectCreationInfo info( data );
					if( !GetHandler()->CreateObject( info ) )
						ctx.ThrowException( "Couldn't create item with given parameters" );
				} );

				RegisterFunctor < void, String >( "addEvent", [this]( ::interop::IContext& ctx, const String& data )
				{
					ObjectCreationInfo info( data );
					EventsGroupAddData eventsGroupAddData;
					GetTypeObject< EventsGroupAddData >()->WriteValue( IRTTIContext::GetDefault(), &eventsGroupAddData, rtti::AccessPath(), *info.m_arguments );

					m_timeLine->AddToEventsGroup( eventsGroupAddData.m_eventsGroupId, eventsGroupAddData.m_events );
				} );

				RegisterFunctor < void, TimelineId, red::DynArray<TimelineId> >( "remEvents", [this]( ::interop::IContext& ctx, TimelineId eventsGroupId, const red::DynArray<TimelineId>& events )
				{
					GetEditHistory()->StartTransaction( "Ungroup events and/or remove group" );

					red::DynArray< EditorObjectIDPath > paths{ red::PoolBackend() };

					for( TimelineId eventId : events )
					{
						paths.PushBack( EditorObjectIDPath( m_timeLine->CreateItemDataholder( eventId )->GetEditorObjectID() ) );
					}

					if ( !GetHandler()->UngroupObjects( paths, { eventsGroupId } ) )
						ctx.ThrowException( "Couldn't ungroup items with given parameters" );

					THandle< TimeLineEventsGroupItem > description = Cast< TimeLineEventsGroupItem >( m_timeLine->GetEventsGroupDescription( eventsGroupId ) );
					if ( !description->HasEvents() )
					{
						if ( !GetHandler()->DeleteObjects( { EditorObjectIDPath( description->GetEditorObjectID() ) } ) )
						{
							ctx.ThrowException( "Couldn't delete empty group with given parameters" );
						}
					}

					GetEditHistory()->EndTransaction();
				} );
			}

		private:
			THandle< ISerializable > m_description;
		};
	}

	TimeLineInteropContext::TimeLineInteropContext()
		: m_editHistory( nullptr )
		, m_resourceBackend( nullptr )
	{}

	TimeLineInteropContext::TimeLineInteropContext( IResourceEditor* resourceEditor )
		: m_editHistory( &resourceEditor->GetAccessHistory() )
		, m_resourceBackend( resourceEditor->GetBackend() )
	{}

	TimeLineInterop::TimeLineInterop( const TimeLineInteropContext& timeLineContext, ITimeLine* timeline, ObjectCollection* sharedSelection, THandle<ISerializable> userContext /*= nullptr*/ )
		: m_timeLine( timeline )
		, m_editHistory( timeLineContext.m_editHistory )
		, m_instanceId( 0 )
		, m_selectionDataObjects( sharedSelection )
		, m_selectionDescriptionObjects( RED_NEW( ObjectCollection )( *m_editHistory ) )
		, m_descriptorWrapper( RED_NEW( ObjectCollection )( *m_editHistory ) )
		, m_userContext( userContext )
		, m_dataWrappers{ red::PoolBackend() }
		, m_selectionDataMapping{ red::PoolBackend() }
		, m_selectedEvents{ red::PoolBackend() }
		, m_isLoading( false )
	{
		m_timeLine->AttachTimeLineListener( this );

		auto selectionCallback = [ this ] ( const SelectionStatePtr& selection )
		{
			UpdateSelectionCache( selection );
		};

		TimeLineSelectionHandler* selectionHandler = RED_NEW( TimeLineSelectionHandler )( *m_editHistory, selectionCallback );
		m_handler = RED_NEW( ObjectHandler )( *m_editHistory, timeLineContext.m_resourceBackend->GetClipboard(), selectionHandler );

		m_objectResolver = red::CreateUniquePtr< TimeLineObjectResolver >( this );
		m_handler->AttachObjectResolver( m_objectResolver.Get());
		
		m_serializableNotifier.SetCallback( [this]( const SerializableID& id, const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& targetValue )
			{ OnNotifyPropertyChange( id, path ); } );
	}

	TimeLineInterop::~TimeLineInterop()
	{
		m_timeLine->DetachTimeLineListener( this );
	}

	Bool TimeLineInterop::IsSelected( TimelineId eventId ) const
	{
		return m_selectedEvents.Exist( eventId );
	}

	void TimeLineInterop::SetupInterop( TimeLineInterop* timeline, ::interop::utils::Object& op )
	{
		ITimeLine* timelineInterface = timeline->m_timeLine;

		// Initialize interfaces object
		timeline->m_tracks = interop::ObjectPtr( RED_NEW( TracksInterop )( timeline ) );
		timeline->m_events = interop::ObjectPtr( RED_NEW( EventsInterop )( timeline ) );
		timeline->m_sectors = interop::ObjectPtr( RED_NEW( SectorsInterop )( timeline ) );
		timeline->m_eventsGroups = interop::ObjectPtr( RED_NEW( EventsGroupsInterop )(timeline) );

		op.RegisterObject( "objects", timeline->m_handler->CreateInterop() );

		op.RegisterObject( "tracks", timeline->m_tracks );
		op.RegisterObject( "events", timeline->m_events );
		op.RegisterObject( "sectors", timeline->m_sectors );
		op.RegisterObject( "eventsGroups", timeline->m_eventsGroups );

		op.RegisterObject( "dataSelection", timeline->m_selectionDataObjects->GetRootInterop() );
		op.RegisterObject( "descriptionSelection", timeline->m_selectionDescriptionObjects->GetRootInterop() );

		// Function used to send the whole timeline hierarchy data to the UI
		op.RegisterFunctor< proxy::TimelineData, InstanceId >( "_fetchTimeline", 
			[ timeline ] ( ::interop::IContext& ctx, InstanceId instance )
			{
				proxy::TimelineData timelineData;

				timeline->Fetch( instance, timelineData );
				timelineData.m_instanceId = instance;
				timelineData.m_rootTrackId = timeline->m_timeLine->GetTracksRootId();

				return timelineData;
			} );

		// Used by functional tests
		op.RegisterFunctor< InstanceId >( "getTimelineInstanceId",
			[ timeline ]( ::interop::IContext& ctx )
			{
				return timeline->m_instanceId;
			} );

		op.RegisterFunctor< red::DynArray< String >, InstanceId >( "getPendingCommands",
			[timeline] ( ::interop::IContext& ctx, InstanceId instance )
		{
			red::DynArray< String > pendingCommands;
			timeline->FlushPendingCommands( pendingCommands );
			return pendingCommands;
		} );

		op.RegisterFunctor< red::DynArray< String > >( "getAvailableLayers",
			[timeline]( ::interop::IContext& ctx )
		{
			red::DynArray< String > layers;
			timeline->GetAvailableLayers( layers );
			return layers;
		} );

		op.RegisterFunctor< void, String >( "toggleLayer",
			[timeline]( ::interop::IContext& ctx, const String& layer )
		{
			timeline->ToggleLayer( layer );
		} );

		op.RegisterFunctor< void >( "stepOut", [ timeline ]( ::interop::IContext& ctx )
		{
			timeline->StepOut();
		} );

		// Initialize notifiers
		op.RegisterNotifier( "_onTimeLineChanged", timeline->m_onTimeLineChanged );
		op.RegisterNotifier( "_onPendingCommands", timeline->m_onPendingCommands );
		op.RegisterNotifier( "_onLayersChanged", timeline->m_onLayersChanged );
	}

	void TimeLineInterop::UpdateSelectionCache( const SelectionStatePtr& selection )
	{
		red::DynArray< TimelineId > ids;
		for ( SelectionIterator iterator = selection->Iterate(); iterator; ++iterator )
		{
			TimelineId selectedId = iterator->GetEditorObjectID().GetInternalID();
			// check if this is track or event, we are adding only events to m_selectedEvents
			// if in future we decide that we want to also have tracks in selection, then this needs to be changed
			if ( m_timeLine->IsEvent( selectedId ) )
			{
				ids.PushBack( selectedId );
			}
		}

		Bool changedSelection = false;
		if ( m_selectedEvents != ids )
		{
			changedSelection = true;
		}

		m_selectionDataMapping.Clear();
		m_selectedEvents.Clear();

		if ( ids.Size() && ids.Front() != TimelineInvalidId )
		{
			TimelineId id = ids.Front();

			Bool readOnly = true;
			TimelineId trackId = m_timeLine->GetEventTrack( id );
			THandle< TimeLineTrackDescription > description = m_timeLine->GetTrackDescription( trackId );
			if ( description )
			{
				readOnly = !description->CanTrackModifyEventProperties();
			}

			red::DynArray< THandle< ISerializable > > dataList;
			red::DynArray< THandle< ISerializable > > descriptorList;

			for ( const TimelineId id : ids )
			{
				THandle< ISerializable > data = m_timeLine->GetItemData( id );
				THandle< TimelineItemDataHolder > itemDataholder = m_timeLine->CreateItemDataholder( id );

				if ( data && itemDataholder )
				{
					m_selectionDataMapping.Insert( data->GetID(), id );

					dataList.PushBack( data );
					descriptorList.PushBack( HandleFromPtr( itemDataholder->GetItem() ) );
				}
			}

			m_selectionDataObjects->SetObjects( dataList, m_userContext, readOnly );
			m_selectionDescriptionObjects->SetObjects( descriptorList, m_userContext, readOnly );

			m_selectedEvents = ids;
		}
		else
		{
			m_selectionDataObjects->Reset();
			m_selectionDescriptionObjects->Reset();

			m_selectedEvents.Clear();

			m_timeLine->OnDeselectionChanged();
		}

		if ( changedSelection )
		{
			m_timeLine->OnSelectionChanged( ids );
			SendEventSelectionChange( ids );
		}
	}

	void TimeLineInterop::OnNotifyPropertyChange( const SerializableID& id, const rtti::AccessPath& path )
	{
		auto itr = m_selectionDataMapping.Find( id );
		if ( itr != m_selectionDataMapping.End() )
		{
			// #scnb_timelineviews_removenote_szi Note id is SerializableID of EventDescriptor or NodeDescriptor. In other words,
			// we inform timeline that EventDescriptor/NodeDescriptor associated with given timeline event has changed.
			m_timeLine->OnDataItemChanged( itr.Value(), path );
		}
	}

	void TimeLineInterop::Fetch( InstanceId cmdInstance, proxy::TimelineData& outTimelineData )
	{
		if ( !IsCommandValid( cmdInstance ) )
		{
			// If timeline was invalidated before first fetch was executed we have to force frontend to get pending commands.
			// If we don't, invalidate msg won't be executed and new instance of the timeline won't be loaded, 
			if ( !m_pendingCommands.Empty() && cmdInstance == 0 )
			{
				m_onPendingCommands.Notify();
			}
			return;
		}

		// TODO: fix tracks order after track reparent

		const Bool includeInLoadingPhase = true;

		red::DynArray< TimelineId > tracks;
		m_timeLine->GetTracksInOrder( tracks );

		red::DynArray< TimelineId > sectors;
		m_timeLine->GetSectors( sectors );

		red::DynArray< TimelineId > events;
		m_timeLine->GetEvents( events );

		red::DynArray< TimelineId > eventsGroups;
		m_timeLine->GetEventsGroups( eventsGroups );

		outTimelineData.m_tracks.Reserve( tracks.Size() );
		outTimelineData.m_sectors.Reserve( sectors.Size() );
		outTimelineData.m_pointEvents.Reserve( events.Size() );
		outTimelineData.m_durationEvents.Reserve( events.Size() );

		TimelineId parentId;
		String type;
		for ( const TimelineId id : tracks )
		{
			const THandle< TimeLineTrackDescription > description = m_timeLine->GetTrackDescription( id );
			parentId = m_timeLine->GetTrackParentId( id );
			m_timeLine->GetTrackType( id, type );

			proxy::TimelineTrack track;
			track.m_id = id;
			track.m_parentId = parentId;
			track.m_type = type;

			track.m_visualType = description->GetVisualType();
			track.m_caption = description->GetCaption();
			track.m_foregroundColor = description->GetForegroundColor();
			track.m_backgroundColor = description->GetBackGroundColor();
			track.m_isMute = description->IsMute();

			track.m_configuration = description->GetConfiguration();
			track.m_isVisible = description->IsSelfVisible();
			track.m_isPinned = description->IsPinned();
			track.m_isMute = description->IsMute();
			track.m_isExpanded = description->IsExpanded();
			track.m_isLocked = description->IsLocked();

			m_timeLine->GetDefaultEventTypes( id, track.m_defaultEventType, track.m_alternativeDefaultEventType );
			m_timeLine->GetSupportedEventTypes( id, track.m_supportedEventTypes );
			m_timeLine->GetSupportedTrackTypes( id, track.m_supportedTrackTypes );

			outTimelineData.m_tracks.PushBack( std::move( track ) );
		}

		for( const TimelineId id : sectors )
		{
			m_timeLine->GetSectorType( id, type );

			proxy::TimelineSector sector;
			sector.m_id = id;
			sector.m_type = type;

			outTimelineData.m_sectors.PushBack( std::move( sector ) );
		}

		for( const TimelineId id : eventsGroups )
		{
			m_timeLine->GetEventsGroupType( id, type );

			proxy::TimelineEventsGroup eventsGroup;
			eventsGroup.m_id = id;
			eventsGroup.m_type = type;

			outTimelineData.m_eventsGroups.PushBack( std::move( eventsGroup ) );
		}

		for ( const TimelineId id : events )
		{
			const THandle< TimeLineEventDescription >& description = m_timeLine->GetEventDescription( id );
			const Float durationTime = description->GetDurationTime();
		
			proxy::TimelineEvent event;
			event.m_id = description->GetId();
			event.m_type = description->GetType();

			event.m_visualType = description->GetVisualType();
			event.m_caption = description->GetCaption();
			event.m_foregroundColor = description->GetForegroundColor();
			event.m_backgroundColor = description->GetBackGroundColor();
			event.m_isMute = description->IsMute();

			event.m_trackId = description->GetBoundTrackId();
			event.m_sectorId = description->GetBoundSectorId();
			event.m_eventsGroupIds = description->GetBoundEventsGroupIds();
			event.m_iconName = description->GetIconName();

			event.m_startTime = description->GetStartTime();
			event.m_durationTime = durationTime;
			event.m_state = description->GetState();
			event.m_isLocked = description->IsLocked();

			if ( durationTime == 0.0f )
			{
				outTimelineData.m_pointEvents.PushBack( std::move( event ) );
			}
			else
			{
				outTimelineData.m_durationEvents.PushBack( std::move( event ) );
			}

		}

		Send( CommandType::CT_Event, "EventsSelectionChanged", m_selectionDataMapping.GetValues(), includeInLoadingPhase );
		Send( CommandType::CT_Timeline, "OnFetchEnd", includeInLoadingPhase );

		m_isLoading = false;

		if ( !m_postponedCommands.Empty() )
		{
			m_pendingCommands.PushBack( std::move( m_postponedCommands ) );
			m_postponedCommands.Clear();
			m_onPendingCommands.Notify();
		}
	}

	Bool TimeLineInterop::IsCommandValid( InstanceId commandInstnace )
	{
		return commandInstnace == m_instanceId;
	}

	void TimeLineInterop::Send( const CommandType commandType, const String& command, Bool includeInLoadingPhase )
	{
		QueueCommand( String::Printf( "%u %s %d", commandType, command.AsChar(), m_instanceId ), includeInLoadingPhase );
	}

	void TimeLineInterop::Send( const CommandType commandType, const AnsiChar* command, const red::DynArray< TimelineId >& ids, Bool includeInLoadingPhase )
	{
		QueueCommand( String::Printf( "%u %s %d %s", commandType, command, m_instanceId, ArrayToString( ids ).AsChar() ), includeInLoadingPhase );
	}

	void TimeLineInterop::Send( const CommandType commandType, const AnsiChar* command, const red::String& additonalParameters, Bool includeInLoadingPhase )
	{
		QueueCommand( String::Printf( "%u %s %d %s", commandType, command, m_instanceId, additonalParameters.AsChar() ), includeInLoadingPhase );
	}

	void TimeLineInterop::Send( const CommandType commandType, const AnsiChar* command, Bool includeInLoadingPhase )
	{
		QueueCommand( String::Printf( "%u %s %d", commandType, command, m_instanceId ), includeInLoadingPhase );
	}

	void TimeLineInterop::Send( const CommandType commandType, const AnsiChar* command, TimelineId id, Bool includeInLoadingPhase )
	{
		QueueCommand( String::Printf( "%u %s %d %d", commandType, command, m_instanceId, id ), includeInLoadingPhase );
	}

	void TimeLineInterop::Send( const CommandType commandType, const AnsiChar* command, TimelineId id, const red::String& additonalParameters, Bool includeInLoadingPhase )
	{
		QueueCommand( String::Printf( "%u %s %d %d %s", commandType, command, m_instanceId, id, additonalParameters.AsChar() ), includeInLoadingPhase );
	}

	void TimeLineInterop::Send( const CommandType commandType, const AnsiChar* command, TimelineId firstId, TimelineId secondId, Bool includeInLoadingPhase )
	{
		QueueCommand( String::Printf( "%u %s %d %d %d", commandType, command, m_instanceId, firstId, secondId ), includeInLoadingPhase );
	}

	void TimeLineInterop::Send( const CommandType commandType, const AnsiChar* command, TimelineId firstId, TimelineId secondId, const red::String& additonalParameters, Bool includeInLoadingPhase )
	{
		QueueCommand( String::Printf( "%u %s %d %d %d %s", commandType, command, m_instanceId, firstId, secondId, additonalParameters.AsChar() ), includeInLoadingPhase );
	}

	///////////////////////////////////////////////////////////////////////////

	void TimeLineInterop::OnInvalidateTimeline()
	{
		m_instanceId++;
		m_pendingCommands.Clear();
		m_postponedCommands.Clear();
		m_isLoading = false;
		Send( CommandType::CT_Timeline, "InvalidateTimeline" );
		m_isLoading = true;

		m_descriptorWrapper->Reset();
		m_dataWrappers.Clear();
	}

	ObjectCollection* TimeLineInterop::GetDataWrapper( TimelineId id )
	{
		red::SharedPtr< ObjectCollection > collection;
		if ( m_dataWrappers.Find( id, collection ) )
			return collection.Get();

		collection = red::CreateSharedPtr< ObjectCollection >( *m_editHistory );
		m_dataWrappers.Insert( id, collection );

		return collection.Get();
	}

	void TimeLineInterop::RemoveDataWrapper( TimelineId id )
	{
		m_dataWrappers.Remove( id );
	}

	void TimeLineInterop::QueueCommand( const String& cmd, Bool includeInLoadingPhase )
	{
		if ( m_isLoading )
		{
			if ( includeInLoadingPhase )
			{
				m_pendingCommands.PushBack( cmd );
				m_onPendingCommands.Notify();
			}
			else
			{
				m_postponedCommands.PushBack( cmd );
			}
		}
		else if ( !m_isLoading )
		{
			m_pendingCommands.PushBack( cmd );
			m_onPendingCommands.Notify();
		}
	}

	void TimeLineInterop::FlushPendingCommands( red::DynArray< String >& outCommands )
	{
		outCommands = std::move( m_pendingCommands );
		m_pendingCommands.Clear();
	}

	void TimeLineInterop::GetAvailableLayers( red::DynArray< String >& outLayers ) const
	{
		m_timeLine->GetAvailableLayers( outLayers );
	}

	void TimeLineInterop::ToggleLayer( const String& layer )
	{
		m_timeLine->ToggleLayer( layer );
	}

	void TimeLineInterop::ActivateLayer( const String& layer )
	{
		m_timeLine->ActivateLayer( layer );
	}

	void TimeLineInterop::DeactivateLayer( const String& layer )
	{
		m_timeLine->DeactivateLayer( layer );
	}

	void TimeLineInterop::OnInvalidateTimelineDescriptionProperty( const red::String& propertyName )
	{
		Send( CommandType::CT_Timeline, "InvalidateTimlineDescriptionProperty", propertyName );
	}

	void TimeLineInterop::OnTimelineActiveLayersChanged( const red::DynArray< String >& activeLayers )
	{
		Send( CommandType::CT_Timeline, "InvalidateActiveLayers", ArrayToString( activeLayers ) );
	}

	void TimeLineInterop::OnInvalidateDescription( TimelineId itemId )
	{
		Send( CommandType::CT_Timeline, "InvalidateItemDescription", itemId );
	}

	void TimeLineInterop::OnInvalidateDescriptionProperty( TimelineId itemId, const red::String& propertyName )
	{
		Send( CommandType::CT_Timeline, "InvalidateItemDescriptionProperty", itemId, propertyName );
	}

	void TimeLineInterop::OnInvalidateDescription( const red::DynArray< TimelineId >& itemIds )
	{
		Send( CommandType::CT_Timeline, "InvalidateItemsDescription", itemIds );
	}

	void TimeLineInterop::OnInvalidateDescriptionProperty( const red::DynArray< TimelineId >& itemIds, const red::String& propertyName )
	{
		Send( CommandType::CT_Timeline, String::Printf( "%s %s %s", "InvalidateItemsDescriptionProperty", propertyName, ArrayToString( itemIds ) ) );
	}

	void TimeLineInterop::OnTrackCreated( TimelineId trackId, TimelineId parentTrackId, const red::String& type, Bool includeInLoadingPhase )
	{
		Send( CommandType::CT_Track, "TrackAdded", trackId, parentTrackId, type, includeInLoadingPhase );
	}

	void TimeLineInterop::OnTrackDeleted( TimelineId trackId, TimelineId parentTrackId )
	{
		Send( CommandType::CT_Track, "TrackDeleted", trackId, parentTrackId );
	}

	void TimeLineInterop::OnTrackReparent( TimelineId trackId, TimelineId newParentId )
	{
		Send( CommandType::CT_Track, "TrackReparent", trackId, newParentId );
	}

	void TimeLineInterop::OnTrackReordered( TimelineId trackId, Uint32 newIndex )
	{
		Send( CommandType::CT_Track, "TrackReordered", trackId, newIndex, false );
	}

	void TimeLineInterop::OnEventsSelected( const red::DynArray< TimelineId >& eventIds )
	{
		red::DynArray< EditorObjectID > editorIDs;
		editorIDs.Reserve( eventIds.Size() );

		for( const TimelineId eventId : eventIds )
		{
			if( THandle< TimelineItemDataHolder > eventDataholder = m_timeLine->CreateItemDataholder( eventId ) )
			{
				if ( ISerializable* item = eventDataholder->GetItem() )
				{
					editorIDs.EmplaceBack( item->GetEditorObjectID() );
				}
			}
		}

		m_handler->SetSelectionFromObjectIDs( editorIDs );
	}

	void TimeLineInterop::OnEventCreated( TimelineId eventId, TimelineId parentTrackId, const red::String& type, Bool includeInLoadingPhase )
	{
		Send( CommandType::CT_Event, "EventAdded", eventId, parentTrackId, type, includeInLoadingPhase );
	}

	void TimeLineInterop::OnEventDeleted( TimelineId eventId )
	{
		RemoveDataWrapper( eventId );
		Send( CommandType::CT_Event, "EventDeleted", eventId );
	}

	void TimeLineInterop::OnEventReparent( TimelineId eventId, TimelineId trackId )
	{
		Send( CommandType::CT_Event, "EventDeleted", eventId );

		String type;
		m_timeLine->GetEventType( eventId, type );
		Send( CommandType::CT_Event, "EventAdded", eventId, trackId, type );
	}

	void TimeLineInterop::OnEventComponentChanged( TimelineId eventId, Uint64 componentId )
	{
		Send( CommandType::CT_Event, "EventComponentChanged", eventId, componentId );
	}

	void TimeLineInterop::OnSectorCreated( TimelineId id, const red::String& type, Bool includeInLoadingPhase )
	{
		Send( CommandType::CT_Sector, "SectorAdded", id, type, includeInLoadingPhase );
	}

	void TimeLineInterop::OnSectorDeleted( TimelineId id )
	{
		Send( CommandType::CT_Sector, "SectorDeleted", id );
	}

	void TimeLineInterop::OnEventsGroupCreated( TimelineId id )
	{
		Send( CommandType::CT_EventsGroup, "EventsGroupCreated", id );
	}

	void TimeLineInterop::OnEventsGroupDeleted( TimelineId id )
	{
		Send( CommandType::CT_EventsGroup, "EventsGroupDeleted", id );
	}

	void TimeLineInterop::SendEventSelectionChange( const red::DynArray< TimelineId >& selection )
	{
		Send( CommandType::CT_Event, "EventsSelectionChanged", ArrayToString( selection ) );
	}

	void TimeLineInterop::ClearSelection()
	{
		if ( !m_selectedEvents.Empty() )
		{
			m_selectedEvents.Clear();
			m_selectionDataObjects->Reset();
			m_selectionDescriptionObjects->Reset();
			m_selectionDataMapping.Clear();
			m_descriptorWrapper->Reset();
			m_handler->GetSelectionHandler()->Action_ChangeSelection( SelectionState::EmptySelection() );
		}
	}

	void TimeLineInterop::RefreshSelection()
	{
		if ( !m_selectedEvents.Empty() )
		{
			ShowEventProperties( m_selectedEvents[ 0 ] );
		}
	}

	void TimeLineInterop::ShowEventProperties( const TimelineId id )
	{
		Bool readOnly = true;
		TimelineId trackId = m_timeLine->GetEventTrack( id );
		THandle< TimeLineTrackDescription > description = m_timeLine->GetTrackDescription( trackId );
		if ( description )
		{
			readOnly = !description->CanTrackModifyEventProperties();
		}

		red::DynArray< THandle< ISerializable > > dataList;
		red::DynArray< THandle< ISerializable > > descriptorList;

		THandle< ISerializable > data = m_timeLine->GetItemData( id );
		THandle< TimelineItemDataHolder > itemDataholder = m_timeLine->CreateItemDataholder( id );

		if ( data && itemDataholder )
		{
			dataList.PushBack( data );
			descriptorList.PushBack( HandleFromPtr( itemDataholder->GetItem() ) );
		}

		m_selectionDataObjects->SetObjects( dataList, m_userContext, readOnly );
		m_selectionDescriptionObjects->SetObjects( descriptorList, m_userContext, readOnly );
	}

}

RTTI_BEGIN_TYPE_IN_NAMESPACE( EventsGroupAddData, tools );
RTTI_PROPERTY( m_eventsGroupId );
RTTI_PROPERTY( m_events );
RTTI_END_TYPE();
