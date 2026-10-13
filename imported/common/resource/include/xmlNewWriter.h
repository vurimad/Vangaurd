/// RenderLab (C) 2015 by Tomasz "RexDex" Jonarski
/// All Right Reserved

#pragma once

#include "../../redReflection/include/types.h"

class CBasicTextBuilder;

namespace xml 
{

	/// Structured XML writer
	/// Contains internal NODE STACK
	class RED_RESOURCE_API Writer
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

	public:
		virtual ~Writer();

		typedef std::function< void(class CBasicTextBuilder*) > TFreeFunction;

		// node, NOTE: non formated version is faster
		virtual void Open( const AnsiChar* name ) = 0;
		virtual void Openf( STATIC_CHECK_PRINTF_MSC const AnsiChar* txt, ... ) = 0;
		virtual void Close() = 0;

		// attribute, NOTE: non formated version is faster
		virtual void Attr( const AnsiChar* name, const AnsiChar* value ) = 0;
		virtual void Attrf( const AnsiChar* name, STATIC_CHECK_PRINTF_MSC const AnsiChar* value, ... ) = 0;

		// value, NOTE: non formated version is faster
		virtual void Value( const AnsiChar* txt ) = 0;
		virtual void Valuef( STATIC_CHECK_PRINTF_MSC const AnsiChar* txt, ... ) = 0;

		// create generic XML writer based on dynamic string buffer
		static red::UniquePtr<Writer> Create( class CBasicTextBuilder* printBuffer, TFreeFunction freeFunction );

		// create generic XML writer based on string buffer from the stack
		static red::UniquePtr<Writer> Create( class CBasicTextBuilder& printBuffer );
	};

	/// Helper block
	class RED_RESOURCE_API WriteBlock
	{
	public:
		WriteBlock( Writer& xml, const AnsiChar* name );
		WriteBlock( WriteBlock& block, const AnsiChar* name );
		~WriteBlock();

		RED_FORCE_INLINE Writer* operator->() const
		{
			return m_writer;
		}

	private:
		Writer*				m_writer;
		const AnsiChar*		m_name;
	};

} // xml