/**
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "tomlValues.h"

namespace red
{
	//TODO support for more types; like time etc.
	class RED_RESOURCE_API TOMLParser
	{
	public:
		using ParseError = std::pair< Uint32, String >;

		TOMLParser( StringView buffer );
		~TOMLParser();

		template< typename T >
		Optional< T > GetValue( const char* key ) const;

		template< typename T >
		Optional< ArraySpan< const T > > GetArray( const char* key ) const;

		//@return false if key was not found
		template< typename Accessor >
		Bool AccessArrayOfTables( const char* key, const Accessor& accessor ) const;

		Uint32 GetArrayOfTablesSize( const char* key ) const;

		Bool HasParseErrors() const;
		ArraySpan< const ParseError > GetParseErrors() const;

	private:
		THandle< TOMLTable > m_root;

		DynArray< ParseError > m_parsingErrors{ PoolEngine() };
		Uint32 m_currentLine = 0u;

		THandle< TOMLBaseValue > Find( StringView key ) const;
		THandle< TOMLTable > GetOrCreateTable( StringView tableHeader );
		THandle< TOMLTable > GetOrCreateArrayOfTables( StringView arrayOfTablesHeader );

		TOMLParser( const THandle< TOMLTable >& root );
	};
}

#include "../src/toml.hpp"