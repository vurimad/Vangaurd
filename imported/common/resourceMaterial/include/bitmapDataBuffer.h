/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#pragma once
#include "../../redReflection/include/deferredDataBuffer.h"

/// Data buffer for bitmap mipmap, can be loaded asynchronously
class BitmapMipLatentDataBuffer
{
public:
	BitmapMipLatentDataBuffer();
	BitmapMipLatentDataBuffer( const void* data, Uint32 size );
	BitmapMipLatentDataBuffer( Uint32 size );
	BitmapMipLatentDataBuffer( const BitmapMipLatentDataBuffer& other ) = delete;
	~BitmapMipLatentDataBuffer();
	BitmapMipLatentDataBuffer& operator=( const BitmapMipLatentDataBuffer& other ) = delete;

	serialization::DeferredDataBuffer m_data;
	mutable serialization::BufferHandleRO m_loadedData;

	RED_INLINE const Uint32 GetSize()  const
	{
		(void)GetData(); // ensure data loaded
		return m_loadedData.GetSize();
	}

	Bool Load() { return true; }
	Bool Unload() { return true; }

	void Serialize( IFile& f );

	void Allocate( const Uint32 size );
	void* GetData() const;
};
