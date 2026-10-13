
#include "build.h"
#include "materialsListInterop.h"
#include "meshStructure.h"
#include "meshStructureVisualization.h"
#include "meshMaterialList.h"
#include "meshDataView.h"
#include "../../backendEngine/include/backendEnginePublic.h"
#include "../../backendEngine/include/editActionHistory.h"
#include "../../backendEngine/include/resourceImportMetadata.h"
#include "../../backendEngine/include/resourceMetadata.h"
#include "../../../common/interop/include/interopUtilsRTTI.h"
#include "../../../common/interop/include/interopInterchange.h"
#include "../../../common/resourceMaterial/include/material.h"
#include "../../../common/resourceMaterial/include/materialTemplate.h"
#include "../../../common/resourceMaterial/include/materialInstance.h"
#include "../../../common/resourceMaterial/include/materialCompilationManager.h"
#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/redReflection/include/rttiPathParser.h"
#include "../../../common/redReflection/include/resourceUtils.h"

namespace tools
{
	//////////////////////////////////////////////////////////////////////////////////////////////
	class Action_AddMaterial2 : public Action_AddListItem
	{
	public:
		Action_AddMaterial2( MaterialsListInterop& list, const Uint32 baseMaterialInd, const String& name, Bool materialCopy = false )
			: Action_AddListItem( list.SharedFromThis() )
			, m_materialsManager( list.GetMaterialsManager() )
			, m_materialName( name )
			, m_baseMaterialInd( baseMaterialInd )
			, m_materialCopy( materialCopy )
		{}

		Action_AddMaterial2( MaterialsListInterop& list, const THandle<IMaterial>& newMaterial, const String& name )
			: Action_AddListItem( list.SharedFromThis() )
			, m_materialsManager( list.GetMaterialsManager() )
			, m_materialName( name )
			, m_baseMaterialInd( 0 )
			, m_materialCopy( false )
			, m_newMaterial( newMaterial )
		{}

		virtual String GetDescription() const override { return "Add material"; }

		virtual Bool AddItem() override
		{
			if ( m_newMaterial != nullptr )
			{
				return m_materialsManager->AddMaterial( m_newMaterial, m_materialName );
			}
			else
			{
				return m_materialsManager->AddMaterial( m_materialName, m_baseMaterialInd, m_materialCopy );
			}
		}

		virtual Bool RemoveItem() override
		{
			Int32 matInd = m_materialsManager->GetMaterialIndex( m_materialName );
			if ( matInd > 0 )
			{
				if ( m_materialsManager->RemoveMaterial( matInd ) )
				{
					return true;
				}
			}
			return false;
		}

		virtual Uint32 GetItemIndex() const override
		{
			return m_materialsManager->GetMaterialIndex( m_materialName );
		}

	private:
		red::SharedPtr< mesh::IMaterialsListInterface > m_materialsManager;
		THandle<IMaterial> m_newMaterial;
		String m_materialName;
		Uint32 m_baseMaterialInd;
		Bool m_materialCopy;
	};

	//////////////////////////////////////////////////////////////////////////////////////////////
	class Action_RemoveMaterial : public Action_RemoveListItem
	{
	public:
		Action_RemoveMaterial( MaterialsListInterop& list, const Uint32 index )
			: Action_RemoveListItem( list.SharedFromThis(), index )
			, m_materialsManager( list.GetMaterialsManager() )
		{
			m_materialName = m_materialsManager->GetMaterialName( index );
			m_material = m_materialsManager->GetMaterial( index );
		}

		virtual String GetDescription() const override { return "Remove material"; }

		virtual Bool PerformRemoval() override
		{
			Int32 matInd = m_materialsManager->GetMaterialIndex( m_materialName );
			if ( matInd >= 0 && m_materialsManager->GetNumMaterials() > 1 /* Do not remove last material */  )
			{
				return m_materialsManager->RemoveMaterial( matInd );
			}
			return false;
		}

		virtual Bool RevertRemoval() override
		{
			return m_materialsManager->AddMaterial( m_material, m_materialName, GetItemIndex() );
		}

		virtual Uint32 GetItemIndex() const override
		{
			return m_materialsManager->GetMaterialIndex( m_materialName );
		}

	private:
		red::SharedPtr< mesh::IMaterialsListInterface > m_materialsManager;
		String m_materialName;
		THandle< IMaterial > m_material;
	};

	//////////////////////////////////////////////////////////////////////////////////////////////
	class Action_RenameMaterial : public Action_RenameListItem
	{
	public:
		Action_RenameMaterial( const String& newName, MaterialsListInterop& list, const Int32 index )
			: Action_RenameListItem( list.SharedFromThis() )
			, m_newMaterialName( newName )
			, m_materialsManager( list.GetMaterialsManager() )
		{
			m_materialName = m_materialsManager->GetMaterialName( index );
		}

		Action_RenameMaterial( const String& newName, MaterialsListInterop& list, const String& materialName )
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
			if ( matInd >= 0 )
			{
				return m_materialsManager->RenameMaterial( matInd, m_newMaterialName );
			}
			return false;
		}

		virtual Bool RevertRename() override
		{
			Int32 matInd = m_materialsManager->GetMaterialIndex( m_newMaterialName );
			if ( matInd >= 0 )
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
	/// This action can create or remove material instance.
	class Action_CreateMaterialInstance : public IEditAction
	{
	public:
		enum Operation : Uint8
		{
			CREATE,
			REMOVE
		};

		Action_CreateMaterialInstance( MaterialsListInterop& list, const Int32 index, Operation operation = CREATE )
			: IEditAction()
			, m_materialsList( list )
			, m_index( index )
			, m_operation( operation )
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

		Bool PerformOperation(Bool reverse)
		{
			if ( m_materialsList.IsMaterialLocalInstance( m_index ) ^ reverse )
				return false;

			Bool ret = reverse ? m_materialsList.GetMaterialsManager()->RemoveMaterialLocalInstance( m_index )
			                   : m_materialsList.GetMaterialsManager()->CreateMaterialLocalInstance( m_index );

			m_materialsList.OnLocalInstanceChanged();
			return ret;
		}

	private:
		MaterialsListInterop& m_materialsList;
		const Int32 m_index;
		Operation m_operation;
	};

	//////////////////////////////////////////////////////////////////////////////////////////////
	/// This action can remove unused materials.
	class Action_RemoveUnusedMaterials : public IEditAction
	{
	public:
		Action_RemoveUnusedMaterials( MaterialsListInterop& list )
			: IEditAction()
			, m_materialsList( list )
			, m_prevMaterials( list.GetMaterials() )
			, m_prevMaterialNames( list.GetMaterialNames() )
		{
		}

		virtual String GetDescription() const override { return "Remove unused materials"; }

		virtual Bool Do() override final
		{
			red::DynArray< Uint32 > removedMaterialIndices;
			m_materialsList.RemoveUnusedMaterials( &removedMaterialIndices );
			for ( Uint32 materialIndex : removedMaterialIndices )
			{
				if ( m_materialsList.IsItemSelected( materialIndex ) )
					m_materialsList.DeselectItem( materialIndex );
			}
			m_materialsList.NotifyListChanged();
			return true;
		}

		virtual Bool Undo() override final
		{
			Bool result = m_materialsList.ResetMaterials( m_prevMaterials, m_prevMaterialNames );
			m_materialsList.NotifyListChanged();
			return result;
		}

	private:
		MaterialsListInterop& m_materialsList;
		red::DynArray< THandle< IMaterial > > m_prevMaterials;  // Mesh materials before removing unused ones.
		red::DynArray< CName > m_prevMaterialNames;             // Material names before removing unused ones.

	};

	//////////////////////////////////////////////////////////////////////////////////////////////
	class Material : public InteropListItem
	{
	public:
		explicit Material( MaterialsListInterop& materialsList, Uint32 index, IEditActionHistory& actionHistory )
			: InteropListItem( index, materialsList.SharedFromThis(), actionHistory, "Material")
		{			
			::interop::utils::Object::RegisterFunctor<Bool, String>( "addMaterial", 
				[&materialsList, index, &actionHistory](interop::IContext& ctx, const String& materialName)
				{
					Bool result = actionHistory.Execute(red::CreateUniquePtr< Action_AddMaterial2 >( materialsList, index, materialName ) );
					if ( !result )
						ctx.ThrowException( "Couldn't add material. Possible reasons: material with a given name already exists or wrong material index was passed." );
					return result;
				} );

			::interop::utils::Object::RegisterFunctor<Bool>( "recompileMaterial", 
				[&materialsList, index](interop::IContext& ctx)
			{
#ifndef NO_RUNTIME_MATERIAL_COMPILATION
				if ( THandle< IMaterial > material = materialsList.GetSelectedMaterial() )
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
				[&materialsList, index](interop::IContext& ctx)
			{
#ifndef NO_RUNTIME_MATERIAL_COMPILATION
				if ( THandle< IMaterial > material = materialsList.GetSelectedMaterial() )
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

			::interop::utils::Object::RegisterFunctor< Bool >( "refreshMaterialParameters",
				[ &materialsList, index ]( interop::IContext& ctx )
			{
				if ( THandle< IMaterial > material = materialsList.GetSelectedMaterial() )
				{
					auto materialInstance = Cast< CMaterialInstance >( material );
					if ( materialInstance )
					{
						if (materialInstance->UpdateParametersListBasedOnTemplate())
						{
							materialInstance->MarkModified();
							materialsList.OnLocalInstanceChanged();
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


			::interop::utils::Object::RegisterFunctor<Bool, String>( "copyMaterial", 
				[&materialsList, index, &actionHistory](interop::IContext& ctx, const String& materialName)
			{
				Bool result = actionHistory.Execute(red::CreateUniquePtr< Action_AddMaterial2 >( materialsList, index, materialName, true ) );
				if ( !result )
					ctx.ThrowException( "Couldn't copy material. Possible reasons: material with a given name already exists or wrong material index was passed." );
				return result;
			} );

			::interop::utils::Object::RegisterFunctor<Bool>( "isMaterialInstance",
				[&materialsList, index]( interop::IContext& ctx )
			{
				return materialsList.IsMaterialLocalInstance( index );
			} );

			::interop::utils::Object::RegisterValueRO< interop::MaterialListDescriptor >( "materialDescriptor",
				[&materialsList, index]()
			{
				interop::MaterialListDescriptor materialListDescriptor;
				materialListDescriptor.m_isLocalInstance = materialsList.IsMaterialLocalInstance( index );
				materialListDescriptor.m_isTemplate = materialsList.IsMaterialTemplate( index );
				materialListDescriptor.m_isMultilayer = materialsList.IsMaterialMultilayer( index );
				materialListDescriptor.m_isForward = materialsList.IsMaterialForwardLit( index );
				materialListDescriptor.m_numLayers = 0;													/// @todo DP: correct this
				return materialListDescriptor;
			} );

			::interop::utils::Object::RegisterFunctor<Bool>( "createLocalInstance",
				[&materialsList, index, &actionHistory]( interop::IContext& ctx )
			{
				auto action = red::CreateUniquePtr< Action_CreateMaterialInstance >( materialsList, index, Action_CreateMaterialInstance::CREATE );
				Bool result = actionHistory.Execute( std::move( action ) );
				if ( !result )
					ctx.ThrowException( "Couldn't create material instance. Possible reasons: material was already an instance or wrong material index was passed." );
				return result;
			} );

			::interop::utils::Object::RegisterFunctor<Bool>( "removeLocalInstance",
				[&materialsList, index, &actionHistory]( interop::IContext& ctx )
			{
				auto action = red::CreateUniquePtr< Action_CreateMaterialInstance >(materialsList, index, Action_CreateMaterialInstance::REMOVE );
				Bool result = actionHistory.Execute( std::move( action ) );
				if ( !result )
					ctx.ThrowException( "Couldn't remove material instance. Possible reasons: material was not an instance or wrong material index was passed." );
				return result;
			} );

			::interop::utils::Object::RegisterFunctor<Bool, String>( "reimportLayersFromJSON",
				[&materialsList, index, &actionHistory]( interop::IContext& ctx, const String& jsonFilePath )
			{
				if ( !materialsList.GetMaterialsManager()->IsMaterialMultilayer( index ) )
				{
					ctx.ThrowException( "Couldn't reimport layers from JSON file. This is not a multilayer material." );
					return false;
				}
				return materialsList.GetMaterialsManager()->ReimportLayersFromJSON( index, jsonFilePath );
			} );

			::interop::utils::Object::RegisterFunctor<String>( "getImportPathForJSON", [&materialsList, index, &actionHistory]( interop::IContext& ctx )
			{
				String jsonImportPath;
				if ( THandle<res::ResourceMetadata> meta = res::ResourceMetadata::Get( materialsList.GetMesh() ) )
				{
					if ( const THandle<res::ResourceImportMetadata>& importMeta = meta->GetImportData() )
					{
						jsonImportPath = importMeta->GetIntermediatePath();
						jsonImportPath = red::paths::ExtractParentPath( jsonImportPath );
					}
				}
				return jsonImportPath;
			} );
		}
	};

	//////////////////////////////////////////////////////////////////////////////////////////////
	MaterialsListInterop::MaterialsListInterop( IEditActionHistory& actionHistory, ObjectCollection* sharedSelection, const THandle< CMesh > mesh, TCallback materialsChangedCallback )
		: InteropList( actionHistory, sharedSelection, InteropListSetupInfo( true, true, true ) )
		, m_mesh( mesh )
		, m_selectedMaterial(-1 )
		, m_materialsChangedCallback( materialsChangedCallback )
	{
		m_materialsManager.Reset(RED_NEW( MeshMaterialsList )( m_mesh ) );
		RegisterInterop();
	}

	MaterialsListInterop::~MaterialsListInterop()
	{
		m_materialsManager.Reset();
	}

	void MaterialsListInterop::SetMesh(const THandle<CMesh>& mesh)
	{
		m_mesh = mesh;
		m_materialsManager->SetMesh(mesh);
	}

	Uint32 MaterialsListInterop::GetSize() const
	{
		return m_materialsManager->GetNumMaterials();
	}

	::interop::ObjectPtr MaterialsListInterop::GetElement( Uint32 index )
	{
		return ::interop::ObjectPtr(RED_NEW( Material )( *this, index, GetActionHistory() ) );
	}

	ISerializable* MaterialsListInterop::GetItem( Uint32 index ) const
	{
		return m_materialsManager->GetMaterialData( index );
	}

	String MaterialsListInterop::GetItemName( Uint32 index ) const
	{
		return m_materialsManager->GetMaterialName( index );
	}

	Bool MaterialsListInterop::IsReadonlyItem( Uint32 index ) const
	{
		return !m_materialsManager->IsMaterialLocalInstance( index );
	}

	Bool MaterialsListInterop::IsMaterialLocalInstance( Uint32 index ) const
	{
		return m_materialsManager->IsMaterialLocalInstance( index );
	}

	Bool MaterialsListInterop::IsMaterialTemplate( Uint32 index ) const
	{
		return m_materialsManager->IsMaterialTemplate( index );
	}

	Bool MaterialsListInterop::IsMaterialMultilayer( Uint32 index ) const
	{
		return m_materialsManager->IsMaterialMultilayer( index );
	}

	Bool MaterialsListInterop::IsMaterialForwardLit( Uint32 index ) const
	{
		return m_materialsManager->IsMaterialForwardLit( index );
	}

	THandle< IMaterial > MaterialsListInterop::GetSelectedMaterial() const
	{
		return m_selectedMaterial > -1 ? m_materialsManager->GetMaterial( m_selectedMaterial ) : nullptr;
	}

	red::DynArray< THandle< IMaterial > > MaterialsListInterop::GetMaterials() const
	{
		return mesh::DataViewRO( *m_mesh ).GetMaterials();
	}

	red::DynArray< CName > MaterialsListInterop::GetMaterialNames() const
	{
		return mesh::DataViewRO( *m_mesh ).GetMaterialNames();
	}

	Bool MaterialsListInterop::ResetMaterials( const red::DynArray< THandle< IMaterial > >& newMaterials, const red::DynArray< CName >& newMaterialNames )
	{
		return mesh::DataView( *m_mesh ).ResetMaterials( newMaterials, newMaterialNames );
	}

	void MaterialsListInterop::RemoveUnusedMaterials( red::DynArray<Uint32>* removedMaterialIndices )
	{
		mesh::DataView( *m_mesh ).RemoveUnusedMaterials( removedMaterialIndices );
	}

	Bool MaterialsListInterop::RemoveMaterial( Uint32 removedMaterialIndices )
	{
		auto action = GetRemoveItemAction( removedMaterialIndices );
		Bool result = GetActionHistory().Execute( std::move( action ) );
		return result;
	}

	void MaterialsListInterop::OnLocalInstanceChanged()
	{
		SetSelection( GetItem( m_selectedMaterial ), IsReadonlyItem( m_selectedMaterial ) );
		m_materialsChangedCallback( true );
	}


	void MaterialsListInterop::OnListChanged()
	{
		m_materialsChangedCallback( false );
	}

	void MaterialsListInterop::OnSelection( Uint32 index )
	{
		m_selectedMaterial = index;
		RefreshVisualization( index );
	}

	void MaterialsListInterop::OnDeselection( Uint32 index )
	{
		m_selectedMaterial = -1;
		RefreshVisualization();
	}

	void MaterialsListInterop::AttachVisualizationContainer(IMeshVisualizationContainer* visContainer)
	{
		m_visualizationContainers.PushBack(visContainer);
		RefreshVisualization();
	}

	void MaterialsListInterop::DetachVisualizationContainer(IMeshVisualizationContainer* visContainer)
	{
		if (m_visualizationContainers.Exist(visContainer))
		{
			m_visualizationContainers.Remove(visContainer);
		}
	}

	void MaterialsListInterop::RefreshVisualization( Int32 index /* = -1 */ )
	{
		mesh::DataViewRO meshView( *m_mesh );

		for (auto* vis : m_visualizationContainers)
		{
			vis->RemoveAllVisualizations();
			vis->SetForcedLOD(-1);
			vis->SetMeshVisibility(true);

			if ( m_selectedMaterial >= 0 )
			{
				const auto materialName = meshView.GetMaterialName( index );
				vis->AttachVisualization( red::CreateUniquePtr< MeshVisualization_Triangles >( m_mesh, -1, materialName ) );
			}
		}
	}

	red::UniquePtr< tools::Action_AddListItem > MaterialsListInterop::GetAddItemAction( const String& newName )
	{
		return red::CreateUniquePtr< Action_AddMaterial2 >( *this, 0, newName );
	}

	red::UniquePtr< tools::Action_RenameListItem > MaterialsListInterop::GetRenameItemAction( Uint32 index, const String& newName )
	{
		return red::CreateUniquePtr< Action_RenameMaterial >( newName, *this, index );
	}

	red::UniquePtr< tools::Action_RemoveListItem > MaterialsListInterop::GetRemoveItemAction( Uint32 index )
	{
		return red::CreateUniquePtr< Action_RemoveMaterial >( *this, index );
	}

	void MaterialsListInterop::OnItemPropertyChanged( const String& name )
	{
		if ( name.BeginsWith( "baseMaterial" ) )
		{
			RefreshSelectionView();
		}

		if ( m_selectedMaterial > -1 )
		{
			GetMaterialsManager()->UpdateMaterialRenderResource( m_selectedMaterial );
		}

		mesh::DataView meshView( *m_mesh );
		meshView.ForceRecreateMaterialSetup();
		meshView.Apply();

		RefreshVisualization();

		m_materialsChangedCallback( true );
	}

	void MaterialsListInterop::NotifyThatAppearancesChanged()
	{
		m_appearancesChanged.Notify();
	}

	void MaterialsListInterop::NotifyThatMaterialChanged()
	{
		m_materialsChanged.Notify();
	}

	void MaterialsListInterop::RegisterInterop()
	{
		::interop::utils::Object::RegisterFunctor<Bool>( "removeUnusedMaterials",
			[this]( interop::IContext& ctx )
		{
			auto action = red::CreateUniquePtr< Action_RemoveUnusedMaterials >( *this );
			Bool result = GetActionHistory().Execute( std::move( action ) );
			if ( !result )
				ctx.ThrowException( "Couldn't remove unused materials." );
			return result;
		} );

		::interop::utils::Object::RegisterFunctor<Int32, String>( "getMaterialIndex",
			[this]( interop::IContext& ctx, const String& materialName )
		{
			return GetMaterialsManager()->GetMaterialIndex( materialName );
		} );

		::interop::utils::Object::RegisterFunctor<Bool, String, String>( "addMaterial",
			[this]( interop::IContext& ctx, const String& materialName, const String& materialPathString )
		{
			TResRef<IMaterial> newMaterialRes( res::ResourcePath::Build( materialPathString ) );
			THandle<IMaterial> newMaterial = newMaterialRes.Get();
			if ( newMaterial == nullptr )
			{
				ctx.ThrowException( "Couldn't add material. Unable to load the material resource." );
				return false;
			}

			Bool result = GetActionHistory().Execute( red::CreateUniquePtr< Action_AddMaterial2 >( *this, newMaterial, materialName ) );
			if ( !result )
				ctx.ThrowException( "Couldn't add material. Possible reasons: material with a given name already exists or wrong material index was passed." );
			return result;
		} );

		::interop::utils::Object::RegisterFunctor< red::DynArray<String> >( "getMaterialFileExtensions",
			[this]( interop::IContext& ctx )
		{
			red::DynArray<String> fileExtensions;
			fileExtensions.PushBack( CMaterialTemplate::GetFileExtension() );
			fileExtensions.PushBack( CMaterialTemplate::GetDeprecatedFileExtension() );
			fileExtensions.PushBack( CMaterialInstance::GetFileExtension() );
			fileExtensions.PushBack( CMaterialInstance::GetDeprecatedFileExtension() );
			return fileExtensions;
		} );

		::interop::utils::Object::RegisterFunctor< red::DynArray< String > >( "getAppearanceNames",
			[this]( interop::IContext& ctx )
		{
			red::DynArray< String > appearancesNames;
			mesh::DataViewRO meshView( *m_mesh );
			for ( Uint32 i = 0; i < meshView.GetNumAppearances(); ++i )
				appearancesNames.PushBack( meshView.GetAppearanceName( i ).AsChar() );
			return appearancesNames;
		} );

		::interop::utils::Object::RegisterFunctor< void, String >( "setAppearanceFilter",
			[this]( interop::IContext& ctx, const String& appearanceFilterName )
		{
			DeselectAllItems();										/// @todo DP: deselect only when needed

			if ( appearanceFilterName == "All appearances" )
			{
				m_materialsManager.Reset( RED_NEW( MeshMaterialsList )(m_mesh) );
				ResetSetupInfo( InteropListSetupInfo( true, true, true ) );
			}
			else
			{
				CName appearanceName = RED_NAME( appearanceFilterName );
				THandle< mesh::MeshAppearance > appearance = m_mesh->GetAppearanceSafe( appearanceName );
				m_materialsManager.Reset( RED_NEW( MeshAppearanceMaterialsList )(m_mesh, appearance, [this]()
				{
					OnLocalInstanceChanged();
				} ) );
				ResetSetupInfo( InteropListSetupInfo( false, false, false ) );
			}

			NotifyListChanged( true );
		} );

		::interop::utils::Object::RegisterNotifier( "_onAppearancesChanged", m_appearancesChanged );
		::interop::utils::Object::RegisterNotifier( "_onMaterialsChanged", m_materialsChanged );
	}

}