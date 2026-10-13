#pragma once

#include "meshMaterialList.h"

#include "../../../common/interop/include/interopUtilsManual.h"
#include "../../../common/redReflection/include/serializableValueNotifier.h"

#include "../../../backend/backendEngine/include/interopList.h"

namespace mesh
{
	class IMaterialsListInterface;
}

namespace tools
{
	class IMeshVisualizationContainer;

	class MaterialsListInterop : public InteropList
	{
	public:
		typedef std::function<void(Bool)> TCallback;

		MaterialsListInterop( IEditActionHistory& actionHistory, ObjectCollection* sharedSelection, const THandle< CMesh > mesh, TCallback materialsChangedCallback );
		virtual ~MaterialsListInterop();

		void SetMesh(const THandle<CMesh>& mesh);
		const THandle< CMesh >& GetMesh() const { return m_mesh; }

		void AttachVisualizationContainer(IMeshVisualizationContainer* visContainer);
		void DetachVisualizationContainer(IMeshVisualizationContainer* visContainer);

		red::SharedPtr<mesh::IMaterialsListInterface> GetMaterialsManager() { return m_materialsManager; }

		virtual ISerializable* GetItem( Uint32 index ) const override;
		virtual String GetItemName( Uint32 index ) const override;
		virtual Bool IsReadonlyItem( Uint32 index ) const override;

		Bool IsMaterialLocalInstance( Uint32 index ) const;
		Bool IsMaterialTemplate( Uint32 index ) const;
		Bool IsMaterialMultilayer( Uint32 index ) const;
		Bool IsMaterialForwardLit( Uint32 index ) const;

		THandle< IMaterial > GetSelectedMaterial() const;
		red::DynArray< THandle< IMaterial > > GetMaterials() const;
		red::DynArray< CName > GetMaterialNames() const;

		Bool ResetMaterials( const red::DynArray< THandle< IMaterial > >& newMaterials, const red::DynArray< CName >& newMaterialNames );
		void RemoveUnusedMaterials( red::DynArray<Uint32>* removedMaterialIndices = nullptr );
		Bool RemoveMaterial( Uint32 removedMaterialIndices );

		void OnLocalInstanceChanged();

		void NotifyThatAppearancesChanged();        // Materials list uses filtering based on mesh appearances, we need to be aware of their changes.
		void NotifyThatMaterialChanged();

	protected:
		virtual Uint32 GetSize() const override;
		virtual ::interop::ObjectPtr GetElement( Uint32 ind ) override;

		virtual void OnItemPropertyChanged( const String& name ) override;
		virtual void OnSelection( Uint32 index ) override;
		virtual void OnDeselection( Uint32 index ) override;
		virtual void OnListChanged() override;

	private:
		void RegisterInterop();			/// @todo DP: maybe we should merge this with base class InteropList::RegisterInterop, make it virtual?

		void RefreshVisualization( Int32 index = -1 );

		virtual red::UniquePtr< Action_AddListItem > GetAddItemAction( const String& name ) override;
		virtual red::UniquePtr< Action_RenameListItem > GetRenameItemAction( Uint32 index, const String& newName ) override;
		virtual red::UniquePtr< Action_RemoveListItem > GetRemoveItemAction( Uint32 index ) override;

		THandle< CMesh > m_mesh;

		Int32 m_selectedMaterial;
		red::SharedPtr<mesh::IMaterialsListInterface> m_materialsManager;
		TCallback m_materialsChangedCallback;
		
		interop::InteropNotifier m_appearancesChanged;
		interop::InteropNotifier m_materialsChanged;

		red::DynArray<IMeshVisualizationContainer*> m_visualizationContainers;
	};

}