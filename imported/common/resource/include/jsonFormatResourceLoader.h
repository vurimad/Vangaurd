/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../common/redReflection/include/serializationLoader.h"
#include "jsonGenericLoader.h"

namespace job
{
	class Builder;
}

namespace serialization
{
	class LoadingContext;
	class IAsyncSource;
	class LoadingResult;
	typedef red::SharedPtr<IAsyncSource> AsyncSourcePtr;
	typedef job::Value< LoadingResult > LoadingJobResult;
}

namespace json
{
	template< typename ResourceType >
	class JsonFormatResourceLoader : public ::serialization::ILoader
	{
		virtual void LoadAsyncWithCallback( const serialization::AsyncSourcePtr& data, const serialization::LoadingContext& context, const CallbackContext& callbackContext, job::CompletionDeferral&& deferral ) const override final
		{
			static_assert( std::is_base_of< CResource, ResourceType >::value, "ResourceType must be a CResource" );

			json::LoadJsonFormatResource<ResourceType>( data, context, callbackContext, std::move( deferral ) );
		}

		virtual Bool UseInCookedGame() const override { return false; }
	};

}