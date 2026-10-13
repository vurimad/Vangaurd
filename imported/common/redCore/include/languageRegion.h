/**
* Copyright C 2019 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "names.h"

namespace red
{
// Returns the current locale of the system as a CName
REDCORE_API CName GetDefaultLocale();

// Returns the current locale of the system as a String
// Note that this is not a sanitised string and it is almost exactly what the system provides
REDCORE_API String GetDefaultLanguageCountryCode();

// Returns the country code parsed from the current locale
REDCORE_API String GetDefaultCountryCode();

// Returns the language code parsed from the current locale
REDCORE_API String GetDefaultLanguageCode();

// Returns the language code parsed from the given locale
REDCORE_API String GetLanguageFromLocale( const red::StringView& locale );

// Returns the country code parsed from the given locale
REDCORE_API String GetCountryFromLocale( const red::StringView& locale );

// Sanitises the locale into a form that we want to work with
REDCORE_API String SanitiseLocale( const red::StringView& locale );

// Parses the locale and converts it to the smallest unique representation of language and country code that we support
// For example 'en-us' & 'en-gb' convert to just 'en' but 'es-es' & 'es-mx' stay unique
REDCORE_API String ConvertToSmallestSupportedLanguageCode( const red::StringView& locale );

#ifdef RED_PLATFORM_ORBIS
// Returns a locale string for the given language id on Orbis as per the documentation
REDCORE_API const char* GetLanguageCodeNameFromSystemId( Uint32 id );
REDCORE_API Uint32 GetLanguageCodeNameToSystemId( const CName &languageCodeName );
#endif
}