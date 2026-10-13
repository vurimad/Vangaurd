/*
 * Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "serializationMapping.h"
#include "serializationNullMapper.h"

namespace serialization
{
	class RED_REFLECTION_API DeferredDataBufferLoader : public IMapper
	{
	public:
		DeferredDataBufferLoader();
		virtual ~DeferredDataBufferLoader();

		// Crawl through the object and loads its deferred data buffers
		void Run( const THandle< ISerializable >& serializable );

		// Mapping interface - implemented
		virtual void MapName( const CName& name, NameIndex& outIndex ) override final { outIndex = 0; };
		virtual void MapType( const rtti::IType* rttiType, TypeIndex& outIndex ) override final { outIndex = 0; };
		virtual void MapPointer( const THandle< ISerializable >& objectRef, ObjectIndex& outIndex ) override final;
		virtual void MapResourceReference( const res::ResourcePath& path, PathIndex& outIndex ) override final { outIndex = 0; };
		virtual void MapResourceDeferredReference( const res::ResourcePath& path, PathIndex& outIndex ) override final { outIndex = 0; };
		virtual void MapBuffer( const MapBufferParam& param, BufferIndex& outIndex ) override final;

	private:
		// Unmapping interface - not used
		virtual void UnmapName( const NameIndex index, CName& outName ) override final {};
		virtual void UnmapType( const TypeIndex index,const rtti::IType*& outTypeRef ) override final {};
		virtual void UnmapPointer( const ObjectIndex index, THandle< ISerializable >& outObjectRef ) override final {};
		virtual void UnmapResourceReference( const PathIndex index, res::ResourcePath& outPath, res::ResourceTokenHandle& token  ) override final {};
		virtual void UnmapResourceDeferredReference( const PathIndex index, res::ResourcePath& outPath ) override final {};
		virtual void UnmapBuffer( const BufferIndex index, const LoadBufferToken& token, BufferAsyncPtr& outBufferAccess ) override final {};

		virtual bool IsRelinking() const override final { return false; }
		virtual res::ResourcePath RelinkPath( const res::ResourcePath& path ) const override final { return path; }

		red::HashSet< ISerializable* > m_processedObjects{ red::PoolEngine() };
	};

} // serialization
