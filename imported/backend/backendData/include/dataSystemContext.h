/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "dataContext.h"

namespace dependency
{
class Cache;
class Database;
}

namespace data
{
class Cache;

// Context containing components specific to the system and tasks which 
// should not be passed down into the generator and cooker classes.
class BACKEND_DATA_API SystemContext : public Context
{
public:
	dependency::Cache& GetDepsCache( data::DepotLocation depotLocation ) const;

	dependency::Database& DependencyDatabase() const;

	data::Cache* GetDataCache() const;

	bool IsForcedBuild() const;

	bool ShouldCollectDebugInfo() const;

protected:
	SystemContext();
	~SystemContext();

	dependency::Cache* m_sourceDepsCache;
	dependency::Cache* m_cookedDepsCache;
	dependency::Database* m_depsDatabase;

	data::Cache* m_dataCache;

	bool m_force;
	bool m_collectDebugInfo;
};

//------------------------------------------------------------------------------

RED_INLINE dependency::Database& SystemContext::DependencyDatabase() const
{
	return *m_depsDatabase;
}

RED_INLINE data::Cache* SystemContext::GetDataCache() const
{
	return m_dataCache;
}

RED_INLINE bool SystemContext::IsForcedBuild() const
{
	return m_force;
}

RED_INLINE bool SystemContext::ShouldCollectDebugInfo() const
{
	return m_collectDebugInfo;
}

} // data
