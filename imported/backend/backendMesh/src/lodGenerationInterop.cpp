/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "lodGenerationInterop.h"

#include "appearancesListInterop.h"
#include "../../../common/core/include/message.h"
#include "../../../common/resource/include/resourceEditorDepotImpl.h"
#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMesh/include/meshAppearance.h"
#include "../../../common/resourceMesh/include/meshDataViewRO.h"
#include "../../../common/resourceMesh/include/meshParamCloth.h"
#include "../../../common/resourceMesh/include/meshUtils.h"

#include "../../backendEngine/include/backendEngine.h"
#include "../../backendEngine/include/houdiniEngineInterface.h"
#include "../../backendEngine/include/resourceBackend.h"
#include "../../backendEngine/include/resourceEditor.h"

#include "backendMeshEditor.h"
#include "backendMeshPanelSharedData.h"
#include "lodGenerationUtils.h"
#include "meshDataView.h"

namespace tools
{
	const Float LODGenerationInterop::s_lodDistanceExponent{ 1.8 };

	LODGenerationInterop::LODGenerationInterop( MeshPanelSharedData* sharedData )
		: m_sharedData{ sharedData }
	{		
		red::DynArray< String > materialNames{ GetMaterialNames() };
		for ( const auto& materialName : materialNames )
		{
			m_lodGenerationInfo.materials.PushBack( { materialName, mesh::LODGenerationUtils::s_maxNumLODs + 1, false } );
		}
	}

	LODGenerationInterop::~LODGenerationInterop() = default;

	void LODGenerationInterop::SetupInterop( LODGenerationInterop* instance, ::interop::utils::Object& op )
	{	
		// values
		op.RegisterValue< LODGenerationInfo >( "generationInfo",
			[instance]( interop::IContext&, LODGenerationInfo& outValue ) { outValue = instance->GetLODGenerationInfo(); },
			[instance]( interop::IContext&, LODGenerationInfo value ) { instance->GetLODGenerationInfo() = value; } );

		op.RegisterValueRO< Bool >( "hasClothGraphical",
			[instance]() { return instance->HasClothGraphicalParameter(); } );

		op.RegisterValueRO< red::DynArray< String > >( "materialNames",
			[instance]() { return instance->GetMaterialNames(); } );

		// functors
		op.RegisterFunctor< Bool >( "generate",
			[instance]( interop::IContext& ) { return instance->Generate(); } );

		op.RegisterFunctor< Float >( "getAutoHideDistance",
			[instance]( interop::IContext& ) { return instance->GetAutoHideDistance(); } );

		op.RegisterFunctor< Uint32, Uint32 >( "getTriangleCount",
			[instance]( interop::IContext&, Uint32 lodIndex ) { return instance->GetTriangleCount( 0 ); } );

		// notifiers
		op.RegisterNotifier( "lodGenerationFinished", instance->m_lodGenerationFinished );
	}

	Bool LODGenerationInterop::HasClothGraphicalParameter() const
	{
		THandle< CMesh > mesh{ m_sharedData->GetEditor().GetResource< CMesh >() };
		auto param = mesh->GetParameter< mesh::MeshParamCloth_Graphical >();

		return param ? true : false;
	}

	Float LODGenerationInterop::GetAutoHideDistance() const
	{
		THandle< CMesh > mesh{ m_sharedData->GetEditor().GetResource< CMesh >() };
		mesh::DataViewRO meshDataView{ *mesh };

		return meshDataView.GetAutoHideDistance();
	}

	red::DynArray< Float > LODGenerationInterop::GetDistances() const
	{
		red::DynArray< Float > distances{ red::PoolBackend() };
		for ( const auto& lod : m_lodGenerationInfo.lods )
		{
			distances.PushBack( lod.distance );
		}
		return distances;
	}

	red::DynArray< String > LODGenerationInterop::GetMaterialNames() const
	{
		THandle< CMesh > mesh{ m_sharedData->GetEditor().GetResource< CMesh >() };
		mesh::DataViewRO meshDataView{ *mesh };

		// Use the default apperance for LOD generation.
		//
		// TODO: Read the currently selected apperance.
		// This would require exposing m_apperanceList from MeshResourceEditor.
		// Then remember to provide the same apperance name in LODGenerationInterop::Generate() as well.
		THandle< mesh::MeshAppearance > appearanceData = meshDataView.GetDefaultAppearanceData();

		red::HashSet< CName > materialNamesUnique{ red::PoolBackend() };
		for ( Uint32 chunkIdx : meshDataView.GetLODChunkIndices( 0 ) )
		{
			materialNamesUnique.Insert( appearanceData->GetChunkMaterialName( chunkIdx ) );
		}
		materialNamesUnique.Shrink();

		red::DynArray< String > materialNames{ red::PoolBackend() };
		for ( const CName& matName : materialNamesUnique )
		{
			materialNames.EmplaceBack( matName.AsChar() );
		}
		return materialNames;
	}

	Uint32 LODGenerationInterop::GetTriangleCount( Uint32 lodIndex ) const
	{
		THandle< CMesh > mesh{ m_sharedData->GetEditor().GetResource< CMesh >() };
		mesh::DataViewRO meshDataView{ *mesh };

		return mesh::utils::CountLODTriangles( meshDataView, lodIndex );
	}

	Bool LODGenerationInterop::Generate()
	{
		Bool result{ true };

		houdini_legacy::HoudiniEngineSession* session{ StartHoudiniEngine() };
		if ( session == nullptr )
		{
			m_lodGenerationFinished.Notify();
			return false;
		}

		if ( LoadHDA( mesh::LODGenerationUtils::s_hdaPath, *session ) == false )
		{
			m_lodGenerationFinished.Notify();
			return false;
		}

		if ( SetHDAPropertyValues( *session ) == false )
		{
			m_lodGenerationFinished.Notify();
			return false;
		}

		MeshResourceEditor& meshEditor{ static_cast< MeshResourceEditor& >( m_sharedData->GetEditor() ) };
		mesh::LODGenerationUtils::GenerationInfo generationInfo{};
		generationInfo.mesh = meshEditor.GetMesh();
		generationInfo.distances = GetDistances();
		generationInfo.numClothLODs = m_lodGenerationInfo.generationOptions.numClothLODs;
		generationInfo.skipUploadRenderData = false;

		const CName appearanceName = CName::NONE(); // Use the default apperance
		if ( meshEditor.GenerateLODsHE( generationInfo, session, appearanceName ) == false )
		{
			m_lodGenerationFinished.Notify();
			return false;
		}

		m_lodGenerationFinished.Notify();
		m_sharedData->NotifyLODsChange();

		return true;
	}

	houdini_legacy::HoudiniEngineSession* LODGenerationInterop::StartHoudiniEngine() const
	{
		CBackendEngine* backendEngine{ Cast< CBackendEngine >( GEngine ) };

		// check for houdini engine
		if ( backendEngine->IsHoudiniEngineAvailable() == false )
		{
			DisplayError( "No valid Houdini Engine license!" );
			return nullptr;
		}

		// check for valid session
		houdini_legacy::HoudiniEngineSession* session{ backendEngine->GetHoudiniSession() };
		if ( houdini_legacy::IsSessionValid( session ) == false )
		{
			String houdiniEngineErrorMessage{ "" };
			if ( backendEngine->StartHoudiniEngine( houdiniEngineErrorMessage ) )
			{
				session = backendEngine->GetHoudiniSession();
				if ( houdini_legacy::IsSessionValid( session ) == false )
				{
					DisplayError( "No valid Houdini Engine session!" );
					return nullptr;
				}
			}
			else
			{
				DisplayError( "Failed to start Houdini Engine! " + houdiniEngineErrorMessage );
				return nullptr;
			}
		}
		
		// session->m_saveDebugScene = true;

		return session;
	}

	Bool LODGenerationInterop::LoadHDA( const String& hdaPath, houdini_legacy::HoudiniEngineSession& session ) const
	{
		Int32 assetCount{ 0 };
		String msg{ "" };

		if ( houdini_legacy::LoadHoudiniDigitalAsset( &session, hdaPath, assetCount, msg ) == false )
		{
			DisplayError( "Failed to load HDA: " + hdaPath + "! " + msg );
			return false;
		}

		return true;
	}

	Bool LODGenerationInterop::SetHDAPropertyValues( houdini_legacy::HoudiniEngineSession& session ) const
	{
		if ( IsLODDistancesValid() == false )
		{
			DisplayError( "LOD distance is incorrect! Values should increase with every LOD level and be lower than auto-hide distance." );
			return false;
		}

		if ( IsLODTrianglePercentagesValid() == false )
		{
			DisplayError( "LOD triangle count is incorrect! Values should decrease with every LOD level." );
			return false;
		}

		LODGenerationInfo::GenerationOptions generationOptions{ m_lodGenerationInfo.generationOptions };
		mesh::LODGenerationUtils::HDAParamNames hdaParam{ mesh::LODGenerationUtils::s_hdaParamNames };
		houdini_legacy::IHDAInterfaceWrapper& wrapper{ *session.GetInterfaceWrapper() };

		// lods
		for ( Uint32 i{ 0 }; i < mesh::LODGenerationUtils::s_maxNumLODs; ++i ) 
		{ 
			SetHDAPropertyValue( String::Printf( "lod%d", i+1 ), false, wrapper ); // reset
		}
		for ( Uint32 i{ 0 }; i < m_lodGenerationInfo.lods.Size(); ++i )
		{
			SetHDAPropertyValue( String::Printf( "lod%d", i+1 ), true, wrapper );
			SetHDAPropertyValue( String::Printf( "lod%dpercent", i+1 ), static_cast< Int32 >( m_lodGenerationInfo.lods[i].trianglePercentage ), wrapper );
		}

		// materials
		String materialsIntact{ "" };
		String materialsO1{ "" };
		String materialsO2{ "" };
		String materialsO3{ "" };

		for ( const auto& material : m_lodGenerationInfo.materials )
		{
			if ( material.isIntact )
			{
				materialsIntact += material.name + " "; 
			}
			else if ( material.discardLevel == 1 ) 
			{ 
				materialsO3 += material.name + " "; 
			}
			else if ( material.discardLevel == 2 ) 
			{
				materialsO2 += material.name + " "; 
			}
			else if ( material.discardLevel == 3 ) 
			{
				materialsO1 += material.name + " "; 
			}
		}
		materialsIntact.TrimRight();
		materialsO1.TrimRight();
		materialsO2.TrimRight();
		materialsO3.TrimRight();

		SetHDAPropertyValue( hdaParam.materialsIntact, materialsIntact, wrapper );
		SetHDAPropertyValue( hdaParam.materialsO1, materialsO1, wrapper );
		SetHDAPropertyValue( hdaParam.materialsO2, materialsO2, wrapper );
		SetHDAPropertyValue( hdaParam.materialsO3, materialsO3, wrapper );

		// skinning type
		String skinningType{ generationOptions.skinningType };
		skinningType.ToLower();
		if ( skinningType == "vertex" )
		{
			SetHDAPropertyValue( hdaParam.skinningType, 0, wrapper );
		}
		else if ( skinningType == "uv" )
		{
			SetHDAPropertyValue( hdaParam.skinningType, 1, wrapper );
		}

		// other
		SetHDAPropertyValue( hdaParam.fuseDistance, generationOptions.fuseDistance, wrapper );
		SetHDAPropertyValue( hdaParam.hardSplitAngle, static_cast< Int32 >( generationOptions.hardSplitAngle ), wrapper );
		SetHDAPropertyValue( hdaParam.mergeCoplanar, generationOptions.mergeCoplanarFaces, wrapper );
		SetHDAPropertyValue( hdaParam.normalsAutoSmooth, generationOptions.normalsAutoSmooth, wrapper );
		SetHDAPropertyValue( hdaParam.polycountLimit, static_cast< Int32 >( generationOptions.polycountLimit ), wrapper );
		SetHDAPropertyValue( hdaParam.preserveLOD1, generationOptions.preserveLOD1, wrapper );
		SetHDAPropertyValue( hdaParam.preserveNormals, generationOptions.forceOriginalVertexNormals, wrapper );
		SetHDAPropertyValue( hdaParam.splitChunk, generationOptions.separateChunks, wrapper );
		SetHDAPropertyValue( hdaParam.useHardsurfaceMode, generationOptions.useHardSurfaceMode, wrapper );
		SetHDAPropertyValue( hdaParam.useLOD1Skinning, generationOptions.useLOD1Skinning, wrapper );
		SetHDAPropertyValue( hdaParam.pushDiodeShader, generationOptions.pushDiodes, wrapper );
		SetHDAPropertyValue( hdaParam.pushDiodeValue, generationOptions.pushDiodesValue, wrapper );

		return true;
	}

	template< typename T >
	Bool LODGenerationInterop::SetHDAPropertyValue( const String& propertyName, T value, houdini_legacy::IHDAInterfaceWrapper& wrapper ) const
	{
		if ( wrapper.SetPropertyValue( RED_NAME( propertyName ), value ) == false )
		{
			DisplayError( "Failed to set " + propertyName + " HDA property!" );
			return false;
		}

		return true;
	}

	Bool LODGenerationInterop::IsLODDistancesValid() const
	{
		THandle< CMesh > mesh{ m_sharedData->GetEditor().GetResource< CMesh >() };
		mesh::DataViewRO meshDataView{ *mesh };
		Float autoHideDistance{ meshDataView.GetAutoHideDistance() };
		Float lastDistance{ 0.0f };

		for ( const auto& lod : m_lodGenerationInfo.lods )
		{
			if ( lod.distance <= lastDistance || lod.distance >= autoHideDistance )
			{
				return false;
			}

			lastDistance = lod.distance;
		}

		return true;
	}

	Bool LODGenerationInterop::IsLODTrianglePercentagesValid() const
	{
		Uint32 lastTrianglePercentage{ 100 };
		for ( const auto& lod : m_lodGenerationInfo.lods )
		{
			if ( lod.trianglePercentage >= lastTrianglePercentage )
			{
				return false;
			}

			lastTrianglePercentage = lod.trianglePercentage;
		}

		return true;
	}

	void LODGenerationInterop::DisplayError( const String& errorMessage ) const
	{
		RED_REPORT_ERROR( RED_LOCATION( m_sharedData->GetEditor().GetPath() ), errorMessage.AsChar() );
	}
} // world

