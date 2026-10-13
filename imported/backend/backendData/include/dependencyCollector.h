/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "dependencyTypes.h"

namespace dependency
{

// Simple object used to collect the dependencies for a resource from the data system
class BACKEND_DATA_API Collector
{
public:
	// Add a dependency for this resource
	// Dependency objects should be created by the relevant Make*Dependency helper functions
	void Add( DependencyUniquePtr&& dependency );

	// Internal function to get the list of dependencies
	const DependencyList& Internal_GetDependencyList() const;

private:
	DependencyList m_dependencies;

	// TODO: Add duplicate checking
};

} // dependency
