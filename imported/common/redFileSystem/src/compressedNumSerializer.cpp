#include "build.h"

#include "file.h"
#include "compressedNumSerializer.h"

//#if 0
void operator<<( IFile& file, CCompressedNumSerializer c )
{
	Uint32 noSign = Abs( c.m_num );

	Uint8 byt = (Uint8)((( c.m_num >= 0 )?(0):(0x80) ) | ( (noSign > 0x3F) ? (0x40 | (noSign & 0x3F) ) : (noSign & 0x3F) ));
	file.Serialize( &byt, 1 );
	Uint8 firstByte = byt;

	c.m_num = 0;
	(Uint32&)c.m_num = byt & 0x3F;

	// larger or equal than 2 ^ 6
	if ( byt & 0x40 )
	{
		// more bytes than 1
		noSign >>= 6;

		byt = (Uint8)( (( noSign > 0x7f ) ? (0x80):(0)) | (noSign & 0x7F) );
		file.Serialize( &byt, 1 );
		(Uint32&)c.m_num |= ( byt & 0x7F )<< 6;

		// larger than 2 ^ 13
		if ( byt & 0x80 )
		{
			// more bytes than 2
			noSign >>= 7;

			byt = (Uint8)( (( noSign > 0x7f ) ? (0x80):(0)) | (noSign & 0x7F) );
			file.Serialize( &byt, 1 );
			(Uint32&)c.m_num |= ( byt & 0x7F )<< 13;

			// larger than 2 ^ 20
			if ( byt & 0x80 )
			{
				// more bytes than 3
				noSign >>= 7;

				byt = (Uint8)( (( noSign > 0x7f ) ? (0x80):(0)) | (noSign & 0x7F) );
				file.Serialize( &byt, 1 );
				(Uint32&)c.m_num |= ( byt & 0x7F )<< 20;

				// larger than 2 ^ 27
				if ( byt & 0x80 )
				{
					// more bytes than 4
					noSign >>= 7;

					byt = (Uint8)( (( noSign > 0x7f ) ? (0x80):(0)) | (noSign & 0x7F) );
					file.Serialize( &byt, 1 );
					(Uint32&)c.m_num |= ( byt & 0x7F )<< 27;
				}
			}
		}
	}
	c.m_num = firstByte & 0x80 ? -c.m_num : c.m_num;

//	ASSERT( file.IsReader() || oldVal == c.m_num );
}
//#endif

// Implementation of signed LEB128. Maximum 5 byte write size.
void WriteVarint_LEB128_Signed( IFile& writer, const Int32 value )
{
	Uint32 valueAbs{ static_cast< Uint32 >( value >= 0 ? value : -value ) };
	Uint64 target{};
	Uint8* current = reinterpret_cast< Uint8* >( &target );

	*current = valueAbs & 0b0011'1111;
		
	if ( value < 0 )
	{
		*current |= 0b1000'0000;
	}
	
	if ( valueAbs > 0b0011'1111 ) // larger or equal than 2 ^ 6
	{
		valueAbs >>= 6;
		*current |= 0b0100'0000;
		*++current = valueAbs & 0b0111'1111;
		
		if ( valueAbs > 0b0111'1111 ) // larger than 2 ^ 13
		{
			valueAbs >>= 7;
			*current |= 0b1000'0000;
			*++current = valueAbs & 0b0111'1111;

			if ( valueAbs > 0b0111'1111 ) // larger than 2 ^ 20
			{
				valueAbs >>= 7;
				*current |= 0b1000'0000;
				*++current = valueAbs & 0b0111'1111;
				
				if ( valueAbs > 0b0111'1111 ) // larger than 2 ^ 27
				{
					valueAbs >>= 7;
					*current |= 0b1000'0000;
					*++current = valueAbs;
				}
			}
		}
	}

	writer.Serialize( &target, current - reinterpret_cast< Uint8* >( &target ) + 1 );
}

// Implementation of unsigned LEB128. Maximum 5 byte write size.
void WriteVarint_LEB128_Unsigned( IFile& writer, const Uint32 value )
{
	auto valueAbs{ value };
	Uint64 target{};
	Uint8* current = reinterpret_cast< Uint8* >( &target );

	*current = valueAbs & 0b0111'1111;
		
	if ( valueAbs > 0b0111'1111 ) // larger or equal than 2 ^ 7
	{
		valueAbs >>= 7;
		*current |= 0b1000'0000;
		*++current = valueAbs & 0b0111'1111;
		
		if ( valueAbs > 0b0111'1111 ) // larger than 2 ^ 14
		{
			valueAbs >>= 7;
			*current |= 0b1000'0000;
			*++current = valueAbs & 0b0111'1111;

			if ( valueAbs > 0b0111'1111 ) // larger than 2 ^ 21
			{
				valueAbs >>= 7;
				*current |= 0b1000'0000;
				*++current = valueAbs & 0b0111'1111;
				
				if ( valueAbs > 0b0111'1111 ) // larger than 2 ^ 28
				{
					valueAbs >>= 7;
					*current |= 0b1000'0000;
					*++current = valueAbs;
				}
			}
		}
	}

	writer.Serialize( &target, current - reinterpret_cast< Uint8* >( &target ) + 1 );
}

// Implementation of modified 4-byte unsigned LEB128. Maximum 4 byte size. 512MiB (29 bit) addressable max.
void WriteVarint_LEB128m4_Unsigned( IFile& writer, const Uint32 value )
{
	auto valueAbs{ value };
	Uint32 target{};
	Uint8* current = reinterpret_cast< Uint8* >( &target );

	*current = valueAbs & 0b0111'1111;
		
	if ( valueAbs > 0b0111'1111 ) // larger or equal than 2 ^ 7
	{
		valueAbs >>= 7;
		*current |= 0b1000'0000;
		*++current = valueAbs & 0b0111'1111;
		
		if ( valueAbs > 0b0111'1111 ) // larger than 2 ^ 14
		{
			valueAbs >>= 7;
			*current |= 0b1000'0000;
			*++current = valueAbs & 0b0111'1111;

			if ( valueAbs > 0b0111'1111 ) // larger than 2 ^ 21
			{
				valueAbs >>= 7;
				*current |= 0b1000'0000;
				*++current = valueAbs & 0b1111'1111;
			}
		}
	}

	writer.Serialize( &target, current - reinterpret_cast< Uint8* >( &target ) + 1 );
}

// Implementation of signed LEB128. Maximum 5 byte read size.
Int32 ReadVarint_LEB128_Signed( IFile& reader )
{
	Uint8 b{};
	reader.Serialize( &b, 1 );
	Int32 result = ( b & 0b0011'1111 );
	const auto signBit = ( b & 0b1000'0000 ) != 0;
	
	if( b & 0b0100'0000 ) // larger or equal than 2 ^ 6
	{
		reader.Serialize( &b, 1 );
		result |= ( b & 0b0111'1111 ) << 6;
		
		if( b & 0b1000'0000 ) // larger than 2 ^ 13
		{
			reader.Serialize( &b, 1 );
			result |= ( b & 0b0111'1111 ) << 13;
			
			if( b & 0b1000'0000 ) // larger than 2 ^ 20
			{
				reader.Serialize( &b, 1 );
				result |= ( b & 0b0111'1111 ) << 20;
				
				if( b & 0b1000'0000 ) // larger than 2 ^ 27
				{
					reader.Serialize( &b, 1 );
					result |= b << 27;
				}
			}
		}
	}

	if( signBit )
	{
		result = -result;
	}

	return result;
}

// Implementation of standard unsigned LEB128. Maximum 5 byte read size.
Uint32 ReadVarint_LEB128_Unsigned( IFile& reader )
{
	Uint8 b{};
	reader.Serialize( &b, 1 );
	Int32 result = ( b & 0b0111'1111 );
	
	if( b & 0b1000'0000 ) // larger or equal than 2 ^ 7
	{
		reader.Serialize( &b, 1 );
		result |= ( b & 0b0111'1111 ) << 7;
		
		if( b & 0b1000'0000 ) // larger than 2 ^ 14
		{
			reader.Serialize( &b, 1 );
			result |= ( b & 0b0111'1111 ) << 14;
			
			if( b & 0b1000'0000 ) // larger than 2 ^ 21
			{
				reader.Serialize( &b, 1 );
				result |= ( b & 0b0111'1111 ) << 21;
				
				if( b & 0b1000'0000 ) // larger than 2 ^ 28
				{
					reader.Serialize( &b, 1 );
					result |= b << 28;
				}
			}
		}
	}

	return result;
}

// Implementation of modified 4-byte unsigned LEB128. Maximum 4 byte size. 512MiB (29 bit) addressable max.
Uint32 ReadVarint_LEB128m4_Unsigned( IFile& reader )
{
	Uint8 b{};
	reader.Serialize( &b, 1 );
	Int32 result = ( b & 0b0111'1111 );
	
	if( b & 0b1000'0000 ) // larger or equal than 2 ^ 7
	{
		reader.Serialize( &b, 1 );
		result |= ( b & 0b0111'1111 ) << 7;

		if( b & 0b1000'0000 ) // larger than 2 ^ 14
		{
			reader.Serialize( &b, 1 );
			result |= ( b & 0b0111'1111 ) << 14;
			
			if( b & 0b1000'0000 ) // larger than 2 ^ 21
			{
				reader.Serialize( &b, 1 );
				result |= b << 21;
			}
		}
	}

	return result;
}
