/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "backendMeshPreviewEditMode_ShadowMesh.h"
#include "backendMeshPreviewPanel.h"
#include "meshDataView.h"
#include "meshDataBuilder.h"
#include "meshEditModes.h"
#include "physicsObjectResolver.h"
#include "physicsEditorScene.h"
#include "backendMeshPreviewPanelInputListener.h"
#include "physicsBackendHelper.h"
#include "physicsMesh.h"
#include "meshPhysicsUtils.h"

#include "../../backendEngine/include/editAction.h"
#include "../../backendEngine/include/editActionHistory.h"
#include "../../backendEngine/include/renderingWindowCamera.h"

#include "../../../common/renderDraw/include/fragmentBuilder.h"
#include "../../../common/renderData/include/renderProxyInitData.h"
#include "../../../common/renderData/include/renderProxy.h"
#include "../../../common/world/include/runtimeSystemRendering.h"
#include "../../../common/world/include/worldRuntimeScene.h"
#include "../../../common/redMemory/include/uniquePtr.h"
#include "../../../common/engine/include/systemsUpdater.h"
#include "../../../common/physics/include/physicsCollider.h"
#include "../../../common/generalEditorBackend/include/meshEditorInterop.h"
#include "../../../common/redContainers/include/string/stringView.h"
#include "../../../common/resource/include/resourceSaver.h"
#include "../../../common/resourceMesh/include/meshUtils.h"
#include "../../../common/resourceMesh/include/meshAppearance.h"
#include "../../../common/resourceMesh/include/meshPacker.h"
#include "../../../common/resourceMesh/include/meshDataViewRO.h"
#include "../../../common/resourceMesh/include/meshParamShadowMeshCreationData.h"
#include "../../../common/resourceMaterial/include/materialTemplate.h"

namespace tools
{
    struct MeshEditModeShadowMesh::impl
    {
        RED_USE_MEMORY_POOL(red::PoolBackend);

		physics::PhysicsEditorScene* m_editorScene;

		red::DynArray< THandle<physics::ICollider>> m_geometries;
		red::UniquePtr<MeshPreviewInputListener> m_inputListener;

		red::UniquePtr<SystemsUpdater> m_processor = nullptr;
		red::UniquePtr<world::RuntimeScene> m_runtimeScene = nullptr;

		RenderProxyPtr m_renderProxy = nullptr;
		THandle<CMesh> m_meshResource = nullptr;
		CName m_meshAppearance = CName::NONE();
		tools::ShadowMeshState m_state;
		interop::InteropNotifier m_onStateChangedNotifier;

		void SaveToMeshParameter();
		void LoadFromMeshParam();

		void Internal_SetShadowMeshState( const tools::ShadowMeshState& state, tools::IEditActionHistory& actionHistory );
    };

	void MeshEditModeShadowMesh::impl::Internal_SetShadowMeshState( const tools::ShadowMeshState& state, tools::IEditActionHistory& actionHistory )
	{
		struct Action_SetState : public tools::IEditAction
		{
			RED_BASE_CLASS( tools::IEditAction );

			Action_SetState( MeshEditModeShadowMesh::impl* pimpl, const tools::ShadowMeshState& current, const tools::ShadowMeshState& previous )
				: m_pimpl( pimpl )
				, m_current( current )
				, m_previous( previous )
			{}

			String GetDescription() const
			{
				return "Set State";
			}

			Bool Do()
			{
				m_pimpl->m_state = m_current;
				m_pimpl->SaveToMeshParameter();
				m_pimpl->m_onStateChangedNotifier.Notify( m_current );
				return true;
			}

			Bool Undo()
			{
				m_pimpl->m_state = m_previous;
				m_pimpl->SaveToMeshParameter();
				m_pimpl->m_onStateChangedNotifier.Notify( m_previous );
				return true;
			}

			virtual void GetAffectedResources( red::Set< res::ResourcePath >& outResourcePaths ) const override final
			{
			}

			MeshEditModeShadowMesh::impl* m_pimpl;
			tools::ShadowMeshState m_current;
			tools::ShadowMeshState m_previous;
		};

		actionHistory.Execute( red::CreateUniquePtr<Action_SetState>( this, state, m_state ) );
	}

	void MeshEditModeShadowMesh::impl::SaveToMeshParameter()
	{
		THandle<mesh::MeshParamShadowMeshCreationData> param = m_meshResource->CreateParameter<mesh::MeshParamShadowMeshCreationData>();

		if( param )
		{
			param->m_bonesPerGeometry = m_state.m_bonesPerGeometry;
			param->m_geometries = m_geometries;
		}
	}

	void MeshEditModeShadowMesh::impl::LoadFromMeshParam()
	{
		THandle<mesh::MeshParamShadowMeshCreationData> param = m_meshResource->GetParameter<mesh::MeshParamShadowMeshCreationData>();

		if( param )
		{
			m_state.m_bonesPerGeometry = param->m_bonesPerGeometry;
			m_geometries = param->m_geometries;
			m_state.m_numGeometries = m_geometries.Size();

			Int32 size = (Int32)m_state.m_boneNamesDictionary.Size();
			for( Uint32 i : param->m_bonesPerGeometry.Indices() )
			{
				if( param->m_bonesPerGeometry[ i ] >= size )
					param->m_bonesPerGeometry[ i ] = m_state.m_defaultBoneIndex;
			}
		}
	}

	//////////////////////////////////////////////////////////////////////////

    MeshEditModeShadowMesh::MeshEditModeShadowMesh()
        : MeshPreviewEditMode("ShadowMeshEditMode")
        , pimpl(red::CreateUniquePtr<MeshEditModeShadowMesh::impl>())
    {}

	MeshEditModeShadowMesh::~MeshEditModeShadowMesh()
	{
	}

    Color MeshEditModeShadowMesh::GetDisplayColor() const
    {
        return Color::DARK_BLUE();
    }

    Bool MeshEditModeShadowMesh::OnInitialize(const PreviewPanelSharedDataPtr& sharedData)
    {
		auto* genericSharedData = static_cast<GenericPreviewPanelSharedData*>(sharedData.Get());

		pimpl->m_processor = red::CreateUniquePtr<SystemsUpdater>();

		world::RuntimeInfo runtimeInfo( world::RuntimeMode::GamePreview );
		world::RuntimeSceneSetupInfo sceneSetup( runtimeInfo );
		sceneSetup.m_existingRenderScene = sharedData->GetRenderScene().Get();
		sceneSetup.m_processor = pimpl->m_processor.Get();

		pimpl->m_runtimeScene = world::CreateBackedScene( sceneSetup );

        return TBaseClass::OnInitialize(sharedData);
    }

    Bool MeshEditModeShadowMesh::OnActivate( IPreviewEditMode* currentMode )
    {
		MeshPanelSharedData* panelSharedData = GetSharedData();
		if( panelSharedData )
		{
			pimpl->m_editorScene = panelSharedData->GetMeshEditorPhysicsScene();
			pimpl->m_editorScene->SetVisible( false );

			physics::TSystemObjectsMap actorMap;
			red::DynArray< physics::PhysicsBasePtr > objects = pimpl->m_editorScene->GetPhysicsObjectsOnScene();
			THandle< physics::PhysicsMesh > physicsMeshData = nullptr;
			// find physics mesh
			for( const THandle< physics::PhysicsBase >& object : objects )
			{
				if( object->GetBaseType() == physics::EPBT_Mesh )
				{
					physicsMeshData = Cast< physics::PhysicsMesh >( object );
					break;
				}
			}

			pimpl->m_inputListener = red::CreateUniquePtr< MeshPreviewInputListener >( panelSharedData->GetEditor().GetPath(), 
				panelSharedData->GetObjectHandler(), pimpl->m_editorScene->m_physicsEditorOptions->m_markVerticesHandlerOptions );

			pimpl->m_meshResource = physicsMeshData->GetMesh();
			pimpl->m_meshAppearance = physicsMeshData->GetAppearance();
			mesh::DataView dataView( *pimpl->m_meshResource );
			const auto& boneNames = dataView.GetBoneNames();
			for( CName name : boneNames )
			{
				pimpl->m_state.m_boneNamesDictionary.PushBack( name.AsChar() );
			}
			if( boneNames.Size() > 0 )
				pimpl->m_state.m_defaultBoneIndex = 0;

			pimpl->LoadFromMeshParam();

			pimpl->m_onStateChangedNotifier.Notify( pimpl->m_state );
		}

		RecreateRenderingProxy();

		for( auto* panel : GetSharedData()->GetPreviewPanels() )
		{
			panel->GetCamera()->ResetDefaultMappings();
			panel->GetCamera()->OrbitLockRotateDuringZoom( false );

			panel->AttachInputListener( pimpl->m_inputListener.Get() );
			pimpl->m_inputListener->RegisterShortcuts( *panel->GetShortcutManager(), static_cast<MeshPreviewPanel*>(panel) );
		}

        return TBaseClass::OnActivate(currentMode);
    }

    void MeshEditModeShadowMesh::OnDeactivate()
    {
		pimpl->m_editorScene->SetVisible( true );

		MeshPanelSharedData* meshPanelSharedData = GetSharedData();
		if( meshPanelSharedData )
		{
			auto& panels = meshPanelSharedData->GetPreviewPanels();
			for( auto* panel : panels )
			{
				panel->DetachInputListener( pimpl->m_inputListener.Get() );
			}
		}

		pimpl->SaveToMeshParameter();

        TBaseClass::OnDeactivate();
    }

	void MeshEditModeShadowMesh::OnRender( CRenderFrameInfo& frame, CRenderCameraFrameInfo& cameraInfo )
	{
		rend::FragmentBuilder builder( cameraInfo.GetDebugDrawer() );
		builder.EnableTransparency( true );
		builder.EnableOverlay( pimpl->m_state.m_enableOverlay );
		builder.BindTransform( Matrix::IDENTITY() );

		for( Uint32 i : pimpl->m_geometries.Indices() )
		{
			const auto& collider = pimpl->m_geometries[ i ];
			builder.SetColor( i == (Uint32)pimpl->m_state.m_selectedGeometryIndex ? Color::GREEN( 128 ) : Color::DARK_BLUE( 128) );
			builder.BindTransform( collider->m_localToBody.ToInvMatrix() );
			collider->OnRenderDebug( builder, physics::DRAW_VOLUME | physics::DRAW_OUTLINES | physics::CUSTOM_COLOR );
		}
        TBaseClass::OnRender(frame, cameraInfo);
    }

	void MeshEditModeShadowMesh::AddConvexFromSelection()
	{
		struct Action_AddConvexFromSelection : public tools::IEditAction
		{
			RED_BASE_CLASS( tools::IEditAction );

			Action_AddConvexFromSelection( MeshEditModeShadowMesh::impl* pimpl)
				: m_pimpl( pimpl )
			{}

			String GetDescription() const
			{
				return "Add convex from selection";
			}

			Bool Do()
			{
				// if the collider exists it means we are redoing the action
				if( !m_collider )
				{
					red::DynArray< Vector3 > selectedVertices = m_pimpl->m_inputListener->GetSelectedVertices();

					// if no vertices are selected, take all mesh vertices
					if( selectedVertices.Empty() )
					{
						tools::ExtractVerticesFromMesh( selectedVertices, *m_pimpl->m_meshResource );
					}

					RED_ASSERT( !selectedVertices.Empty(), "MeshEditModeShadowMesh::AddConvexFromSelection: Mesh has no vertices (?!) or a bug in input listener - verify" );
			
					Box box;
					box.Clear();
					for( auto v : selectedVertices )
						box.AddPoint( v );
					const auto center = box.CalcCenter();

					m_collider = physics::ColliderConvex::Create( selectedVertices.TypedData(), selectedVertices.Size(), m_pimpl->m_state.m_maxVertices, true );
					if( !m_collider )
					{
						return false;
					}
					m_collider->m_localToBody = Transform( center );
				}
				m_pimpl->m_geometries.PushBack( m_collider );
				m_pimpl->m_state.m_bonesPerGeometry.PushBack( m_pimpl->m_state.m_defaultBoneIndex );
				++m_pimpl->m_state.m_numGeometries;
				m_pimpl->SaveToMeshParameter();
				m_pimpl->m_onStateChangedNotifier.Notify( m_pimpl->m_state );
				return true;
			}

			Bool Undo()
			{
				m_pimpl->m_geometries.Remove( m_collider );
				m_pimpl->m_state.m_bonesPerGeometry.PopBack();
				--m_pimpl->m_state.m_numGeometries;
				m_pimpl->SaveToMeshParameter();
				m_pimpl->m_onStateChangedNotifier.Notify( m_pimpl->m_state );
				return true;
			}

			virtual void GetAffectedResources( red::Set< res::ResourcePath >& outResourcePaths ) const override final
			{
			}

			MeshEditModeShadowMesh::impl* m_pimpl = nullptr;
			THandle < physics::ICollider > m_collider = nullptr;
		};

		GetSharedData()->GetActionHistory().Execute( red::CreateUniquePtr<Action_AddConvexFromSelection>( pimpl.Get()) );
	}

	void MeshEditModeShadowMesh::DeleteSelectedConvex()
	{
		struct Action_DeleteSelectedConvex : public tools::IEditAction
		{
			RED_BASE_CLASS( tools::IEditAction );

			Action_DeleteSelectedConvex( MeshEditModeShadowMesh::impl* pimpl )
				: m_pimpl( pimpl )
			{}

			String GetDescription() const
			{
				return "Delete selected convex";
			}

			Bool Do()
			{
				if( m_pimpl->m_state.m_selectedGeometryIndex == -1 )
					return false;

				m_index = m_pimpl->m_state.m_selectedGeometryIndex;

				m_collider = m_pimpl->m_geometries[ m_index ];
				m_boneIndex = m_pimpl->m_state.m_bonesPerGeometry[ m_index ];

				m_pimpl->m_geometries.RemoveAt( m_index );
				m_pimpl->m_state.m_bonesPerGeometry.RemoveAt( m_index );
				m_pimpl->m_state.m_selectedGeometryIndex = -1;
				--m_pimpl->m_state.m_numGeometries;
				m_pimpl->SaveToMeshParameter();
				m_pimpl->m_onStateChangedNotifier.Notify( m_pimpl->m_state );
				return true;
			}

			Bool Undo()
			{
				m_pimpl->m_geometries.InsertAt( m_index, m_collider );
				m_pimpl->m_state.m_bonesPerGeometry.InsertAt( m_index, m_boneIndex );
				m_pimpl->m_state.m_selectedGeometryIndex = m_index;
				++m_pimpl->m_state.m_numGeometries;
				m_pimpl->SaveToMeshParameter();
				m_pimpl->m_onStateChangedNotifier.Notify( m_pimpl->m_state );
				return true;
			}

			virtual void GetAffectedResources( red::Set< res::ResourcePath >& outResourcePaths ) const override final
			{
			}

			MeshEditModeShadowMesh::impl* m_pimpl = nullptr;
			THandle < physics::ICollider > m_collider = nullptr;
			Int32 m_boneIndex = -1;
			Int32 m_index = -1;
		};

		GetSharedData()->GetActionHistory().Execute( red::CreateUniquePtr<Action_DeleteSelectedConvex>( pimpl.Get() ) );
	}

	void MeshEditModeShadowMesh::RecreateRenderingProxy()
	{
		world::RuntimeSystemRendering& system = pimpl->m_runtimeScene->GetSystemRef<world::RuntimeSystemRendering>();
		pimpl->m_renderProxy = nullptr;

		auto appearance = pimpl->m_meshResource->GetAppearanceSafe( pimpl->m_meshAppearance );

		RenderProxyMeshComponentInitData meshInfo;
		meshInfo.m_isCharacterShadowFallback = false;
		meshInfo.m_hasMeshResource = true;

		meshInfo.m_meshCanMergeIntoGlobalShadowMesh = false;
		meshInfo.m_renderMesh = pimpl->m_meshResource->GetRenderResource();
		meshInfo.m_materialSetup = appearance->GetRenderMaterialSetup();
		meshInfo.m_setupsNum = 1;

		meshInfo.m_componentFriendlyName = "<MeshEditModeShadowMesh>";
		meshInfo.m_forcedLODLevel = 0;
		meshInfo.m_isCastingGlobalShadows = pimpl->m_meshResource->GetCastGlobalShadows();
		meshInfo.m_isCastingLocalShadows = pimpl->m_meshResource->GetCastLocalShadows();
		meshInfo.m_isTwoSided = false;
		meshInfo.m_isCastingShadowsEvenIfNotVisible = false;

		// drawable component
		meshInfo.m_isVisible = true;
		meshInfo.m_isDynamicGeometryComponent = true;
		meshInfo.m_isForceNoAutohide = true;
		meshInfo.m_autoHideDistanceNear = 0.0f;
		meshInfo.m_autoHideDistanceFar = pimpl->m_meshResource->GetAutoHideDistance();
		meshInfo.m_renderingPlane = RPl_Scene;
		meshInfo.m_objectTypeID = pimpl->m_meshResource->GetRenderObjectType();

		// base
		meshInfo.m_type = RPT_Mesh;
		meshInfo.m_boundingBox = pimpl->m_meshResource->GetBoundingBox();

		RenderProxyInitInfo initInfo;
		initInfo.m_componentData = &meshInfo;

		pimpl->m_renderProxy = system.CreateAndRegisterRenderProxy( initInfo, true );
	}

	void MeshEditModeShadowMesh::SaveToMesh()
	{
		static CName materialName = RED_NAME_CONSTEXPR( "metal_base" );

		// Get file path for output mesh
		const res::ResourcePath& filePath = pimpl->m_meshResource->GetPath();
		red::StringView filePathSV = filePath.ToStringView();
		red::String extension = red::paths::GetExtension( filePathSV ).ToString();
		filePathSV.TrimBack( extension.Length() + 1 );
		String outputPathString = filePathSV.ToString() + "_shadowmesh." + extension;
		const res::ResourcePath& outputPath = res::ResourcePath::Build( outputPathString );

		Bool isSkinned = pimpl->m_state.m_boneNamesDictionary.Size() > 0;
		THandle<CMesh> shadowMesh = CreateHandle<CMesh>();
		mesh::DataBuilder builder;

		// prepare geometry data
		red::DynArray< Vector3 > vertices{ red::PoolDoubleBufferedFrame() };
		red::DynArray< Uint16 > indices{ red::PoolDoubleBufferedFrame() };
		red::DynArray< Uint8 > skinningIndices{ red::PoolDoubleBufferedFrame() };
		red::DynArray< Float > skinningWeights{ red::PoolDoubleBufferedFrame() };
		red::DynArray<Int32> outputBones{ red::PoolDoubleBufferedFrame() };

		Uint32 vertexCount = 0;
		Uint32 indexCount = 0;
		Uint16 vertexOffset = 0;

		for( Uint32 collIndex : pimpl->m_geometries.Indices() )
		{
			const THandle<physics::ICollider>& collider = pimpl->m_geometries[ collIndex ];
			physics::ExtractedCollisionGeometry collGeom;
			collider->ExtractGeometry( collider->m_localToBody.ToInvMatrix(), collGeom );
			Uint32 numVerticesForGeom = collGeom.m_vertices.Size();
			vertexCount += numVerticesForGeom;
			indexCount += collGeom.m_indices.Size();
			vertices.Reserve( vertexCount );
			indices.Reserve( indexCount );

			vertices.PushBack( collGeom.m_vertices );
			Uint32 numFaces = collGeom.m_indices.Size() / 3;
			for( Uint32 idx = 0; idx < numFaces; ++idx )
			{
				const Uint32 i = collGeom.m_indices[ 3 * idx + 2];
				const Uint32 j = collGeom.m_indices[ 3 * idx + 1];
				const Uint32 k = collGeom.m_indices[ 3 * idx + 0];

				indices.PushBack( (Uint16)i + vertexOffset );
				indices.PushBack( (Uint16)j + vertexOffset );
				indices.PushBack( (Uint16)k + vertexOffset );

			}
			vertexOffset += numVerticesForGeom;

			if( isSkinned )
			{
				Uint32 currSize = skinningIndices.Size();
				Uint32 newSize = currSize + 4 * numVerticesForGeom;
				skinningIndices.Resize( newSize );
				skinningWeights.Resize( newSize );
				Int32 boneIndex = pimpl->m_state.m_bonesPerGeometry[ collIndex ];
				Int32 boneIndexInOutputArray = outputBones.GetIndex( boneIndex );
				if( boneIndexInOutputArray == -1 )
				{
					boneIndexInOutputArray = outputBones.Size();
					outputBones.PushBack( boneIndex );
				}
				Uint32 startIndex = currSize / 4;
				Uint32 endIndex = startIndex + numVerticesForGeom;

				for( ; startIndex < endIndex; ++startIndex )
				{
					Uint32 indexInArray = 4 * startIndex;
					skinningIndices[ indexInArray + 0 ] = boneIndexInOutputArray;
					skinningIndices[ indexInArray + 1 ] = 0;
					skinningIndices[ indexInArray + 2 ] = 0;
					skinningIndices[ indexInArray + 3 ] = 0;
					skinningWeights[ indexInArray + 0 ] = 1.0f;
					skinningWeights[ indexInArray + 1 ] = 0.0f;
					skinningWeights[ indexInArray + 2 ] = 0.0f;
					skinningWeights[ indexInArray + 3 ] = 0.0f;
				}
			}
		}	

		// prepare chunk data
		mesh::DataBuilder::UnpackedChunk chunk;
		chunk.m_lodMask = 1;
		chunk.m_materials.PushBack( materialName );
		chunk.m_vertexType = isSkinned ? MVT_SkinnedMesh : MVT_StaticMesh;
		chunk.m_flags.m_renderInShadows = true;
		chunk.m_flags.m_renderInScene = false;

		Uint32 streamNumber = isSkinned ? 4 : 2;
		chunk.m_streams.Resize( streamNumber );
		red::DynArray<EMeshStreamType> streamTypes{ red::PoolDoubleBufferedFrame() };
		streamTypes.Resize( streamNumber );
		Uint32 streamIndex = 0;
		streamTypes[ streamIndex++ ] = MST_Position_3F;
		if( isSkinned )
		{
			streamTypes[ streamIndex++ ] = MST_SkinningIndices_4U8;
			streamTypes[ streamIndex++ ] = MST_SkinningWeights_4F;
		}
		streamTypes[ streamIndex++ ] = MST_Index_U16;

		for( streamIndex = 0; streamIndex < streamNumber; ++streamIndex )
		{
			auto& stream = chunk.m_streams[ streamIndex ];
			stream.m_type = streamTypes[ streamIndex ];
			Uint32 stride = MeshPackerUtils::GetMeshStreamSize( stream.m_type );
			if( stream.m_type == MST_Index_U16 )
			{
				stream.m_data = DataBuffer( stride * indexCount );
			}
			else if( stream.m_type == MST_SkinningIndices_4U8 || stream.m_type == MST_SkinningWeights_4F )
			{
				stream.m_data = DataBuffer( stride * vertexCount );
			}
			else
			{
				stream.m_data = DataBuffer( stride * vertexCount );
			}
		}

		auto copyStream = []( void* sourceData, Uint32 size, mesh::UnpackedStream& destinationStream )
		{
			void* destinationData = static_cast<Uint8 *>(destinationStream.m_data.Data());
			red::Memcpy( destinationData, sourceData, size );
		};

		streamIndex = 0;
		copyStream( vertices.TypedData(), vertices.DataSize(), chunk.m_streams[ streamIndex++ ] );
		if( isSkinned )
		{
			copyStream( skinningIndices.TypedData(), skinningIndices.DataSize(), chunk.m_streams[ streamIndex++ ] );
			copyStream( skinningWeights.TypedData(), skinningWeights.DataSize(), chunk.m_streams[ streamIndex++ ] );
		}
		copyStream( indices.TypedData(), indices.DataSize(), chunk.m_streams[ streamIndex++ ] );

		builder.m_chunks.PushBack( std::move(chunk) );

		mesh::DataBuilder::UnpackedLOD lod;
		lod.m_distance = 0;
		builder.m_lods.PushBack( lod );

		if( isSkinned )
		{
			const mesh::DataViewRO view( *pimpl->m_meshResource );

			for( Int32 boneIndex : outputBones )
			{
				mesh::DataBuilder::UnpackedBone newBone;
				newBone.m_rig = view.GetBoneMatrix( boneIndex );
				newBone.m_name = view.GetBoneName( boneIndex );
				builder.m_bones.PushBack( newBone );
			}
		}

		mesh::DataView view( *shadowMesh );		
		res::ResourcePath materialPath = res::ResourcePath::Build( pimpl->m_state.m_materialPath.Empty() ? "engine\\materials\\metal_base.remt" : pimpl->m_state.m_materialPath );
		auto token = GResourceLoader->IssueLoadingRequest( materialPath );

		token->WaitUntilLoaded();
		if( token->GetResource() )
		{
			view.AddMaterial( Cast< CMaterialTemplate >( token->GetResource() ), materialName );
		}

		view.Apply();

		mesh::DataBuilder::FlushOptions flushOptions;
		flushOptions.m_rebuildMaterials = false;
		builder.Flush( *shadowMesh, flushOptions );

		res::ResourceSavingOptions options;
		options.m_targetPath = outputPath;
		res::SaveResource( shadowMesh, options );
	}

	void MeshEditModeShadowMesh::OnSetupInterop( ::interop::utils::Object& op )
	{
		op.RegisterFunctor<void>( "addConvexFromSelection", [ this ]( ::interop::IContext& )
		{
			AddConvexFromSelection();
		} );

		op.RegisterFunctor<void>( "deleteSelectedConvex", [ this ]( ::interop::IContext& )
		{
			DeleteSelectedConvex();
		} );

		op.RegisterFunctor<void>( "saveToMesh", [ this ]( ::interop::IContext& )
		{
			SaveToMesh();
		} );

		op.RegisterValue<tools::ShadowMeshState>( "shadowMeshState",
			[ this ]( interop::IContext& ctx, tools::ShadowMeshState& out ) { out = pimpl->m_state; },
			[ this ]( interop::IContext& ctx, const tools::ShadowMeshState& in ) { pimpl->Internal_SetShadowMeshState( in, GetSharedData()->GetActionHistory() ); },
		&pimpl->m_onStateChangedNotifier );

	}
}