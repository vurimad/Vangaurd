/**
* Copyright (c) 2017-2020 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "editableProxy.h"
#include "timelineTypes.h"
#include "objectResolver.h"
#include "objectPersistentCollection.h"

class IBackendData;

namespace tools
{
	class IEditAction;
	class ITimeLine;
	class TimeLineInterop;
	class TimelineItemDataHolder;
	
	struct BACKEND_ENGINE_API TimelineObjectPlacementInfo
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

		TimelineObjectPlacementInfo( const IObjectPersistentCollection::ObjectInfo& info, red::Map< EditorObjectID, THandle< IBackendData > >& outObjects )
			: m_info( info )
			, m_outObjects( outObjects )
			, m_editor( nullptr )
			, m_timeOffset( 0 )
		{
		}

		const IObjectPersistentCollection::ObjectInfo& m_info;
		red::Map< EditorObjectID, THandle< IBackendData > >& m_outObjects;

		ITimeLine* m_editor;
		Float m_timeOffset;
	};

	class BACKEND_ENGINE_API TimeLineBaseObjectConstructor : private red::NonCopyable
	{
		RTTI_DECLARE_TYPE( TimeLineBaseObjectConstructor )
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		TimeLineBaseObjectConstructor() { }
		TimeLineBaseObjectConstructor( ITimeLine* timeline ) : m_timeline( timeline ) { }
		virtual ~TimeLineBaseObjectConstructor() {}

		virtual const Bool CanCreateObject( const ObjectCreationInfo& info, String* outReason = nullptr ) const = 0;
		virtual const Bool CreateObject( const ObjectCreationInfo& info, ITimeLine* editor, THandle< IBackendData >& outObject ) const = 0;

		virtual const Bool CanDeleteObject( const EditorObjectID& id ) const = 0;
		virtual const Bool DeleteObject( const EditorObjectID& id, ITimeLine* editor ) const = 0;

		virtual const Bool CanRestoreObject( const IObjectPersistentCollection::ObjectInfo& info ) const = 0;
		virtual const Bool RestoreObject( const IObjectPersistentCollection::ObjectInfo& info, ITimeLine* editor ) const = 0;

		virtual const Bool CanPlaceObject( const IObjectPersistentCollection::ObjectInfo& info ) const = 0;
		virtual const Bool PlaceObject( const TimelineObjectPlacementInfo& info ) const = 0;

		virtual const Bool CanResolveObject( const EditorObjectID& id ) const = 0;
		virtual THandle< TimelineItemDataHolder > ResolveObject( const EditorObjectID& id ) const = 0;

		virtual const Bool CanGroupObject( const EditorObjectID& id ) const = 0;
		virtual const Bool GroupObject( const EditorObjectID& id, const red::DynArray< Uint64 >& groupIds ) const = 0;

		virtual const Bool CanUngroupObject( const EditorObjectID& id, red::DynArray< Uint64 >& outObjectGroups ) const = 0;
		virtual const Bool UngroupObject( const EditorObjectID& id ) const = 0;

		virtual const Bool SupportsContentType( const CName& contentTypeName ) const = 0;

		// this will return ISerializable object with data associated with this item, where ResolveObject may return item wrapper, for example ClipboardDataHolder for copy/paste/delete/restore operations.
		virtual THandle< ISerializable > GetData( const IObjectPersistentCollection::ObjectInfo& info ) const = 0;

		void SetTimeline( ITimeLine* timeline ) { m_timeline = timeline; }

	protected:
		ITimeLine* m_timeline;
	};

	struct EventCreationData
	{
		RTTI_DECLARE_TYPE( EventCreationData )
		RED_USE_MEMORY_POOL( red::PoolBackend );

		TimelineId m_trackId;
		String m_type;
		String m_caption;
		Float m_time;
	};

	class BACKEND_ENGINE_API TimeLineEventConstructor : public TimeLineBaseObjectConstructor
	{
		RTTI_DECLARE_TYPE( TimeLineEventConstructor )

	public:
		TimeLineEventConstructor();
		TimeLineEventConstructor( ITimeLine* timeline );

		static CName GetSupportedType();

		virtual const Bool CanCreateObject( const ObjectCreationInfo& info, String* outReason = nullptr ) const override;
		virtual const Bool CreateObject( const ObjectCreationInfo& info, ITimeLine* timeline, THandle< IBackendData >& outObject ) const override;

		virtual const Bool CanDeleteObject( const EditorObjectID& id ) const override;
		virtual const Bool DeleteObject( const EditorObjectID& id, ITimeLine* timeline ) const override;

		virtual const Bool CanRestoreObject( const IObjectPersistentCollection::ObjectInfo& info ) const override;
		virtual const Bool RestoreObject( const IObjectPersistentCollection::ObjectInfo& info, ITimeLine* timeline ) const override;

		virtual const Bool CanPlaceObject( const IObjectPersistentCollection::ObjectInfo& info ) const override;
		virtual const Bool PlaceObject( const TimelineObjectPlacementInfo& info ) const override;

		virtual const Bool CanResolveObject( const EditorObjectID& id ) const override;
		virtual THandle< TimelineItemDataHolder > ResolveObject( const EditorObjectID& id ) const override;

		virtual const Bool CanGroupObject( const EditorObjectID& id ) const override;
		virtual const Bool GroupObject( const EditorObjectID& id, const red::DynArray< Uint64 >& groupIds ) const override;

		virtual const Bool CanUngroupObject( const EditorObjectID& id, red::DynArray< Uint64 >& outObjectGroups ) const override;
		virtual const Bool UngroupObject( const EditorObjectID& id ) const override;

		virtual const Bool SupportsContentType( const CName& contentTypeName ) const override;

		virtual THandle< ISerializable > GetData( const IObjectPersistentCollection::ObjectInfo& info ) const override;

	protected:
		void AddToEventsGroup( const THandle< TimeLineEventDescription >& eventDesc, const TimelineId groupId ) const;
		void RemoveFromEventsGroup( const THandle< TimeLineEventDescription >& eventDesc ) const;

	private:
		static CName s_type;
	};

	struct TrackCreationData
	{
		RTTI_DECLARE_TYPE( TrackCreationData )
		RED_USE_MEMORY_POOL( red::PoolBackend );

		TimelineId m_parentId = TimelineInvalidId;
		String m_type;
		String m_caption;
		TimelineId m_previousId = TimelineInvalidId;
	};

	class BACKEND_ENGINE_API TimeLineTrackConstructor : public TimeLineBaseObjectConstructor
	{
		RTTI_DECLARE_TYPE( TimeLineTrackConstructor )

	public:
		TimeLineTrackConstructor();
		TimeLineTrackConstructor( ITimeLine* timeline );

		static CName GetSupportedType();

		virtual const Bool CanCreateObject( const ObjectCreationInfo& info, String* outReason = nullptr ) const override;
		virtual const Bool CreateObject( const ObjectCreationInfo& info, ITimeLine* editor, THandle< IBackendData >& outObject ) const override;

		virtual const Bool CanDeleteObject( const EditorObjectID& id ) const override;
		virtual const Bool DeleteObject( const EditorObjectID& id, ITimeLine* editor ) const override;

		virtual const Bool CanRestoreObject( const IObjectPersistentCollection::ObjectInfo& info ) const override;
		virtual const Bool RestoreObject( const IObjectPersistentCollection::ObjectInfo& info, ITimeLine* editor ) const override;

		virtual const Bool CanPlaceObject( const IObjectPersistentCollection::ObjectInfo& info ) const override;
		virtual const Bool PlaceObject( const TimelineObjectPlacementInfo& info ) const override;

		virtual const Bool CanResolveObject( const EditorObjectID& id ) const override;
		virtual THandle< TimelineItemDataHolder > ResolveObject( const EditorObjectID& id ) const override;

		virtual const Bool CanGroupObject( const EditorObjectID& id ) const override;
		virtual const Bool GroupObject( const EditorObjectID& id, const red::DynArray< Uint64 >& groupIds ) const override;

		virtual const Bool CanUngroupObject( const EditorObjectID& id, red::DynArray< Uint64 >& outObjectGroups ) const override;
		virtual const Bool UngroupObject( const EditorObjectID& id ) const override;

		virtual const Bool SupportsContentType( const CName& contentTypeName ) const override;

		virtual THandle< ISerializable > GetData( const IObjectPersistentCollection::ObjectInfo& info ) const override;

	private:
		static CName s_type;
	};

	class TimeLineEditableProxy;
	using TimeLineEditableProxyPtr = red::SharedPtr< TimeLineEditableProxy >;

	class BACKEND_ENGINE_API TimeLineEditableProxy : public IEditableProxy
	{
	public:
		TimeLineEditableProxy( const THandle< TimelineItemDataHolder >& data );
		virtual Bool IsReadOnly() const override final;
		virtual world::NodeScalingMode GetScalingMode() const override final;
		virtual Bool SupportsGizmoTransformation() const override final;
		virtual tools::EditorObjectID GetEditorObjectID() const override final;
		virtual const tools::ViewClass GetViewClass() const override final;
		virtual red::SharedPtr<tools::IEditableView> CreateView( const tools::ViewClass viewClass ) const override final;
		virtual void ToggleSelectionVisualization( const Bool isSelected, const red::SharedPtr< SelectionState > & selection ) const override final;
		virtual Transform GetWorldTransform() const override final;
		virtual Bool SetWorldTransform( const Transform& newTransform, Bool preview = false ) override final;
		virtual Bool RestoreObject() const override final;
		virtual THandle< ISerializable > GetSerializable() const override final;

		// create editable proxy for backend data
		static TimeLineEditableProxyPtr Create( const THandle< TimelineItemDataHolder >& data );

	private:
		Bool EnsureExistance() const;

		tools::EditorObjectID m_globalId;
		mutable THandle< TimelineItemDataHolder > m_data;
	};

	class BACKEND_ENGINE_API TimeLineObjectResolver : public IObjectResolver, private red::NonCopyable
	{
		RTTI_DECLARE_TYPE( TimeLineObjectResolver );

	public:
		TimeLineObjectResolver();
		TimeLineObjectResolver( TimeLineInterop* timeline );

		/// IObjectResolver interface
		virtual Bool IsResolvable( const EditorObjectIDPath& path, Bool forEditing ) const override final;
		virtual Bool SupportsContentType( const CName& contentTypeName ) const override final;
		virtual Bool CanCreateObject( const ObjectCreationInfo& info, String* outReason = nullptr ) const override final;
		virtual Bool CanDeleteObjects( const red::ArraySpan< const EditorObjectIDPath >& paths, Bool isCut, String* outFailReason = nullptr ) const override final;
		virtual red::SharedPtr< IEditableProxy > ResolveProxy( const EditorObjectIDPath& path, const ResolvingParams& params = ResolvingParams() ) const override final;
		virtual THandle< ISerializable > ResolveObject( const EditorObjectIDPath& path, const ResolvingParams& params = ResolvingParams() ) const override final;
		virtual red::SharedPtr< IObjectPreview > CreateObjectPreview( const ObjectCreationInfo& initInfo ) override final;
		virtual red::SharedPtr< IObjectPersistentCollection > CaptureObjects( const red::ArraySpan< const EditorObjectIDPath >& paths, const CapturingParams& params = CapturingParams() ) override final;
		virtual red::SharedPtr< IObjectPersistentCollection > Action_CreateObjects( const red::ArraySpan< const ObjectCreationInfo > creationInfos, const CreateObjectsOutParams& out ) override final;
		virtual Bool Action_DeleteObjects( DeleteContext& deleteContext ) override final;
		virtual Bool Action_RestoreObjects( const red::SharedPtr< IObjectPersistentCollection >& objects, red::DynArray< EditorObjectIDPath >& outActualCreatedPaths ) override final;
		virtual Bool Action_PlaceObjects( const red::SharedPtr< IObjectPersistentCollection >& objects, const ObjectPlacementInfo& info, red::DynArray< EditorObjectIDPath >& outActualCreatedPaths ) override final;
		virtual red::SharedPtr< IObjectPersistentCollection > Action_ReplaceObjects( const red::ArraySpan< const EditorObjectIDPath >& objectPathsToReplace, const red::SharedPtr< IObjectPersistentCollection >& objectTemplate, red::DynArray< EditorObjectIDPath >& outActualCreatedPaths ) override final;
		virtual red::UniquePtr< IEditAction > CreateDeleteAction( ObjectHandler& objectHandler, const red::ArraySpan< const EditorObjectIDPath >& paths, const Bool isShiftModifier ) const override final;

		virtual Bool CanUngroupObjects( const red::ArraySpan< const EditorObjectIDPath >& paths, String* outReason = nullptr ) const override final;
		virtual Bool Action_UngroupObjects( const red::ArraySpan< const EditorObjectIDPath >& paths ) override final;
		virtual Bool Action_GroupObjects( const red::ArraySpan< const EditorObjectIDPath >& paths, const red::DynArray< Uint64 >& groupIds ) override final;

	private:
		Bool UnpackObjects( const red::SharedPtr< IObjectPersistentCollection >& objects, red::DynArray< IObjectPersistentCollection::ObjectInfo >& unpackedSerializables, Bool reuseExistingObjects );

		ITimeLine* m_timeline;

		red::DynArray< red::UniquePtr< TimeLineBaseObjectConstructor > > m_constructors;
	};

	struct EventsGroupCreationData
	{
		RTTI_DECLARE_TYPE( EventsGroupCreationData )
		RED_USE_MEMORY_POOL( red::PoolBackend );

		String m_internalType;
		red::DynArray< TimelineId > m_events{ red::PoolBackend() };
	};

	class BACKEND_ENGINE_API TimeLineEventsGroupConstructor: public TimeLineBaseObjectConstructor
	{
		RTTI_DECLARE_TYPE( TimeLineEventsGroupConstructor )
		RED_BASE_CLASS( TimeLineBaseObjectConstructor )
	public:
		TimeLineEventsGroupConstructor();
		TimeLineEventsGroupConstructor( ITimeLine* timeline );

		static CName GetSupportedType();

		virtual const Bool CanCreateObject( const ObjectCreationInfo& info, String* outReason = nullptr ) const override;
		virtual const Bool CreateObject( const ObjectCreationInfo& info, ITimeLine* editor, THandle< IBackendData >& outObject ) const override;

		virtual const Bool CanDeleteObject( const EditorObjectID& id ) const override;
		virtual const Bool DeleteObject( const EditorObjectID& id, ITimeLine* editor ) const override;

		virtual const Bool CanPlaceObject( const IObjectPersistentCollection::ObjectInfo& info ) const override { return false; }
		virtual const Bool PlaceObject( const TimelineObjectPlacementInfo& info ) const override { return false; }

		virtual const Bool CanRestoreObject( const IObjectPersistentCollection::ObjectInfo& info ) const override;
		virtual const Bool RestoreObject( const IObjectPersistentCollection::ObjectInfo& info, ITimeLine* editor ) const override;

		virtual const Bool CanResolveObject( const EditorObjectID& id ) const override;
		virtual THandle< TimelineItemDataHolder > ResolveObject( const EditorObjectID& id ) const override;

		virtual const Bool CanGroupObject( const EditorObjectID& id ) const override;
		virtual const Bool GroupObject( const EditorObjectID& id, const red::DynArray< Uint64 >& groupIds ) const override;

		virtual const Bool CanUngroupObject( const EditorObjectID& id, red::DynArray< Uint64 >& outObjectGroups ) const override;
		virtual const Bool UngroupObject( const EditorObjectID& id ) const override;

		virtual const Bool SupportsContentType( const CName& contentTypeName ) const override;

		virtual THandle< ISerializable > GetData( const IObjectPersistentCollection::ObjectInfo& info ) const override;

	private:
		static CName s_type;
	};
}