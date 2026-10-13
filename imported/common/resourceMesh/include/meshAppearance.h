/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../renderData/include/renderingMemory.h"
#include "../../renderData/include/renderObjectPtr.h"

class IMaterial;
class IRenderResource;
class CMesh;

namespace mesh
{
	class DataViewRO;
	class DataView;
	class DataBuilder;
	class MeshAppearance;

																					/// @todo DP: where to put this? here or in meshChunk?
	/// Definition of materials chosen for a single mesh chunk.
	/// Usually contains just a single material name, multiple names are used for layered materials.
	struct ChunkMaterials
	{
		RTTI_DECLARE_TYPE( ChunkMaterials );
		RED_USE_MEMORY_POOL( rend::PoolMesh );

		red::DynArray< CName > m_materialNames { rend::PoolMesh() };          ///< Names of materials for this chunk. Multiple entries are used for layered materials.
	};

	// Keeps the mesh appearance materials cached to avoid reloading from disk or deserializing them from memory
	// and helps sync calls to GetRenderMaterialSetup()
	class RESOURCEMESH_API AppearanceMaterialsRef : red::NonCopyable
	{
	public:
		AppearanceMaterialsRef();

		AppearanceMaterialsRef(AppearanceMaterialsRef&& other)
			: m_appearance(std::move(other.m_appearance))
		{}

		AppearanceMaterialsRef& operator=(AppearanceMaterialsRef&& rhs);

		~AppearanceMaterialsRef();

		// Get the counter to sync to guarantee that GetRenderMaterialSetup() will return a valid render resource.
		const job::Counter& GetWaitCounter() const;

		// You can use this to optimistically check if you can call GetRenderMaterialSetup() without first using GetWaitCounter()
		// and synchronizing on that.
		//
		// Can be useful where you want to avoid the overhead on the job system where it's expected to be ready the majority of the time.
		// E.g., lots of mesh nodes that all load the same mesh and appearance combination.
		//
		// Avoid using this to poll, and prefer GetWaitCounter() instead.
		Bool MustWaitForCounter() const;

		// Deprecate MustWaitForCounter(); use this for logical consistency with LoadingToken()
		Bool HasFinished() const { return m_appearance && !MustWaitForCounter(); }

		void Clear();

	private:
		friend class MeshAppearance;
		explicit AppearanceMaterialsRef(const THandle<MeshAppearance>& appearance);

		THandle<MeshAppearance> m_appearance;
	};

	/// Preset of materials defining appearance of the whole mesh.
	/// Defines materials for each chunk (possibly using multi layer materials), each material is just a name of one of the materials from mesh.
	/// The number of ChunkMaterials structures here must match the number of chunks in the mesh.
	///
	/// I left the "Mesh" prefix in this class name because "Appearance" alone seems to vague (mesh::MeshParameter is a precedent here).
	class RESOURCEMESH_API MeshAppearance : public ISerializable
	{
		RTTI_DECLARE_TYPE( MeshAppearance );
		RED_USE_MEMORY_POOL( rend::PoolMesh );

	public:
		MeshAppearance();
		MeshAppearance( const MeshAppearance& other, CName name, CMesh* meshOwner = nullptr );
		MeshAppearance( CMesh* meshOwner, CName name = c_DefaultAppearanceName );          ///< This constructor will copy chunk material names.
		MeshAppearance( const red::DynArray< CName >& matNames, CMesh* meshOwner, CName name = c_DefaultAppearanceName );
		MeshAppearance( const MeshAppearance& other ) = delete;
		virtual ~MeshAppearance();

		MeshAppearance& operator=( const MeshAppearance& other ) = delete;

		RED_INLINE Bool IsValid() const { return !m_chunkMaterials.Empty() && m_renderMaterialSetup; }

		/// Get name of this appearance.
		RED_INLINE CName GetName() const { return m_name; }										/// @todo DP: move to INL

		/// Get number of chunks this mesh appearance is prepared for.
		RED_INLINE Uint32 GetNumChunks() const { return m_chunkMaterials.Size(); }										/// @todo DP: move to INL

		/// Get the mesh that owns this appearance.
		RED_INLINE CMesh* GetMesh() const { return m_meshOwner; }

		/// Get number of materials in a given chunk. This will be more than one only if this chunk uses multilayer material.
		RED_INLINE Uint8 GetChunkMaterialsCount( Uint32 chunkIndex ) const { return 1; }		// legacy

		RED_INLINE CName GetChunkMaterialName( Uint32 chunkIndex ) const { return m_chunkMaterials[ chunkIndex ]; }

		/// Get handle to the material in a chunk.
		THandle< IMaterial > GetChunkMaterialData( Uint32 chunkIndex ) const;

		RED_NODISCARD AppearanceMaterialsRef IssueLoadRenderMaterialSetup(io::EAsyncPriority priority = io::EAsyncPriority::eAsyncPriority_Normal);

		const THandle< IMaterial >& GetMaterialDataByName(CName materialName) const;

		/// Get the material setup data for the mesh. Synchronize with IssueLoadRenderMaterialSetup().
		const TRenderPtr<IRenderResource>& GetRenderMaterialSetup() const;

		/// Get tags assigned to this appearance.
		RED_INLINE const red::DynArray< CName >& GetTags() const { return m_tags; }
		void SetTags( const red::DynArray< CName>& tags );

		/// Default name for appearance: "default".
		/// It is used by mesh so we always have some preferred appearance to choose when none was explicitly defined.
		static const CName c_DefaultAppearanceName;
		/// Default name for appearance in a string representation: "default".
		static const char c_DefaultAppearanceNameString[];

		static const TRenderPtr<IRenderResource>& StaticGetFallbackMaterialForAppearanceError();

		tools::EditorObjectID GetEditorObjectID() const override;

		red::DynArray< res::ResourcePath > CollectDependencies() const;
        red::DynArray< THandle<IMaterial> > Cook_GetLocalMaterials() const;

	private:
		const job::Counter& GetWaitCounter() const;


		Bool ShouldPinRenderMaterialSetup() const;

		void ReleaseRenderMaterialSetup();

		void ForceReloadRenderMaterialSetup(Bool preloadResources, Bool flushWaitCounter);

		// Re(create) internal render material setup
		// NOTE TO FRIEND CLASSES: Call ForceReloadRenderMaterialSetup() instead of this directly
		void FinishRecreateRenderMaterialSetup_NoLock(Bool preloadResources);

		// Editor / cooking
		virtual void OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& value ) override final;
		virtual Bool OnPropertyTypeMismatch( CName propertyName, const rtti::Property* existingProperty, const rtti::Variant& readValue ) override;


		CName m_name;                                      ///< Name of the appearance.
		red::DynArray< CName > m_chunkMaterials{ rend::PoolMesh() };  ///< The size of this array must always match the number of chunks in the mesh.

		TRenderPtr<IRenderResource> m_renderMaterialSetup;

		CMesh* m_meshOwner;                                ///< Mesh that owns this appearance. We need this for editing purposes and to access materials inside CMesh.

		red::DynArray< CName > m_tags{ rend::PoolMesh() };                     ///< We can tag the appearance, i.e. for Audio needs.

		red::Map< CName, THandle< IMaterial > > m_materialsCache; /// Storage for loaded materials to avoid lazily reloading from disk

		job::Counter m_waitCounter;
		mutable red::RWSpinLock m_loadRenderMaterialSetupLock;

		Uint32 m_renderMaterialSetupRefCount;

		struct Latch
		{
		public:
			Latch()
				: m_value(false)
			{}

			explicit operator Bool() const { return m_value; }

			void Set()
			{
				m_value = true;
			}

		private:
			Bool m_value;
		};

		// Once set, should stay set, otherwise can break assumptions to optimize m_renderMaterialSetupRefCount
		// m_renderMaterialSetupRefCount should be neither incremented nor decremented if set
		Latch m_isRenderMaterialSetupPinned;

		friend class ::CMesh;
		friend class AppearanceToken;
		friend class mesh::DataView;
		friend class mesh::DataViewRO;
		friend class mesh::DataBuilder;
		friend class AppearanceMaterialsRef;
	};

} // mesh
