/**
* Copyright (c) 2013-2020 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "objectPersistentCollection.h"

namespace tools
{
	/// object collection implemented using serialization
	/// the sub-type can be specified when the objects are created
	class BACKEND_ENGINE_API ObjectSerializablePersistentCollection : public IObjectPersistentCollection
	{
		RTTI_DECLARE_TYPE( ObjectSerializablePersistentCollection );

	public:

		/// IObjectPersistentCollection interface
		virtual const Bool SupportsTextFormat() const override final;
		virtual const Bool CanLoadFrom( const void* data, const Uint32 dataSize ) const override final;
		virtual const Bool UnpackObjects( const rtti::ClassType* baseType, red::DynArray< ObjectInfo >& outObjects, Bool reuseExistingObjects = false ) const override final;

		/// IObjectPersistentCollection interface
		virtual const Bool Save( EContentFormat format, GrowableMemoryBuffer& memoryBuffer ) const override final;
		virtual const Bool Load( const void* data, const Uint32 dataSize ) override final;

		/// Capture from table of serializable objects
		static ObjectPersistentCollectionPtr CaptureObjects( const red::DynArray< THandle< ISerializable > >& objects );

	private:
		ObjectSerializablePersistentCollection();

		typedef red::DynArray< EditorObjectID > TObjectIDs;
		typedef red::DynArray< String > TObjectMetaData;

		static const Bool CheckBinaryFormat( const void* data, const Uint32 dataSize );
		const Bool LoadBinary( const void* data, const Uint32 dataSize );
		const Bool SaveBinary( GrowableMemoryBuffer& memoryBuffer ) const;

		static constexpr Uint32 MAGIC = 'CJBO';
		static constexpr Uint32 VERSION = 1;

		DataBuffer m_data; // binary data of the objects
	};

} // tools
