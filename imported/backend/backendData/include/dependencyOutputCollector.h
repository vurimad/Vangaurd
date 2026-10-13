/*
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "dataTypes.h"
#include "dependencyTypesImpl.h"

namespace dependency
{

class BACKEND_DATA_API OutputCollector
{
public:
	void SetPrimary( const res::ResourcePath& path, data::DepotLocation depot );
	void Add( const res::ResourcePath& path, data::DepotLocation depot );

	void Clear();

	OutputFileInfo GetPrimaryOutput() const;
	OutputFileList GetOutputList() const;
	Uint32 GetOutputListSize() const;

private:
	OutputFileInfo m_primary;
	OutputFileList m_outputs;

	mutable red::SpinLock m_guard;
};

} // dependency
