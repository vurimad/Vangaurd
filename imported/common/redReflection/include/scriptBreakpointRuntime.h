/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "rttiFunction.h"

namespace script
{
	namespace breakpoint
	{
		class Condition;
	}

	class RuntimeBreakpoint
	{
	public:
		// LineStart = Offset from start of document
		// breakpointOffset = Offset from start of line
		RuntimeBreakpoint( Uint32 codeOffset, Uint32 lineStart, Uint16 breakpointOffset, Uint16 length, Uint16 line );
		RuntimeBreakpoint();

		RED_INLINE void SetFunction( rtti::Function* function ) { m_function = function; }

		RED_INLINE Uint16 GetLine() const { return m_line; }
		RED_INLINE Uint32 GetLineStart() const { return m_lineStart; }
		RED_INLINE Uint16 GetColumn() const { return m_breakpointOffset; }
		RED_INLINE Uint16 GetLength() const { return m_length; }

		RED_INLINE Uint32 GetStartPosition() const { return m_lineStart + m_breakpointOffset; }
		RED_INLINE Uint32 GetEndPosition() const { return m_lineStart + m_breakpointOffset + m_length; }

		Bool IsSet() const;
		void Set( Bool isSet );

		void ClearCondition();

		void SetPasscount( Uint32 passcount, Uint32 style );
		void SetHitcount( Uint32 hitcount );

		void SetCondition( const String& expression, Uint32 style );

	private:
		void ClearCondition( Uint8* toggleByte );
		const Uint8* GetToggleByte() const;
		RED_INLINE Uint8* GetToggleByte() { return const_cast< Uint8* >( static_cast< const RuntimeBreakpoint* >( this )->GetToggleByte() ); }
		class breakpoint::Condition* GetOrCreateCondition();
		class breakpoint::Condition* GetCondition( const Uint8* toggleByte );
		class breakpoint::Condition* GetCondition();

		rtti::Function* m_function;
		Uint32 m_codeOffset;
		Uint32 m_lineStart;
		Uint16 m_breakpointOffset;
		Uint16 m_length;
		Uint16 m_line;
	};
}
