/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_CALLSTACK_H_
#define _RED_MEMORY_CALLSTACK_H_

#include "callstackCollector.h"
#include "callstackCollectorConstant.h"

namespace red
{
namespace memory
{
	class Serializer;

	class Callstack
	{
	public:
		Callstack();

		u32 GetHash() const { return m_hash; }
		u32 GetDepth() const { return m_depth; }
		u32 GetSize() const { return m_size; }
	
		Bool SerializeCallstack( Serializer & serializer ) const;
		u32 GetSerializationSize() const;

	private:
		u64 m_callstack[ c_callstackMaxDepth ];
		u32 m_hash;
		u32 m_depth;
		u32 m_size;
		u16 m_callstackMask;
		CallstackCollector m_callstackCollector;
	};
}
}

#endif

