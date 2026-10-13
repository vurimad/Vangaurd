/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/redCore/include/crt.h"
#include "basicTextBuilder.h"

//------

CBasicTextBuilder::PagedBuffer::PagedBuffer( const Uint32 pageSize )
	: m_pageSize( pageSize )
	, m_curPage( nullptr )
{
}

CBasicTextBuilder::PagedBuffer::~PagedBuffer()
{
}

const Uint32 CBasicTextBuilder::PagedBuffer::GetTotalSize() const
{
	Uint32 totalSize = 0;
	for ( const auto& page : m_pages )
	{
		totalSize += page.GetDataSize();
	}
	return totalSize;
}

void CBasicTextBuilder::PagedBuffer::CopyData( void* outData ) const
{
	Uint8* writePtr = (Uint8*) outData;
	for ( const auto& page : m_pages )
	{
		red::Memcpy( writePtr, page.m_base, page.GetDataSize() );
		writePtr += page.GetDataSize();
	}
}

Uint64 CBasicTextBuilder::PagedBuffer::CalcCRC() const
{
	Uint64 crc = HASH64_BASE;
	
	for ( const auto& page : m_pages )
	{
		Uint32 size = page.GetDataSize();

		if ( size && page.m_base[size-1] == 0 )
			size -= 1;

		crc = ACalcBufferHash64Merge( page.m_base, size, crc );
	}

	return crc;
}

Bool CBasicTextBuilder::PagedBuffer::SaveToFile( IFile* file ) const
{
	for ( const auto& page : m_pages )
	{
		file->Serialize( page.m_base, page.GetDataSize() );
	}
	return true;
}

void CBasicTextBuilder::PagedBuffer::Append( const void* data, const Uint32 dataSize )
{
	// empty
	if ( !dataSize )
		return;

	// will we fit into current page ?
	if ( !m_curPage || (m_curPage->m_pos + dataSize > m_curPage->m_end) )
	{
		// add new page
		Page newPage;
		newPage.m_base = AllocPage( m_pageSize );
		newPage.m_pos = newPage.m_base;
		newPage.m_end = newPage.m_base + m_pageSize;
		m_pages.PushBack( newPage );

		// get pointer to the new page
		m_curPage = &m_pages.Back();
	}

	// append data in page
	RED_FATAL_ASSERT( m_curPage != nullptr, "Invalid writer state" );
	RED_FATAL_ASSERT( m_curPage->m_pos + dataSize <= m_curPage->m_end, "Invalid writer state" );
	red::Memcpy( m_curPage->m_pos, data, dataSize );
	m_curPage->m_pos += dataSize;
}

Uint8* CBasicTextBuilder::PagedBuffer::AllocPage( const Uint32 pageSize )
{
	return (Uint8*) RED_ALLOCATE( red::PoolEngine, pageSize );
}

void CBasicTextBuilder::PagedBuffer::FreePage( void* ptr )
{
	RED_FREE( red::PoolEngine, ptr );
}

//------

CBasicTextBuilder::CBasicTextBuilder( const Uint32 pageSize /*= 65536*/ )
	: m_buffer( pageSize )
	, m_stream( m_buffer )
	, m_writer( m_stream )
	, m_replacer( nullptr )
{
}

CBasicTextBuilder::~CBasicTextBuilder()
{

}

void CBasicTextBuilder::SetTextReplacer( const IBasicTextBuilderAutotext* textReplacer )
{
	m_replacer = textReplacer;
}

void CBasicTextBuilder::Write( const AnsiChar* txt, Uint32 length /*= Uint32(-1)*/ )
{
	// no dynamic text replacer
	if ( !m_replacer )
	{
		m_writer.Append( txt, length );
	}
	else
	{
		// process text
		const AnsiChar* cur = txt;
		Uint32 charsLeft = length;
		while ( charsLeft > 0 && *cur )
		{
			// replacement delimiter
			if ( charsLeft > 1 && cur[0] == '#' && cur[1] == '<' )
			{
				// skip initial marker
				cur += 2;
				charsLeft -= 2;

				// look up the end symbol
				const AnsiChar* keyStart = cur;
				const AnsiChar* keyEnd = cur;
				while ( charsLeft > 0 && *cur )
				{
					if ( charsLeft > 1 && cur[0] == '>' && cur[1] == '#' )
					{
						keyEnd = cur;
						cur += 2; // skip the marker
						charsLeft -= 2;
						break;
					}
					++cur;
					--charsLeft;
				}

				// match key in the replacer
				const Uint32 keyLength = (Uint32)(keyEnd - keyStart);
				const AnsiChar* replacedText = m_replacer->MatchKey( keyStart, keyLength );
				if ( replacedText != nullptr )
				{
					// insert the replaced text only
					m_writer.Append( replacedText );
				}
				else
				{
					// insert key
					m_writer.Append( "(Missing key: " );
					m_writer.Append( String( keyStart, keyLength ).AsChar() );
					m_writer.Append( ")");
				}
			}
			else
			{
				// normal part of the text
				// TODO: optimize - we can print as much as till next #
				m_writer.Append( cur, 1 );
				cur += 1;
				charsLeft -= 1;
			}
		}
	}
}

STATIC_CHECK_USE_DECL
void CBasicTextBuilder::Writef( const AnsiChar* txt, ... )
{
	TLocalBuffer buf;
	va_list args;

	va_start( args, txt );
	red::VSNPrintF( buf, RED_ARRAY_COUNT(buf), txt, args );
	va_end( args );

	Write( buf );
}

const Uint32 CBasicTextBuilder::GetDataSize() const
{
	m_writer.Flush();
	return m_buffer.GetTotalSize();
}

void CBasicTextBuilder::CopyData( void* outputBuffer ) const
{
	m_writer.Flush();
	m_buffer.CopyData( outputBuffer );
}

Uint64 CBasicTextBuilder::CalcCRC() const
{
	return m_buffer.CalcCRC();
}

namespace Helper
{
	Bool CalcFileCRC( const red::AbsolutePath& absolutePath, Uint64& outCRC )
	{
		auto existingFile = GFileManager->CreateFileReader( absolutePath, FOF_AbsolutePath );
		if ( !existingFile )
			return false;

		// calculate CRC
		Uint8 buffer[ 64*1024 ];
		const auto size = existingFile->GetSize();
		while ( existingFile->GetOffset() < size )
		{
			const Uint32 readSize = Min< Uint32 >( RED_ARRAY_COUNT_U32(buffer), (Uint32)( size - existingFile->GetOffset()) );
			existingFile->Serialize( buffer, readSize );
			outCRC = ACalcBufferHash64Merge( buffer, readSize, outCRC );
		}

		return true;
	}
}

void CBasicTextBuilder::SaveToString( String& outString ) const
{
	m_writer.Flush();

	const auto size = m_buffer.GetTotalSize();
	outString.Resize( size+1 );
	m_buffer.CopyData( outString.Data() );
	outString[size] = 0;
}

Bool CBasicTextBuilder::SaveToFile( const red::AbsolutePath& absolutePath, const Bool onlyIfDifferent /*= true*/, Bool* outActuallySaved /*= nullptr*/, const red::DateTime* timestampToSet /*= nullptr*/ ) const
{
	// make sure we flushed the writer
	m_writer.Flush();

	// compare - and don't save if the file already contains the same content
	if ( onlyIfDifferent )
	{
		// do we have existing file ? if so calculate CRC of the content
		Uint64 existingCRC = HASH64_BASE;
		if ( Helper::CalcFileCRC( absolutePath, existingCRC ) )
		{
			// get current CRc
			const Uint64 currentCRC = CalcCRC();
			if ( currentCRC == existingCRC )
			{
				// we didnt save the file
				if ( outActuallySaved )
					*outActuallySaved = false;

				// sync the timestamp
				if ( timestampToSet )
					GFileManager->SetFileTime( absolutePath, *timestampToSet );

				// content DOES NOT require saving
				return true;
			}
		}
	}

	// save
	{
		{
			auto file = GFileManager->CreateFileWriter( absolutePath, FOF_AbsolutePath );
			if ( file )
			{
				// we did save the file
				if ( outActuallySaved )
					*outActuallySaved = true;

				if ( ! m_buffer.SaveToFile( file.Get() ) )
					return false; // not saved
			}
		}

		// sync the timestamp
		if ( timestampToSet )
			GFileManager->SetFileTime( absolutePath, *timestampToSet );

		return true;
	}

	// not saved
	return false;
}

void CBasicTextBuilder::SaveToFile( IFile* file ) const
{
	m_writer.Flush();
	m_buffer.SaveToFile( file );
}

//------
