/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

#include "serializationMapping.h"

namespace serialization
{
	/// Empty mapper - one that does not do anything, good base class though
	class NullMapper : public IMapper
	{
	public:
		virtual void MapName( const CName& name, NameIndex& outIndex ) override { outIndex = 0; }
		virtual void MapType( const rtti::IType* rttiType, TypeIndex& outIndex ) override { outIndex = 0; }
		virtual void MapPointer( const THandle< ISerializable >& objectRef, ObjectIndex& outIndex ) override { outIndex = 0; }
		virtual void MapResourceReference( const res::ResourcePath& path, PathIndex& outIndex ) override { outIndex = 0; }
		virtual void MapResourceDeferredReference( const res::ResourcePath& path, PathIndex& outIndex ) override { outIndex = 0; }
		virtual void MapBuffer( const MapBufferParam& param, BufferIndex& outIndex ) override { outIndex = 0; }

		virtual void UnmapName( const NameIndex index, CName& outName ) override final {};
		virtual void UnmapType( const TypeIndex index,const rtti::IType*& outTypeRef ) override final {};
		virtual void UnmapPointer( const ObjectIndex index, THandle< ISerializable >& outObjectRef ) override final {};
		virtual void UnmapResourceReference( const PathIndex index, res::ResourcePath& outPath, res::ResourceTokenHandle& token ) override final {};
		virtual void UnmapResourceDeferredReference( const PathIndex index, res::ResourcePath& outPath ) override final {};
		virtual void UnmapBuffer( const BufferIndex index, const LoadBufferToken& token, BufferAsyncPtr& outBufferAccess ) override final {};

		virtual bool IsRelinking() const { return false; }
		virtual res::ResourcePath RelinkPath( const res::ResourcePath& path ) const { return path; }
	};

} // serialization