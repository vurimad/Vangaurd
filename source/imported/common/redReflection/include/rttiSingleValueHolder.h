/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#pragma once

#include "../../redMemory/include/sharedPtr.h"
#include "reflectionPool.h"

namespace rtti
{
	class ValueBuilder;
	class ValueParser;

	class SingleValueHolder;
	typedef red::SharedPtr< SingleValueHolder > SingleValuePtr;

	/// Stores a single simple value for editor/automation use, not meant for in-game usage
	/// SAFE STORAGE - Object pointers are stored as SerializableID
	/// ALL TYPES ARE STORED AS STRINGS - PERFORMANCE IS VERY POOR
	class RED_REFLECTION_API SingleValueHolder
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		~SingleValueHolder();

		/// Most costly version of ToString
		RED_FORCE_INLINE const String& ToString() const { return m_text; }

		/// Parse a value with given type and if everything is valid assign it into the value holder
		/// The internal type will be determined based on the data prefix
		/// NOTE: the quotes are always dropped when specified and added automatically
		static SingleValuePtr Create( const AnsiChar* value );

	private:
		SingleValueHolder(); // empty is no longer allowed
		SingleValueHolder( const SingleValueHolder& other );

		/// Assign copy, slow
		SingleValueHolder& operator=( const SingleValueHolder& other );

		String		m_text;	// type of the data
	};

} // rtti