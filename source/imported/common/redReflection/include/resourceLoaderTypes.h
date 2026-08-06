/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redMemory/include/weakPtr.h"
#include "../../redJobs2/include/jobPriority.h"
#include "resourcePath.h"
#include "postLoadContext.h"
#include "../../redIO/include/redIOCommon.h"
#include "serializationAsyncSource.h"

namespace res
{
	class ResourceToken;
	typedef red::SharedPtr< ResourceToken > ResourceTokenHandle;
	typedef red::WeakPtr< ResourceToken > ResourceTokenWeakHandle;

	// ctremblay: Legacy options. Kept around for faster refactor. Will go in due time.
	struct LoadingOptions
	{
		// Initial resource path that triggered loading of the resource (for debugging)
		ResourcePath initialResourcePath;
		PostLoadFlags postLoadFlags;
		Bool skipPostLoad = false;			// Skips the post load step during resource loading
		Bool skipBuffers = false;			// Skips all the buffers during resource loading
		Bool verifyExportTypes = false;
		Bool useGameCache = false;
	};

	struct IssueLoadingRequestParameter
	{
		ResourcePath path;
		ResourcePath parentPath;
		io::EAsyncPriority priority = io::eAsyncPriority_Normal;
		PostLoadFlags postLoadFlags;
		bool skipImports = false;
		bool skipPostLoad = false;
		bool skipBuffers = false;
		bool verifyExportTypes = false;
		bool useGameCache = false;
		serialization::RawDiskPosition ioHint;

		IssueLoadingRequestParameter() = default;

		explicit IssueLoadingRequestParameter( const ResourcePath& resourcePath )
			: IssueLoadingRequestParameter()
		{
			path = resourcePath;
		}

		IssueLoadingRequestParameter( const ResourcePath& resourcePath, const ResourcePath& parentResourcePath )
			: IssueLoadingRequestParameter()
		{
			path = resourcePath;
			parentPath = parentResourcePath;
		}

	};

	RED_REFLECTION_API bool operator==( const IssueLoadingRequestParameter & left, const IssueLoadingRequestParameter & right );

	RED_REFLECTION_API IssueLoadingRequestParameter Convert( const ResourcePath& path, const LoadingOptions & options );
}
