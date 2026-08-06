/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

//////////////////////////////////////////////////////////////////////////
// headers
#include "build.h"
#include "systemTypesFormatters.h"
#include "log.h"
#include <algorithm>


//////////////////////////////////////////////////////////////////////////
// char* - check old c-style format
Bool red::CheckFormatString( const char* val, const char* formatToCheck )
{
	if ( formatToCheck )
	{
		const char* checkFormat = std::strchr( formatToCheck, '%' );
		const char* checkh = checkFormat ? std::strstr( checkFormat+1, "hs" ) : nullptr;
		const char* checkl = (!checkh && checkFormat) ? std::strchr( checkFormat+1, 'l' ) : nullptr;
		const char* checks = (!checkh && checkFormat) ? std::strchr( checkFormat+1, 's' ) : nullptr;
		
		if ( checkFormat && !checkl && (checkh || checks) )
		{
			return true;
		}
	}
	return false;
}


//////////////////////////////////////////////////////////////////////////
// wchar_t* (UniChar*) - check old c-style format
Bool red::CheckFormatString( const UniChar* val, const char* formatToCheck )
{
	if ( formatToCheck )
	{
		const char* checkFormat = std::strchr( formatToCheck, '%' );
		const char* checkl = checkFormat ? std::strstr( checkFormat+1, "ls" ) : nullptr;

		if ( checkFormat && checkl )
		{
			return true;
		}
	}
	return false;
}


//////////////////////////////////////////////////////////////////////////
// Bool - old or new format
Bool red::CheckFormatString( const Bool val, const char* formatToCheck )
{
	if ( formatToCheck )
	{
		const char* checkFormatOld = std::strchr( formatToCheck, '%' );	
		if ( checkFormatOld )
		{
			return true;
		}
	}
	return false;
}

// custom bool formatter
// supports custom strings for 'true' and 'false' values
// eg. {tak|nie}, {1|0}, {fuck|fest}..
// or  %tak|nie, %1|0, %fuck|fest
Bool red::ToBuffer( char* buffer, const Uint32 bufferLen, const Bool val, Int32& written, const char* formatString )
{
	if ( buffer && bufferLen )
	{
		if ( formatString && (red::CheckFormatString( val, formatString ) || red::prv::CheckNewFormatString( val, formatString )) )
		{
			// auto-format {true|false}
			const char* separator = std::strchr( formatString, '|' );
			if ( !separator )
			{
				written += red::SNPrintFUnsafe( buffer, bufferLen, red::GetFormatString( val ), val ? "true" : "false" );
				return true;
			}

			// custom formatting
			size_t howManyChars = separator-formatString-1;
			const char* boolValueString = formatString+1;
			if ( !val )
			{
				boolValueString = separator+1;
				howManyChars = std::strlen( formatString )-3-howManyChars;
			}

			// one more char on old format
			if ( !red::prv::CheckNewFormatString( val, formatString ) )
				++howManyChars;

			// print
			howManyChars = std::min( howManyChars, static_cast<size_t>( bufferLen ) );
			red::Strcpy( buffer, boolValueString, bufferLen, howManyChars );
			buffer[howManyChars] = '\0';
			written += static_cast<Int32>( howManyChars );
			return true;
		}
	}
	return false;
}


//////////////////////////////////////////////////////////////////////////
// Int8 - check old c-style format
Bool red::CheckFormatString( const Int8 val, const char* formatToCheck )
{
	if ( formatToCheck )
	{
		const char* checkFormat = std::strchr( formatToCheck, '%' );
		const char* checkhh = checkFormat ? std::strstr( checkFormat+1, "hh" ) : nullptr;
		const char* check = checkhh ? std::strpbrk( checkhh+1, "idoxX" ) : nullptr;

		if ( checkFormat && checkhh && check )
		{
			return true;
		}
	}
	return false;
}


//////////////////////////////////////////////////////////////////////////
// Int16 - check old c-style format
Bool red::CheckFormatString( const Int16 val, const char* formatToCheck )
{
	if ( formatToCheck )
	{
		const char* checkFormat = std::strchr( formatToCheck, '%' );
		const char* checkh = checkFormat ? std::strchr( checkFormat+1, 'h' ) : nullptr;
		const char* check = checkh ? std::strpbrk( checkh+1, "idoxX" ) : nullptr;

		if ( checkFormat && checkh && check )
		{
			return true;
		}
	}
	return false;
}

//////////////////////////////////////////////////////////////////////////
// Int32 - check old c-style format
Bool red::CheckFormatString( const Int32 val, const char* formatToCheck )
{
	if ( formatToCheck )
	{
		const char* checkFormat = std::strchr( formatToCheck, '%' );
		const char* check = checkFormat ? std::strpbrk( checkFormat+1, "idoxX" ) : nullptr;

		if ( checkFormat && check )
		{
			return true;
		}
	}
	return false;
}


//////////////////////////////////////////////////////////////////////////
// Int64 - check old c-style format
Bool red::CheckFormatString( const Int64 val, const char* formatToCheck )
{
	if ( formatToCheck )
	{
		const char* checkFormat = std::strchr( formatToCheck, '%' );
		const char* checkll = checkFormat ? std::strstr( checkFormat+1, "ll" ) : nullptr;
		const char* check = checkll ? std::strpbrk( checkll+1, "idoxX" ) : nullptr;

		if ( checkFormat && checkll && check )
		{
			return true;
		}
	}
	return false;
}


//////////////////////////////////////////////////////////////////////////
// Uint8 - check old c-style format
Bool red::CheckFormatString( const Uint8 val, const char* formatToCheck )
{
	if ( formatToCheck )
	{
		const char* checkFormat = std::strchr( formatToCheck, '%' );
		const char* checkhh = checkFormat ? std::strstr( checkFormat+1, "hh" ) : nullptr;
		const char* check = checkhh ? std::strpbrk( checkhh+1, "uoxX" ) : nullptr;

		if ( checkFormat && checkhh && check )
		{
			return true;
		}
	}
	return false;
}


//////////////////////////////////////////////////////////////////////////
// Uint16 - check old c-style format
Bool red::CheckFormatString( const Uint16 val, const char* formatToCheck )
{
	// check custom
	if ( formatToCheck )
	{
		const char* checkFormat = std::strchr( formatToCheck, '%' );
		const char* checkh = checkFormat ? std::strchr( checkFormat+1, 'h' ) : nullptr;
		const char* check = checkh ? std::strpbrk( checkh+1, "uoxX" ) : nullptr;

		if ( checkFormat && checkh && check )
		{
			return true;
		}
	}
	return false;
}


//////////////////////////////////////////////////////////////////////////
// Uint32 - check old c-style format
Bool red::CheckFormatString( const Uint32 val, const char* formatToCheck )
{
	if ( formatToCheck )
	{
		const char* checkFormat = std::strchr( formatToCheck, '%' );
		const char* check = checkFormat ? std::strpbrk( checkFormat+1, "uoxX" ) : nullptr;

		if ( checkFormat && check )
		{
			return true;
		}
	}
	return false;
}


//////////////////////////////////////////////////////////////////////////
// Uint64 - check old c-style format
Bool red::CheckFormatString( const Uint64 val, const char* formatToCheck )
{
	if ( formatToCheck )
	{
		const char* checkFormat = std::strchr( formatToCheck, '%' );
		const char* checkll = checkFormat ? std::strstr( checkFormat+1, "ll" ) : nullptr;
		const char* check = checkll ? std::strpbrk( checkll+1, "uoxX" ) : nullptr;

		if ( checkFormat && checkll && check )
		{
			return true;
		}
	}
	return false;
}


//////////////////////////////////////////////////////////////////////////
// Float - check old c-style format
Bool red::CheckFormatString( const Float val, const char* formatToCheck )
{
	if ( formatToCheck )
	{
		const char* checkFormat = std::strchr( formatToCheck, '%' );
		const char* checkFloat = checkFormat ? std::strpbrk( checkFormat+1, "fFeEgGaA" ) : nullptr;

		if ( checkFormat && checkFloat )
		{
			return true;
		}
	}
	return false;
}


//////////////////////////////////////////////////////////////////////////
// Double - check old c-style format
Bool red::CheckFormatString( const Double val, const char* formatToCheck )
{
	if ( formatToCheck )
	{
		const char* checkFormat = std::strchr( formatToCheck, '%' );
		const char* checkDouble = checkFormat ? std::strpbrk( checkFormat+1, "fFeEgGaA" ) : nullptr;

		if ( checkFormat && checkDouble )
		{
			return true;
		}
	}
	return false;
}


//////////////////////////////////////////////////////////////////////////
// void* - check old c-style format
Bool red::CheckFormatString( const void* val, const char* formatToCheck )
{
	if ( formatToCheck )
	{
		const char* checkFormat = std::strchr( formatToCheck, '%' );
		const char* check = checkFormat ? std::strpbrk( checkFormat+1, "xXp" ) : nullptr;
		
		if ( checkFormat && check )
		{
			return true;
		}
	}
	return false;
}

//////////////////////////////////////////////////////////////////////////
// RUID
Bool red::CheckFormatString( const red::RUID val, const char* formatToCheck )
{
	if (formatToCheck)
	{
		// old c-style format
		const char* checkFormat = std::strchr( formatToCheck, '%' );
		if (checkFormat)
		{
			return true;
		}
	}
	return false;
}

Bool red::ToBuffer( char* buffer, const red::Uint32 bufferLen, const red::RUID val, red::Int32& written, const char* formatString /*= nullptr*/ )
{
	if (buffer && bufferLen)
	{
		if (formatString && ( red::CheckFormatString( val, formatString ) || red::prv::CheckNewFormatString( val, formatString ) ))
		{
			// '0000000000000000':
			const Uint64 numVal = reinterpret_cast<const Uint64&>( val );
			written += red::SNPrintFUnsafe( buffer, bufferLen, red::GetFormatString( val ), numVal );
			return true;
		}
	}
	return false;
}

Bool red::CheckFormatString( const red::RUIDRef val, const char* formatToCheck )
{
	return CheckFormatString( static_cast< RUID >( val ), formatToCheck );
}

Bool red::ToBuffer( char* buffer, const red::Uint32 bufferLen, const red::RUIDRef val, red::Int32& written, const char* formatString /*= nullptr*/ )
{
	return ToBuffer( buffer, bufferLen, static_cast< RUID >( val ), written, formatString );
}

//////////////////////////////////////////////////////////////////////////
// GUID
Bool red::CheckFormatString(const GUID& val, const char* formatToCheck)
{
	if (formatToCheck)
	{
		// old c-style format
		const char* checkFormat = std::strchr(formatToCheck, '%');
		if (checkFormat)
		{
			return true;
		}
	}
	return false;
}

// custom GUID formatter
// todo: https://msdn.microsoft.com/en-us/library/97af8hh4%28v=vs.110%29.aspx
// right now default format only is supported: '00000000-00000000-00000000-00000000'
Bool red::ToBuffer(char* buffer, const Uint32 bufferLen, const red::GUID& val, Int32& written, const char* formatString)
{
	if (buffer && bufferLen)
	{
		if (formatString && (red::CheckFormatString(val, formatString) || red::prv::CheckNewFormatString(val, formatString)))
		{
			// '00000000-00000000-00000000-00000000':
			written += red::SNPrintFUnsafe(buffer, bufferLen, red::GetFormatString(val), val.parts.A, val.parts.B, val.parts.C, val.parts.D);
			return true;
		}
	}
	return false;
}
