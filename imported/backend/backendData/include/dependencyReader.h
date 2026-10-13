/*
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#pragma once

namespace dependency
{
using Timestamp = Uint64;
class Hash;

class Reader;
using ReaderScope = red::UniquePtr< Reader >;

class ReaderArray;
using ReaderArrayScope = red::UniquePtr< ReaderArray >;

// Interface for reading dependencies from disk in some format
// TODO: Replace with custom package serialiser once it has support for JSON and RTTI (non-ISerialisable) types
class Reader
{
	RED_USE_MEMORY_POOL( red::PoolBackend );

public:
	virtual ~Reader() = default;

	virtual bool HasErrors() const = 0;

	virtual ReaderScope StartScope( const red::StringView& key ) = 0;
	virtual ReaderArrayScope StartArray( const red::StringView& key ) = 0;

	virtual bool HasValue( const red::StringView& key ) const = 0;

	virtual void ReadName( const red::StringView& key, CName& name ) = 0;
	virtual void ReadBool( const red::StringView& key, bool& value ) = 0;
	virtual void ReadNumber( const red::StringView& key, Int32& number ) = 0;
	virtual void ReadNumber( const red::StringView& key, Uint32& number ) = 0;
	virtual void ReadNumber( const red::StringView& key, Int64& number ) = 0;
	virtual void ReadNumber( const red::StringView& key, Uint64& number ) = 0;
	virtual void ReadFloat( const red::StringView& key, float& number ) = 0;
	virtual void ReadString( const red::StringView& key, red::String& value ) = 0;

	virtual void ReadPath( const red::StringView& key, res::ResourcePath& value ) = 0;
	virtual void ReadTimestamp( const red::StringView& key, Timestamp& timestamp ) = 0;
	virtual void ReadHash( const red::StringView& key, Hash& hash ) = 0;

	template < typename EnumType >
	void ReadEnum( const red::StringView& key, EnumType& value )
	{
		const rtti::IType* type = GetRttiSystem().FindTypeByHash( GetNativeTypeHash< EnumType >() );
		RED_FATAL_ASSERT( type && type->GetType() == RT_Enum, "Must be an enumeration" );
		const rtti::EnumType* enumType = static_cast< const rtti::EnumType* >( type );
		if ( enumType )
		{
			CName valueName;
			this->ReadName( key, valueName );

			Int64 intValue = 0ll;
			if ( enumType->FindValue( valueName, intValue ) )
			{
				value = static_cast< EnumType >( intValue );
			}
		}
	}
};

// Interface for reading arrays in dependencies from disk
// NOTE: Contains read function for only the types that make sense to be read from an array
class ReaderArray
{
	RED_USE_MEMORY_POOL( red::PoolBackend );

public:
	virtual ~ReaderArray() = default;

	virtual Uint32 Size() const = 0;
	virtual ReaderScope GetScope( Uint32 index ) const = 0;

	virtual void ReadName( Uint32 index, CName& name ) = 0;
	virtual void ReadNumber( Uint32 index, Int32& number ) = 0;
	virtual void ReadNumber( Uint32 index, Uint32& number ) = 0;
	virtual void ReadNumber( Uint32 index, Int64& number ) = 0;
	virtual void ReadNumber( Uint32 index, Uint64& number ) = 0;
	virtual void ReadFloat( Uint32 index, float& number ) = 0;
	virtual void ReadString( Uint32 index, red::String& value ) = 0;
	virtual void ReadPath( Uint32 index, res::ResourcePath& value ) = 0;
};

} // dependency
