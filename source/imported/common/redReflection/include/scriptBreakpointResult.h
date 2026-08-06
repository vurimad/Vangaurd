/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace script
{
	class RuntimeBreakpoint;

	class RED_REFLECTION_API BreakpointResult
	{
	public:
		BreakpointResult( const RuntimeBreakpoint* source );
		BreakpointResult( const BreakpointResult& other );

		Bool Success() const;

		Uint16 GetLine() const;
		Uint32 GetLineStart() const;
		Uint16 GetColumn() const;
		Uint16 GetLength() const;

	private:
		const RuntimeBreakpoint* m_breakpoint;
	};
}
