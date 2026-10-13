/*
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "dataTaskMetrics.h"

#include "../../../common/redFileSystem/include/fileSys.h"

namespace data
{
namespace metrics
{

red::AbsolutePath Writer::DefaultMetricsDirectory()
{
	return red::paths::ParentAbsolutePath( GFileManager->GetGameRoot() ).AddDirPath( "temp\\metrics" );
}

red::AbsolutePath Writer::ComputeMetricsFilePath( const char* prefix, const red::DateTime& startTime )
{
	String filename = String::Printf( "%hs_%04d-%02d-%02d_%02d.%02d.txt", prefix,
		startTime.GetYear(), startTime.GetMonth() + 1, startTime.GetDay() + 1, startTime.GetHour(), startTime.GetMinute() );
	return data::metrics::Writer::DefaultMetricsDirectory().AddFilePath( filename );
}

Writer::Writer( red::UniquePtr< IFile >&& file )
	: m_buffer( red::CreateUniqueBuffer< red::PoolBackend >( RED_MEGA_BYTE( 4 ), 16 ) )
	, m_begin( static_cast< char* >( m_buffer.Get() ) )
	, m_current( m_begin )
	, m_end( m_begin + m_buffer.GetSize() )
	, m_file( std::move( file ) )
{
}

Writer::~Writer()
{
	Flush();
}

void Writer::Write( const String& str )
{
	Uint32 length = str.Length();
	const char* data = str.AsChar();

	while ( length > 0 )
	{
		Uint32 bytesToCopy = Min( static_cast< Uint32 >( m_end - m_current ), length );
		red::Memcpy( m_current, data, bytesToCopy );
		m_current += bytesToCopy;
		data += bytesToCopy;
		length -= bytesToCopy;

		if ( m_current == m_end )
		{
			Flush();
		}
	}
}

void Writer::Flush()
{
	m_file->Serialize( m_begin, static_cast< Uint32 >( m_current - m_begin ) );
	RED_FATAL_ASSERT( !m_file->HasErrors() );
	m_current = m_begin;
}

} // metrics
} // data