#pragma once

//////////////////////////////////////////////////////////////////////////
// headers
#include "../src/systemAssert.h"
#include <cctype>


//////////////////////////////////////////////////////////////////////////
// macros
#define RED_VPRINTF_FORMAT_CHECK


/////////////////////////////////////////////////////////////////////////
// sprintf with type check
namespace red
{
	namespace prv
	{
		// check new format
		template <typename T>
		RED_INLINE Bool CheckNewFormatString( T&& val, const char* formatString )
		{
			RED_UNUSED( val ); 

			if ( formatString )
			{
				const char* checkFormatNewStart = std::strchr( formatString, '{' );
				const char* checkFormatNewEnd = std::strchr( formatString, '}' );

				if ( checkFormatNewStart && checkFormatNewEnd && checkFormatNewStart < checkFormatNewEnd )
				{
					return true;
				}
			}
			return false;
		}

		// prepare format string
		RED_INLINE Bool PrepareFormatString( const char* format, const char* defaultFormat, char* outString, const Uint32 outStringLen )
		{
			if ( format && defaultFormat )
			{
				size_t formatLen = std::strlen( format );

				// old format
				if ( format[0] == '%' )
				{
					// with specified type
					const char* formatValuesEnd = std::strpbrk( format, "idulhsfFcpeEgGaAnxXo" );
					if ( !formatValuesEnd )
					{
						format = defaultFormat;
						formatValuesEnd = defaultFormat+1;
						formatLen = std::strlen( defaultFormat ); 
					}

					// preparing format string
					outString[0] = '%';
					const size_t formatValuesLen = formatValuesEnd-(format+1);
					if ( formatValuesLen )
					{
						red::Strcpy( outString+1, format+1, outStringLen-1, formatValuesLen );
					}
					red::Strcpy( outString+1+formatValuesLen, formatValuesEnd, outStringLen-1-formatValuesLen, formatLen-(formatValuesEnd-format) );
					outString[ formatValuesLen+formatLen ] = '\0';
					return true;
				}

				// new format - only formating args inside {..}
				else if ( format[0] == '{' )
				{
					// preparing format string
					if ( format[ formatLen-1] == '}' )
					{
						outString[0] = '%';
						const size_t formatValuesLen = formatLen-2;
						const size_t defaultFormatLen = std::strlen( defaultFormat );
						if ( formatValuesLen )
						{
							red::Strcpy( outString+1, format+1, outStringLen-1, formatValuesLen );
						}
						red::Strcpy( outString+1+formatValuesLen, defaultFormat+1, outStringLen-1-formatValuesLen, defaultFormatLen-1 );
						outString[ formatValuesLen+defaultFormatLen ] = '\0';
						return true;
					}
				}
			}
			return false;
		}
	}

	// print to buffer
	template <typename T>
	RED_INLINE Bool ToBuffer( char* buffer, const Uint32 bufferLen, T&& val, Int32& written, const char* formatString )
	{
		static_assert( std::is_fundamental< typename std::remove_reference< T >::type >::value ||
					   std::is_pointer< typename std::remove_reference< typename std::decay< T >::type >::type >::value || 
					   std::is_enum< typename std::remove_reference< T >::type >::value, "Val must varargs compatible" );

		if ( buffer && bufferLen )
		{
			// using provided format string
			if ( formatString && (red::CheckFormatString( std::forward<T>(val), formatString ) || prv::CheckNewFormatString( std::forward<T>(val), formatString )) )
			{
				char outFormatString[ 64 ];
				prv::PrepareFormatString( formatString, red::GetFormatString( std::forward<T>(val) ), outFormatString, sizeof( outFormatString ) );
				written += red::SNPrintFUnsafe( buffer, bufferLen, outFormatString, val );
				return true;
			}
		}
		written = 0;
		return false;
	}

	namespace prv
	{
		RED_INLINE Int32 SNPrintFInternal( AnsiChar* buffer, Uint32 bufferLen, const AnsiChar* format, const Uint32 line, const char* file )
		{
			RED_UNUSED( line ); 
			RED_UNUSED( file ); 

			const size_t len = red::Strlen( format );
			const Bool copyRestOfFormat = len ? red::Strcpy( buffer, format, bufferLen, len ) : false;
			return copyRestOfFormat ? static_cast<Int32>( len ) : 0;
		}

		template <typename T, typename... Args>
		RED_INLINE Int32 SNPrintFInternal( AnsiChar* buffer, Uint32 bufferLen, const AnsiChar* format, const Uint32 line, const char* file, T&& val, Args&& ... args )
		{
#if !defined( RED_LOGGING_ENABLED )
			RED_UNUSED( line );
			RED_UNUSED( file );
#endif
			RED_SYSTEM_ASSERT( buffer, "Empty buffer!" );
			RED_SYSTEM_ASSERT( format, "Empty format!" );
			if ( buffer == nullptr || format == nullptr )
				return 0;

			// init
			Uint32 bufPos = 0;
			char formatView[ 64 ] = { 0 };
			const Uint32 formatViewSize = static_cast<Uint32>( sizeof( formatView )-1 );

			const char* s = format;
			while ( *s )
			{
				// beginning of old c-style format
				if ( *s == '%' )
				{
					if ( *(s + 1) == '%' )
					{
						++s;
					}
					else
					{
						// begin
						const char* send = s+1;

						// looking for the format end
						char c = *send;
						while ( (isalnum( c ) || (c == '.' && isdigit(*(send+1))) || c == '-' || c == '+')
							&& c != ' '
							&& c != '%'
							&& c != 0
							&& c != 0x0A
							&& c != 0x0D )
						{
							++send;
							c = *send;
						}

						// getting format string
						const Uint32 len = static_cast<Uint32>( send-s );
						RED_SYSTEM_ASSERT( len < formatViewSize, "Format length is too long! [%u] chars!", len );
						const Uint32 formatLen = (len < formatViewSize) ? len : formatViewSize;
						red::Strcpy( formatView, s, sizeof(formatView), formatLen );
						formatView[ formatLen ] = '\0';

						// printing arg to buffer
						Int32 written = 0;
						if ( !red::ToBuffer( buffer+bufPos, bufferLen-bufPos, std::forward<T>(val), written, formatView ) )
						{
							RED_WARNING( false, "[%hs](%u: cannot use format string: %hs, ...[%u args left] )", file, line, format, sizeof...(Args)+1 );

							// fallback - using default format
							written += red::SNPrintFUnsafe( buffer+bufPos, bufferLen-bufPos, red::GetFormatString( std::forward<T>(val) ), val );
						}
						if ( written >= 0 )
						{
							// advance output buffer
							bufPos += written;

							// go to next argument
							written = SNPrintFInternal( buffer+bufPos, bufferLen-bufPos, send, 0, nullptr, args... );
							if ( written >= 0 )
							{
								bufPos += written;
							}
							return bufPos;
						}

						// next
						s = send;
					}
				}

				// beginning of new style format - nested { are included
				else if ( *s == '{' )
				{
					// looking for end
					const char* send = std::strchr( s+1, '}' );
					if ( send )
					{
						// check nested
						const char* nextArg = std::strchr( s+1, '{' );

						// if nested than move format beginning
						if ( nextArg && nextArg < send )
						{
							const Uint32 nestedTextLen = static_cast<Uint32>( nextArg-s );
							red::Strcpy( buffer+bufPos, s, bufferLen-bufPos, nestedTextLen );
							s = nextArg;
							bufPos += nestedTextLen;
						}
					
						// getting format string
						const Uint32 len = static_cast<Uint32>( send-s+1 );
						RED_SYSTEM_ASSERT( len < formatViewSize, "Format length is too long! [%u] chars!", len );
						const Uint32 formatLen = (len < formatViewSize) ? len : formatViewSize;
						red::Strcpy( formatView, s, sizeof(formatView), formatLen );
						formatView[ formatLen ] = '\0';

						// printing arg to buffer
						Int32 written = 0;
						if ( !red::ToBuffer( buffer+bufPos, bufferLen-bufPos, std::forward<T>(val), written, formatView ) )
						{
							RED_WARNING( false, "[%hs](%u): format: %hs, ...[%u args] )", file, line, format, sizeof...(Args)+1 );

							// fallback - using default format
							written += red::SNPrintFUnsafe( buffer+bufPos, bufferLen-bufPos, red::GetFormatString( std::forward<T>(val) ), val );
						}
						if ( written >= 0 )
						{
							// advance output buffer
							bufPos += written;

							// go to next argument
							written = SNPrintFInternal( buffer+bufPos, bufferLen-bufPos, send+1, 0, nullptr, args... );
							if ( written >= 0 )
							{
								bufPos += written;
							}
							return bufPos;
						}

						// next
						s = s+1;
					}
				}

				// copy next char from format to buffer
				if ( bufPos < bufferLen && *s )
				{
					buffer[ bufPos++ ] = *s++;
				}
			}
			return bufPos;
		}
	}

	// type-safe print to buffer of ascii chars
	template <typename... Args>
	RED_INLINE Int32 SNPrintFSafe( AnsiChar* buffer, Uint32 bufferLen, const AnsiChar* format, Args&& ... args )
	{
		const Int32 written = prv::SNPrintFInternal( buffer, bufferLen, format, 0, nullptr, std::forward<Args>(args)... );
		buffer[ written ] = 0;
		return written;
	}

	// type-safe print to buffer of ascii chars (debug version)
	template <typename... Args>
	#define SNPrintF( buffer, bufferLen, format, ... ) SNPrintFSafeDebug( __LINE__, __FILE__, buffer, bufferLen, format, __VA_ARGS__ )
	RED_INLINE Int32 SNPrintFSafeDebug( const Uint32 line, const char* file, AnsiChar* buffer, Uint32 bufferLen, const AnsiChar* format, Args&& ... args )
	{
		const Int32 written = prv::SNPrintFInternal( buffer, bufferLen, format, line, file, std::forward<Args>(args)... );
		buffer[ written ] = 0;
		return written;
	}
}