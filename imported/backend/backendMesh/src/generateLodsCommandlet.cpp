/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"

#include "../../../common/core/include/commandlet.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/redReflection/include/resourceDepot.h"
#include "../../../common/resource/include/resourceSaver.h"
#include "../../../common/renderData/include/renderVertices.h"
#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMesh/include/meshAppearance.h"

#include "../../backendEngine/include/commandletEngineInstance.h"
#include "../../backendEngine/include/houdiniEngineUtils.h"
#include "../../backendEngine/include/interactiveCommandletCommunicationHandler.h"

#include "lodGenerationUtils.h"
#include "meshDataBuilder.h"
#include "meshDataView.h"
#include "../../../common/renderData/include/renderer.h"

namespace tools
{
	class GenerateLODsCommandlet : public red::ICommandlet
	{
		RTTI_DECLARE_TYPE( GenerateLODsCommandlet );

	public:
		GenerateLODsCommandlet();
		~GenerateLODsCommandlet();

		virtual Bool Execute( const red::CommandLine& options ) override;
		virtual const char* GetOneLiner() const override;
		virtual void PrintHelp() const override;

		virtual Bool InitializeEnvironment() override;
		virtual void TeardownEnvironment() override;

		void SetError( const String& errorMsg );

	private:
		Bool ParseSettings( const red::CommandLine& options, red::AbsolutePath& settingsFilePath );
		Bool SetupParameters();

		template< typename T >
		Bool SetHDAPropertyValue( const String& propertyName, T value, THandle< houdini_legacy::IHDAInterfaceWrapper > wrapper, String& outMsg );

		red::UniquePtr< houdini_legacy::HoudiniEngineSession > m_internalHoudiniSession;

		Uint32 m_numLODs;
		red::DynArray< Float > m_distances;
		red::DynArray< Int32 > m_trianglePercentages;

		Float m_fuseDistance;
		Int32 m_hardSplitAngle;
		Bool m_mergeCoplanar;
		Bool m_normalsAutoSmooth;
		Uint32 m_numClothLODs;
		Int32 m_polycountLimit;
		Bool m_preserveLOD1;
		Bool m_preserveNormals;
		Bool m_splitChunk;
		String m_skinningType;
		Bool m_useHardSurfaceMode;
		Bool m_useLOD1Skinning;
		Bool m_pushDiodeShader;
		Float m_pushDiodeValue;

		red::UniquePtr< red::InteractiveCommandletCommunicationHandler > m_comm;
		Bool m_errorSet = false;

		commandlet::EngineInstance m_engineInstance;
	};

	GenerateLODsCommandlet::GenerateLODsCommandlet()
		: ICommandlet( RED_NAME_CONSTEXPR( "GenerateLODs" ) )
		, m_numLODs{ 0 }
		, m_distances{ red::PoolBackend() }
		, m_trianglePercentages{ red::PoolBackend() }

		, m_fuseDistance{ 0.0001f }
		, m_hardSplitAngle{ 50 }
		, m_mergeCoplanar{ true }
		, m_normalsAutoSmooth{ false }
		, m_numClothLODs{ 2 }
		, m_polycountLimit{ 500 }
		, m_preserveLOD1{ false }
		, m_preserveNormals{ false }
		, m_skinningType{ "Vertex" }
		, m_splitChunk{ false }
		, m_useHardSurfaceMode{ true }
		, m_useLOD1Skinning{ true }
		, m_pushDiodeShader{ false }
		, m_pushDiodeValue{ .005f }
	{
	}

	GenerateLODsCommandlet::~GenerateLODsCommandlet()
	{
	}

	Bool GenerateLODsCommandlet::Execute( const red::CommandLine& options )
	{
		red::AbsolutePath settingsFilePath;
		if ( !ParseSettings( options, settingsFilePath ) || !settingsFilePath.IsFilePath() )
		{
			return false;
		}
		m_internalHoudiniSession->m_saveDebugScene = true;
		m_comm = red::CreateUniquePtr< red::InteractiveCommandletCommunicationHandler >( options );
		red::Stages mainStages = m_comm->DeclareStages( { "Generate LODs" } );

		String pathsToProceed;
		if ( !red::LoadFileToString( settingsFilePath, pathsToProceed ) )
		{
			SetError( String::Printf( "Couldn't load file %hs", settingsFilePath.ToDebugString() ) );
			return false;
		}
		
		red::DynArray< String > filePaths;
		filePaths = pathsToProceed.Split( ";" );
		
		if ( !SetupParameters() )
		{
			return false;
		}

		const Uint32 taskId = m_comm->GetTask( "Save resources" );
		const Uint32 size = filePaths.Size();
		for ( Uint32 index : filePaths.Indices() )
		{
			const String& filePath = filePaths[ index ];
			m_comm->SetTaskProgress( taskId, index, size );

			res::IssueLoadingRequestParameter param;
			param.path = res::ResourcePath::Build( filePath.AsChar() );
			param.postLoadFlags.skipUploadRenderData = true;
	
			res::ResourceTokenHandle token = GResourceLoader->IssueLoadingRequest( param );
			auto mesh = token->WaitUntilLoaded< CMesh >();

			mesh::LODGenerationUtils::GenerationInfo generationInfo;
			generationInfo.mesh = mesh;
			generationInfo.distances = m_distances;
			generationInfo.numClothLODs = 2;
			generationInfo.skipUploadRenderData = true;

			const CName appearanceName = CName::NONE(); // Use the default apperance
			if ( mesh && mesh::LODGenerationUtils::GenerateLODsHE( generationInfo, m_internalHoudiniSession.Get(), appearanceName ) )
			{
				if ( !res::SaveResource( mesh ) )
				{
					m_comm->GetMessage( taskId, String::Printf( "Failed to save %hs", param.path.ToString().AsChar() ), red::TaskTextMessage::Error );
				}
			}
			
			mesh.Reset();
			token.Reset();
			GRender->Flush();
		}
		mainStages.FinishCurrentStage();

		return true;
	}

	Bool GenerateLODsCommandlet::SetupParameters()
	{
		Int32 count;
		String msg{ "" };
		if ( !houdini_legacy::LoadHoudiniDigitalAsset( m_internalHoudiniSession.Get(), mesh::LODGenerationUtils::s_hdaPath, count, msg ) )
		{
			SetError( "Couldn't load houdini asset. " + msg );
			return false;
		}

		THandle< houdini_legacy::IHDAInterfaceWrapper > wrapper = m_internalHoudiniSession->GetInterfaceWrapper();
		if ( wrapper == nullptr )
		{
			SetError( "Couldn't obtain houdini interface wrapper" );
			return false;
		}

		//
		
		Bool result{ true };

		for ( Uint32 i{ 0 }; i < mesh::LODGenerationUtils::s_maxNumLODs; ++i ) 
		{ 
			result &= SetHDAPropertyValue( String::Printf( "lod%d", i + 1 ), false, wrapper, msg ); // reset
		}
		for ( Uint32 i{ 0 }; i < m_numLODs; ++i )
		{
			result &= SetHDAPropertyValue( String::Printf( "lod%d", i + 1 ), true, wrapper, msg );
			result &= SetHDAPropertyValue( String::Printf( "lod%dpercent", i + 1 ), m_trianglePercentages[i], wrapper, msg );
		}

		auto hdaParam{ mesh::LODGenerationUtils::s_hdaParamNames };

		String skinningType{ m_skinningType };
		skinningType.ToLower();
		if ( skinningType == "vertex" )
		{
			result &= SetHDAPropertyValue( hdaParam.skinningType, 0, wrapper, msg );
		}
		else if ( skinningType == "uv" )
		{
			result &= SetHDAPropertyValue( hdaParam.skinningType, 1, wrapper, msg );
		}

		result &= SetHDAPropertyValue( hdaParam.fuseDistance, m_fuseDistance, wrapper, msg );
		result &= SetHDAPropertyValue( hdaParam.hardSplitAngle, m_hardSplitAngle, wrapper, msg );
		result &= SetHDAPropertyValue( hdaParam.mergeCoplanar, m_mergeCoplanar, wrapper, msg );
		result &= SetHDAPropertyValue( hdaParam.normalsAutoSmooth, m_normalsAutoSmooth, wrapper, msg );
		result &= SetHDAPropertyValue( hdaParam.polycountLimit, m_polycountLimit, wrapper, msg );
		result &= SetHDAPropertyValue( hdaParam.preserveLOD1, m_preserveLOD1, wrapper, msg );
		result &= SetHDAPropertyValue( hdaParam.preserveNormals, m_preserveNormals, wrapper, msg );
		result &= SetHDAPropertyValue( hdaParam.splitChunk, m_splitChunk, wrapper, msg );
		result &= SetHDAPropertyValue( hdaParam.useHardsurfaceMode, m_useHardSurfaceMode, wrapper, msg );
		result &= SetHDAPropertyValue( hdaParam.useLOD1Skinning, m_useLOD1Skinning, wrapper, msg );
		result &= SetHDAPropertyValue( hdaParam.pushDiodeShader, m_pushDiodeShader, wrapper, msg );
		result &= SetHDAPropertyValue( hdaParam.pushDiodeValue, m_pushDiodeValue, wrapper, msg );

		if ( result == false )
		{
			SetError( msg );
		}

		return result;
	}

	const char* GenerateLODsCommandlet::GetOneLiner() const
	{
		return "Generate LODs";
	}

	void GenerateLODsCommandlet::PrintHelp() const
	{
	}

	Bool GenerateLODsCommandlet::InitializeEnvironment()
	{
		m_engineInstance.Initialise();

		m_internalHoudiniSession = red::CreateUniquePtr< houdini_legacy::HoudiniEngineSession >();

		if ( !houdini_legacy::IsHoudiniEngineAvailable() )
		{
			return false;
		}

		houdini_legacy::InitializeHoudiniEngine( String(), *m_internalHoudiniSession );

		if ( !houdini_legacy::Start( m_internalHoudiniSession.Get(), String() ) )
		{
			return false;
		}

		return true;
	}

	void GenerateLODsCommandlet::TeardownEnvironment()
	{
		if ( m_internalHoudiniSession && houdini_legacy::IsHoudiniEngineAvailable() && houdini_legacy::IsSessionValid( m_internalHoudiniSession.Get() ) )
		{
			if ( !houdini_legacy::Close( m_internalHoudiniSession.Get() ) )
			{
				SetError( "Houdini Engine - Shutdown FAILURE!" );
			}
		}

		m_engineInstance.Shutdown();
	}

	void GenerateLODsCommandlet::SetError( const String& errorMsg )
	{
		if ( m_errorSet || !m_comm )
		{
			return;
		}

		m_errorSet = true;
		m_comm->SendGenericMessage( errorMsg );
	}

	Bool GenerateLODsCommandlet::ParseSettings( const red::CommandLine& options, red::AbsolutePath& settingsFilePath )
	{
		String path;
		if ( !options.GetFirstOption( "tempFile", path ) )
		{
			return false;
		}
		settingsFilePath = red::AbsolutePath::ParseFilePath( path );

		if ( !options.GetFirstParam( "numLODs", m_numLODs ) )
		{
			SetError( "Couldn't read lods count!" );
			return false;
		}

		if ( m_numLODs == 0 || m_numLODs > 3 )
		{
			SetError( String::Printf( "Wrong number of LODs passed: %u", m_numLODs ) );
			return false;
		}

		for ( Uint32 i{ 0 }; i < m_numLODs; ++i )
		{
			Uint32 val;
			String key = String::Printf( "lod%dpercent", i + 1 );
			if ( !options.GetFirstParam( key, val ) )
			{
				SetError( String::Printf( "Couldn't read parameter %hs!", key.AsChar() ) );
				return false;
			}
			m_trianglePercentages.PushBack( val );

			key = String::Printf( "lod%ddistance", i + 1 );
			if ( !options.GetFirstParam( key, val ) )
			{
				SetError( String::Printf( "Couldn't read parameter %hs! Distance will be calculated from bounding box", key.AsChar() ) );
				val = 0;
			}
			m_distances.PushBack( static_cast< Float >( val ) );
		}

		// optional parameters
		options.GetFirstParam( "fuseDistance", m_fuseDistance );
		options.GetFirstParam( "hardSplitAngle", m_hardSplitAngle );
		options.GetFirstParam( "mergeCoplanar", m_mergeCoplanar );
		options.GetFirstParam( "normalsAutoSmooth", m_normalsAutoSmooth );
		options.GetFirstParam( "numClothLODs", m_numClothLODs );
		options.GetFirstParam( "polycountLimit", m_polycountLimit );
		options.GetFirstParam( "preserveLOD1", m_preserveLOD1 );
		options.GetFirstParam( "preserveNormals", m_preserveNormals );
		options.GetFirstParam( "skinningType", m_skinningType );
		options.GetFirstParam( "splitChunk", m_splitChunk );
		options.GetFirstParam( "useHardsurfaceMode", m_useHardSurfaceMode );
		options.GetFirstParam( "useLOD1Skinning", m_useLOD1Skinning );
		options.GetFirstParam( "pushDiodeShader", m_pushDiodeShader );
		options.GetFirstParam( "pushDiodeValue", m_pushDiodeValue);

		return true;
	}

	template< typename T >
	Bool GenerateLODsCommandlet::SetHDAPropertyValue( const String& propertyName, T value, THandle< houdini_legacy::IHDAInterfaceWrapper > wrapper, String& outMsg )
	{
		if ( wrapper->SetPropertyValue( RED_NAME( propertyName ), value ) == false )
		{
			outMsg += "Failed to set " + propertyName + " HDA property! ";
			return false;
		}

		return true;
	}
}

RTTI_BEGIN_TYPE_IN_NAMESPACE( GenerateLODsCommandlet, tools )
	RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE()