/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#pragma once

namespace rtti
{

	/// Path representing general RTTI property access
	/// Example:
	///  "isVisible"
	///  "position.X"
	///  "components[0].position.X"
	/// The path always represents DATA PATH
	/// For RTTI properties it's ALWAYS resolvable to void* memory and const rtti::IType* pair

	class RED_REFLECTION_API AccessPath
	{
	public:
		RED_FORCE_INLINE AccessPath() {}

		// copy
		RED_FORCE_INLINE AccessPath( const AccessPath& path )
			: m_path( path.m_path )
		{}

		RED_FORCE_INLINE explicit AccessPath( const red::StringView pathView )
			: m_path( pathView.Data(), pathView.Length() )
		{
		}

		/// Append array operator ([xxx])
		AccessPath operator[]( const Int32 index ) const;
		AccessPath operator[]( const Uint32 index ) const;

		/// Append member operator (.xxx)
		AccessPath operator[]( const char* name ) const;
		AccessPath operator[]( const String& name ) const;
		AccessPath operator[]( const CName name ) const;

		/// Is the path empty ?
		RED_FORCE_INLINE const Bool IsEmpty() const { return m_path.Empty(); }

		/// Get string representation
		RED_FORCE_INLINE const red::String& ToString() const { return m_path; }

	private:
		red::String		m_path;
	};

} // rtti