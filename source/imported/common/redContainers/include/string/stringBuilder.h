/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../redSystem/include/utility.h"
#include "../../../redContainers/include/string/string.h"

namespace red
{
	template<typename TString>
	class StringBuilder : NonCopyable
	{
	public:
		typedef typename TString::value_type TChar;

		static const Uint32 INLINE_BUFFER_SIZE = 64;

	public:
		StringBuilder();
		~StringBuilder();

	public:
		RED_FORCE_INLINE TString ToString() const
		{
			return { AsChar(), GetLength() };
		}

		// WARNING: The pointer may not be valid after StringBuilder modification.
		RED_FORCE_INLINE const TChar* AsChar() const
		{
			assertNullTerminator();
			return m_buf;
		}

		RED_FORCE_INLINE Uint32 GetLength() const
		{
			return m_length;
		}

		RED_FORCE_INLINE void Append( const TString& str )
		{
			Append( str.AsChar(), str.Length() );
		}

		RED_FORCE_INLINE void Append( const TChar* str )
		{
			RED_FATAL_ASSERT( str, "" );
			const Uint32 len = static_cast<Uint32>( Strlen( str ) );
			Append( str, len );
		}

		RED_FORCE_INLINE void Append( const TChar ch )
		{
			TChar buf[2] = { ch, 0 };
			Append( buf, 1 );
		}

		RED_INLINE void Prepend( const TString& str )
		{
			Prepend( str.AsChar(), str.Length() );
		}

		RED_INLINE void Prepend( const TChar* str )
		{
			RED_FATAL_ASSERT( str, "" );
			const Uint32 len = static_cast< Uint32 >( Strlen( str ) );
			Prepend( str, len );
		}

		RED_INLINE void Prepend( const TChar ch )
		{
			TChar buf[ 2 ] = { ch, 0 };
			Prepend( buf, 1 );
		}

		void Append( const TChar* str, Uint32 len );
		void Prepend( const TChar* str, Uint32 len );

		void Appendf( STATIC_CHECK_PRINTF_MSC const TChar* fmt, ... );

		RED_FORCE_INLINE void Reset() 
		{ 
			m_length = 0;
			writeNullTerminator();
		}

		RED_FORCE_INLINE Bool Empty() const
		{
			return m_length == 0;
		}

	private:
		RED_FORCE_INLINE void assertCapacity() const
		{
			RED_FATAL_ASSERT( m_capacity > 0 && m_length <= m_capacity - 1, "No room for null terminiator" );
		}

		RED_FORCE_INLINE void assertNullTerminator() const
		{
			RED_FATAL_ASSERT( m_buf[ m_length ] == 0, "" );
		}

		RED_FORCE_INLINE void writeNullTerminator()
		{
			assertCapacity();
			m_buf[ m_length ] = 0;
		}

	private:
		void EnsureCapacity( Uint32 requiredCapacity );
		void EnsureCapacity();

	private:
		static const Float GROWTH_FACTOR;

	private:
		TChar	m_inlineBuf[64];
		TChar*	m_buf;
		Uint32	m_capacity;
		Uint32	m_length;
	};

}

#if defined(RED_DLL) && !defined(RED_EXPORT_redContainers)
template class RED_CONTAINERS_API red::StringBuilder<red::String>;
#endif
