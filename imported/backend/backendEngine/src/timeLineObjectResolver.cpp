/**
* Copyright (c) 2017-2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "timelineObjectResolver.h"

#include "objectSerializablePersistentCollection.h"

#include "timeLineInterop.h"
#include "timeLineBase.h"
#include "objectActionUngroupObjects.h"

#include "../../../common/redReflection/include/editableView.h"
#include "../../../common/redReflection/include/cloningUtils.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( TimeLineObjectResolver, tools )
	RTTI_PARENT_TYPE( tools::IObjectResolver );
RTTI_END_TYPE()

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( TimeLineBaseObjectConstructor, tools )
RTTI_END_TYPE()

RTTI_BEGIN_TYPE_IN_NAMESPACE( TimeLineEventConstructor, tools )
	RTTI_PARENT_TYPE( TimeLineBaseObjectConstructor );
RTTI_END_TYPE()

RTTI_BEGIN_TYPE_IN_NAMESPACE( TimeLineTrackConstructor, tools )
	RTTI_PARENT_TYPE( TimeLineBaseObjectConstructor );
RTTI_END_TYPE()

RTTI_BEGIN_TYPE_IN_NAMESPACE( TimeLineEventsGroupConstructor, tools )
	RTTI_PARENT_TYPE( TimeLineBaseObjectConstructor );
RTTI_END_TYPE()

RTTI_BEGIN_TYPE_IN_NAMESPACE( EventCreationData, tools )
	RTTI_PROPERTY( m_trackId ).editable();
	RTTI_PROPERTY( m_type ).editable();
	RTTI_PROPERTY( m_caption ).editable();
	RTTI_PROPERTY( m_time ).editable();
RTTI_END_TYPE()

RTTI_BEGIN_TYPE_IN_NAMESPACE( TrackCreationData, tools )
	RTTI_PROPERTY( m_parentId ).editable();
	RTTI_PROPERTY( m_type ).editable();
	RTTI_PROPERTY( m_caption ).editable();
	RTTI_PROPERTY( m_previousId ).editable();
RTTI_END_TYPE()

RTTI_BEGIN_TYPE_IN_NAMESPACE( EventsGroupCreationData, tools )
	RTTI_PROPERTY( m_internalType );
	RTTI_PROPERTY( m_events );
RTTI_END_TYPE()

namespace tools
{
	///
	///
	///

	CName TimeLineEventConstructor::s_type = RED_NAME_CONSTEXPR( "TimeLineEventDescription" );

	TimeLineEventConstructor::TimeLineEventConstructor() = default;

	TimeLineEventConstructor::TimeLineEventConstructor( ITimeLine* timeline )
		: TimeLineBaseObjectConstructor( timeline )
	{
	}

	CName TimeLineEventConstructor::GetSupportedType()
	{
		return s_type;
	}

	const Bool TimeLineEventConstructor::CanCreateObject( const ObjectCreationInfo& info, String* outReason ) const
	{
		EventCreationData data;
		if ( GetTypeObject< EventCreationData> ()->WriteValue( IRTTIContext::GetDefault(), &data, rtti::AccessPath(), *info.m_arguments ) )
			return true;

		return false;
	}

	const Bool TimeLineEventConstructor::CreateObject( const ObjectCreationInfo& info, ITimeLine* timeline, THandle< IBackendData >& outObject ) const
	{
		if ( !CanCreateObject( info ) )
			return false;

		// create node
		EventCreationData data;
		GetTypeObject<EventCreationData>()->WriteValue( IRTTIContext::GetDefault(), &data, rtti::AccessPath(), *info.m_arguments );

		TimelineId id = timeline->CreateTimelineEvent( data.m_trackId, data.m_type, data.m_time, data.m_caption );
		outObject = timeline->CreateItemDataholder( id );

		return true;
	}

	const Bool TimeLineEventConstructor::CanDeleteObject( const EditorObjectID& id ) const
	{
		if( !SupportsContentType( id.GetKind() ) )
		{
			return false;
		}
		
		const Bool canRemoveEvent = m_timeline->CanRemoveEvent( id.GetInternalID() );
		return canRemoveEvent;
	}

	const Bool TimeLineEventConstructor::DeleteObject( const EditorObjectID& id, ITimeLine* timeline ) const
	{
		if ( !CanDeleteObject( id ) )
			return false;

		timeline->RemoveEvent( id.GetInternalID() );
		return true;
	}

	const Bool TimeLineEventConstructor::CanRestoreObject( const IObjectPersistentCollection::ObjectInfo& info ) const
	{
		return SupportsContentType( info.m_id.GetKind() );
	}

	const Bool TimeLineEventConstructor::RestoreObject( const IObjectPersistentCollection::ObjectInfo& info, ITimeLine* timeline ) const
	{
		if ( !CanRestoreObject( info ) )
			return false;

		const THandle< TimelineItemDataHolder > itemDesc = Cast< TimelineItemDataHolder >( info.m_object );
		return timeline->RestoreEvent( Cast< TimeLineEventDescription >( itemDesc->GetItemHandle() ) );
	}

	const Bool TimeLineEventConstructor::CanPlaceObject( const IObjectPersistentCollection::ObjectInfo& info ) const
	{
		if ( !SupportsContentType( info.m_id.GetKind() ) )
			return false;

		if ( !info.m_object || !m_timeline->CanPlaceEventObject( info.m_object ) )
			return false;

		return true;
	}

	TimelineId FindTargetTrack( const TimelineId userTargetTrackId, ITimeLine* m_timeline, const THandle< TimelineItemDataHolder >& itemDesc )
	{
		if ( !m_timeline->IsSmartTrackSelectionEnabled() )
		{
			return m_timeline->GetTargetTrack();
		}

		TimelineId proposedTargetTrackId = userTargetTrackId;
		const TimelineId sourceTrackId = itemDesc->GetParentHandle()->GetId();

		red::DynArray< TimelineId > tracksIds{ red::PoolBackend() };
		tracksIds.Reserve( 32 );
		m_timeline->GetTracks( tracksIds );

		const Bool isExternalPaste = !tracksIds.Exist( sourceTrackId );

		if ( proposedTargetTrackId == TimelineInvalidId )
		{
			// user used keyboard shortcut, target track is set to TimelineInvalidId, we need to find proposed target track here
			if ( !isExternalPaste )
			{
				proposedTargetTrackId = sourceTrackId;
			}
			else
			{
				proposedTargetTrackId = TimelineInvalidId;

				// pasting with keyboard shortcuts from different timeline, try to find by name instead of id
				const String sourceTrackId = itemDesc->GetParentHandle()->GetCaption();
				for ( const TimelineId id : tracksIds )
				{
					if ( m_timeline->GetTrackDescription( id )->GetCaption() == sourceTrackId )
					{
						proposedTargetTrackId = id;
						break;
					}
				}
			}
		}
		else
		{
			if ( isExternalPaste )
			{
				proposedTargetTrackId = TimelineInvalidId;

				// pasting with mouse from different timeline, try to find by name instead of id
				const String sourceTrackCaption = itemDesc->GetParentHandle()->GetCaption();
				for ( const TimelineId id : tracksIds )
				{
					if ( m_timeline->GetTrackDescription( id )->GetCaption() == sourceTrackCaption )
					{
						proposedTargetTrackId = id;
						break;
					}
				}
			}
		}
		
		return proposedTargetTrackId;
	}

	const Bool TimeLineEventConstructor::PlaceObject( const TimelineObjectPlacementInfo& placementInfo ) const
	{
		const THandle< TimelineItemDataHolder > itemDesc = Cast< TimelineItemDataHolder >( placementInfo.m_info.m_object );
		THandle< TimeLineEventDescription > oldEvent = Cast< TimeLineEventDescription >( itemDesc->GetItemHandle() );
		const THandle< TimeLineTrackDescription > sourceParentTrack = itemDesc->GetParentHandle();
		const String sourceTrackName = sourceParentTrack->GetCaption();
		const TimelineId userTargetTrackId = m_timeline->GetTargetTrack();

		TimelineId proposedTargetTrackId = FindTargetTrack( userTargetTrackId, m_timeline, itemDesc );

		if ( proposedTargetTrackId == TimelineInvalidId )
		{
			// create default track
			// check for parent track id if it's a group, create group track first
			TimelineId parentTrackId = m_timeline->GetTracksRootId();
			if ( sourceParentTrack->GetType() == TimeLineTracks::TracksGroup )
			{
				parentTrackId = m_timeline->CreateTrack( sourceParentTrack->GetId(), TimeLineTracks::TracksGroup, sourceParentTrack->GetCaption(), TimelineInvalidId );
			}
			proposedTargetTrackId = m_timeline->CreateTrack( parentTrackId, TimeLineTracks::Track, sourceTrackName, TimelineInvalidId );
		}

		m_timeline->SetTargetTrack( proposedTargetTrackId );

		THandle< TimeLineEventDescription > newEvent = Cast< TimeLineEventDescription >( CloneObject( oldEvent ) );

		if ( !placementInfo.m_editor->PlaceEvent( newEvent ) )
			return false;

		newEvent->MoveEventByOffset( placementInfo.m_timeOffset );

		placementInfo.m_outObjects.Insert( placementInfo.m_info.m_id, CreateHandle< TimelineItemDataHolder >( newEvent, m_timeline->GetTrackDescription( proposedTargetTrackId ), newEvent->GetBoundEventsGroupIds() ) );

		m_timeline->SetTargetTrack( TimelineInvalidId );

		return true;
	}

	const Bool TimeLineEventConstructor::CanResolveObject( const EditorObjectID& id ) const
	{
		return SupportsContentType( id.GetKind() );
	}

	THandle< TimelineItemDataHolder > TimeLineEventConstructor::ResolveObject( const EditorObjectID& id ) const
	{
		return m_timeline->CreateItemDataholder( id.GetInternalID() );
	}

	const Bool TimeLineEventConstructor::CanGroupObject( const EditorObjectID& id ) const
	{
		return SupportsContentType( id.GetKind() );
	}

	const Bool TimeLineEventConstructor::GroupObject( const EditorObjectID& id, const red::DynArray< Uint64 >& groupIds ) const
	{
		const THandle< TimelineItemDataHolder > itemDesc = ResolveObject( id );
		const THandle< TimeLineEventDescription > eventDesc = Cast< TimeLineEventDescription >( itemDesc->GetItemHandle() );

		for( Uint64 groupId : groupIds )
		{
			AddToEventsGroup( eventDesc, groupId );
		}

		return true;
	}

	const Bool TimeLineEventConstructor::CanUngroupObject( const EditorObjectID& id, red::DynArray< Uint64 >& outObjectGroups ) const
	{
		if ( !SupportsContentType( id.GetKind() ) )
			return false;

		const THandle< TimelineItemDataHolder > itemDesc = ResolveObject( id );
		if ( itemDesc->IsGrouped() )
		{
			outObjectGroups.PushBack( itemDesc->GetGroups() );
		}
		return itemDesc->IsGrouped();
	}

	void TimeLineEventConstructor::AddToEventsGroup( const THandle< TimeLineEventDescription >& eventDesc, const TimelineId groupId ) const
	{
		red::DynArray< TimelineId > eventGroupsToRemove{ red::PoolBackend() };
		m_timeline->AddToEventsGroup( groupId, { eventDesc->GetId() } );
	}

	void TimeLineEventConstructor::RemoveFromEventsGroup( const THandle< TimeLineEventDescription >& eventDesc ) const
	{
		const red::DynArray< TimelineId > boundEventGroups = eventDesc->GetBoundEventsGroupIds();
		for ( const TimelineId id : boundEventGroups )
		{
			m_timeline->RemFromEventsGroup( id, { eventDesc->GetId() } );
		}
	}

	const Bool TimeLineEventConstructor::UngroupObject( const EditorObjectID& id ) const
	{
		const THandle< TimelineItemDataHolder > itemDesc = ResolveObject( id );
		const THandle< TimeLineEventDescription > eventDesc = Cast< TimeLineEventDescription >( itemDesc->GetItemHandle() );

		RemoveFromEventsGroup( eventDesc );

		return true;
	}

	const Bool TimeLineEventConstructor::SupportsContentType( const CName& contentTypeName ) const
	{
		return contentTypeName == s_type;
	}

	THandle< ISerializable > TimeLineEventConstructor::GetData( const IObjectPersistentCollection::ObjectInfo& info ) const
 	{
		return info.m_object;
	}

	//////////////////////////////////////////////////////////////////////////

	CName TimeLineTrackConstructor::s_type = RED_NAME_CONSTEXPR( "TimeLineTrackDescription" );

	TimeLineTrackConstructor::TimeLineTrackConstructor() = default;

	TimeLineTrackConstructor::TimeLineTrackConstructor( ITimeLine* timeline ) 
		: TimeLineBaseObjectConstructor( timeline )
	{
	}

	CName TimeLineTrackConstructor::GetSupportedType()
	{
		return s_type;
	}

	const Bool TimeLineTrackConstructor::CanCreateObject( const ObjectCreationInfo& info, String* outReason ) const
	{
		TrackCreationData data;
		if ( GetTypeObject< TrackCreationData > ()->WriteValue( IRTTIContext::GetDefault(), &data, rtti::AccessPath(), *info.m_arguments ) )
			return true;

		return false;
	}

	const Bool TimeLineTrackConstructor::CreateObject( const ObjectCreationInfo& info, ITimeLine* editor, THandle< IBackendData >& outObject ) const
	{
		if ( !CanCreateObject( info ) )
			return false;

		TrackCreationData data;
		GetTypeObject< TrackCreationData >()->WriteValue( IRTTIContext::GetDefault(), &data, rtti::AccessPath(), *info.m_arguments );

		TimelineId id = editor->CreateTrack( data.m_parentId, data.m_type, data.m_caption, data.m_previousId );
		if ( id == TimelineInvalidId )
			return false;

		outObject = Cast< IBackendData >( editor->GetTrackDescription( id ) );

		return true;
	}

	const Bool TimeLineTrackConstructor::CanDeleteObject( const EditorObjectID& id ) const
	{
		return SupportsContentType( id.GetKind() );
	}

	const Bool TimeLineTrackConstructor::DeleteObject( const EditorObjectID& id, ITimeLine* editor ) const
	{
		if ( !CanDeleteObject( id ) )
			return false;

		editor->RemoveTrack( id.GetInternalID() );
		return true;
	}

	const Bool TimeLineTrackConstructor::CanRestoreObject( const IObjectPersistentCollection::ObjectInfo& info ) const
	{
		return SupportsContentType( info.m_id.GetKind() );
	}

	const Bool TimeLineTrackConstructor::RestoreObject( const IObjectPersistentCollection::ObjectInfo& info, ITimeLine* editor ) const
	{
		if ( !CanRestoreObject( info ) )
			return false;

		const THandle< TimelineItemDataHolder > itemDesc = Cast< TimelineItemDataHolder >( info.m_object );
		THandle< TimeLineTrackBaseItem > track = Cast< TimeLineTrackBaseItem >( itemDesc->GetItemHandle() );
		return editor->RestoreTrack( track );
	}

	const Bool TimeLineTrackConstructor::CanPlaceObject( const IObjectPersistentCollection::ObjectInfo& info ) const
	{
		// LL: for now there is no operation to copy/paste whole track
		return false;
	}

	const Bool TimeLineTrackConstructor::PlaceObject( const TimelineObjectPlacementInfo& placementInfo ) const
	{
		// LL: for now there is no operation to copy/paste whole track
		return false;
	}

	const Bool TimeLineTrackConstructor::CanResolveObject( const EditorObjectID& id ) const
	{
		return SupportsContentType( id.GetKind() );
	}

	THandle< TimelineItemDataHolder > TimeLineTrackConstructor::ResolveObject( const EditorObjectID& id ) const
	{
		const auto trackDescription = m_timeline->GetTrackDescription( id.GetInternalID() );
		const auto parentTrackDescription = m_timeline->GetTrackDescription( m_timeline->GetTrackParentId( id.GetInternalID() ) );
		return CreateHandle< TimelineItemDataHolder >( trackDescription, parentTrackDescription );
	}

	const Bool TimeLineTrackConstructor::CanGroupObject( const EditorObjectID& id ) const
	{
		return false;
	}

	const Bool TimeLineTrackConstructor::GroupObject( const EditorObjectID& id, const red::DynArray< Uint64 >& groupIds ) const
	{
		return false;
	}

	const Bool TimeLineTrackConstructor::CanUngroupObject( const EditorObjectID& id, red::DynArray< Uint64 >& outObjectGroups ) const
	{
		return false;
	}

	const Bool TimeLineTrackConstructor::UngroupObject( const EditorObjectID& id ) const
	{
		return false;
	}

	const Bool TimeLineTrackConstructor::SupportsContentType( const CName& contentTypeName ) const
	{
		return contentTypeName == s_type;
	}

	THandle< ISerializable > TimeLineTrackConstructor::GetData( const IObjectPersistentCollection::ObjectInfo& info ) const
	{
		return info.m_object;
	}

	//////////////////////////////////////////////////////////////////////////


	TimeLineEditableProxy::TimeLineEditableProxy( const THandle< TimelineItemDataHolder >& data )
		: m_globalId( data->GetEditorObjectID() )
		, m_data( data )
	{
	}

	Bool TimeLineEditableProxy::IsReadOnly() const
	{
		return false;
	}

	world::NodeScalingMode TimeLineEditableProxy::GetScalingMode() const
	{
		return world::NodeScalingMode::None;
	}

	Bool TimeLineEditableProxy::SupportsGizmoTransformation() const
	{
		return false;
	}

	EditorObjectID TimeLineEditableProxy::GetEditorObjectID() const
	{
		return m_globalId;
	}

	const tools::ViewClass TimeLineEditableProxy::GetViewClass() const
	{
		if ( !EnsureExistance() )
			return CName::NONE();

		return m_data->GetItem()->GetClass()->GetName();
	}

	red::SharedPtr<tools::IEditableView> TimeLineEditableProxy::CreateView( const tools::ViewClass viewClass ) const
	{
		if ( !EnsureExistance() )
			return red::SharedPtr<tools::IEditableView>();

		return m_data->GetItem()->CreateView( nullptr, IsReadOnly(), viewClass );
	}

	void TimeLineEditableProxy::ToggleSelectionVisualization( const Bool isSelected, const red::SharedPtr< SelectionState > & selection ) const
	{
	}

	Transform TimeLineEditableProxy::GetWorldTransform() const
	{
		return Transform::IDENTITY();
	}

	Bool TimeLineEditableProxy::SetWorldTransform( const Transform& newTransform, Bool preview )
	{
		return false;
	}

	Bool TimeLineEditableProxy::RestoreObject() const
	{
		return EnsureExistance();
	}

	THandle< ISerializable > TimeLineEditableProxy::GetSerializable() const
	{
		if ( !EnsureExistance() )
			return nullptr;

		return HandleFromPtr( m_data->GetItem() );
	}

	Bool TimeLineEditableProxy::EnsureExistance() const
	{
		return m_data != nullptr && m_data->GetItem() != nullptr;
	}

	TimeLineEditableProxyPtr TimeLineEditableProxy::Create( const THandle< TimelineItemDataHolder >& data )
	{
		return red::CreateSharedPtr< TimeLineEditableProxy >( data );
	}

	//////////////////////////////////////////////////////////////////////////

	TimeLineObjectResolver::TimeLineObjectResolver() 
		: m_timeline( nullptr )
		, m_constructors{ red::PoolBackend() }
	{

	}

	TimeLineObjectResolver::TimeLineObjectResolver( TimeLineInterop* timeline )
		: m_timeline( timeline->GetTimeLine() )
		, m_constructors{ red::PoolBackend() }
	{
		// create content constructors
		red::DynArray< const rtti::ClassType* > constructorClasses{ red::PoolBackend() };
		GetRttiSystem().EnumClasses( ClassID< TimeLineBaseObjectConstructor >(), constructorClasses );

		// create content constructors
		for ( const auto* constructorClass : constructorClasses )
		{
			auto* constructor = constructorClass->CreateObject< TimeLineBaseObjectConstructor >();
			if ( nullptr != constructor )
			{
				constructor->SetTimeline( m_timeline );
				m_constructors.PushBack( red::UniquePtr< TimeLineBaseObjectConstructor >( constructor ) );
			}
		}
	}

	Bool TimeLineObjectResolver::IsResolvable( const EditorObjectIDPath& path, Bool forEditing ) const
	{
		if ( path.GetDepth() != 1 )
		{
			return false;
		}

		const EditorObjectID& id = path.GetFinalChildID();
		return SupportsContentType( id.GetKind() );
	}

	Bool TimeLineObjectResolver::SupportsContentType( const CName& contentTypeName ) const
	{
		for ( const auto& ptr : m_constructors )
		{
			if ( ptr->SupportsContentType( contentTypeName ) )
				return true;
		}

		return false;
	}

	red::SharedPtr< IEditableProxy > TimeLineObjectResolver::ResolveProxy( const EditorObjectIDPath& path, const ResolvingParams& params ) const
	{
		const auto object = ResolveObject( path, params );
		if ( object )
		{
			return TimeLineEditableProxy::Create( Cast< TimelineItemDataHolder >( object ) );
		}

		return red::SharedPtr< IEditableProxy >();
	}

	THandle< ISerializable > TimeLineObjectResolver::ResolveObject( const EditorObjectIDPath& path, const ResolvingParams& params ) const
	{
		if ( path.GetDepth() != 1 )
		{
			return nullptr;
		}

		const EditorObjectID& id = path.GetFinalChildID();
		for ( const auto& ptr : m_constructors )
		{
			if ( ptr->CanResolveObject( id ) )
			{
				return ptr->ResolveObject( id );
			}
		}

		return nullptr;
	}

	Bool TimeLineObjectResolver::CanCreateObject( const ObjectCreationInfo& info, String* outReason ) const
	{
		// Make sure we have a constructor that can handle the data type
		for ( const auto& ptr : m_constructors )
		{
			if ( ptr->CanCreateObject( info ) )
			{
				return true;
			}
		}

		return false;
	}

	Bool TimeLineObjectResolver::CanDeleteObjects( const red::ArraySpan< const EditorObjectIDPath >& paths, Bool isCut, String* outFailReason ) const
	{
		for ( Uint32 i = paths.Size(); i-- > 0; )
		{
			if ( paths[i].GetDepth() != 1 )
			{
				return false;
			}

			const EditorObjectID id = paths[i].GetFinalChildID();

			Bool constructorFound = false;
			for ( const auto& ptr : m_constructors )
			{
				if ( ptr->CanDeleteObject( id ) )
				{
					constructorFound = true;
					break;
				}
			}

			if ( !constructorFound )
			{
				if ( outFailReason )
				{
					*outFailReason = "No valid object constructor found";
				}

				return false;
			}
		}

		return true;
	}

	red::SharedPtr< IObjectPreview > TimeLineObjectResolver::CreateObjectPreview( const ObjectCreationInfo& initInfo )
	{
		return red::SharedPtr< IObjectPreview >();
	}

	red::SharedPtr< IObjectPersistentCollection > TimeLineObjectResolver::CaptureObjects( const red::ArraySpan< const EditorObjectIDPath >& paths, const CapturingParams& params )
	{
		red::DynArray< THandle< ISerializable > > objectsToCapture{ red::PoolBackend() };
		for ( const EditorObjectIDPath& path : paths )
		{
			objectsToCapture.PushBack( ResolveObject( path ) );
		}

		return ObjectSerializablePersistentCollection::CaptureObjects( objectsToCapture );
	}

	red::SharedPtr< IObjectPersistentCollection > TimeLineObjectResolver::Action_CreateObjects( const red::ArraySpan< const ObjectCreationInfo > creationInfos, const CreateObjectsOutParams& out )
	{
		red::DynArray< THandle< ISerializable > > objectsCaptureList{ red::PoolBackend() };

		for ( const ObjectCreationInfo& info : creationInfos )
		{
			THandle< IBackendData > newObject;
			for ( const auto& ptr : m_constructors )
			{
				if ( ptr->CanCreateObject( info ) )
				{
					if ( ptr->CreateObject( info, m_timeline, newObject ) )
					{
						break;
					}
				}
			}

			if ( !newObject )
			{
				RED_LOG_ERROR( "TimelineObjectResolver: No constructor serviced the new object request" );
				return red::SharedPtr< IObjectPersistentCollection >();
			}

			objectsCaptureList.PushBack( newObject );
			out.actualCreatedPaths.EmplaceBack( newObject->GetEditorObjectID() );
		}

		out.selectAll = true;

		return ObjectSerializablePersistentCollection::CaptureObjects( objectsCaptureList );
	}

	Bool TimeLineObjectResolver::Action_DeleteObjects( DeleteContext& deleteContext )
	{
		for ( const EditorObjectIDPath& path : deleteContext.pathsToDelete )
		{
			if ( path.GetDepth() != 1 )
			{
				return false;
			}

			const EditorObjectID& id = path.GetFinalChildID();

			for ( const auto& ptr : m_constructors )
			{
				if ( ptr->CanDeleteObject( id ) )
				{
					if ( !ptr->DeleteObject( id, m_timeline ) )
					{
						RED_LOG_ERROR( "TimelineObjectResolver: Object for deletion not found. Make sure global ID is valid." );
						return false;
					}
				}
			}
		}

		return true;
	}

	Bool TimeLineObjectResolver::UnpackObjects( const red::SharedPtr< IObjectPersistentCollection >& objects, red::DynArray< IObjectPersistentCollection::ObjectInfo >& unpackedSerializables, Bool reuseExistingObjects )
	{
		if ( !objects )
		{
			RED_LOG_ERROR( "TimelineObjectResolver: No content to unpack objects from" );
			return false;
		}

		// unpack objects from storage
		
		const auto* serializedStorage = static_cast< ObjectSerializablePersistentCollection* >( objects.Get() );
		if ( !serializedStorage->UnpackObjects( ClassID< IBackendData >(), unpackedSerializables, reuseExistingObjects ) )
		{
			RED_LOG_ERROR( "TimelineObjectResolver: There are no objects that can be added" );
			return false;
		}

		return true;
	}

	Bool TimeLineObjectResolver::Action_RestoreObjects( const red::SharedPtr< IObjectPersistentCollection >& objects, red::DynArray< EditorObjectIDPath >& outActualCreatedPaths )
	{
		red::DynArray< IObjectPersistentCollection::ObjectInfo > unpackedSerializables{ red::PoolBackend() };
		if ( !UnpackObjects( objects, unpackedSerializables, true ) )
			return false;

		// nothing to restore
		if ( unpackedSerializables.Empty() )
			return true;

		// restore nodes back in the prefabs there were coming from
		red::DynArray< THandle< IBackendData > > placedObjects{ red::PoolBackend() };

		// reversed because objects are originally sorted from timeline events to timeline tracks. Here we are restoring objects so we need to restore tracks first.
		for ( IObjectPersistentCollection::ObjectInfo& obj : unpackedSerializables.Reverse() )
		{
			for ( const auto& ptr : m_constructors )
			{
				if ( ptr->CanRestoreObject( obj ) )
				{
					if ( ptr->RestoreObject( obj, m_timeline ) )
					{
						placedObjects.PushBack( Cast< IBackendData >( obj.m_object ) );
						break;
					}
				}
			}
		}

		// placing the nodes may have changed their IDs, output the true ones
		// Note: The order is reversed again to maintain the original order. Without this, redo would delete objects in wrong, i.e. reverse, order.
		for ( const auto& object : placedObjects.Reverse() )
		{
			outActualCreatedPaths.PushBack( EditorObjectIDPath( object->GetEditorObjectID() ) );
		}

		// all nodes must be restored for this operation to be valid
		return placedObjects.Size() == unpackedSerializables.Size();
	}

	Bool TimeLineObjectResolver::Action_PlaceObjects( const red::SharedPtr< IObjectPersistentCollection >& objects, const ObjectPlacementInfo& info, red::DynArray< EditorObjectIDPath >& outActualCreatedPaths )
	{
		red::DynArray< IObjectPersistentCollection::ObjectInfo > unpackedSerializables{ red::PoolBackend() };
		if ( !UnpackObjects( objects, unpackedSerializables, false ) )
			return false;

		// nothing to restore
		if ( unpackedSerializables.Empty() )
			return true;

		// calculate the arithmetic center of all placed objects
		Float minTime = std::numeric_limits<Float>::max();
		red::HashSet< TimelineId > placedEvents;

		for ( const auto& obj : unpackedSerializables )
		{
			for ( const auto& ptr : m_constructors )
			{
				if ( ptr->SupportsContentType( obj.m_id.GetKind() ) )
				{
					THandle< ISerializable > objectData = ptr->GetData( obj );

					const THandle< TimelineItemDataHolder > itemDesc = Cast< TimelineItemDataHolder >( objectData );
					if ( THandle< TimeLineEventDescription > event = Cast< TimeLineEventDescription >( itemDesc->GetItemHandle() ) )
					{
						minTime = math::Min( minTime, event->GetStartTime() );
						placedEvents.Insert( obj.m_id.GetInternalID() );
					}
				}
			}
		}

		Float offset = info.m_transform.GetPosition3().X - minTime;

		// restore nodes back in the prefabs there were coming from
		red::Map< EditorObjectID, THandle< IBackendData > > placedObjects;
		for ( const auto& obj : unpackedSerializables )
		{
			for ( const auto& ptr : m_constructors )
			{
				if ( ptr->CanPlaceObject( obj ) )
				{
					TimelineObjectPlacementInfo placementInfo( obj, placedObjects );
					placementInfo.m_editor = m_timeline;

					if ( info.m_useAbsoluteTransform )
					{
						placementInfo.m_timeOffset = offset;
					}

					ptr->PlaceObject( placementInfo );
					break;
				}
			}
		}

		m_timeline->SetTargetTrack( TimelineInvalidId );

		// placing the nodes may have changed their IDs, output the true ones
		for ( const auto& object : placedObjects )
			outActualCreatedPaths.PushBack( EditorObjectIDPath( object.Value()->GetEditorObjectID() ) );

		// all nodes must be restored for this operation to be valid
		return placedObjects.Size() == unpackedSerializables.Size();
	}

	red::SharedPtr< IObjectPersistentCollection > TimeLineObjectResolver::Action_ReplaceObjects( const red::ArraySpan< const EditorObjectIDPath >& objectPathsToReplace, const red::SharedPtr< IObjectPersistentCollection >& objectTemplate, red::DynArray< EditorObjectIDPath >& outActualCreatedPaths )
	{
		return red::SharedPtr< IObjectPersistentCollection >();
	}

	red::UniquePtr< IEditAction > TimeLineObjectResolver::CreateDeleteAction( ObjectHandler& objectHandler, const red::ArraySpan< const EditorObjectIDPath >& paths, const Bool isShiftModifier ) const
	{
		red::DynArray< EditorObjectIDPath > objectsToUngroup{ red::PoolBackend() };
		red::DynArray< Uint64 > groupIds{ red::PoolBackend() };

		for ( const EditorObjectIDPath& path : paths )
		{
			const EditorObjectID& id = path.GetFinalChildID();

			for ( const auto& ptr : m_constructors )
			{
				red::DynArray< Uint64 > objectGroups;
				if ( ptr->CanUngroupObject( id, objectGroups ) )
				{
					objectsToUngroup.PushBack( path );
					groupIds.PushBack( objectGroups );
					break;
				}
			}
		}

		auto baseDeleteAction = IObjectResolver::CreateDeleteAction( objectHandler, paths, isShiftModifier );

		if ( !objectsToUngroup.Empty() )
		{
			auto ungroupObjectsAction = red::CreateUniquePtr< tools::Action_UngroupObjects >( objectHandler, objectsToUngroup, groupIds );

			red::UniquePtr< EditGroupAction > groupAction = red::CreateUniquePtr< EditGroupAction >( "Delete objects" );
			groupAction->AddAction( std::move( ungroupObjectsAction ) );
			groupAction->AddAction( std::move( baseDeleteAction ) );

			return groupAction;
		}

		return baseDeleteAction;
	}

	Bool TimeLineObjectResolver::CanUngroupObjects( const red::ArraySpan< const EditorObjectIDPath >& paths, String* outReason ) const
	{
		Uint32 numObjectsToUngroup = 0;
		red::DynArray< Uint64 > groupIds{ red::PoolBackend() };

		for ( const EditorObjectIDPath& path : paths )
		{
			const EditorObjectID& id = path.GetFinalChildID();

			for ( const auto& ptr : m_constructors )
			{
				if ( ptr->CanUngroupObject( id, groupIds ) )
				{
					numObjectsToUngroup++;
					break;
				}
			}
		}

		if ( numObjectsToUngroup != paths.Size() )
		{
			*outReason = "Not all objects are grouped!";
			return false;
		}

		return true;
	}

	Bool TimeLineObjectResolver::Action_UngroupObjects( const red::ArraySpan< const EditorObjectIDPath >& paths )
	{
		Bool allUngrouped = true;
		red::DynArray< Uint64 > groupIds{ red::PoolBackend() };

		for ( const EditorObjectIDPath& path : paths )
		{
			const EditorObjectID& id = path.GetFinalChildID();

			for ( const auto& ptr : m_constructors )
			{
				if ( ptr->CanUngroupObject( id, groupIds ) )
				{
					allUngrouped = allUngrouped && ptr->UngroupObject( id );
					break;
				}
			}
		}

		return allUngrouped;
	}

	Bool TimeLineObjectResolver::Action_GroupObjects( const red::ArraySpan< const EditorObjectIDPath >& paths, const red::DynArray< Uint64 >& groupIds )
	{
		Bool allGrouped = true;

		for ( const EditorObjectIDPath& path : paths )
		{
			const EditorObjectID& id = path.GetFinalChildID();

			for ( const auto& ptr : m_constructors )
			{
				if ( ptr->CanGroupObject( id ) )
				{
					allGrouped = allGrouped && ptr->GroupObject( id, groupIds );
					break;
				}
			}
		}

		return allGrouped;
	}

	//////////////////////////////////////////////////////////////////////////

	CName TimeLineEventsGroupConstructor::s_type = RED_NAME_CONSTEXPR( "TimeLineEventsGroupDescription" );

	TimeLineEventsGroupConstructor::TimeLineEventsGroupConstructor() = default;

	TimeLineEventsGroupConstructor::TimeLineEventsGroupConstructor( ITimeLine* timeline )
		: TimeLineBaseObjectConstructor( timeline )
	{
	}

	CName TimeLineEventsGroupConstructor::GetSupportedType()
	{
		return s_type;
	}

	const Bool TimeLineEventsGroupConstructor::CanCreateObject( const ObjectCreationInfo& info, String* outReason ) const
	{
		EventsGroupCreationData data;
		if( GetTypeObject< EventsGroupCreationData >()->WriteValue( IRTTIContext::GetDefault(), &data, rtti::AccessPath(), *info.m_arguments ) )
			return true;

		return false;
	}

	const Bool TimeLineEventsGroupConstructor::CreateObject( const ObjectCreationInfo& info, ITimeLine* editor, THandle< IBackendData >& outObject ) const
	{
		if( !CanCreateObject( info ) )
			return false;

 		// create events group
		EventsGroupCreationData data;
 		GetTypeObject< EventsGroupCreationData >()->WriteValue( IRTTIContext::GetDefault(), &data, rtti::AccessPath(), *info.m_arguments );
 
 		TimelineId id = editor->CreateEventsGroup( data.m_internalType, data.m_events );
 		outObject = editor->CreateItemDataholder( id );

		return true;
	}

	const Bool TimeLineEventsGroupConstructor::CanDeleteObject( const EditorObjectID& id ) const
	{
		if( !SupportsContentType( id.GetKind() ) )
		{
			return false;
		}

 		const Bool canRemoveEventsGroup = m_timeline->CanRemoveEventsGroup( id.GetInternalID() );
 		return canRemoveEventsGroup;
	}

	const Bool TimeLineEventsGroupConstructor::DeleteObject( const EditorObjectID& id, ITimeLine* editor ) const
	{
		if( !CanDeleteObject( id ) )
			return false;

 		editor->RemoveEventsGroup( id.GetInternalID() );
 		return true;
	}

	const Bool TimeLineEventsGroupConstructor::CanRestoreObject( const IObjectPersistentCollection::ObjectInfo& info ) const
	{
		return SupportsContentType( info.m_id.GetKind() );
	}

	const Bool TimeLineEventsGroupConstructor::RestoreObject( const IObjectPersistentCollection::ObjectInfo& info, ITimeLine* editor ) const
	{
		if( !CanRestoreObject( info ) )
			return false;

		const THandle< TimelineItemDataHolder > itemDesc = Cast< TimelineItemDataHolder >( info.m_object );
		return editor->RestoreEventsGroup( Cast< TimeLineEventsGroupDescription >( itemDesc->GetItemHandle() ) );
	}

	const Bool TimeLineEventsGroupConstructor::CanResolveObject( const EditorObjectID& id ) const
	{
		return SupportsContentType( id.GetKind() );
	}

	THandle< TimelineItemDataHolder > TimeLineEventsGroupConstructor::ResolveObject( const EditorObjectID& id ) const
	{
		const auto groupDescription = m_timeline->GetEventsGroupDescription( id.GetInternalID() );
		return CreateHandle< TimelineItemDataHolder >( groupDescription, THandle< tools::TimeLineTrackBaseItem >(), red::DynArray< Uint64 >() );
	}

	const Bool TimeLineEventsGroupConstructor::CanGroupObject( const EditorObjectID& id ) const
	{
		// we cannot group EventGroup objects
		return false;
	}

	const Bool TimeLineEventsGroupConstructor::GroupObject( const EditorObjectID& id, const red::DynArray< Uint64 >& groupIds ) const
	{
		return false;
	}

	const Bool TimeLineEventsGroupConstructor::CanUngroupObject( const EditorObjectID& id, red::DynArray< Uint64 >& outObjectGroups ) const
	{
		return false;
	}

	const Bool TimeLineEventsGroupConstructor::UngroupObject( const EditorObjectID& id ) const
	{
		// we cannot group and ungroup EventGroup objects
		return false;
	}

	const Bool TimeLineEventsGroupConstructor::SupportsContentType( const CName& contentTypeName ) const
	{
		return contentTypeName == s_type;
	}

	THandle< ISerializable > TimeLineEventsGroupConstructor::GetData( const IObjectPersistentCollection::ObjectInfo& info ) const
	{
		return info.m_object;
	}

	//////////////////////////////////////////////////////////////////////////
}