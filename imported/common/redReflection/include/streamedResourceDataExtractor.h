/*
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "streamedResource.h"
#include "mathBox.h"
#include "../../redCore/include/absolutePath.h"

namespace res
{

// An extractor to get streaming data from a resource.
// Injected into the streaming cache, so StreamedResource doesn't have to
// contain all the properties we want to cache here.
class RED_REFLECTION_API IStreamedResourceDataExtractor
{
	RTTI_DECLARE_POLYMORPHIC_TYPE( IStreamedResourceDataExtractor );
	RED_USE_MEMORY_POOL( red::PoolBackend );

protected:
	IStreamedResourceDataExtractor();

public:
	using DependenciesList = red::DynArray< res::ResourcePath >;

	virtual ~IStreamedResourceDataExtractor();

	// check, if given resource extension is supported by this extractor
	virtual Bool SupportsResource( const res::ResourcePath& path ) const = 0;

	// check if a given loaded resource is supported by this extractor
	// needed if the resource extension lies about what resource is contained within
	virtual Bool SupportsResource( const res::StreamedResource& res ) const = 0;

	// get data from partially loaded resource
	virtual void ExtractStreamingData( const res::ResourcePath& path, CName optionalName, const StreamedResource& resource, StreamingData& outData ) const = 0;

	// get list of resources that the given resource is dependent on
	virtual void ExtractDependencies( const res::ResourcePath& path, const StreamedResource& resource, DependenciesList& outDependencies ) const;
};

} // res
