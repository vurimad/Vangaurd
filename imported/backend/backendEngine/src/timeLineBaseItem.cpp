/**
* Copyright (c) 2016-2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "timeLineBaseItem.h"

#include "../../../common/redReflection/include/rttiPathParser.h"
#include "../../../common/particles/include/evaluatorColor.h"
#include "../../../common/particles/include/evaluatorFloat.h"
#include "../../../common/particles/include/evaluatorVector.h"
#include "timeLineBase.h"
#include "../../common/redReflection/include/serializableMap.h"
#include "../../common/redReflection/include/editableView.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( TimeLineTrackBaseItem, tools );
	RTTI_PARENT_TYPE( TimeLineTrackDescription );
	RTTI_PROPERTY( m_children );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( TimeLineEventItem, tools );
	RTTI_PARENT_TYPE( TimeLineEventDescription );
	RTTI_PROPERTY( m_evaluatorsPath ).editable();
	RTTI_PROPERTY( m_curveDataPath ).editable();
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( TimeLineSectorItem, tools );
	RTTI_PARENT_TYPE( TimeLineSectorDescription );
	RTTI_PROPERTY( m_children );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( TimeLineEventsGroupItem, tools );
	RTTI_PARENT_TYPE( TimeLineEventsGroupDescription );
	RTTI_PROPERTY( m_events );
RTTI_END_TYPE();

namespace tools
{
	TimeLineTrackBaseItem::TimeLineTrackBaseItem() 
		: m_timeline( nullptr )
		, m_children{ red::PoolBackend() }
	{

	}

	TimeLineTrackBaseItem::TimeLineTrackBaseItem( ITimeLine* timeline, TimelineId id, const String& type, const String& visualType )
		: TimeLineTrackDescription( id, type, visualType )
		, m_timeline( timeline )
		, m_children{ red::PoolBackend() }
	{
	}

	void TimeLineTrackBaseItem::OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& value )
	{
		m_timeline->OnItemDescriptionPropertyChange( GetId(), path );
	}

	void TimeLineTrackBaseItem::AddChild( TimelineId childId )
	{
		if ( !m_children.Exist( childId ) )
			m_children.PushBack( childId );
	}

	Int32 TimeLineTrackBaseItem::MoveChildBefore( TimelineId childId, TimelineId refChildId )
	{
		// #backendEngine_timeline_szi Consider asserting that's not the case as such request is invalid.
		if( childId == refChildId )
		{
			return -1;
		}

		// #backendEngine_timeline_szi Consider detecting that child is already where it should be and doing nothing in that case.
		m_children.Remove( childId );
		const Uint32 insertionIndex = m_children.GetIndex( refChildId );
		m_children.InsertAt( insertionIndex, childId );

		const Uint32 childIndex = insertionIndex;
		return childIndex;
	}

	Int32 TimeLineTrackBaseItem::MoveChildAfter( TimelineId childId, TimelineId refChildId )
	{
		// #backendEngine_timeline_szi Consider asserting that's not the case as such request is invalid.
		if( childId == refChildId )
		{
			return -1;
		}

		// #backendEngine_timeline_szi Consider detecting that child is already where it should be and doing nothing in that case.
		m_children.Remove( childId );
		const Uint32 insertionIndex = m_children.GetIndex( refChildId ) + 1;
		m_children.InsertAt( insertionIndex, childId );

		const Uint32 childIndex = insertionIndex;
		return childIndex;
	}

	void TimeLineTrackBaseItem::MoveChildToFront( TimelineId childId )
	{
		Uint32 destionationIndex = m_children.GetIndex( childId );
		if ( destionationIndex == -1 || destionationIndex == 0)
		{
			return;
		}

		m_children.Remove( childId );
		m_children.InsertAt( 0, childId );
	}

	void TimeLineTrackBaseItem::SortChildren( const SortFunc & pred )
	{
		std::sort( m_children.Begin(), m_children.End(), pred );
	}

	void TimeLineTrackBaseItem::RegisterTracks( red::DynArray< TimelineId >& outChildren )
	{
		if ( GetType() == TimeLineTracks::TracksGroup || GetType() == TimeLineTracks::TracksRoot )
		{
			for ( TimelineId id : m_children )
			{
				outChildren.PushBack( id );
			}

			ITimeLine* timeline = GetTimeLine();
			for ( TimelineId id : m_children )
			{
				THandle< TimeLineTrackDescription > description = timeline->GetTrackDescription( id );
				THandle< TimeLineTrackBaseItem > base = Cast< TimeLineTrackBaseItem >( description );

				base->RegisterTracks( outChildren );
			}
		}
	}

	//////////////////////////////////////////////////////////////////////////////
	
	TimeLineEventItem::TimeLineEventItem() 
		: m_timeline( nullptr )
		, m_evaluatorsPath{ red::PoolBackend() }
		, m_curveDataPath{ red::PoolBackend() }
	{

	}
	
	TimeLineEventItem::TimeLineEventItem( ITimeLine* timeline, TimelineId id, const String& type, const String& visualType )
		: TimeLineEventDescription( id, type, visualType )
		, m_timeline( timeline )
		, m_evaluatorsPath{ red::PoolBackend() }
		, m_curveDataPath{ red::PoolBackend() }
	{
	}

	Bool TimeLineEventItem::Initialize( ITimeLine* timeline, TimelineId id, const String& type, const String& visualType )
	{
		InitValues( id, type, visualType );
		m_timeline = timeline;

		return OnInitialize();
	}

	void TimeLineEventItem::InvalidateDescriptor()
	{
		m_timeline->OnInvalidateDescription( GetId() );
	}

	void TimeLineEventItem::InvalidateComponent( Uint64 componentId )
	{
		m_timeline->OnEventComponentChanged( GetId(), componentId );
	}

	void TimeLineEventItem::OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& value )
	{
		rtti::PathParser parser( path );

		CName propertyName;
		if ( parser.EatName( propertyName ) )
		{
			if( propertyName == RED_NAME_CONSTEXPR_NOREG( "startTime" ) || 
				propertyName == RED_NAME_CONSTEXPR_NOREG( "durationTime" ) || 
				propertyName == RED_NAME_CONSTEXPR_NOREG( "caption" ) || 
				propertyName == RED_NAME_CONSTEXPR_NOREG( "isMute" ) )
			{
				SyncRuntimeEvent();
			}
		}

		m_timeline->OnItemDescriptionPropertyChange( GetId(), path );

		ISerializable::OnPropertyPostChange( path, oldValue, value );
	}

	void TimeLineEventItem::OnRuntimeDataChanged( const rtti::AccessPath& path )
	{
		NotifyPropertyChanged( path );
	}

	void TimeLineEventItem::UpdateCurveData( const rtti::AccessPath& path, const Bool isCurve )
	{
		Int32 index = -1;

		for ( Uint32 i = 0; i < m_curveDataPath.Size(); ++i )
		{
			String& item = m_curveDataPath[ i ];
			if ( item == path.ToString() )
			{
				index = i;
				break;
			}
		}

		if ( !isCurve && index > -1 )
		{
			m_curveDataPath.RemoveAt( ( Uint32 ) index );
		}
		else if ( isCurve && index == -1 )
		{
			m_curveDataPath.PushBack( path.ToString() );
		}

		InvalidateComponent( Component::CurveView );
	}

	void TimeLineEventItem::UpdateCurveEntry( const rtti::AccessPath& path )
	{
		THandle< ISerializable > runtimeData = GetRuntimeData();

		if ( !runtimeData )
			return;

		CName propertyName;
		rtti::PathParser parser( path );

		while ( parser.EatName( propertyName ) )
		{
			const rtti::ClassType* classType = runtimeData->GetClass();
			const rtti::Property* prop = classType->FindProperty( propertyName );

			if ( !prop )
				return;

			if ( prop->GetType()->GetType() == RT_Class )
			{
				UpdateCurveData( path, IsCurveType( path ) );
			}

			else if ( prop->GetType()->GetType() == RT_Handle )
			{
				const rtti::IBasePointerType* propertyClassType = static_cast< const rtti::IBasePointerType* >( prop->GetType() );

				if ( propertyClassType->GetPointedType()->IsA< IEvaluator >() )
				{
					UpdateCurveData( path, IsCurveType( path ) );
				}
			}
		}
	}

	void TimeLineEventItem::RecurseCurveData( red::DynArray< const rtti::Property* >& properties, String& path )
	{
		for ( const rtti::Property* prop : properties )
		{
			const rtti::IType* propType = prop->GetType();
			if ( propType && propType->GetType() == RT_Handle )
			{
				const rtti::IBasePointerType* propertyClassType = static_cast< const rtti::IBasePointerType* >( prop->GetType() );
				
				if ( propertyClassType->GetPointedType()->IsA< IEvaluator >() )
				{
					red::StringBuilder<String> builder;
					if ( !path.Empty() )
					{
						builder.Append( path );
						builder.Append( "." );
					}
					builder.Append( prop->GetName().AsChar() );
					
					if ( IsCurveType( rtti::AccessPath( builder.ToString().AsChar() ) ) )
					{
						m_curveDataPath.PushBack( builder.ToString() );
					}

					m_evaluatorsPath.PushBack( prop->GetName().AsChar() );
				}
			}

			if ( propType->GetType() == RT_Class )
			{
 				const rtti::ClassType* classType = static_cast< const rtti::ClassType* >( prop->GetType() );

				red::DynArray< const rtti::Property* > childProperties;
				classType->GetProperties( childProperties );

				if ( !childProperties.Empty() )
				{
					RecurseCurveData( childProperties, path + prop->GetName().AsChar() );
				}
			}
		}
	}

	void TimeLineEventItem::RegisterCurveData()
	{
		THandle< ISerializable > runtimeData = GetRuntimeData();
		if ( runtimeData )
		{
			m_curveDataPath.Clear();

			const rtti::ClassType* classType = runtimeData->GetClass();

			red::DynArray< const rtti::Property* > properties;
			classType->GetProperties( properties );

			if ( !properties.Empty() )
			{
				String rootPath = String::EMPTY();
				RecurseCurveData( properties, rootPath );
			}
		}
	}

	Bool TimeLineEventItem::IsCurveType( const rtti::AccessPath& path )
	{
		THandle< ISerializable > runtimeData = GetRuntimeData();

		if ( runtimeData )
		{
			red::SharedPtr< IEditableView > view = runtimeData->CreateView();

			rtti::ValuePtr valuePtr;
			view->GetValue( IRTTIContext::GetDefault(), path, valuePtr );

			if ( valuePtr->IsHandle() )
			{
				THandle< ISerializable > curveSerializable;
				GSerializableMap->FindSerializable( FromStringDirect< SerializableID >( valuePtr->GetHandleValue() ), curveSerializable );

				if ( curveSerializable )
				{
					return IsCurveType( curveSerializable->GetClass() );
				}
			}
		}

		return false;
	}

	Bool TimeLineEventItem::IsCurveType( const rtti::ClassType* evaluatorType )
	{
		return evaluatorType->IsA< CEvaluatorColorCurve >() || evaluatorType->IsA< CEvaluatorFloatCurve >() || evaluatorType->IsA< CEvaluatorVectorCurve >();
	}

	Bool TimeLineEventItem::IsEventVisible() const
	{
		return m_timeline->IsEventVisible( GetId() );
	}

	//////////////////////////////////////////////////////////////////////////////

	TimeLineSectorItem::TimeLineSectorItem() 
		: m_timeline( nullptr )
		, m_children{ red::PoolBackend() }
	{

	}
	
	TimeLineSectorItem::TimeLineSectorItem( ITimeLine* timeline, TimelineId id, const String& type, const String& visualType )
		: TimeLineSectorDescription( id, type, visualType )
		, m_timeline( timeline )
		, m_children{ red::PoolBackend() }
	{
	}

	void TimeLineSectorItem::OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& value )
	{
		m_timeline->OnItemDescriptionPropertyChange( GetId(), path );
	}

	void TimeLineSectorItem::AddChild( TimelineId childId )
	{
		if ( !m_children.Exist( childId ) )
			m_children.PushBack( childId );
	}

	//////////////////////////////////////////////////////////////////////////////

	TimeLineEventsGroupItem::TimeLineEventsGroupItem( ITimeLine* timeline, TimelineId id, String type )
		: TimeLineEventsGroupDescription( id, std::move( type ), "default" )
		, m_timeline( timeline )
	{}

	void TimeLineEventsGroupItem::OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& value )
	{
		m_timeline->OnItemDescriptionPropertyChange( GetId(), path );
	}

	void TimeLineEventsGroupItem::AddEvent( TimelineId eventId )
	{
		if( !m_events.Exist( eventId ) )
		{
			const THandle< TimeLineEventDescription > event = GetTimeLine()->GetEventDescription( eventId );
			event->BindToEventsGroup( GetId() );

			m_events.PushBack( eventId );
		}
	}

	void TimeLineEventsGroupItem::RemoveEvent( TimelineId eventId )
	{
		const THandle< TimeLineEventDescription > event = GetTimeLine()->GetEventDescription( eventId );
		event->UnbindFromEventsGroup( GetId() );
		m_events.Remove( eventId );
	}
}