/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../common/redReflection/include/serializationLoader.h"
#include "../../redJobs/include/jobValue.h"
#include "fileHelperLoader.h"

namespace job
{
	class Builder;
}

namespace io
{
	class IOContext;
}

namespace serialization
{
	class LoadingContext;
	class IAsyncSource;
	class LoadingResult;
	typedef red::SharedPtr<IAsyncSource> AsyncSourcePtr;
	typedef job::Value< LoadingResult > LoadingJobResult;
}

namespace serialization
{
	namespace helper
	{
		template< typename ResourceType, Bool checkParseData >
		struct ParseDataInvoker
		{
			static Bool Invoke( const THandle< ResourceType >& resource, String&& loadedText )
			{
				return resource->ParseData( std::move( loadedText ) );
			}
		};

		// Note: not every ParseData() even returns bool, hence partial class template specialization until if-constexpr available
		template< typename ResourceType >
		struct ParseDataInvoker< ResourceType, false >
		{
			static Bool Invoke( const THandle< ResourceType >& resource, String&& loadedText )
			{
				resource->ParseData( std::move( loadedText ) );
				return true;
			}
		};
	}

	template< typename ResourceType, Bool checkParseData = false >
	class TextFormatResourceLoader : public ILoader
	{
		virtual void LoadAsyncWithCallback( const AsyncSourcePtr& data, const LoadingContext& context, const CallbackContext& callbackContext, job::CompletionDeferral&& deferral ) const override final
		{
			static_assert( std::is_base_of< CResource, ResourceType >::value, "ResourceType must be a CResource" );

			loading::utils::LoadingParams params = { sizeof( ResourceType ), data, context };

			auto parsingFunc = []( red::String&& loadedText ) -> THandle< ISerializable >
			{
				auto resource = CreateHandle< ResourceType >();
				if ( helper::ParseDataInvoker< ResourceType, checkParseData >::Invoke( resource, std::move( loadedText ) ) )
				{
					return resource;
				}
				return nullptr;
			};

			auto postProcessFunc = []( job::Builder& builder, const job::Value< serialization::LoadingResult >& resultPtr )
			{
				/*no-op*/ 
			};

			loading::utils::prv::TResultFunc resultFunc{ red::PoolEngine(), 
				[callbackContext, context, deferral = std::move( deferral )](const serialization::LoadingResult& result) mutable /*mutable to move deferral*/
			{
				const auto& callback = callbackContext.callback;
				const auto& tokenUserData = callbackContext.tokenUserData;
				void* const userData = callbackContext.userData;

				auto token = tokenUserData.Lock();
				if ( token && !token->HasFailed()  && !result.IsFailed() ) //#tbd: not sure why we're checking token->HasFailed() here....
				{						
					RED_FATAL_ASSERT( !result.m_loadedRootObjects.Empty(), "Failed to create any root objects '%s'", token->GetPath().ToDebugString() );
					THandle< CResource > loadedResource = Cast< CResource >( result.m_loadedRootObjects[ 0 ] );

					if ( !loadedResource->GetPath().IsValid() )
					{
						loadedResource->Internal_SetPath(token->GetPath() );
					}
				}

				callback(result, tokenUserData, context, std::move(deferral), userData);
			} };

			red::SharedPtr<io::IOContext> ioContext;
			if (auto token = callbackContext.tokenUserData.Lock())
			{
				ioContext = token->ResourceLoaderInternal_GetIOContext();
			}

			loading::utils::prv::LoadTextResourceAsync( params, parsingFunc, postProcessFunc, std::move( resultFunc ), ioContext );
		}

		virtual Bool UseInCookedGame() const override { return false; }
	};
}