/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "xvcPackage.h"

//////////////////////////////////////////////////////////////////////////
// SXvcLocale
//////////////////////////////////////////////////////////////////////////
SXvcLocale::SXvcLocale()
{
	m_buf[0] = '\0';
}

SXvcLocale::SXvcLocale( const AnsiChar* name )
{
	red::Strcpy( m_buf, name, RED_ARRAY_COUNT(m_buf) );
	for ( AnsiChar* pch = m_buf; *pch; ++pch )
	{
		if ( *pch >= 'A' && *pch <= 'Z' )
		{
			*pch += static_cast< AnsiChar >('a' - 'A');
		}
	}
}

void SXvcLocale::RemoveRegionCode()
{
	AnsiChar* pch = red::Strchr( m_buf, '-' );
	if ( pch )
	{
		*pch = '\0';
	}
}

//////////////////////////////////////////////////////////////////////////
// Serialization
//////////////////////////////////////////////////////////////////////////
void operator<<( IFile& file, SXvcLocale& localeName )
{
	file.Serialize( localeName.m_buf, RED_ARRAY_COUNT(localeName.m_buf) );
}

void operator<<( IFile& file, SXvcPackage& package )
{
	file << package.m_header;
	file << package.m_languageLocaleMap;
	file << package.m_chunkNameMap;
	file << package.m_installOrderContent;
	file << package.m_binChunkID;
	file << package.m_supportedSpeechLanguages;
	file << package.m_supportedTextLanguages;
	file << package.m_defaultSpeechLanguage;
	file << package.m_launchContentName;
	file << package.m_patchLevel;
}

void operator<<( IFile& file, SXvcPackageHeader& header )
{
	file << header.m_magic;
	file << header.m_version;
}

void operator<<( IFile& file, SXvcContent& content )
{
	file << content.m_contentName;
	file << content.m_chunkID;
	file << content.m_languageChunks;
}

void operator<<( IFile& file, SXvcLanguageChunk& chunk )
{
	file << chunk.m_locale;
	file << chunk.m_chunkID;
}

Bool SXvcPackage::Verify( const SXvcPackage& xvcPackage )
{
	const SXvcPackageHeader& packageHeader = xvcPackage.m_header;
	if ( packageHeader.m_magic != SXvcPackageHeader::MAGIC )
	{
		RED_LOG_ERROR( "Core: XVC package magic %u does not match expected %u", packageHeader.m_magic, SXvcPackageHeader::MAGIC );
		return false;
	}

	if ( packageHeader.m_version != SXvcPackageHeader::VERSION )
	{
		RED_LOG_ERROR( "Core: XVC package version %u does not match expected %u", packageHeader.m_version, SXvcPackageHeader::VERSION );
		return false;
	}

	if ( xvcPackage.m_binChunkID == INVALID_XVC_CHUNK_ID )
	{
		RED_LOG_ERROR( "Core: XVC binChunkID %u is invalid", xvcPackage.m_binChunkID );
		return false;
	}

	return true;
}

#ifdef RED_LOGGING_ENABLED
void DumpXvcPackage( const SXvcPackage& package )
{
	RED_LOG( "Core: ---------------------");
	RED_LOG( "Core: ]XVC package dump[");
	RED_LOG( "Core: ---------------------");

	RED_LOG( "Core: package.m_header.m_magic: 0x%08X (expect 0x%08X: %hs)", package.m_header.m_magic, package.m_header.MAGIC, 
		(package.m_header.m_magic == package.m_header.MAGIC) ? "match" : "MISMATCH!");

	RED_LOG( "Core: package.m_header.m_version: %u (expect %u: %hs)", package.m_header.m_version, package.m_header.VERSION, 
		(package.m_header.m_version == package.m_header.VERSION) ? "match" : "MISMATCH!" );

	RED_LOG( "Core: package.m_languageLocaleMap:" );
	for ( auto it = package.m_languageLocaleMap.Begin(); it != package.m_languageLocaleMap.End(); ++it )
	{
		const String& gameLang = it.Key();
		const SXvcLocale& locale = it.Value();
		RED_LOG( "Core: \tgameLang {%hs} -> locale {%hs}", gameLang.AsChar(), locale.AsChar() );
	}

	RED_LOG( "Core: launch0: {%hs}", package.m_launchContentName.AsChar() );
	RED_LOG( "Core: package.m_installOrderContent (%u entries):", package.m_installOrderContent.Size() );
	for ( const SXvcContent& content : package.m_installOrderContent )
	{
		RED_LOG( "Core: \tcontent {%hs}, chunkID {%u}, numLangChunks {%u}", content.m_contentName.AsChar(), content.m_chunkID, content.m_languageChunks.Size() );
		for ( const SXvcLanguageChunk& langChunk : content.m_languageChunks )
		{
			RED_LOG( "Core: \t\tlanguage chunkID {%u}, locale {%hs}", langChunk.m_chunkID, langChunk.m_locale.AsChar() );
		}
	}

	RED_LOG( "Core: package.m_chunkNameMap (%u entries):", package.m_chunkNameMap.Size() );
	for ( auto it = package.m_chunkNameMap.Begin(); it != package.m_chunkNameMap.End(); ++it )
	{
		const Uint32 chunkID = it.Key();
		CName chunkName = it.Value();
		RED_LOG( "Core: \tchunkID {%u} -> chunkName {%hs}", chunkID, chunkName.AsChar() );
	}

	RED_LOG( "Core: package.m_supportedSpeechLanguages (%u entries):", package.m_supportedSpeechLanguages.Size());
	for ( const String& lang : package.m_supportedSpeechLanguages )
	{
		RED_LOG( "Core: \tlang {%hs}", lang.AsChar());
	}


	RED_LOG( "Core: package.m_defaultSpeechLanguage: {%hs}", package.m_defaultSpeechLanguage.AsChar() );
	RED_LOG( "Core: package.m_patchLevel: {%u}", package.m_patchLevel );

	RED_LOG( "Core: package.m_supportedTextLanguages (%u entries):", package.m_supportedTextLanguages.Size());
	for ( const String& lang : package.m_supportedTextLanguages )
	{
		RED_LOG( "Core: \tlang {%hs}", lang.AsChar());
	}
}
#endif