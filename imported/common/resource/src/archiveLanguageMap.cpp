/*
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"

#if defined(RED_PLATFORM_ORBIS) || defined(RED_PLATFORM_DURANGO)

#include "archiveLanguageMap.h"

#include "../../redFileSystem/include/fileSys.h"

#include "rapidXml.h"

#if defined( RED_PLATFORM_ORBIS )
#include "archiveHelperOrbis.h"
#elif defined( RED_PLATFORM_DURANGO )
#include "archiveHelperDurango.h"
#endif

namespace res
{

const char* ToLanguageCode( const LanguageID id )
{
	switch ( id )
	{
	case LanguageID::AR: return "ar-ae";
	case LanguageID::CS: return "cs-cz";
	case LanguageID::DA: return "da-dk";
	case LanguageID::DE: return "de-de";
	case LanguageID::EL: return "el-gr";
	case LanguageID::EN: return "en-us";
	case LanguageID::ES: return "es";
	case LanguageID::ES_ES: return "es-es";
	case LanguageID::ES_MX: return "es-mx";
	case LanguageID::FI: return "fi-fi";
	case LanguageID::FR: return "fr-fr";
	case LanguageID::HU: return "hu-hu";
	case LanguageID::ID: return "id-id";
	case LanguageID::IT: return "it-it";
	case LanguageID::JA: return "ja-jp";
	case LanguageID::KO: return "ko-kr";
	case LanguageID::NL: return "nl-nl";
	case LanguageID::NO: return "no-no";
	case LanguageID::PL: return "pl-pl";
	case LanguageID::PT: return "pt";
	case LanguageID::PT_BR: return "pt-br";
	case LanguageID::PT_PT: return "pt-pt";
	case LanguageID::RO: return "ro-ro";
	case LanguageID::RU: return "ru-ru";
	case LanguageID::SV: return "sv-se";
	case LanguageID::TH: return "th-th";
	case LanguageID::TR: return "tr-tr";
	case LanguageID::VI: return "vi-vn";
	case LanguageID::ZH: return "zh";
	case LanguageID::ZH_CN: return "zh-cn";
	case LanguageID::ZH_TW: return "zh-tw";
	}

	RED_FATAL( "Unknown language" );

	return nullptr;
}

LanguageID ToLanguageID( const red::StringView language )
{
	if ( language == "ar" || language == "ar-ae" || language == "ar-ar" )
	{
		return LanguageID::AR;
	}
	else if ( language == "cs" || language == "cs-cz" || language == "cz-cz" )
	{
		return LanguageID::CS;
	}
	else if ( language == "da" || language == "da-dk" )
	{
		return LanguageID::DA;
	}
	else if ( language == "de" || language == "de-de" )
	{
		return LanguageID::DE;
	}
	else if ( language == "el" || language == "el-gr" )
	{
		return LanguageID::EL;
	}
	else if ( language == "en" || language == "en-us" || language == "en-gb" )
	{
		return LanguageID::EN;
	}
	else if ( language == "es-es" )
	{
		return LanguageID::ES_ES;
	}
	else if ( language == "es-mx" )
	{
		return LanguageID::ES_MX;
	}
	else if ( language == "fi" || language == "fi-fi" )
	{
		return LanguageID::FI;
	}
	else if ( language == "fr" || language == "fr-fr" )
	{
		return LanguageID::FR;
	}
	else if ( language == "fr-ca" )
	{
		return LanguageID::FR_CA;
	}
	else if ( language == "hu" || language == "hu-hu" )
	{
		return LanguageID::HU;
	}
	else if ( language == "id" || language == "id-id" )
	{
		return LanguageID::ID;
	}
	else if ( language == "it" || language == "it-it" )
	{
		return LanguageID::IT;
	}
	else if ( language == "ja" || language == "ja-jp" || language == "jp-jp" )
	{
		return LanguageID::JA;
	}
	else if ( language == "ko" || language == "ko-kr" || language == "kr-kr" )
	{
		return LanguageID::KO;
	}
	else if ( language == "nl" || language == "nl-nl" )
	{
		return LanguageID::NL;
	}
	else if ( language == "no" || language == "no-no" )
	{
		return LanguageID::NO;
	}
	else if ( language == "pl" || language == "pl-pl" )
	{
		return LanguageID::PL;
	}
	else if ( language == "pt" )
	{
		return LanguageID::PT;
	}
	else if ( language == "pt-br" )
	{
		return LanguageID::PT_BR;
	}
	else if ( language == "pt-pt" )
	{
		return LanguageID::PT_PT;
	}
	else if ( language == "ro" || language == "ro-ro" )
	{
		return LanguageID::RO;
	}
	else if ( language == "ru" || language == "ru-ru" )
	{
		return LanguageID::RU;
	}
	else if ( language == "sv" || language == "sv-se" )
	{
		return LanguageID::SV;
	}
	else if ( language == "th" || language == "th-th" )
	{
		return LanguageID::TH;
	}
	else if ( language == "tr" || language == "tr-tr" )
	{
		return LanguageID::TR;
	}
	else if ( language == "vi" || language == "vi-vn" )
	{
		return LanguageID::VI;
	}
	else if ( language == "zh-cn" )
	{
		return LanguageID::ZH_CN;
	}
	else if ( language == "zh-tw" )
	{
		return LanguageID::ZH_TW;
	}

	RED_FATAL( "Unknown %s language", language.Data() );

	return LanguageID::Invalid;
}

LanguageID ToVoiceOverLanguageID( const LanguageID id )
{
	switch ( id )
	{
	case LanguageID::ES_ES:
	case LanguageID::ES_MX:
		return LanguageID::ES;

	case LanguageID::PT_PT:
	case LanguageID::PT_BR:
		return LanguageID::PT;

	case LanguageID::ZH_CN:
	case LanguageID::ZH_TW:
		return LanguageID::ZH;
	case LanguageID::FR_CA:
		return LanguageID::EN;
	}

	return id;
}

namespace prv
{

static const char ARCHIVE_LANGUAGE_MAP_FILENAME[] = "ArchiveLanguageMap.xml";
static const char ALM_ARCHIVES[] = "archives";
static const char ALM_LANGUAGE[] = "lang";
static const char ALM_TYPE[] = "type";
static const char ALM_DEFAULT_VO[] = "default_vo";
#if defined( RED_PLATFORM_ORBIS )
static const char ALM_CHUNK[] = "chunk_ps4";
#elif defined( RED_PLATFORM_DURANGO )
static const char ALM_CHUNK[] = "chunk_xb1";
#endif
static const char ALM_ARCHIVE[] = "archive";

ArchiveLanguageMap::ArchiveLanguageMap()
	: m_textEntries{ red::PoolEngine() }
	, m_voiceEntries{ red::PoolEngine() }
	, m_globalDefaultVO{ LanguageID::EN }
	, m_defaultVO{ LanguageID::EN }
	, m_systemLanguageID{ LanguageID::Invalid }
{
}

ArchiveLanguageMap::~ArchiveLanguageMap() = default;

bool ArchiveLanguageMap::Load( const red::AbsolutePath& basePath )
{
	red::String buffer;
	rapidxml::xml_document<> doc;
	rapidxml::xml_node<>* node;

	const auto archiveLanguageMapPath = basePath.AddFilePath( ARCHIVE_LANGUAGE_MAP_FILENAME );
	if ( !red::LoadFileToString( archiveLanguageMapPath, buffer ) )
	{
		RED_LOG_ERROR( "ArchiveLanguageMap: Couldn't load '%hs'", archiveLanguageMapPath.AsChar() );
		return false;
	}

	rapidxml::ResetErrors();
	doc.parse<0>( buffer.AsChar() );
	if ( rapidxml::HasError() )
	{
		RED_LOG_ERROR( "ArchiveLanguageMap: Error in parsing '%hs'", archiveLanguageMapPath.AsChar() );
		return false;
	}

	node = doc.first_node( ALM_ARCHIVES );
	if ( node == nullptr )
	{
		RED_LOG_ERROR( "ArchiveLanguageMap: Error in format of '%hs'", archiveLanguageMapPath.AsChar() );
		return false;
	}

	rapidxml::xml_attribute<>* globalDefaultVOAttrib = node->first_attribute( ALM_DEFAULT_VO );

	if ( globalDefaultVOAttrib )
	{
		const red::StringView globalDefaultVOString{ globalDefaultVOAttrib->value(), globalDefaultVOAttrib->value_size() };
		m_globalDefaultVO = ToVoiceOverLanguageID( ToLanguageID( globalDefaultVOString ) );
	}

	node = node->first_node( ALM_ARCHIVE );
	if ( node == nullptr )
	{
		RED_LOG_ERROR( "ArchiveLanguageMap: Error in format of '%hs'", archiveLanguageMapPath.AsChar() );
		return false;
	}

	while ( node )
	{
		rapidxml::xml_attribute<>* langNameAttrib = node->first_attribute( ALM_LANGUAGE );
		rapidxml::xml_attribute<>* typeAttrib = node->first_attribute( ALM_TYPE );
		rapidxml::xml_attribute<>* chunkAttrib = node->first_attribute( ALM_CHUNK );

		RED_FATAL_ASSERT( langNameAttrib );
		RED_FATAL_ASSERT( typeAttrib );
		RED_FATAL_ASSERT( chunkAttrib );

		const red::StringView langString{ langNameAttrib->value(), langNameAttrib->value_size() };
		const red::StringView typeString{ typeAttrib->value(), typeAttrib->value_size() };
		const red::String chunkString( chunkAttrib->value(), static_cast< Uint32 >( chunkAttrib->value_size() ) );
		LanguageID defaultVO = LanguageID::Invalid;
		LanguageID defaultFullVO = LanguageID::Invalid;

		Type type = Type::Text;

		if ( typeString == "text" )
		{
			rapidxml::xml_attribute<>* defaultVOAttrib = node->first_attribute( ALM_DEFAULT_VO );
			RED_FATAL_ASSERT( defaultVOAttrib );

			const red::StringView defaultVOString{ defaultVOAttrib->value(), defaultVOAttrib->value_size() };

			type = Type::Text;
			defaultFullVO = ToLanguageID( defaultVOString );
			defaultVO = ToVoiceOverLanguageID( defaultFullVO );
		}
		else if ( typeString == "voice" )
		{
			type = Type::Voice;
		}
		else
		{
			RED_FATAL( "Invalid type" );
		}

		Entry &entry = type == Type::Text ? m_textEntries.EmplaceBack() : m_voiceEntries.EmplaceBack();

		entry.file = String( node->value(), static_cast< Uint32 >( node->value_size() ) );
		auto langId = ToLanguageID( langString );
		entry.language = type == Type::Voice ? ToVoiceOverLanguageID( langId ) : langId;
		entry.fullLanguage = langId;
		entry.defaultVO = defaultVO;
		entry.defaultFullVO = defaultFullVO;
		FromString( chunkString, entry.chunkID );
		entry.type = type;
		entry.status = LanguagePackStatus::NotInstalled;

		node = node->next_sibling();
	}

	return true;
}

red::DynArray< red::String > ArchiveLanguageMap::GetFileNamesForLanguages( const red::ArraySpan< red::String >& languages ) const
{
	RED_FATAL_ASSERT( !m_textEntries.Empty() && !m_voiceEntries.Empty(), "Trying to use an ArchiveLanguageMap when it is not loaded" );

	red::DynArray< red::String > result{ red::PoolEngine() };
	result.Reserve( m_textEntries.Size() * 2 );

	red::DynArray< LanguageID > langs{ red::PoolEngine() };
	langs.Reserve( languages.Size() );

	for ( const auto &lang : languages )
	{
		langs.PushBack( ToLanguageID( lang ) );
	}

	red::DynArray< LanguageID > vo{ red::PoolEngine() };
	vo.Reserve( languages.Size() );

	for ( const auto &entry : m_textEntries )
	{
		result.PushBack( entry.file );

		if ( langs.GetIndex( entry.language ) != -1 )
		{
			vo.PushBack( entry.defaultVO );
		}
	}

	for ( const auto &entry : m_voiceEntries )
	{
		if ( vo.GetIndex( entry.language ) != -1 )
		{
			result.PushBack( entry.file );
		}
	}

	red::alg::RemoveDuplicates( result );

	return result;
}

red::DynArray< Uint16 > ArchiveLanguageMap::GetChunksForLanguage( const LanguageID id ) const
{
	red::DynArray< Uint16 > result{ red::PoolEngine() };
	result.Reserve( m_textEntries.Size() + m_voiceEntries.Size() );

	LanguageID voLanguageID = LanguageID::Invalid;

	for ( const auto& entry : m_textEntries )
	{
		if ( entry.language == id )
		{
			result.PushBack( entry.chunkID );
			voLanguageID = entry.defaultVO;
		}
	}

	for ( const auto& entry : m_voiceEntries )
	{
		if ( entry.language == voLanguageID )
		{
			result.PushBack( entry.chunkID );
		}
	}

	// to sort and in case we get duplicates
	red::alg::RemoveDuplicates( result );

	return result;
}

Bool ArchiveLanguageMap::RefreshStatus()
{
	return prv::RefreshInstalledLanguages( *this );
}

red::DynArray< ArchiveLanguageMap::Entry > & ArchiveLanguageMap::GetTextEntries()
{
	return m_textEntries;
}

const red::DynArray< ArchiveLanguageMap::Entry > & ArchiveLanguageMap::GetTextEntries() const
{
	return m_textEntries;
}

red::DynArray< ArchiveLanguageMap::Entry > & ArchiveLanguageMap::GetVoiceEntries()
{
	return m_voiceEntries;
}

const red::DynArray< ArchiveLanguageMap::Entry > & ArchiveLanguageMap::GetVoiceEntries() const
{
	return m_voiceEntries;
}

LanguageID ArchiveLanguageMap::GetGlobalDefaultVoiceOver() const
{
	return m_globalDefaultVO;
}

LanguageID ArchiveLanguageMap::GetDefaultVoiceOver( const LanguageID id ) const
{
	for ( const auto &entry : m_textEntries )
	{
		if ( entry.language == id )
		{
			return entry.defaultVO;
		}
	}

	return LanguageID::Invalid;
}

Bool ArchiveLanguageMap::IsTextLanguageAvailable( const LanguageID id ) const
{
	return red::alg::FindIf( m_textEntries, [&]( const auto &entry )
	{
		return entry.language == id;
	} ) != m_textEntries.End();
}

Bool ArchiveLanguageMap::IsVoiceLanguageAvailable( const LanguageID id ) const
{
	const LanguageID voID = ToVoiceOverLanguageID( id );

	return red::alg::FindIf( m_voiceEntries, [&]( const auto &entry )
	{
		return entry.language == voID;
	} ) != m_voiceEntries.End();
}

Bool ArchiveLanguageMap::IsLanguageAvailable( const LanguageID id )
{
	return IsTextLanguageAvailable( id ) && IsVoiceLanguageAvailable( GetDefaultVoiceOver( id ) );
}

Bool ArchiveLanguageMap::IsTextLanguageInstalled( const LanguageID id ) const
{
	return red::alg::FindIf( m_textEntries, [&]( const auto &entry )
	{
		return entry.language == id && entry.status == LanguagePackStatus::Installed;
	} ) != m_textEntries.End();
}

Bool ArchiveLanguageMap::IsVoiceLanguageInstalled( const LanguageID id ) const
{
	const LanguageID voID = ToVoiceOverLanguageID( id );

	const Bool installed = red::alg::FindIf( m_voiceEntries, [&]( const auto &entry )
	{
		return entry.language == voID && entry.status == LanguagePackStatus::Installed;
	} ) != m_voiceEntries.End();

	return installed;
}

Bool ArchiveLanguageMap::IsLanguageInstalled( const LanguageID id )
{
	return IsTextLanguageInstalled( id ) && IsVoiceLanguageInstalled( GetDefaultVoiceOver( id ) );
}

Uint16 ArchiveLanguageMap::GetChunkForVoicePack( LanguageID id )
{
	const LanguageID voID = ToVoiceOverLanguageID( id );
	auto it = red::alg::FindIf( m_voiceEntries, [ & ]( const auto &entry )
	{
		return entry.language == voID;
	} );

	if ( it != m_voiceEntries.End() )
	{
		return it->chunkID;
	}

	return 0;
}

LanguagePackStatus ArchiveLanguageMap::GetVoicePackStatus( const LanguageID id )
{
	const LanguageID voID = ToVoiceOverLanguageID( id );
	auto it = red::alg::FindIf( m_voiceEntries, [ & ]( const auto &entry )
	{
		return entry.language == voID;
	} );

	if ( it != m_voiceEntries.End() )
	{
		return it->status;
	}

	return LanguagePackStatus::Invalid;
}

red::DynArray< red::String > ArchiveLanguageMap::ChangeChunksStatus( const red::DynArray< Uint16 > &chunks, const LanguagePackStatus status )
{
	red::DynArray< red::String > result{ red::PoolEngine() };

	for ( auto &entry : m_textEntries )
	{
		for ( auto const &chunkID : chunks )
		{
			if ( entry.chunkID == chunkID )
			{
				entry.status = status;
				if ( status == LanguagePackStatus::Installed )
				{
					result.PushBack( entry.file );
				}
			}
		}
	}

	for ( auto &entry : m_voiceEntries )
	{
		for ( auto const &chunkID : chunks )
		{
			if ( entry.chunkID == chunkID )
			{
				entry.status = status;
				if ( status == LanguagePackStatus::Installed )
				{
					result.PushBack( entry.file );
				}
			}
		}
	}

	red::alg::RemoveDuplicates( result );

	return result;
}

Bool ArchiveLanguageMap::IsLanguageChunkID( const Uint16 chunkID ) const
{
	for ( auto &entry : m_textEntries )
	{
		if ( entry.chunkID == chunkID )
		{
			return true;
		}
	}

	for ( auto &entry : m_voiceEntries )
	{
		if ( entry.chunkID == chunkID )
		{
			return true;
		}
	}

	return false;
}

Bool ArchiveLanguageMap::IsLoaded() const
{
	return !m_textEntries.Empty() && !m_voiceEntries.Empty();
}

void ArchiveLanguageMap::SetSystemLanguageID( const LanguageID id )
{
	m_systemLanguageID = id;
}

LanguageID ArchiveLanguageMap::GetSystemLanguageID() const
{
	return m_systemLanguageID;
}

void ArchiveLanguageMap::SetDefaultVO( const LanguageID id )
{
	m_defaultVO = id;
}

LanguageID ArchiveLanguageMap::GetDefaultVO() const
{
	return m_defaultVO;
}

} // prv
} // res

#else

RED_NO_EMPTY_FILE();

#endif
