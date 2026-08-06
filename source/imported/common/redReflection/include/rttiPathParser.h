/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#pragma once

namespace rtti
{
	class AccessPath;

	/// Path parser for RTTI stuff
	/// Parsing RTTI path leaves you only three options: ident, array index, end of stream
	/// The parsing is always driven by the calling site as it's type-dependent
	class RED_REFLECTION_API PathParser : public red::NonCopyable
	{
	public:
		PathParser( const AccessPath& path ); // makes copy of the string
		PathParser( const String& rawPath );  // makes copy of the string
		PathParser( const AccessPath* pathNoCopy );  // DOES NOT MAKE COPY OF THE STRING
		PathParser( const AnsiChar* rawPathNoCopy );  // DOES NOT MAKE COPY OF THE STRING

		/// True if not end of stream
		RED_FORCE_INLINE operator Bool() const { return m_pos < m_end; }

		/// Get child identifier, returns true on success
		const Bool EatName( CName& outName );

		/// Get array index, returns true on success
		const Bool EatIndex( Int32& outArrayIndex );

		/// Get the remaining, uneaten path
		/// NOTE: the returned path is NOT COPIED, just references, the PathParser must be alive for the returned AccessPath to be valid
		AccessPath GetUneatenPath() const;

	private:
		String		m_localCopyBuffer; // COPY, for now, we need better strings

		const AnsiChar*	m_pos;
		const AnsiChar*	m_end;
	};

} // rtti
