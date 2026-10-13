/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../common/redJobs2/include/jobRunner.h"
#include "../../../common/redReflection/include/resource.h"
#include "../../../common/redReflection/include/serializationLoader.h"
#include "../../../common/redReflection/include/serializationAsyncSource.h"
#include "../../../common/redFileSystem/include/memoryFileReader.h"

namespace job
{
	class Builder;
}

namespace io
{
	class IOContext;
}

namespace loading
{
	namespace utils
	{
		struct RED_RESOURCE_API LoadingParams : red::NonCopyable
		{
			LoadingParams( Uint32 typeSize, const serialization::AsyncSourcePtr& data, const serialization::LoadingContext& loadingContext )
				: m_typeSize( typeSize )
				, m_data( data )
				, m_loadingContext( loadingContext )
			{
			}

			size_t m_typeSize;

			const serialization::AsyncSourcePtr& m_data;
			const serialization::LoadingContext& m_loadingContext;
		};

		namespace prv
		{				
			using TParsingFunc = red::Function < THandle< ISerializable >( red::String&& loadedText ) >;
			using TPostProcessFunc = red::Function < void( job::Builder& builder, const job::Value< serialization::LoadingResult >& resultPtr ) >;
			using TResultFunc = red::Function < void( const serialization::LoadingResult& result ) >;

			extern RED_RESOURCE_API void LoadTextResourceAsync( const LoadingParams& params, TParsingFunc parsingFunc, TPostProcessFunc postProcessFunc, TResultFunc resultFunc, const red::SharedPtr<io::IOContext>& ioContext );
		}

	}	// namespace utils

}	// namespace loading
