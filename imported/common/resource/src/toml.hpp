/**
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/
#pragma once

namespace red
{
	template< typename T >
	Optional< T > TOMLParser::GetValue( const char* key ) const
	{
		auto found = Find( key );
		if ( auto foundKeyValue = Cast< TOMLKeyValue >( found ) )
		{
			return foundKeyValue->GetValue< T >();
		}
		return {};
	}

	template< typename T >
	Optional< ArraySpan< const T > > TOMLParser::GetArray( const char* key ) const
	{
		auto found = Find( key );
		if ( auto foundArray = Cast< TOMLArray< T >::Derived >( found ) )
		{
			return MakeOptional< ArraySpan< const T > >( foundArray->GetValues() );
		}
		return {};
	}

	template< typename Accessor >
	Bool TOMLParser::AccessArrayOfTables( const char* key, const Accessor& accessor ) const
	{
		auto found = Find( key );
		if ( auto foundArray = Cast< TOMLArrayTable >( found ) )
		{
			auto values = foundArray->GetValues();
			for ( Uint32 i = 0; i < values.Size(); ++i )
			{
				TOMLParser tempParser( values[i] );
				accessor( i, tempParser );
			}

			return true;
		}

		return false;
	}
}