/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "serializableCollector.h"
#include "../../redFileSystem/include/nullFile.h"

SerializableCollector::SerializableCollector()
{
}

SerializableCollector::~SerializableCollector()
{
}

void SerializableCollector::Run( const THandle< ISerializable >& serializable )
{
	m_visitedObjects.Insert( serializable.Get() );
	m_handles.PushBack( serializable );

	CNullFileWriter file;
	file.m_mapper = this;
	file.m_flags |= FF_Mapper;
	serializable->OnSerialize( file );
}

void SerializableCollector::GetCollectedIDs( red::DynArray< SerializableID >& outIDs ) const
{
	for ( const auto& object : m_handles )
	{
		outIDs.PushBack( object->GetID() );
	}
}

red::ArraySpan< const THandle< ISerializable > > SerializableCollector::GetCollectedHandles() const
{
	return m_handles;
}

void SerializableCollector::MapPointer( const THandle< ISerializable >& objectRef, ObjectIndex& outIndex )
{
	if ( objectRef && !m_visitedObjects.Exist( objectRef.Get() ) )
	{
		m_visitedObjects.Insert( objectRef.Get() );
		m_handles.PushBack( objectRef );

		CNullFileWriter file;
		file.m_mapper = this;
		file.m_flags |= FF_Mapper;
		objectRef->OnSerialize( file );
	}
}