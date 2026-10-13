/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "backendMeshPreviewEditMode_Destruction.h"
#include "backendMeshPreviewPanelInputListener.h"
#include "backendMeshPreviewPanel.h"
#include "meshDataView.h"
#include "meshDataBuilder.h"
#include "meshEditModes.h"
#include "meshPhysicsUtils.h"
#include "physicsObjectResolver.h"
#include "physicsEditorScene.h"
#include "physicsBackendHelper.h"
#include "physicsMesh.h"

#include "../../backendEngine/include/editAction.h"
#include "../../backendEngine/include/editActionHistory.h"
#include "../../backendEngine/include/renderingWindowCamera.h"
#include "../../backendEngine/include/objectPersistentCollection.h"
#include "../../backendEngine/include/objectHandler.h"
#include "../../backendEngine/include/editableProxy.h"

#include "../../../common/engine/include/systemsUpdater.h"
#include "../../../common/generalEditorBackend/include/meshEditorInterop.h"
#include "../../../common/physics/include/physicsSystemResource.h"
#include "../../../common/physics/include/physicsSystemBody.h"

#include "../../../common/redContainers/include/string/stringView.h"
#include "../../../common/redContainers/include/queue.h"

#include "../../../common/redMemory/include/uniquePtr.h"
#include "../../../common/resource/include/resourceSaver.h"
#include "../../../common/redReflection/include/serializable.h"

#include "../../../common/renderDraw/include/fragmentBuilder.h"
#include "../../../common/renderData/include/renderProxyInitData.h"
#include "../../../common/renderData/include/renderProxy.h"
#include "../../../common/renderData/include/renderSkinningData.h"
#include "../../../common/renderData/include/renderer.h"
#include "../../../common/renderData/include/renderSelectionResult.h"
#include "../../../common/renderData/include/renderSelectable.h"

#include "../../../common/resourceMaterial/include/materialTemplate.h"
#include "../../../common/resourceMesh/include/meshUtils.h"
#include "../../../common/resourceMesh/include/meshAppearance.h"
#include "../../../common/resourceMesh/include/meshPacker.h"
#include "../../../common/resourceMesh/include/meshDataViewRO.h"
#include "../../../common/resourceMesh/include/meshParamDestructionStepsData.h"

#include "../../../common/world/include/runtimeSystemRendering.h"
#include "../../../common/world/include/worldRuntimeScene.h"
#include "../../../common/renderData/include/effectContextUpdateData.h"

namespace tools
{
	static CName destructionChunkType = RED_NAME_CONSTEXPR( "DestructionChunk" );
	class DestructionChunk : public IRenderSelectable
	{
	public:
		DestructionChunk()
		{

		}

		virtual tools::EditorObjectIDPath Selectable_CreateEditorId() const override
		{
			return m_path;
		}

		Box m_bounds;
		Matrix m_transform;
		Transform m_bodyToBone;
		Transform m_originalBodyToModel;
		Uint32 m_boneIndex = 0;
		Uint32 m_destructionLevel = 0;
		Int32 m_parentIndex = -1;
		physics::DestructionHierarchyOffset m_children = 0;
		tools::EditorObjectIDPath m_path;
		Bool m_isHovered = false;
	};

	//////////////////////////////////////////////////////////////////////////

	class DestructionPreviewInputListener : public IPreviewInputListener
	{
	public:
		DestructionPreviewInputListener( std::function<void( Int32 )> selectIndexFunc );
		virtual ~DestructionPreviewInputListener();

		virtual Bool OnMouseUp( PreviewPanel& panel, const InputMouseButton& evt ) override;

		red::ArraySpan<DestructionChunk> m_destructionChunksData = {};
		std::function<void( Int32 )> m_selectIndex;

		virtual Bool OnMouseMoved( PreviewPanel& panel, const InputMouseMove& evt ) override;

	};

	DestructionPreviewInputListener::DestructionPreviewInputListener( std::function<void( Int32 )> selectIndexFunc )
		: m_selectIndex( selectIndexFunc )
	{}

	DestructionPreviewInputListener::~DestructionPreviewInputListener()
	{}

	Bool DestructionPreviewInputListener::OnMouseUp( PreviewPanel& panel, const InputMouseButton& evt )
	{
		if( evt.m_button == EMouseButton::LMB )
		{
			Int32 selectedIndex = -1;
			auto selectionMap = red::CreateSharedPtr<rend::SelectionResult>( red::PoolBackend() );
			Rect selectionRect = Rect::Make( evt.m_x, evt.m_y, 1, 1 );
			panel.RenderSelectionMap( selectionRect, selectionMap );

			red::DynArray< rend::SelectedObjectInfo > uniqueSelectedObjects = selectionMap->GetUniqueSelectedObjects( &selectionRect );
			for( const auto& objectInfo : uniqueSelectedObjects )
			{
				auto editorPath = IRenderSelectable::ResolveSelectionID( objectInfo.GetId(), false );
				for( Uint32 i : m_destructionChunksData.Indices() )
				{
					const auto& chunk = m_destructionChunksData[ i ];
					if( chunk.m_path == editorPath )
					{
						selectedIndex = i;
						break;
					}
				}
			}
			m_selectIndex( selectedIndex );
		}

		return false;
	}

	Bool DestructionPreviewInputListener::OnMouseMoved( PreviewPanel& panel, const InputMouseMove& evt )
	{
		auto selectionMap = red::CreateSharedPtr<rend::SelectionResult>( red::PoolBackend() );
		Rect selectionRect = Rect::Make( evt.m_x, evt.m_y, 1, 1 );
		panel.RenderSelectionMap( selectionRect, selectionMap );

		red::DynArray< rend::SelectedObjectInfo > uniqueSelectedObjects = selectionMap->GetUniqueSelectedObjects( &selectionRect );
		for( Uint32 i : m_destructionChunksData.Indices() )
		{
			auto& chunk = m_destructionChunksData[ i ];
			chunk.m_isHovered = false;
		}
		for( const auto& objectInfo : uniqueSelectedObjects )
		{
			auto editorPath = IRenderSelectable::ResolveSelectionID( objectInfo.GetId(), false );
			for( Uint32 i : m_destructionChunksData.Indices() )
			{
				auto& chunk = m_destructionChunksData[ i ];
				if( chunk.m_path == editorPath )
				{
					chunk.m_isHovered = true;
					break;
				}
			}
		}
		return false;
	}

	//////////////////////////////////////////////////////////////////////////


    struct MeshEditModeDestructionMesh::impl
    {
        RED_USE_MEMORY_POOL(red::PoolBackend);

		impl();

		physics::PhysicsEditorScene* m_editorScene;

		red::DynArray<String> m_errors{ red::PoolBackend() };
		red::DynArray< DestructionChunk > m_destructionChunksData{ red::PoolBackend() }; //!< mapping for fractured bodies
		red::BitSet64< 256 > m_visibility;
		RenderProxyPtr m_renderProxy = nullptr;
		THandle<CMesh> m_meshResource = nullptr;
		TRenderPtr<IRenderSkinningData> m_skinningData;

		red::UniquePtr<SystemsUpdater> m_processor = nullptr;
		red::UniquePtr<world::RuntimeScene> m_runtimeScene = nullptr;
		red::UniquePtr< DestructionPreviewInputListener > m_destructionSelectionHelper = nullptr;
		interop::InteropNotifier m_onStateChangedNotifier;
		Box m_bounds;
		CName m_meshAppearance = CName::NONE();
		Int32 m_forcedLOD = 0;
		DestructionDebugDataState m_state;

		void LoadFromMeshParam();
		void InitializeDestructionChunkData();
		void UpdateProxyFromChunks();
		void RecreateRenderingProxy();
		void SpreadDestructionLevels();
		void Render( rend::FragmentBuilder& builder );
		void RenderDestructionBonds( rend::FragmentBuilder& builder );
		void UpdateVisibilityFromSelection( );

		void Internal_SetDebugDataState( const tools::DestructionDebugDataState& state, tools::IEditActionHistory& actionHistory );
    };

	MeshEditModeDestructionMesh::impl::impl()
	{
		m_processor = red::CreateUniquePtr<SystemsUpdater>();
	}

	void MeshEditModeDestructionMesh::impl::LoadFromMeshParam()
	{
		InitializeDestructionChunkData();
	}

	void MeshEditModeDestructionMesh::impl::InitializeDestructionChunkData()
	{
		THandle<physics::SystemResource> data = mesh::utils::GetActivePhysics( *m_meshResource.Get() );
		if( data && data->GetNumObjects() > 0 )
		{
			const mesh::DataViewRO& meshDataViewRO = mesh::DataViewRO( *m_meshResource.Get() );
			auto numBones = meshDataViewRO.GetNumBones();
			const auto& bones = meshDataViewRO.GetBoneNames();
			m_destructionChunksData.Reserve( data->GetBodies().Size() );
			for( auto& obj : data->GetBodies() )
			{
				const auto body = Cast<physics::SystemBody>( obj );
				if( !body->m_mappedBoneName )
				{
					continue;
				}

				// find the bone index
				Int16 boneIndex = bones.GetIndex( body->m_mappedBoneName );
				if( red::INVALID_INDEX == boneIndex )
				{
					continue;
				}

				// create mapping info
				DestructionChunk& dc = m_destructionChunksData.EmplaceBack();
				dc.m_boneIndex = boneIndex;
				dc.m_originalBodyToModel = body->m_localToModel;
				dc.m_bodyToBone = body->m_mappedBoneToBody;
				dc.m_transform = dc.m_originalBodyToModel.ToMatrix();
				dc.m_path = tools::EditorObjectIDPath( tools::EditorObjectID::Build( m_meshResource->GetPath(), m_destructionChunksData.Size() - 1, destructionChunkType, obj->GetName() ) );

				dc.m_bounds = Box::EMPTY();

				for( auto collider : body->m_collisionShapes )
				{
					dc.m_bounds.AddBox( collider->CalcBoundingBox() );
				}
			}

			const auto& boneNames = meshDataViewRO.GetBoneNames();
			for( CName name : boneNames )
			{
				m_state.m_boneNamesDictionary.PushBack( name.AsChar() );
			}

			auto meshDestroParam = m_meshResource->GetParameter< mesh::MeshParamDestructionStepData>();
			const auto& offsets = meshDestroParam->m_offsets;
			red::Queue< std::pair<Uint32, Int32 > > toProcess{ red::PoolBackend() };
			toProcess.Push( { 0, -1 } );
			Uint32 currentLevel = 0;
			while( !toProcess.Empty() )
			{
				auto current = toProcess;
				toProcess.Clear();
				while( !current.Empty() )
				{
					auto c = current.Pop();
					auto i = c.first;

					physics::DestructionHierarchyOffset offset = offsets[ i ];
					if( offset.m_combined )
					{
						Uint32 start = offset.m_begin - 1;
						do
						{
							++start;
							if( start < m_destructionChunksData.Size() )
							{
								toProcess.Push( { start, i } );
							}
							else
							{
								m_errors.PushBack( String::Printf( "Actor mapped to %hs has children outside of actor's range", m_state.m_boneNamesDictionary[ i ] ) );
								break;
							}
						} while( start != offset.m_end );
					}

					DestructionChunk& destChunk = m_destructionChunksData[ i ];
					destChunk.m_destructionLevel = currentLevel;
					destChunk.m_children = offset;
					destChunk.m_parentIndex = c.second;
				}
				++currentLevel;
			}
			SpreadDestructionLevels();
			m_destructionSelectionHelper->m_destructionChunksData = { m_destructionChunksData };
		}
	}

	void MeshEditModeDestructionMesh::impl::SpreadDestructionLevels()
	{
		Matrix currentOffsetXForm = Matrix::IDENTITY();
		Matrix spread = Matrix::IDENTITY();
		auto bbox = m_meshResource->GetBoundingBox();
		Vector4 center = bbox.CalcCenter();
		Vector4 levelOffset = -bbox.CalcExtents() * m_state.m_levelsSpread;
		levelOffset.Y = 0.f;
		levelOffset.Z = 0.f;

		Uint32 numMappings = m_destructionChunksData.Size();
		for( Uint32 i = 0; i < numMappings; ++i )
		{
			auto& destChunk = m_destructionChunksData[ i ];

			currentOffsetXForm.SetTranslation( levelOffset * (Float)destChunk.m_destructionLevel );
			Vector4 spreadOffset = destChunk.m_destructionLevel ? destChunk.m_originalBodyToModel.GetPosition() - center : Vector4::ZEROS();
			RED_ASSERT( spreadOffset.IsOk() );
			spreadOffset = spreadOffset * m_state.m_actorSpread;
			spread.SetTranslation( spreadOffset );
			destChunk.m_transform = destChunk.m_originalBodyToModel.ToMatrix();
			destChunk.m_transform = destChunk.m_transform * spread;
			destChunk.m_transform = destChunk.m_transform * currentOffsetXForm;
			RED_ASSERT( destChunk.m_transform.IsOk() );
		}
	}

	void MeshEditModeDestructionMesh::impl::RecreateRenderingProxy()
	{
		world::RuntimeSystemRendering& system = m_runtimeScene->GetSystemRef<world::RuntimeSystemRendering>();
		m_renderProxy = nullptr;

		auto appearance = m_meshResource->GetAppearanceSafe( m_meshAppearance );

		RenderProxyMeshComponentInitData meshInfo;
		meshInfo.m_isCharacterShadowFallback = false;
		meshInfo.m_hasMeshResource = true;

		meshInfo.m_meshCanMergeIntoGlobalShadowMesh = false;
		meshInfo.m_renderMesh = m_meshResource->GetRenderResource();
		meshInfo.m_materialSetup = appearance->GetRenderMaterialSetup();
		meshInfo.m_setupsNum = 1;

		meshInfo.m_componentFriendlyName = "<MeshEditModeDestructionMesh>";
		meshInfo.m_forcedLODLevel = m_forcedLOD;
		meshInfo.m_isCastingGlobalShadows = m_meshResource->GetCastGlobalShadows();
		meshInfo.m_isCastingLocalShadows = m_meshResource->GetCastLocalShadows();
		meshInfo.m_isTwoSided = false;
		meshInfo.m_isCastingShadowsEvenIfNotVisible = false;

		// drawable component
		meshInfo.m_isVisible = true;
		meshInfo.m_isDynamicGeometryComponent = true;
		meshInfo.m_isForceNoAutohide = true;
		meshInfo.m_autoHideDistanceNear = 0.0f;
		meshInfo.m_autoHideDistanceFar = m_meshResource->GetAutoHideDistance();
		meshInfo.m_renderingPlane = RPl_Scene;

		// base
		meshInfo.m_type = RPT_Mesh;
		meshInfo.m_boundingBox = m_meshResource->GetBoundingBox();

		RenderProxyInitInfo initInfo;
		initInfo.m_componentData = &meshInfo;

		m_renderProxy = system.CreateAndRegisterRenderProxy( initInfo, true );
		UpdateProxyFromChunks();
	}

	void MeshEditModeDestructionMesh::impl::UpdateProxyFromChunks()
	{
		Uint32 numMappings = m_destructionChunksData.Size();
		Uint32 numBones = m_state.m_boneNamesDictionary.Size();
		if( m_skinningData && m_skinningData->GetMatrixCount() != numBones )
		{
			m_skinningData = nullptr;
		}

		// Recreate skinning data if needed
		if( !m_skinningData )
		{
			m_skinningData = GRender->CreateSkinningBuffer();
			m_skinningData->KeepDataAlive( true );

			red::BitSet64<256> activeBones;
			activeBones.SetAll();

			SMaterialParameterEffectContextUpdate updateInfo;

			constexpr Uint8 c_paramSlotFirst = 3;
			const Uint8 c_paramSlotCount = 2;
			RED_ASSERT( RED_ARRAY_COUNT( updateInfo.m_parameters ) >= c_paramSlotFirst + c_paramSlotCount );
			updateInfo.m_paramMask = ECMP_CustomParam3 | ECMP_CustomParam4;
			red::Memcpy( &updateInfo.m_parameters[ c_paramSlotFirst ], activeBones.Data(), sizeof( Uint64 ) * 4 );
			if( m_renderProxy )
			{
				m_renderProxy->UpdateEffectInfo( updateInfo );
			}
		}

		if( m_skinningData )
		{
			RenderSkinningWrite skinnigData = m_skinningData->GetWriteData( SkinningDataRequest::NoLOD( numBones ) );
			if( skinnigData.IsValid() )
			{
				red::StaticArray< Matrix, 256 > bonePoses;
				bonePoses.Resize( numBones );
				m_bounds = Box::EMPTY();

				for( Uint32 i = 0; i < numBones; ++i )
				{
					bonePoses[ i ] = Matrix::IDENTITY();
					bonePoses[ i ].SetScale33( 0.001f );
				}

				for( Uint32 i = 0; i < numMappings; ++i )
				{
					auto& chunkInfo = m_destructionChunksData[ i ];
					Uint32 boneInd = chunkInfo.m_boneIndex;

					if( boneInd >= numBones )
					{
						RED_ERROR( false, "Bone index out of bounds! fix asset" );
						continue;
					}

					if( m_visibility.Get( i )  )
					{
						bonePoses[ boneInd ] = chunkInfo.m_transform;
						bonePoses[ boneInd ] = chunkInfo.m_originalBodyToModel.ToInvMatrix() * bonePoses[ boneInd ];
						bonePoses[ boneInd ] = chunkInfo.m_bodyToBone.ToMatrix() * bonePoses[ boneInd ];
					}
					RED_ASSERT( bonePoses[ boneInd ].IsOk() );

					// Update bounds
					m_bounds.AddBox( chunkInfo.m_transform.TransformBox( chunkInfo.m_bounds ) );
				}
				for( Uint32 i = 0; i < numBones; ++i )
				{
					const Matrix& actorMat = bonePoses[ i ];
					skinnigData.Set( i, actorMat, 0 );
				}

				m_skinningData->AdvanceWrite();
				if( m_renderProxy )
				{
					m_renderProxy->RelinkAndSkin( rend::RenderProxyTransform::IDENTITY(), m_bounds, m_skinningData, nullptr );
				}
			}
		}
	}

	void MeshEditModeDestructionMesh::impl::Render( rend::FragmentBuilder& builder )
	{
		builder.EnableTransparency( true );
		builder.EnableOverlay( true );

		Color transparentColor = Color::WHITE( 0 );
		for( Uint32 i : m_destructionChunksData.Indices() )
		{
			if( !m_visibility.Get( i ) )
				continue;

			const auto& dc = m_destructionChunksData[ i ];
			builder.BindTransform( dc.m_transform );
			builder.BindSelectable( &dc );
			builder.SetColor( Color::WHITE() );
			if( i == (Uint32)m_state.m_selectedIndex )
			{
				builder.SetColor( Color::LIGHT_BLUE() );
				builder.AddWireBox( dc.m_bounds );
			}
			else if( dc.m_isHovered )
			{
				builder.SetColor( Color::CYAN() );
				builder.AddWireBox( dc.m_bounds );
			}

			if( m_state.m_showBoneNames )
			{
				String boneName = dc.m_boneIndex >= 0 && dc.m_boneIndex < m_state.m_boneNamesDictionary.Size() ? m_state.m_boneNamesDictionary[ dc.m_boneIndex ] : "None";
				builder.AddWorldText( dc.m_transform.GetTranslation(), 0, 0, boneName );
			}

			builder.SetColor( transparentColor );
			builder.AddSolidBox( dc.m_bounds );
		}

		builder.BindTransform( Matrix::IDENTITY() );

		if( m_state.m_visualizeBonds )
		{
			RenderDestructionBonds( builder );
		}

		Uint32 numErrors = m_errors.Size( );
		constexpr Uint32 lineSpacing = 14;
		constexpr Uint32 initialOffset = 3;
		Uint32 offset = lineSpacing;
		builder.SetColor( Color::RED() );
		for( String errorText : m_errors )
		{
			builder.AddFlatText( initialOffset, initialOffset + offset, errorText );
			offset += lineSpacing;
		}
	}

	void MeshEditModeDestructionMesh::impl::RenderDestructionBonds( rend::FragmentBuilder& builder )
	{
		const Color noHealthColor = Color::RED( 128 );
		const Color fullHealthColor = Color::GREEN( 128 );

		auto meshBondsParam = m_meshResource->GetParameter< mesh::MeshParamDestructionBonds>();
		Uint32 numChunks = m_destructionChunksData.Size();
		builder.EnableTransparency( true );
		builder.EnableOverlay( true );
		const auto& bonds = meshBondsParam->GetDestructionBonds();
		for( auto& bond : bonds )
		{
			physics::BondIndexHelper bondHelper( bond.m_bondIndex );
			if( bondHelper.m_a < numChunks && bondHelper.m_b < numChunks )
			{
				if( !m_visibility.Get( bondHelper.m_a ) || !m_visibility.Get( bondHelper.m_b ) )
					continue;

				Float healthValue = ((Float)bond.m_bondHealth) / 256.0f;
				Color color = Color::Lerp( healthValue, noHealthColor, fullHealthColor );
				builder.SetColor( color );
				builder.AddFatLine( m_destructionChunksData[ bondHelper.m_a ].m_transform.GetTranslation(), m_destructionChunksData[ bondHelper.m_b ].m_transform.GetTranslation(), 0.003f );
			}
		}
	}

	void MeshEditModeDestructionMesh::impl::UpdateVisibilityFromSelection( )
	{
		if( m_state.m_selectedIndex == -1 || m_state.m_visualizationType == 0 )
		{
			m_visibility.SetAll();
		}
		else
		{
			m_visibility.ClearAll();
			if( m_state.m_visualizationType == 1 )
			{
				red::DynArray< Uint32 > indexesToSet{ red::PoolBackend() };
				indexesToSet.PushBack( m_state.m_selectedIndex );
				while( !indexesToSet.Empty() )
				{
					Uint32 index = indexesToSet.PopBack();
					const auto& destructionChunk = m_destructionChunksData[ index ];
					m_visibility.Set( index );
					physics::DestructionHierarchyOffset offset = destructionChunk.m_children;
					if( offset.m_combined )
					{
						Uint32 start = offset.m_begin - 1;
						do
						{
							++start;
							indexesToSet.PushBack( start );
						} while( start != offset.m_end );
					}
				}
				Int32 parentIndex = m_destructionChunksData[ m_state.m_selectedIndex ].m_parentIndex;
				while( parentIndex != -1 )
				{
					m_visibility.Set( parentIndex );
					parentIndex = m_destructionChunksData[ parentIndex ].m_parentIndex;
				}
			}
			else if( m_state.m_visualizationType == 2 )
			{
				auto meshBondsParam = m_meshResource->GetParameter< mesh::MeshParamDestructionBonds>();
				Uint32 numChunks = m_destructionChunksData.Size();
				m_visibility.Set( m_state.m_selectedIndex );
				physics::DestructionActorIndex selectedActor = (physics::DestructionActorIndex)m_state.m_selectedIndex;
				const auto& bonds = meshBondsParam->GetDestructionBonds();
				for( auto& bond : bonds )
				{
					physics::BondIndexHelper bondHelper( bond.m_bondIndex );
					if( bondHelper.m_a < numChunks && bondHelper.m_b < numChunks )
					{
						if( bondHelper.m_a == selectedActor || bondHelper.m_b == selectedActor )
						{
							m_visibility.Set( bondHelper.m_a );
							m_visibility.Set( bondHelper.m_b );
						}
					}
				}
			}
		}
	}

	void MeshEditModeDestructionMesh::impl::Internal_SetDebugDataState( const tools::DestructionDebugDataState& state, tools::IEditActionHistory& actionHistory )
	{
		struct Action_SetState : public tools::IEditAction
		{
			RED_BASE_CLASS( tools::IEditAction );

			Action_SetState( MeshEditModeDestructionMesh::impl* pimpl, const tools::DestructionDebugDataState& current, const tools::DestructionDebugDataState& previous )
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
				Bool requiresRenderUpdate = false;
				if( m_current.m_selectedIndex != m_previous.m_selectedIndex
					|| m_current.m_visualizationType != m_previous.m_visualizationType )
				{
					requiresRenderUpdate = true;
					m_pimpl->UpdateVisibilityFromSelection();
				}
				if( m_current.m_levelsSpread != m_previous.m_levelsSpread
					|| m_current.m_actorSpread != m_previous.m_actorSpread )
				{
					m_pimpl->SpreadDestructionLevels();
					requiresRenderUpdate = true;
				}

				if( requiresRenderUpdate )
				{
					m_pimpl->UpdateProxyFromChunks();
				}
				m_pimpl->m_onStateChangedNotifier.Notify( m_current );
				return true;
			}

			Bool Undo()
			{
				m_pimpl->m_state = m_previous;
				m_pimpl->UpdateVisibilityFromSelection();
				m_pimpl->UpdateProxyFromChunks();
				m_pimpl->m_onStateChangedNotifier.Notify( m_previous );
				return true;
			}

			virtual void GetAffectedResources( red::Set< res::ResourcePath >& outResourcePaths ) const override final
			{
			}

			MeshEditModeDestructionMesh::impl* m_pimpl;
			tools::DestructionDebugDataState m_current;
			tools::DestructionDebugDataState m_previous;

			virtual Bool IsModification() const override
			{
				return false;
			}
		};

		actionHistory.Execute( red::CreateUniquePtr<Action_SetState>( this, state, m_state ) );
	}

	//////////////////////////////////////////////////////////////////////////

    MeshEditModeDestructionMesh::MeshEditModeDestructionMesh()
        : MeshPreviewEditMode("DestructionMeshEditMode")
        , pimpl(red::CreateUniquePtr<MeshEditModeDestructionMesh::impl>())
    {}

	MeshEditModeDestructionMesh::~MeshEditModeDestructionMesh()
	{
		if( pimpl )
		{
			pimpl->m_renderProxy = nullptr;
		}
	}

    Color MeshEditModeDestructionMesh::GetDisplayColor() const
    {
        return Color::DARK_ORANGE();
    }

    Bool MeshEditModeDestructionMesh::OnInitialize(const PreviewPanelSharedDataPtr& sharedData)
    {
		auto* genericSharedData = static_cast<GenericPreviewPanelSharedData*>(sharedData.Get());
		world::RuntimeInfo runtimeInfo( world::RuntimeMode::GamePreview );
		world::RuntimeSceneSetupInfo sceneSetup( runtimeInfo );
		sceneSetup.m_existingRenderScene = sharedData->GetRenderScene().Get();
		sceneSetup.m_processor = pimpl->m_processor.Get();

		pimpl->m_runtimeScene = world::CreateBackedScene( sceneSetup );

        return TBaseClass::OnInitialize(sharedData);
    }

    Bool MeshEditModeDestructionMesh::OnActivate( IPreviewEditMode* currentMode )
    {
		MeshPanelSharedData* panelSharedData = GetSharedData();
		if( panelSharedData )
		{
			pimpl->m_editorScene = panelSharedData->GetMeshEditorPhysicsScene();
			pimpl->m_editorScene->SetVisible( false );

			if( !pimpl->m_meshResource )
			{
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

				pimpl->m_meshResource = physicsMeshData->GetMesh();
				pimpl->m_meshAppearance = physicsMeshData->GetAppearance();

				pimpl->m_destructionSelectionHelper = red::CreateUniquePtr<DestructionPreviewInputListener>( [ this, panelSharedData ]( Int32 index )
				{
					DestructionDebugDataState state = pimpl->m_state;
					state.m_selectedIndex = index;
					pimpl->Internal_SetDebugDataState( state, panelSharedData->GetActionHistory() );
				} );

				mesh::DataView dataView( *pimpl->m_meshResource );

				pimpl->LoadFromMeshParam();

				pimpl->m_onStateChangedNotifier.Notify( pimpl->m_state );
			}

			auto& panels = panelSharedData->GetPreviewPanels();
			for( auto* panel : panels )
			{
				panel->AttachInputListener( pimpl->m_destructionSelectionHelper.Get(), false );
			}
		}

		pimpl->m_visibility.SetAll();
		pimpl->RecreateRenderingProxy();

        return TBaseClass::OnActivate(currentMode);
    }

    void MeshEditModeDestructionMesh::OnDeactivate()
    {
		for( auto* panel : GetSharedData()->GetPreviewPanels() )
		{
			panel->DetachInputListener( pimpl->m_destructionSelectionHelper.Get() );
		}
		pimpl->m_editorScene->SetVisible( true );
		pimpl->m_renderProxy = nullptr;
		pimpl->m_skinningData = nullptr;
        TBaseClass::OnDeactivate();
    }

	void MeshEditModeDestructionMesh::OnRender( CRenderFrameInfo& frame, CRenderCameraFrameInfo& cameraInfo )
	{
		rend::FragmentBuilder builder( cameraInfo.GetDebugDrawer() );

		pimpl->Render( builder );
        TBaseClass::OnRender(frame, cameraInfo);
    }

	void MeshEditModeDestructionMesh::OnSetupInterop( ::interop::utils::Object& op )
	{
		op.RegisterValue<tools::DestructionDebugDataState>( "debugState",
			[ this ]( interop::IContext& ctx, tools::DestructionDebugDataState& out ) { out = pimpl->m_state; },
			[ this ]( interop::IContext& ctx, const tools::DestructionDebugDataState& in ) { pimpl->Internal_SetDebugDataState( in, GetSharedData()->GetActionHistory() ); },
		&pimpl->m_onStateChangedNotifier );

	}

	void MeshEditModeDestructionMesh::OnUpdateSharedState( CName appearanceName, Int32 forcedLOD )
	{
		pimpl->m_meshAppearance = appearanceName;
		pimpl->m_forcedLOD = forcedLOD;
		pimpl->RecreateRenderingProxy();
	}
}