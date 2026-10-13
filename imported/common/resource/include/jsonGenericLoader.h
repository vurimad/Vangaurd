/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "../../../common/resource/include/fileHelperLoader.h"
#include "../../../common/redReflection/include/resourceAsyncReference.h"
#include "../../../common/redReflection/include/serializationLoader.h"
#include "../../../common/redJobs2/include/jobBuilder.h"
#include "jsonParser.h"
#include "../../redJobs2/include/jobCounterOwner.h"

namespace json
{
	namespace prv
	{
		template< typename ResourceType > 
		RED_FORCE_INLINE typename std::enable_if< std::is_same< JsonResource, ResourceType >::value, void >::type
		AssignLoadedResource( const THandle< ResourceType >& resource, const res::json::JsonParser& jsonParser )
		{ 
			resource->SetRoot( jsonParser.GetRoot() );
		}

		template< typename ResourceType > 
		RED_FORCE_INLINE typename std::enable_if< !std::is_same< JsonResource, ResourceType >::value, void >::type
		AssignLoadedResource( const THandle< ResourceType >& resource, const res::json::JsonParser& jsonParser )
		{
			if( const THandle<ISerializable>& root = jsonParser.GetRoot() )
			{
				// Do nothing, loading was done directly into the destination.
				RED_ASSERT( resource == root, "Json loading should have been done directly into the target memory." );
			}
		}

		template< typename ResourceType > 
		RED_FORCE_INLINE typename std::enable_if< std::is_same< JsonResource, ResourceType >::value, void >::type
		AssignExportMemory( const THandle< ResourceType >& resource, res::json::JsonParser& jsonParser )
		{
			// Do nothing, we load into a new resource handle, because resource is the JsonResource container, not what
			// we're loading from the json.
		}

		template< typename ResourceType > 
		RED_FORCE_INLINE typename std::enable_if< !std::is_same< JsonResource, ResourceType >::value, void >::type
		AssignExportMemory( const THandle< ResourceType >& resource, res::json::JsonParser& jsonParser )
		{ 
			jsonParser.SetRoot( resource );
		}

		template< typename ResourceType >
		struct ResourceParserData
		{
			res::json::JsonParser jsonParser;
			THandle< ResourceType > resource;
		};

		template< typename ResourceType >
		RED_INLINE void PostProcessJSONHelper( job::Builder& builder, const job::Value< serialization::LoadingResult >& resultPtr, const job::Value<ResourceParserData<ResourceType>>& parserData, const serialization::LoadingContext& context )
		{
			ScopedProfilerChannel scopedProfilerChannel{ PBC_IO };

			builder.DispatchJob( "Json/ImportTableLoading", [parserData, resultPtr]( const job::RunContext& )
			{
				if ( !parserData->jsonParser.CreateImportTable() )
				{
					resultPtr->SetFailed();
				}
			} );

			builder.DispatchJob( "Json/DependResourcesLoading", [parserData, resultPtr, context]( const job::RunContext& runContext )
			{
				if ( !resultPtr->IsFailed() )
				{
					job::Builder depBuilder{ runContext };

					const red::DynArray<TResAsyncRef<CResource>>& dependencies = parserData->jsonParser.GetDependencies();

					res::LoadingOptions options;
					options.skipPostLoad = context.m_skipPostLoad;
					options.skipBuffers = context.m_skipBuffers;
					options.postLoadFlags = context.m_postLoadFlags;
					options.initialResourcePath = context.m_initialResourcePath;

					job::Counter loadingCounter{ job::Priority::Latent };
					const Uint32 arraySize = dependencies.Size();
					for ( Uint32 i = 0; i < arraySize; ++i )
					{
						const TResAsyncRef<CResource>& res = dependencies[ i ];
						res::ResourceTokenHandle token = res.IssueLoadingRequest( options );
						parserData->jsonParser.StoreLoadingToken( i, token );
						loadingCounter += token->GetWaitCounter();
					}
					depBuilder.DispatchWait(loadingCounter);
				}
			} );

			builder.DispatchJob( "Json/ExportTableLoading", [parserData, resultPtr]( const job::RunContext& )
			{
				if ( !resultPtr->IsFailed() )
				{
					if ( !parserData->jsonParser.CreateExportTable() )
					{
						resultPtr->SetFailed();
					}
				}
			} );

			builder.DispatchJob( "Json/ObjectStructureLoading", [parserData, context, resultPtr]( const job::RunContext& )
			{
				if ( !resultPtr->IsFailed() )
				{
					PostLoadContext postLoadContext{ context.m_postLoadFlags };
					if ( !parserData->jsonParser.LoadObjectStructure( postLoadContext, context.m_skipPostLoad ) )
					{
						resultPtr->SetFailed();
					}

					THandle< ResourceType > resource = Cast< ResourceType >( parserData->resource );
					AssignLoadedResource<ResourceType>( resource, parserData->jsonParser );
				}
			} );
		}
	}

	template< typename ResourceType >
	void LoadJsonFormatResource( const serialization::AsyncSourcePtr& data, const serialization::LoadingContext& context, const serialization::ILoader::CallbackContext& callbackContext, job::CompletionDeferral&& deferral )
	{
		loading::utils::LoadingParams params{ sizeof( ResourceType ), data, context };
		job::Value<prv::ResourceParserData<ResourceType>> parserData;
		parserData->resource = CreateHandle< ResourceType >();
		parserData->resource->Internal_SetPath( context.m_resourcePath );
		parserData->resource->Internal_SetCreationId( context.m_creationId );
		parserData->jsonParser.SetDebugName( context.m_resourcePath.ToDebugString() );

		loading::utils::prv::TParsingFunc parsingFunc
		{
			red::PoolResource(),
			[parserData]( red::String&& loadedText ) -> THandle< ISerializable >
			{
				prv::AssignExportMemory( parserData->resource, parserData->jsonParser );
				if ( parserData->jsonParser.ParseJsonFile( std::move( loadedText ) ) )
				{
					return parserData->resource;
				}
				return nullptr;
			}
		};

		loading::utils::prv::TPostProcessFunc postProcessFunc
		{
			red::PoolResource(),
			[parserData, context](job::Builder& builder, const job::Value< serialization::LoadingResult >& resultPtr)
			{
				prv::PostProcessJSONHelper<ResourceType>(builder, resultPtr, parserData, context);
			}
		};

		loading::utils::prv::TResultFunc resultFunc
		{ 
			red::PoolResource(),
			[callbackContext, context, deferral = std::move( deferral )]( const serialization::LoadingResult& result ) mutable
			{
				const auto& callback = callbackContext.callback;
				const auto& tokenUserData = callbackContext.tokenUserData;
				void* const userData = callbackContext.userData;
				callback( result, tokenUserData, context, std::move( deferral ), userData );
			}
		};

		red::SharedPtr<io::IOContext> ioContext;
		{
			if (auto token = callbackContext.tokenUserData.Lock())
			{
				ioContext = token->ResourceLoaderInternal_GetIOContext();
			}
		}


		loading::utils::prv::LoadTextResourceAsync( params, std::move( parsingFunc ), std::move( postProcessFunc ), std::move( resultFunc ), ioContext );
	}
}
