#pragma once

#include "meshMaterialList.h"

#include "../../../common/interop/include/interopUtilsManual.h"
#include "../../../common/redReflection/include/serializableValueNotifier.h"

#include "../../../backend/backendEngine/include/interopList.h"
#include "../../../common/interop/include/interopInterchange.h"

namespace mesh
{
	class IMaterialsListInterface;

	struct ChunkStats
	{
		RTTI_DECLARE_TYPE( ChunkStats );

		ChunkStats() {};
		~ChunkStats() {};
		
		Uint32			m_chunkIndexCount = 0;
		Uint32			m_chunkVertexCount = 0;
		Uint32			m_chunkTriangleCount = 0;
		Uint32			m_bonesPerVertex = 0;
		EMeshVertexType m_vertexType = EMeshVertexType::MVT_StaticMesh;
	};

	struct ChunkEntry : public ISerializable
	{
		RTTI_DECLARE_TYPE( ChunkEntry );

		ChunkEntry() {};
		~ChunkEntry() {};

		String			m_lod;
		ChunkFlags		m_flags;
		ChunkStats		m_statistics;
	};
}

namespace tools
{
	class ObjectCollection;
	class IMeshVisualizationContainer;
	
	typedef std::function<void( void )> TCallback;
	typedef std::function<void( CName )> TActivationCallback;

	typedef Uint16 ChunkIndex;

	class ChunksListInterop : public InteropList
	{
		friend class MaterialsListInterop;

	public:
		ChunksListInterop( IEditActionHistory& actionHistory, ObjectCollection* sharedSelection, const THandle< CMesh > mesh, TCallback meshChangedCallback, TCallback appearancesChangedCallback, TActivationCallback appearanceActivatedCallback );
		virtual ~ChunksListInterop();

		virtual ISerializable* GetItem( Uint32 index ) const override;
		virtual String GetItemName( Uint32 index ) const override;
		virtual Bool IsReadonlyItem( Uint32 index ) const override;

		// returns false when multiple materials found in this (chunk) index
		Bool GetMaterialInfo( Uint32 index, interop::MaterialListDescriptor& materialDescriptor ) const;
		
		const red::DynArray< String > GetMaterialsList() const;
		const red::DynArray< Uint32 > GetChunkIndicesUsingMaterial( const Uint32 materialIndex ) const;

		THandle< IMaterial > GetSelectedMaterial() const;
		red::DynArray< THandle< IMaterial > > GetAllMaterials() const; 
		red::DynArray< CName > GetAllMaterialNames() const;
		Bool ResetMaterials( const red::DynArray< THandle< IMaterial > >& newMaterials, const red::DynArray< CName >& newMaterialNames );

		Uint32 GetAppearanceIndex();
		void GetAppearances( red::DynArray< CName >& outAppearances );
		CName GetActiveAppearance();
		
		const red::DynArray< String > GetAvailableMaterials( const Uint32 index ) const;

		const String GenerateUniqueMaterialName( const String& suggestedName ) const;
		Bool DuplicateMaterial( const Uint32 materialIndex, const Uint32 chunkIndex, const Uint32 appearanceIndex, Uint32& addedMaterialIndex );
		void RemoveUnusedMaterials( red::DynArray<Uint32>* removedMaterialIndices );

		void OnLocalInstanceChanged();
		void NotifyAppearancesChange();
		void NotifyMaterialsChange();
		void NotifyChange( const Int32 forcedLOD = -1 );

		void SetMesh( const THandle<CMesh>& mesh );
		void OnMaterialAdded( const String& baseMaterialName, const String& resultingNewMaterialName );
		
		void AttachVisualizationContainer( IMeshVisualizationContainer* visContainer );
		void DetachVisualizationContainer( IMeshVisualizationContainer* visContainer );

		red::SharedPtr<mesh::IMaterialsListInterface> GetMaterialsManager() { return m_materialsManager; }

		RED_INLINE const THandle< CMesh >& GetMesh() { return m_mesh; }

		Bool CopyAllAppearances();
		Bool PasteAppearances();

		Bool CopyMaterial();
		Bool PasteMaterial();

	protected:
		virtual Uint32 GetSize() const override;
		virtual ::interop::ObjectPtr GetElement( Uint32 ind ) override;

		virtual void OnNotifyPropertyChange( const SerializableID& id, const rtti::AccessPath& path ) override;
		virtual void OnSelection( Uint32 index ) override;
		virtual void OnDeselection( Uint32 index ) override;
		virtual void OnListChanged() override;

	private:
		THandle< CMesh > m_mesh;
		ObjectCollection* m_materialSelection;

		virtual red::UniquePtr< Action_AddListItem > GetAddItemAction( const String& name ) override;
		virtual red::UniquePtr< Action_RenameListItem > GetRenameItemAction( Uint32 index, const String& newName ) override;
		virtual red::UniquePtr< Action_RemoveListItem > GetRemoveItemAction( Uint32 index ) override;

		void RegisterInterop();
		void RefreshVisualization( Int32 index = -1 );

		void GatherChunkIndices( const Uint32 materialIndex, const Uint32 lodIndex, red::DynArray< Uint32 >& outIndices ) const;
		void GatherChunkMaterials( const Uint32 lodIndex, red::DynArray< String >& outMaterials ) const;

		Int32 m_selectedMaterialIndex;
		Int32 m_selectedItem;
		Int32 m_currentForcedLODIndex;
		Uint32 m_currentAppearanceIndex;
		THandle< mesh::ChunkEntry > m_currentSelectedChunkEntry;

		red::SharedPtr<mesh::IMaterialsListInterface> m_materialsManager;
		TCallback m_materialsChangedCallback;
		TCallback m_appearancesChangedCallback;
		TActivationCallback m_appearanceActivatedCallback;

		interop::InteropNotifier m_appearancesChanged;
		interop::InteropNotifier m_materialsChanged;

		red::DynArray<IMeshVisualizationContainer*> m_visualizationContainers;
	};
}