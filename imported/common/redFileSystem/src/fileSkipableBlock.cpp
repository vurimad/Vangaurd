/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "fileSkipableBlock.h"
#include "file.h"

namespace fs
{
	SkipableBlock::SkipableBlock( IFile& file )
		: m_file( file )
		, m_archiveOffset( file.GetOffset() )
		, m_skipOffset( 0 )
	{
		// Serialize skip offset
		file << m_skipOffset;
	}

	SkipableBlock::~SkipableBlock()
	{
		Uint64 diskPastOffset = m_file.GetOffset();
		m_skipOffset = static_cast< Uint32 >( diskPastOffset - m_archiveOffset );

		// Saving, save skip offset
		if ( m_file.IsWriter() )
		{
			// Serialize skip offset
			m_file.Seek( m_archiveOffset );
			m_file << m_skipOffset;
			m_file.Seek( diskPastOffset );
		}
	}

	void SkipableBlock::Skip()
	{
		RED_FATAL_ASSERT( m_file.IsReader(), "Skip() called on a file that is not a reader" );

		// Calculate jump position
		const Uint64 skipPos = m_archiveOffset + m_skipOffset;
		const Uint64 howMuchLeftToSkip = skipPos - m_file.GetOffset();

		if ( howMuchLeftToSkip > SKIP_BUFFER_SIZE )
		{
			m_file.Seek( skipPos );
		}
		else
		{
			Uint8 skipBuffer[ SKIP_BUFFER_SIZE ];
			m_file.Serialize( skipBuffer, static_cast<size_t>( howMuchLeftToSkip ) );
		}
	}

} // fs