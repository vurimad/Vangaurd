/**
* Copyright (c) 2018-2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"


#include "resourceLoaderScheduler.h"
#include "resourceToken.h"
#include "resourceDepot.h"
#include "resourceClassLookup.h"
#include "resource.h"
#include "resourceBank.h"
#include "serializationAsyncSource.h"
#include "resourceLoaderThrottler.h"
#include "multiplayerSetup.h"
#include "../../redCore/include/absolutePath.h"

namespace res
{
	ResourceLoaderScheduler::ResourceLoaderScheduler()
		: m_resourceLoader( nullptr )
		, m_resourceBank( nullptr )
		, m_depot( nullptr )
		, m_throttler( red::CreateUniquePtr< ResourceLoaderThrottler >() )
		, m_creationId( 0 )
		, m_isCooked( false )
	{
	}

	ResourceLoaderScheduler::~ResourceLoaderScheduler()
	{}

	void ResourceLoaderScheduler::Initialize( const ResourceLoaderSchedulerParameter & param )
	{
		m_resourceLoader = param.resourceLoader;
		m_resourceBank = param.resourceBank;
		m_depot = param.depot;
		m_isCooked = param.isCooked;
	}

	namespace helper
	{
		RED_INLINE ECookingPlatform GetCookingPlatform()
	{
#if defined( RED_PLATFORM_LINUX )
		RED_ASSERT( red::MultiplayerSetup::IsServer() );
		return PLATFORM_LinuxServer;
#elif defined( RED_PLATFORM_WINPC )
		if ( red::MultiplayerSetup::ShouldUseWindowsServerData() )
		{
			return PLATFORM_WindowsServer;
		}
		return PLATFORM_PC;
#elif defined( RED_PLATFORM_ORBIS )
		RED_ASSERT( !red::MultiplayerSetup::IsServer() );
		return PLATFORM_PS4;
#elif defined( RED_PLATFORM_DURANGO )
		RED_ASSERT( !red::MultiplayerSetup::IsServer() );
		return PLATFORM_XboxOne;
#else
	#error Unknown platform
		return PLATFORM_None;
#endif
	}

		static const char* GetDebugTxt( ECookingPlatform platform )
		{
			switch ( platform )
			{
			case PLATFORM_None: return "None";
			case PLATFORM_PC: return "PC";
			case PLATFORM_XboxOne: return "XboxOne";
			case PLATFORM_PS4: return "PS4";
			case PLATFORM_WindowsServer: return "WindowsServer";
			case PLATFORM_LinuxServer: return "LinuxServer";
			default:
				break;
			}
			return "<Unknown cooking platform>";
		}

		static void VerifyCookingPlatform( const CResource& resource )
		{
			if (!IsGameMode())
			{
				return;
			}

			const ECookingPlatform expectedPlatform = GetCookingPlatform();
			const ECookingPlatform resourcePlatform = resource.GetCookingPlatform();

			// Note: not all resources are actually cooked (yet), so None is also allowed and useful in its own way to verify whether something
			// was actually cooked.
			const Bool isCorrectCookingPlatform = resourcePlatform == expectedPlatform || resourcePlatform == PLATFORM_None;

			RED_FATAL_ASSERT( resource.GetClass()->GetName() != RED_NAME_CONSTEXPR_NOREG( "CBitmapTexture" ) || resourcePlatform != PLATFORM_None,
				"Do not disable setting what platform the resource was cooked for" );

			RED_FATAL_ASSERT( isCorrectCookingPlatform, "Wrong cooking platform in resource '%hs'; expected=%hs, actual=%hs",
				resource.GetPath().ToDebugString(),
				GetDebugTxt( expectedPlatform ),
				GetDebugTxt( resourcePlatform ) );
		}
	}

	void ResourceLoaderScheduler::StaticResourceLoadedCallback( const serialization::LoadingResult& result, const ResourceTokenWeakHandle& tokenUserData, const serialization::LoadingContext& loadingContext, job::CompletionDeferral&& deferral, void* userData )
	{
		auto* self = static_cast<ResourceLoaderScheduler*>( userData );

		auto token = tokenUserData.Lock();
		if (result.IsCancelled() || !token)
		{
			deferral.FinishDeferral();
			return;
		}

		RED_FATAL_ASSERT( token->GetPath().IsValid() );

		if ( !token->HasFailed() )
		{
			if ( !result.IsFailed() )
			{
				RED_FATAL_ASSERT(!result.m_loadedRootObjects.Empty(), "Failed to create any root objects '%s'", token->GetPath().ToDebugString());
				THandle< CResource > loadedResource = Cast< CResource >( result.m_loadedRootObjects[ 0 ] );
				RED_FATAL_ASSERT( loadedResource, "Root object of Resource is not an actual CResource!" );
				if ( self->m_isCooked )
				{
					helper::VerifyCookingPlatform( *loadedResource );
				}

				const Bool useGameCache = loadingContext.m_useGameCache;
				loadedResource = self->m_resourceBank->TryRegisterResource( token->GetPath(), loadedResource, useGameCache );
				token->Internal_AssignLoadedResource( loadedResource, std::move( deferral ) );
			}
			else
			{
				auto error = ResourceTokenErrorType::UnknownError;
				self->m_resourceLoader->Internal_RegisterFailedToken( token, error, std::move( deferral ) );
			}
		}
	}

	void ResourceLoaderScheduler::StaticUnregisteredResourceLoadedCallback( const serialization::LoadingResult& result, const ResourceTokenWeakHandle& tokenUserData, const serialization::LoadingContext& loadingContext, job::CompletionDeferral&& deferral, void* userData )
	{
		auto* self = static_cast<ResourceLoaderScheduler*>(userData);
		auto token = tokenUserData.Lock();
		if (result.IsCancelled() || !token)
		{
			// Shouldn't happen otherwise
			RED_FATAL_ASSERT(IsGameMode());

			deferral.FinishDeferral();
			return;
		}

		RED_FATAL_ASSERT( token->GetPath().IsValid() );

		if( !token->HasFailed() )
		{
			if( !result.IsFailed() )
			{
				RED_FATAL_ASSERT( !result.m_loadedRootObjects.Empty(), "Failed to create any root objects '%s'", token->GetPath().ToDebugString() );
				THandle< CResource > loadedResource = Cast< CResource >( result.m_loadedRootObjects[0] );
				RED_FATAL_ASSERT( loadedResource, "Root object of Resource is not an actual CResource!" );
				if( self->m_isCooked )
				{
					helper::VerifyCookingPlatform( *loadedResource );
				}
				token->Internal_AssignLoadedResource( loadedResource, std::move( deferral ) );
			}
			else
			{
				token->Internal_MarkAsFailed( ResourceTokenErrorType::UnknownError, std::move( deferral ) );
			}
		}
	}

	static void FillLoadingContext( serialization::LoadingContext& loadingContext, const res::ResourcePath& resolvedPath, Uint32 creationId, const IssueLoadingRequestParameter& param )
	{
		loadingContext.m_creationId = creationId;
		loadingContext.m_getAllLoadedObjects = true;
		loadingContext.m_resourcePath = resolvedPath;
		loadingContext.m_initialResourcePath = param.parentPath.IsValid() ? param.parentPath : resolvedPath;
		loadingContext.m_skipImports = param.skipImports;
		loadingContext.m_skipPostLoad = param.skipPostLoad;
		loadingContext.m_postLoadFlags = param.postLoadFlags;
		loadingContext.m_verifyExportTypes = param.verifyExportTypes;
		//loadingContext.m_debugStackTraceHandle = job::DebugTraceCall();
		loadingContext.m_priority = param.priority;
		loadingContext.ioHint = param.ioHint;
		loadingContext.m_useGameCache = param.useGameCache;
	}

	static serialization::LoaderPtr ResolveLoader( const res::ResourcePath resolvedPath, const bool isCooked )
	{
		serialization::LoaderPtr loader;

		if ( isCooked )
		{
			// Always create a binary loader here since using cooked binary data
			loader = serialization::ILoader::CreateLoader();
			RED_FATAL_ASSERT(loader&& loader->UseInCookedGame());
		}
		else
		{
			const red::StringView resolvedStringPath = resolvedPath.ToStringView();
			const bool hasStringPath = !resolvedStringPath.Empty();
			if ( !hasStringPath )
			{
				RED_LOG_ERROR( "Editor and other applications require to always have a ResourcePath with its string data: '%hs'", res::prv::Debug_GetResourcePathDebugText( resolvedPath ).AsChar() );
				return nullptr;
			}

			const rtti::ClassType* resourceClass = GetResourceClassForFile( resolvedPath ); 
			if ( resourceClass )
			{
				const red::StringView extension = red::paths::GetExtension( resolvedStringPath ); 
				// NOTE: Normally don't do this but ResourcePath's strings are guaranteed to be null terminated
				loader = serialization::ILoader::CreateLoader( extension.Data() );
			}
		}

		return loader;
	}

	void ResourceLoaderScheduler::ScheduleResourceLoadingJobs( const ResourceTokenHandle& token, DeferralRvalArgWorkaroundForMocks arg, const res::ResourcePath& resolvedPath, const IssueLoadingRequestParameter& param )
	{
		RED_FATAL_ASSERT( arg.movableDeferral );
		auto deferral = std::move( *arg.movableDeferral );

		serialization::LoaderPtr loader = ResolveLoader( resolvedPath, m_isCooked );
		if( !loader )
		{
			m_resourceLoader->Internal_RegisterFailedToken( token, ResourceTokenErrorType::InvalidExtension, std::move( deferral ) );
			return;
		}

		CreateResourceAsyncSourceParams createParams;
		createParams.ioHint = param.ioHint;

		red::SharedPtr< serialization::IAsyncSource > asyncSource = m_depot->CreateResourceAsyncSource( resolvedPath, createParams );
		if( !asyncSource )
		{
			m_resourceLoader->Internal_RegisterFailedToken( token, ResourceTokenErrorType::ResourceNotFound, std::move( deferral ) );
			return;
		}

		if (IsGameMode())
		{
			RED_FATAL_ASSERT( !param.skipPostLoad || !m_isCooked, "Cannot verify cooking platform with skipPostLoad" );
		}

		Uint32 creationId = m_creationId.Increment();
		serialization::LoadingContext loadingContext;
		FillLoadingContext( loadingContext, resolvedPath, creationId, param );

		//RED_LOG_CATEGORY( red::LoggerCategory::LoggerCategory_Resources, "++ %hs, %u", resolvedPath.ToDebugString(), creationId );

		serialization::ILoader::CallbackContext callbackContext;
		callbackContext.callback = &ResourceLoaderScheduler::StaticResourceLoadedCallback;
		callbackContext.userData = this;
		callbackContext.tokenUserData = token;
		loader->LoadAsyncWithCallback( asyncSource, loadingContext, callbackContext, std::move( deferral ) );
	}

	void ResourceLoaderScheduler::CantTouchThis_ScheduleUnregisteredResourceLoadingJobs( const ResourceTokenHandle& token, DeferralRvalArgWorkaroundForMocks arg, const res::ResourcePath& resolvedPath, const IssueLoadingRequestParameter& param )
	{
		RED_FATAL_ASSERT( arg.movableDeferral );
		auto deferral = std::move( *arg.movableDeferral );

		serialization::LoaderPtr loader = ResolveLoader( resolvedPath, m_isCooked );
		if( !loader )
		{
			token->Internal_MarkAsFailed( res::ResourceTokenErrorType::InvalidExtension, std::move( deferral ) );
			return;
		}

		red::SharedPtr< serialization::IAsyncSource > asyncSource = m_depot->CreateResourceAsyncSource( resolvedPath );
		if( !asyncSource )
		{
			token->Internal_MarkAsFailed( res::ResourceTokenErrorType::ResourceNotFound, std::move( deferral ) );
			return;
		}

		RED_FATAL_ASSERT( !param.skipPostLoad || !m_isCooked, "Cannot verify cooking platform with skipPostLoad" );

		Uint32 creationId = m_creationId.Increment();

		serialization::LoadingContext loadingContext;
		FillLoadingContext( loadingContext, resolvedPath, creationId, param );

		serialization::ILoader::CallbackContext callbackContext;
		callbackContext.callback = &ResourceLoaderScheduler::StaticUnregisteredResourceLoadedCallback;
		callbackContext.userData = this;
		callbackContext.tokenUserData = token;
		loader->LoadAsyncWithCallback( asyncSource, loadingContext, callbackContext, std::move( deferral ) );
	}

	red::UniquePtr< ResourceLoaderScheduler > CreateResourceLoaderScheduler( const ResourceLoaderSchedulerParameter & param )
	{
		red::UniquePtr< ResourceLoaderScheduler > scheduler = red::CreateUniquePtr< ResourceLoaderScheduler >();
		scheduler->Initialize( param );
		return scheduler;
	}
}
