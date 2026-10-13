/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "objectFwd.h"
#include "backendMemory.h"

class GrowableMemoryBuffer;

namespace tools
{

	/// A collection of editable objects that are stored in a persistent (non-runtime) way
	/// Such a collection can be exchanged between applications safely
	/// Main internal purpose: safe storage of objects between complex operations
	/// Main external purpose: copy/paste
	class BACKEND_ENGINE_API IObjectPersistentCollection
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		RTTI_DECLARE_TYPE( IObjectPersistentCollection );

	public:
		virtual ~IObjectPersistentCollection();
		
		enum class EContentFormat
		{
			Text,
			Binary,
		};

		struct ObjectInfo
		{
			THandle< ISerializable >	m_object;	// object data
			EditorObjectID				m_id;		// ID (when it was stored, does not have to be unique any longer, ie. multiple PASTE operations)
		};

		/// Can this content be saved to a human-readable format ?
		virtual const Bool SupportsTextFormat() const = 0;

		/// Unpack objects from the collection
		virtual const Bool UnpackObjects( const rtti::ClassType* baseType, red::DynArray< ObjectInfo >& outObjects, Bool reuseExistingObjects = false ) const = 0;

		/// Check if a collection can be restored from given data format, should mostly check for compatibility
		virtual const Bool CanLoadFrom( const void* data, const Uint32 dataSize ) const = 0;

		/// Save to persistent storage (memory, disk, clipboard, etc)
		/// NOTE: the way the objects are stored should be really PERSISTENT and it should be possible to copy paste this data between two editors
		/// We use the paged buffer in order to handle really big data sets
		virtual const Bool Save( EContentFormat format, GrowableMemoryBuffer& memoryBuffer ) const = 0;

		/// Try to load from data, may fail if the data is corrupted, the compatibility should be checked by the CanLoadFrom
		virtual const Bool Load( const void* data, const Uint32 dataSize ) = 0;

		/// Restore object collection from arbitrary memory buffer
		/// Note: we don't know if the buffer is text or binary
		static ObjectPersistentCollectionPtr CreateFromBuffer( const void* data, const Uint32 dataSize );
	};

} // tools

