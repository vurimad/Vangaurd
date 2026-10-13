/*
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#if defined(RED_PLATFORM_ORBIS) || defined(RED_PLATFORM_DURANGO)

#include "../../redCore/include/absolutePath.h"

namespace res
{

enum class LanguageID
{
	Invalid = -1,
	AR,			// Arabic
	CS,			// Chech
	DA,			// Danish
	DE,			// German
	EL,			// Greek
	EN,			// English
	ES,			// Spanish
	ES_ES,		// Spanish
	ES_MX,		// Spanish LA
	FI,			// Finnish
	FR,			// French
	FR_CA,		// French Canadian
	HU,			// Hungarian
	ID,			// Indonesian
	IT,			// Italian
	JA,			// Japanese
	KO,			// Korean
	NL,			// Dutch
	NO,			// Norwegian
	PL,			// Polish
	PT,			// Portuguese
	PT_BR,			// Portuguese
	PT_PT,			// Portuguese
	RO,			// Romanian
	RU,			// Russian
	SV,			// Swedish
	TH,			// Thai
	TR,			// Turkish
	VI,			// Vietnamese
	ZH,			// Chinese
	ZH_CN,		// Chinese S
	ZH_TW,		// Chinese T
};

enum class LanguagePackStatus : Int8
{
	Invalid = -1,
	NotInstalled,
	Installing,
	Installed
};


const char* ToLanguageCode( LanguageID id );
LanguageID ToLanguageID( const red::StringView language );
LanguageID ToVoiceOverLanguageID( LanguageID id );

namespace prv
{

class ArchiveLanguageMap
{
public:
	ArchiveLanguageMap();
	~ArchiveLanguageMap();

	ArchiveLanguageMap( const ArchiveLanguageMap& ) = delete;
	ArchiveLanguageMap( ArchiveLanguageMap&& ) = delete;
	ArchiveLanguageMap& operator=( const ArchiveLanguageMap& ) = delete;
	ArchiveLanguageMap& operator=( ArchiveLanguageMap&& ) = delete;

	enum class Type : Bool
	{
		Text,
		Voice
	};

	struct Entry
	{
		String file;
		LanguageID language;
		LanguageID fullLanguage;
		LanguageID defaultVO;
		LanguageID defaultFullVO;
		Uint16 chunkID;
		Type type;
		LanguagePackStatus status;
	};

	bool Load( const red::AbsolutePath& basePath );

	red::DynArray< red::String > GetFileNamesForLanguages( const red::ArraySpan< red::String >& languages ) const;
	red::DynArray< Uint16 > GetChunksForLanguage( LanguageID id ) const;
	Bool RefreshStatus();

	red::DynArray< Entry > & GetTextEntries();
	const red::DynArray< Entry > & GetTextEntries() const;

	red::DynArray< Entry > & GetVoiceEntries();
	const red::DynArray< Entry > & GetVoiceEntries() const;

	LanguageID GetGlobalDefaultVoiceOver() const;
	LanguageID GetDefaultVoiceOver( LanguageID id ) const;

	Bool IsTextLanguageAvailable( LanguageID id ) const;
	Bool IsVoiceLanguageAvailable( LanguageID id ) const;
	Bool IsLanguageAvailable( LanguageID id );

	Bool IsTextLanguageInstalled( LanguageID id ) const;
	Bool IsVoiceLanguageInstalled( LanguageID id ) const;
	Bool IsLanguageInstalled( LanguageID id );

	Uint16 GetChunkForVoicePack( LanguageID id );
	LanguagePackStatus GetVoicePackStatus( LanguageID id );
	red::DynArray< red::String > ChangeChunksStatus( const red::DynArray< Uint16 > &chunks, LanguagePackStatus status );

	Bool IsLanguageChunkID( Uint16 chunkID ) const;

	Bool IsLoaded() const;

	void SetSystemLanguageID( LanguageID id );
	LanguageID GetSystemLanguageID() const;

	void SetDefaultVO( LanguageID id );
	LanguageID GetDefaultVO() const;

private:
	red::DynArray< Entry > m_textEntries;
	red::DynArray< Entry > m_voiceEntries;
	LanguageID m_globalDefaultVO;
	LanguageID m_defaultVO;
	LanguageID m_systemLanguageID;
};

} // prv
} // res

#endif
