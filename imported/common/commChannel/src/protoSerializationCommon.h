/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include <stack>

namespace ProtoUtils
{
	template<typename TElement>
	class ProtoStack
	{
	public:
		inline Bool Empty() const { return m_stack.Empty(); }
		inline Uint32 Size() const { return m_stack.Size(); }
		inline TElement& Top() { return m_stack.Back(); }
		inline void Pop() { m_stack.PopBack(); }
		inline void Push( TElement& element ) { m_stack.PushBack( element ); }
		inline void Push( const TElement& element ) { m_stack.PushBack( element ); }

	private:
		red::DynArray<TElement> m_stack{ red::PoolBackend() };
	};

	enum class ETypeID
	{
		Varint = 0, // Int8, Uint8, Int16, Uint16, Int32, Uint32, Int64, Uint64, bool (ToDo: use this)
		Bit64 = 1, // fixed64, sfixed64, double
		Bit32 = 2,	// fixed32, sfixed32, float
		LengthDelimited = 3, // string, bytes, embedded messages, packet repeated fields - it's followed by 16-bit length value
		Bit16 = 4,	// ToDo: remove this and use Varint only
		Bit8 = 5,	// ToDo: remove this and use Varint only
	};


}

