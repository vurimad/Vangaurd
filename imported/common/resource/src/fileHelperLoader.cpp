/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "fileHelperLoader.h"
#include "../../redFileSystem/include/fileSys.h"
#include "../../redJobs2/include/jobRunner.h"
#include "../../redJobs2/include/jobDeferral.h"
#include "../../redReflection/include/serializationDecompressor.h"
#include "../../redJobs2/include/jobBuilder.h"
#include "../../redIO/include/redIOAsyncReadToken.h"

namespace loading
{
namespace utils
{
namespace prv
{
	namespace helper
	{
		struct LoadAsyncResourceAsyncContext : red::NonCopyable
		{
			RED_USE_MEMORY_POOL( red::PoolResource );

			serialization::LoadingContext loadingContext;
			TParsingFunc parsingFunc;
			TPostProcessFunc postProcessFunc;
			TResultFunc resultFunc;
			serialization::AsyncSourcePtr source;
			serialization::AsyncSourceReadSizes readSizes;
			red::SharedPtr<io::IOContext> ioContext;
		};

		static void NotifyFinished( LoadAsyncResourceAsyncContext* asyncContext, serialization::LoadingResult& result )
		{
			const auto& resultFunc = asyncContext->resultFunc;
			resultFunc( result );
			RED_DELETE( asyncContext );
		}

		static void BuildCoreJobChain( LoadAsyncResourceAsyncContext* asyncContext, serialization::AsyncSourceReadBuffer onDiskBuffer )
		{
			ScopedProfilerChannel scopedProfilerChannel{ PBC_IO };

			job::Value< serialization::LoadingResult > result;
			job::Value< String > loadedText;

			if ( onDiskBuffer.ValidRegion().Empty() )
			{
				result->SetFailed();
				NotifyFinished( asyncContext, *result );
				return;
			}

			const auto& context = asyncContext->loadingContext;

			job::Builder builder{ job::Priority::Latent, context.m_resourcePath.ToDebugString() };

			const auto& readSizes = asyncContext->readSizes;
			if ( !readSizes.IsCompressed() )
			{
				const auto& onDiskMemory = onDiskBuffer.ValidRegion();
				RED_FATAL_ASSERT( onDiskMemory.Size() == readSizes.memorySize );

				loadedText->Set( onDiskMemory.Pointer<char>(), onDiskMemory.Size() );
			}
			else
			{
				builder.DispatchJob( "Serialization/DecompressTextData", [ asyncContext, loadedText, result, onDiskBuffer = std::move( onDiskBuffer ) ]( const job::RunContext& )
				{
					const auto& onDiskMemory = onDiskBuffer.ValidRegion();
					const auto compressionType = compression::GetCompressionTypeFromData( onDiskMemory.Data(), onDiskMemory.Size() );

					const auto& readSizes = asyncContext->readSizes;
					loadedText->Resize( readSizes.memorySize );
					red::BlobSpan textSpan( &( *loadedText )[ 0 ], loadedText->Length() ); // TODO: If this gets used more, create a MakeBlobSpan for it

					const char* debugLogicalFileName = asyncContext->loadingContext.m_resourcePath.ToDebugString();
					if ( !serialization::Decompressor::DecompressData( debugLogicalFileName, onDiskMemory, textSpan ) )
					{
						RED_LOG_ERROR( "Failed  to decompress object buffer!" );
						loadedText->Clear();
						result->SetFailed();
					}
				} );
			}

			builder.DispatchJob( "Serialization/ParseTextData", [ asyncContext, loadedText, result ]( const job::RunContext& runContext )
			{
				if ( !result->IsFailed() )
				{
					const auto& parsingFunc = asyncContext->parsingFunc;
					auto resource = parsingFunc( std::move( *loadedText ) );
					if ( !resource )
					{
						result->SetFailed();
					}
					else
					{
						result->m_loadedObjects.PushBack( resource );
						result->m_loadedRootObjects.PushBack( resource );
					}
				}
			});

			builder.DispatchJob( "Serialization/PostProcessTextCallback", [asyncContext, result]( const job::RunContext& runContext )
			{
				if ( !result->IsFailed() )
				{
					job::Builder depBuilder{ runContext };
					const auto& postProcessFunc = asyncContext->postProcessFunc;
					postProcessFunc( depBuilder, result );
				}
			} );

			builder.DispatchJob( "Serialization/TextResultCallback", [asyncContext, result]( const job::RunContext& runContext )
			{
				NotifyFinished( asyncContext, *result );
			});
		}

		static void OnTextDataLoaded( serialization::AsyncSourceReadBuffer buffer, red::UniqueBuffer, void* userData, red::EAsyncResult )
		{
			auto* asyncContext = static_cast<LoadAsyncResourceAsyncContext*>( userData );
			RED_FATAL_ASSERT( asyncContext );

			BuildCoreJobChain( asyncContext, std::move( buffer ) );
		}

		static void LoadTextData( LoadAsyncResourceAsyncContext* asyncContext )
		{
			const auto& asyncSource = asyncContext->source;

			// If a memory async source, then can just start deserialization now. The memory is to remain valid as long as we keep a strong reference to the async source.
			{
				red::BlobView rawView;
				if ( asyncSource->TryGetMemorySource( rawView ) )
				{
					RED_FATAL_ASSERT(!asyncSource->RequiresHeaderPreParseStep(), "This should only be true for loose files");
					RED_FATAL_ASSERT(!asyncSource->GetReadSizes(0, asyncSource->GetSizeOnDisk()).IsCompressed(),
						"MemoryAsyncSource has no concept of in-memory buffer compresssion, and if it did we didn't get any memory from the AsyncIO system or anywhere else to decompress into");

					BuildCoreJobChain( asyncContext, serialization::AsyncSourceReadBuffer( rawView ) );
					return;
				}
			}

			serialization::IAsyncSource::CallbackContext callbackContext;
			callbackContext.userData = asyncContext;
			callbackContext.callback = &OnTextDataLoaded;

			const auto& readSizes = asyncContext->readSizes;
			serialization::IAsyncSource::ReadAsyncParams params;
			params.requestSource = io::RequestSource::ResourceSystem;
			params.offset = 0;
			params.size = readSizes.diskSize;
			params.priority = asyncContext->loadingContext.m_priority;
			params.ioContext = asyncContext->ioContext;
			
			// We'll decompress into the string itself instead of using temp serialization memory
			// NOTE: if not compressed, we could read into the string directly as well. However, text files should
			// be compressed in archives, and better to just have a simpler code path here.
			// Although the archives do choose an arbitrary cut-off for compression size, without considering the overall compression savings.
			params.allocDeserializationMemoryForDecompressorIfCompressed = false;

			asyncSource->ReadAsync(params, serialization::AsyncSourceReadBuffer(readSizes), callbackContext);
		}
	}

	void RED_RESOURCE_API LoadTextResourceAsync( const LoadingParams& params, TParsingFunc parsingFunc, TPostProcessFunc postProcessFunc, TResultFunc resultFunc, const red::SharedPtr<io::IOContext>& ioContext )
	{
		RED_FATAL_ASSERT( parsingFunc, "ParsingFunc cannot be invalid; if no parsing, pass in a no-op function" );
		RED_FATAL_ASSERT( postProcessFunc, "PostProcessFunc cannot be invalid; if no post-processing, pass in a no-op function" );
		RED_FATAL_ASSERT( resultFunc, "Result func cannot be invalid - fire and forget doesn't make sense here" );

		const auto& asyncSource = params.m_data;
		Uint32 sizeOnDisk = asyncSource->GetSizeOnDisk();

		auto* asyncContext = RED_NEW( helper::LoadAsyncResourceAsyncContext )();
		asyncContext->loadingContext = params.m_loadingContext;
		asyncContext->parsingFunc = std::move( parsingFunc );
		asyncContext->postProcessFunc = std::move( postProcessFunc );
		asyncContext->resultFunc = std::move( resultFunc );
		asyncContext->source = asyncSource;
		asyncContext->readSizes = asyncSource->GetReadSizes( 0, sizeOnDisk );
		asyncContext->ioContext = ioContext;

		if ( sizeOnDisk == 0 )
		{
			job::Value< serialization::LoadingResult > result;
			result->SetFailed();
			helper::NotifyFinished( asyncContext, *result );
			return;
		}

		helper::LoadTextData(asyncContext);
	}

} // prv
} // utils
} // loading
