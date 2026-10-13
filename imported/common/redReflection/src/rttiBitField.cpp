/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#include "build.h"
#include "../../redContainers/include/fundamentalStringParser.h"
#include "rttiBitField.h"
#include "mathCommon.h"
#include "textWriter.h"
#include <limits.h>

namespace rtti
{

	BitFieldType::BitFieldType( const CName name, Uint32 size, const Bool isScripted )
		: m_name( name )
		, m_options()
		, m_size( size )
		, m_usedBitMask( 0 )
		, m_isScripted( isScripted )
	{
		RED_FATAL_ASSERT( size == 1 || size == 2 || size == 4 || size == 8, "Incompatible bitfield size. Only 8, 16, 32 and 64 bit bitfields are supported by rtti system");

		m_refName = FormatScriptedReferenceTypeName( name );
	}

	void BitFieldType::GetOptions( TNames& outNames ) const
	{
		outNames.Clear();

		for ( Uint32 i = 0; i < RED_ARRAY_COUNT( m_options ); ++i )
		{
			if ( m_options[i] == CName::NONE() )
				continue;

			outNames.PushBack( m_options[i] );
		}
	}

	const CName BitFieldType::GetBitName( const Uint64 bitIndex ) const
	{
		RED_FATAL_ASSERT( bitIndex < RED_ARRAY_COUNT( m_options ), "Invalid bit index" );
		return m_options[ bitIndex ];
	}

	const Int32 BitFieldType::GetBitValue( const CName name ) const
	{
		for ( Uint32 i=0; i<RED_ARRAY_COUNT(m_options); ++i )
			if ( m_options[i] == name )
				return (Int32)i;

		return -1;
	}

	void BitFieldType::Reset( const Uint32 size )
	{
		for ( Uint32 i=0; i<RED_ARRAY_COUNT(m_options); ++i )
			m_options[i] = CName::NONE();

		m_usedBitMask = 0;
		m_size = size;
	}

	void BitFieldType::AddBit( const CName name, const Uint64 bitMask )
	{
		// Make sure bit value is OK
		RED_FATAL_ASSERT( red::IsPowerOf2( bitMask ), "Bit filed value '%hs' in '%hs' is not a valid bit", name.AsChar(), m_name.AsChar() );

		// Set the bit name
		Uint32 bitIndex = 0;
		Uint64 bitLog = bitMask;
		while ( bitLog > 1 )
		{
			bitLog /= 2;
			bitIndex++;
		}

		// Make sure it's not duplicated
		RED_FATAL_ASSERT( !m_options[ bitIndex ], "Bit filed value '%hs' in '%hs' already specified as '%hs'", 
			name.AsChar(), m_name.AsChar(), m_options[ bitIndex ].AsChar() );

		// Set the bit name	
		m_usedBitMask |= RED_FLAG64( bitIndex );
		m_options[ bitIndex ] = name;
	}

	void BitFieldType::ReadUint64( const void* data, Uint64& outValue ) const
	{
		switch ( GetSize() )
		{
			case 1: 
				outValue = *static_cast< const Uint8*>( data );
				break;
			case 2: 
				outValue = *static_cast< const Uint16*>( data );
				break;
			case 4: 
				outValue = *static_cast< const Uint32*>( data );
				break;
			case 8: 
				outValue = *static_cast< const Uint64*>( data );
				break;
		}
	}

	void BitFieldType::WriteUint64( void* data, const Uint64 value ) const
	{
		switch ( GetSize() )
		{
			case 1:
				RED_FATAL_ASSERT( value <= std::numeric_limits<Uint8>::max(), "Out of range. Invalid value to store in bitfield");
				*static_cast<Uint8*>( data ) = static_cast<Uint8>( value );
				break;
			case 2: 
				RED_FATAL_ASSERT( value <= std::numeric_limits<Uint16>::max(), "Out of range. Invalid value to store in bitfield");
				*static_cast<Uint16*>( data ) = static_cast<Uint16>( value );
				break;
			case 4: 
				RED_FATAL_ASSERT( value <= std::numeric_limits<Uint32>::max(), "Out of range. Invalid value to store in bitfield");
				*static_cast<Uint32*>( data ) = static_cast<Uint32>( value );
				break;
			case 8: 
				*static_cast<Uint64*>( data ) = value;
				break;
			default:
				RED_FATAL( "Incompatible rtti bit field size!" );
		}
	}

	Bool BitFieldType::Compare( const void* data1, const void* data2, Uint32 ) const
	{
		Uint64 value1=0, value2=0;
		ReadUint64( data1, value1 );
		ReadUint64( data2, value2 );
		return (value1 == value2);
	}

	void BitFieldType::Copy( void* dest, const void* src ) const
	{
		Uint64 value=0;
		ReadUint64( src, value );
		WriteUint64( dest, value );
	}

	Bool BitFieldType::Serialize( IFile& file, void* data, ISerializable* owner ) const
	{
		if ( file.IsReader() )
		{
			Uint64 bitValue = 0;

			// Load set bits
			for ( ;; )
			{
				// Load bit name
				CName bitName;
				file << bitName;

				// End of list
				if ( !bitName )
					break;
	
				// Set the bit
				for ( Uint32 i=0; i<RED_ARRAY_COUNT( m_options ); i++ )
				{
					if ( m_options[i] == bitName )
					{
						bitValue |= RED_FLAG64( i );
						break;
					}
				}
			}

			// Set value
			WriteUint64( data, bitValue );
		}
		else if ( file.IsWriter() )
		{
			// Read value
			Uint64 bitValue = 0;
			ReadUint64( data, bitValue );

			// Save bits
			for ( Uint32 i=0; i<RED_ARRAY_COUNT( m_options ); i++ )
			{
				const Uint64 bitMask = RED_FLAG64( i );
				if ( (bitValue & bitMask) && m_options[i] )
				{
					// Save the bit name
					CName bitName = m_options[i];
					file << bitName;
				}
			}

			// End of array
			CName endOfList = CName::NONE();
			file << endOfList;
		}

		// Saved
		return true;
	}

	Bool BitFieldType::ToString( const void* object, String& valueString ) const
	{
		Uint64 bitValue = 0;
		ReadUint64( object, bitValue );

		// extract into the <a;b;c> kind of string
		valueString += "<";

		{
			Bool firstOption = true;
			for ( Uint32 i=0; i<RED_ARRAY_COUNT( m_options ); i++ )
			{
				const Uint64 bitMask = RED_FLAG64( i );
				if ( (bitValue & bitMask) && m_options[i] )
				{
					if ( !firstOption )
						valueString += ";";

					valueString += m_options[i].AsChar();
					firstOption = false;
				}
			}
		}

		valueString += ">";
		return true;
	}

	Bool BitFieldType::FromString( void* object, const String& valueString ) const
	{
		const AnsiChar* ptr = valueString.AsChar();
		if ( !GParseKeyword( ptr, "<" ) )
			return false;

		Uint64 bitValue = 0;

		Bool first = true;
		for ( ;; )
		{
			// end of stream
			if ( GParseKeyword( ptr, ">" ) )
				break;

			// separator
			if ( !first && !GParseKeyword(ptr, ";" ) )
				return false;

			// ident
			String bitIdent;
			if ( !GParseIdentifier( ptr, bitIdent ) )
				return false;

			const auto bitIdentName = RED_NAME_NOREG( bitIdent );
			// match name
			Bool matched = false;
			for ( Uint32 j=0; j<RED_ARRAY_COUNT(m_options); ++j )
			{
				if ( m_options[j] == bitIdentName )
				{
					const Uint64 bitMask = 1LL << j;
					bitValue |= bitMask;
					matched = true;
					break;
				}
			}

			// invalid bit
			if ( !matched )
				return false;

			// expect separator before next value
			first = false;
		}

		// Save value
		WriteUint64( object, bitValue );
		return true;
	}

} // rtti