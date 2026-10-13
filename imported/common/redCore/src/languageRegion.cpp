#include "build.h"
#include "languageRegion.h"

#include "../../redContainers/include/string/stringUtils.h"

#if defined( RED_PLATFORM_ORBIS )
#include <system_param.h>
#include <system_service.h>
#endif

namespace
{
	const char c_fallbackLanguageCodeName[] = "en-us";
}

namespace red
{
#if defined( RED_PLATFORM_WINPC )

static String GetUserLanguageCodeName()
{
	wchar_t localeName[LOCALE_NAME_MAX_LENGTH];
	if ( !GetUserDefaultLocaleName( localeName, LOCALE_NAME_MAX_LENGTH ) )
	{
		RED_LOG_ERROR( "Failed to retrieve default local name due to '%lu'", GetLastError() );
		return "en-us";
	}

	char codeName[LOCALE_NAME_MAX_LENGTH];
	red::StrToLower( localeName, static_cast< Uint32 >( red::Strlen( localeName ) ) );
	red::WideCharToStdChar( codeName, localeName, LOCALE_NAME_MAX_LENGTH );
	String result{ codeName };

	return result;
}

#elif defined( RED_PLATFORM_DURANGO )

namespace prv
{

String GetUserLocale()
{
	wchar_t localeName[LOCALE_NAME_MAX_LENGTH];
	if ( GetUserDefaultLocaleName( localeName, LOCALE_NAME_MAX_LENGTH ) == 0 )
	{
		RED_LOG_ERROR( "Failed to retrieve default local name due to '%lu'", GetLastError() );
		return "en-us";
	}

	char codeName[LOCALE_NAME_MAX_LENGTH];
	red::StrToLower( localeName, static_cast< Uint32 >( red::Strlen( localeName ) ) );
	red::WideCharToStdChar( codeName, localeName, LOCALE_NAME_MAX_LENGTH );
	String result{ codeName };

	RED_LOG( "RegionDebug: Detected user locale as '%hs'", result.AsChar() );

	// Quick hacky fix in case we get these strange locales as hu-HU and cs-CZ is not directly supported yet
	// TODO: Remove this after using the August 2020 where they added hu-HU and cs-CZ to Xbox
	if ( result == "en-cz" )
	{
		result = "cs-cz";
	}
	else if ( result == "en-hu" )
	{
		result = "hu-hu";
	}

	return result;
}

String GetSystemCountry()
{
	GEOID geoId = ::GetUserGeoID( GEOCLASS_ALL );
	if ( geoId == GEOID_NOT_AVAILABLE )
	{
		RED_LOG_ERROR( "Failed to get the user's GeoID, error: '%lu'", GetLastError() );
		return "";
	}

	wchar_t wideBuffer[LOCALE_NAME_MAX_LENGTH]; // Not the right constant for this but it creates a big enough buffer
	if ( ::GetGeoInfoW( geoId, GEO_ISO2, wideBuffer, LOCALE_NAME_MAX_LENGTH, 0 ) == 0 )
	{
		RED_LOG_ERROR( "Failed to extract the country code from the user's GeoID, error: '%lu'", GetLastError() );
		return "";
	}

	char narrowBuffer[LOCALE_NAME_MAX_LENGTH];
	red::StrToLower( wideBuffer, static_cast< Uint32 >( red::Strlen( wideBuffer ) ) );
	red::WideCharToStdChar( narrowBuffer, wideBuffer, LOCALE_NAME_MAX_LENGTH );

	RED_LOG( "RegionDebug: Detected system country as '%hs'", narrowBuffer );

	return { narrowBuffer };
}

} // prv

static String GetUserLanguageCodeName()
{
	String userLocale = prv::GetUserLocale();
	String systemCountry = prv::GetSystemCountry();
	if ( userLocale.EndsWith( systemCountry ) )
	{
		return userLocale;
	}

	if ( systemCountry == "hu" )
	{
		return "hu-hu";
	}
	if ( systemCountry == "cz" )
	{
		return "cs-cz";
	}

	String result;
	result.Append( userLocale.AsChar(), 3 ); // Copy over "ll-" from "ll-cc"
	result += systemCountry;
	return result;
}

#elif defined( RED_PLATFORM_LINUX )

static String GetUserLanguageCodeName()
{
	return c_fallbackLanguageCodeName;
}

#elif defined( RED_PLATFORM_ORBIS )

const char* GetLanguageCodeNameFromSystemId( Uint32 id )
{
	switch ( id )
	{
	case SCE_SYSTEM_PARAM_LANG_JAPANESE:
		return "ja-jp";
	case SCE_SYSTEM_PARAM_LANG_ENGLISH_US:
		return "en-us";
	case SCE_SYSTEM_PARAM_LANG_FRENCH:
		return "fr-fr";
	case SCE_SYSTEM_PARAM_LANG_SPANISH:
		return "es-es";
	case SCE_SYSTEM_PARAM_LANG_GERMAN:
		return "de-de";
	case SCE_SYSTEM_PARAM_LANG_ITALIAN:
		return "it-it";
	case SCE_SYSTEM_PARAM_LANG_DUTCH:
		return "nl-nl";
	case SCE_SYSTEM_PARAM_LANG_PORTUGUESE_PT:
		return "pt-pt";
	case SCE_SYSTEM_PARAM_LANG_RUSSIAN:
		return "ru-ru";
	case SCE_SYSTEM_PARAM_LANG_KOREAN:
		return "ko-kr";
	case SCE_SYSTEM_PARAM_LANG_CHINESE_T:
		return "zh-tw";
	case SCE_SYSTEM_PARAM_LANG_CHINESE_S:
		return "zh-cn";
	case SCE_SYSTEM_PARAM_LANG_FINNISH:
		return "fi-fi";
	case SCE_SYSTEM_PARAM_LANG_SWEDISH:
		return "sv-se";
	case SCE_SYSTEM_PARAM_LANG_DANISH:
		return "da-dk";
	case SCE_SYSTEM_PARAM_LANG_NORWEGIAN:
		return "no-no";
	case SCE_SYSTEM_PARAM_LANG_POLISH:
		return "pl-pl";
	case SCE_SYSTEM_PARAM_LANG_PORTUGUESE_BR:
		return "pt-br";
	case SCE_SYSTEM_PARAM_LANG_ENGLISH_GB:
		return "en-gb";
	case SCE_SYSTEM_PARAM_LANG_TURKISH:
		return "tr-tr";
	case SCE_SYSTEM_PARAM_LANG_SPANISH_LA:
		return "es-mx";
	case SCE_SYSTEM_PARAM_LANG_ARABIC:
		return "ar-ae";
	case SCE_SYSTEM_PARAM_LANG_FRENCH_CA:
		return "fr-ca";
	case SCE_SYSTEM_PARAM_LANG_CZECH:
		return "cs-cz";
	case SCE_SYSTEM_PARAM_LANG_HUNGARIAN:
		return "hu-hu";
	case SCE_SYSTEM_PARAM_LANG_GREEK:
		return "el-gr";
	case SCE_SYSTEM_PARAM_LANG_ROMANIAN:
		return "ro-ro";
	case SCE_SYSTEM_PARAM_LANG_THAI:
		return "th-th";
	case SCE_SYSTEM_PARAM_LANG_VIETNAMESE:
		return "vi-vn";
	case SCE_SYSTEM_PARAM_LANG_INDONESIAN:
		return "id-id";

	default:
		return "";
	}
}

REDCORE_API Uint32 GetLanguageCodeNameToSystemId( const CName &languageCodeName )
{
	if ( languageCodeName == RED_NAME_CONSTEXPR_NOREG( "ja-jp" ) )
	{
		return SCE_SYSTEM_PARAM_LANG_JAPANESE;
	}
	else if ( languageCodeName == RED_NAME_CONSTEXPR_NOREG( "en-us" ) )
	{
		return SCE_SYSTEM_PARAM_LANG_ENGLISH_US;
	}
	else if ( languageCodeName == RED_NAME_CONSTEXPR_NOREG( "fr-fr" ) )
	{
		return SCE_SYSTEM_PARAM_LANG_FRENCH;
	}
	else if ( languageCodeName == RED_NAME_CONSTEXPR_NOREG( "es-es" ) )
	{
		return SCE_SYSTEM_PARAM_LANG_SPANISH;
	}
	else if ( languageCodeName == RED_NAME_CONSTEXPR_NOREG( "de-de" ) )
	{
		return SCE_SYSTEM_PARAM_LANG_GERMAN;
	}
	else if ( languageCodeName == RED_NAME_CONSTEXPR_NOREG( "it-it" ) )
	{
		return SCE_SYSTEM_PARAM_LANG_ITALIAN;
	}
	else if ( languageCodeName == RED_NAME_CONSTEXPR_NOREG( "nl-nl" ) )
	{
		return SCE_SYSTEM_PARAM_LANG_DUTCH;
	}
	else if ( languageCodeName == RED_NAME_CONSTEXPR_NOREG( "pt-pt" ) )
	{
		return SCE_SYSTEM_PARAM_LANG_PORTUGUESE_PT;
	}
	else if ( languageCodeName == RED_NAME_CONSTEXPR_NOREG( "ru-ru" ) )
	{
		return SCE_SYSTEM_PARAM_LANG_RUSSIAN;
	}
	else if ( languageCodeName == RED_NAME_CONSTEXPR_NOREG( "ko-kr" ) )
	{
		return SCE_SYSTEM_PARAM_LANG_KOREAN;
	}
	else if ( languageCodeName == RED_NAME_CONSTEXPR_NOREG( "zh-tw" ) )
	{
		return SCE_SYSTEM_PARAM_LANG_CHINESE_T;
	}
	else if ( languageCodeName == RED_NAME_CONSTEXPR_NOREG( "zh-cn" ) )
	{
		return SCE_SYSTEM_PARAM_LANG_CHINESE_S;
	}
	else if ( languageCodeName == RED_NAME_CONSTEXPR_NOREG( "fi-fi" ) )
	{
		return SCE_SYSTEM_PARAM_LANG_FINNISH;
	}
	else if ( languageCodeName == RED_NAME_CONSTEXPR_NOREG( "sv-se" ) )
	{
		return SCE_SYSTEM_PARAM_LANG_SWEDISH;
	}
	else if ( languageCodeName == RED_NAME_CONSTEXPR_NOREG( "da-dk" ) )
	{
		return SCE_SYSTEM_PARAM_LANG_DANISH;
	}
	else if ( languageCodeName == RED_NAME_CONSTEXPR_NOREG( "no-no" ) )
	{
		return SCE_SYSTEM_PARAM_LANG_NORWEGIAN;
	}
	else if ( languageCodeName == RED_NAME_CONSTEXPR_NOREG( "pl-pl" ) )
	{
		return SCE_SYSTEM_PARAM_LANG_POLISH;
	}
	else if ( languageCodeName == RED_NAME_CONSTEXPR_NOREG( "pt-br" ) )
	{
		return SCE_SYSTEM_PARAM_LANG_PORTUGUESE_BR;
	}
	else if ( languageCodeName == RED_NAME_CONSTEXPR_NOREG( "en-gb" ) )
	{
		return SCE_SYSTEM_PARAM_LANG_ENGLISH_GB;
	}
	else if ( languageCodeName == RED_NAME_CONSTEXPR_NOREG( "tr-tr" ) )
	{
		return SCE_SYSTEM_PARAM_LANG_TURKISH;
	}
	else if ( languageCodeName == RED_NAME_CONSTEXPR_NOREG( "es-mx" ) )
	{
		return SCE_SYSTEM_PARAM_LANG_SPANISH_LA;
	}
	else if ( languageCodeName == RED_NAME_CONSTEXPR_NOREG( "ar-ae" ) )
	{
		return SCE_SYSTEM_PARAM_LANG_ARABIC;
	}
	else if ( languageCodeName == RED_NAME_CONSTEXPR_NOREG( "fr-ca" ) )
	{
		return SCE_SYSTEM_PARAM_LANG_FRENCH_CA;
	}
	else if ( languageCodeName == RED_NAME_CONSTEXPR_NOREG( "cs-cz" ) )
	{
		return SCE_SYSTEM_PARAM_LANG_CZECH;
	}
	else if ( languageCodeName == RED_NAME_CONSTEXPR_NOREG( "hu-hu" ) )
	{
		return SCE_SYSTEM_PARAM_LANG_HUNGARIAN;
	}
	else if ( languageCodeName == RED_NAME_CONSTEXPR_NOREG( "el-gr" ) )
	{
		return SCE_SYSTEM_PARAM_LANG_GREEK;
	}
	else if ( languageCodeName == RED_NAME_CONSTEXPR_NOREG( "ro-ro" ) )
	{
		return SCE_SYSTEM_PARAM_LANG_ROMANIAN;
	}
	else if ( languageCodeName == RED_NAME_CONSTEXPR_NOREG( "th-th" ) )
	{
		return SCE_SYSTEM_PARAM_LANG_THAI;
	}
	else if ( languageCodeName == RED_NAME_CONSTEXPR_NOREG( "vi-vn" ) )
	{
		return SCE_SYSTEM_PARAM_LANG_VIETNAMESE;
	}
	else if ( languageCodeName == RED_NAME_CONSTEXPR_NOREG( "id-id" ) )
	{
		return SCE_SYSTEM_PARAM_LANG_INDONESIAN;
	}

	return std::numeric_limits<Uint32>::max();
}

static String GetUserLanguageCodeName()
{
	SceSystemParamLang systemLanguage;
	const Int32 error = ::sceSystemServiceParamGetInt( SCE_SYSTEM_SERVICE_PARAM_ID_LANG, &systemLanguage );
	if ( error != SCE_OK )
	{
		RED_LOG_ERROR( "Failed to retrieve system language due to '0x%08X'", error );
		return c_fallbackLanguageCodeName;
	}

	red::String languageCodeName = GetLanguageCodeNameFromSystemId( systemLanguage );
	if ( languageCodeName.Empty() )
	{
		RED_LOG_ERROR( "Not expected system language '%lu'", systemLanguage );
		return c_fallbackLanguageCodeName;
	}

	return languageCodeName;
}

#endif

static std::pair<red::StringView, red::StringView> DeconstructLocaleString( const red::StringView& locale )
{
	// Parsing based on RFC-4646
	auto tags = red::StrSplit( locale, '-' );
	if ( tags.Size() < 2 )
	{
		RED_LOG_ERROR( "Error parsing '%hs' it does not appear to be a valid locale string", locale.ToString().AsChar() );
		return {};
	}

	red::StringView language = tags[0];
	red::StringView country = tags[1];
	if ( tags.Size() > 2 )
	{
		// The only thing that could be between the language tag and the country tag is the script tag
		// it is formatted as 4 alphabetic characters
		if ( country.Length() == 4 )
		{
			country = tags[2];
		}
		// The remaining tags we don't care about so we can ignore them
	}

	return { language, country };
}

String SanitiseLocale( const red::StringView& locale )
{
	auto deconstructed = DeconstructLocaleString( locale );

	auto& language = deconstructed.first;
	auto& country = deconstructed.second;
#if defined( RED_PLATFORM_DURANGO )
	if ( language == "en" )
	{
		if ( country == "cz" || country == "CZ" )
		{
			language = "cs";
		}
		else if ( country == "hu" || country == "HU" )
		{
			language = "hu";
		}
	}
#endif

	String result = red::StrCat( language, "-", country );
	result.ToLower();
	return result;
}

String GetLanguageFromLocale( const red::StringView& locale )
{
	return DeconstructLocaleString( locale ).first.ToString();
}

String GetCountryFromLocale( const red::StringView& locale )
{
	return DeconstructLocaleString( locale ).second.ToString();
}

CName GetDefaultLocale()
{
	return RED_NAME( GetUserLanguageCodeName() );
}

String GetDefaultCountryCode()
{
	return GetCountryFromLocale( GetUserLanguageCodeName() );
}

String GetDefaultLanguageCode()
{
	return GetLanguageFromLocale( GetUserLanguageCodeName() );
}

String GetDefaultLanguageCountryCode()
{
	return GetUserLanguageCodeName();
}

String ConvertToSmallestSupportedLanguageCode( const red::StringView& locale )
{
	auto deconstructed = DeconstructLocaleString( locale );
	const auto& language = deconstructed.first;
	if ( language == "es" )
	{
		const auto& country = deconstructed.second;
		if ( country == "es" )
		{
			return "es-es";
		}
		else
		{
			return "es-mx";
		}
	}
	else if ( language == "zh" )
	{
		const auto& country = deconstructed.second;
		// We will are not going to get 'zh-HANS' and 'zh-HANT' here since the script will be taken out in the deconstruction above
		if ( country == "hk" || country == "mo" || country == "tw" )
		{
			return "zh-tw";
		}
		if ( country == "cn" || country == "sg" )
		{
			return "zh-cn";
		}
		return "zh-cn";
	}
#if defined( RED_PLATFORM_DURANGO )
	else if ( language == "en" )
	{
		const auto& country = deconstructed.second;
		if ( country == "hu" )
		{
			return "hu";
		}
		if ( country == "cz" )
		{
			return "cs";
		}
	}
#endif

	return language.ToString();
}

} // red
