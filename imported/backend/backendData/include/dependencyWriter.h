/*
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "../../../common/redReflection/include/rttiSystem.h"

namespace dependency
{
using Timestamp = Uint64;
class Hash;

class Writer;
using WriterScope = red::UniquePtr< Writer >;

class WriterArray;
using WriterArrayScope = red::UniquePtr< WriterArray >;

// Interface for writing dependencies to disk in some format
// TODO: Replace with custom package serialiser once it has support for JSON and RTTI (non-ISerialisable) types
class Writer
{
	RED_USE_MEMORY_POOL( red::PoolBackend );

public:
	virtual ~Writer() = default;

	virtual WriterScope StartScope( const red::StringView& key ) = 0;
	virtual WriterArrayScope StartArray( const red::StringView& key, Uint32 elements ) = 0;

	virtual void WriteName( const red::StringView& key, const CName name ) = 0;
	virtual void WriteBool( const red::StringView& key, bool value ) = 0;
	virtual void WriteNumber( const red::StringView& key, Int32 number ) = 0;
	virtual void WriteNumber( const red::StringView& key, Uint32 number ) = 0;
	virtual void WriteNumber( const red::StringView& key, Int64 number ) = 0;
	virtual void WriteNumber( const red::StringView& key, Uint64 number ) = 0;
	virtual void WriteFloat( const red::StringView& key, float number ) = 0;
	virtual void WriteString( const red::StringView& key, const red::StringView& value ) = 0;

	virtual void WritePath( const red::StringView& key, const res::ResourcePath& value ) = 0;
	virtual void WriteTimestamp( const red::StringView& key, const Timestamp timestamp ) = 0;
	virtual void WriteHash( const red::StringView& key, const Hash& hash ) = 0;

	template < typename EnumType >
	void WriteEnum( const red::StringView& key, const EnumType value )
	{
		const rtti::IType* type = GetRttiSystem().FindTypeByHash( GetNativeTypeHash< EnumType >() );
		RED_FATAL_ASSERT( type && type->GetType() == RT_Enum, "Must be an enumeration" );
		const rtti::EnumType* enumType = static_cast< const rtti::EnumType* >( type );
		CName valueName;
		if ( enumType && enumType->FindName( static_cast< Int64 >( value ), valueName ) )
		{
			this->WriteName( key, valueName );
		}
	}
};

// Interface for writing array inside dependencies to disk in some format
class WriterArray
{
	RED_USE_MEMORY_POOL( red::PoolBackend );

public:
	virtual ~WriterArray() = default;

	virtual WriterScope StartScope( const red::StringView& key ) = 0;

	virtual void WriteName( const CName name ) = 0;
	virtual void WriteNumber( Int32 number ) = 0;
	virtual void WriteNumber( Uint32 number ) = 0;
	virtual void WriteNumber( Int64 number ) = 0;
	virtual void WriteNumber( Uint64 number ) = 0;
	virtual void WriteFloat( float number ) = 0;
	virtual void WriteString( const red::StringView& value ) = 0;
	virtual void WritePath( const res::ResourcePath& value ) = 0;
};

} // dependency
