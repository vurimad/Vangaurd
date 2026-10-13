/**
* Copyright (C)2019 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace tools
{
	// Recursive search for all resources with extensions specified in SupportedExtensions set
	void FindResourcesByExtensionRecursive( const String& directoryPath, const red::HashSet< String >& supportedExtensions, red::DynArray< res::ResourcePath >& outResources );

	// Extract supported extensions from resource class type
	void GatherSupportedExtensions( const rtti::ClassType* resourceClass, red::HashSet< String >& outExtensions );
}

