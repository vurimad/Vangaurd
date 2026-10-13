/**
* Copyright (c) 2013-2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "objectSerializablePersistentCollection.h"
#include "../../../common/core/include/memoryBufferFile.h"
#include "../../../common/redFileSystem/include/memoryFileReader.h"
#include "../../../common/redReflection/include/serializableMap.h"
#include "../../../common/redReflection/include/serializationLoader.h"
#include "../../../common/redReflection/include/serializationSaver.h"
#include "../../../common/redSystem/include/stopWatch.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( ObjectSerializablePersistentCollection, tools );
	RTTI_PARENT_TYPE( tools::IObjectPersistentCollection )
RTTI_END_TYPE();


namespace tools
{

	static red::Atomic<Uint32> ContainerCount(0);
	static red::Atomic<Uint32> HolderCount(0);

	/// container for editor objects stored in persistent collections
	class ObjectContainer : public ISerializable
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		RTTI_DECLARE_TYPE( ObjectContainer );

	public:
		RED_FORCE_INLINE ObjectContainer()
		{
			auto count = ContainerCount.Increment();
		}

		RED_FORCE_INLINE ObjectContainer( const THandle< ISerializable >& obj )
			: m_object( obj )
			, m_serializableId( obj->GetID().Get() )
		{
			if ( obj )
			{
				m_id = obj->GetEditorObjectID();
				RED_ASSERT( m_id.IsValid(), "Capturing object that has no ID. Copy/Paste and undo operations may not work propertly." );
			}

			auto count = ContainerCount.Increment();
		}

		RED_FORCE_INLINE ObjectContainer( const THandle< ISerializable >& obj, const EditorObjectID& id )
			: m_object( obj )
			, m_id( id )
			, m_serializableId( obj->GetID().Get() )
		{
			auto count = ContainerCount.Increment();
		}

		virtual ~ObjectContainer()
		{
			auto count = ContainerCount.Decrement();
		}

		THandle< ISerializable >	m_object;
		tools::EditorObjectID		m_id;
		SerializableID::IDType		m_serializableId;
	};

	//----

	class ObjectCollectionHolder : public ISerializable
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		RTTI_DECLARE_TYPE( ObjectCollectionHolder );

	public:
		typedef red::DynArray< THandle< ObjectContainer > > TObjects;
		typedef red::DynArray< THandle< ISerializable > > TRawObjects;

		RED_FORCE_INLINE ObjectCollectionHolder()
			: m_objects{ red::PoolBackend() }
		{
			auto count = HolderCount.Increment();
		}

		RED_FORCE_INLINE ObjectCollectionHolder( const TRawObjects& serializables )
			: m_objects{ red::PoolBackend() }
		{
			m_objects.Reserve( serializables.Size() );

			for ( const THandle< ISerializable >& ptr : serializables )
			{
				if ( ptr )
				{
					m_objects.PushBack( CreateHandle< ObjectContainer >( ptr ) );
				}
			}

			auto count = HolderCount.Increment();
		}

		RED_FORCE_INLINE const TObjects& GetObjects() const
		{
			return m_objects;
		}

		virtual ~ObjectCollectionHolder()
		{
			auto count = HolderCount.Decrement();
		}

		virtual bool OnDestructionRequest() override final
		{
			return true; // it's imperative to delete this object
		}

	private:
		TObjects		m_objects;
	};

	//----

	struct Header
	{
		Uint32 magic;
		Uint32 version;
		Uint32 dataSize;
	};

	ObjectSerializablePersistentCollection::ObjectSerializablePersistentCollection()
	{
	}

	const Bool ObjectSerializablePersistentCollection::SupportsTextFormat() const
	{
		return false;
	}

	const Bool ObjectSerializablePersistentCollection::CanLoadFrom( const void* data, const Uint32 dataSize ) const
	{
		return CheckBinaryFormat( data, dataSize );
	}

	const Bool ObjectSerializablePersistentCollection::CheckBinaryFormat( const void* data, const Uint32 dataSize )
	{
		// cannot be valid data
		if ( !data || dataSize < sizeof( Header ) )
		{
			return false;
		}

		// check the magic marker at the start and the CRC (extra safety)
		const auto headerData = static_cast< const Header* >( data );
		if ( headerData->magic != MAGIC )
		{
			return false;
		}

		// check the version
		if ( headerData->version > VERSION ) // content too new (requires backwards compat)
		{
			return false;
		}

		// data seems OK
		return true;
	}

	const Bool ObjectSerializablePersistentCollection::Save( const EContentFormat format, GrowableMemoryBuffer& memoryBuffer ) const
	{
		if ( format != IObjectPersistentCollection::EContentFormat::Binary )
		{
			return false;
		}

		return SaveBinary( memoryBuffer );
	}

	const Bool ObjectSerializablePersistentCollection::Load( const void* data, const Uint32 dataSize )
	{
		return LoadBinary( data, dataSize );
	}

	const Bool ObjectSerializablePersistentCollection::LoadBinary( const void* data, const Uint32 dataSize )
	{
		// extra safety..
		if ( !CheckBinaryFormat( data, dataSize ) )
		{
			return false;
		}

		// get the actual data size
		const auto headerData = static_cast< const Header* >( data );
		const Uint32 actualBufferSize = headerData->dataSize;

		// setup reader just after the header
		CMemoryFileReader reader( static_cast< const Uint8* >( data ), actualBufferSize, 0 );
		reader.Seek( sizeof( Header ) ); // skip the header

		// load the objects
		m_data.Serialize( reader );
		return true;
	}

	const Bool ObjectSerializablePersistentCollection::SaveBinary( GrowableMemoryBuffer& memoryBuffer ) const
	{
		// no data
		if ( m_data.Empty() )
		{
			return false;
		}

		// write as binary blob
		MemoryBufferFileWriter writer( memoryBuffer );
		
		// binary header
		Header header{ MAGIC, VERSION, 0 };
		writer.Serialize( &header, sizeof( header ) );

		// binary data
		const_cast< DataBuffer& >( m_data ).Serialize( writer );

		// fill in the header
		writer.Seek( 0 );
		header.dataSize = static_cast< Uint32 >( writer.GetSize() );
		writer.Serialize( &header, sizeof( header ) );

		return true;
	}

	ObjectPersistentCollectionPtr ObjectSerializablePersistentCollection::CaptureObjects( const red::DynArray< THandle< ISerializable > >& objects )
	{
		red::StopWatch timer;

		// count valid object - we can create a collection only if we have at least one valid object
		Uint32 numValidObjects = 0;
		for ( const auto& ptr : objects )
		{
			if ( ptr != nullptr )
			{
				RED_ASSERT( ptr->GetEditorObjectID().IsValid(), "If you want to use copy/paste/delete/undo etc your objects must have the EditorID" );
				if ( ptr->GetEditorObjectID().IsValid() )
				{
					numValidObjects += 1;
				}
			}
		}

		// no valid objects
		if ( !numValidObjects )
		{
			return ObjectPersistentCollectionPtr();
		}

		// capture the collection into a single holder ISerializable - this is required because we can't save the ISerializables directly yet
		THandle< ObjectCollectionHolder > storage = CreateHandle< ObjectCollectionHolder >( objects );

		// rebind parents
		red::DynArray< ISerializable* > originalParents{ red::PoolBackend() };
		originalParents.Resize( objects.Size() );
		for ( Uint32 i=0; i<objects.Size(); ++i )
		{
			const THandle< ISerializable >& obj = objects[i];
			if ( obj )
			{
				originalParents[i] = obj->GetParent();
				obj->SetParent( storage.Get() );
			}
		}

		// store only the root object into the memory blob
		serialization::SavingContext context( storage );
		context.m_isCloner = true;

		// save to memory blob
		GrowableMemoryBuffer memoryBuffer;
		MemoryBufferFileWriter memoryWriter( memoryBuffer );
		memoryWriter.SetCloner( true );
		auto saver = serialization::ISaver::CreateSaver();
		const Bool saved = saver->SaveObjects( memoryWriter, context );

		// restore original parents
		for ( Uint32 i=0; i<objects.Size(); ++i )
		{
			const THandle< ISerializable >& obj = objects[i];
			if ( obj )
			{
				obj->SetParent( originalParents[i] );
			}
		}

		if ( !saved )
		{
			RED_LOG_ERROR( "Core: Failed to store the deserialized objects in a binary blob" );
			return ObjectPersistentCollectionPtr();
		}

		// setup output object
		ObjectSerializablePersistentCollection* ptr = RED_NEW( ObjectSerializablePersistentCollection )();

		// we are done, setup memory blob
		ptr->m_data = DataBuffer::Copy( memoryBuffer.Data(), memoryBuffer.Size() );

		RED_LOG( "Core: Captured %d object in persistent storage (%1.2f KB, %1.2fms)", 
			storage->GetObjects().Size(), memoryBuffer.Size() / 1024.0f, timer.GetDeltaMS() );

		return ObjectPersistentCollectionPtr( ptr );
	}

	const Bool ObjectSerializablePersistentCollection::UnpackObjects( const rtti::ClassType* baseType, red::DynArray< ObjectInfo >& outObjects, Bool reuseExistingObjects ) const
	{
		if ( m_data.Empty() )
		{
			RED_LOG_WARNING( "Core: Tried to unpack objects from empty collection" );
			return false;
		}

		// setup loading context
		serialization::LoadingContext context;
		context.m_replication = true;
		serialization::LoadingResult result;

		// deserialize the objects
		if ( !serialization::LoadFromMemory( m_data.Data(), m_data.Size(), context, result ) )
		{
			RED_LOG_WARNING( "Core: Failed to unpack objects from empty collection" );
			return false;
		}

		// get the storage object
		THandle< ObjectCollectionHolder > holder = nullptr;
		if ( result.m_loadedRootObjects.Size() >= 1 )
		{
			holder = Cast< ObjectCollectionHolder >( result.m_loadedRootObjects[0] );
		}
		if ( !holder )
		{
			RED_LOG_ERROR( "Core: No object holder found in the blob data" );
			return false;
		}

		// extract object
		outObjects.Reserve( holder->GetObjects().Size() );
		for ( const THandle< ISerializable >& ptr : holder->GetObjects() )
		{
			const THandle< ObjectContainer >& infoPtr = Cast< ObjectContainer >( ptr );
			if ( infoPtr )
			{
				if ( infoPtr->m_object && infoPtr->m_object->IsA( baseType ) )
				{
					RED_ASSERT( infoPtr->m_id.IsValid(), "Restored object has no ID" );

					ObjectInfo info;
					info.m_id = infoPtr->m_id;

					if ( reuseExistingObjects )
					{
						GSerializableMap->FindSerializable( SerializableID( infoPtr->m_serializableId ), info.m_object );
					}

					if ( !info.m_object )
					{
						info.m_object = infoPtr->m_object;
					}

					outObjects.PushBack( info );
				}
			}
		}

		// something was loaded (the holder may be empty though)
		return true;
	}	

} // tools

RTTI_BEGIN_TYPE_IN_NAMESPACE( ObjectContainer, tools );
	RTTI_PARENT_TYPE( ISerializable );
	RTTI_PROPERTY( m_object );
	RTTI_PROPERTY( m_id );
	RTTI_PROPERTY( m_serializableId );
RTTI_END_TYPE();



RTTI_BEGIN_TYPE_IN_NAMESPACE( ObjectCollectionHolder, tools );
	RTTI_PARENT_TYPE( ISerializable );
	RTTI_PROPERTY( m_objects );
RTTI_END_TYPE();


