/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "entityIncludesLoadingHelper.h"

#include "../../../common/worldEntities/include/entityTemplate.h"
#include "../../../common/worldEntities/include/entityTemplateInclude.h"
#include "../../../common/redJobs2/include/jobBuilder.h"

namespace tools
{
	static void LoadTemplateIncludesRecursive( LoadTemplateIncludesParam& param, const red::SharedPtr< TemplateIncludesStack, red::PoolBackend >& templateIncludesStack,
	                                  job::Builder& builder, const PostLoadFlags& postLoadFlags, const res::ResourcePath* extraInclude = nullptr )
	{
		RED_FATAL_ASSERT( (templateIncludesStack != nullptr) && !templateIncludesStack->Empty() );
		const THandle< ent::EntityTemplate >& currentTemplate = templateIncludesStack->Back();

		// We iterate through all the template includes + one optional additional include.
		// This extra include is used when testing for include dependency cycles when adding new include.
		Uint32 numIncludes = currentTemplate->GetIncludes().Size() + (extraInclude != nullptr);

		for ( Uint32 includeIndex = 0; includeIndex < numIncludes; ++includeIndex )
		{
			TResAsyncRef< ent::EntityTemplate > templateResRef = (includeIndex < currentTemplate->GetIncludes().Size()) ?
				currentTemplate->GetIncludes()[includeIndex].GetTemplate() :
				TResAsyncRef< ent::EntityTemplate >( *extraInclude );

			if ( !templateResRef.IsValid() )    // Skip null include.
				continue;

			const res::ResourcePath templateIncludePath = templateResRef.GetPath();

			// Test for cyclic includes.
			for ( const THandle< ent::EntityTemplate >& previouslyIncludedTemplate : *templateIncludesStack )
			{
				if ( templateIncludePath == previouslyIncludedTemplate->GetPath() )
				{
					RED_SCOPE_LOCK( param.includesMapLock );
					param.IsCycleDetected = true;
					return;
				}
			}

			// Test if already loaded.
			{
				RED_SCOPE_LOCK( param.includesMapLock );
				if ( param.includesMap.KeyExist( templateIncludePath ) )
				{
					RED_LOG_INFO( "LoadTemplateIncludes:   template '%hs' is already loaded, not realoading", templateIncludePath.ToDebugString() );
					continue;          // This include is already loaded, skip it.
				}
			}

			// Add loading job.
			res::LoadingOptions loadingOptions;
			loadingOptions.postLoadFlags = postLoadFlags;
			res::ResourceTokenHandle token = templateResRef.IssueLoadingRequest( loadingOptions );
			builder.DispatchWait(token->GetWaitCounter());

			// Add recurse into includes job.
			builder.DispatchJob( "EntityResourceEditor/LoadIncludes", [&param, token, templateIncludesStack, templateResRef, postLoadFlags](const job::RunContext& runContext )
			{
				if ( token->HasFailed() )
				{
					RED_DATA_ERROR_BEGIN( red::LoggerCategory_Default, !token->HasFailed(),
					                      "Unable to load included entity template '%hs', the file might be missing.\r\n"
					                      "It is included by '%hs'", token->GetPath().ToDebugString(), templateIncludesStack->Back()->GetPath().ToDebugString() );
					RED_DATA_ERROR_ACTION_SHOW_ASSET( templateIncludesStack->Back()->GetPath().ToDebugString() );
					RED_DATA_ERROR_END();
					return;
				}

				THandle< ent::EntityTemplate > includedTemplate = Cast< ent::EntityTemplate >( token->GetResource() );

				{
					RED_SCOPE_LOCK( param.includesMapLock );
					param.includesMap.Insert( templateResRef.GetPath(), includedTemplate );
				}

				// Create new template includes stack with the newly loaded template added at the end.
				// We use ugly dynamic allocations here, this is needed due to concurrency and jobs.
				red::SharedPtr< TemplateIncludesStack, red::PoolBackend > newTemplateIncludesStack = red::CreateSharedPtr< TemplateIncludesStack, red::PoolBackend >();
				for ( const THandle< ent::EntityTemplate >& previouslyIncludedTemplate : *templateIncludesStack )
					newTemplateIncludesStack->PushBack( previouslyIncludedTemplate );
				newTemplateIncludesStack->PushBack( includedTemplate );

				job::Builder depBuilder{ runContext };
				LoadTemplateIncludesRecursive( param, newTemplateIncludesStack, depBuilder, postLoadFlags, nullptr );
			} );
		}
	}

	void LoadTemplateIncludes( LoadTemplateIncludesParam& param, const THandle<ent::EntityTemplate>& rootTemplate, job::Builder& builder, const PostLoadFlags& postLoadFlags, const res::ResourcePath* extraInclude )
	{
		red::SharedPtr< tools::TemplateIncludesStack, red::PoolBackend > templateIncludesStack = red::CreateSharedPtr< tools::TemplateIncludesStack, red::PoolBackend >();
		templateIncludesStack->PushBack( rootTemplate );

		LoadTemplateIncludesRecursive( param, templateIncludesStack, builder, postLoadFlags, extraInclude );
	}

} // tools
