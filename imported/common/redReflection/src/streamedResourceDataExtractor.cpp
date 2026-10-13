/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "streamedResourceDataExtractor.h"

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( IStreamedResourceDataExtractor, res )
RTTI_END_TYPE()

namespace res
{

	IStreamedResourceDataExtractor::IStreamedResourceDataExtractor() = default;

	IStreamedResourceDataExtractor::~IStreamedResourceDataExtractor() = default;

	void IStreamedResourceDataExtractor::ExtractDependencies( const res::ResourcePath&, const StreamedResource&, DependenciesList& ) const
	{
	}

} // res