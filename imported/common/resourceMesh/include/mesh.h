/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../redReflection/include/streamedResource.h"
#include "../../renderData/include/gpuWrapApiVertexPacking.h"
#include "../../renderData/include/renderingMemory.h"
#include "../../renderData/include/renderObjectPtr.h"
#if !defined( VANGUARD_ASSET_HOST )
#include "../../world/include/worldApi.h"
#include "../../world/include/worldObjectTag.h"
#endif
#ifndef NO_EDITOR
#include "navigationSetting.h"
#endif

#include "meshChunk.h"
#include "../../redReflection/include/resourceAsyncReference.h"
#include "meshMaterialBuffer.h"

struct SMeshChunkPacked;
struct SMeshChunk;
class IMaterial;
class CMaterialInstance;
class IRenderResource;
class IRenderResourceBlob;

enum EMaterialVertexFactory : Uint8;
enum ERenderObjectType : Uint8;

namespace mesh
{
	class MeshParameter;
	class MeshAppearance;

	class DataViewRO;
	class DataView;
	class DataBuilder;
}

namespace serialization
{
	class LoadingContext;
}

namespace tools
{
	class MeshCustomPropertiesDescriptor_Appearances;
	class MeshResourceEditor;
	struct AppearancesSetup;
}

struct MeshAutohideParams
{
	math::Box boundingBox = math::Box::EMPTY();
	math::Vector3 surfaceAreaPerAxis = math::Vector3( -1.0f, -1.0f, -1.0f );
	math::Vector3 meshScale = math::Vector3::ONES();
};

class RESOURCEMESH_API CMesh : public res::StreamedResource
{
	RTTI_DECLARE_TYPE( CMesh );
	IMPLEMENT_RESOURCE_INTERFACE( "w2mesh", "mesh", "Mesh" );
	RED_BASE_CLASS( res::StreamedResource );
	RED_USE_MEMORY_POOL( rend::PoolMesh );

public:

	// Single material on the list. May be an external material, or a local material instance.
	// For simplicity of the mesh code we combine that into one materials list.
	class RESOURCEMESH_API MaterialEntry
	{
	public:
		RTTI_DECLARE_TYPE( MaterialEntry );

		THandle<IMaterial> GetMaterial() const;

		THandle<IMaterial> GetMaterialNoFallbackIfNull() const;

		void SetMaterial(const THandle<IMaterial>& material);
		void SetLoadedMaterial(const THandle<IMaterial>& material);

		void SetName(CName name)
		{
			m_name = name;
		}

		void SetIndex(Uint16 index)
		{
			m_index = index;
		}

		void SetIsLocalInstance(Bool value)
		{
			m_isLocalInstance = value;
		}

		CName GetName() const { return m_name; }
		Uint16 GetIndex() const { return m_index; }
		Bool IsLocalInstance() const { return m_isLocalInstance; }

	private:
		WeakHandle<IMaterial> m_material;  // This is not serialized, but patched at runtime; strong handle stored in appearances or in preloadedMaterial
		THandle<IMaterial> m_pinnedMaterial; // Not serialized, used if this mesh is preloaded or if the material is set by the editor, since there's no appearance to store it in instead
		CName m_name;                   // Local name of the material (this name is used by mesh and it's appearances).
		Uint16 m_index;                 // Index either on the m_externalMaterials or m_localMaterialInstances array.
		Bool m_isLocalInstance;         // Which array does this material come from, m_externalMaterials or m_localMaterialInstances?
	};

    CMesh();
	virtual ~CMesh();

	//! Get bounding box of the mesh
	const Box& GetBoundingBox() const { return m_boundingBox; }

	const Vector3& GetSurfaceAreaPerAxis() const { return m_surfaceAreaPerAxis; }

	#ifndef RED_CONFIGURATION_FINAL
	void Editor_SetBoundingBox(const Box& boundingBox) { m_boundingBox = boundingBox; }
	#endif // RED_CONFIGURATION_FINAL

	//! Calculate auto hide distance of a mesh given it's bounding box and optionally projection of triangles surface area in all axes and scale.
	//! Note: use this instead of scaling autohide distance of non-scaled mesh
	static Float CalculateAutoHideDistance( const MeshAutohideParams& params );

	//! Calculate scaled mesh auto hide distance
	Float GetAutoHideDistance( const Vector3& scale = Vector3::ONES() ) const;

	//! Get the rendering data for this mesh (already uploaded to the rendering)
	RED_INLINE const TRenderPtr<IRenderResource>& GetRenderResource() const { return m_renderResource;  }

	//! TEMP CODE - waiting to change the CMeshPacker to use the DataViewRO
#ifndef RED_CONFIGURATION_FINAL
	RED_INLINE const red::DynArray< SMeshChunkPacked >& DEPRECATED_GetChunks() const { return m_chunks;  }
#endif // RED_CONFIGURATION_FINAL

	RED_FORCE_INLINE Uint32 GetNumAppearances() const { return m_appearances.Size(); }

	virtual Uint32 GetGPUSize() const override;

	virtual job::Counter* HACK_GetPostLoadWaitCounter() const override final;

	//----

	// Get mesh parameter of given type, may return NULL if parameter is not defined
	THandle< mesh::MeshParameter > GetParameter(const rtti::ClassType* parameterType) const;

	// Create mesh parameter or return existing one
	THandle< mesh::MeshParameter > CreateParameter(const rtti::ClassType* parameterType);

	// Retrieve mesh parameter by type
	template< typename T >
	RED_INLINE THandle< T > GetParameter() const { return Cast< T >( GetParameter(ClassID<T>()) ); }

	// Retrieve existing mesh parameter or create a new one by type
	template< typename T >
	RED_INLINE THandle< T > CreateParameter() { return Cast< T >( CreateParameter(ClassID<T>()) ); }

	// Add unique mesh parameter
	// NOTE: will assert if parameter of given class already exists
	// NOTE: mesh parameters will be parented to the mesh (they are ISerializables)
	void AddParameter(THandle< mesh::MeshParameter > parameter);

	// Remove existing mesh parameter
	void RemoveParameter(THandle< mesh::MeshParameter > parameter);

	// Remove existing mesh parameter by type
	void RemoveParameter(const rtti::ClassType* parameterType);

	//-----
	
	/// Get mesh appearance of a given name. Empty name is also allowed, default appearance will be returned.
	/// This method will always return non nullptr handle, if the appearance is not found then the default appearance will be returned.
	const THandle< mesh::MeshAppearance >& GetAppearanceSafe( const CName appearanceName ) const;

	/// Get mesh appearance for a given index (used eg. for randomizing appearaces). Invalid index (out of bounds) is also allowed, default appearance will be returned.
	/// This method will always return non nullptr handle, if the appearance is not found then the default appearance will be returned.
	const THandle< mesh::MeshAppearance >& GetAppearanceSafe( const Uint32 appearanceIndex ) const;

	/// Get default mesh appearance.
	/// The default appearance is either an appearance named "default", or the first appearance (index 0) when name "default" was not found.
	/// This method will always return non nullptr handle, there is always at least one appearance in the mesh and it will be used as default.
	const THandle< mesh::MeshAppearance >& GetDefaultAppearance() const;

	RED_INLINE red::ArraySpan< const THandle< mesh::MeshAppearance > > GetAllAppearances() const { return m_appearances; }

	RED_INLINE ERenderObjectType GetRenderObjectType() const { return m_objectType; }
	RED_INLINE Bool GetBlendedWithTerrainHeightMap() const { return m_constrainAutoHideDistanceToTerrainHeightMap; }

	//-----

	//! Fill render resource blob during import and cooking
	Bool SetRenderResourceBlob( THandle< IRenderResourceBlob >& blob );

	virtual res::GatheredResource * GetDefaultResource() const override;
    static res::GatheredResource* GetDefaultResourceStatic();

	virtual void RecreateInternalRenderResources( Uint32 flag ) override; 

	red::DynArray< THandle< IMaterial > > GetMaterials() const;

	//! Mesh is casting global shadows (cascades + distant)
	Bool GetCastGlobalShadows() const;
	//! Mesh is casting local shadows
	Bool GetCastLocalShadows() const;

	//! Call it before mesh chunks are destroyed.
	void CacheShadowsSettingsForCooking();

#ifndef RED_CONFIGURATION_FINAL
		//! Used only by GI Generator. Mesh is used in GI generation (probe placement + surfels)
		Bool GetIsGIVisible() const { return m_isGIVisible; };
		//! Used only by GI Generator. Mesh is skipped in GI occlusion generation (tetrahedron shadow map)
		Bool GetIsGITransparent() const { return m_isGITransparent; };
#if !defined( VANGUARD_ASSET_HOST )
		world::ObjectTagExt GetObjectTagExt() const { return m_tagExt; }
	    world::ObjectTag    GetObjectTag   () const { return m_tag; }
#endif

	// Get date time of last save of this file. Might be empty for older data.
	RED_INLINE const CDateTime& GetSaveDateTime() const { return m_saveDateTime; }				/// @todo DP: #ifndef RED_CONFIGURATION_FINAL

	// Get hash of the geometry data of the mesh (vertices and triangles).
	// When this is 0 means it is not valid hash. Older data might not have hashes always calculated.
	RED_INLINE Uint64 GetGeometryHash() const { return m_geometryHash; }
#endif // RED_CONFIGURATION_FINAL

	Uint8 GetNumberOfLODs() const { return m_lodLevelInfo.Size(); }

	virtual Uint32 GetRenderResourceSize() const override;

	using TMaterialArray = red::DynArray<THandle<IMaterial> >;
	struct MaterialArrayWrapper // for the array memory class in a sharedptr
	{
		TMaterialArray arr{ rend::PoolMesh() };
	};

	// Primarily intended for internal use by appearances
	RED_NODISCARD std::pair< job::Counter, red::SharedPtr< MaterialArrayWrapper, rend::PoolMesh > > IssueLoadMaterials(const red::DynArray< CName >& materialNames, io::EAsyncPriority priority);

	Bool IsRenderMaterialSetupForAllAppearancesPreloaded() const;

	virtual void OnPostLoad(const PostLoadContext& context) override final;

#ifndef NO_EDITOR
		Bool IsForceLoadAllAppearances() { return m_forceLoadAllAppearances; }
	NavGen::NavigationSetting GetNavigationSetting() const { return m_navigationSetting; }
	TrafficGen::TrafficSetting GetTrafficSetting() const { return m_trafficSetting; }
	TrafficGen::DynamicTrafficSetting GetDynamicTrafficSetting() const;
		void SetNavigationSetting( NavGen::NavigationSetting value ) { m_navigationSetting = value; };
#endif
#if defined( VANGUARD_ASSET_HOST )
		Bool IsForceLoadAllAppearances() const { return m_forceLoadAllAppearances; }
#endif

	const THandle< IRenderResourceBlob >& GetRenderResourceBlob() const { return m_renderResourceBlob; }

	static THandle< IMaterial > StaticGetFallbackMaterialForAppearanceError();

	Uint8 GetConsoleMeshBias() const;
	void SetConsoleMeshBias(Uint8 bias);

	red::DynArray< res::ResourcePath > CollectDependencies( const red::DynArray< CName >& materialName ) const;
    red::DynArray< THandle<IMaterial> > Cook_GetLocalMaterials( const red::DynArray< CName >& materialName ) const;

	void AddInplaceResourceDependency( const res::ResourcePath& path );

protected:
	// Custom properties
	virtual void GetCustomEditableProperties( rtti::EditableProperties& outRootProperties ) const override;
	virtual Bool ReadCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, red::SharedPtr< rtti::ValueHolder >& outValue ) const override;
	virtual void OnSerialize(IFile& file) override;

private:
	enum MeshInternalData
	{
		MeshInternalData_RenderResource = RED_FLAG(2),
		MeshInternalData_RenderMaterialSetup = RED_FLAG(3),

		MeshInternalData_All = 0xFF,
	};

	typedef red::DynArray< Float >	TLODLevelArray;
	typedef red::DynArray< CName >		TSkeletonBoneNameArray;
	typedef red::DynArray< Matrix >		TSkeletonBoneMatrixArray;
	typedef red::DynArray< Float >		TSkeletonBoneEpsilonArray;
	typedef red::DynArray< THandle< IMaterial > > TMaterials;
	typedef red::DynArray< THandle< mesh::MeshAppearance > > TAppearances;
	typedef red::DynArray< THandle< mesh::MeshParameter > > TParameters;
	typedef red::DynArray< CName >		TFloatTrackNameArray;

	void TempDoMaterialInstanceConversion();

	// Kick of material preloading and possibly create their renderMaterialSetup depending on the loading context postload flags.
	// Sad function name since mesh is friends with the universe - friends can use RecreateInternalResources() instead.
	// It's especially not thread safe to call this function from multiple threads at once, because of lazily creating the preloadCounter.
	void PreloadAppearances_StrictlyPrivateNeverCallFromFriends(Bool flushAppearanceWaitCounter, const serialization::LoadingContext* optLoadingContext = nullptr );

	Bool TryPreloadFromCookedProperties_StrictlyPrivateNeverCallFromFriends(const serialization::LoadingContext* optLoadingContext = nullptr);

	/// Get mesh appearance of a given name. Empty name is also allowed, default appearance will be returned.
	const THandle< mesh::MeshAppearance >& GetAppearanceInternal(const CName appearanceName) const;

	/// Get mesh appearance for a given index. Invalid index (out of bounds) is also allowed, default appearance will be returned.
	const THandle< mesh::MeshAppearance >& GetAppearanceInternal( const Uint32 appearanceIndex ) const;

	// Re(create) internal resources
	// NOTE: this is a costly call, usually done after mesh structure changes
	// NOTE: we keep every "Recreate" in one function so when things are created they are always done in proper order
	void RecreateInternalResources( const Uint32 what, const Bool preloadResources );

	void AssertHasValidMaterialEntries();

	void CreateRenderResource();

	void CreateDefaultAppearance(); // Create default appearance, used when the mesh contained no appearances. We need to guarantee there is always at least one appearance.
	void SetAppearanceBackPointers();

	void SetLocalMaterialEntriesMaterialPointers(const red::DynArray<THandle<IMaterial>>& localMaterials);
	void SetExternalMaterialEntriesMaterialPointers( const red::DynArray<THandle<IMaterial>>& externalMaterials);

	//Should not be exposed. Needed in importers to automatically properly setup RenderObjectType for new meshes. In that case make those Importer classes friends
	RED_INLINE void SetRenderObjectType( const ERenderObjectType objectType ) { m_objectType = objectType; };
	RED_INLINE void SetBlendedWithTerrainHeightMap( const Bool enable ) { m_constrainAutoHideDistanceToTerrainHeightMap = enable; };

	// Compute triangles area projected to each axis
	void ComputeSurfaceAreaPerAxis();

#ifndef RED_CONFIGURATION_FINAL
	void ResaveMaterials(Bool isCooking);
#endif // RED_CONFIGURATION_FINAL

	void CleanupMeshParameters( Bool isCooking );

	virtual void OnPreSave( const PreSaveContext& context ) override final;
	virtual Bool OnPropertyPreChange( const rtti::AccessPath& path, red::SharedPtr< const rtti::ValueHolder >& inOutValue ) override final;
	virtual void OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& value ) override;
	virtual Bool OnPropertyTypeMismatch(CName propertyName, const rtti::Property* existingProperty, const rtti::Variant& readValue) override;
	
	// Placed here, since we want to use this both in CMesh and in mesh::DataView.
	static void InsertMaterialToInternalArray( red::DynArray< TResAsyncRef<IMaterial> >& externalMaterials,
												mesh::MeshMaterialBuffer& localMeshMaterialBuffer,
	                                           CMesh::MaterialEntry& materialEntry );

	// Apply any changes imposed by m_resourceVersion.
	void UpgradeVersion();

	Box m_boundingBox; // General mesh bounding box ( vertices only )

	Vector3 m_surfaceAreaPerAxis; // triangles surface area (LOD 0) when projecting to all three axes

	//can't be bitfields unfortunately cause used in RTTI
	Bool m_constrainAutoHideDistanceToTerrainHeightMap;
	Bool m_forceLoadAllAppearances;
	Bool m_castGlobalShadowsCachedInCook;
	Bool m_castLocalShadowsCachedInCook;

	TParameters m_parameters{ rend::PoolMesh() }; // Mesh parameters

	// TODO: merge into single bone array
	TSkeletonBoneNameArray		m_boneNames{ rend::PoolMesh() };
	TSkeletonBoneMatrixArray	m_boneRigMatrices{ rend::PoolMesh() };
	TSkeletonBoneEpsilonArray	m_boneVertexEpsilons{ rend::PoolMesh() };
	red::DynArray<Uint8>		m_lodBoneMask{rend::PoolMesh()};

	TFloatTrackNameArray m_floatTrackNames{ rend::PoolMesh() };		// used for example by wrinkle maps in facial animation

	TLODLevelArray m_lodLevelInfo{ rend::PoolMesh() }; // LOD levels

#ifndef RED_CONFIGURATION_FINAL
	// Packed raw chunks
	red::DynArray< SMeshChunkPacked > m_chunks{ rend::PoolMesh() };
#endif // RED_CONFIGURATION_FINAL

	// Materials.
	red::DynArray< MaterialEntry > m_materialEntries{ rend::PoolMesh() };                      // All of the meshes materials, both external and internal.
	red::DynArray< TResAsyncRef<IMaterial> > m_externalMaterials{ rend::PoolMesh() };               // Materials coming from external files, they have ResourcePath etc.
	red::DynArray< THandle<CMaterialInstance> > m_localMaterialInstances{ rend::PoolMesh() };  // Material instances stored inside the mesh data.
	mesh::MeshMaterialBuffer m_localMaterialBuffer;

	// As proper imports/exports instead of loading on PostLoad
	red::DynArray< TResRef<IMaterial> > m_preloadExternalMaterials{ rend::PoolMesh() };
	red::DynArray< THandle<IMaterial> > m_preloadLocalMaterialInstances{ rend::PoolMesh() };
	red::DynArray< TResRef< CResource > > m_inplaceResources{rend::PoolMesh()};

	// Set of appearances defined for this mesh.
	TAppearances m_appearances{ rend::PoolMesh() };                  // This should never be empty, always at least "default" appearance is present there.

	TRenderPtr<IRenderResource> m_renderResource;				
	THandle< IRenderResourceBlob  > m_renderResourceBlob;
#ifndef RED_CONFIGURATION_FINAL
	Uint32 m_renderResourceBlobSize;
#endif // RED_CONFIGURATION_FINAL
			
	/// @todo DP: restore #ifndef RED_CONFIGURATION_FINAL when post E3
#ifndef RED_CONFIGURATION_FINAL
	CDateTime m_saveDateTime;                   // Time stamp of the last save of this file. We need this for garment data up-to-date check.
	Uint64 m_geometryHash;                      // Hash value of all the vertices and triangles. We don't want heavy SHA1 here, using just 64-bit hash, should be enough.
#endif // RED_CONFIGURATION_FINAL

	red::UniquePtr< job::Counter > m_preloadCounter;
	mutable red::RWSpinLock m_materialEntriesLock;

	ERenderObjectType m_objectType;
#ifndef RED_CONFIGURATION_FINAL
	Uint8 m_resourceVersion;
	Uint8 m_consoleBias;
#endif // RED_CONFIGURATION_FINAL

#ifndef NO_EDITOR
	NavGen::NavigationSetting m_navigationSetting;
	TrafficGen::TrafficSetting m_trafficSetting;
#endif	

#ifndef RED_CONFIGURATION_FINAL
		Bool m_isGIVisible { false };
		Bool m_isGITransparent { false };
		bool m_generatedLOD { false };
#if !defined( VANGUARD_ASSET_HOST )
		world::ObjectTagExt m_tagExt = world::ObjectTagExt::None;
	    world::ObjectTag m_tag = world::ObjectTag::None;
#endif
#endif


	friend class mesh::DataViewRO;
	friend class mesh::DataView;
	friend class mesh::DataBuilder;
	friend class tools::MeshCustomPropertiesDescriptor_Appearances;
	friend class tools::MeshResourceEditor;
	friend struct tools::AppearancesSetup;
	friend class XMLSpeedTreeMeshImporter;
	friend class CREXMeshImporter;
};

namespace MeshUtilities
{
	RESOURCEMESH_API Bool HasPosition( const EMeshVertexType vertexType );
	RESOURCEMESH_API Bool HasInstancing( const EMeshVertexType vertexType );
	RESOURCEMESH_API Bool HasTexCoord0( const EMeshVertexType vertexType );
	RESOURCEMESH_API Bool HasNormalAndTangent( const EMeshVertexType vertexType );
	RESOURCEMESH_API Bool HasSkinning( const EMeshVertexType vertexType );
	RESOURCEMESH_API Bool HasExtendedSkinning( const EMeshVertexType vertexType );
	RESOURCEMESH_API Bool HasDestruction( const EMeshVertexType vertexType );
	RESOURCEMESH_API Bool HasGarmentExtraData( const EMeshVertexType vertexType );
	RESOURCEMESH_API Bool HasSpeedTreeExtraData( const EMeshVertexType vertexType );
	RESOURCEMESH_API Bool CanHaveVehicleDamageData( const EMeshVertexType vertexType );
	RESOURCEMESH_API Bool CanHaveLightBlockerIntensity( const EMeshVertexType vertexType );

	RESOURCEMESH_API red::DynArray< GpuWrapApi::VertexPacking::PackingElement > GetVertexPackingForVertexType( const EMeshVertexType vertexType );
	RESOURCEMESH_API red::DynArray< GpuWrapApi::VertexPacking::PackingElement > GetShadowMeshPositionOnlyVertexPacking();
	RESOURCEMESH_API EMaterialVertexFactory GetVertexFactoryForMeshChunk(const SMeshChunkPacked& sourceChunk);
	RESOURCEMESH_API GpuWrapApi::VertexPacking::PackingVertexLayout GetVertexLayout();
}
