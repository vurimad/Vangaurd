/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "houdiniEngineInterface.h"

#include "../include/editableCollectionCustomPropertyEditor.h"
#include "../../../common/world/include/worldNodeTransform.h"
#include "../../../common/resourceMesh/include/meshVertex.h"

struct DebugVertex;

namespace world 
{
	class NodeTransform;
	class Node;
	class Prefab;
	typedef THandle< Node > NodePtr;
	class NodeRef;
	class INodeInstanceRegistry;
}

class CMesh;

namespace tools
{
	class IMessageReporter;
	class EditableCollection;
	class ResourceBackend;

	namespace prv
	{
		class EditableCollectionRootItem;
		class OutputControlType : public tools::IEditableCustomProperty
		{
			RTTI_DECLARE_TYPE(OutputControlType);
			using BaseClass = tools::IEditableCustomProperty;

		public:
			OutputControlType();
			virtual const Bool HandlesCustomType(const CName customTypeName, const EditableCollectionRootItem* /*root*/) const override;
			virtual CName GetControlType() const override;
			virtual Bool HasOptions() const override;
			virtual const red::DynArray<String>& GetOptions() const override;
			CName m_name;

		private:
			mutable red::DynArray<String> m_options; // GetOptions returns const& !
		};

		class OutputControlAction : public OutputControlType
		{
			RTTI_DECLARE_TYPE(OutputControlAction);
		
		public:
			OutputControlAction();
		};

		class PropControlToggle : public OutputControlType
		{
			RTTI_DECLARE_TYPE(PropControlToggle);

		public:
			PropControlToggle();
		};

		class PropControlComboBox : public OutputControlType
		{
			RTTI_DECLARE_TYPE(PropControlComboBox);

		public:
			PropControlComboBox();

			virtual void Initialize(const CName customPropertyType, EditableCollection* collection, prv::EditableCollectionRootItem* root, const rtti::AccessPath& pathSoFar, const rtti::IType* type, const Bool isInlined, const Bool isReadOnly) override;


			virtual const Bool HandlesCustomType(const CName customTypeName, const prv::EditableCollectionRootItem* root) const override;

		};

	}
}

namespace houdini_legacy
{
	class IHDAInterfaceWrapper;
	class ResourceEntryElement;

	struct EnvConfig
	{
		red::AbsolutePath m_envFiles;
		red::AbsolutePath m_otlFiles;
		red::AbsolutePath m_dsoFiles;
		red::AbsolutePath m_debugOutputDirectory;
		red::String m_libraryDepotLocation;

		const char* m_houdiniEngineSessionName = "hapi";
		const char* m_libraryExtension = "hda";
		const char* m_debugOutputSceneName = "output_debug.hip";

		EnvConfig() {};
		EnvConfig(const red::AbsolutePath& envFiles, const red::AbsolutePath& otlFiles, const red::AbsolutePath& dsoFiles, 
			const red::String& libraryDepotLocation, const red::AbsolutePath& debugOutputDirectory);
	};

	struct ChunkInfo
	{
		Uint32 m_start = 0u;	//Primitive number where the chunk starts
		Uint32 m_end = 0u;		//Primitive number past one where the chunk ends
		String m_material = String();
		Uint32 m_originalChunkId = 0u;

		EMeshVertexType m_chunkType = MVT_StaticMesh;

		Uint32 m_flags;
	};

	struct LodInfo
	{
		Uint32 m_lod = 0u;
		red::DynArray< ChunkInfo > m_chunks;
	};

	struct BACKEND_ENGINE_API GeometryBuffer
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		red::String m_groupName = "";
		red::String m_nodeRef = "";
		red::String m_materialPath = "";
		red::String m_materialName = "";
		red::String m_outputResourcePath = "";
		red::String m_outputName = "";
		red::String m_bendedMeshFile = "";
		red::String m_destructionOwnerHashStart = red::String::EMPTY();
		red::String m_destructionOwnerHashEnd = red::String::EMPTY();
		red::Uint16	m_cableLength = 0;
		red::Uint16	m_cableRadius = 0;

		red::DynArray< DebugVertex > m_vertices;
		red::DynArray< Vector3 > m_normals;
		red::DynArray< Vector2 > m_uvs;
		red::DynArray< Vector3 > m_tangents;
		red::DynArray< Vector3 > m_binormals;
		red::DynArray< Vector3 > m_garmentmorphs;
		red::DynArray< Uint32 > m_garmentflags;
		red::DynArray< Uint32 > m_destruction;
		red::DynArray< Color > m_wis;
		red::DynArray< Vector4 > m_wts;
		red::DynArray< Color > m_ewis;
		red::DynArray< Vector4 > m_ewts;
		red::DynArray< Uint32 > m_indices;
		red::DynArray< Uint32 > m_simulation;
		red::DynArray< Vector3 > m_vehicleDamagePosition;
		red::DynArray< Vector3 > m_vehicleDamageNormal;
		world::NodeTransform m_transform;
		world::NodeTransform m_pivotTransform;
		red::DynArray< LodInfo > m_lods;
		red::DynArray< Matrix > m_deformationData;


		//Collision
		red::DynArray< Vector3 > m_verticesCollision;
		red::DynArray< Uint16 >	m_indicesCollision;
		red::DynArray< String > m_collisionMaterials;
		red::DynArray< Uint32 > m_collisionMaterialIndices;

		//Occlusion
		red::DynArray< Vector3 > m_verticesOcclusion;
		red::DynArray< Uint16 >	m_indicesOcclusion;

		GeometryBuffer( const red::memory::Pool& pool = red::PoolBackend() );
		GeometryBuffer( const GeometryBuffer& other );
		~GeometryBuffer();
		GeometryBuffer& operator=( const GeometryBuffer& other );

		void GeometryBuffer::Shrink()
		{
			// m_vertices.Shrink(); // this is not shrinked, as DebugVertex doesn't allow for that in templated code
			m_lods.Shrink();
			m_indices.Shrink();
			m_uvs.Shrink();
			m_normals.Shrink();
			m_binormals.Shrink();
			m_tangents.Shrink();
			m_wis.Shrink();
			m_wts.Shrink();
			m_garmentmorphs.Shrink();
			m_garmentflags.Shrink();
			m_destruction.Shrink();
			m_ewis.Shrink();
			m_ewts.Shrink();
			m_simulation.Shrink();
			m_vehicleDamagePosition.Shrink();
			m_vehicleDamageNormal.Shrink();
			m_verticesCollision.Shrink();
			m_indicesCollision.Shrink();
			m_collisionMaterials.Shrink();
			m_collisionMaterialIndices.Shrink();
			m_verticesOcclusion.Shrink();
			m_indicesOcclusion.Shrink();
		}
	};

	// houdini engine environmental vars
	class BACKEND_ENGINE_API HoudiniEngineSession
	{
		RTTI_DECLARE_TYPE(HoudiniEngineSession);

		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		HoudiniEngineSession();
		virtual ~HoudiniEngineSession() {};

		HAPI_Session* Get() { return &m_hapiSession; }
		HAPI_ProcessId* GetID() { return &m_processId; }
		HAPI_CookOptions* GetCookingOptions() { return &m_cookOptions; }
		THandle<IHDAInterfaceWrapper> GetInterfaceWrapper() { return m_wrapper; }

		const EnvConfig& GetConfig() { return m_config; }

		// initialization
		void SetupEnvironment(const red::AbsolutePath& envFiles, const red::AbsolutePath& otlFiles,
			const red::AbsolutePath& dsoFiles, const red::String& libraryDepotLocation, const red::AbsolutePath& debugOutputDirectory);

		// de-initializaion and clean up
		Bool CleanUp();

		// debug scene output path
		const red::AbsolutePath GetDebugPath();
		void SetDebugPath(const red::AbsolutePath& outDir, const red::String& fileName);

		// TEMP
		red::String m_msg = red::String();
		Bool				m_saveDebugScene;

	private:
		HAPI_Session					m_hapiSession;
		HAPI_ProcessId					m_processId;
		HAPI_CookOptions				m_cookOptions;
		EnvConfig						m_config;
		THandle<IHDAInterfaceWrapper>	m_wrapper;
		String							m_loadedHdaName;

		friend BACKEND_ENGINE_API Bool LoadHoudiniDigitalAsset( HoudiniEngineSession* session, const String& hdaName, Int32& outAssetCount, red::String& msg, Bool forceReload );
	};

	

	// internal calls wrapper
	Bool HAPIx( HAPI_Result result, red::String& msg = red::String(), const HAPI_Session* session = nullptr, HAPI_StatusType = HAPI_STATUS_CALL_RESULT );
	const red::String HAPIs(const HAPI_Session* session, HAPI_StringHandle stringHandle );
	Bool HAPIc(HoudiniEngineSession* session);

	// initialization
	BACKEND_ENGINE_API Bool IsHoudiniEngineAvailable();
	BACKEND_ENGINE_API void InitializeHoudiniEngine( red::String& msg, HoudiniEngineSession& heSession );
	BACKEND_ENGINE_API Bool Start(HoudiniEngineSession* session, red::String& msg = red::String());
	BACKEND_ENGINE_API Bool Close(HoudiniEngineSession* session);
	BACKEND_ENGINE_API Bool IsSessionValid(HoudiniEngineSession* session);
	BACKEND_ENGINE_API void ScanHDALibrary(HoudiniEngineSession* session, red::DynArray< String >& outHDANames);

	// interface / node cook / loading
	BACKEND_ENGINE_API Bool ValidateInterfaceParameters(HoudiniEngineSession* session);
	BACKEND_ENGINE_API Bool MapExportNodes(HoudiniEngineSession* session, THandle< IHDAInterfaceWrapper >& wrapper, 
		red::DynArray< ResourceEntryElement >& newNodes, world::INodeInstanceRegistry* registry);
	BACKEND_ENGINE_API void MapParametersToInterface(HoudiniEngineSession* session, HAPI_NodeId nodeID, Uint32 paramCount,
		HAPI_ParmInfo* paramInfos);
	BACKEND_ENGINE_API Bool LoadHoudiniDigitalAsset(HoudiniEngineSession* session, const String& hdaName, Int32& outAssetCount, 
		red::String& msg = red::String(), Bool forceReload = false);
	BACKEND_ENGINE_API Bool CookHoudiniDigitalAsset( HoudiniEngineSession* session, Bool async = true );
	
	// geometry
	BACKEND_ENGINE_API Bool RequestCookedData(HoudiniEngineSession* session,  GeometryBuffer& outGeometry);
	BACKEND_ENGINE_API Bool GetGeometryBufferArray(HoudiniEngineSession* session, red::DynArray < GeometryBuffer> &outGeometryArray);
	
	BACKEND_ENGINE_API Bool AddSOPNode(HoudiniEngineSession* session, ResourceEntryElement& nodeEntry, const CName& appearanceName );	
	BACKEND_ENGINE_API Bool AddFoliageBrushNode(HoudiniEngineSession* session, ResourceEntryElement& nodeEntry);
	BACKEND_ENGINE_API Bool AddSOPCurveNode(HoudiniEngineSession* session, ResourceEntryElement& nodeEntry, red::DynArray <Vector3> positions);

	// terrain
	BACKEND_ENGINE_API Bool AddHeightfieldSOPNode(HoudiniEngineSession* session, ResourceEntryElement& nodeEntry, TerrainDataHelper dataHelper);
	BACKEND_ENGINE_API Bool GetMlMaskCount(HoudiniEngineSession* session, red::DynArray<Uint32> &mlMasksCount);
	BACKEND_ENGINE_API Bool GetHeighfieldData(HoudiniEngineSession* session, Float* newData, Int32 newDataSize);
	BACKEND_ENGINE_API Bool GetMlMaskData(HoudiniEngineSession* session, Uint8* newData, Int32 dataSize, String layerName);

	// get config / environmental paths
	BACKEND_ENGINE_API Bool GetEnvironmentConfig(HoudiniEngineSession* session, red::DynArray<red::AbsolutePath>& outConfigPaths);

	// bake AO and LOD WIP pipeline
	BACKEND_ENGINE_API Bool CookLODs(HoudiniEngineSession* session, const String& mesh_path, GeometryBuffer& outGeometry, const CName& appearanceName);
	BACKEND_ENGINE_API Bool BakeAO( HoudiniEngineSession* session, const String& mesh_path, GeometryBuffer& outGeometry, Float maxDistance, Float spread, Float softness, Uint8 procLods );
	BACKEND_ENGINE_API Bool MergeCollisions(HoudiniEngineSession* session, red::ArraySpan<GeometryBuffer> inGeometry, Float percentage, GeometryBuffer& outGeometry);
	BACKEND_ENGINE_API Bool SimpleMergeCollisions( HoudiniEngineSession* session, red::ArraySpan<GeometryBuffer> inGeometry, Float percentage, const red::DynArray<float>& bones, GeometryBuffer& outGeometry, Bool roadVersion = false, Int32 version =0 );
	BACKEND_ENGINE_API Bool RemoveFacingFaces( HoudiniEngineSession* session, THandle<CMesh> inMesh, GeometryBuffer& outGeometry, Uint8 ver = 0);

	// terrain cooking pipeline
	BACKEND_ENGINE_API Bool CookTerrainNode(HoudiniEngineSession* session, const Float reductionRatio, const bool genOccluder, const TerrainDataHelper dataHelper, GeometryBuffer& outGeometry);

	// utils
	BACKEND_ENGINE_API Bool GetRootObjectLocalTransform(HoudiniEngineSession* session, world::NodeTransform& transform);
	BACKEND_ENGINE_API Uint64 GetUniqueHash(const ResourceEntryElement& entry);
	BACKEND_ENGINE_API Bool GetCustomParameter(HoudiniEngineSession* session, const HAPI_NodeId nodeId, const red::String& paramName, HAPI_ParmId& outParamId);

} // houdini
