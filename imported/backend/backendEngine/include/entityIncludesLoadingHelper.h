/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace ent
{
	class EntityTemplate;
}

namespace tools
{
	// Maximum number of nested includes.
	static constexpr Uint32 c_MaxTemplateIncludeDepth = 16u;

	// Stack of template includes, used during includes loading, mainly for the purpose of detection of include cycles.
	typedef red::StaticArray< THandle< ent::EntityTemplate >, c_MaxTemplateIncludeDepth > TemplateIncludesStack;

	// Structure used when loading includes. Also used when checking for include dependency cycles, when adding & changing includes.
	struct LoadTemplateIncludesParam
	{
		red::RWLock& includesMapLock;
		red::HashMap< res::ResourcePath, THandle<ent::EntityTemplate> >& includesMap;
		Bool IsCycleDetected;
	};

	// Load all includes of a given rootTemplate (recursively), store them in a map.
	// Does tests for cyclic dependencies, in case any are found reports the error in param structure.
	void BACKEND_ENGINE_API LoadTemplateIncludes( LoadTemplateIncludesParam& param, const THandle<ent::EntityTemplate>& rootTemplate,
	                           job::Builder& builder, const PostLoadFlags& postLoadFlags, const res::ResourcePath* extraInclude = nullptr );

} // tools
