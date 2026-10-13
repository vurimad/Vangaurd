/*
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#pragma once

namespace tools
{

class BACKEND_ENGINE_API HashingMapper : public serialization::IMapper
{
public:
	HashingMapper();
	explicit HashingMapper( Uint32 initialCapacity );
	~HashingMapper();

	void Reset();

	Uint64 Run( const THandle< ISerializable >& serializable );

	virtual void MapName( const CName& name, NameIndex& outIndex ) override;
	virtual void MapType( const rtti::IType* rttiType, TypeIndex& outIndex ) override;
	virtual void MapPointer( const THandle< ISerializable >& objectRef, ObjectIndex& outIndex ) override;
	virtual void MapResourceReference( const res::ResourcePath& path, PathIndex& outIndex ) override;
	virtual void MapResourceDeferredReference( const res::ResourcePath& path, PathIndex& outIndex ) override;
	virtual void MapBuffer( const MapBufferParam& param, BufferIndex& outIndex ) override;

	virtual void UnmapName( const NameIndex index, CName& outName ) override {}
	virtual void UnmapType( const TypeIndex index, const rtti::IType*& outTypeRef ) override {}
	virtual void UnmapPointer( const ObjectIndex index, THandle< ISerializable >& outObjectRef ) override {}
	virtual void UnmapResourceReference( const PathIndex index, res::ResourcePath& outPath, res::ResourceTokenHandle& token ) override {}
	virtual void UnmapResourceDeferredReference( const PathIndex index, res::ResourcePath& outPath ) override {}
	virtual void UnmapBuffer( const BufferIndex index, const LoadBufferToken& token, serialization::BufferAsyncPtr& outBufferAccess ) override {}

	virtual bool IsRelinking() const override final { return false; }
	virtual res::ResourcePath RelinkPath( const res::ResourcePath& path ) const override final { return path; }

private:
	Int16 AddNameIndex( const CName& name );
	Int16 AddObjectIndex( const THandle< ISerializable >& object );
	Int16 AddReferenceIndex( const res::ResourcePath& path );
	Uint16 AddBufferIndex( const MapBufferParam& buffer );

	Int16 GetNameIndex( const CName& name );
	Int16 GetObjectIndex( const THandle< ISerializable >& object );
	Int16 GetReferenceIndex( const res::ResourcePath& path );
	Uint16 GetBufferIndex( const MapBufferParam& buffer );

	bool IsMapping() const { return ! m_writingMode; }
	bool IsSerializing() const { return m_writingMode; }

	red::HashSet< ISerializable* > m_visitedObjects;

	red::DynArray< CName > m_names;
	red::DynArray< THandle< ISerializable > > m_objects;
	red::DynArray< res::ResourcePath > m_paths;
	red::DynArray< serialization::BufferHandleRO > m_buffers;

	red::HashMap< CName, Int16 > m_nameIndexes;
	red::HashMap< ISerializable*, Int16 > m_objectIndexes;
	red::HashMap< res::ResourcePath, Int16 > m_pathIndexes;
	red::HashMap< uintptr_t, Uint16 > m_bufferIndexes;

	bool m_writingMode;
};

} // tools
