/*
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "hashingFile.h"
#include "../../../common/redSystem/include/hash.h"
#include "../../../common/redFileSystem/include/file.h"
#include "../../../common/redFileSystem/include/nullFile.h"
#include "../../../common/redReflection/include/serializationMapping.h"
#include "../../../common/redReflection/include/bufferHandle.h"

namespace tools
{

//------------------------------------------------------------------------------

class HashingFile : public IFile
{
public:
	HashingFile();
	virtual ~HashingFile();

	Uint64 GetCalculatedHash() const;

	virtual void Serialize( void* buffer, size_t size ) override;
	virtual Uint64 GetOffset() const override;
	virtual Uint64 GetSize() const override;
	virtual void Seek( Int64 offset ) override;
	virtual void Flush() override;

private:
	Uint64 m_hash;
};

HashingFile::HashingFile()
	: IFile( FF_Writer | FF_NullBased )
	, m_hash( RED_FNV_OFFSET_BASIS64 )
{
}

HashingFile::~HashingFile()
{
	m_mapper = nullptr;
}

Uint64 HashingFile::GetCalculatedHash() const
{
	return m_hash;
}

void HashingFile::Serialize( void* buffer, size_t size )
{
	m_hash = red::CalculateHash64( buffer, size, m_hash );
}

Uint64 HashingFile::GetOffset() const
{
	return 0ull;
}

Uint64 HashingFile::GetSize() const
{
	return 0ull;
}

void HashingFile::Seek( Int64 offset )
{
}

void HashingFile::Flush()
{
}

//------------------------------------------------------------------------------

HashingMapper::HashingMapper()
	: m_writingMode( false )
{
}

HashingMapper::HashingMapper( Uint32 initialCapacity )
	: HashingMapper()
{
	m_visitedObjects.Reserve( initialCapacity );

	m_names.Reserve( initialCapacity );
	m_objects.Reserve( initialCapacity );
	m_paths.Reserve( initialCapacity );
	m_buffers.Reserve( initialCapacity );

	m_nameIndexes.Reserve( initialCapacity );
	m_objectIndexes.Reserve( initialCapacity );
	m_pathIndexes.Reserve( initialCapacity );
	m_bufferIndexes.Reserve( initialCapacity );
}

HashingMapper::~HashingMapper()
{
}

void HashingMapper::Reset()
{
	m_visitedObjects.Clear();

	m_names.Clear();
	m_objects.Clear();
	m_paths.Clear();
	m_buffers.Clear();

	m_nameIndexes.Clear();
	m_objectIndexes.Clear();
	m_pathIndexes.Clear();
	m_bufferIndexes.Clear();
}

Uint64 HashingMapper::Run( const THandle< ISerializable >& serializable )
{
	// Mapping
	{
		m_writingMode = false;

		Int32 index = 0;
		MapPointer( serializable, index );
	}

	// Hashing
	{
		m_writingMode = true;

		HashingFile hashingFile;
		hashingFile.m_mapper = this;
		for ( const THandle< ISerializable >& handle : m_objects )
		{
			handle->OnSerialize( hashingFile );
		}

		for ( const CName& name : m_names )
		{
			auto nameHash = name.GetHash();
			hashingFile.Serialize( &nameHash, sizeof( nameHash ) );
		}

		for ( const res::ResourcePath& path : m_paths )
		{
			auto resourcePathHash = path.GetHash();
			hashingFile.Serialize( &resourcePathHash, sizeof( resourcePathHash ) );
		}

		for ( const serialization::BufferHandleRO& buffer : m_buffers )
		{
			hashingFile.Serialize( const_cast< void* >( buffer.Data() ), buffer.GetSize() );
		}

		return hashingFile.GetCalculatedHash();
	}
}

void HashingMapper::MapName( const CName& name, NameIndex& outIndex )
{
	if ( IsMapping() )
	{
		outIndex = AddNameIndex( name );
	}
	else
	{
		outIndex = GetNameIndex( name );
	}
}

void HashingMapper::MapType( const rtti::IType* rttiType, TypeIndex& outIndex )
{
	if ( rttiType )
	{
		MapName( rttiType->GetName(), outIndex );
	}
	else
	{
		outIndex = 0;
	}
}

void HashingMapper::MapPointer( const THandle< ISerializable >& objectRef, ObjectIndex& outIndex )
{
	outIndex = 0;
	if ( IsMapping() )
	{
		if ( objectRef && ! m_visitedObjects.Exist( objectRef.Get() ) )
		{
			m_visitedObjects.Insert( objectRef.Get() );
			outIndex = AddObjectIndex( objectRef );

			CNullFileWriter file;
			file.m_mapper = this;
			objectRef->OnSerialize( file );
		}
	}
	else
	{
		outIndex = GetObjectIndex( objectRef );
	}
}

void HashingMapper::MapResourceReference( const res::ResourcePath& path, PathIndex& outIndex )
{
	if ( IsMapping() )
	{
		outIndex = AddReferenceIndex( path );
	}
	else
	{
		outIndex = GetReferenceIndex( path );
	}
}

void HashingMapper::MapResourceDeferredReference( const res::ResourcePath& path, PathIndex& outIndex )
{
	MapResourceReference( path, outIndex );
}

void HashingMapper::MapBuffer( const MapBufferParam& param, BufferIndex& outIndex )
{
	if ( IsMapping() )
	{
		outIndex = AddBufferIndex( param );
	}
	else
	{
		outIndex = GetBufferIndex( param );
	}
}

Int16 HashingMapper::AddNameIndex( const CName& name )
{
	Int16 index = 0;
	if ( name && !m_nameIndexes.Find( name, index ) )
	{
		index = static_cast< Int16 >( m_names.Size() );
		m_names.PushBack( name );
		m_nameIndexes.Insert( name, index );
	}
	return index;
}

Int16 HashingMapper::GetNameIndex( const CName& name )
{
	Int16 index = 0;
	m_nameIndexes.Find( name, index );
	return index;
}

Int16 HashingMapper::AddObjectIndex( const THandle< ISerializable >& objectRef )
{
	Int16 index = 0;
	if ( objectRef && !m_objectIndexes.Find( objectRef.Get(), index ) )
	{
		index = static_cast< Int16 >( m_objects.Size() );
		m_objects.PushBack( objectRef );

		m_objectIndexes.Insert( objectRef.Get(), index );
	}
	return index;
}

Int16 HashingMapper::GetObjectIndex( const THandle< ISerializable >& objectRef )
{
	Int16 index = 0;
	m_objectIndexes.Find( objectRef.Get(), index );
	return index;
}

Int16 HashingMapper::AddReferenceIndex( const res::ResourcePath& path )
{
	Int16 index = 0;
	if ( path.IsValid() && !m_pathIndexes.Find( path, index ) )
	{
		index = static_cast< Int16 >( m_paths.Size() );
		m_paths.PushBack( path );
		m_pathIndexes.Insert( path, index );
	}
	return index;
}

Int16 HashingMapper::GetReferenceIndex( const res::ResourcePath& path )
{
	Int16 index = 0;
	m_pathIndexes.Find( path, index );
	return index;
}

Uint16 HashingMapper::AddBufferIndex( const MapBufferParam& buffer )
{
	Uint16 index = 0;
	uintptr_t pointer = reinterpret_cast< uintptr_t >( buffer.inMemoryData.Data() );
	if ( buffer.inMemoryData.Data() && !m_bufferIndexes.Find( pointer, index ) )
	{
		index = static_cast< Uint16 >( m_buffers.Size() );
		m_buffers.PushBack( buffer.inMemoryData );
		m_bufferIndexes.Insert( pointer, index );
	}
	return index;
}

Uint16 HashingMapper::GetBufferIndex( const MapBufferParam& buffer )
{
	Uint16 index = 0;
	uintptr_t pointer = reinterpret_cast< uintptr_t >( buffer.inMemoryData.Data() );
	m_bufferIndexes.Find( pointer, index );
	return index;
}

//------------------------------------------------------------------------------

Uint64 CalculateSerializableHash( const THandle< ISerializable >& handle )
{
	HashingMapper mapper;
	return mapper.Run( handle );
}

} // tools
