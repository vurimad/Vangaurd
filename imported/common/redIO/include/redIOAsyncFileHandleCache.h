/*
 * Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
 */
#pragma once

#include "redIOCommon.h"
#include "redIOFile.h"
#include "../../redMemory/include/uniquePtr.h"

namespace io
{

using AsyncFile = prv::SystemFile;

class MemoryAllocator;

class REDIO_API AsyncFileHandleCache : private red::NonCopyable
{
	RED_USE_MEMORY_POOL( red::PoolEngine );
public:
	virtual ~AsyncFileHandleCache() = 0;

	virtual TFileHandle Open( const char* absoluteFilePath, Uint8 asyncFlags ) = 0;

	virtual void AddRef( TFileHandle handle ) = 0;
	virtual void Release( TFileHandle handle ) = 0;

	virtual AsyncFile* GetAsyncFile( TFileHandle handle ) = 0;

	virtual const char* GetFileName( TFileHandle handle ) const = 0;

	virtual Uint8 GetAsyncFlags( TFileHandle handle ) const = 0;

	virtual void SetMemoryAllocator(MemoryAllocator* allocator) = 0;

	static red::UniquePtr< AsyncFileHandleCache > Create();
};

} // io
