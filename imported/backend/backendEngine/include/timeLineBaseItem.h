/**
* Copyright (c) 2016-2019 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../../common/redReflection/include/types.h"
#include "../../../common/redContainers/include/dynArray.h"

#include "timeLineTypes.h"

namespace tools
{
	class ITimeLine;

	class BACKEND_ENGINE_API TimeLineTrackBaseItem : public TimeLineTrackDescription
	{
		RTTI_DECLARE_TYPE( TimeLineTrackBaseItem )
	public:
		TimeLineTrackBaseItem();
		TimeLineTrackBaseItem( ITimeLine* timeline, TimelineId id, const String& type, const String& visualType );

		ITimeLine* GetTimeLine() const { return m_timeline; }
		void SetTimeline( ITimeLine* timeline ) { m_timeline = timeline; }

		void AddChild( TimelineId childId );
		void RemoveChild( TimelineId childId ) { m_children.Remove( childId ); }
		Bool HasChild( TimelineId childId ) const { return m_children.Exist( childId ); }
		Bool HasChildren() const { return m_children.Size() != 0; }
		void GetChildren( red::DynArray< TimelineId >& outChildren ) const { outChildren = m_children; }

		void MoveChildToFront( TimelineId trackId );
		Int32 MoveChildBefore( TimelineId childId, TimelineId refChildId );
		Int32 MoveChildAfter( TimelineId childId, TimelineId refChildId );

		using SortFunc = std::function< Bool( TimelineId, TimelineId ) >;
		void SortChildren( const SortFunc& pred );

		void RegisterTracks( red::DynArray< TimelineId >& outChildren );

		virtual void OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& value ) override;

		//
		// Called by TimeLineBase implementation and informs runtime code that something might happened to the resource
		//

		virtual void OnItemCreated( const String& typeName, TimelineId parentId ) { }
		virtual void OnItemAttachedToParent( TimelineId oldParentId, TimelineId newParentId ) { }
		virtual void OnItemRemoved() { }

		virtual void OnChildItemReordered( TimelineId childId, Uint32 newIndex ) { }

		virtual THandle< ISerializable > GetRuntimeData() { return nullptr; }

	protected:
		const red::DynArray< TimelineId >& GetChildren() const { return m_children; }

	private:
		ITimeLine* m_timeline;
		red::DynArray< TimelineId > m_children; // Note: Children of root/group track are tracks themselves. Children or regular track are events.
	};

	///
	///
	///

	typedef TimeLineTrackBaseItem TimeLineTrackItem;
	typedef TimeLineTrackBaseItem TimeLineTracksGroupItem;

	class BACKEND_ENGINE_API TimeLineEventItem : public TimeLineEventDescription
	{
		RTTI_DECLARE_TYPE( TimeLineEventItem )
	public:
		TimeLineEventItem();
		TimeLineEventItem( ITimeLine* timeline, TimelineId id, const String& type, const String& visualType );

		Bool Initialize( ITimeLine* timeline, TimelineId id, const String& type, const String& visualType );
		void InvalidateDescriptor();

		enum Component
		{
			CurveView = 0,
			PointMarkers
			// Below 100 are reserved
		};

		void InvalidateComponent( Uint64 componentId );

		ITimeLine* GetTimeLine() const { return m_timeline; }
		void SetTimeline( ITimeLine* timeline ) { m_timeline = timeline; }

		virtual THandle< ISerializable > GetRuntimeData() { return nullptr; }

		const red::DynArray< String >& GetCurveData() const { return m_curveDataPath; }
		const red::DynArray< String >& GetEvaluatorsPath() const { return m_evaluatorsPath; }

		// TODO: remove me
		virtual Bool IsEventVisible() const;

		virtual void OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& value ) override;

		///
		/// RUNTIME EVENTS SUPPORT
		///

		// Called by TimeLineBase implementation

		// Called after the event is created. You can create your runtime event here
		virtual void OnItemCreated( const String& typeName, TimelineId parentId, CRUID ruid ) { }
		// Called after the event is attached to parent or attached to new parent. If you have a runtime track, rebind your runtime event to new runtime track.
		// Before changing parent the TimelineBase::CanEventBindToTrack is called where you can reject this action.
		virtual void OnItemAttachedToParent( TimelineId oldParentId, TimelineId newParentId ) { }
		// Remove runtime event
		virtual void OnItemRemoved() { }
		// Restore your runtime event eg. add runtime event to the resource again/create it and return true if successes.
		virtual Bool OnItemRestored() override { return false; }
		// after copy action the event was copied (by RTTI) and you may attach it to the resource. If not possible return false (See TimeLineBase::PlaceEvent for details)
		virtual Bool OnItemPlace( TimelineId oldEventId, TimelineId newParentId ) override { return false; }

		// An user modified runtime data by using property page
		virtual void OnRuntimeDataChanged( const rtti::AccessPath& path );
		///
		///
		///

		// check if runtime data has curve evaluators and fill information about them into m_curveDataMapping
		void RegisterCurveData();

	protected:
		virtual Bool OnInitialize() { return true; }; // #backendEngine_timeline_szi Watch for this vs TimeLineItemDescription::OnInitialized().

		void UpdateCurveEntry( const rtti::AccessPath& path );
		Bool IsCurveType( const rtti::ClassType* evaluatorType );
		Bool IsCurveType( const rtti::AccessPath& path ); 
		void UpdateCurveData( const rtti::AccessPath& path, const Bool isCurve );

		Bool HasEvaluators() const { return !m_evaluatorsPath.Empty(); }

	private:
		void RecurseCurveData( red::DynArray< const rtti::Property* >& properties, String& path );
		
		ITimeLine* m_timeline;

		red::DynArray< String > m_evaluatorsPath;
		red::DynArray< String > m_curveDataPath;
	};

	///
	///
	///

	class BACKEND_ENGINE_API TimeLineSectorItem : public TimeLineSectorDescription
	{
		RTTI_DECLARE_TYPE( TimeLineSectorItem )
	public:
		TimeLineSectorItem();
		TimeLineSectorItem( ITimeLine* timeline, TimelineId id, const String& type, const String& visualType );

		ITimeLine* GetTimeLine() const { return m_timeline; }
		void SetTimeline( ITimeLine* timeline ) { m_timeline = timeline; }
		
		virtual void OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& value ) override;

		void AddChild( TimelineId childId );
		void RemoveChild( TimelineId childId ) { m_children.Remove( childId ); }
		Bool HasChildren() const { return m_children.Size() != 0; }
		void GetChildren( red::DynArray< TimelineId >& outChildren ) { outChildren = m_children; }

		// Called by TimeLineBase implementation
		virtual void OnItemCreated( const String& typeName ) { }
		virtual void OnItemRemoved() { }

	private:
		ITimeLine* m_timeline;
		red::DynArray< TimelineId > m_children;
	};

	///
	///
	///

	class BACKEND_ENGINE_API TimeLineEventsGroupItem: public TimeLineEventsGroupDescription
	{
		RTTI_DECLARE_TYPE( TimeLineEventsGroupItem )
		RED_BASE_CLASS( TimeLineEventsGroupDescription )
	public:
		TimeLineEventsGroupItem() = default;
		TimeLineEventsGroupItem( ITimeLine* timeline, TimelineId id, String type );

		ITimeLine* GetTimeLine() const { return m_timeline; }
		void SetTimeline( ITimeLine* timeline ) { m_timeline = timeline; }

		virtual void OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& value ) override;

		void AddEvent( TimelineId eventId );
		void RemoveEvent( TimelineId eventId );
		Bool HasEvents() const { return m_events.Size() != 0; }
		const red::DynArray< TimelineId >& GetEvents() const { return m_events; };

		void BindEvents( const red::DynArray< TimelineId >& events ) { m_events = events; }

		// Called by TimeLineBase implementation
		virtual void OnItemCreated() { }
		virtual void OnItemRemoved() { }

		virtual void OnEventAdded( TimelineId eventId ) { }
		virtual void OnEventRemoved( TimelineId eventId ) { }

	private:
		ITimeLine* m_timeline = nullptr;

		red::DynArray< TimelineId > m_events{ red::PoolBackend() };
	};
}