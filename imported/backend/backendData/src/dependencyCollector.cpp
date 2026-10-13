/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "dependencyCollector.h"
#include "dependencyTypesImpl.h"

namespace dependency
{

void Collector::Add( DependencyUniquePtr&& dependency )
{
	m_dependencies.PushBack( std::move( dependency ) );
}

const DependencyList& Collector::Internal_GetDependencyList() const
{
	return m_dependencies;
}

} // dependency
