/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "resourceImportCommand.h"

#include "importer.h"
#include "factory.h"

#include "../../../common/redReflection/include/resourceClassLookup.h"
#include "../../../common/redReflection/include/serializationSaver.h"
#include "../../../common/redReflection/include/resource.h"
#include "../../../common/resource/include/backendResourceDepot.h"
#include "../../../common/resource/include/resourceSaver.h"
#include "thumbnailGenerator.h"
#include "resourceBackend.h"
#include "environmentPresetsManager.h"
#include "resourceMetadata.h"
#include "resourceImportMetadata.h"

namespace tools
{

	ResourceImportCommand::ResourceImportCommand( 
		const res::ResourcePath& destinationPath, 
		const red::AbsolutePath& sourcePath,
		EnvironmentPresetsManager& envPresetsManager,
		res::IBackendResourceDepot* depot,
		interop::InteropNotifier* optionalThumbnailNotifier )
		: m_depot( depot )
		, m_thumbnailNotifier( optionalThumbnailNotifier )
		, m_envPresetsManager( envPresetsManager )
		, m_resourcePath( destinationPath )
		, m_sourcePath( sourcePath )
	{
		RED_FATAL_ASSERT( m_depot, "depot must not be null" );
	}

	ResourceImportCommand::~ResourceImportCommand()
	{
	}

	THandle< CResource > ResourceImportCommand::CreateResource( const rtti::ClassType* resourceClass, String& outMessage ) const
	{
		auto* factory = IFactory::FindFactory( resourceClass );
		if ( nullptr == factory )
		{
			outMessage = String::Printf( "Core: Unable to create '%hs' because there's no factory for resource '%hs'",
				m_resourcePath.ToString().AsChar(), resourceClass->GetName().AsChar() );
			return nullptr;
		}

		THandle< CResource > resource = resourceClass->CreateHandle< CResource >();
		if ( !resource )
		{
			outMessage = String::Printf( "Core: Unable to create '%hs' because resource '%hs' could not be created",
				m_resourcePath.ToString().AsChar(), resourceClass->GetName().AsChar() );
			return nullptr;
		}

		resource->Internal_SetPath( m_resourcePath );

		THandle< FactoryOptions > factoryOptions = factory->CreateOptions( m_resourcePath );
		if ( !factory->DoCreate( resource, factoryOptions ) )
		{
			outMessage = String::Printf( "Core: Unable to create '%hs' because factory for resource '%hs' failed",
				m_resourcePath.ToString().AsChar(), resourceClass->GetName().AsChar() );
			return nullptr;
		}

		return resource;
	}

	ResourceCommandResult ResourceImportCommand::Execute( const THandle< ResourceCommandOptions >& options )
	{
		THandle< ResourceImportOptions > importOptions = Cast< ResourceImportOptions >( options );
		if ( !importOptions )
		{
			if( options )
				return ResourceCommandResult::Error( "Core: Invalid options type: %s ", options->GetFriendlyName().AsChar() );
			else
				return ResourceCommandResult::Error( "Core: Options are not passed" );
		}

		// Check to make sure we're operating on the correct resources
		res::ResourcePath resourcePath = importOptions->GetResourcePath();
		RED_FATAL_ASSERT( resourcePath == m_resourcePath );
		red::AbsolutePath sourcePath = importOptions->GetSourcePath();
		RED_FATAL_ASSERT( sourcePath == m_sourcePath );

		const rtti::ClassType* resourceClass = res::prv::ResourceClassLookup::GetInstance().GetResourceClassForFile( m_resourcePath );
		if ( !resourceClass )
		{
			return ResourceCommandResult::Error( "Core: Unable to create '%hs' because it's using unknown extension", m_resourcePath.ToDebugString() );
		}

		String sourceExtension = red::paths::ExtractExtension( m_sourcePath );
		IImporter* importer = IImporter::FindImporter( resourceClass, sourceExtension );
		if ( nullptr == importer )
		{
			return ResourceCommandResult::Error( "Core: Unable to create '%hs' because there's no importer for resource '%hs' and extension '%hs'",
				m_resourcePath.ToDebugString(), resourceClass->GetName().AsChar(), sourceExtension.AsChar() );
		}

		THandle< CResource > resource;
		if ( m_depot->ResourceExists( m_resourcePath ) )
		{
			if ( m_depot->ResourceIsReadOnly( m_resourcePath ) )
			{
				return ResourceCommandResult::Error( "Core: Unable to re-import '%hs' (%hs) because the resource file has READ-ONLY attribute.\r\n"
				                                     "Check-out the file or remove the READ-ONLY attribute.",
				                                     m_resourcePath.ToDebugString(), resourceClass->GetName().AsChar() );
			}

			res::IssueLoadingRequestParameter loadingParam;
			loadingParam.path = m_resourcePath;
			loadingParam.postLoadFlags.skipUploadRenderData = true;
			resource = GResourceLoader->IssueLoadingRequest( loadingParam )->WaitUntilLoaded();
			if ( !resource )
			{
				return ResourceCommandResult::Error( "Core: Unable to import '%hs' because resource '%hs' could not be loaded",
					m_resourcePath.ToDebugString(), resourceClass->GetName().AsChar() );
			}
		}
		else
		{
			String message;
			resource = CreateResource( resourceClass, message );
			if ( !resource )
			{
				return ResourceCommandResult::Error( message );
			}
		}

		ImportResult importResult = importer->DoImport( resource, importOptions );
		if ( importResult.HasFailed() )
		{
			String message = importResult.GetErrorMessage();
			if ( message.Empty() )
			{
				return ResourceCommandResult::Error( "Core: Unable to import '%hs' because importer for resource '%hs' has failed",
					m_resourcePath.ToDebugString(), resourceClass->GetName().AsChar() );
			}
			return ResourceCommandResult::Error( message );
		}

		RED_FATAL_ASSERT( importResult.GetResources().Size() == 1, "Single resource expected" );
		resource = importResult.GetResources().Front();

		if(!importOptions->m_skipThumbnail)
		{
			if ( res::UpdateThumbnailInResource( resource, m_envPresetsManager ) && m_thumbnailNotifier )
			{
				m_thumbnailNotifier->Notify( m_resourcePath.ToString() );
			}
		}
		res::ResourceMetadata::SetImportOptions( resource, importOptions );

		res::ResourceSavingOptions savingOptions;
		savingOptions.m_targetPath = m_resourcePath;
		savingOptions.m_depot = m_depot;
		if ( !res::SaveResource( resource, savingOptions ) )
		{
			return ResourceCommandResult::Error( "Core: Unable to import '%hs' because saving failed", m_resourcePath.ToDebugString() );
		}

		return ResourceCommandResult::Success( resource, importResult.GetSuccessMessage() );
	}

	THandle< ResourceCommandOptions > ResourceImportCommand::CreateOptions( const THandle< ResourceCommandOptions > inputOptions ) const
	{
		THandle< ResourceCommandOptions > suggestedOptions = inputOptions;
		if ( !suggestedOptions )
		{
			if ( const THandle< res::ResourceMetadata >& metadata = res::ResourceMetadata::Get( m_resourcePath, true ) )
			{
				if ( const THandle< res::ResourceImportMetadata >& importMetadata = metadata->GetImportData() )
				{
					if ( const THandle< ResourceImportOptions >& importOptions = importMetadata->GetImportOptions() )
					{
						suggestedOptions = importOptions;
					}
				}
			}
		}

		const rtti::ClassType* resourceClass = res::prv::ResourceClassLookup::GetInstance().GetResourceClassForFile( m_resourcePath );
		if ( !resourceClass )
		{
			RED_LOG_WARNING( "Core: Unable to create '%hs' because it's using unknown extension", m_resourcePath.ToDebugString() );
			return nullptr;
		}

		String srcExtension = red::utils::ExtractExtension( m_sourcePath );
		IImporter* importer = IImporter::FindImporter( resourceClass, srcExtension );
		if ( nullptr == importer )
		{
			RED_LOG_WARNING( "Core: Unable to create '%hs' because there's no importer for resource '%hs' and extension '%hs'",
				m_resourcePath.ToDebugString(), resourceClass->GetName().AsChar(), srcExtension.AsChar() );

			return nullptr;
		}

		THandle< ResourceImportOptions > options = importer->CreateOptions( m_sourcePath );
		if ( suggestedOptions && suggestedOptions->GetClass() == options->GetClass() && !options->m_skipSuggestedOptions )
		{
			suggestedOptions->GetClass()->Copy( options.Get(), suggestedOptions.Get() );
		}
		options->SetResourcePath( m_resourcePath );
		options->SetSourcePath( m_sourcePath );
		return options;
	}
}
