/**
* Copyright (c) 2007-16 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../../common/redContainers/include/map.h"

namespace red { class AbsolutePath; }

/// Storage helper for config vars
namespace Config
{
	class CParseHelper
	{
	public:
		static Bool IsIdent( const red::String& str );
		static Bool IsNumber( const red::String& str );
		static Bool IsString( const red::String& str );
		static Bool IsCharAlpha( const red::AnsiChar chr );
		static Bool IsCharNum( const red::AnsiChar chr );
		static Bool IsCharAlphaNum( const red::AnsiChar chr );
	};

	/// Saving/Loading of console config variables
	class RED_CONFIG_API CConfigVarStorage
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

	public:

		typedef Uint32 TNameHash;

		struct Entry
		{
			red::String	m_name;
			red::String	m_value;

			RED_FORCE_INLINE Entry( const red::AnsiChar* name = "", const red::String& value = red::String::EMPTY() )
				: m_name( name )
				, m_value( value )
			{}

			RED_FORCE_INLINE Entry( const Entry& other )
				: m_name( other.m_name )
				, m_value( other.m_value )
			{}

			RED_FORCE_INLINE Entry( Entry&& other )
			{
				m_name = std::move( other.m_name );
				m_value = std::move( other.m_value );
			}

			RED_FORCE_INLINE Entry& operator=( const Entry& other )
			{
				if ( this != &other )
				{
					m_name = other.m_name;
					m_value = other.m_value;
				}
				return *this;
			}

			RED_FORCE_INLINE Entry& operator=( Entry&& other )
			{
				if ( this != &other )
				{
					m_name = std::move( other.m_name );
					m_value = std::move( other.m_value );
				}
				return *this;
			}
		};

		struct Group
		{
			RED_USE_MEMORY_POOL( red::PoolEngine );

		public:

			red::String						m_name;
			red::Map< TNameHash, Entry >		m_entries;

			Group( const red::AnsiChar* name )
				: m_name( name )
				, m_entries( red::PoolEngine() )
			{}
		};


		CConfigVarStorage();
		~CConfigVarStorage();

		// find entry for given group/var 
		Bool GetEntry( const red::AnsiChar* groupName, const red::AnsiChar* varName, red::String& outValue ) const;

		// set entry for given group/vars
		Bool SetEntry( const red::AnsiChar* groupName, const red::AnsiChar* varName, const red::String& value );

		// remove group
		Bool RemoveGroup( const red::AnsiChar* groupName );

		// remove entry in group
		Bool RemoveEntry( const red::AnsiChar* groupName, const red::AnsiChar* varName );

		// clear all
		void Clear();

		// filter the values from this group using a given base
		Bool FilterDifferences( const CConfigVarStorage& base, CConfigVarStorage& outDifference ) const;

		// load settings from file
		Bool Load( const red::AbsolutePath& absoluteFilePath );

		// save settings to file, filter out settings that are the same as in base
		Bool Save( const red::AbsolutePath& absoluteFilePath );

		// load settings from string, filter out settings that are the same as in base
		Bool LoadFromString( const red::String& content );

		// save settings to string, filter out settings that are the same as in base
		Bool SaveToString( red::String& output ) const;

		// is this storage modified ?
		Bool IsModified() const;

	private:
		
		typedef red::Map< TNameHash, Group* >		TGroups;
		TGroups		m_groups;
		Bool		m_isModified;

		static TNameHash CalcHash( const red::AnsiChar* text );
		static TNameHash CalcHash( const red::String& text );
	};

} // Console