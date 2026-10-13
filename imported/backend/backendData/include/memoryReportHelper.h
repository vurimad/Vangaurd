/*
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */

#pragma once

class BACKEND_DATA_API MemoryReportHelper
{
public:
	MemoryReportHelper();
	MemoryReportHelper( const red::AbsolutePath& basePath, bool enabled );
	MemoryReportHelper( const MemoryReportHelper& parent, const red::StringView prefix );
	~MemoryReportHelper();

	void Write( const red::StringView title ) const;

private:
	const red::AbsolutePath m_basePath;
	const red::StringView m_prefix;
	const bool m_enabled;
};