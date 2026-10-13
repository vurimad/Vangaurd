/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "serializationMapping.h"

class RED_REFLECTION_API SerializableCollector : public serialization::IMapper
{
public:	
	SerializableCollector();
	virtual ~SerializableCollector();

	// crawl through the specified object's references and pointers
	void Run( const THandle< ISerializable >& serializable );

	// outputs all collected IDs
	void GetCollectedIDs( red::DynArray< SerializableID >& outIDs ) const;

	// Gets all of the collected handles in the order which they were collected
	red::ArraySpan< const THandle< ISerializable > > GetCollectedHandles() const;

protected:
	virtual void MapName( const CName& name, NameIndex& outIndex ) override {};
	virtual void MapType( const rtti::IType* rttiType, TypeIndex& outIndex ) override {};
	virtual void MapPointer( const THandle< ISerializable >& objectRef, ObjectIndex& outIndex ) override;
	virtual void MapResourceReference( const res::ResourcePath& path, PathIndex& outIndex ) override {};
	virtual void MapResourceDeferredReference( const res::ResourcePath& path, PathIndex& outIndex ) override {};
	virtual void MapBuffer( const MapBufferParam& param, BufferIndex& outIndex ) override {};

	virtual void UnmapName( const NameIndex index, CName& outName ) override {};
	virtual void UnmapType( const TypeIndex index,const rtti::IType*& outTypeRef ) override {};
	virtual void UnmapPointer( const ObjectIndex index, THandle< ISerializable >& outObjectRef ) override {};
	virtual void UnmapResourceReference( const PathIndex index, res::ResourcePath& outPath, res::ResourceTokenHandle& token  ) override {};
	virtual void UnmapResourceDeferredReference( const PathIndex index, res::ResourcePath& outPath ) override {};
	virtual void UnmapBuffer( const BufferIndex index, const LoadBufferToken& token, serialization::BufferAsyncPtr& outBufferAccess ) override {};

	virtual bool IsRelinking() const override final { return false; }
	virtual res::ResourcePath RelinkPath( const res::ResourcePath& path ) const override final { return path; }

private:
	// Quick lookup for visisted serializable objects
	red::HashSet< ISerializable* > m_visitedObjects{ red::PoolDebug() };

	// Ordered (by visit order) of all visited handles
	red::DynArray< THandle< ISerializable > > m_handles{ red::PoolDebug() };
};