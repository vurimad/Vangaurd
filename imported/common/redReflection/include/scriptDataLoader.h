/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

#include "resourceReferenceScriptToken.h"

class IScriptDataObject;

//---------------------------------------------------------------------------

/// Loader for script data
class IScriptDataLoader
{
public:

	// TODO: Unify types with IScriptDataSaver
	using ObjectIndex = Uint32;
	using NameIndex = Uint32;
	using TweakDBIDIndex = Uint32;
	using ResRefIndex = Uint32;

	IScriptDataLoader();
	virtual ~IScriptDataLoader();

	// saving interface
	virtual CName MapName( const NameIndex index ) = 0;
	virtual TweakDBID MapTweakDBID( const TweakDBIDIndex index ) = 0;
	virtual red::ResourceReferenceScriptToken MapResRef( const ResRefIndex index ) = 0;
	virtual IScriptDataObject* MapObject( const ObjectIndex index ) = 0;
	virtual void ReadData( void* data, const Uint32 dataSize ) = 0;

	// helping operators for simple types
	IScriptDataLoader& operator>>( Uint8& data ) { ReadData( &data, sizeof(data) ); return *this; }
	IScriptDataLoader& operator>>( Uint16& data ) { ReadData( &data, sizeof(data) ); return *this; }
	IScriptDataLoader& operator>>( Uint32& data ) { ReadData( &data, sizeof(data) ); return *this; }
	IScriptDataLoader& operator>>( Uint64& data ) { ReadData( &data, sizeof(data) ); return *this; }
	IScriptDataLoader& operator>>( Int8& data ) { ReadData( &data, sizeof(data) ); return *this; }
	IScriptDataLoader& operator>>( Int16& data ) { ReadData( &data, sizeof(data) ); return *this; }
	IScriptDataLoader& operator>>( Int32& data ) { ReadData( &data, sizeof(data) ); return *this; }
	IScriptDataLoader& operator>>( Int64& data ) { ReadData( &data, sizeof(data) ); return *this; }
	IScriptDataLoader& operator>>( Float& data ) { ReadData( &data, sizeof(data) ); return *this; }
	IScriptDataLoader& operator>>( Double& data ) { ReadData( &data, sizeof(data) ); return *this; }
	IScriptDataLoader& operator>>( Bool& data ) { Uint8 x=0; ReadData( &x, sizeof(x) ); data = (x!=0); return *this; }

	// pointer serialization
	template< typename T >
	IScriptDataLoader& operator>>( T*& data )
	{
		ObjectIndex index = 0;
		*this >> index;

#ifndef TMP_DEBUG_SCRIPTS
		RED_FATAL_ASSERT( !MapObject( index ) || dynamic_cast< T* >( MapObject( index ) ), "Invalid static cast!" );
#endif

		data = static_cast< T* >( MapObject( index ) );

		return *this;
	}

	// name serialization
	IScriptDataLoader& operator>>( CName& name )
	{
		NameIndex index = 0;
		*this >> index;

		name = MapName( index );
		return *this;
	}

	IScriptDataLoader& operator>>( TweakDBID& id )
	{
		TweakDBIDIndex index = 0;
		*this >> index;

		id = MapTweakDBID( index );
		return *this;
	}

	IScriptDataLoader& operator>>( red::ResourceReferenceScriptToken& resRef )
	{
		ResRefIndex index = 0;
		*this >> index;

		resRef = MapResRef( index );
		return *this;
	}

	// string serialization (ansi only)
	IScriptDataLoader& operator>>( String& txt )
	{
		Uint16 length = 0;
		*this >> length;

		if ( length > 0 )
		{
			AnsiChar* buf = reinterpret_cast<AnsiChar*>( RED_ALLOCA( length + 1 ) );
			ReadData( buf, length );
			buf[ length ] = 0;

			txt = buf;
		}
		else
		{
			txt.Clear();
		}

		return *this;
	}

	// dynamic array serialization
	template< typename T >
	IScriptDataLoader& operator>>( red::DynArray< T >& ar )
	{
		Uint32 count = 0;
		*this >> count;

		ar.SetPool( red::PoolScript() );
		ar.Resize( count );

		for ( Uint32 i=0; i<count; ++i )
		{
			*this >> ar[i];
		}
		return *this;
	}
};

//---------------------------------------------------------------------------
