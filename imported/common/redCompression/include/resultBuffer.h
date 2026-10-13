/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../redMemory/include/sharedPtr.h"
#include "redCompressionApi.h"

namespace compression
{

	/// a very simple data holder for compression/decompression output
	class REDCOMPRESSION_API ResultBuffer : public red::NonCopyable
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

	public:
		typedef red::FixedSizeFunction< void(void*) > TFreeFunc;

		ResultBuffer(); // allocates empty buffer
		ResultBuffer(void* data, const red::Uint64 size, TFreeFunc freeFunc); // creates a buffer of given size
		~ResultBuffer(); // frees the data

		// get pointer to data, read only access, should NOT be freed
		RED_FORCE_INLINE const void* GetData() const { return m_data; }

		// get size of the data in the buffer
		RED_FORCE_INLINE const Uint64 GetDataSize() const { return m_size; }
		
		// free the data in the buffer
		void Clear();

		// patch size of stored data
		void PatchDataSize( const Uint64 size );

	private:
		void*		m_data;		// allocated buffer data
		Uint64		m_size;		// buffer size

		TFreeFunc	m_freeFunc;	// function to free the buffer
	};

	typedef red::SharedPtr<ResultBuffer> ResultBufferPtr;

} // compression