/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "serializationMapping.h"
#include "../../../common/redContainers/include/redContainersPublic.h"

class RED_REFLECTION_API ResourceCollector : public serialization::IMapper
{
public:	
	ResourceCollector();
	virtual ~ResourceCollector();

	// crawl through the specified object's references and pointers
	void Run( const THandle< ISerializable >& serializable );

	// outputs all collected paths
	void GetCollectedPaths( red::DynArray< String >& outPaths );
	void GetCollectedPaths( red::DynArray< res::ResourcePath >& outPaths );

protected:
	virtual void MapName( const CName& name, NameIndex& outIndex ) override {}
	virtual void MapType( const rtti::IType* rttiType, TypeIndex& outIndex ) override {}
	virtual void MapPointer( const THandle< ISerializable >& objectRef, ObjectIndex& outIndex ) override;
	virtual void MapResourceReference( const res::ResourcePath& path, PathIndex& outIndex ) override;
	virtual void MapResourceDeferredReference( const res::ResourcePath& path, PathIndex& outIndex ) override;
	virtual void MapBuffer( const MapBufferParam& param, BufferIndex& outIndex ) override {}

	virtual void UnmapName( const NameIndex index, CName& outName ) override {}
	virtual void UnmapType( const TypeIndex index,const rtti::IType*& outTypeRef ) override {}
	virtual void UnmapPointer( const ObjectIndex index, THandle< ISerializable >& outObjectRef ) override {}
	virtual void UnmapResourceReference( const PathIndex index, res::ResourcePath& outPath, res::ResourceTokenHandle& outToken ) override {}
	virtual void UnmapResourceDeferredReference( const PathIndex index, res::ResourcePath& outPath ) override {}
	virtual void UnmapBuffer( const BufferIndex index, const LoadBufferToken& token, serialization::BufferAsyncPtr& outBufferAccess ) override {}

	virtual bool IsRelinking() const override final { return false; }
	virtual res::ResourcePath RelinkPath( const res::ResourcePath& path ) const override final { return path; }

private:
	red::HashSet< ISerializable* > m_visitedObjects{ red::PoolDebug() };
	red::DynArray< res::ResourcePath > m_collectedPaths{ red::PoolDebug() };

	void CollectPath( const res::ResourcePath& path );
	void FinalisePathList();
};