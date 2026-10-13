/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "../../backendEngine/include/commandletEngineInstance.h"
#include "../../backendEngine/include/streamingManagerInstance.h"

#include "../../../common/resource/include/resourceEditorDepotImpl.h"
#include "../../../common/core/include/commandlet.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../common/redJobs2/include/jobSystem.h"
#include "../../../common/resource/include/jsonFileHelper.h"

#include "../../../common/redFileSystem/include/fileSys.h"

#include "../../../common/worldEntities/include/worldNodeEntity.h"
#include "../../../common/worldEntities/include/entityTemplate.h"
#include "../../../common/worldEntities/include/entityTemplateInclude.h"
#include "../../../common/redJobs2/include/jobBuilder.h"
#include "../../backendEngine/include/commandletUtils.h"
#include "../../../common/resourceMaterial/include/bitmapTexture.h"
#include "../../backendMaterial/include/bitmapTextureCooker.h"
#include "../../../common/resource/include/resourceSaver.h"

using namespace world;

using JsonWriterType = JSONFileHelper::JSONWriter;

class TextureConsoleBiasIncrementCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE( TextureConsoleBiasIncrementCommandlet );

public:
	TextureConsoleBiasIncrementCommandlet()
		: ICommandlet( RED_NAME_CONSTEXPR( "texture-console-bias" ) )
	{ }

	~TextureConsoleBiasIncrementCommandlet() = default;

	// ICommandlet interface
	virtual Bool InitializeEnvironment() override;
	virtual void TeardownEnvironment() override;
	virtual void PrintHelp() const override;
	virtual const char* GetOneLiner() const override { return "Finds entities that doesn't have proxy set"; }
	virtual bool Execute( const red::CommandLine& options ) override;

private:
	Bool ParseParams( const red::CommandLine& options );
	Bool ProcessTextureFiles( red::DynArray< res::ResourcePath >& extractedNodes );

	res::StreamingManagerInstance m_streamingInstance;
	commandlet::EngineInstance m_engineInstance;

	res::ResourceEditorDepot* m_editorDepot;

	String m_workDirectory;
	Bool m_isDryRun{ false };
	red::AbsolutePath m_outputFilePath;

	red::HashMap< res::ResourcePath, Bool > m_resourceInclusionCache;
};

//////////////////////////////////////////////////////////////////////////

RTTI_BEGIN_TYPE( TextureConsoleBiasIncrementCommandlet )
RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE()

//////////////////////////////////////////////////////////////////////////

Bool TextureConsoleBiasIncrementCommandlet::InitializeEnvironment()
{
	return m_engineInstance.Initialise() && m_streamingInstance.Initialize();
}

void TextureConsoleBiasIncrementCommandlet::TeardownEnvironment()
{
	m_streamingInstance.Shutdown();
	m_engineInstance.Shutdown();
}

void TextureConsoleBiasIncrementCommandlet::PrintHelp() const
{
	RED_LOG( "Usage:" );
	RED_LOG( "  texture-console-bias -dir=path [-dryRun -output=output_file_path]" );
}

Bool TextureConsoleBiasIncrementCommandlet::ParseParams( const red::CommandLine& options )
{
	if ( !options.GetFirstParam( "dir", m_workDirectory ) )
	{
		printf( "ReplaceWorldNodeCommandlet: World resource path not provided\n" );
		return false;
	}

	String tmp;
	if ( options.HasOption( "dryrun" ) || options.HasOption( "dryRun" ) )
	{
		m_isDryRun = true;

		if ( !options.GetFirstParam( "output", tmp ) )
		{
			printf( "ReplaceWorldNodeCommandlet: Output path not provided\n" );
			return false;
		}

		m_outputFilePath = red::AbsolutePath::ParseFilePath( tmp.AsChar() );
		if ( !m_outputFilePath.IsFilePath() )
		{
			printf( "ReplaceWorldNodeCommandlet: Output path is not a valid filePath.\n" );
			return false;
		}
	}

	return true;
}

Bool TextureConsoleBiasIncrementCommandlet::ProcessTextureFiles( red::DynArray< res::ResourcePath >& extractedNodes )
{
	red::StringBuilder< String > sb;

	Uint32 index = 0;
	for ( const res::ResourcePath& path : extractedNodes )
	{
		res::ResourceTokenHandle token = GResourceLoader->IssueLoadingRequest( path );
		token->WaitUntilLoaded();

		if ( THandle< CBitmapTexture > texture = Cast< CBitmapTexture >( token->GetResource() ) )
		{
			ITexture::SizeDesc descriptor = texture->GetSizeDesc();

			Int32 biasWidth = descriptor.width / ( ( Int32 ) ( math::Pow( 2.0, texture->GetTextureSetup().m_platformMipBiasConsole ) ) );
			Int32 biasHeight = descriptor.width / ( ( Int32 ) ( math::Pow( 2.0, texture->GetTextureSetup().m_platformMipBiasConsole ) ) );

			if ( biasWidth >= 512 || biasHeight >= 512 )
			{
				if ( !m_isDryRun )
				{
					texture->EnableMipChain( true );

					Int32 bias = texture->GetTextureSetup().m_platformMipBiasConsole;
					bias = Max( 0, Min( bias + 1, texture->GetMaxMipCount() ) );

					texture->SetDownscaleBiasForCookingPlatform( ECookingPlatform::PLATFORM_PS4, bias );

					tools::BitmapTextureCookerSetup cookerSetup( *texture );
					cookerSetup.cookPC = true;
					cookerSetup.cookDURANGO = true;
					cookerSetup.cookORBIS = true;
					cookerSetup.cookWINDOWS_SERVER = true;

					tools::BitmapTextureCooker cooker( cookerSetup );
					cooker.DoCook();

					texture->ReleaseRenderResource();

					res::ResourceSavingOptions options;
					options.m_targetPath = texture->GetPath();
					if ( !res::SaveResource( texture, options ) )
					{
						RED_LOG_ERROR( "Failed to save: %s", texture->GetPath().ToString().AsChar() );
					}	
				}
				else
				{
					sb.Appendf( "%s\n", path.ToString().AsChar() );
				}
			}
		}
	}

	if ( m_isDryRun )
	{
		if ( !m_outputFilePath.Empty() )
		{
			if ( !red::SaveStringToFile( m_outputFilePath, sb.ToString() ) )
			{
				RED_LOG_ERROR( "Failed to save report file %hs", m_outputFilePath.AsChar() );
			}
		}
	}

	return true;
}

bool TextureConsoleBiasIncrementCommandlet::Execute( const red::CommandLine& options )
{
	if ( !ParseParams( options ) )
	{
		return false;
	}

	auto* depot = commandlet::GetEditorDepot();

	const red::DynArray< String > patterns{ "*.xbm" };
	red::DynArray< res::ResourcePath > allTextureResources = commandlet::ScanResourceDepotDirectories( *depot, m_workDirectory.AsChar(), patterns );

	RED_LOG( "Found %u prefab resources", allTextureResources.Size() );

	// generate report
	return ProcessTextureFiles( allTextureResources );
}

