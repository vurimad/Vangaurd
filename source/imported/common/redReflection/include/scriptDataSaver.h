/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

class IScriptDataObject;

namespace red
{
	class ResourceReferenceScriptToken;
}

//---------------------------------------------------------------------------

/// Saver for script data
class IScriptDataSaver
{
public:
	using ObjectIndex = Uint32;
	using NameIndex = Uint32;
	using TweakDBIDIndex = Uint32;
	using ResRefIndex = Uint32;

	IScriptDataSaver();
	virtual ~IScriptDataSaver();

	// saving interface
	virtual NameIndex MapName( const CName name ) = 0;
	virtual TweakDBIDIndex MapTweakDBID( const TweakDBID id ) = 0;
	virtual ResRefIndex MapResRef( const red::ResourceReferenceScriptToken& resRef ) = 0;
	virtual ObjectIndex MapObject( const IScriptDataObject* object ) = 0;
	virtual void WriteData( const void* data, const Uint32 dataSize ) = 0;

	// helping operators for simple types
	IScriptDataSaver& operator<<( const Uint8 data ) { WriteData( &data, sizeof(data) ); return *this; }
	IScriptDataSaver& operator<<( const Uint16 data ) { WriteData( &data, sizeof(data) ); return *this; }
	IScriptDataSaver& operator<<( const Uint32 data ) { WriteData( &data, sizeof(data) ); return *this; }
	IScriptDataSaver& operator<<( const Uint64 data ) { WriteData( &data, sizeof(data) ); return *this; }
	IScriptDataSaver& operator<<( const Int8 data ) { WriteData( &data, sizeof(data) ); return *this; }
	IScriptDataSaver& operator<<( const Int16 data ) { WriteData( &data, sizeof(data) ); return *this; }
	IScriptDataSaver& operator<<( const Int32 data ) { WriteData( &data, sizeof(data) ); return *this; }
	IScriptDataSaver& operator<<( const Int64 data ) { WriteData( &data, sizeof(data) ); return *this; }
	IScriptDataSaver& operator<<( const Float data ) { WriteData( &data, sizeof(data) ); return *this; }
	IScriptDataSaver& operator<<( const Double data ) { WriteData( &data, sizeof(data) ); return *this; }
	IScriptDataSaver& operator<<( const Bool data ) { Uint8 x = data ? 1 : 0; WriteData( &x, sizeof(x) ); return *this; }

	// pointer serialization
	IScriptDataSaver& operator<<( const IScriptDataObject* data )
	{
		const ObjectIndex index = MapObject( data );
		*this << index;
		return *this;
	}

	// name serialization
	IScriptDataSaver& operator<<( const CName name )
	{
		const NameIndex index = MapName( name );
		*this << index;
		return *this;
	}

	IScriptDataSaver& operator<<( const TweakDBID id )
	{
		const TweakDBIDIndex index = MapTweakDBID( id );
		*this << index;
		return *this;
	}

	IScriptDataSaver& operator<<( const red::ResourceReferenceScriptToken& resRef )
	{
		const ResRefIndex index = MapResRef( resRef );
		*this << index;
		return *this;
	}

	// string serialization (ansi only)
	IScriptDataSaver& operator<<( const String& txt )
	{
		const Uint16 length = (Uint16) txt.Length();
		*this << length;
		WriteData( txt.AsChar(), length * sizeof(AnsiChar) );
		return *this;
	}

	// dynamic array serialization
	template< typename T >
	IScriptDataSaver& operator<<( const red::DynArray< T >& ar )
	{
		const Uint32 count = ar.Size();
		*this << count;

		for ( Uint32 i=0; i<count; ++i )
		{
			*this << ar[i];
		}

		return *this;
	}
};
