/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/

#pragma once
#include "../../../common/redContainers/include/map.h"
#include "../../../common/redContainers/include/string/stringLocale.h"

const Uint32 INVALID_XVC_CHUNK_ID = 0xFFFFFFFF;
const Uint32 BIN_XVC_CHUNK_ID = 1;
const Uint32 MAX_XVC_CHUNK_ID = 511;
const Uint32 MAX_XVC_CHUNKS = 512;

struct SXvcLocale
{
	AnsiChar m_buf[ 8 ];
	
	SXvcLocale( const AnsiChar* name );
	SXvcLocale();

	Bool operator==( const SXvcLocale& rhs ) const { return red::Strcmp( m_buf, rhs.m_buf ) == 0; }
	Bool operator!=( const SXvcLocale& rhs ) const { return !(*this == rhs); }
	Bool operator<( const SXvcLocale& rhs ) const { return red::Strcmp( m_buf, rhs.m_buf ) < 0; }
	const AnsiChar* AsChar() const { return m_buf; }
	void RemoveRegionCode();
};

struct SXvcLanguageChunk
{
	SXvcLocale		m_locale;		
	Uint32			m_chunkID;

	SXvcLanguageChunk()
		: m_chunkID( INVALID_XVC_CHUNK_ID )
	{}

	SXvcLanguageChunk( const SXvcLocale& locale, Uint32 chunkID )
		: m_locale( locale )
		, m_chunkID( chunkID )
	{}

	Bool operator==( const SXvcLanguageChunk& rhs ) const { return m_chunkID == rhs.m_chunkID; }
	Bool operator!=( const SXvcLanguageChunk& rhs ) const { return !(*this == rhs); }
};

struct SXvcContent
{
	CName								m_contentName;				//!< E.g., 'content0'
	Uint32								m_chunkID;					//!< Base chunk ID. Non-language masked content
	red::DynArray< SXvcLanguageChunk >	m_languageChunks{ red::PoolEngine() };			//!< Split language chunks

	SXvcContent()
		: m_chunkID( INVALID_XVC_CHUNK_ID )
	{}

	SXvcContent( CName contentName, Uint32 chunkID, const red::DynArray< SXvcLanguageChunk >& languageChunks )
		: m_contentName( contentName )
		, m_chunkID( chunkID )
		, m_languageChunks( languageChunks )
	{}

	Bool operator==( const SXvcContent& rhs ) const { return m_contentName == rhs.m_contentName; }
	Bool operator!=( const SXvcContent& rhs ) const { return !(*this == rhs); }
	Bool operator<( const SXvcContent& rhs ) const { return m_contentName < rhs.m_contentName; }
};
//////////////////////////////////////////////////////////////////////////
// SXvcPackageHeader
//////////////////////////////////////////////////////////////////////////
struct SXvcPackageHeader
{
	static const Uint32 MAGIC = 'XVCP';
	static const Uint32 VERSION = 1;

	Uint32 m_magic;
	Uint32 m_version;

	SXvcPackageHeader()
		: m_magic( MAGIC )
		, m_version( VERSION )
	{}
};

//////////////////////////////////////////////////////////////////////////
// SXvcPackage
//////////////////////////////////////////////////////////////////////////
struct SXvcPackage
{
	SXvcPackageHeader m_header;
	red::Map< String, SXvcLocale > m_languageLocaleMap{ red::PoolEngine() };		//!< Map of game language names to locales. E.g., "en" -> "en-US"
	red::Map< Uint32 , CName > m_chunkNameMap{ red::PoolEngine() };				//!< Map of XVC chunk to chunk name, including lanuage. E.g., 99 -> 'content0_en'
	red::DynArray< SXvcContent > m_installOrderContent{ red::PoolEngine() };		//!< List of non-language masked content in install order
	Uint32 m_binChunkID;				//!< chunk ID of non-content chunk
	red::DynArray< String >	m_supportedSpeechLanguages{ red::PoolEngine() };	//!< E.g., "en", "pl" for *.w3speech
	red::DynArray< String >	m_supportedTextLanguages{ red::PoolEngine() };	//!< E.g., "en", "pl" for *.w3strings
	String m_defaultSpeechLanguage;	//!< E.g., "en" for en*.w3speech
	CName m_launchContentName;		//!< Aliased by launch0
	Uint32 m_patchLevel;				//!< Zero for base game, incremented in each patch. Possible for use in save game prerequesite.

	static Bool Verify( const SXvcPackage& xvcPackage );

	SXvcPackage()
		: m_binChunkID(INVALID_XVC_CHUNK_ID)
		, m_patchLevel(0)
	{}
};

//////////////////////////////////////////////////////////////////////////
// Serialization
//////////////////////////////////////////////////////////////////////////
void operator<<( IFile& ar, SXvcLocale& localeName );
void operator<<( IFile& ar, SXvcLanguageChunk& chunk );
void operator<<( IFile& ar, SXvcContent& content );
void operator<<( IFile& ar, SXvcPackageHeader& header );
void operator<<( IFile& ar, SXvcPackage& package );

#ifdef RED_LOGGING_ENABLED
void DumpXvcPackage( const SXvcPackage& package );
#endif
