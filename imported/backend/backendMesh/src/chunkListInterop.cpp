
#include "build.h"
#include "chunkListInterop.h"
#include "meshStructureVisualization.h"
#include "../../backendEngine/include/editActionHistory.h"
#include "../../backendEngine/include/objectCollection.h"
#include "../../backendEngine/include/objectSerializablePersistentCollection.h"
#include "../../../common/core/include/clipboardBase.h"
#include "../../../common/core/include/memoryBufferFile.h"
#include "../../../common/resourceMaterial/include/materialCompilationManager.h"
#include "../../../common/resourceMaterial/include/materialInstance.h"
#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMesh/include/meshAppearance.h"
#include "../../../common/core/include/message.h"
#include "../../../common/redReflection/include/cloningUtils.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( ChunkStats, mesh );
	RTTI_PROPERTY( m_chunkIndexCount ).readOnly();
	RTTI_PROPERTY( m_chunkVertexCount ).readOnly();
	RTTI_PROPERTY( m_chunkTriangleCount ).readOnly();
	RTTI_PROPERTY( m_bonesPerVertex ).readOnly();
	RTTI_PROPERTY( m_vertexType ).readOnly();
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( ChunkEntry, mesh );
	RTTI_PROPERTY( m_lod ).readOnly();
	RTTI_PROPERTY( m_flags ).editable();
	RTTI_PROPERTY( m_statistics ).readOnly();
RTTI_END_TYPE();

namespace tools
{
	class MaterialWithName: public ISerializable
	{
		RTTI_DECLARE_TYPE( MaterialWithName );

	public:
		MaterialWithName() = default;
		MaterialWithName( const CName& name, const mesh::DataViewRO& meshView, THandle< IMaterial >& material )
			: m_materialName( name)
		{
			Int32 materialIndex = meshView.GetMaterialIndex( material.Get() );
			RED_ASSERT( materialIndex != red::INVALID_INDEX, "Material doesn't belong to this mesh." );
			if ( meshView.IsMaterialLocalInstance( materialIndex ) )
			{
				const red::StringView pathView = material->GetPath().ToStringView();
				RED_ASSERT( pathView.Empty() || red::paths::ExtractFileName( pathView ).BeginsWith( "localmaterial_" ), "This local instance material has wrong material path" );

				material->Internal_SetPath( res::ResourcePath() );
				m_localMaterial = material;
			}
			else
			{
				m_externalMaterial = material;
			}
		}

		CName GetName() const { return m_materialName; }
		THandle< IMaterial > GetMaterial() { return m_localMaterial ? m_localMaterial : Cast< IMaterial >( m_externalMaterial.Get() ); }

		EditorObjectID GetEditorObjectID() const override
		{
			return EditorObjectID::Build( GetID().Get(), GetClass()->GetName(), GetName() );
		}
	private:
		CName m_materialName;
		THandle< IMaterial > m_localMaterial;
		TResRef< IMaterial > m_externalMaterial;
	};

	class Action_AddMaterialCopy: public IEditAction
	{
	public:
		Action_AddMaterialCopy( ChunksListInterop& list, const String& name, const THandle< IMaterial >& sourceMaterial )
			: m_list( list.SharedFromThis() )
			, m_materialsManager( list.GetMaterialsManager() )
			, m_materialName( name )
			, m_sourceMaterial( sourceMaterial )
		{}

		virtual String GetDescription() const override { return "Add material"; }

		virtual Bool Do() override
		{
			Uint32 oldMaterialIndex = red::INVALID_INDEX;
			if ( ( oldMaterialIndex = m_materialsManager->GetMaterialIndex( m_materialName ) ) != red::INVALID_INDEX )
			{
				mesh::DataView meshView( *red::StaticCast< ChunksListInterop >( m_list )->GetMesh() );
				m_changedMaterial = m_materialsManager->GetMaterial( oldMaterialIndex );
				meshView.SetMaterialData( oldMaterialIndex, m_sourceMaterial );
				return true;
			}

			if ( m_materialsManager->AddMaterial( m_sourceMaterial, m_materialName ) )
			{
				if ( m_list )
				{
					m_list->NotifyListChanged();
				}
				return true;
			}
			return false;
		}

		virtual Bool Undo() override
		{
			Bool result = false;
			if ( m_changedMaterial )
			{
				mesh::DataView meshView( *red::StaticCast< ChunksListInterop >( m_list )->GetMesh() );
				meshView.SetMaterialData( meshView.GetMaterialIndex( RED_NAME( m_materialName ) ), m_changedMaterial );
				return true;
			}
			else
			{
				return m_materialsManager->RemoveMaterial( m_materialsManager->GetMaterialIndex( m_materialName ) );
			}
			red::StaticCast< ChunksListInterop >( m_list )->NotifyAppearancesChange();
			return result;
		}

	private:
		red::SharedPtr< mesh::IMaterialsListInterface > m_materialsManager;
		red::SharedPtr< InteropList > m_list;

		const String m_materialName;
		const THandle< IMaterial > m_sourceMaterial;
		THandle< IMaterial > m_changedMaterial = nullptr; // For undo
	};

	//////////////////////////////////////////////////////////////////////////

	class Action_AddModelApperanceCopy: public IEditAction
	{
	public:
		Action_AddModelApperanceCopy( ChunksListInterop& list, const String& name, const THandle< mesh::MeshAppearance >& sourceAppearance )
			: m_list( list.SharedFromThis() )
			, m_appearanceName( RED_NAME( name ) )
			, m_sourceAppearance( sourceAppearance )
		{}

		virtual String GetDescription() const override { return "Add Appearance"; }

		virtual Bool Do() override
		{
			return AddAppearance( m_appearanceName, m_sourceAppearance, &m_changedAppearance );
		}

		virtual Bool Undo() override
		{
			Bool result = false;
			if ( m_changedAppearance )
			{
				result = AddAppearance( m_appearanceName, m_changedAppearance, nullptr );
			}
			else
			{
				mesh::DataView meshView( *red::StaticCast< ChunksListInterop >( m_list )->GetMesh() );
				result = meshView.RemoveAppearance( m_appearanceName );
			}
			red::StaticCast< ChunksListInterop >( m_list )->NotifyAppearancesChange();
			return result;
		}

		Bool AddAppearance( const CName& name, THandle< mesh::MeshAppearance >& sourceAppearance, THandle< mesh::MeshAppearance >* outModifiedAppearance )
		{
			THandle< CMesh > mesh = red::StaticCast< ChunksListInterop >( m_list )->GetMesh();
			mesh::DataView meshView( *mesh );

			if ( !meshView.IsAppearanceCompatible( *sourceAppearance ) )
			{
				Uint32 chunksCount = 0;
				Uint32 existingLODs = 0;
				for ( Int32 i = 0; i < meshView.GetNumLODs(); ++i )
				{
					chunksCount += meshView.GetLODChunkCount( i );
					existingLODs++;

					if ( chunksCount == sourceAppearance->GetNumChunks() )
					{
						break;
					}

					if ( chunksCount > sourceAppearance->GetNumChunks() )
					{
						RED_REPORT_ERROR( RED_LOCATION( mesh->GetPath() ), "Appearance is incompatible with the mesh." );
						return false;
					}
				}

				if ( sourceAppearance->GetNumChunks() > chunksCount )
				{
					// cut appearance's chunks materials
					red::DynArray< CName > materialNames;
					for ( Uint32 i = 0; i < chunksCount; ++i )
					{
						materialNames.PushBack( sourceAppearance->GetChunkMaterialName( i ) );
					}

					THandle< mesh::MeshAppearance > newAppearance = CreateHandle< mesh::MeshAppearance >( materialNames, mesh.Get(), sourceAppearance->GetName() );
					newAppearance->SetTags( sourceAppearance->GetTags() );
					sourceAppearance = newAppearance;
				}
				else
				{
					Uint32 lastCommonLODChunkCount = meshView.GetLODChunkCount( existingLODs - 1 );
					for ( Uint32 i = existingLODs; i < meshView.GetNumLODs(); i++ )
					{
						if ( meshView.GetLODChunkCount( i ) != lastCommonLODChunkCount )
						{
							RED_REPORT_ERROR( RED_LOCATION( mesh->GetPath() ), "Appearance is incompatible with the mesh." );
							return false;
						}
					}

					// extend appearance's chunks materials
					red::DynArray< CName > materialNames;
					for ( Uint32 i = 0; i < chunksCount; ++i )
					{
						materialNames.PushBack( sourceAppearance->GetChunkMaterialName( i ) );
					}

					for ( Uint32 i = existingLODs; i < meshView.GetNumLODs(); i++ )
					{
						for ( Uint32 j = 0; j < lastCommonLODChunkCount; ++j )
						{
							materialNames.PushBack( sourceAppearance->GetChunkMaterialName( chunksCount - lastCommonLODChunkCount + j ) );
						}
					}

					THandle< mesh::MeshAppearance > newAppearance = CreateHandle< mesh::MeshAppearance >( materialNames, mesh.Get(), sourceAppearance->GetName() );
					newAppearance->SetTags( sourceAppearance->GetTags() );
					sourceAppearance = newAppearance;
				}
			}

			if ( name == RED_NAME_CONSTEXPR_NOREG( "default" ) )
			{
				if ( outModifiedAppearance )
				{
					*outModifiedAppearance = meshView.GetDefaultAppearanceData();
				}

				return meshView.ReplaceDefaultAppearance( sourceAppearance );
			}
			else
			{
				Int32 oldAppearanceIndex = red::INVALID_INDEX;
				if ( ( oldAppearanceIndex = meshView.GetAppearanceIndex( name ) ) != red::INVALID_INDEX )
				{
					if ( outModifiedAppearance )
					{
						*outModifiedAppearance = meshView.GetAppearanceData( oldAppearanceIndex );
					}

					if ( !meshView.RemoveAppearance( name ) )
					{
						return false;
					}
				}

				return meshView.AddAppearanceCopy( name, *m_sourceAppearance );
			}
			return true;
		}
	private:
		red::SharedPtr< InteropList > m_list;

		const CName m_appearanceName;
		THandle< mesh::MeshAppearance > m_sourceAppearance;
		THandle< mesh::MeshAppearance > m_changedAppearance = nullptr;	// For undo
	};

	class Action_AddMaterial : public IEditAction
	{
	public:
		Action_AddMaterial( ChunksListInterop& list, const String& name, const String& baseMaterialName )
			: m_list( list.SharedFromThis() )
			, m_materialsManager( list.GetMaterialsManager() )
			, m_materialName( name )
			, m_baseMaterialName( baseMaterialName )
		{}

		virtual String GetDescription() const override { return "Add material"; }

		virtual Bool Do() override
		{
			if ( m_materialsManager->AddMaterial( m_materialName, m_materialsManager->GetMaterialIndex( m_baseMaterialName ), true ) )
			{
				if ( m_list )
				{
					m_list->NotifyListChanged();
				}
				return true;
			}
			return false;
		}

		virtual Bool Undo() override
		{
			Int32 matInd = m_materialsManager->GetMaterialIndex( m_materialName );
			if ( matInd > 0 && m_materialsManager->RemoveMaterial( matInd ) )
			{
				if ( m_list )
				{
					m_list->NotifyListChanged();
				}
				return true;
			}
			return false;
		}

	private:
		red::SharedPtr< mesh::IMaterialsListInterface > m_materialsManager;
		red::SharedPtr< InteropList > m_list;

		const String m_materialName;
		const String m_baseMaterialName;
	};

	//////////////////////////////////////////////////////////////////////////////////////////////
	class Action_AddModelApperance : public IEditAction
	{
	public:
		enum Operation : Uint8
		{
			CREATE,
			REMOVE
		};

		Action_AddModelApperance( ChunksListInterop& list, const Uint32 baseAppearanceIndex, const String& name, Bool appearanceCopy = true, Operation operation = CREATE )
			: IEditAction()
			, m_appearanceName( name )
			, m_baseAppearanceIndex( baseAppearanceIndex )
			, m_appearanceCopy( appearanceCopy )
			, m_mesh( list.GetMesh() )
			, m_operation( operation )
		{}

		virtual String GetDescription() const override { return (m_operation == CREATE) ? "Add appearance" : "Revert Add appearance"; }

		virtual Bool Do() override final
		{
			return PerformOperation( m_operation != CREATE );
		}

		virtual Bool Undo() override final
		{
			return PerformOperation( m_operation == CREATE );
		}

		Bool PerformOperation( Bool reverse )
		{
			mesh::DataView meshView( *m_mesh );
			if ( reverse )
			{
				return meshView.RemoveAppearance( RED_NAME( m_appearanceName ) );
			}
			else
			{
				if (m_appearanceCopy)
				{
					THandle< mesh::MeshAppearance > sourceAppearance = meshView.GetAppearanceData( m_baseAppearanceIndex );
					return meshView.AddAppearanceCopy( RED_NAME( m_appearanceName ), *sourceAppearance );
				}
				return meshView.AddAppearance( RED_NAME( m_appearanceName ) );
			}
		}

	private:
		String m_appearanceName;
		Uint32 m_baseAppearanceIndex;               // Used as source when copying.
		Bool m_appearanceCopy;                      // Set to true when copying.
		const THandle< CMesh >& m_mesh;
		Operation m_operation;
	};

	//////////////////////////////////////////////////////////////////////////////////////////////
	class Action_RemoveModelAppearance : public IEditAction
	{
	public:
		enum Operation : Uint8
		{
			CREATE,
			REMOVE
		};

		Action_RemoveModelAppearance( ChunksListInterop& list, const String appearanceName, Operation operation = CREATE )
			: IEditAction()
			, m_mesh( list.GetMesh() )
			, m_list( list )
			, m_appearanceName( RED_NAME( appearanceName ) )
			, m_operation( operation )
		{
			mesh::DataViewRO meshView( *m_mesh );
			m_appearance = meshView.GetAppearanceData( meshView.GetAppearanceIndex( m_appearanceName ) );
		}

		virtual String GetDescription() const override { return (m_operation == CREATE) ? "Remove appearance" : "Revert Remove appearance"; }

		virtual Bool Do() override final
		{
			return PerformOperation( m_operation != CREATE );
		}

		virtual Bool Undo() override final
		{
			return PerformOperation( m_operation == CREATE );
		}

		Bool PerformOperation( Bool reverse )
		{
			mesh::DataView meshView( *m_mesh );
			if ( reverse )
			{
				return meshView.AddAppearance( m_appearance, GetItemIndex() );
			}
			else
			{
				Bool result = meshView.RemoveAppearance( m_appearanceName );

				if ( result )
					m_list.NotifyAppearancesChange();

				return result;
			}
		}

		Uint32 GetItemIndex() const
		{
			return mesh::DataViewRO( *m_mesh ).GetAppearanceIndex( m_appearanceName );
		}

	private:
		CName m_appearanceName;
		THandle< mesh::MeshAppearance > m_appearance;
		const THandle< CMesh >& m_mesh;
		ChunksListInterop& m_list;
		Operation m_operation;
	};

	//////////////////////////////////////////////////////////////////////////////////////////////
	class Action_RenameModelAppearance : public IEditAction
	{
	public:
		enum Operation : Uint8
		{
			CREATE,
			REMOVE
		};

		Action_RenameModelAppearance( ChunksListInterop& list, const String& oldName, const String& newName, Operation operation = CREATE )
			: IEditAction()
			, m_newAppearanceName( newName )
			, m_appearanceName( oldName )
			, m_mesh( list.GetMesh() )
			, m_operation( operation )
		{
		}

		virtual String GetDescription() const override { return (m_operation == CREATE) ? "Rename appearance" : "Revert Rename appearance"; }

		virtual Bool Do() override final
		{
			return PerformOperation( m_operation != CREATE );
		}

		virtual Bool Undo() override final
		{
			return PerformOperation( m_operation == CREATE );
		}

		Bool PerformOperation( Bool reverse )
		{
			mesh::DataView meshView( *m_mesh );
			if ( !reverse )
			{
				Int32 appearanceInd = meshView.GetAppearanceIndex( m_appearanceName );
				if (appearanceInd >= 0)
				{
					return meshView.RenameAppearance( appearanceInd, RED_NAME( m_newAppearanceName ) );
				}
				return false;
			}
			else
			{
				Int32 appearanceInd = meshView.GetAppearanceIndex( m_newAppearanceName );
				if (appearanceInd >= 0)
				{
					return meshView.RenameAppearance( appearanceInd, RED_NAME( m_appearanceName ) );
				}
				return false;
			}
		}

	private:
		String m_newAppearanceName;
		String m_appearanceName;
		const THandle< CMesh >& m_mesh;
		Operation m_operation;
	};

	//////////////////////////////////////////////////////////////////////////////////////////////
	class Action_RenameMaterial : public Action_RenameListItem
	{
	public:
		Action_RenameMaterial( const String& newName, ChunksListInterop& list, const Int32 index )
			: Action_RenameListItem( list.SharedFromThis() )
			, m_newMaterialName( newName )
			, m_materialsManager( list.GetMaterialsManager() )
		{
			m_materialName = m_materialsManager->GetMaterialName( index );
		}

		Action_RenameMaterial( const String& newName, ChunksListInterop& list, const String& materialName )
			: Action_RenameListItem( list.SharedFromThis() )
			, m_newMaterialName( newName )
			, m_materialsManager( list.GetMaterialsManager() )
			, m_materialName( materialName )
		{
		}

		virtual String GetDescription() const override { return "Rename material"; }

		virtual Bool PerformRename() override
		{
			Int32 matInd = m_materialsManager->GetMaterialIndex( m_materialName );
			if (matInd >= 0)
			{
				return m_materialsManager->RenameMaterial( matInd, m_newMaterialName );
			}
			return false;
		}

		virtual Bool RevertRename() override
		{
			Int32 matInd = m_materialsManager->GetMaterialIndex( m_newMaterialName );
			if (matInd >= 0)
			{
				return m_materialsManager->RenameMaterial( matInd, m_materialName );
			}
			return false;
		}

	private:
		red::SharedPtr< mesh::IMaterialsListInterface > m_materialsManager;
		String m_newMaterialName;
		String m_materialName;
	};

	//////////////////////////////////////////////////////////////////////////////////////////////
	class Action_AssignMaterial : public IEditAction
	{
	public:
		Action_AssignMaterial( ChunksListInterop& list, const Int32 index, const String& materialName )
			: IEditAction()
			, m_chunksList( list )
			, m_newMaterialName( RED_NAME( materialName ) )
			, m_appearance( list.GetActiveAppearance() )
		{
			const red::DynArray< String > materialList = m_chunksList.GetMaterialsList();
			const Uint32 materialIndex = m_chunksList.GetMaterialsManager()->GetMaterialIndex( materialList[index] );
			const red::DynArray< Uint32 > chunkIndices = m_chunksList.GetChunkIndicesUsingMaterial( materialIndex );

			m_oldMaterialName = RED_NAME( materialList[index] );
			for ( Uint32 i = 0; i < chunkIndices.Size(); ++i )
			{
				m_oldMaterialChunks.Insert( chunkIndices[i] );
			}
		}

		virtual String GetDescription() const override
		{
			static const String s_description( "Assign material" );
			return s_description;
		}

		virtual Bool Do() override final
		{
			return PerformOperation( false );
		}

		virtual Bool Undo() override final
		{
			return PerformOperation( true );
		}

		Bool PerformOperation( Bool reverse )
		{
			if ( m_oldMaterialChunks.Empty() )
			{
				return false;
			}

			red::DynArray<CName> appearances;
			m_chunksList.GetAppearances( appearances );
			Int32 appearanceIndex = appearances.GetIndex( m_appearance );

			if ( appearanceIndex < 0 )
			{
				return false;
			}

			if( !reverse )
			{
				for ( Uint32 chunkIndex : m_oldMaterialChunks )
				{
					m_chunksList.GetMaterialsManager()->SetChunkMaterial( chunkIndex, m_newMaterialName, appearanceIndex );
				}
			}
			else
			{
				for ( Uint32 chunkIndex : m_oldMaterialChunks )
				{
					m_chunksList.GetMaterialsManager()->SetChunkMaterial( chunkIndex, m_oldMaterialName, appearanceIndex );
				}
			}

			m_chunksList.DeselectAllItems();
			m_chunksList.NotifyMaterialsChange();
			m_chunksList.NotifyListChanged( true );
			return true;
		}

	private:
		ChunksListInterop& m_chunksList;
		const CName m_appearance;
		const CName m_newMaterialName;
		CName m_oldMaterialName;
		red::HashSet< Uint32 > m_oldMaterialChunks;
	};

	//////////////////////////////////////////////////////////////////////////////////////////////
	/// This action can create or remove material instance.
	class Action_CreateMaterialInstance : public IEditAction
	{
	public:
		enum Operation : Uint8
		{
			CREATE,
			REMOVE
		};

		Action_CreateMaterialInstance( ChunksListInterop& list, const Int32 index, const Int32 appearanceIndex, const Int32 currentForcedLODIndex, Operation operation = CREATE )
			: IEditAction()
			, m_chunksList( list )
			, m_operation( operation )
			, m_index( index )
			, m_currentAppearanceIndex( appearanceIndex )
			, m_currentForcedLODIndex( currentForcedLODIndex )
		{
		}

		virtual String GetDescription() const override { return (m_operation == CREATE) ? "Create material instance" : "Remove material instance"; }

		virtual Bool Do() override final
		{
			return PerformOperation( m_operation != CREATE );
		}

		virtual Bool Undo() override final
		{
			return PerformOperation( m_operation == CREATE );
		}

		Bool PerformOperation( Bool reverse )
		{
			interop::MaterialListDescriptor materialListDescriptor;
			m_chunksList.GetMaterialInfo( m_index, materialListDescriptor );

			if ( materialListDescriptor.m_isLocalInstance ^ reverse )
				return false;


			red::DynArray< Uint32 > materialsToProcess;
			red::DynArray< CName > appearances;
			m_chunksList.GetAppearances( appearances );

			// perform for selected appearance only
			if (m_currentAppearanceIndex > -1)
			{
				const Uint32 materialIndex = m_chunksList.GetMaterialsManager()->GetMaterialIndexAppliedOnChunk( m_index, m_currentAppearanceIndex );
				if (!reverse)
				{
					// if no LOD forced - set for all LODs
					if( m_currentForcedLODIndex == -1 )
					{
						materialsToProcess.PushBack( materialIndex );
					}
					else
					{
						Uint32 addedMaterialIndex = 0;
						if (m_chunksList.DuplicateMaterial( materialIndex, m_index, m_currentAppearanceIndex, addedMaterialIndex ))
						{
							materialsToProcess.PushBack( addedMaterialIndex );
						}
					}
				}
				else
				{
					materialsToProcess.PushBack( materialIndex );
				}
			}
			// perform for all appearances
			else
			{
				for (Uint32 i = 0; i < appearances.Size(); ++i)
				{
					const Uint32 materialIndex = m_chunksList.GetMaterialsManager()->GetMaterialIndexAppliedOnChunk( m_index, i );
					Bool process = true;

					for (auto j : materialsToProcess)
					{
						if (j == materialIndex)
							process = false;
					}
					if (process)
						materialsToProcess.PushBack( materialIndex );
				}
			}

			Bool ret = true;

			for (Uint32 i = 0; i < materialsToProcess.Size(); ++i)
			{
				ret &= reverse ? m_chunksList.GetMaterialsManager()->RemoveMaterialLocalInstance( materialsToProcess[i] )
					: m_chunksList.GetMaterialsManager()->CreateMaterialLocalInstance( materialsToProcess[i] );
			}

			m_chunksList.OnLocalInstanceChanged();
			return ret;
		}

	private:
		ChunksListInterop& m_chunksList;
		const Int32 m_index;
		const Int32 m_currentAppearanceIndex;
		const Int32 m_currentForcedLODIndex;
		Operation m_operation;
	};

	//////////////////////////////////////////////////////////////////////////////////////////////
	/// This action can remove unused materials.
	class Action_RemoveUnusedMaterials : public IEditAction
	{
	public:
		Action_RemoveUnusedMaterials( ChunksListInterop& list )
			: IEditAction()
			, m_chunkList( list )
			, m_prevMaterials( list.GetAllMaterials() )
			, m_prevMaterialNames( list.GetAllMaterialNames() )
		{
		}

		virtual String GetDescription() const override { return "Remove unused materials"; }

		virtual Bool Do() override final
		{
			red::DynArray< Uint32 > removedMaterialIndices;
			m_chunkList.RemoveUnusedMaterials( &removedMaterialIndices );
			for (Uint32 materialIndex : removedMaterialIndices)
			{
				if (m_chunkList.IsItemSelected( materialIndex ))
					m_chunkList.DeselectItem( materialIndex );
			}
			m_chunkList.NotifyListChanged();
			return true;
		}

		virtual Bool Undo() override final
		{
			Bool result = m_chunkList.ResetMaterials( m_prevMaterials, m_prevMaterialNames );
			m_chunkList.NotifyListChanged();
			return result;
		}

	private:
		ChunksListInterop& m_chunkList;
		red::DynArray< THandle< IMaterial > > m_prevMaterials;  // Mesh materials before removing unused ones.
		red::DynArray< CName > m_prevMaterialNames;             // Material names before removing unused ones.

	};

	////////////////////

	class ChunkItem : public InteropListItem
	{
	public:
		explicit ChunkItem( ChunksListInterop& chunkList, Uint32 index, Uint32 currentAppearanceIndex, Uint32 currentForcedLODIndex, IEditActionHistory& actionHistory )
			: InteropListItem( index, chunkList.SharedFromThis(), actionHistory, "Chunk" )
		{
			::interop::utils::Object::RegisterFunctor<Bool>( "recompileMaterial",
				[&chunkList, index]( interop::IContext& ctx )
			{
#ifndef NO_RUNTIME_MATERIAL_COMPILATION
				if ( THandle< IMaterial > material = chunkList.GetSelectedMaterial() )
				{
					const Bool recompilationResult = material->Recompile( false );
					if ( !recompilationResult )
					{
						ctx.ThrowException( "Recompilation of material failed. Possible reasons: errors in shader code (check log or engine\\shadersDump directory." );
					}
					GetMaterialCompilationManager().Run( false );
					return recompilationResult;
				}
				ctx.ThrowException( "Invalid material." );
#endif // NO_RUNTIME_MATERIAL_COMPILATION
				return false;
			} );

			::interop::utils::Object::RegisterFunctor<Bool>( "recompileMaterialSingle",
				[&chunkList, index]( interop::IContext& ctx )
			{
#ifndef NO_RUNTIME_MATERIAL_COMPILATION
				if ( THandle< IMaterial > material = chunkList.GetSelectedMaterial() )
				{
					const Bool recompilationResult = material->Recompile( false, true );
					if ( !recompilationResult )
					{
						ctx.ThrowException( "Recompilation of material failed. Possible reasons: errors in shader code (check log or engine\\shadersDump directory." );
					}
					GetMaterialCompilationManager().Run( false );
					return recompilationResult;
				}
				ctx.ThrowException( "Invalid material." );
#endif // NO_RUNTIME_MATERIAL_COMPILATION
				return false;
			} );

			::interop::utils::Object::RegisterFunctor< Bool >("refreshMaterialParameters",
				[ &chunkList, index ](interop::IContext& ctx)
			{
				if ( THandle< IMaterial > material = chunkList.GetSelectedMaterial() )
				{

					auto materialInstance = Cast< CMaterialInstance >( material );
					if ( materialInstance )
					{
						if ( materialInstance->UpdateParametersListBasedOnTemplate() )
						{
							materialInstance->MarkModified();
							chunkList.OnLocalInstanceChanged();
						}
						return true;
					}
					else
					{
						ctx.ThrowException("Not a material instance.");
						return false;
					}
				}
				ctx.ThrowException("Invalid material.");
				return false;
			});

			::interop::utils::Object::RegisterFunctor<Bool>( "isMaterialInstance",
				[&chunkList, index]( interop::IContext& ctx )
			{
				interop::MaterialListDescriptor materialListDescriptor;
				chunkList.GetMaterialInfo( index, materialListDescriptor );
				return materialListDescriptor.m_isLocalInstance;
			} );

			::interop::utils::Object::RegisterValueRO< interop::MaterialListDescriptor >( "materialDescriptor",
				[&chunkList, index]()
			{
				interop::MaterialListDescriptor materialListDescriptor;

				Bool singleMaterial = chunkList.GetMaterialInfo( index, materialListDescriptor );
				materialListDescriptor.m_appearanceName = chunkList.GetActiveAppearance().AsChar();

				red::DynArray< String > availableMaterials = chunkList.GetAvailableMaterials( index );
				std::sort( availableMaterials.Begin(), availableMaterials.End() );
				materialListDescriptor.m_availableMaterials = availableMaterials;
				materialListDescriptor.m_itemMaterialIndex = availableMaterials.GetIndex( materialListDescriptor.m_materialName );

				materialListDescriptor.m_numLayers = 0;													/// @todo DP: correct this
				return materialListDescriptor;
			} );

			::interop::utils::Object::RegisterFunctor<Bool>( "createLocalInstance",
				[&chunkList, index, currentAppearanceIndex, currentForcedLODIndex, &actionHistory]( interop::IContext& ctx )
			{
				auto action = red::CreateUniquePtr< Action_CreateMaterialInstance >( chunkList, index, -1, currentForcedLODIndex, Action_CreateMaterialInstance::CREATE );
				Bool result = actionHistory.Execute( std::move( action ) );
				if (!result)
					ctx.ThrowException( "Couldn't create material instance. Possible reasons: material was already an instance or wrong material index was passed." );
				return result;
			} );

			::interop::utils::Object::RegisterFunctor<Bool>( "createLocalInstanceForAppearance",
				[&chunkList, index, currentAppearanceIndex, currentForcedLODIndex, &actionHistory]( interop::IContext& ctx )
			{
				auto action = red::CreateUniquePtr< Action_CreateMaterialInstance >( chunkList, index, currentAppearanceIndex, currentForcedLODIndex, Action_CreateMaterialInstance::CREATE );
				Bool result = actionHistory.Execute( std::move( action ) );
				if (!result)
					ctx.ThrowException( "Couldn't create material instance. Possible reasons: material was already an instance or wrong material index was passed." );
				return result;
			} );

			::interop::utils::Object::RegisterFunctor<Bool>( "removeLocalInstance",
				[&chunkList, index, currentAppearanceIndex, currentForcedLODIndex, &actionHistory]( interop::IContext& ctx )
			{
				auto action = red::CreateUniquePtr< Action_CreateMaterialInstance >( chunkList, index, -1, currentForcedLODIndex, Action_CreateMaterialInstance::REMOVE );
				Bool result = actionHistory.Execute( std::move( action ) );
				if (!result)
					ctx.ThrowException( "Couldn't remove material instance. Possible reasons: material was not an instance or wrong material index was passed." );
				return result;
			} );

			::interop::utils::Object::RegisterFunctor<Bool>( "removeLocalInstanceForAppearance",
				[&chunkList, index, currentAppearanceIndex, currentForcedLODIndex, &actionHistory]( interop::IContext& ctx )
			{
				auto action = red::CreateUniquePtr< Action_CreateMaterialInstance >( chunkList, index, currentAppearanceIndex, currentForcedLODIndex, Action_CreateMaterialInstance::REMOVE );
				Bool result = actionHistory.Execute( std::move( action ) );
				if (!result)
					ctx.ThrowException( "Couldn't remove material instance. Possible reasons: material was not an instance or wrong material index was passed." );
				return result;
			} );

			::interop::utils::Object::RegisterFunctor<Bool, String, String>( "renameMaterial",
				[&chunkList, index, &actionHistory]( interop::IContext& ctx, const String& oldName, const String& newName )
			{
				const Uint32 materialIndex = chunkList.GetMaterialsManager()->GetMaterialIndex( oldName );

				auto action = red::CreateUniquePtr< Action_RenameMaterial >( newName, chunkList, materialIndex );

				Bool result = actionHistory.Execute( std::move( action ) );
				if (!result)
					ctx.ThrowException( "Couldn't rename material. Possible reasons: material was not an instance or wrong material index was passed." );
				return result;
			} );

			::interop::utils::Object::RegisterFunctor<Bool, String, String>( "addMaterial",
				[&chunkList, &actionHistory]( interop::IContext& ctx, const String& newMaterialName, const String& baseMaterialName )
			{
				const String resultingNewMaterialName = chunkList.GenerateUniqueMaterialName( newMaterialName );
				Bool result = actionHistory.Execute( red::CreateUniquePtr< Action_AddMaterial >( chunkList, resultingNewMaterialName, baseMaterialName ) );

				chunkList.OnMaterialAdded( baseMaterialName, resultingNewMaterialName );
				chunkList.NotifyAppearancesChange();

				if (!result)
					ctx.ThrowException( "Couldn't add material. Possible reasons: material with a given name already exists or wrong material index was passed." );
				return result;
			} );

			::interop::utils::Object::RegisterFunctor< void, String >( "applyMaterial",
				[&chunkList, &actionHistory, index]( interop::IContext& ctx, const String& materialName )
			{
				Bool result = actionHistory.Execute( red::CreateUniquePtr< Action_AssignMaterial >( chunkList, index, materialName ) );
				if (!result)
					ctx.ThrowException( "Couldn't apply this material. Possible reasons: material with a given name already exists or wrong material index was passed." );
				return result;

			} );

			::interop::utils::Object::RegisterFunctor< void >( "copyAppearances", [ &chunkList ] ( interop::IContext& ctx )
			{
				return chunkList.CopyAllAppearances();
			} );

			::interop::utils::Object::RegisterFunctor< void >( "pasteAppearances", [ &chunkList ] ( interop::IContext& ctx )
			{
				return chunkList.PasteAppearances();
			} );

			::interop::utils::Object::RegisterFunctor< void >( "copyMaterial", [&chunkList] ( interop::IContext& ctx )
			{
				return chunkList.CopyMaterial();
			} );

			::interop::utils::Object::RegisterFunctor< void >( "pasteMaterial", [&chunkList] ( interop::IContext& ctx )
			{
				return chunkList.PasteMaterial();
			} );

		}
	};

	///////////////////

	ChunksListInterop::ChunksListInterop( IEditActionHistory& actionHistory, ObjectCollection* sharedSelection, const THandle< CMesh > mesh, TCallback materialsChangedCallback, TCallback appearancesChangedCallback, TActivationCallback appearanceActivatedCallback )
		: InteropList( actionHistory, sharedSelection, InteropListSetupInfo( false, false, false ) )
		, m_mesh( mesh )
		, m_selectedItem( -1 )
		, m_selectedMaterialIndex( -1 )
		, m_currentForcedLODIndex( -1 )
		, m_currentAppearanceIndex( 0 )
		, m_materialsChangedCallback( materialsChangedCallback )
		, m_appearancesChangedCallback( appearancesChangedCallback )
		, m_appearanceActivatedCallback( appearanceActivatedCallback )
	{
		m_currentSelectedChunkEntry = CreateHandle< mesh::ChunkEntry >();
		m_materialSelection = RED_NEW( ObjectCollection )(actionHistory);

		::interop::utils::Object::RegisterObject( "selectionmaterial", interop::ObjectPtr( m_materialSelection->GetRootInterop() ) );

		m_materialsManager.Reset( RED_NEW( MeshMaterialsList )(m_mesh) );
		RegisterInterop();
	}

	ChunksListInterop::~ChunksListInterop()
	{
		if (m_materialSelection)
		{
			RED_DELETE( m_materialSelection );
			m_materialSelection = nullptr;
		}

		m_materialsManager.Reset();
	}

	void ChunksListInterop::RegisterInterop()
	{
		::interop::utils::Object::RegisterNotifier( "_onAppearancesChanged", m_appearancesChanged );
		::interop::utils::Object::RegisterNotifier( "_onMaterialsChanged", m_materialsChanged );

		::interop::utils::Object::RegisterFunctor< red::DynArray< String > >( "getAppearanceNames",
			[this]( interop::IContext& ctx )
		{
			red::DynArray< String > appearancesNames;
			mesh::DataViewRO meshView( *m_mesh );
			for (Uint32 i = 0; i < meshView.GetNumAppearances(); ++i)
				appearancesNames.PushBack( meshView.GetAppearanceName( i ).AsChar() );

			Int32 defaultAppearanceIndex = appearancesNames.GetIndex( mesh::MeshAppearance::c_DefaultAppearanceName.AsChar() );
			if ( defaultAppearanceIndex > 0 )
			{
				appearancesNames.RemoveAt( defaultAppearanceIndex );
				appearancesNames.InsertAt( 0, mesh::MeshAppearance::c_DefaultAppearanceName.AsChar() );
			}

			if ( !appearancesNames.Empty() )
				std::sort( appearancesNames.Begin() + 1, appearancesNames.End() );

			return appearancesNames;
		} );

		::interop::utils::Object::RegisterFunctor< Uint32 >( "hasUnusedMaterials",
			[this]( interop::IContext& ctx )
		{
			red::DynArray< Uint32 > removedIndices;
			mesh::DataView( *m_mesh ).RemoveUnusedMaterials( &removedIndices, true );
			return removedIndices.Size();
		} );

		::interop::utils::Object::RegisterFunctor< void, String >( "setAppearanceFilter",
			[this]( interop::IContext& ctx, const String& appearanceFilterName )
		{
			DeselectAllItems();

			CName appearanceName = RED_NAME( appearanceFilterName );

			m_materialsManager.Reset( RED_NEW( MeshMaterialsList )( m_mesh ) );
			m_currentAppearanceIndex = mesh::DataViewRO( *m_mesh ).GetAppearanceIndex( appearanceFilterName );


			m_appearancesChangedCallback();
			m_appearanceActivatedCallback( appearanceName );

			NotifyListChanged( true );
		} );

		::interop::utils::Object::RegisterFunctor< Bool, String, String >( "renameAppearance",
			[this]( interop::IContext& ctx, const String& appearanceName, const String& newAppearanceName )
		{
			Bool result = GetActionHistory().Execute( red::CreateUniquePtr< Action_RenameModelAppearance >( *this, appearanceName, newAppearanceName ) );
			if (!result)
				ctx.ThrowException( "Couldn't rename appearance." );
			return result;

		} );

		::interop::utils::Object::RegisterFunctor< Bool, String >( "removeAppearance",
			[this]( interop::IContext& ctx, const String& appearanceName )
		{
			Bool result = GetActionHistory().Execute( red::CreateUniquePtr< Action_RemoveModelAppearance >( *this, appearanceName ) );
			if (!result)
				ctx.ThrowException( "Couldn't remove this appearance. Possible reason - can't remove last appearance" );
			return result;

		} );

		::interop::utils::Object::RegisterFunctor< Bool, String, String >( "addAppearance",
			[this]( interop::IContext& ctx, const String& baseAppearanceName, const String& newAppearanceName )
		{
			const Uint32 baseAppearanceIndex = mesh::DataViewRO( *m_mesh ).GetAppearanceIndex( baseAppearanceName );
			Bool result = GetActionHistory().Execute( red::CreateUniquePtr< Action_AddModelApperance >( *this, baseAppearanceIndex, newAppearanceName ) );
			if (!result)
				ctx.ThrowException( "Couldn't add this appearance." );
			return result;

		} );

		::interop::utils::Object::RegisterFunctor< Uint32 >( "removeUnusedMaterials",
			[this]( interop::IContext& ctx )
		{
			Uint32 removedCount = 0;
			red::DynArray< Uint32 > removedIndices;
			mesh::DataView( *m_mesh ).RemoveUnusedMaterials( &removedIndices, true );

			auto action = red::CreateUniquePtr< Action_RemoveUnusedMaterials >( *this );
			Bool result = GetActionHistory().Execute( std::move( action ) );

			if (!result)
			{
				ctx.ThrowException( "Couldn't remove unused materials." );
			}
			else
			{
				removedCount = removedIndices.Size();
			}
			return removedCount;
		} );

	}

	void ChunksListInterop::NotifyAppearancesChange()
	{
		m_appearancesChanged.Notify();
	}

	void ChunksListInterop::NotifyMaterialsChange()
	{
		if (m_selectedMaterialIndex > -1)
		{
			GetMaterialsManager()->UpdateMaterialRenderResource( m_selectedMaterialIndex );
		}

		mesh::DataView meshView( *m_mesh );
		meshView.ForceRecreateMaterialSetup();
		meshView.Apply();

		RefreshVisualization();

		m_materialsChangedCallback();

		m_materialsChanged.Notify();
	}

	void ChunksListInterop::NotifyChange( const Int32 forcedLOD )
	{
		if (m_currentForcedLODIndex != forcedLOD)
		{
			DeselectAllItems();
			m_currentForcedLODIndex = forcedLOD;
			NotifyListChanged( true );
		}
	}

	void ChunksListInterop::SetMesh( const THandle<CMesh>& mesh )
	{
		m_mesh = mesh;
		m_materialsManager->SetMesh( mesh );
	}

	void ChunksListInterop::OnMaterialAdded( const String& baseMaterialName, const String& resultingNewMaterialName )
	{
		// when adding new material, automatically set it to last chunk (on each LOD)
		if( m_currentForcedLODIndex == -1 )
		{
			const Uint32 materialIndex = m_materialsManager->GetMaterialIndex( baseMaterialName );
			const red::DynArray< Uint32 > chunkIndicesUsingThisMaterial = GetChunkIndicesUsingMaterial( materialIndex );

			if( !chunkIndicesUsingThisMaterial.Empty() )
			{
				mesh::DataView( *m_mesh ).SetChunkMaterial( RED_NAME( resultingNewMaterialName ), chunkIndicesUsingThisMaterial[ chunkIndicesUsingThisMaterial.Size()-1 ], m_currentAppearanceIndex );
			}
		}
	}

	void ChunksListInterop::AttachVisualizationContainer( IMeshVisualizationContainer* visContainer )
	{
		m_visualizationContainers.PushBack( visContainer );
		RefreshVisualization();
	}

	void ChunksListInterop::DetachVisualizationContainer( IMeshVisualizationContainer* visContainer )
	{
		if (m_visualizationContainers.Exist( visContainer ))
		{
			m_visualizationContainers.Remove( visContainer );
		}
	}

	void ChunksListInterop::RefreshVisualization( Int32 index /* = -1 */ )
	{
		mesh::DataViewRO meshView( *m_mesh );

		for (auto* vis : m_visualizationContainers)
		{
			vis->RemoveAllVisualizations();
			vis->SetForcedLOD( m_currentForcedLODIndex );
			vis->SetMeshVisibility( true );

			if (m_selectedMaterialIndex >= 0)
			{
				const auto materialName = meshView.GetMaterialName( m_selectedMaterialIndex );
				vis->AttachVisualization( red::CreateUniquePtr< MeshVisualization_Triangles >( m_mesh, -1, materialName ) );
			}
		}
	}

	ISerializable* ChunksListInterop::GetItem( Uint32 index ) const
	{
		const red::DynArray< String > materialList = GetMaterialsList();

		// if someone added materials
		// but didn't assigned them to any chunk yet....
		if( index < materialList.Size() )
		{
			const Uint32 materialIndex = m_materialsManager->GetMaterialIndex( materialList[index] );
			const red::DynArray< Uint32 > chunkIndices = GetChunkIndicesUsingMaterial( materialIndex );

			m_currentSelectedChunkEntry->m_lod = m_currentForcedLODIndex == -1 ? "Automatic" : String::Printf( "Forced LOD: %d", m_currentForcedLODIndex );

			mesh::DataViewRO meshView( *m_mesh );

			m_currentSelectedChunkEntry->m_flags							= meshView.GetChunkFlags( chunkIndices[0] );
			m_currentSelectedChunkEntry->m_statistics.m_bonesPerVertex		= meshView.GetChunkNumBonesPerVertex( chunkIndices[0] );
			m_currentSelectedChunkEntry->m_statistics.m_chunkTriangleCount	= meshView.GetChunkIndexCount( chunkIndices[0] ) / 3u;
			m_currentSelectedChunkEntry->m_statistics.m_chunkIndexCount		= meshView.GetChunkIndexCount ( chunkIndices[0] );
			m_currentSelectedChunkEntry->m_statistics.m_chunkVertexCount	= meshView.GetChunkVertexCount( chunkIndices[0] );
			m_currentSelectedChunkEntry->m_statistics.m_vertexType			= meshView.GetChunkVertexType( chunkIndices[0] );
		}
		else
		{
			m_currentSelectedChunkEntry->m_lod = "Unassigned";
		}

		return m_currentSelectedChunkEntry.Get();
	}

	String ChunksListInterop::GetItemName( Uint32 index ) const
	{
		const red::DynArray< String > materialList = GetMaterialsList();
		return materialList[ index ];
	}

	Bool ChunksListInterop::IsReadonlyItem( Uint32 index ) const
	{
		return !m_mesh->CanModify();
	}

	const red::DynArray< String > ChunksListInterop::GetMaterialsList() const
	{
		red::DynArray< String > materialNames;
		// keep the LOD chunks collapsed as you would display them for LOD0
		if( m_currentForcedLODIndex == -1 )
		{
			for (Uint32 i = 0; i < mesh::DataViewRO( *m_mesh ).GetNumLODs(); ++i)
			{
				GatherChunkMaterials( i, materialNames );
			}
		}
		else
		{
			GatherChunkMaterials( m_currentForcedLODIndex, materialNames );
		}

		return materialNames;
	}

	void ChunksListInterop::GatherChunkMaterials( const Uint32 lodIndex, red::DynArray< String >& outMaterials ) const
	{
		red::DynArray< Uint16 > lodIndices = mesh::DataViewRO( *m_mesh ).GetLODChunkIndices( lodIndex );
		for (auto chunkIndex : lodIndices)
		{
			Uint32 materialIndex;
			String materialName;
			m_materialsManager->GetMaterialAppliedOnChunk( chunkIndex, materialIndex, materialName, m_currentAppearanceIndex );

			if ( !outMaterials.Exist( materialName ) )
			{
				outMaterials.PushBack( materialName );
			}
		}
	}

	const red::DynArray< Uint32 > ChunksListInterop::GetChunkIndicesUsingMaterial( const Uint32 materialIndex ) const
	{
		red::DynArray< Uint32 > chunkIndices;

		if (m_currentForcedLODIndex == -1)
		{
			for (Uint32 i = 0; i < mesh::DataViewRO( *m_mesh ).GetNumLODs(); ++i)
			{
				GatherChunkIndices( materialIndex, i, chunkIndices );
			}
		}
		else
		{
			GatherChunkIndices( materialIndex, m_currentForcedLODIndex, chunkIndices );
		}
		return chunkIndices;
	}

	void ChunksListInterop::GatherChunkIndices( const Uint32 materialIndex, const Uint32 lodIndex, red::DynArray< Uint32 >& outIndices ) const
	{
		red::DynArray< Uint16 > lodIndices = mesh::DataViewRO( *m_mesh ).GetLODChunkIndices( lodIndex );
		for ( auto chunkIndex : lodIndices )
		{
			Uint32 chunkMaterialIndex;
			String materialName;
			m_materialsManager->GetMaterialAppliedOnChunk( chunkIndex, chunkMaterialIndex, materialName, m_currentAppearanceIndex );

			if ( chunkMaterialIndex == materialIndex )
			{
				outIndices.PushBack( chunkIndex );
			}
		}
	}

	Uint32 ChunksListInterop::GetSize() const
	{
		return GetMaterialsList().Size();
	}

	::interop::ObjectPtr ChunksListInterop::GetElement( Uint32 index )
	{
		return ::interop::ObjectPtr( RED_NEW( ChunkItem )(*this, index, m_currentAppearanceIndex, m_currentForcedLODIndex, GetActionHistory()) );
	}

	void ChunksListInterop::OnNotifyPropertyChange( const SerializableID& id, const rtti::AccessPath& path )
	{
		if (m_selectedItem > -1)
		{
			if (path.ToString().BeginsWith( "flags" ))
			{
				const red::DynArray< Uint32 > chunkIndices = GetChunkIndicesUsingMaterial( m_selectedMaterialIndex );

				mesh::DataView meshView( *m_mesh );
				for (auto c : chunkIndices)
				{
					meshView.SetChunkFlags( c, m_currentSelectedChunkEntry->m_flags );
				}
				NotifyMaterialsChange();
			}
		}

		if (path.ToString().BeginsWith( "baseMaterial" ))
		{
			RefreshSelectionView();
			NotifyMaterialsChange();
		}

		if( m_materialSelection && m_materialSelection->HasObject( id ) )
		{
			if ( !path.IsEmpty() )
			{
				NotifyMaterialsChange();
			}
		}
	}

	void ChunksListInterop::OnListChanged()
	{
		m_materialsChangedCallback();
	}

	void ChunksListInterop::OnSelection( Uint32 index )
	{
		const red::DynArray< String > materialList = GetMaterialsList();
		if( index < materialList.Size() )
		{
			const Uint32 materialIndex = m_materialsManager->GetMaterialIndex( materialList[index] );

			THandle< IMaterial > selectedMaterial = m_materialsManager->GetMaterial( materialIndex );
			m_materialSelection->SetObject( selectedMaterial.Get(), !mesh::DataViewRO( *m_mesh ).IsMaterialLocalInstance( materialIndex ) );

			m_selectedMaterialIndex = materialIndex;
			m_selectedItem = index;
			RefreshVisualization( index );
		}
		else
		{
			m_selectedMaterialIndex = -1;
			m_selectedItem = 0;
		}
	}

	void ChunksListInterop::OnDeselection( Uint32 index )
	{
		m_materialSelection->SetObject( nullptr );
		m_selectedMaterialIndex = -1;
		m_selectedItem = -1;
		RefreshVisualization();
	}

	Bool ChunksListInterop::GetMaterialInfo( Uint32 index, interop::MaterialListDescriptor& materialDescriptor ) const
	{
		const red::DynArray< String > uniqueChunkMaterialNames = GetMaterialsList();
		String materialName = uniqueChunkMaterialNames[ index ];

		const Uint32 materialIndex = m_materialsManager->GetMaterialIndex( materialName );

		materialDescriptor.m_isMultilayer		= m_materialsManager->IsMaterialMultilayer( materialIndex );
		materialDescriptor.m_isForward			= m_materialsManager->IsMaterialForwardLit( materialIndex );
		materialDescriptor.m_isLocalInstance	= m_materialsManager->IsMaterialLocalInstance( materialIndex );
		materialDescriptor.m_isTemplate			= m_materialsManager->IsMaterialTemplate( materialIndex );
		materialDescriptor.m_materialName		= materialName;

		const red::DynArray< Uint32 > chunkIndices = GetChunkIndicesUsingMaterial( materialIndex );

		if( chunkIndices.Size() > 1 )
		{
			String accumChunkIndexInfo = "<";
			String accumChunkLODInfo = "<";

			// multiple chunks per material
			mesh::DataViewRO meshView( *m_mesh );
			for( auto c : chunkIndices )
			{
				// print chunk no. and LOD index
				accumChunkIndexInfo += String::Printf( "%u, ", c );

				for ( Uint32 i = 0; i < meshView.GetNumLODs(); ++i )
				{
					red::DynArray< Uint16 > lodIndices = meshView.GetLODChunkIndices( i );

					if (lodIndices.Exist( c ))
					{
						if( !accumChunkLODInfo.Contains( String::Printf( "%u", i ) ) )
							accumChunkLODInfo += String::Printf( "%u, ", i );
						break;
					}
				}
			}

			materialDescriptor.m_chunksInfo = accumChunkIndexInfo.StringBeforeFromRight(",");
			materialDescriptor.m_chunksInfo += "> ";
			materialDescriptor.m_chunksInfo += m_materialsManager->GetMaterialShader( materialIndex );

			materialDescriptor.m_chunksLODInfo = accumChunkLODInfo.StringBeforeFromRight( "," );
			materialDescriptor.m_chunksLODInfo += ">";

			return false;
		}
		else
		{
			m_materialsManager->GetMaterialInfo( chunkIndices[0], materialIndex, materialDescriptor.m_chunksInfo, materialDescriptor.m_chunksLODInfo );
			return true;
		}
	}

	void ChunksListInterop::OnLocalInstanceChanged()
	{
		THandle< IMaterial > selectedMaterial = m_materialsManager->GetMaterial( m_selectedMaterialIndex );
		m_materialSelection->SetObject( selectedMaterial.Get(), !mesh::DataViewRO( *m_mesh ).IsMaterialLocalInstance( m_selectedMaterialIndex ) );

		m_materialsChangedCallback();
	}

	Uint32 ChunksListInterop::GetAppearanceIndex()
	{
		return m_currentAppearanceIndex;
	}

	void ChunksListInterop::GetAppearances( red::DynArray< CName >& outAppearances )
	{
		mesh::DataViewRO meshView( *m_mesh );
		const Uint32 appearanceCount = meshView.GetNumAppearances();

		for (Uint32 i = 0; i < appearanceCount; ++i)
		{
			outAppearances.PushBack( meshView.GetAppearanceName( i ) );
		}
	}

	CName ChunksListInterop::GetActiveAppearance()
	{
		return mesh::DataViewRO( *m_mesh ).GetAppearanceName( GetAppearanceIndex() );
	}

	const red::DynArray< String > ChunksListInterop::GetAvailableMaterials( const Uint32 index ) const
	{
		red::DynArray< String > materialList;
		mesh::DataViewRO meshView( *m_mesh );

		for (Uint32 i = 0; i < meshView.GetNumMaterials(); ++i)
		{
			materialList.PushBack( meshView.GetMaterialName( i ).AsChar() );
		}

		return materialList;
	}

	const String ChunksListInterop::GenerateUniqueMaterialName( const String& suggestedName ) const
	{
		return mesh::DataView( *m_mesh ).GenerateUniqueMaterialName( suggestedName );
	}

	Bool ChunksListInterop::DuplicateMaterial( const Uint32 materialIndex, const Uint32 chunkIndex, const Uint32 appearanceIndex, Uint32& addedMaterialIndex )
	{
		mesh::DataView meshView( *m_mesh );

		String materialName = String::Printf( "%hs_%d", meshView.GetAppearanceName( appearanceIndex ).AsChar(), chunkIndex );
		materialName = meshView.GenerateUniqueMaterialName( materialName );

		Bool materialAdded = GetMaterialsManager()->AddMaterial( materialName, materialIndex, true );
		if (materialAdded)
		{
			addedMaterialIndex = m_materialsManager->GetMaterialIndex( materialName );
		}

		return materialAdded;
	}

	THandle< IMaterial > ChunksListInterop::GetSelectedMaterial() const
	{
		return m_selectedMaterialIndex > -1 ? m_materialsManager->GetMaterial( m_selectedMaterialIndex ) : nullptr;
	}

	red::DynArray< THandle< IMaterial > > ChunksListInterop::GetAllMaterials() const
	{
		return mesh::DataViewRO( *m_mesh ).GetMaterials();
	}

	red::DynArray< CName > ChunksListInterop::GetAllMaterialNames() const
	{
		return mesh::DataViewRO( *m_mesh ).GetMaterialNames();
	}

	void ChunksListInterop::RemoveUnusedMaterials( red::DynArray<Uint32>* removedMaterialIndices )
	{
		mesh::DataView( *m_mesh ).RemoveUnusedMaterials( removedMaterialIndices );
	}

	Bool ChunksListInterop::ResetMaterials( const red::DynArray< THandle< IMaterial > >& newMaterials, const red::DynArray< CName >& newMaterialNames )
	{
		return mesh::DataView( *m_mesh ).ResetMaterials( newMaterials, newMaterialNames );
	}

	Bool ChunksListInterop::CopyAllAppearances()
	{
		red::DynArray< THandle< ISerializable > > objectsToCopy;
		mesh::DataViewRO meshView( *GetMesh() );

		// TBD[PB]: Should we copy all materials, or only materials actually used be appearances?
		for ( Uint32 i = 0u; i < meshView.GetNumMaterials(); ++i )
		{
			objectsToCopy.PushBack( CreateHandle< MaterialWithName >( meshView.GetMaterialName( i ), meshView, meshView.GetMaterialData( i ) ) );
		}

		for ( const auto& appearance : GetMesh()->GetAllAppearances() )
		{
			objectsToCopy.PushBack( appearance );
		}

		GrowableMemoryBuffer binaryData;
		ObjectPersistentCollectionPtr persistentCollection = ObjectSerializablePersistentCollection::CaptureObjects( objectsToCopy );

		if ( !persistentCollection->Save( IObjectPersistentCollection::EContentFormat::Binary, binaryData ) )
		{
			return false;
		}

		if ( !GClipboard->Open( CF_Blob ) )
		{
			return false;
		}

		Bool result = GClipboard->Copy( binaryData.Data(), binaryData.Size() );
		GClipboard->Close();

		return result;
	}

	Bool ChunksListInterop::PasteAppearances()
	{
		red::DynArray< Uint8 > data{ red::PoolBackend() };
		if ( !GClipboard->HasData() || !GClipboard->Paste( data ) )
		{
			RED_REPORT_ERROR( RED_LOCATION( GetMesh()->GetPath() ), "No data to paste");
			return false;
		}

		ObjectPersistentCollectionPtr persistentCollection = IObjectPersistentCollection::CreateFromBuffer( data.TypedData(), data.DataSize() );
		if ( !persistentCollection )
		{
			return false;
		}

		red::DynArray< IObjectPersistentCollection::ObjectInfo > objectInfos{ red::PoolBackend() };
		persistentCollection->UnpackObjects( ClassID<ISerializable>(), objectInfos );
		if ( objectInfos.Empty() )
		{
			RED_REPORT_ERROR( RED_LOCATION( GetMesh()->GetPath() ), "No valid data to paste" );
			return false;
		}

		red::UniquePtr< EditGroupAction > groupAction = red::CreateUniquePtr< EditGroupAction >( "Paste appearances" );

		for ( const auto& info : objectInfos )
		{
			if ( info.m_object && info.m_object->IsA< MaterialWithName >() )
			{
				THandle< MaterialWithName > material = Cast< MaterialWithName >( info.m_object );
				groupAction->AddAction( red::CreateUniquePtr< Action_AddMaterialCopy >( *this, material->GetName().AsChar(), material->GetMaterial() ) );
			}
		}

		for ( const auto& info : objectInfos )
		{
			if ( info.m_object && info.m_object->IsA< mesh::MeshAppearance >() )
			{
				THandle< mesh::MeshAppearance > meshAppearance = Cast< mesh::MeshAppearance >( info.m_object );
				groupAction->AddAction( red::CreateUniquePtr< Action_AddModelApperanceCopy >( *this, meshAppearance->GetName().AsChar(), meshAppearance ) );
			}
		}

		Bool result = GetActionHistory().Execute( std::move( groupAction ) );
		NotifyAppearancesChange();

		if ( !result )
		{
			RED_REPORT_ERROR( RED_LOCATION( GetMesh()->GetPath() ), "Pasting appearances failed." );
		}
		return result;
	}

	Bool ChunksListInterop::CopyMaterial()
	{
		if ( !GetSelectedMaterial() )
		{
			return false;
		}

		mesh::DataViewRO meshView( *GetMesh() );
		THandle< MaterialWithName > material = CreateHandle< MaterialWithName >( RED_NAME( GetItemName( m_selectedItem ) ), meshView, GetSelectedMaterial() );

		red::DynArray< THandle< ISerializable > > objectsToCopy;
		objectsToCopy.PushBack( material );

		GrowableMemoryBuffer binaryData;
		ObjectPersistentCollectionPtr persistentCollection = ObjectSerializablePersistentCollection::CaptureObjects( objectsToCopy );

		if ( !persistentCollection->Save( IObjectPersistentCollection::EContentFormat::Binary, binaryData ) )
		{
			return false;
		}

		if ( !GClipboard->Open( CF_Blob ) )
		{
			return false;
		}

		Bool result = GClipboard->Copy( binaryData.Data(), binaryData.Size() );
		GClipboard->Close();
		return result;
	}

	Bool ChunksListInterop::PasteMaterial()
	{
		red::DynArray< Uint8 > data{ red::PoolBackend() };
		if ( !GClipboard->HasData() || !GClipboard->Paste( data ) )
		{
			RED_REPORT_ERROR( RED_LOCATION( GetMesh()->GetPath() ), "No data to paste" );
			return false;
		}

		ObjectPersistentCollectionPtr persistentCollection = IObjectPersistentCollection::CreateFromBuffer( data.TypedData(), data.DataSize() );
		if ( !persistentCollection )
		{
			return false;
		}

		red::DynArray< IObjectPersistentCollection::ObjectInfo > objectInfos{ red::PoolBackend() };
		persistentCollection->UnpackObjects( ClassID<ISerializable>(), objectInfos );
		if ( objectInfos.Empty() )
		{
			RED_REPORT_ERROR( RED_LOCATION( GetMesh()->GetPath() ), "No valid data to paste" );
			return false;
		}

		red::UniquePtr< EditGroupAction > groupAction = red::CreateUniquePtr< EditGroupAction >( "Paste appearances" );

		const auto& info = objectInfos.Front();
		if ( !info.m_object || !info.m_object->IsA< MaterialWithName >() )
		{
			RED_REPORT_ERROR( RED_LOCATION( GetMesh()->GetPath() ), "Pasted data is not a material" );
			return false;
		}

		THandle< MaterialWithName > material = Cast< MaterialWithName >( info.m_object );
		groupAction->AddAction( red::CreateUniquePtr< Action_AddMaterialCopy >( *this, material->GetName().AsChar(), material->GetMaterial() ) );
		groupAction->AddAction( red::CreateUniquePtr< Action_AssignMaterial >( *this, m_selectedItem, material->GetName().AsChar() ) );

		Bool result = GetActionHistory().Execute( std::move( groupAction ) );
		NotifyMaterialsChange();

		if ( !result )
		{
			RED_REPORT_ERROR( RED_LOCATION( GetMesh()->GetPath() ), "Pasting material failed." );
		}
		return result;
	}

	red::UniquePtr< tools::Action_AddListItem > ChunksListInterop::GetAddItemAction( const String& newName )
	{
		return nullptr;
	}

	red::UniquePtr< tools::Action_RenameListItem > ChunksListInterop::GetRenameItemAction( Uint32 index, const String& newName )
	{
		return nullptr;
	}

	red::UniquePtr< tools::Action_RemoveListItem > ChunksListInterop::GetRemoveItemAction( Uint32 index )
	{
		return nullptr;
	}
}

RTTI_BEGIN_TYPE_IN_NAMESPACE( MaterialWithName, tools);
	RTTI_PARENT_TYPE( ISerializable );
	RTTI_PROPERTY( m_materialName).readOnly();
	RTTI_PROPERTY( m_localMaterial ).readOnly();
	RTTI_PROPERTY( m_externalMaterial ).readOnly();
RTTI_END_TYPE();