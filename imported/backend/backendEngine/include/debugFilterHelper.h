/**
* Copyright (c) CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../common/interop/include/interopUtilsManual.h"

namespace rend
{
	class DebugFilterMask;
}

class DebugFilterHelper
{
	RED_USE_MEMORY_POOL( red::PoolBackend );

	DECLARE_INTEROP( DebugFilterHelper );

public:
	DebugFilterHelper();
	virtual ~DebugFilterHelper();

	void SetFilterHierarchyFromString( const String &maskName, const String &filters );

	void DebugFilterChangedNotify();

protected:
	interop::InteropNotifier m_debugFilterChangedNotifier;
};

class QualityLevelHelper
{
	DECLARE_INTEROP( QualityLevelHelper );
	RED_USE_MEMORY_POOL( red::PoolBackend );

public:
	QualityLevelHelper();
	virtual ~QualityLevelHelper();

	void SetQualityLevel( int option );
	void SetTextureQualityLevel( int option );

	//void DebugFilterChangedNotify();

protected:
	//interop::InteropNotifier m_debugFilterChangedNotifier;
};
