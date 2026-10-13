/*
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "memoryReportHelper.h"

#include "../../../common/redContainers/include/string/stringUtils.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/redMemory/include/reportUtils.h"

MemoryReportHelper::MemoryReportHelper()
	: m_basePath()
	, m_prefix()
	, m_enabled( false )
{
}

MemoryReportHelper::MemoryReportHelper( const red::AbsolutePath& basePath, bool enabled )
	: m_basePath( basePath )
	, m_prefix( "" )
	, m_enabled( enabled )
{
	if ( m_enabled )
	{
		GFileManager->CreatePath( m_basePath );
	}
}

MemoryReportHelper::MemoryReportHelper( const MemoryReportHelper& parent, const red::StringView prefix )
	: m_basePath( parent.m_basePath )
	, m_prefix( prefix )
	, m_enabled( parent.m_enabled )
{
}

MemoryReportHelper::~MemoryReportHelper()
{
}

void MemoryReportHelper::Write( const red::StringView title ) const
{
	if ( m_enabled )
	{
		const auto reportPath = m_basePath.AddFilePath( red::StrCat( m_prefix, title, ".txt" ) );
		red::memory::LogFullReportToCustomFile( reportPath.AsChar() );
	}
}
