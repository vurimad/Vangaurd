/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "streamedResource.h"
#include "streamedResourceManager.h"
#include "rttiPathParser.h"
#include "resourceLoader.h"
#include "rttiAccessPath.h"
#include "mathVector3.h"

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( StreamedResource, res );
	RTTI_PARENT_TYPE( CResource );
RTTI_END_TYPE();

namespace res
{

	StreamingData::StreamingData()
		: m_boundingBox( Box::EMPTY() )
		, m_visibleBoundingBox( Box::EMPTY() )
		, m_surfaceAreaPerAxis( -1.0f, -1.0f, -1.0f ) // not computed
		, m_streamingRefPosition( math::Vector3::ZEROS() )
		, m_streamingDistance( 0.0f )
		, m_autoHideDistance( 0.0f )
		, m_proxyHash( 0 )
		, m_triangleCount( 0 )
	{
	}

	Bool StreamingData::operator == ( const StreamingData& other ) const
	{
		return red::Memcmp( this, &other, sizeof( StreamingData ) ) == 0;
	}

	Bool StreamingData::operator != ( const StreamingData& other ) const
	{
		return !operator == ( other );
	}

	void StreamedResource::OnStreamingDataChanged( const StreamingData& streamingData )
	{
		RED_FATAL_ASSERT( streamingData.m_streamingDistance > 0.0f, "Invalid streaming data" );

		auto streamingManager = GetStreamingResourceManager();
		const res::ResourcePath& path = GetPath();
		if( streamingManager && path.IsValid() )
		{
			// let the streaming manager know about the change
			streamingManager->NotifyListenersStreamingDataChanged( path, streamingData );
		}
	}

	void StreamedResource::NotifyStreamingDataChanged()
	{
		// NOTE: the following code is needed only in the editor
		auto streamingManager = GetStreamingResourceManager();
		const res::ResourcePath& path = GetPath();
		if ( streamingManager && path.IsValid() )
		{
			// let the streaming manager know about the change
			streamingManager->RefreshStreamingData( path );
		}
	}

	StreamingData StreamedResource::GetStreamingData() const
	{
		// NOTE: the following code is needed only in the editor
		auto streamingManager = GetStreamingResourceManager();
		const res::ResourcePath& path = GetPath();
		if( streamingManager && path.IsValid() )
		{
			// let the streaming manager know about the distance change
			return streamingManager->GetStreamingData( path );
		}

		return StreamingData();
	}

	void StreamedResource::GetCustomEditableProperties( rtti::EditableProperties& outRootProperties ) const
	{
		rtti::ClassEditablePropertyInfo info;

		TBaseClass::GetCustomEditableProperties( outRootProperties );

		info.m_category = RED_NAME_CONSTEXPR( "StreamingData" );
		info.m_isReadOnly = true;

		info.m_name = RED_NAME_CONSTEXPR( "streamingBoundingBox" );
		info.m_type = GetTypeObject< Box >();
		outRootProperties.Add( info );

		info.m_name = RED_NAME_CONSTEXPR( "streamingDistance" );
		info.m_type = GetTypeObject< Float >();
		outRootProperties.Add( info );

		info.m_name = RED_NAME_CONSTEXPR( "streamingReferencePosition" );
		info.m_type = GetTypeObject< Vector3 >();
		outRootProperties.Add( info );

		info.m_name = RED_NAME_CONSTEXPR( "triangleCount" );
		info.m_type = GetTypeObject< Uint64 >();
		outRootProperties.Add( info );
	}

	Bool StreamedResource::ReadCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, red::SharedPtr< rtti::ValueHolder >& outValue ) const
	{
		if ( TBaseClass::ReadCustomEditableProperty( ctx, propertyName, restOfThePath, outValue ) )
		{
			return true;
		}

		const StreamingData streamingData = GetStreamingData();

		if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "streamingBoundingBox" ) )
		{
			const Box value = streamingData.m_boundingBox;
			return GetTypeObject< Box >()->ReadValue( ctx, &value, restOfThePath, outValue );
		}
		else if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "streamingDistance" ) )
		{
			const Float value = streamingData.m_streamingDistance;
			return GetTypeObject< Float >()->ReadValue( ctx, &value, restOfThePath, outValue );
		}
		else if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "streamingReferencePosition" ) )
		{
			const Vector3 value = streamingData.m_streamingRefPosition;
			return GetTypeObject< Vector3 >()->ReadValue( ctx, &value, restOfThePath, outValue );
		}
		else if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "triangleCount" ) )
		{
			const Uint64 value = streamingData.m_triangleCount;
			return GetTypeObject< Uint64 >()->ReadValue( ctx, &value, restOfThePath, outValue );
		}

		return false;
	}

} // res
