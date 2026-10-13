/*
 * Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "deferredBufferMapper.h"
#include "../../redFileSystem/include/nullFile.h"

namespace serialization
{

	DeferredDataBufferLoader::DeferredDataBufferLoader()
	{
	}

	DeferredDataBufferLoader::~DeferredDataBufferLoader()
	{
	}

	void DeferredDataBufferLoader::Run( const THandle< ISerializable >& serializable )
	{
		CNullFileWriter file;
		file.m_mapper = this;
		serializable->OnSerialize( file );
	}

	void DeferredDataBufferLoader::MapPointer( const THandle< ISerializable >& objectRef, ObjectIndex& outIndex )
	{
		outIndex = 0;
		if ( objectRef && m_processedObjects.Insert( objectRef.Get() ).IsSuccessful() )
		{
			CNullFileWriter file;
			file.m_mapper = this;
			objectRef->OnSerialize( file );
		}
	}

	void DeferredDataBufferLoader::MapBuffer( const MapBufferParam& param, BufferIndex& outIndex )
	{
		outIndex = 0;
		// Nothing to do, just make sure the buffer loads itself, which is taken care of by just calling this function
	}


} // serialization
