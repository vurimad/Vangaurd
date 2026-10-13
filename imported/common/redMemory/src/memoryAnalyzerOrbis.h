/**
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_MEMORY_ANALYZER_ORBIS_H_
#define _RED_MEMORY_MEMORY_ANALYZER_ORBIS_H_

#include "hookHandler.h"

namespace red
{
namespace memory
{
	struct PoolInfo;

	class MemoryAnalyzerOrbis
	{
	public:
		MemoryAnalyzerOrbis();
		~MemoryAnalyzerOrbis();

		void Initialize( HookHandler* hookHandler );

	private:
		HookHandler* m_hookHandler;
		HookHandle m_hookHandle;
	};

	void RegisterPoolInMemoryAnalyzer( const PoolInfo* pool, const PoolInfo* parent );

	void MemoryAnalyzerFree( const Block& block );
	void MemoryAnalyzerAllocate( const Block& block, const PoolHandle& poolHandle );
	void MemoryAnalyzerReallocateBegin( const Block& block );
	void MemoryAnalyzerReallocateEnd( const Block& block );
	
}
}

#endif