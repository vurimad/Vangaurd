/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "packageErrorReporter.h"
#include "rttiClass.h"
#include "serializable.h"

namespace red
{
	PackageErrorReporter::PackageErrorReporter()
		: m_propertyMissingContainer{ red::PoolDebug() }
		, m_propertyMismatchContainer{ red::PoolDebug() }
		, m_propertyUnkownContainer{ red::PoolDebug() }
		, m_hasErrors{}
	{}

	PackageErrorReporter::~PackageErrorReporter()
	{}

	void PackageErrorReporter::ReportPropertyMissing( PropertyMissingContext&& context )
	{
		if( m_currentObject )
		{
			m_hasErrors = true;
			m_propertyMissingContainer.PushBack( std::make_pair( m_currentObject, std::move( context ) ) );
		}
	}

	void PackageErrorReporter::ReportPropertyMismatch( PropertyMismatchContext&& context )
	{
		if( m_currentObject )
		{
			m_hasErrors = true;
			m_propertyMismatchContainer.PushBack( std::make_pair( m_currentObject, std::move( context ) ) );
		}
	}

	void PackageErrorReporter::ReportMissingSerializer( const rtti::IType* type )
	{
		RED_LOG_ERROR( "Package: Cannot serialize %hs. No serializer could be found.", type->GetName().ToDebugString() );
	}

	void PackageErrorReporter::ReportPropertyUnknown( const PropertyUnknownContext& context )
	{
		m_hasErrors = true;
		m_propertyUnkownContainer.PushBack( context ); // ctremblay: should be broadcaster only once per object.
	}

	void PackageErrorReporter::BroadcastAllReportedErrors()
	{
		if( !m_hasErrors )
		{
			return;
		}

		for( auto iter = m_propertyMissingContainer.Begin(), end = m_propertyMissingContainer.End(); iter != end; ++iter )
		{
			const THandle< ISerializable > object = iter->first;
			const PropertyMissingContext& context = iter->second;
			if( !object->OnPropertyMissing( context.propertyName, context.propertyData ) )
			{
				// ctremblay: Should we log something ? Data was not recovered and will be lost if resource is saved.
				RED_LOG_ERROR( "Data was not recovered and will be lost if resource is saved. (OnPropertyMissing)" );
			}
		}

		m_propertyMissingContainer.Clear();

		for( auto iter = m_propertyMismatchContainer.Begin(), end = m_propertyMismatchContainer.End(); iter != end; ++iter )
		{
			const THandle< ISerializable > object = iter->first;
			const PropertyMismatchContext& context = iter->second;

			if( !object->OnPropertyTypeMismatch( context.propertyName, context.property, context.propertyData ) )
			{
				// ctremblay: Should we log something ? Data was not recovered and will be lost if resource is saved.
				RED_LOG_ERROR( "Data was not recovered and will be lost if resource is saved. (OnPropertyTypeMismatch)" );
			}
		}

		m_propertyMismatchContainer.Clear();

		for( auto iter = m_propertyUnkownContainer.Begin(), end = m_propertyUnkownContainer.End(); iter != end; ++iter )
		{
			const PropertyUnknownContext& context = *iter;
			if( context.objectType )
			{
				RED_LOG_ERROR( "Package: Object '%hs' cannot serialize property '%hs'. Property of type '%hs' could not be found.",
							   context.objectType->GetName().ToDebugString(),
							   context.propertyName.ToDebugString(),
							   context.propertyTypeName.ToDebugString() );
			}
		}

		m_propertyUnkownContainer.Clear();
		m_hasErrors = false;
	}

	void PackageErrorReporter::SetCurrentObject( const THandle< ISerializable > object )
	{
		m_currentObject = object;
	}
}
