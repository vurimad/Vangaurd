/**
* Copyright (c) 2015-2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "backendMeshEditor.h"

#include "../../../common/core/include/message.h"
#include "../../../common/engine/include/physicsDecorationResource.h"
#include "../../../common/generalEditorBackend/include/meshEditorInterop.h"
#include "../../../common/resource/include/resourceSaver.h"
#include "../../../common/redReflection/include/rttiPathParser.h"
#include "../../../common/resourceMaterial/include/material.h"
#include "../../../common/resourceMesh/include/meshAppearance.h"
#include "../../../common/resourceMesh/include/meshPacker.h"
#include "../../../common/resourceMesh/include/meshParamCloth.h"
#include "../../../common/resourceMesh/include/meshParamDestructionStepsData.h"
#include "../../../internal/re_file_format/include/re_archive/reSerializer.h"

#include "../../backendEngine/include/backendEngine.h"
#include "../../backendEngine/include/editActionHistory.h"
#include "../../backendEngine/include/houdiniEngineInterface.h"
#include "../../backendEngine/include/houdiniEngineUtils.h"
#include "../../backendEngine/include/resourceBackend.h"
#include "../../backendEngine/include/treeInterop.h"
#include "../../backendREA/include/reFileHelpers.h"

#include "appearancesListInterop.h"
#include "backendMeshPanelSharedData.h"
#include "backendMeshPreviewPanel.h"
#include "chunkListInterop.h"
#include "materialsListInterop.h"
#include "meshBackendUtils.h"
#include "meshDataBuilder.h"
#include "meshRenderBlobUtils.h"
#include "meshReport.h"
#include "physicsEditorScene.h"
#include "worldMeshBuilder.h"
#include "../../backendEngine/include/resourceMetadata.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( MeshResourceEditor, tools );
	RTTI_PARENT_TYPE( tools::IResourceEditor );
RTTI_END_TYPE();

namespace tools
{
	class Action_RemoveUnusedBones : public IEditAction
	{
	public:
		Action_RemoveUnusedBones( const THandle< CMesh >& mesh, const std::function< void() >& callback = std::function< void() >() )
			: IEditAction()
			, m_mesh( mesh )
			, m_meshView( *mesh )
			, m_callback( callback )
		{
		}

		virtual String GetDescription() const override { return "Remove unused bones"; }

		virtual Bool Do() override final
		{
			m_prevBoneNames = m_meshView.GetBoneNames();
			m_prevBoneRigMatrices = m_meshView.GetBoneRigMatrices();
			m_prevBoneVertexEpsilons = m_meshView.GetBoneVertexEpsilons();
			m_meshView.RemoveUnusedBones( m_boneReindex );
			m_meshView.InvertBoneReindex( m_boneReindex );
			if ( m_callback )
				m_callback();
			return true;
		}

		virtual Bool Undo() override final
		{
			m_meshView.ReindexVertexBoneIndices( m_boneReindex );      // This will invert re-indexing done when removing bones.
			m_meshView.ResetBones( m_prevBoneNames, m_prevBoneRigMatrices, m_prevBoneVertexEpsilons );
			if ( m_callback )
				m_callback();
			return true;
		}

		virtual Bool IsModification() const override final
		{
			return true;
		}

		virtual void GetAffectedResources( red::Set< res::ResourcePath >& outResourcePaths ) const override final
		{
			outResourcePaths.Insert( m_mesh->GetPath() );
		}

	private:
		THandle< CMesh > m_mesh;
		mesh::DataView m_meshView;
		red::DynArray<Uint32> m_boneReindex;
		red::DynArray< CName > m_prevBoneNames;           // Mesh bone names before removing unused ones.
		red::DynArray< Matrix > m_prevBoneRigMatrices;    // Mesh bone rig matrices before removing unused ones.
		red::DynArray< Float > m_prevBoneVertexEpsilons;  // Mesh bone vertex epsilons before removing unused ones.
		std::function< void() > m_callback;           // Called to notify about the finished operation. Mesh editor can refresh views etc.
	};

	class Action_SetLodDistances : public IEditAction
	{
	public:
		Action_SetLodDistances(const THandle< CMesh >& mesh, const std::function< void() >& callback = std::function< void() >())
			: IEditAction()
			, m_mesh(mesh)
			, m_meshView(*mesh)
			, m_callback(callback)
		{
		}

		virtual String GetDescription() const override { return "Set LOD Distances"; }

		virtual Bool Do() override final
		{
			for (Uint32 i = 0; i < m_meshView.GetNumLODs(); ++i)
			{
				m_LODDistances.PushBack(m_meshView.GetLODDistance(i));
			}
			if (m_callback)
				m_callback();
			return true;
		}

		virtual Bool Undo() override final
		{
			if (m_meshView.GetNumLODs() == m_LODDistances.Size())
			{
				for (Uint32 i = 0; i < m_LODDistances.Size(); ++i)
				{
					m_meshView.SetLODDistance(i, m_LODDistances[i]);
				}
			}
			else
				return false;

			if (m_callback)
				m_callback();
			return true;
		}

		virtual Bool IsModification() const override final
		{
			return true;
		}

		virtual void GetAffectedResources(red::Set< res::ResourcePath >& outResourcePaths) const override final
		{
			outResourcePaths.Insert(m_mesh->GetPath());
		}

	private:
		THandle< CMesh > m_mesh;
		mesh::DataView m_meshView;
		red::DynArray< Float > m_LODDistances;
		std::function< void() > m_callback;           // Called to notify about the finished operation. Mesh editor can refresh views etc.
	};

    Bool IsClothEditingEnabled(const THandle<CResource>& res )
    {
        if( auto mesh = Cast<CMesh>(res) )
        {
			auto param = mesh->GetParameter<mesh::MeshParamCloth>();
            return (param && mesh::IsValid_ForMeshEditor(param));
        }

        return false;
    }

	Bool IsDestructionDebugEnabled( const THandle<CResource>& res )
	{
		if( auto mesh = Cast<CMesh>( res ) )
		{
			auto paramBonds = mesh->GetParameter<mesh::MeshParamDestructionBonds>();
			auto paramHierarchy = mesh->GetParameter<mesh::MeshParamDestructionStepData>();
			return paramBonds && paramHierarchy;
		}

		return false;
	}

	//////////////////////////////////////////////////////////////////////////////////////////////

	MeshResourceEditor::MeshResourceEditor() = default;
	MeshResourceEditor::~MeshResourceEditor() = default;

	const THandle< CMesh > MeshResourceEditor::GetMesh() const
	{
		return m_resource;
	}

	tools::MeshPanelSharedData* MeshResourceEditor::GetMeshPanelSharedData() const
	{
		return ( tools::MeshPanelSharedData* )m_previewSharedData.Get();
	}

	Bool MeshResourceEditor::Initialize( const THandle< CResource >& resource )
	{
		// get the mesh to edit
		m_resource = Cast<CMesh>(resource);
		if ( !m_resource )
			return false;

		m_serializableNotifier.SetCallback([this]( const SerializableID& id, const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& targetValue ){ OnNotifyPropertyChange( id, path ); });

		InitializePreviewPanelSharedData();

		// the mesh structure
		auto meshChangedCallback = [this] { OnMeshChanged(); };
		m_structureTree = red::CreateSharedPtr<StructureTree>( this, GetSharedSelection(), meshChangedCallback );
		m_structureTree->CreateRoot();
		m_structureTreeInterop = red::CreateSharedPtr< TreeContainerInterop >( m_structureTree.Get(), GetSharedSelection() );

		auto appearancesChangedCallback = [this]{ OnAppearancesChanged(); };
		auto appearanceActivatedCallback = [this]( CName appearanceName ) { OnSelectedAppearanceChanged( appearanceName ); };

		// chunk list
		auto chunkChangedCallback = [this]{ OnMeshChanged(); };
		m_chunksList = red::CreateSharedPtr< ChunksListInterop >( GetAccessHistory(), GetSharedSelection(), m_resource, chunkChangedCallback, appearancesChangedCallback, appearanceActivatedCallback );

		// material list
		auto materialChangedCallback = [this]( Bool vis ) { OnMaterialsChanged( vis ); };
		m_materialsList = red::CreateSharedPtr< MaterialsListInterop >( GetAccessHistory(), GetSharedSelection(), m_resource, materialChangedCallback );

		// appearances list
		m_apperancesList = red::CreateSharedPtr< AppearancesListInterop >( GetAccessHistory(), GetSharedSelection(), m_resource, appearancesChangedCallback, appearanceActivatedCallback );

		m_report = red::CreateUniquePtr<MeshReport>( m_resource );

		// resource monitor
		m_resMonitor.Bind( m_resource->GetPath() );
		m_resMonitor.SetCallback( [this]( const res::ResourcePath& path, const CName& message, const res::MonitorUserDataPtr& userData )
		{
			// react to resource reload
			if ( message == RED_NAME_CONSTEXPR_NOREG( "reload" ) )
			{
				THandle<CResource> newResource = res::GetMonitorUserData< THandle<CResource> >( userData );
				OnOpenResourceReloaded( newResource );
			}
		} );

		InitializeDependenciesMonitors();
		return true;
	}

	const Bool MeshResourceEditor::SupportsResourceClass( const rtti::ClassType* resourceClass ) const
	{
		return resourceClass->IsA( ClassID< CMesh >() ) && !resourceClass->IsA( ClassID< CPhysicsDecorationResource >() );
	}

	const THandle< CResource > MeshResourceEditor::GetResource() const
	{
		return m_resource;
	}

	void MeshResourceEditor::OnPreviewPanelRequest(const String& type, tools::RenderingWindowPtr renderingWindowPtr, tools::PreviewPanelPtr& outPanel)
	{
		m_previewPanel = red::CreateSharedPtr<MeshPreviewPanel>( renderingWindowPtr, m_previewSharedData );

		// NOTE: this is very risky, consider moving the visualization handler out of the preview panel itself
		auto* visualizationContainer = static_cast<IMeshVisualizationContainer*>(m_previewPanel.Get());
		m_chunksList->AttachVisualizationContainer(visualizationContainer);
		m_materialsList->AttachVisualizationContainer(visualizationContainer);
		m_structureTree->AttachVisualizationContainer(visualizationContainer);

		outPanel = m_previewPanel;

        // NOTE: If we support multiple preview panels this will break
        m_previewSharedData->ChangeEditMode( GetMeshPanelSharedData()->GetDefaultEditModeName() );
        static_cast<MeshPanelSharedData*>(m_previewSharedData.Get())->RegisterShortcuts(*m_previewPanel->GetShortcutManager(), m_previewPanel->GetContextName());
	}

	void MeshResourceEditor::InitializePreviewPanelSharedData()
	{
		m_previewSharedData = red::CreateSharedPtr<MeshPanelSharedData>( *this );
	}

	Bool MeshResourceEditor::OnOpenResourceReloaded( const THandle<CResource>& newResource )
	{
		m_resource = Cast<CMesh>(newResource);

		if ( !TBaseClass::OnOpenResourceReloaded( newResource ) )
		{
			return false;
		}

		m_structureTree->SetMesh( m_resource );
		m_chunksList->SetMesh( m_resource );
		m_materialsList->SetMesh( m_resource );
		m_apperancesList->SetMesh( m_resource );
		m_report->SetMesh( m_resource );

		RefreshStructure();
		InitializeDependenciesMonitors();

		return true;
	}

	void MeshResourceEditor::InitializeDependenciesMonitors()
	{
		m_dependenciesMonitor.Clear();

		auto& lock = GetMesh()->m_materialEntriesLock;
		RED_SCOPE_SHARED_LOCK(lock);

		for ( const CMesh::MaterialEntry& materialEntry : GetMesh()->m_materialEntries )
		{
			rtti::EditableProperties properties;
			materialEntry.GetMaterial()->GetCustomEditableProperties( properties );

			for ( const rtti::ClassEditablePropertyInfo& prop : properties.Get() )
			{
				res::ResourcePath path;
				const ERTTITypeType propertyType = prop.m_type->GetType();

				if ( propertyType == ERTTITypeType::RT_ResourceReference )
				{
					res::ResourceReference resRef;
					materialEntry.GetMaterial()->ReadParameterRaw( prop.m_name, prop.m_type, &resRef );
					path = resRef.GetPath();

				}
				else if ( propertyType == ERTTITypeType::RT_ResourceAsyncReference )
				{
					res::ResourceAsyncReference resRef;
					materialEntry.GetMaterial()->ReadParameterRaw( prop.m_name, prop.m_type, &resRef );
					path = resRef.GetPath();
				}

				AppendDependencyMonitor( path );
			}
		}
	}

	void MeshResourceEditor::AppendDependencyMonitor( const res::ResourcePath& dependencyPath )
	{
		if ( dependencyPath.IsEmpty() || m_dependenciesMonitor.KeyExist( dependencyPath ) )
		{
			return;
		}

		red::UniquePtr< res::Monitor > monitor = red::CreateUniquePtr< res::Monitor >();
		monitor->Bind( dependencyPath );
		monitor->SetCallback( [this] ( const res::ResourcePath& path, const CName& message, const res::MonitorUserDataPtr& userData )
		{
			if ( message == RED_NAME_CONSTEXPR_NOREG( "reload" ) )
			{
				{
					mesh::DataView view( *GetMesh().Get() );
					view.ForceRecreateMaterialSetup();
					view.ForceRecreateBlob();
				}

				GetMeshPanelSharedData()->PlaceMesh( GetMesh() );
				OnMeshChanged();
			}
		} );
		m_dependenciesMonitor.Insert( dependencyPath, std::move( monitor ) );
	}

	Bool MeshResourceEditor::OnResourcePreSave( const THandle<CResource>& resource )
	{
		if( const THandle<CMesh> mesh = Cast< CMesh >( resource ) )
		{
			auto& lock = GetMesh()->m_materialEntriesLock;
			RED_SCOPE_SHARED_LOCK(lock);

			for( const CMesh::MaterialEntry& mEntry : mesh->m_materialEntries )
			{
				const THandle< IMaterial > material = mEntry.GetMaterial();
				if ( !material->GetMaterialDefinition() )
				{
					tools::Message logMessage( MessageSeverity::Error, std::move( RED_LOCATION( resource->GetPath() ) ) );
					logMessage << RED_NAME_CONSTEXPR( "MaterialInstance: " );
					logMessage << mEntry.GetName().AsChar();
					logMessage << " is missing base material. ";
					IMessageReporter::ReportSafe( logMessage );
					return false;
				}

				red::StringBuilder< String > errorMessageBuilder;
				if ( !material->ValidateParameters( errorMessageBuilder ) )
				{
					tools::Message logMessage( MessageSeverity::Error, std::move(RED_LOCATION( resource->GetPath() ) ) );
					logMessage << RED_NAME_CONSTEXPR( "MaterialInstance: " );
					logMessage << mEntry.GetName().AsChar();
					logMessage << " has following parameter errors: ";
					logMessage << errorMessageBuilder.AsChar();
					IMessageReporter::ReportSafe( logMessage );
					return false;
				}
			}
		}
		return TBaseClass::OnResourcePreSave( resource );
	}

	Bool MeshResourceEditor::Save( red::DynArray< res::ResourcePath >& successfullySaved, red::DynArray< res::ResourcePath >& failedToSave  )
	{
		tools::MeshPanelSharedData* sharedData = GetMeshPanelSharedData();
		sharedData->StopSimulation();
		sharedData->Save();
		if( TBaseClass::Save( successfullySaved, failedToSave ) )
		{
			for( auto& res : successfullySaved )
			{
				res::MonitorRouter::GetInstance().QueueChange(res, RED_NAME_CONSTEXPR("onSave"), nullptr);
			}

			m_reportChanged.Notify();
			return true;
		}

		return false;
	}

	void MeshResourceEditor::OnMaterialsChanged( Bool visual )
	{
		if ( visual )
		{
			if ( m_previewPanel != nullptr )
				m_previewPanel->Refresh();

			SendPreviewMessage( "refresh" );
		}
		else
		{
			m_structureTree->RefreshTree();
		}

		m_chunksList->NotifyMaterialsChange();
		m_materialsList->NotifyThatMaterialChanged();
		m_report->UpdateStats();
		m_reportChanged.Notify();
	}

	void MeshResourceEditor::OnMeshChanged()
	{
		if ( m_previewPanel != nullptr )
			m_previewPanel->Refresh();

		SendPreviewMessage( "refresh" );
		m_report->UpdateStats();
		m_reportChanged.Notify();

		res::MonitorRouter::GetInstance().QueueChange( GetPath(), RED_NAME_CONSTEXPR( "meshChanged" ) );
	}

	void MeshResourceEditor::OnAppearancesChanged()
	{
		Int32 activeAppearanceIndex = m_apperancesList->GetActiveItemIndex();
		if ( activeAppearanceIndex >= 0 )
		{
			String activeAppearanceName = m_apperancesList->GetItemName( activeAppearanceIndex );
			if ( m_previewPanel != nullptr )
			{
				m_previewPanel->SetMeshAppearance( RED_NAME( activeAppearanceName ) );           // In case the active appearance was renamed.
			}
		}

		if ( m_previewPanel != nullptr )
			m_previewPanel->Refresh();

		m_structureTree->RefreshTree();
		m_chunksList->NotifyAppearancesChange();
		m_materialsList->NotifyThatAppearancesChanged();
		m_report->UpdateStats();
		m_reportChanged.Notify();
	}

	void MeshResourceEditor::OnSelectedAppearanceChanged( CName newMeshAppearance )
	{
		if ( m_previewPanel != nullptr)
			m_previewPanel->SetMeshAppearance( newMeshAppearance );
	}

	void MeshResourceEditor::OnBonesChanged()
	{
		m_structureTree->RefreshTree();
		m_report->UpdateStats();
		m_reportChanged.Notify();
		CreateMeshRenderResourceBlob( *GetMesh().Get(), PLATFORM_PC, false );
	}

	void MeshResourceEditor::CalculateGarmentMeshStreams( const String& bodyTypeAbbr )
	{
		res::ResourcePath meshPath = GetMesh()->GetPath();
		ReFileHelpers::ExportGarmentHelper(meshPath, bodyTypeAbbr);																/// @todo DP: insert Houdini service call here
	}

	void MeshResourceEditor::ImportGarmentMeshStreams( const String& garmentPath )
	{
		const float SCALE = 0.01;
		Bool result = false;
		String errMsg;

		do
		{
			THandle<CMesh> outputMesh = GetMesh();
			if (!outputMesh)
			{
				RED_LOG_ERROR( "Unable to get original mesh" );
				break;
			}

			res::ResourceSavingOptions savingOptions;
			savingOptions.m_targetPath = outputMesh->GetPath();
			mesh::DataView meshDataView( *outputMesh );

			std::unique_ptr<ReReadStream> reFile( ReReadStream::Create( garmentPath.AsChar() ) );
			std::vector< ReFileMeshPtr > meshes;

			if (!reFile->Read( meshes ))
			{
				errMsg = String::Printf( "Unable to read garment stream file: %hs", garmentPath.AsChar() );
				RED_LOG_ERROR( errMsg.AsChar() );
				break;
			}

			// we dealing here with .re file so it's supposed to have 1 mesh node inside (at least for garments)
			if (meshes.size() != 1)
			{
				errMsg = String::Printf( "No mesh found, or there are more than just one mesh in garment stream file: %hs", garmentPath.AsChar() );
				RED_LOG_ERROR( errMsg.AsChar() );
				break;
			}

			const ReFileMesh& reMesh = *meshes[0];
			Int32 numChunks = reMesh.getNumChunks();

			for (Int32 c = 0; c < numChunks; c++)
			{

				ReFileMeshChunkPtr reChunk = reMesh.getMeshChunk( c );

				reFloat3* garmentOffsetsData = nullptr;
				Uint32 garmentDataCount = 0;

				int* garmentFlagsData = nullptr;
				Uint32 garmentFlagsDataCount = 0;

				int* fv0Data = nullptr;
				int* fv1Data = nullptr;
				int* fv2Data = nullptr;
				Uint32 fv0iCount = 0;
				Uint32 fv1iCount = 0;
				Uint32 fv2iCount = 0;

				// get data from .re file
				Bool isOk = true;
				isOk &= reChunk->getDataChannel( "re_attrv3morph_target", garmentDataCount, garmentOffsetsData );
				isOk &= reChunk->getDataChannel( "re_attrgarmentFlags", garmentFlagsDataCount, garmentFlagsData );

				if (!isOk)
				{
					RED_LOG( "No Garment Data in chunk: %u, skipping...", c );
					continue;
				}

				isOk &= reChunk->getDataChannel( "FV0I", fv0iCount, fv0Data );
				isOk &= reChunk->getDataChannel( "FV1I", fv1iCount, fv1Data );
				isOk &= reChunk->getDataChannel( "FV2I", fv2iCount, fv2Data );

				Bool faceDataConsistent = fv0iCount == fv1iCount && fv1iCount == fv2iCount && fv2iCount;
				if (!faceDataConsistent || !isOk)
				{
					errMsg = String::Printf( "This mesh is corrupted!" );
					RED_LOG_ERROR( errMsg.AsChar() );
					break;
				}

				// add streams
				meshDataView.AddChunkStream( c, MST_MorphOffset_3F );
				meshDataView.AddChunkStream( c, MST_GarmentFlags_U32 );

				const Uint32 dataLength = meshDataView.GetChunkVertexCount( c );

				red::DynArray<Vector3> morphOffsetsDataArr( dataLength );
				red::DynArray<Uint32> morphFlagsDataArr( dataLength );

				// because we iterate over triangles this array will help to determine if vertex was already written
				red::DynArray<Bool> vertexCheck( dataLength );

				mesh::ChunkStreamDataRO morphOffsetsData = mesh::ChunkStreamDataRO();
				morphOffsetsData.m_stream = MST_MorphOffset_3F;
				morphOffsetsData.m_stride = MeshPackerUtils::GetMeshStreamSize( MST_MorphOffset_3F );

				mesh::ChunkStreamDataRO morphFlagsData = mesh::ChunkStreamDataRO();
				morphFlagsData.m_stream = MST_GarmentFlags_U32;
				morphFlagsData.m_stride = MeshPackerUtils::GetMeshStreamSize( MST_GarmentFlags_U32 );

				// get original mesh indices array
				red::DynArray<Uint16>  indices;
				mesh::ChunkStreamDataRO indicesStream = meshDataView.GetChunkStreamData( c, MST_Index_U16 );
				for (mesh::StreamIteratorRO<Uint16> it( indicesStream ); it; ++it)
				{
					Uint16 i = ( *it );
					indices.PushBack( i );
				}

				//check if input file has the same triangle count as resource mesh
				const Uint16 trisCount = indices.Size() / 3;
				if (trisCount != reChunk->getNumFaces())
				{
					errMsg = String::Printf("Number of triangles in resource mesh and imported mesh doesn't match.");
					RED_LOG_ERROR( errMsg.AsChar() );
					break;
				}

				// sometimes input mesh can have datasets with different length
				// (than original one, it's because adding new data to .re file makes vertices became "unique"),
				// if number of triangles match with resource mesh we can do lookup using indices
				// to get and set proper values
				for (Uint16 i = 0; i < trisCount; ++i)
				{
					if (!vertexCheck[indices[i * 3 + 0]])
					{
						morphOffsetsDataArr[indices[i * 3 + 0]] = Vector3( garmentOffsetsData[fv2Data[i]].x * SCALE, garmentOffsetsData[fv2Data[i]].y * SCALE, garmentOffsetsData[fv2Data[i]].z * SCALE );
						vertexCheck[indices[i * 3 + 0]] = true;
					}
					if (!vertexCheck[indices[i * 3 + 1]])
					{
						morphOffsetsDataArr[indices[i * 3 + 1]] = Vector3( garmentOffsetsData[fv1Data[i]].x * SCALE, garmentOffsetsData[fv1Data[i]].y * SCALE, garmentOffsetsData[fv1Data[i]].z * SCALE );
						vertexCheck[indices[i * 3 + 1]] = true;
					}
					if (!vertexCheck[indices[i * 3 + 2]])
					{
						morphOffsetsDataArr[indices[i * 3 + 2]] = Vector3( garmentOffsetsData[fv0Data[i]].x * SCALE, garmentOffsetsData[fv0Data[i]].y * SCALE, garmentOffsetsData[fv0Data[i]].z * SCALE );
						vertexCheck[indices[i * 3 + 2]] = true;
					}

					morphFlagsDataArr[indices[i * 3 + 0]] = garmentFlagsData[fv2Data[i]];
					morphFlagsDataArr[indices[i * 3 + 1]] = garmentFlagsData[fv1Data[i]];
					morphFlagsDataArr[indices[i * 3 + 2]] = garmentFlagsData[fv0Data[i]];
				}

				morphOffsetsData.m_data = morphOffsetsDataArr.TypedData();
				morphOffsetsData.m_count = dataLength;

				morphFlagsData.m_data = morphFlagsDataArr.TypedData();
				morphFlagsData.m_count = dataLength;

				meshDataView.SetChunkData( c, morphOffsetsData );
				meshDataView.SetChunkData( c, morphFlagsData );

				EMeshVertexType vertexType = meshDataView.GetChunkVertexType( c );
				switch (vertexType)
				{
					case MVT_SkinnedMesh:
						vertexType = MVT_GarmentSkinnedMesh;
						break;

					case MVT_ExtSkinnedMesh:
						vertexType = MVT_ExtGarmentSkinnedMesh;
						break;

					case MVT_SkinnedMeshLightBlocker:
						vertexType = MVT_GarmentSkinnedMeshLightBlocker;
						break;

					case MVT_ExtSkinnedMeshLightBlocker:
						vertexType = MVT_ExtGarmentSkinnedMeshLightBlocker;
						break;

					case MVT_GarmentSkinnedMesh:
					case MVT_ExtGarmentSkinnedMesh:
						//the type is fine, but keep it separate from the warning case
						break;

					default:
						errMsg = String::Printf( "Unable to promote vertex type to garment type. The mesh does not appear to be skinned... (vertex type: %u)", static_cast<Uint32>( vertexType ) );
						RED_REPORT_ERROR( RED_LOCATION( GetPath() ), errMsg.AsChar());
						RED_LOG_ERROR( errMsg.AsChar() );
				}
				meshDataView.SetChunkVertexType( c, vertexType );
			}

			meshDataView.ForceRecreateRenderResource();
			meshDataView.ForceRecreateBlob();
			meshDataView.Apply();

			if (!res::SaveResource( outputMesh, savingOptions ))
			{
				RED_LOG_ERROR( "Could not save the resource!" );
				errMsg = "Could not save the resource!";
				break;
			}
			ResourceBackend& resourceBackend = *GetBackend();
			resourceBackend.ReloadResource( savingOptions.m_targetPath );
			result = true;

		} while (false);

		if (result)
		{
			RED_REPORT_SUCCESS( RED_LOCATION( GetPath() ), "Garment streams imported" );
			RED_LOG( "Garment streams imported" );
		}
		else
		{
			RED_REPORT_ERROR( RED_LOCATION( GetPath() ), "Could not import garment streams! (%)", errMsg.AsChar() );
			RED_LOG_ERROR( "Could not import garment streams!");
		}
	}

	Bool MeshResourceEditor::BakeAO( Float maxDistance, Float spread, Float softness, Uint8 procLods )
	{
		Bool result = false;
		CBackendEngine* backendEngine = Cast<CBackendEngine>( GEngine );
		houdini_legacy::HoudiniEngineSession* session = backendEngine->GetHoudiniSession();
		if (session == nullptr) return false;

		if (!houdini_legacy::IsSessionValid( session ))
		{
			if (!backendEngine->StartHoudiniEngine())
			{
				return false;
			}
			else
			{
				session = backendEngine->GetHoudiniSession();
				if (!houdini_legacy::IsSessionValid( session )) return false;
			}
		}

		const String mesh_path = GetPath().ToString();
		houdini_legacy::GeometryBuffer geometry;
		result = houdini_legacy::BakeAO( session, mesh_path, geometry, maxDistance, spread, softness, procLods );
		if (result)
		{
			red::DynArray < tools::ChunkInfo > houdiniChunks;
			for (auto& lod : geometry.m_lods)
			{
				for (auto& chunk : lod.m_chunks)
				{
					houdiniChunks.PushBack( { chunk.m_start, chunk.m_end, lod.m_lod } );
				}
			}

			THandle<CMesh> mesh = GetMesh();
			if (mesh)
			{
				res::ResourceSavingOptions savingOptions;
				savingOptions.m_targetPath = mesh->GetPath();

				mesh::DataBuilder buildData( *mesh );
				mesh::DataView meshDataView( *mesh );

				//Rewrite the color information
				for (Uint8 lodIdx = 0; lodIdx < meshDataView.GetNumLODs(); lodIdx++)
				{
					for (auto chunkIdx : meshDataView.GetLODChunkIndices( lodIdx ))
					{
						if (chunkIdx >= houdiniChunks.Size())
						{
							continue;
						}
						red::DynArray<Uint16>  indices;
						mesh::ChunkStreamDataRO indicesStream = meshDataView.GetChunkStreamData( chunkIdx, MST_Index_U16 );
						for (mesh::StreamIteratorRO<Uint16> it( indicesStream ); it; ++it)
						{
							Uint16 i = ( *it );
							indices.PushBack( i );
						}

						auto &houdiniChunk = houdiniChunks[chunkIdx];
						// Mismatch in chunk sizes
						if (indices.Size() != 3* (houdiniChunk.m_end - houdiniChunk.m_start))
						{
							continue;
						}

						meshDataView.RemoveChunkStream( chunkIdx, MST_Color_U32 );
						if (meshDataView.AddChunkStream( chunkIdx, MST_Color_U32 ))
						{
							auto &colorStream = meshDataView.GetChunkStreamDataForWriting( chunkIdx, MST_Color_U32 );
							Uint32* raw_data = static_cast<Uint32*>( colorStream.m_data );

							Uint32 chunkIndicesStart = houdiniChunk.m_start * 3;
							Uint32 chunkIndicesEnd = houdiniChunk.m_end * 3;
							auto vertex_indexes_range = std::minmax_element( &geometry.m_indices[chunkIndicesStart], &(geometry.m_indices[chunkIndicesEnd-1])+1 );
							Uint32 firstChunkVertexId = *vertex_indexes_range.first;
							for (auto idx : indices)
							{
								const Color color = Color::FromVectorLinearAccu( geometry.m_vertices[ firstChunkVertexId + idx ].colorLinearSpace.ToVector4() );
								raw_data[ idx ] = color.ToUint32();
							}
						}
					}
				}

				meshDataView.ForceRecreateRenderResource();
				meshDataView.ForceRecreateBlob();
				meshDataView.Apply();

				auto save_res = res::SaveResource( mesh, savingOptions );
				result = save_res;

				ResourceBackend& resourceBackend = *GetBackend();
				resourceBackend.ReloadResource( savingOptions.m_targetPath );
			}
			else
			{
				result = false;
			}
		}
		houdini_legacy::Close( session );
		return result;
	}

	void MeshResourceEditor::CloseHoudiniSesssion()
	{
		houdini_legacy::HoudiniEngineSession* session = Cast<CBackendEngine>(GEngine)->GetHoudiniSession();
		if (houdini_legacy::IsSessionValid(session))
		{
			session->CleanUp();
			houdini_legacy::Close(session);
		}
	}


	Bool MeshResourceEditor::GenerateLODsHE( const mesh::LODGenerationUtils::GenerationInfo& generationInfo, houdini_legacy::HoudiniEngineSession* session, const CName& appearanceName )
	{
		if ( !mesh::LODGenerationUtils::GenerateLODsHE( generationInfo, session, appearanceName ) )
		{
			return false;
		}

		//saving for the sake of refresh - so the user will see the results immmediately
		res::ResourceSavingOptions savingOptions;
		savingOptions.m_targetPath = GetPath();
		res::SaveResource( GetMesh(), savingOptions );

		// reolad resources
		ResourceBackend& resourceBackend = *GetBackend();
		resourceBackend.ReloadResource( GetPath() );

		/// @todo DP: insert Houdini service call here
		return true;
	}

	void MeshResourceEditor::ExportMesh(const String& filePath)
	{
		if (GetMesh() != nullptr)
		{
			res::ResourcePath meshPath = GetMesh()->GetPath();
			ReFileHelpers::ExportMeshEditorHelper(meshPath, filePath);
		}
	}

	Bool MeshResourceEditor::RemoveLOD( Int32 lod )
	{
		THandle< res::ResourceMetadata > metadata = res::ResourceMetadata::Get( GetResource() );
		if ( metadata && metadata->GetTags().Exist( RED_NAME_CONSTEXPR( "lod0Removed" ) ) )
		{
			lod--;
		}

		if ( lod <= 0 )
		{
			RED_REPORT_ERROR( RED_LOCATION( GetPath() ), "Couldn't remove LOD%", lod );
			return false;
		}

		mesh::DataView meshDataView = mesh::DataView( *GetMesh().Get() );
		if ( meshDataView.GetNumLODs() == 1 )
		{
			RED_REPORT_ERROR( RED_LOCATION( GetPath() ), "There is only one LOD in this mesh", );
			return false;
		}

		red::DynArray< Float > distances;
		for ( Uint32 i = 0; i < meshDataView.GetNumLODs(); ++i )
		{
			if ( lod == i && lod != 0 )
			{
				continue;
			}
			distances.PushBack( meshDataView.GetLODDistance( i ) );
		}

		if ( meshDataView.RemoveLOD( lod ) )
		{
			for ( Uint32 i = 0; i < meshDataView.GetNumLODs(); ++i )
			{
				meshDataView.SetLODDistance( i, distances[i] );
			}

			meshDataView.Apply();
			GetPreviewPanels().Front()->OnEditorMessage( "refresh", nullptr );
			m_structureTree->RefreshTree();
			GetResource()->MarkModified();
			GetMeshPanelSharedData()->NotifyLODsChange();

			m_report->UpdateReportInfo();
			m_reportChanged.Notify();
			return true;
		}

		RED_REPORT_ERROR( RED_LOCATION( GetPath() ), "Couldn't remove LOD%", lod );
		return false;
	}

	Bool MeshResourceEditor::RemoveLOD0()
	{
		// check lod0Removed tag first
		THandle< res::ResourceMetadata > metadata = res::ResourceMetadata::Get( GetResource() );
		if ( metadata && metadata->GetTags().Exist( RED_NAME_CONSTEXPR( "lod0Removed" ) ) )
		{
			RED_REPORT_INFO( RED_LOCATION( GetPath() ), "LOD0 was already removed" );
			return false;
		}

		mesh::DataView meshDataView = mesh::DataView( *GetMesh().Get() );
		if ( meshDataView.GetNumLODs() == 1 )
		{
			RED_REPORT_ERROR( RED_LOCATION( GetPath() ), "There is only one LOD in this mesh", );
			return false;
		}

		red::DynArray< Float > distances;
		for ( Uint32 i = 0; i < meshDataView.GetNumLODs(); ++i )
		{
			distances.PushBack( meshDataView.GetLODDistance( i ) );
		}

		if ( !meshDataView.RemoveLODWithData( 0 ) )
		{
			RED_REPORT_ERROR( RED_LOCATION( GetPath() ), "Couldn't remove LOD0" );
			return false;
		}

		for ( Uint32 i = 0; i < meshDataView.GetNumLODs(); ++i )
		{
			meshDataView.SetLODDistance( i, distances[i] );
		}

		meshDataView.Apply();

		// set a tag lod0Removed
		if ( !metadata )
		{
			metadata = CreateHandle< res::ResourceMetadata >();
		}
		res::ResourceMetadata::TFileTags& tags = metadata->GetTags();
		tags.PushBack( RED_NAME_CONSTEXPR( "lod0Removed" ) );
		res::ResourceMetadata::Set( GetResource(), metadata );

		GetPreviewPanels().Front()->OnEditorMessage( "refresh", nullptr );
		m_structureTree->RefreshTree();
		GetResource()->MarkModified();
		GetMeshPanelSharedData()->NotifyLODsChange();
		GetBackend()->ClearMetadata( GetPath() );

		m_report->UpdateReportInfo();
		m_reportChanged.Notify();
		return true;
	}

	void MeshResourceEditor::SetupInterop( MeshResourceEditor* editor, ::interop::utils::Object& op )
	{
		op.RegisterObject( "chunksList", ::interop::ObjectPtr( editor->m_chunksList ) );
		op.RegisterObject( "materialsList", ::interop::ObjectPtr( editor->m_materialsList ) );
		op.RegisterObject( "appearancesList", ::interop::ObjectPtr( editor->m_apperancesList ) );
		op.RegisterObject( "tree", interop::ObjectPtr( editor->m_structureTreeInterop ) );

		op.RegisterValue< Int32 >( "_forcedLOD",
			/*GET*/ [ editor ]( ::interop::IContext& ctx, Int32& outValue )
		{
			IMeshVisualizationContainer* visualizationContainer = static_cast<IMeshVisualizationContainer*>(editor->m_previewPanel.Get());
			if (visualizationContainer)
			{
				outValue = visualizationContainer->GetForcedLOD();
			}
			else
			{
				outValue = -1;
			}
		},
			/*SET*/ [ editor ]( ::interop::IContext& ctx, const Int32& newValue )
		{
			IMeshVisualizationContainer* visualizationContainer = static_cast<IMeshVisualizationContainer*>(editor->m_previewPanel.Get());
			if ( visualizationContainer )
			{
				if( visualizationContainer->SetForcedLOD( newValue ) )
				{
					editor->RefreshChunkList( newValue );
				}
			}
		} );

		op.RegisterFunctor< void, Uint32, Float >( "setLODDistance",
			[editor]( ::interop::IContext& ctx, Uint32 index, Float newValue )
		{
			mesh::DataView meshDataView = mesh::DataView( *editor->GetMesh().Get() );

			if( meshDataView.GetLODDistance( index ) != newValue )
			{
				auto action = red::CreateUniquePtr< Action_SetLodDistances >(editor->GetMesh(), [editor, index, newValue]()
				{
					mesh::DataView& meshDataView = mesh::DataView(*editor->GetMesh().Get());
					meshDataView.SetLODDistance(index, newValue);
					editor->SendPreviewMessage("refresh");
					editor->RefreshStructure();
				});

				Bool result = editor->GetAccessHistory().Execute(std::move(action));
				if (!result)
					ctx.ThrowException("Couldn't change LOD distances!");
			}
		} );

		op.RegisterValueRO<Bool>( "isLod0Removed", [editor] ()
		{
			THandle< res::ResourceMetadata > metadata = res::ResourceMetadata::Get( editor->GetResource() );
			if ( metadata && metadata->GetTags().Exist( RED_NAME_CONSTEXPR( "lod0Removed" ) ) )
			{
				return true;
			}
			return false;
		} );

		op.RegisterFunctor<void>( "removeLod0",
			[editor] ( ::interop::IContext& ctx )
			{
				editor->RemoveLOD0();
			} );

		op.RegisterFunctor<void, Int32>( "removeLod",
			[editor] ( ::interop::IContext& ctx, Int32 lod )
			{
				if ( lod == 0 )
				{
					RED_REPORT_ERROR( RED_LOCATION( editor->GetPath() ), "Cannot remove LOD0" );
					return;
				}
				editor->RemoveLOD( lod );
			} );

		op.RegisterFunctor< red::DynArray< Float > >( "getLODsDistances", [editor]( interop::IContext& ctx )
		{
			red::DynArray< Float > outLODsDistances;
			const mesh::DataViewRO& meshDataViewRO = mesh::DataViewRO( *editor->GetMesh().Get() );
			outLODsDistances.PushBack( meshDataViewRO.GetAutoHideDistance() );

			for( Uint32 i=0; i<meshDataViewRO.GetNumLODs(); ++i )
			{
				outLODsDistances.PushBack( meshDataViewRO.GetLODDistance(i) );
			}

			return outLODsDistances;
		} );

		op.RegisterObject( "structureSelection", interop::ObjectPtr( editor->GetSharedSelection()->GetRootInterop() ) );

		op.RegisterValueRO<String>("report", [editor](){ return editor->m_report->GetReport(); });
		op.RegisterNotifier( "_onReportChanged", editor->m_reportChanged );
		op.RegisterValueRO<MeshEditorReportInfo>( "reportInfo", [ editor ] () { return editor->m_report->GetReportInfo(); } );
		op.RegisterValueRO<String>( "importPath", [editor]() { return editor->m_report->GetImportPath(); } );
        op.RegisterValueRO<Bool>("clothEditingEnabled", [editor]{ return IsClothEditingEnabled(editor->GetResource()); });
		op.RegisterValueRO<Bool>( "destructionDebugEnabled", [ editor ] { return IsDestructionDebugEnabled( editor->GetResource() ); } );

		op.RegisterFunctor<Bool>( "removeUnusedBones", [editor]( interop::IContext& ctx )
		{
			auto action = red::CreateUniquePtr< Action_RemoveUnusedBones >( editor->GetMesh(), [editor]() { editor->OnBonesChanged(); } );
			Bool result = editor->GetAccessHistory().Execute( std::move( action ) );
			if ( !result )
				ctx.ThrowException( "Couldn't remove unused bones." );
			return result;
		} );

		op.RegisterFunctor<String>( "extractBodyTypeFromName", [editor]( interop::IContext& ctx ) { return mesh::ExtractBodyTypeNameFromMeshPath( editor->GetMesh()->GetPath().ToStringView() ); } );
		op.RegisterFunctor<void, Float, Float, Float,Uint8>( "bakeAO", [editor]( interop::IContext& ctx, Float maxDistance, Float spread, Float softness, Uint8 procLods) { editor->BakeAO(maxDistance, spread, softness, procLods ); } );
		op.RegisterValueRO<Bool>( "isHoudiniAvailable", [editor]() { return houdini_legacy::IsHoudiniEngineAvailable(); } );
		op.RegisterFunctor<void>("closeHoudiniSession", [editor](interop::IContext& ctx) { editor->CloseHoudiniSesssion(); });

		op.RegisterFunctor<void, String>( "calculateGarmentMeshStreams", [editor]( interop::IContext& ctx, const String& bodyTypeName ) { editor->CalculateGarmentMeshStreams( bodyTypeName ); } );
		op.RegisterFunctor<void, String>( "importGarmentMeshStreams", [editor]( interop::IContext& ctx, const String& path ) { editor->ImportGarmentMeshStreams( path ); } );

		op.RegisterFunctor< red::DynArray<String> >( "getAllBodyTypeNames", [editor]( interop::IContext& ctx ) { return mesh::GetAllBodyTypeNames(); } );
		op.RegisterFunctor<void, String>("exportMesh", [editor](interop::IContext& ctx, const String& filePath) { editor->ExportMesh(filePath); });

		editor->m_previewSharedData->ExposeInterop( op );
	}

	void MeshResourceEditor::OnNotifyPropertyChange( const SerializableID& id, const rtti::AccessPath& path )
	{
		CName propName;
		rtti::PathParser parser( path );
		parser.EatName( propName );

		const THandle<CMesh> mesh = GetMesh();
		if ( mesh && mesh->GetID() == id )
		{
			if ( propName == RED_NAME_CONSTEXPR_NOREG( "autoHideDistance" ) )
			{
				SendPreviewMessage("refresh");
			}
			else if ( propName == RED_NAME_CONSTEXPR_NOREG( "isGIVisible" ) )
			{
				SetChunksCascadeFlags();
			}
		}
		else if ( m_chunksList->GetSelectedMaterial() && m_chunksList->GetSelectedMaterial()->GetID() == id )
		{
			rtti::ValuePtr propertyValue;
			if ( m_chunksList->GetSelectedMaterial()->ReadCustomEditableProperty( IRTTIContext::GetDefault(), propName, parser.GetUneatenPath(), propertyValue ) && propertyValue )
			{
				TResRef<CResource> resRef;
				TResAsyncRef<CResource> asyncRef;
				if ( GetTypeObject< TResRef<CResource> >()->WriteValue( IRTTIContext::GetDefault(), &resRef, parser.GetUneatenPath(), *propertyValue.Get() ) )
				{
					AppendDependencyMonitor( resRef.GetPath() );
				}
				else if ( GetTypeObject< TResAsyncRef<CResource> >()->WriteValue( IRTTIContext::GetDefault(), &asyncRef, parser.GetUneatenPath(), *propertyValue.Get() ) )
				{
					AppendDependencyMonitor( asyncRef.GetPath() );
				}
			}
		}
		else if ( propName == RED_NAME_CONSTEXPR_NOREG( "isTwoSided" ) )
		{
			OnMeshChanged();
		}
	}

	void MeshResourceEditor::SetChunksCascadeFlags()
	{
		THandle<CMesh> outputMesh = GetMesh();
		if ( !outputMesh )
		{
			RED_LOG_ERROR( "Unable to get original mesh" );
		}

		if ( GetMesh()->GetIsGIVisible() )
		{
			mesh::DataView meshDataView( *outputMesh );

			const auto numChunks = meshDataView.GetNumChunks();
			for ( Uint32 i = 0; i < numChunks; ++i )
			{
				const auto chunkLodMask = meshDataView.GetChunkLodMask( i );
				if ( chunkLodMask )
				{
					mesh::ChunkFlags flags = meshDataView.GetChunkFlags( i );
					flags.m_renderInShadows = true;
					meshDataView.SetChunkFlags( i, flags );
				}
			}

			outputMesh->MarkModified();
		}
	}

	void MeshResourceEditor::RefreshChunkList( const Int32 forcedLOD )
	{
		if (m_chunksList)
			m_chunksList->NotifyChange( forcedLOD );
	}

	Bool MeshResourceEditor::RemoveMaterial( Uint32 removedMaterialIndices ) const
	{
		if( m_materialsList && m_materialsList->RemoveMaterial( removedMaterialIndices ) )
		{
			return true;
		}
		tools::Message logMessage( MessageSeverity::Error, std::move( RED_LOCATION( GetResource()->GetPath() ) ) );
		logMessage << RED_NAME_CONSTEXPR( "MaterialInstance " );
		logMessage << " is used in an appearance and cannot be removed. ";
		IMessageReporter::ReportSafe( logMessage );
		return false;
	}

	void MeshResourceEditor::RefreshStructure()
	{
        RED_FATAL_ASSERT(m_chunksList, "Error: Chunk list is NULL");
        RED_FATAL_ASSERT(m_materialsList, "Error: Materials list is NULL");
        RED_FATAL_ASSERT(m_apperancesList, "Error: Appearances list is NULL");

		RefreshStructureTree();

		m_chunksList->NotifyListChanged( true );
		m_materialsList->NotifyListChanged( true );
		m_apperancesList->NotifyListChanged( true );
	}

	void MeshResourceEditor::RefreshStructureTree()
	{
		if( m_structureTree )
			m_structureTree->RefreshTree();
	}

	void MeshResourceEditor::OnActionExecute( const IEditAction& action )
	{
		TBaseClass::OnActionExecute( action );
		m_structureTree->RefreshVisualization();

		if( m_chunksList && action.GetDescription().Contains("baseMaterial") )
		{
			m_chunksList->NotifyListChanged( true );
		}
	}

} // interop