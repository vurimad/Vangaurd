/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#include "build.h"
#include "rttiEnum.h"
#include "serializationUtils.h"
#include "rttiUtils.h"

#include "../../redFileSystem/include/file.h"

namespace rtti
{

	EnumType::EnumType( const CName name, Uint32 size, Bool scripted )
		: m_name( name )
		, m_size( size )
		, m_isScripted( scripted ? 1 : 0 )
		, m_options( red::PoolRTTI() )
		, m_values( red::PoolRTTI() )
		, m_legacyOptions( red::PoolRTTI() )
		, m_legacyValues( red::PoolRTTI() )
	{
		RED_FATAL_ASSERT( size == 1 || size == 2 || size == 4 || size == 8, "Incompatible enum size. Only 8, 16, 32 and 64 bit enums are supported by rtti system");

		m_refName = FormatScriptedReferenceTypeName( name );
	}

	void EnumType::Reset( const Uint32 size )
	{
		m_size = size;
		m_options.Clear();
		m_values.Clear();
	}

	void EnumType::Add( const CName name, const Int64 value )
	{
		m_options.PushBack( name );
		m_values.PushBack( value );
	}

	void EnumType::AddLegacy( const CName name, const Int64 value )
	{
		m_legacyOptions.PushBack( name );
		m_legacyValues.PushBack( value );
	}


	Bool EnumType::FindValue( const CName name, Int64& outValue ) const
	{
		const Uint32 numOptions = m_options.Size();
		for ( Uint32 i=0; i<numOptions; ++i )
		{
			if ( m_options[i] == name )
			{
				outValue = m_values[i];
				return true;
			}
		}

		const Uint32 numLegacyOptions = m_legacyOptions.Size();
		for ( Uint32 i=0; i<numLegacyOptions; ++i )
		{
			if ( m_legacyOptions[i] == name )
			{
				outValue = m_legacyValues[i];
				return true;
			}
		}

		return false;
	}

	Bool EnumType::FindName( const Int64 value, CName& outName ) const
	{
		const Uint32 numOptions = m_options.Size();
		for ( Uint32 i=0; i<numOptions; ++i )
		{
			if ( m_values[i] == value )
			{
				outName = m_options[i];
				return true;
			}
		}

		return false;
	}

	void EnumType::ReadInt64( const void* data, Int64& outValue ) const
	{
		switch ( GetSize() )
		{
			case 1: 
				outValue = *static_cast< const Int8*>( data );
				break;
			case 2: 
				outValue = *static_cast< const Int16*>( data );
				break;
			case 4: 
				outValue = *static_cast< const Int32*>( data );
				break;
			case 8: 
				outValue = *static_cast< const Int64*>( data );
				break;
			default:
				RED_FATAL( "Incompatible rtti enum size!" );
		}
	}

	void EnumType::WriteInt64( void* data, const Int64 value ) const
	{
		switch ( GetSize() )
		{
			case 1:
				RED_FATAL_ASSERT( value <= std::numeric_limits<Int8>::max() && value >= std::numeric_limits<Int8>::min(), "Out of range. Invalid value to store in enum");
				*static_cast<Int8*>( data ) = static_cast<Int8>( value );
				break;
			case 2:
				RED_FATAL_ASSERT( value <= std::numeric_limits<Int16>::max() && value >= std::numeric_limits<Int16>::min(), "Out of range. Invalid value to store in enum");
				*static_cast<Int16*>( data ) = static_cast<Int16>( value );
				break;
			case 4:
				RED_FATAL_ASSERT( value <= std::numeric_limits<Int32>::max() && value >= std::numeric_limits<Int32>::min(), "Out of range. Invalid value to store in enum");
				*static_cast<Int32*>( data ) = static_cast<Int32>( value );
				break;
			case 8:
				*static_cast<Int64*>( data ) = value;
				break;
			default:
				RED_FATAL( "Incompatible rtti enum size!" );
		}
	}

	Bool EnumType::Compare( const void* data1, const void* data2, Uint32 ) const
	{
		Int64 value1 = 0, value2 = 0;
		ReadInt64( data1, value1 );
		ReadInt64( data2, value2 );

		return (value1 == value2);
	}

	void EnumType::Copy( void* dest, const void* src ) const
	{
		Int64 value = 0;
		ReadInt64( src, value );
		WriteInt64( dest, value );
	}

	Bool EnumType::Serialize( IFile& file, void* data, ISerializable* owner ) const
	{
		if ( file.IsReader() )
		{
			CName name;
			file << name;

			Int64 value = 0;
			if ( FindValue( name, value ) )
			{
				WriteInt64( data, value );
			}
		}
		else if ( file.IsWriter() )
		{
			Int64 value = 0;
			ReadInt64( data, value );

			CName name;
			FindName( value, name ); 
			file << name;
		}

		return true;
	}

	Bool EnumType::ToString( const void* object, String& valueString ) const
	{
		Int64 value = 0;
		ReadInt64( object, value );

		CName name;
		if ( FindName( value, name ) )
		{
			valueString = name.AsChar();
			return true;
		}

		return false;
	}

	Bool EnumType::FromString( void* object, const String& valueString ) const
	{
		auto view = red::StringView{ valueString };
		const auto it = view.Find( '.' );
		if( it != red::StringView::npos )
		{
			view.RemovePrefix( it + 1 );
		}
		const CName name = RED_NAME_NOREG( view );

		Int64 value = 0;
		if ( FindValue( name, value ) )
		{
			WriteInt64( object, value );
			return true;
		}
	
		return false;
	}

	CName EnumType::GetName( const void * data ) const
	{
		Int64 value = 0;
		ReadInt64( data, value );

		CName name;
		FindName( value, name ); 
		return name;
	}

	void EnumType::SetValue( void * data, CName enumName ) const
	{
		Int64 value = 0;
		if ( FindValue( enumName, value ) )
		{
			WriteInt64( data, value );
		}
	}

} // rtti