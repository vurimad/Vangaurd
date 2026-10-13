/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "houdiniEngineUtils.h"

#include "../../../common/redReflection/include/stringParser.h"

#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMesh/include/meshDataViewRO.h"

#include "../../../common/world/include/worldNodeInstance.h"
#include "../../../common/resource/include/jsonResource.h"
#include "../../../common/resource/include/resourceEditorDepotImpl.h"

#include "../../backendEngine/include/editableCollectionInterop.h"

#include "../../../common/redReflection/include/editableView.h"
#include "../../../common/redReflection/include/resourceDepot.h"

#include "../../../common/engine/include/baseEngine.h"

#include "../../../common/redFileSystem/include/fileSys.h"

#include "../../../common/renderData/include/renderVertices.h"
#include "../../../common/redJobs2/include/jobBuilder.h"

#include "../../../common/world/include/worldSplineNodeInstance.h"
#include "../../../common/spline/include/splineNew.h"

#include "../../../common/world/include/foliageBrush.h"
#include "../../../common/resource/include/jsonSerializer.h"
#include "../../../common/resource/include/basicTextBuilder.h"
#include "../../common/resourceMesh/include/meshParamCloth.h"
#include "../../common/redContainers/include/arraySpan.h"
#include "../../common/redReflection/include/handle.h"

#include "../../../common/resourceMaterial/include/material.h"
#include "../../../common/resourceMaterial/include/materialDefinition.h"


RTTI_BEGIN_TYPE_IN_NAMESPACE( OutputControlType, tools, prv );
	RTTI_PARENT_TYPE( tools::IEditableCustomProperty );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE(OutputControlAction, tools, prv);
	RTTI_PARENT_TYPE( OutputControlType );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE(PropControlToggle, tools, prv);
	RTTI_PARENT_TYPE(OutputControlType);
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE(PropControlComboBox, tools, prv);
RTTI_PARENT_TYPE(OutputControlType);
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE(HoudiniEngineSession, houdini_legacy);
RTTI_END_TYPE();

namespace tools
{
	namespace prv
	{
		OutputControlType::OutputControlType()
		{
			m_name = houdini_legacy::outputTypeName;
		}

		OutputControlAction::OutputControlAction()
		{
			m_name = houdini_legacy::outputActionName;
		}

		PropControlToggle::PropControlToggle()
		{
			m_name = houdini_legacy::togglePropName;
		}

		PropControlComboBox::PropControlComboBox()
		{
		}

		void PropControlComboBox::Initialize(const CName customPropertyType, EditableCollection* collection, prv::EditableCollectionRootItem* root, const rtti::AccessPath& pathSoFar, const rtti::IType* type, const Bool isInlined, const Bool isReadOnly)
		{
			OutputControlType::Initialize(customPropertyType, collection, root, pathSoFar, type, isInlined, isReadOnly);

			m_name = RED_NAME( pathSoFar.ToString().AsChar() );
		}

		const Bool PropControlComboBox::HandlesCustomType(const CName customTypeName, const prv::EditableCollectionRootItem* root) const
		{
			return customTypeName == houdini_legacy::comboBoxPropName;
		}

		const Bool OutputControlType::HandlesCustomType(const CName customTypeName, const EditableCollectionRootItem* /*root*/) const
		{
			return customTypeName == m_name;
		}

		CName OutputControlType::GetControlType() const
		{
			return RED_NAME(red::String::Printf("%hs:Restricted", ::GetTypeName<CName>().AsChar()).AsChar());
		}

		Bool OutputControlType::HasOptions() const
		{
			return true;
		}

		const red::DynArray<red::String>& OutputControlType::GetOptions() const
		{
			m_options.Clear();
			auto root = GetRoot();

			THandle< houdini_legacy::IHDACustomPropetyContext > userContext = Cast< houdini_legacy::IHDACustomPropetyContext >(root->GetNegotiatedView()->GetUserContext());
			
			if (userContext && !m_name.Empty())
			{
				red::DynArray<CName> keys;
				userContext->m_map.GetKeys(keys);

				for (auto key : keys)
				{
					if ( key == m_name || m_name == houdini_legacy::togglePropName )
					{
						m_options = userContext->m_map[key];
					}
				}
			}
			return m_options;
		}
	}
}
namespace houdini_legacy
{
	EnvConfig::EnvConfig(const red::AbsolutePath& envFiles, const red::AbsolutePath& otlFiles, const red::AbsolutePath& dsoFiles, 
		const red::String& libraryDepotLocation, const red::AbsolutePath& debugOutputDirectory)
		: m_envFiles(envFiles)
		, m_otlFiles(otlFiles)
		, m_dsoFiles(dsoFiles)
		, m_libraryDepotLocation(libraryDepotLocation)
		, m_debugOutputDirectory(debugOutputDirectory)
	{};

	GeometryBuffer::GeometryBuffer( const red::memory::Pool& pool )
		: m_vertices( pool )
		, m_normals( pool )
		, m_uvs( pool )
		, m_tangents( pool )
		, m_binormals( pool )
		, m_garmentmorphs( pool )
		, m_garmentflags( pool )
		, m_wis( pool )
		, m_wts( pool )
		, m_indices( pool )
		, m_lods( pool )
		, m_verticesCollision( pool )
		, m_indicesCollision( pool )
	{
	}

	GeometryBuffer::GeometryBuffer( const GeometryBuffer& other ) = default;

	GeometryBuffer::~GeometryBuffer() = default;

	GeometryBuffer& GeometryBuffer::operator=( const GeometryBuffer& other ) = default;


	HoudiniEngineSession::HoudiniEngineSession()
	{
		m_processId = HAPI_ProcessId();
		m_hapiSession = HAPI_Session();
		m_wrapper = red::CreateUniquePtr<IHDAInterfaceWrapper>();

		m_cookOptions = HAPI_CookOptions_Create();
		m_cookOptions.curveRefineLOD = 8.0f;
		m_cookOptions.clearErrorsAndWarnings = false;
		m_cookOptions.maxVerticesPerPrimitive = 3;
		m_cookOptions.splitGeosByGroup = true;
		m_cookOptions.refineCurveToLinear = true;
		m_cookOptions.handleBoxPartTypes = false;
		m_cookOptions.splitPointsByVertexAttributes = false;
		m_cookOptions.handleSpherePartTypes = false;
		m_cookOptions.splitGeosByAttribute= false;
		m_cookOptions.packedPrimInstancingMode = HAPI_PACKEDPRIM_INSTANCING_MODE_DISABLED;
		m_cookOptions.cookTemplatedGeos = true;
		m_saveDebugScene = false;
		m_loadedHdaName = "";
	}

	void HoudiniEngineSession::SetupEnvironment(const red::AbsolutePath& envFiles, const red::AbsolutePath& otlFiles,
		const red::AbsolutePath& dsoFiles, const red::String& libraryDepotLocation, const red::AbsolutePath& debugOutputDirectory)
	{
		m_config.m_envFiles = envFiles;
		m_config.m_otlFiles = otlFiles;
		m_config.m_dsoFiles = dsoFiles;
		m_config.m_libraryDepotLocation = libraryDepotLocation;
		m_config.m_debugOutputDirectory = debugOutputDirectory;
	}

	Bool HoudiniEngineSession::CleanUp()
	{
		m_loadedHdaName = "";
		return m_wrapper->UnloadNodes(Get());
	}

	Bool IsHoudiniEngineAvailable()
	{
		LPCWSTR hapiFileName = L"HARS";
		LPCWSTR fileExt = L"exe";

		wchar_t* filePart;
		wchar_t filename[MAX_PATH];
		wchar_t currDirectory[MAX_PATH];
		if ( SearchPathW( nullptr, hapiFileName, fileExt, MAX_PATH, filename, &filePart ) )
		{
			return true;
		}

		GetCurrentDirectoryW( MAX_PATH, currDirectory );
		std::wstring wPath = currDirectory;
		wPath += L"\\bin\\";
		wPath += hapiFileName;
		wPath += L".";
		wPath += fileExt;
		DWORD fileAttr = GetFileAttributesW( wPath.c_str() );
		return fileAttr != INVALID_FILE_ATTRIBUTES;
	}

	void InitializeHoudiniEngine( red::String& msg, HoudiniEngineSession& heSession )
	{
		RED_LOG( "Status: Initializing Houdini Engine..." );
		msg += red::String::Printf( "Houdini Engine Initialization" );

		// project dependent env variables
		red::AbsolutePath HAPIKeyEnv;

		char* val = getenv( "HOUDINI_ENGINE_PATH" );
		if ( val )
		{
			HAPIKeyEnv = red::AbsolutePath::CreateDirPath( val );
			msg += red::String::Printf( "[HOUDINI_ENGINE_PATH]: %hs\n", HAPIKeyEnv.ToDebugString() );
		}
		else
		{
			HAPIKeyEnv = GFileManager->GetEngineRoot();
			HAPIKeyEnv.AppendDirPath( "hapi" );
			msg += red::String::Printf( "[HOUDINI_ENGINE_PATH]: not found, fallbacking to engine root: %hs\n", HAPIKeyEnv.ToDebugString() );
		}

		RED_FATAL_ASSERT( HAPIKeyEnv.IsDirectoryPath() );

		const red::String HAPIKeyResourcesOtls = "base\\houdini\\otls\\";
		red::AbsolutePath HAPIKeyOtls = res::GetEditorDepot()->TranslateResourcePath( res::ResourcePath::Build( HAPIKeyResourcesOtls ) ); // NOTE: Not supported as ResourcePath should not be a directory
		red::AbsolutePath HAPIDebugOutput = GFileManager->GetCacheDirectory();
		red::AbsolutePath HAPIKeyDso = HAPIKeyEnv.AddDirPath( "houdini\\dso" );

		heSession.SetupEnvironment( HAPIKeyEnv, HAPIKeyOtls, HAPIKeyDso, HAPIKeyResourcesOtls, HAPIDebugOutput );

		// update the paths for houdini python interpreter
		const red::AbsolutePath toolsPath = GFileManager->GetEngineRoot().AddDirPath( "tools\\python\\site-packages" );
		SetEnvironmentVariableA( "PYTHONPATH", toolsPath.AsChar() );
	}

	Bool Start(HoudiniEngineSession* session, red::String& msg)
	{
		Bool result = true;
		red::String detailedErrorMsg = red::String();

		HAPI_ThriftServerOptions serverOptions;
		serverOptions.autoClose = true;
		serverOptions.timeoutMs = 60000;

		// named session example
		// HE_result = HAPI_CreateInProcessSession(session);
		//
		// thrift socket session example
		// HE_result = HAPI_StartThriftSocketServer(&ServerOptions, 55555, pId);
		// HE_result = HAPI_CreateThriftSocketSession(session, "pipe", 55555);

		msg += red::String::Printf("Attempting to start ThriftNamedPipeServer: [%hs_%d]",
			session->GetConfig().m_houdiniEngineSessionName, *session->GetID());

		// thrift named pipe session
		if (HAPIx(HAPI_StartThriftNamedPipeServer(&serverOptions, session->GetConfig().m_houdiniEngineSessionName, session->GetID()),
			detailedErrorMsg, session->Get()))
		{
			msg += red::String::Printf(" - OK\n");
		}
		else
		{
			msg += red::String::Printf(" - Failed\n");

			detailedErrorMsg.Empty() ? msg += "HAPI: Empty response from server!\n"
				: msg += String::Printf("%hs\n", detailedErrorMsg.AsChar());

			return false;
		}

		result &= HAPIx(HAPI_CreateThriftNamedPipeSession(session->Get(), session->GetConfig().m_houdiniEngineSessionName), msg);
		result &= HAPIx(HAPI_IsSessionValid(session->Get()), msg);

		// is already initialized?
		if (!HAPIx(HAPI_IsInitialized(session->Get())))
		{
			result &= HAPIx(HAPI_Initialize(session->Get(), session->GetCookingOptions(), true, -1, session->GetConfig().m_envFiles.ToDebugString(),
			session->GetConfig().m_otlFiles.ToDebugString(), session->GetConfig().m_dsoFiles.ToDebugString(), nullptr, nullptr), msg);
		}

		return result;
	}

	Bool Close(HoudiniEngineSession* session)
	{
		return IsSessionValid(session) ? HAPIx(HAPI_CloseSession(session->Get())) : true;
	}

	const red::AbsolutePath HoudiniEngineSession::GetDebugPath()
	{
		red::AbsolutePath out = m_config.m_debugOutputDirectory;
		out.AppendFilePath(m_config.m_debugOutputSceneName);
		return out;
	}

	void HoudiniEngineSession::SetDebugPath(const red::AbsolutePath& outDir, const red::String& fileName)
	{
		m_config.m_debugOutputDirectory = outDir;
		m_config.m_debugOutputSceneName = fileName.AsChar();
	}

	const red::String HAPIe(HAPI_Result result)
	{
		red::String errorStr = "HAPI_RESULT_FAILURE";
		switch (result)
		{
		case HAPI_RESULT_ALREADY_INITIALIZED: return "HAPI_RESULT_ALREADY_INITIALIZED";
		case HAPI_RESULT_NOT_INITIALIZED: return "HAPI_RESULT_NOT_INITIALIZED";
		case HAPI_RESULT_CANT_LOADFILE: return "HAPI_RESULT_CANT_LOADFILE";
		case HAPI_RESULT_PARM_SET_FAILED: return "HAPI_RESULT_PARM_SET_FAILED";
		case HAPI_RESULT_INVALID_ARGUMENT: return "HAPI_RESULT_INVALID_ARGUMENT";
		case HAPI_RESULT_CANT_LOAD_GEO: return "HAPI_RESULT_CANT_LOAD_GEO";
		case HAPI_RESULT_CANT_GENERATE_PRESET: return "HAPI_RESULT_CANT_GENERATE_PRESET";
		case HAPI_RESULT_CANT_LOAD_PRESET: return "HAPI_RESULT_CANT_LOAD_PRESET";
		case HAPI_RESULT_ASSET_DEF_ALREADY_LOADED: return "HAPI_RESULT_ASSET_DEF_ALREADY_LOADED";
		case HAPI_RESULT_NO_LICENSE_FOUND: return "HAPI_RESULT_NO_LICENSE_FOUND";

		case HAPI_RESULT_DISALLOWED_NC_LICENSE_FOUND: return "HAPI_RESULT_DISALLOWED_NC_LICENSE_FOUND";
		case HAPI_RESULT_DISALLOWED_NC_ASSET_WITH_C_LICENSE: return "HAPI_RESULT_DISALLOWED_NC_ASSET_WITH_C_LICENSE";
		case HAPI_RESULT_DISALLOWED_NC_ASSET_WITH_LC_LICENSE: return "HAPI_RESULT_DISALLOWED_NC_ASSET_WITH_LC_LICENSE";
		case HAPI_RESULT_DISALLOWED_LC_ASSET_WITH_C_LICENSE: return "HAPI_RESULT_DISALLOWED_LC_ASSET_WITH_C_LICENSE";
		case HAPI_RESULT_DISALLOWED_HENGINEINDIE_W_3PARTY_PLUGIN: return "HAPI_RESULT_DISALLOWED_HENGINEINDIE_W_3PARTY_PLUGIN";

		case HAPI_RESULT_ASSET_INVALID: return "HAPI_RESULT_ASSET_INVALID";
		case HAPI_RESULT_NODE_INVALID: return "HAPI_RESULT_NODE_INVALID";
		case HAPI_RESULT_USER_INTERRUPTED: return "HAPI_RESULT_USER_INTERRUPTED";
		case HAPI_RESULT_INVALID_SESSION: return "HAPI_RESULT_INVALID_SESSION";
		}
		return errorStr;
	}

	// executes HAPI function, logs errors
	Bool HAPIx(HAPI_Result result, red::String& msg, const HAPI_Session* session, HAPI_StatusType statusType)
	{
		if (result != HAPI_RESULT_SUCCESS)
		{
			msg += red::String::Printf("HAPIx error: %hs\n", HAPIe(result).AsChar());

			Int32 buffer_length = 0;
			HAPI_GetStatusStringBufLength(session, statusType, HAPI_STATUSVERBOSITY_ALL, &buffer_length);
			red::String errorStr;

			if (buffer_length > 256)
			{
				msg += red::String::Printf("HAPIx: Status function trying to allocate: %dkb\n", buffer_length / 1024);
				return false;
			}
			else if (buffer_length == 0)
			{
				return false;
			}
			else
			{
				errorStr.Resize(buffer_length);
				HAPI_GetStatusString(session, statusType, errorStr.AsChar(), buffer_length);
				msg += red::String::Printf("HAPI: %hs\n", errorStr.AsChar());
			}
			return false;
		}
		return true;
	}

	// translates HAPI string handles
	const red::String HAPIs(const HAPI_Session* session, HAPI_StringHandle stringHandle)
	{
		if (stringHandle == 0) return "";

		Int32 bufferLength = 0;
		HAPI_GetStringBufLength(session, stringHandle, &bufferLength);

		red::String result = String::EMPTY();
		// > 1 because HAPI_GetStringBufLength return NULL terminated string
		if (bufferLength > 1 && bufferLength < 256)
		{
			result.Resize(bufferLength);
			HAPI_GetString(session, stringHandle, result.AsChar(), bufferLength);
		}
		return result;
	}

	// performs cooking feedback loop
	Bool HAPIc(HoudiniEngineSession* session)
	{
		Float time = GEngine->GetRawEngineTime().GetNow();
		Float deltaTime = 0.0f;

		red::String statusStr = red::String();
		int status = -1;
		do
		{
			int newStatus = -1;
			HAPI_GetStatus(session->Get(), HAPI_STATUS_COOK_STATE, &newStatus);

			if (newStatus != status)
			{
				status = newStatus;
				statusStr = "";
			}

			int statusBufSize = 0;
			HAPIx(HAPI_GetStatusStringBufLength(session->Get(), HAPI_STATUS_COOK_STATE, HAPI_STATUSVERBOSITY_ALL, &statusBufSize));
			char * statusBuf = NULL;
			if (statusBufSize > 0)
			{
				statusBuf = new char[statusBufSize];
				HAPIx(HAPI_GetStatusString(session->Get(), HAPI_STATUS_COOK_STATE, statusBuf, statusBufSize));
			}
			if (statusBuf)
			{
				statusStr = String(statusBuf, statusBufSize);
				delete[] statusBuf;
			}

			Float currentDeltaTime = (Float)(GEngine->GetRawEngineTime().GetNow()) - time;
			if (floor(currentDeltaTime) > floor(deltaTime))
			{
				deltaTime = currentDeltaTime;
				red::String displayStatus = red::String::Printf("%hs [ %0.fs ]", statusStr.AsChar(), deltaTime);
				session->GetInterfaceWrapper()->AddProgress(0.0f, displayStatus);
			}

			if( status > HAPI_STATE_MAX_READY_STATE )
			{
				Sleep(200u);
			}

		} while (status > HAPI_STATE_MAX_READY_STATE);

		Bool result = HAPIx((HAPI_Result)status, session->m_msg, session->Get(), HAPI_STATUS_COOK_STATE);
		session->m_msg = statusStr;

		const Float currentDeltaTime = (Float)(GEngine->GetRawEngineTime().GetNow()) - time;
		red::String displayStatus = red::String::Printf("Cook time : %0.fs", currentDeltaTime);
		session->GetInterfaceWrapper()->AddProgress(0.0f, displayStatus);
		return result;
	}

	template <class T> void InitArray(T* arr, T val, const Uint32 size)
	{
		for (Uint32 i = 0; i < size; ++i)
		{
			arr[i] = val;
		}
	}

	Bool IsMeshResource(const red::String& resourcePath, CName& meshResourceType)
	{
		const red::StringView fileExtension = red::paths::GetExtension(resourcePath);
		if (fileExtension == ClassID<CMesh>()->GetDefaultObject<CResource>()->GetExtension()
			|| fileExtension == "w2mesh"
			|| fileExtension == "Mesh")
		{
			meshResourceType = RED_NAME(ClassID<CMesh>()->GetDefaultObject<CResource>()->GetExtension());
			return true;
		}
		return false;
	}

	Bool IsJsonResource(const red::String& resourcePath)
	{
		const red::StringView fileExtension = red::paths::GetExtension(resourcePath);
		if (fileExtension == JsonResource::GetFileExtension())
			return true;

		return false;
	}

	Bool IsFoliageBrushResource(const red::String& resourcePath, CName& resourceType)
	{
		const red::StringView fileExtension = red::paths::GetExtension(resourcePath);
		if (fileExtension == world::FoliageBrush::GetFileExtension())
		{
			resourceType = GetTypeObject< world::FoliageBrush >()->GetName();
			return true;
		}
		return false;

	}

	Bool AddDetailAttribute(HAPI_Session* session, const HAPI_NodeId nodeID, const red::String& attributeName, const red::String& attributeValue)
	{
		HAPI_AttributeInfo enginePathAttrInfo = HAPI_AttributeInfo_Create();
		enginePathAttrInfo.storage = HAPI_STORAGETYPE_STRING;
		enginePathAttrInfo.owner = HAPI_ATTROWNER_DETAIL;
		enginePathAttrInfo.originalOwner = HAPI_ATTROWNER_DETAIL;
		enginePathAttrInfo.typeInfo = HAPI_ATTRIBUTE_TYPE_ST;
		enginePathAttrInfo.tupleSize = 1;
		enginePathAttrInfo.count = 1;
		enginePathAttrInfo.exists = true;

		char ** dataArray = new char *[1];
		dataArray[0] = _strdup(attributeValue.AsChar());

		Bool result = HAPIx(HAPI_AddAttribute(session, nodeID, 0, attributeName.AsChar(), &enginePathAttrInfo));
		result &= HAPIx(HAPI_SetAttributeStringData(session, nodeID, 0, attributeName.AsChar(), &enginePathAttrInfo, (const char **)dataArray, 0, 1));
		return result;
	}

	void ValidateNode(HAPI_Session* session, HAPI_NodeId nodeId, HAPI_ObjectInfo*& objectInfos, Int32& objectCount)
	{
		HAPI_NodeInfo nodeInfo;
		HAPIx(HAPI_GetNodeInfo(session, nodeId, &nodeInfo));

		objectCount = 0;
		objectInfos = 0;

		if (nodeInfo.type == HAPI_NODETYPE_SOP)
		{
			// For pure SOP asset, a parent object will be created autohtically,
			// so use parent's ID to get the object info
			objectCount = 1;
			// objectInfos - freed outside
			objectInfos = RED_NEW_ARRAY(HAPI_ObjectInfo, objectCount, red::PoolBackend);
			HAPIx(HAPI_GetObjectInfo(session, nodeId, objectInfos));
		}
		else if (nodeInfo.type == HAPI_NODETYPE_OBJ)
		{
			// This could have children objects or not.
			// If has children, get child object infos.
			// If no children, presume this node is the only object.
			HAPIx(HAPI_ComposeObjectList(session, nodeId, nullptr, &objectCount));
			if (objectCount > 0)
			{
				// objectInfos - freed outside
				objectInfos = RED_NEW_ARRAY(HAPI_ObjectInfo, objectCount, red::PoolBackend);
				HAPIx(HAPI_GetComposedObjectList(session, nodeInfo.parentId, objectInfos, 0, objectCount));
			}
			else
			{
				objectCount = 1;
				// objectInfos - freed outside
				objectInfos = RED_NEW_ARRAY(HAPI_ObjectInfo, objectCount, red::PoolBackend);
				HAPIx(HAPI_GetObjectInfo(session, nodeId, objectInfos));
			}
		}
		else
		{
			RED_LOG_ERROR("Unsupported node type: %d", HAPI_NODETYPE_OBJ);
			return;
		}
	}

	template< class T > Bool GetAttributeData(HAPI_Session* session, HAPI_NodeId geoNode, HAPI_PartId partId, 
		HAPI_AttributeOwner owner, const red::String& name, AttributeData& outAttributeData )
	{
		HAPI_AttributeInfo attribInfo = HAPI_AttributeInfo_Create();
		Bool result = true;
		
		HAPIx(HAPI_GetAttributeInfo(session, geoNode, partId, name.AsChar(), owner, &attribInfo));

		if (attribInfo.count > 0)
		{
			outAttributeData.m_tupleSize = attribInfo.tupleSize;
			outAttributeData.m_atrribCount = attribInfo.count;

			// is freed by parent function
			if (GetTypeName< T >() == RED_NAME_CONSTEXPR_NOREG("Matrix"))
			{
				outAttributeData.m_attribData = RED_NEW_ARRAY(T, outAttributeData.m_atrribCount, red::PoolBackend);
			}
			else
				outAttributeData.m_attribData = RED_NEW_ARRAY(T, outAttributeData.m_atrribCount*outAttributeData.m_tupleSize, red::PoolBackend);

			if (GetTypeName< T >() == RED_NAME_CONSTEXPR_NOREG("Float"))
			{
				Float* rawDataPtr = (Float*)(outAttributeData.m_attribData);
				result &= HAPIx(HAPI_GetAttributeFloatData(session, geoNode, partId, name.AsChar(), &attribInfo, -1, rawDataPtr, 0, attribInfo.count));
			}
			else if (GetTypeName< T >() == RED_NAME_CONSTEXPR_NOREG("Int32"))
			{
				Int32* rawDataPtr = (Int32*)(outAttributeData.m_attribData);
				result &= HAPIx(HAPI_GetAttributeIntData(session, geoNode, partId, name.AsChar(), &attribInfo, -1, rawDataPtr, 0, attribInfo.count));
			}
			else if (GetTypeName< T >() == RED_NAME_CONSTEXPR_NOREG("String"))
			{
				HAPI_StringHandle* rawDataPtr = (HAPI_StringHandle*)(outAttributeData.m_attribData);
				result &= HAPIx(HAPI_GetAttributeStringData(session, geoNode, partId, name.AsChar(), &attribInfo, rawDataPtr, 0, attribInfo.count));
			}
			else if (GetTypeName< T >() == RED_NAME_CONSTEXPR_NOREG("Matrix"))
			{
				// due to bug all matrices are hanlded as *float
				Float* rawDataPtr = RED_NEW_ARRAY(Float, attribInfo.count*attribInfo.tupleSize, red::PoolBackend);
				result &= HAPIx(HAPI_GetAttributeFloatData(session, geoNode, partId, name.AsChar(), &attribInfo, -1, rawDataPtr, 0, attribInfo.count));
				
				Matrix* matrixData = (Matrix*)(outAttributeData.m_attribData);
				Uint32 j = 0;
				Uint32 row = 0;
				Uint32 col = 0;
				Matrix mat = Matrix::IDENTITY();

				for (int i = 0; i < attribInfo.count*attribInfo.tupleSize; ++i)
				{
					mat[row][col] = rawDataPtr[i];
					row += 1;
					if (row == 4)
					{
						row = 0;
						col += 1;
						if (col == 4)
						{
							col = 0;
							matrixData[j] = mat;
							mat = Matrix::IDENTITY();
							j += 1;
						}
					}
				}
				RED_DELETE_ARRAY(rawDataPtr, attribInfo.count*attribInfo.tupleSize);
			}
			else
			{
				result = false;
			}
		}
		else
			result = false;

		return result;
	}

	template< class DataType, class Type >
	void AttribHelper(const mesh::ChunkStreamDataRO& chunkStream, DataType* attribData , Uint32 &index)
	{
		return;
	}
	template<>
	void AttribHelper< Float, Float>(const mesh::ChunkStreamDataRO& chunkStream, Float* attribData, Uint32 &index)
	{
		mesh::StreamViewRO< Float > streamView(chunkStream);
		for (Uint32 p = 0; p < streamView.Size(); ++p)
		{
			attribData[index] = streamView[p];
			index++;
		}
	}

	template<>
	void AttribHelper< Uint32, Uint32>(const mesh::ChunkStreamDataRO& chunkStream, Uint32* attribData, Uint32 &index)
	{
		mesh::StreamViewRO< Uint32 > streamView(chunkStream);
		for (Uint32 p = 0; p < streamView.Size(); ++p)
		{
			attribData[index] = streamView[p];
			index++;
		}
	}

	template<>
	void AttribHelper< Float, Color>(const mesh::ChunkStreamDataRO& chunkStream, Float* attribData, Uint32 &index)
	{
		mesh::StreamViewRO< Color > streamView(chunkStream);
		for (Uint32 p = 0; p < streamView.Size(); ++p)
		{
			attribData[index + 0] = streamView[p].R / 255.0f;
			attribData[index + 1] = streamView[p].G / 255.0f;
			attribData[index + 2] = streamView[p].B / 255.0f;
			attribData[index + 3] = streamView[p].A / 255.0f;
			index += 4;
		}
	}

	template<>
	void AttribHelper< Uint32, Color>(const mesh::ChunkStreamDataRO& chunkStream, Uint32* attribData, Uint32 &index)
	{
		mesh::StreamViewRO< Color > streamView(chunkStream);
		for (Uint32 p = 0; p < streamView.Size(); ++p)
		{
			attribData[index + 0] = (Uint32)streamView[p].R;
			attribData[index + 1] = (Uint32)streamView[p].G;
			attribData[index + 2] = (Uint32)streamView[p].B;
			attribData[index + 3] = (Uint32)streamView[p].A;
			index += 4;
		}
	}

	template<>
	void AttribHelper<Float, Vector2>(const mesh::ChunkStreamDataRO& chunkStream, Float* attribData, Uint32 &index)
	{
		mesh::StreamViewRO< Vector2 > streamView(chunkStream);
		for (Uint32 p = 0; p < streamView.Size(); ++p)
		{
			attribData[index + 0] = streamView[p].X;
			attribData[index + 1] = streamView[p].Y;
			attribData[index + 2] = 0;
			index += 3;
		}
	}

	template<>
	void AttribHelper<Float, Vector3>(const mesh::ChunkStreamDataRO& chunkStream, Float* attribData, Uint32 &index)
	{
		mesh::StreamViewRO< Vector3 > streamView(chunkStream);
		for (Uint32 p = 0; p < streamView.Size(); ++p)
		{
			attribData[index + 0] = streamView[p].X;
			attribData[index + 1] = streamView[p].Y;
			attribData[index + 2] = streamView[p].Z;
			index += 3;
		}
	}

	template<>
	void AttribHelper<Float, Vector4>(const mesh::ChunkStreamDataRO& chunkStream, Float* attribData, Uint32 &index)
	{
		mesh::StreamViewRO< Vector4 > streamView(chunkStream);
		for (Uint32 p = 0; p < streamView.Size(); ++p)
		{
			attribData[index + 0] = streamView[p].X;
			attribData[index + 1] = streamView[p].Y;
			attribData[index + 2] = streamView[p].Z;
			attribData[index + 3] = streamView[p].W;
			index += 4;
		}
	}

	template< class T, class T2 > Bool SetAttributeData(HAPI_Session* session, HAPI_NodeId& outHDANode, const HAPI_AttributeInfo &attribInfo, const String &name, const mesh::DataViewRO &meshView, const red::DynArray<Uint16> &chunkIndices, const EMeshStreamType &streamType)
	{
		String msg = String();
		Bool result = HAPIx(HAPI_AddAttribute(session, outHDANode, 0, name.AsChar(), &attribInfo), msg);

		T* attribData = RED_NEW_ARRAY(T, attribInfo.tupleSize * attribInfo.count, red::PoolBackend);
		Uint32 index = 0; //acumulate offset
		for (Uint32 c = 0; c < chunkIndices.Size(); ++c)
		{
			mesh::ChunkStreamDataRO chunkStream = meshView.GetChunkStreamData(chunkIndices[c], streamType);
			if (chunkStream.Empty())
			{
				// sometimes we can have chunks without stream, so we need to handle it (offset). Our base is position (points)
				// take positions count for that chunk and fill data with zeros, offset index

				Uint32 size = meshView.GetChunkStreamData(chunkIndices[c], MST_Position_3F).m_count * attribInfo.tupleSize;
				for ( Uint32 i = 0; i < size;++i)
				{
					attribData[index + i] = 0;
				}
				index += size;
			}
			else
			{
				AttribHelper<T, T2>(chunkStream, (T*)(attribData), index);
			}

		}
		if (attribInfo.storage == HAPI_STORAGETYPE_FLOAT)
		{
			result &= HAPIx(HAPI_SetAttributeFloatData(session, outHDANode, 0, name.AsChar(), &attribInfo, (float*)attribData, 0, attribInfo.count), msg);
		}
		else if (attribInfo.storage == HAPI_STORAGETYPE_INT)
		{
			result &= HAPIx(HAPI_SetAttributeIntData(session, outHDANode, 0, name.AsChar(), &attribInfo, (int*)attribData, 0, attribInfo.count), msg);
		}

		RED_DELETE_ARRAY(attribData, attribInfo.tupleSize * attribInfo.count);
		return result;
	}

	BACKEND_ENGINE_API Bool GetCustomParameter(HoudiniEngineSession* session, const HAPI_NodeId nodeId, const red::String& paramName, HAPI_ParmId& outParamId)
	{
		HAPI_NodeInfo nodeInfo;
		Bool result = false;

		if (HAPIx(HAPI_GetNodeInfo(session->Get(), nodeId, &nodeInfo)) && nodeInfo.parmCount > 0)
		{
			HAPI_ParmInfo* paramInfos = RED_NEW_ARRAY(HAPI_ParmInfo, nodeInfo.parmCount, red::PoolBackend);
			HAPIx(HAPI_GetParameters(session->Get(), nodeId, paramInfos, 0, nodeInfo.parmCount));

			for (Int32 i = 0; i < nodeInfo.parmCount; ++i)
			{
				const red::String name = HAPIs(session->Get(), paramInfos[i].nameSH);
				if (strcmp(name.AsChar(), paramName.AsChar()) == 0)
				{
					outParamId = paramInfos[i].id;
					result = true;
					break;
				}
			}
			RED_DELETE_ARRAY(paramInfos, nodeInfo.parmCount);
		}
		return result;
	}

	Bool CreateNode(THandle<IHDAInterfaceWrapper> wrapper, const HAPI_Session* session, HAPI_NodeId parentId,
		const red::String& operatorName, const red::String& name, Bool cookOnCreation, HAPI_NodeId& outNodeId, Bool forceCreation, red::String& msg)
	{
		Bool result = false;
		Bool shouldCreate = false;
		if (wrapper->GetDependencyNode(name) == -1)
		{
			shouldCreate = true;
		}
		else
		{
			if (forceCreation)
			{
				wrapper->UnloadDependencyNode(session, wrapper->GetDependencyNode(name));
				shouldCreate = true;
			}
			else
			{
				outNodeId = wrapper->GetDependencyNode(name);
				result = false;
			}
		}

		if (shouldCreate)
		{
			operatorName.Empty() ? result = HAPIx(HAPI_CreateInputNode(session, &outNodeId, name.AsChar()), msg)
				: result = HAPIx(HAPI_CreateNode(session, parentId, operatorName.AsChar(), name.AsChar(), cookOnCreation, &outNodeId), msg);

			wrapper->AddDependencyNode(outNodeId, name);
			result = true;
		}
		return result;
	}
	
	void TranslateChunkRange(const AttributeData &attributeData, red::DynArray<ChunkInfo> &items)
	{
		Uint32 startPosition = 0;
		Uint8 currentId = 0;

		const Uint32 attribCount = attributeData.m_atrribCount;
		Int32* rawData = (Int32*)(attributeData.m_attribData);

		for (Uint32 elemIndex = 0; elemIndex < attribCount; ++elemIndex)
		{
			Bool processChunk = false;
			Uint32 chunkIndex = 0u;

			if (currentId != rawData[elemIndex])
			{
				processChunk = true;
				chunkIndex = elemIndex;
			}
			if (elemIndex == attribCount - 1)
			{
				processChunk = true;
				chunkIndex = elemIndex + 1;
			}

			if (processChunk)
			{
				ChunkInfo item;
				item.m_start = startPosition;
				item.m_end = chunkIndex;
				items.PushBack(item);
				startPosition = chunkIndex;
				++currentId;
			}
		}
	}

#define HE_ATTRIB_DATA_CLEAN_UP RED_DELETE_ARRAY((Float*)outPosition.m_attribData, outPosition.m_atrribCount*outPosition.m_tupleSize); \
	RED_DELETE_ARRAY((Float*)outUv.m_attribData, outUv.m_atrribCount*outUv.m_tupleSize); \
	RED_DELETE_ARRAY((Float*)outNormal.m_attribData, outNormal.m_atrribCount*outNormal.m_tupleSize); \
	RED_DELETE_ARRAY((Float*)outTangent.m_attribData, outTangent.m_atrribCount*outTangent.m_tupleSize); \
	RED_DELETE_ARRAY((Float*)outBinormal.m_attribData, outBinormal.m_atrribCount*outBinormal.m_tupleSize); \
	RED_DELETE_ARRAY((Int32*)outWI.m_attribData, outWI.m_atrribCount*outWI.m_tupleSize); \
	RED_DELETE_ARRAY((Float*)outWT.m_attribData, outWT.m_atrribCount*outWT.m_tupleSize); \
	RED_DELETE_ARRAY((Int32*)outEWI.m_attribData, outWI.m_atrribCount*outEWT.m_tupleSize); \
	RED_DELETE_ARRAY((Float*)outEWT.m_attribData, outWT.m_atrribCount*outEWT.m_tupleSize); \
	RED_DELETE_ARRAY((Float*)outGarmentMorph.m_attribData, outGarmentMorph.m_atrribCount*outGarmentMorph.m_tupleSize); \
	RED_DELETE_ARRAY((Int32*)outGarmentFlags.m_attribData, outGarmentFlags.m_atrribCount); \
	RED_DELETE_ARRAY((Int32*)outDestruction.m_attribData, outDestruction.m_atrribCount); \
	RED_DELETE_ARRAY((Float*)outVehicleDamagePosition.m_attribData, outVehicleDamagePosition.m_atrribCount*outVehicleDamagePosition.m_tupleSize); \
	RED_DELETE_ARRAY((Float*)outVehicleDamageNormal.m_attribData, outVehicleDamageNormal.m_atrribCount*outVehicleDamageNormal.m_tupleSize); \
	RED_DELETE_ARRAY((Float*)outChunks.m_attribData, outChunks.m_atrribCount); \
	RED_DELETE_ARRAY((Int32*)outOriginalChunk.m_attribData, outOriginalChunk.m_atrribCount); \
	RED_DELETE_ARRAY((Int32*)outLods.m_attribData, outLods.m_atrribCount); \
	RED_DELETE_ARRAY((Float*)outColor.m_attribData, outColor.m_atrribCount*outColor.m_tupleSize); \
	RED_DELETE_ARRAY((Int32*)outChunkFlags.m_attribData, outLods.m_atrribCount); \

	Bool TranslateHDAToMesh(HoudiniEngineSession* session, HAPI_AssetInfo &assetInfo, HAPI_NodeId objectNode, 
		HAPI_NodeId geoNode, HAPI_PartId partId, GeometryBuffer& outGeometry)
	{
		red::String status = red::String();

		// winding flipped here!
		const Bool flipWinding = false;

		Bool isYUp = false;

		HAPI_Session* s = session->Get();
		THandle<IHDAInterfaceWrapper> wrapper = session->GetInterfaceWrapper();

		HAPI_PartInfo partInfo;
		Bool result = HAPIx(HAPI_GetPartInfo(s, geoNode, partId, &partInfo));

		if (partInfo.faceCount == 0 || partInfo.vertexCount == 0)
		{
			RED_LOG_ERROR("Houdini Engine: Node output has no geometry!");
			return false;
		}

		// required
		AttributeData outPosition;
		result &= GetAttributeData<Float>(s, geoNode, partId, HAPI_ATTROWNER_POINT, "P", outPosition);
		const Uint32 numVertices = outPosition.m_atrribCount;

		// optional
		AttributeData outNormal;
		Bool hasNormals = GetAttributeData<Float>(s, geoNode, partId, HAPI_ATTROWNER_POINT, "N", outNormal);

		AttributeData outBinormal;
		Bool hasBinormals = GetAttributeData<Float>(s, geoNode, partId, HAPI_ATTROWNER_POINT, "B", outBinormal);

		AttributeData outTangent;
		Bool hasTangents = GetAttributeData<Float>(s, geoNode, partId, HAPI_ATTROWNER_POINT, "T", outTangent);

		AttributeData outUv;
		Bool hasUVs = GetAttributeData<Float>(s, geoNode, partId, HAPI_ATTROWNER_POINT, "uv", outUv);

		AttributeData outGarmentMorph;
		Bool hasGarmentMorph = GetAttributeData<Float>(s, geoNode, partId, HAPI_ATTROWNER_POINT, "morph_offsets", outGarmentMorph);

		AttributeData outGarmentFlags;
		Bool hasGarmentFlags = GetAttributeData<Int32>(s, geoNode, partId, HAPI_ATTROWNER_POINT, "morph_flags", outGarmentFlags);

		Bool hasGarments = (hasGarmentMorph && hasGarmentFlags);

		AttributeData outWI;
		Bool hasWI = GetAttributeData<Int32>(s, geoNode, partId, HAPI_ATTROWNER_POINT, "WI", outWI);

		AttributeData outWT;
		Bool hasWT = GetAttributeData<Float>(s, geoNode, partId, HAPI_ATTROWNER_POINT, "WT", outWT);

		AttributeData outEWI;
		Bool hasEWI = GetAttributeData<Int32>(s, geoNode, partId, HAPI_ATTROWNER_POINT, "EWI", outEWI);

		AttributeData outEWT;
		Bool hasEWT = GetAttributeData<Float>(s, geoNode, partId, HAPI_ATTROWNER_POINT, "EWT", outEWT);

		AttributeData outDestruction;
		Bool hasDestruction = GetAttributeData<Int32>(s, geoNode, partId, HAPI_ATTROWNER_POINT, "destruction_indicies", outDestruction);

		Bool hasSkinning = (hasWI && hasWT);
		Bool hasExtSkinning = (hasEWI && hasEWT);

		AttributeData outChunks;
		Bool hasChunks = GetAttributeData<Int32>(s, geoNode, partId, HAPI_ATTROWNER_PRIM, "chunk_id", outChunks);

		// while generating lods we need that int to know original chunk number that was used to make new ones
		AttributeData outOriginalChunk;
		Bool hasOriginalChunks = GetAttributeData<Int32>(s, geoNode, partId, HAPI_ATTROWNER_PRIM, "original_chunk_id", outOriginalChunk);

		AttributeData outMaterial;
		Bool hasMaterial = GetAttributeData<String>(s, geoNode, partId, HAPI_ATTROWNER_PRIM, "material", outMaterial);
		
		AttributeData outLods;
		Bool hasLods = GetAttributeData<Int32>(s, geoNode, partId, HAPI_ATTROWNER_PRIM, "lod", outLods);

		AttributeData outColor;
		Bool hasColor = GetAttributeData<Float>(s, geoNode, partId, HAPI_ATTROWNER_POINT, "Cd", outColor);
		
		AttributeData outChunkFlags;
		Bool hasChunkFlags = GetAttributeData<Int32>(s, geoNode, partId, HAPI_ATTROWNER_PRIM, "chunk_flags", outChunkFlags);

		AttributeData outSimulation;
		Bool hasSimulation = GetAttributeData<Int32>(s, geoNode, partId, HAPI_ATTROWNER_POINT, "simulation", outSimulation);

		AttributeData outCollision;
		Bool hasCollision = GetAttributeData<Int32>(s, geoNode, partId, HAPI_ATTROWNER_PRIM, "collision", outCollision);

		AttributeData outOcclusion;
		Bool hasOcclusion = GetAttributeData<Int32>(s, geoNode, partId, HAPI_ATTROWNER_PRIM, "occlusion", outOcclusion);

		AttributeData outVehicleDamagePosition;
		AttributeData outVehicleDamageNormal;
		const Bool hasVDPF = GetAttributeData<Float>(s, geoNode, partId, HAPI_ATTROWNER_POINT, "VDPF", outVehicleDamagePosition);
		const Bool hasVDNF = GetAttributeData<Float>(s, geoNode, partId, HAPI_ATTROWNER_POINT, "VDNF", outVehicleDamageNormal);
		const Bool hasVehicleDamage = hasVDPF && hasVDNF;

		//check group name		
		outGeometry.m_groupName = HAPIs(s,partInfo.nameSH);

		Int32* primCollisionAttr = nullptr;
		Int32* primOcclusionAttr = nullptr;

		//check if collision mesh exists
		Int32* group_membership = RED_NEW_ARRAY(Int32, partInfo.faceCount, red::PoolBackend);

		Bool collsionGeoEqual = true; //check if all faces has the same attribute 
		Bool occlusionGeoEqual = true;

		if (hasCollision)
		{
			Bool hasAnyCollsionFaces = false; //check if group has any face with collision attribute set to 1 
			primCollisionAttr = (Int32*)(outCollision.m_attribData);
			
			for (Uint32 elemIndex = 0; elemIndex < outCollision.m_atrribCount; ++elemIndex)
			{
				if (primCollisionAttr[elemIndex] == 0) {
					collsionGeoEqual = false;
				}
				if (primCollisionAttr[elemIndex] == 1) {
					hasAnyCollsionFaces = true;
				}
				if (hasAnyCollsionFaces && !collsionGeoEqual)
					break;
			}

			if (hasAnyCollsionFaces)
			{
				//add collision geometry
				Int32* faceCounts = RED_NEW_ARRAY(Int32, partInfo.faceCount, red::PoolBackend);
				HAPIx(HAPI_GetFaceCounts(s, geoNode, partId, faceCounts, 0, partInfo.faceCount));
				Int32* vertexList = RED_NEW_ARRAY(Int32, partInfo.vertexCount, red::PoolBackend);
				HAPIx(HAPI_GetVertexList(s, geoNode, partId, vertexList, 0, partInfo.vertexCount));

				status = red::String::Printf("Importing %d triangles", partInfo.faceCount);
				wrapper->AddProgress(5.0f, status);

				Float* pointPositionData = (Float*)(outPosition.m_attribData);

				for (Uint32 elemIndex = 0; elemIndex < outPosition.m_atrribCount; ++elemIndex)
				{
					Vector3 v;
					const Uint32 rawOffset = elemIndex * outPosition.m_tupleSize;

					const Uint32 ix = rawOffset + (isYUp ? 2 : 0);
					const Uint32 iy = rawOffset + (isYUp ? 0 : 1);
					const Uint32 iz = rawOffset + (isYUp ? 1 : 2);

					v.X = pointPositionData[ix];
					v.Y = pointPositionData[iy];
					v.Z = pointPositionData[iz];
					outGeometry.m_verticesCollision.PushBack(v);
				}

				Int32 faceIndicesIndex = 0;
				for (Int32 ii = 0; ii < partInfo.faceCount; ii++)
				{

					for (Int32 jj = faceCounts[ii] - 1; jj > -1; jj--)
					{
						if (primCollisionAttr[ii] == 1) {
							const Int32 ind = flipWinding ? faceIndicesIndex + jj : faceIndicesIndex + (faceCounts[ii] - 1) - jj;
							outGeometry.m_indicesCollision.PushBack(vertexList[ind]);
						}
					}
					faceIndicesIndex += faceCounts[ii];

				}
				RED_DELETE_ARRAY(faceCounts, partInfo.faceCount);
				RED_DELETE_ARRAY(vertexList, partInfo.vertexCount);
			}
		}

		if (hasOcclusion)
		{
			Bool hasAnyOcclusionFaces = false; //check if group has any face with collision attribute set to 1 
			primOcclusionAttr = (Int32*)(outOcclusion.m_attribData);

			for (Uint32 elemIndex = 0; elemIndex < outOcclusion.m_atrribCount; ++elemIndex)
			{
				if (primOcclusionAttr[elemIndex] == 0) {
					occlusionGeoEqual = false;
				}
				if (primOcclusionAttr[elemIndex] == 1) {
					hasAnyOcclusionFaces = true;
				}
				if (hasAnyOcclusionFaces && !occlusionGeoEqual)
					break;
			}

			if (hasAnyOcclusionFaces)
			{
				//add Occlusion geometry
				Int32* faceCounts = RED_NEW_ARRAY(Int32, partInfo.faceCount, red::PoolBackend);
				HAPIx(HAPI_GetFaceCounts(s, geoNode, partId, faceCounts, 0, partInfo.faceCount));
				Int32* vertexList = RED_NEW_ARRAY(Int32, partInfo.vertexCount, red::PoolBackend);
				HAPIx(HAPI_GetVertexList(s, geoNode, partId, vertexList, 0, partInfo.vertexCount));

				status = red::String::Printf("Importing %d triangles", partInfo.faceCount);
				wrapper->AddProgress(5.0f, status);

				Float* pointPositionData = (Float*)(outPosition.m_attribData);

				for (Uint32 elemIndex = 0; elemIndex < outPosition.m_atrribCount; ++elemIndex)
				{
					Vector3 v;
					const Uint32 rawOffset = elemIndex * outPosition.m_tupleSize;

					const Uint32 ix = rawOffset + (isYUp ? 2 : 0);
					const Uint32 iy = rawOffset + (isYUp ? 0 : 1);
					const Uint32 iz = rawOffset + (isYUp ? 1 : 2);

					v.X = pointPositionData[ix];
					v.Y = pointPositionData[iy];
					v.Z = pointPositionData[iz];
					outGeometry.m_verticesOcclusion.PushBack(v);
				}

				Int32 faceIndicesIndex = 0;
				for (Int32 ii = 0; ii < partInfo.faceCount; ii++)
				{

					for (Int32 jj = faceCounts[ii] - 1; jj > -1; jj--)
					{
						if (primOcclusionAttr[ii] == 1) {
							const Int32 ind = flipWinding ? faceIndicesIndex + jj : faceIndicesIndex + (faceCounts[ii] - 1) - jj;
							outGeometry.m_indicesOcclusion.PushBack(vertexList[ind]);
						}
					}
					faceIndicesIndex += faceCounts[ii];

				}
				RED_DELETE_ARRAY(faceCounts, partInfo.faceCount);
				RED_DELETE_ARRAY(vertexList, partInfo.vertexCount);
			}
		}

		if (outLods.m_atrribCount > outChunks.m_atrribCount || numVertices < 3)
		{
			RED_LOG_ERROR("Houdini Engine: Node output has invalid geometry!");
			HE_ATTRIB_DATA_CLEAN_UP;

			return false;
		}

		if (hasChunks)
		{
			status = red::String::Printf("Importing %u chunks", outChunks.m_atrribCount);
			wrapper->AddProgress(5.0f, status);

			//geometry that comes from houdini is one big stream with attributes (chunk_id, lod)
			//that help us to split it into the chunks

			//  Data from houdini (per prim):
			//  lod:    [0000000000000000000][1111111111111111]
			//  chunk:  [0000][11111][222222][3333333][4444444]

			//  Arrays with start/end position (primitive num) we need
			//  lod:    [0---------------100][101----------200]
			//  chunk:  [0-30][31-60][61-100][101-120][121-200]

			//  geometryBuffer data structure:
			//  m_vertices
			//  m_normals
			//  m_uvs
			//  m_tangents
			//  m_binormals
			//  m_indices;
			//  m_transform;
			//  m_lods;
			//   |
			//   +--m_lod
			//      m_chunks
			//      |
			//      +--m_start
			//         m_end
			//         m_material
			//         m_flags

			red::DynArray<ChunkInfo> lods;
			TranslateChunkRange(outLods, lods);

			red::DynArray<ChunkInfo> chunks;
			TranslateChunkRange(outChunks, chunks);

			HAPI_StringHandle* rawMaterialdData = hasMaterial ? (HAPI_StringHandle*)(outMaterial.m_attribData) : nullptr;

			Uint32 currentLodId = 0;
			if (lods.Empty())
			{
				ChunkInfo encapsulingChunk = { 0u, 0xffffffff };
				lods.EmplaceBack( encapsulingChunk );
			}

			for (ChunkInfo l : lods)
			{
				LodInfo lod;
				lod.m_lod = currentLodId;
				for (ChunkInfo c : chunks)
				{
					if (l.m_start == c.m_start || l.m_end == c.m_end || (l.m_start < c.m_start && l.m_end > c.m_end))
					{	
						c.m_material = rawMaterialdData ? HAPIs(s, rawMaterialdData[c.m_start]) : "metal_base";

						if (hasChunkFlags)
						{
							Int32* flagsData = (Int32*)(outChunkFlags.m_attribData);
							c.m_flags = flagsData[c.m_start];
						}

						if (hasOriginalChunks)
						{
							Int32* orgChunkData = (Int32*)(outOriginalChunk.m_attribData);
							c.m_originalChunkId = orgChunkData[c.m_start];
						}

						if (hasSkinning)
						{
							const Bool usesLightBlockers = false; // TODO: Add support for character light blocker streams
							c.m_chunkType = SelectSkinnedVertexType( hasGarments, hasExtSkinning, usesLightBlockers, hasVehicleDamage, hasDestruction );
						}
						else if (hasVehicleDamage)
						{
							c.m_chunkType = MVT_StaticMeshVehicle;
						}
						else if (hasDestruction)
						{
							c.m_chunkType = MVT_DestructibleMesh;
						}

						lod.m_chunks.PushBack(c);
					}
				}
				outGeometry.m_lods.PushBack(lod);
				currentLodId++;
			}
		}

		Float* rawPositionData = (Float*)(outPosition.m_attribData);
		Float* rawNormalData = (Float*)(outNormal.m_attribData);
		Float* rawTangentData = (Float*)(outTangent.m_attribData);
		Float* rawBinormalData = (Float*)(outBinormal.m_attribData);
		Float* rawColorData = (Float*)(outColor.m_attribData);

		Uint32* rawWIData = (Uint32*)(outWI.m_attribData);
		Float* rawWTData = (Float*)(outWT.m_attribData);
		Uint32* rawEWIData = (Uint32*)(outEWI.m_attribData);
		Float* rawEWTData = (Float*)(outEWT.m_attribData);

		Float* rawUvData = (Float*)(outUv.m_attribData);

		Float* rawGarmentMorph = (Float*)(outGarmentMorph.m_attribData);
		Uint32* rawGarmentFlags = (Uint32*)(outGarmentFlags.m_attribData);

		Uint32* rawDestruction = (Uint32*)(outDestruction.m_attribData);

		Uint32* rawSimulationData = (Uint32*)(outSimulation.m_attribData);

		Float* rawVehicleDamagePosition = (Float*)(outVehicleDamagePosition.m_attribData);
		Float* rawVehicleDamageNormal = (Float*)(outVehicleDamageNormal.m_attribData);

		// swizzling Y / Z up
		// currently all scenes assume Z up! 
		// if needed transform inside your OTL

		status = red::String::Printf("Importing %u vertices", numVertices);
		wrapper->AddProgress(5.0f, status);

		// extract vertex data
		for (Uint32 elemIndex = 0; elemIndex < numVertices; ++elemIndex)
		{	
			DebugVertex v;
			const Uint32 rawOffset = elemIndex * outPosition.m_tupleSize;

			const Uint32 ix = rawOffset + (isYUp ? 2 : 0);
			const Uint32 iy = rawOffset + (isYUp ? 0 : 1);
			const Uint32 iz = rawOffset + (isYUp ? 1 : 2);

			v.x = rawPositionData[ix];
			v.y = rawPositionData[iy];
			v.z = rawPositionData[iz];

			if (hasColor)
			{
				Uint32 rawColorOffset = elemIndex * outColor.m_tupleSize;
				Uint32 R = Min( 255, Int32( rawColorData[rawColorOffset + 0] * 255));
				Uint32 G = Min( 255, Int32( rawColorData[rawColorOffset + 1] * 255));
				Uint32 B = Min( 255, Int32( rawColorData[rawColorOffset + 2] * 255));
				Uint32 A = 255;
				if (outColor.m_tupleSize == 4)
				{
					A = Min( 255, Int32( rawColorData[rawColorOffset + 3] * 255 ) );
				}
				v.colorLinearSpace = Color( R, G, B, A ).ToVectorLinearAccu();
			}
			outGeometry.m_vertices.PushBack(v);

			if (hasNormals)
			{ 
				const Vector3 n = Vector3( rawNormalData[ix], rawNormalData[iy], rawNormalData[iz] );
				outGeometry.m_normals.PushBack(n);
			}

			if (hasBinormals)
			{
				const Vector3 b = Vector3( rawBinormalData[ix], rawBinormalData[iy], rawBinormalData[iz] );
				outGeometry.m_binormals.PushBack(b);
			}

			if (hasTangents)
			{
				const Vector3 t = Vector3( rawTangentData[ix], rawTangentData[iy], rawTangentData[iz] );
				outGeometry.m_tangents.PushBack(t);
			}

			if (hasUVs)
			{
				const Vector2 uv = Vector2( rawUvData[ix], rawUvData[iy] );
				outGeometry.m_uvs.PushBack(uv);
			}

			if (hasGarments)
			{
				const Vector3 garment_morph = Vector3( rawGarmentMorph[ix], rawGarmentMorph[iy], rawGarmentMorph[iz] );
				outGeometry.m_garmentmorphs.PushBack(garment_morph);

				const Uint32 garment_flag = rawGarmentFlags[elemIndex];
				outGeometry.m_garmentflags.PushBack(garment_flag);
			}

			if (hasDestruction)
			{
				outGeometry.m_destruction.PushBack(rawDestruction[elemIndex]);
			}

			if (hasSimulation)
			{
				outGeometry.m_simulation.PushBack(rawSimulationData[elemIndex]);
			}

			if (hasVehicleDamage)
			{
				outGeometry.m_vehicleDamagePosition.PushBack(Vector3(
					rawVehicleDamagePosition[rawOffset],
					rawVehicleDamagePosition[rawOffset + 1],
					rawVehicleDamagePosition[rawOffset + 2]
				));
				outGeometry.m_vehicleDamageNormal.PushBack(Vector3(
					rawVehicleDamageNormal[rawOffset],
					rawVehicleDamageNormal[rawOffset + 1],
					rawVehicleDamageNormal[rawOffset + 2]
				));
			}
		}

		for (Uint32 elemIndex = 0; elemIndex < numVertices; ++elemIndex)
		{
			if (hasSkinning)
			{
				RED_ASSERT( outWI.m_tupleSize == 4 );
				const Uint32 rawOffset = elemIndex * outWI.m_tupleSize;

				Color wi = Color(static_cast<Uint8>(rawWIData[rawOffset+0]), static_cast<Uint8>(rawWIData[rawOffset+1]), static_cast<Uint8>(rawWIData[rawOffset+2]), static_cast<Uint8>(rawWIData[rawOffset+3]));
				outGeometry.m_wis.PushBack(wi);

				Vector4 wt = Vector4(rawWTData[rawOffset + 0], rawWTData[rawOffset + 1], rawWTData[rawOffset + 2], rawWTData[rawOffset + 3]);
				outGeometry.m_wts.PushBack(wt);
			}

			if (hasExtSkinning)
			{
				RED_ASSERT( outEWI.m_tupleSize == 4 );
				const Uint32 rawOffset = elemIndex * outEWI.m_tupleSize;

				Color ewi = Color(static_cast<Uint8>(rawEWIData[rawOffset+0]), static_cast<Uint8>(rawEWIData[rawOffset+1]), static_cast<Uint8>(rawEWIData[rawOffset+2]), static_cast<Uint8>(rawEWIData[rawOffset+3]));
				outGeometry.m_ewis.PushBack(ewi);

				Vector4 ewt = Vector4(rawEWTData[rawOffset + 0], rawEWTData[rawOffset + 1], rawEWTData[rawOffset + 2], rawEWTData[rawOffset + 3]);
				outGeometry.m_ewts.PushBack(ewt);
			}
		}

		if (partInfo.type != HAPI_PARTTYPE_CURVE)
		{
			Int32* faceCounts = RED_NEW_ARRAY(Int32, partInfo.faceCount, red::PoolBackend);
			HAPIx(HAPI_GetFaceCounts(s, geoNode, partId, faceCounts, 0, partInfo.faceCount));

			Int32* vertexList = RED_NEW_ARRAY(Int32, partInfo.vertexCount, red::PoolBackend);
			HAPIx(HAPI_GetVertexList(s, geoNode, partId, vertexList, 0, partInfo.vertexCount));

			status = red::String::Printf("Importing %d triangles", partInfo.faceCount);
			wrapper->AddProgress(5.0f, status);

			Int32 currIndex = 0;
			for ( Int32 faceIdx = 0; faceIdx < partInfo.faceCount; ++faceIdx )
			{
				const Bool isCollisionPrim = ( hasCollision && ( primCollisionAttr[ faceIdx ] == 1 ) && !collsionGeoEqual );
				const Bool isOcclusionPrim = ( hasOcclusion && ( primOcclusionAttr[ faceIdx ] == 1 ) && !occlusionGeoEqual );
				if ( isCollisionPrim || isOcclusionPrim )
				{
					continue;
				}

				const Uint32 numVertsInFace = faceCounts[ faceIdx ];
				for ( Uint32 vertexInFace = 0; vertexInFace < numVertsInFace; ++vertexInFace )
				{
					const Int32 ind = currIndex + vertexInFace;
					outGeometry.m_indices.PushBack( vertexList[ ind ] );

					//RED_LOG("Vertex : %d, belonging to face: %d, index: %d  of points array\n", currIndex, ii, vertexList[currIndex]);
				}
				currIndex += numVertsInFace;
			}

			RED_DELETE_ARRAY(faceCounts, partInfo.faceCount);
			RED_DELETE_ARRAY(vertexList, partInfo.vertexCount);
		}
		else
			result = false;

		HE_ATTRIB_DATA_CLEAN_UP;
		outGeometry.Shrink();

		return result;
	}

	HAPI_AttributeInfo Create_HAPI_AttributeInfo(const HAPI_PartInfo &nodePart, const HAPI_AttributeOwner &attribOwner, const int &tupleSize,
		const HAPI_StorageType &storage, const HAPI_AttributeTypeInfo &typeInfo = HAPI_ATTRIBUTE_TYPE_NONE)
	{
		int count = 0;
		if (attribOwner == HAPI_ATTROWNER_POINT)
		{
			count = nodePart.pointCount;
		}
		else if (attribOwner == HAPI_ATTROWNER_PRIM)
		{
			count = nodePart.faceCount;
		}

		HAPI_AttributeInfo newAttrInfo = HAPI_AttributeInfo_Create();
		newAttrInfo.count = count;
		newAttrInfo.tupleSize = tupleSize;
		newAttrInfo.exists = true;
		newAttrInfo.storage = storage;
		newAttrInfo.owner = attribOwner;
		newAttrInfo.originalOwner = attribOwner;
		newAttrInfo.typeInfo = typeInfo;

		return newAttrInfo;
	}

	Bool TranslateMeshToHDA(HoudiniEngineSession* session, const THandle< CMesh > meshToTranslate, 
		const ResourceEntryElement& entry, HAPI_NodeId& outHDANode, const HAPI_CookOptions* cookOptions,
		const CName& appearanceName )
	{
		// TODO translation errors reporting
		red::String msg;
		mesh::DataViewRO meshView(*meshToTranslate.Get());

		// hashing based on property, TODO find something better
		Uint64 hash = GetUniqueHash(entry);

		const red::String nodeName = red::String::Printf("%hs_%.0f", entry.m_attribName.AsChar(), (Double)hash);
		Bool cookingResult = true;
			
		// if node was created (and not re-used)
		if (CreateNode(session->GetInterfaceWrapper(), session->Get(), -1, String(), nodeName, true, outHDANode, false, msg))
		{
			red::DynArray<Uint16> chunkIndices {}; // Chunks to export
			red::DynArray<Uint8> chunkIndicesToLodIdx{}; // Chunk to LOD mapping (index = chunk, value = LOD level)
			

			HAPI_PartInfo newNodePart = HAPI_PartInfo_Create();
			newNodePart.type = HAPI_PARTTYPE_MESH;
			newNodePart.faceCount = 0;
			newNodePart.vertexCount = 0;
			newNodePart.pointCount = 0;

			// TODO: Not needed, because it's overriden anyway?
			newNodePart.attributeCounts[HAPI_ATTROWNER_VERTEX] = 0;
			newNodePart.attributeCounts[HAPI_ATTROWNER_POINT] = 1;
			newNodePart.attributeCounts[HAPI_ATTROWNER_PRIM] = 4;
			newNodePart.attributeCounts[HAPI_ATTROWNER_DETAIL] = 3;

			Uint8 processLodCount = 1;
			if (!session->GetInterfaceWrapper()->GetPropertyValue(houdini_legacy::requiresFullGeo).Empty())
			{
				processLodCount = meshView.GetNumLODs();
			}

			for (Uint8 lodIdx = 0; lodIdx < processLodCount; lodIdx++)
			{
				for (auto chunkIdx : meshView.GetLODChunkIndices(lodIdx))
				{
					chunkIndices.PushBack(chunkIdx);
					chunkIndicesToLodIdx.PushBack(lodIdx);
				}
			}

			if (chunkIndices.Size() == 0)
			{
				return false;
			}


			Bool hasPos = false;
			Bool hasColors = false;
			Bool hasUVs = false;
			Bool hasNormals = false;
			Bool hasTangents = false;
			Bool hasBinormals = false;
			Bool hasSkinning = false;
			Bool hasExtSkinning = false;
			Bool hasGarment = false;
			Bool hasDestruction = false;
			Bool hasVehicleDamage = false;

			for (auto c : chunkIndices)
			{
				//validate chunk data

				const Uint32 numChunkVerts = meshView.GetChunkStreamData(c, MST_Position_3F).m_count;
				const Uint32 numChunkIndices = meshView.GetChunkStreamData(c, MST_Index_U16).m_count;

				newNodePart.faceCount += numChunkIndices / 3;
				newNodePart.vertexCount += numChunkIndices;
				newNodePart.pointCount += numChunkVerts;

				EMeshVertexType chunkType = meshView.GetChunkVertexType(c);

				hasPos |= !meshView.GetChunkStreamData(c, MST_Position_3F).Empty();
				hasColors |= !meshView.GetChunkStreamData(c, MST_Color_U32).Empty();
				hasUVs |= !meshView.GetChunkStreamData(c, MST_TexCoord0_2F).Empty();
				hasNormals |= !meshView.GetChunkStreamData(c, MST_Normal_3F).Empty();
				hasTangents |= !meshView.GetChunkStreamData(c, MST_Tangent_3F).Empty();
				hasBinormals |= !meshView.GetChunkStreamData(c, MST_Binormal_3F).Empty();
				
				Bool chunkHasExtSkinning = !meshView.GetChunkStreamData(c, MST_SkinningWeightsExt_4F).Empty();
				Bool chunkHasSkinning = !meshView.GetChunkStreamData(c, MST_SkinningWeights_4F).Empty();
				hasExtSkinning |= chunkHasExtSkinning;
				hasSkinning |= chunkHasSkinning;

				Bool chunkHasGarment = !meshView.GetChunkStreamData(c, MST_GarmentFlags_U32).Empty();
				chunkHasGarment &= !meshView.GetChunkStreamData(c, MST_MorphOffset_3F).Empty();
				hasGarment |= chunkHasGarment;

				hasDestruction |= !meshView.GetChunkStreamData(c, MST_DestructionIndices_2U16).Empty();

				hasVehicleDamage |= (!meshView.GetChunkStreamData(c, MST_VehicleDmgPosFront_3F).Empty()) && (!meshView.GetChunkStreamData(c, MST_VehicleDmgNormalFront_3F).Empty());
			}

			const Int32 indicesCount = newNodePart.vertexCount;
			if (indicesCount < 1)
				return false;

			HAPIx(HAPI_SetPartInfo(session->Get(), outHDANode, 0, &newNodePart), msg);

			if (auto param = meshView.GetParameter<mesh::MeshParamCloth_Graphical>())
			{
				if( param->m_chunks.Size() == chunkIndices.Size() )
				{
					HAPI_AttributeInfo simInfo = Create_HAPI_AttributeInfo(newNodePart,HAPI_ATTROWNER_POINT,1,HAPI_STORAGETYPE_INT,HAPI_ATTRIBUTE_TYPE_POINT);
					Bool result = HAPIx(HAPI_AddAttribute(session->Get(),outHDANode,0,"simulation",&simInfo),msg);
					red::DynArray<Int32> simData(newNodePart.pointCount,red::PoolBackend());

					Uint32 offset = 0;
					for(auto c : chunkIndices)
					{
						auto simulation = red::MakeArraySpan(param->m_chunks[c].m_simulation);
						auto indices = mesh::MakeSpan<Uint16>(meshView,c,MST_Index_U16);
						for(auto idx : simulation)
						{
							simData[idx + offset] = 1;
						}

						offset += meshView.GetChunkStreamData(c,MST_Position_3F).m_count;
					}

					HAPIx(HAPI_SetAttributeIntData(session->Get(),outHDANode,0,"simulation",&simInfo,simData.TypedData(),0,simInfo.count),msg);
				}
				else
				{
					RED_REPORT_ERROR(RED_LOCATION(meshToTranslate->GetPath()), "Failed to update Graphical Cloth Mesh Parameter. Please reimport mesh from re file to correct this");
					return false;
				}
			}

			if (hasPos)
			{
				HAPI_AttributeInfo newNodeInfo = Create_HAPI_AttributeInfo(newNodePart, HAPI_ATTROWNER_POINT, 3, HAPI_STORAGETYPE_FLOAT, HAPI_ATTRIBUTE_TYPE_POINT);
				SetAttributeData< Float, Vector3 >(session->Get(), outHDANode, newNodeInfo, "P", meshView, chunkIndices, MST_Position_3F);
			}
			if (hasColors)
			{
				HAPI_AttributeInfo newNodeInfo = Create_HAPI_AttributeInfo(newNodePart, HAPI_ATTROWNER_POINT, 4, HAPI_STORAGETYPE_FLOAT, HAPI_ATTRIBUTE_TYPE_COLOR);
				SetAttributeData< Float, Color >(session->Get(), outHDANode, newNodeInfo, "Cd", meshView, chunkIndices, MST_Color_U32);
			}
			if (hasNormals)
			{
				HAPI_AttributeInfo newNodeInfo = Create_HAPI_AttributeInfo(newNodePart, HAPI_ATTROWNER_POINT, 3, HAPI_STORAGETYPE_FLOAT, HAPI_ATTRIBUTE_TYPE_NORMAL);
				SetAttributeData< Float, Vector3 >(session->Get(), outHDANode, newNodeInfo, "N", meshView, chunkIndices, MST_Normal_3F);
			}
			if (hasUVs)
			{
				HAPI_AttributeInfo newNodeInfo = Create_HAPI_AttributeInfo(newNodePart, HAPI_ATTROWNER_POINT, 3, HAPI_STORAGETYPE_FLOAT, HAPI_ATTRIBUTE_TYPE_TEXTURE);
				SetAttributeData< Float, Vector2 >(session->Get(), outHDANode, newNodeInfo, "uv", meshView, chunkIndices, MST_TexCoord0_2F);
			}
			if (hasTangents)
			{
				HAPI_AttributeInfo newNodeInfo = Create_HAPI_AttributeInfo(newNodePart, HAPI_ATTROWNER_POINT, 3, HAPI_STORAGETYPE_FLOAT, HAPI_ATTRIBUTE_TYPE_NONE);
				SetAttributeData< Float, Vector3 >(session->Get(), outHDANode, newNodeInfo, "T", meshView, chunkIndices, MST_Tangent_3F);
			}
			if (hasBinormals)
			{
				HAPI_AttributeInfo newNodeInfo = Create_HAPI_AttributeInfo(newNodePart, HAPI_ATTROWNER_POINT, 3, HAPI_STORAGETYPE_FLOAT, HAPI_ATTRIBUTE_TYPE_NONE);
				SetAttributeData< Float, Vector3 >(session->Get(), outHDANode, newNodeInfo, "B", meshView, chunkIndices, MST_Binormal_3F);
			}
			if (hasSkinning)
			{
				{
					HAPI_AttributeInfo newNodeInfo = Create_HAPI_AttributeInfo(newNodePart, HAPI_ATTROWNER_POINT, 4, HAPI_STORAGETYPE_INT, HAPI_ATTRIBUTE_TYPE_NONE);
					SetAttributeData< Uint32, Color >(session->Get(), outHDANode, newNodeInfo, "WI", meshView, chunkIndices, MST_SkinningIndices_4U8);
				}
				{
					HAPI_AttributeInfo newNodeInfo = Create_HAPI_AttributeInfo(newNodePart, HAPI_ATTROWNER_POINT, 4, HAPI_STORAGETYPE_FLOAT, HAPI_ATTRIBUTE_TYPE_NONE);
					SetAttributeData< Float, Vector4 >(session->Get(), outHDANode, newNodeInfo, "WT", meshView, chunkIndices, MST_SkinningWeights_4F);
				}
			}
			if (hasExtSkinning)
			{
				{
					HAPI_AttributeInfo newNodeInfo = Create_HAPI_AttributeInfo(newNodePart, HAPI_ATTROWNER_POINT, 4, HAPI_STORAGETYPE_INT, HAPI_ATTRIBUTE_TYPE_NONE);
					SetAttributeData< Uint32, Color >(session->Get(), outHDANode, newNodeInfo, "EWI", meshView, chunkIndices, MST_SkinningIndicesExt_4U8);
				}
				{
					HAPI_AttributeInfo newNodeInfo = Create_HAPI_AttributeInfo(newNodePart, HAPI_ATTROWNER_POINT, 4, HAPI_STORAGETYPE_FLOAT, HAPI_ATTRIBUTE_TYPE_NONE);
					SetAttributeData< Float, Vector4 >(session->Get(), outHDANode, newNodeInfo, "EWT", meshView, chunkIndices, MST_SkinningWeightsExt_4F);
				}
			}
			if (hasGarment)
			{
				{
					HAPI_AttributeInfo newNodeInfo = Create_HAPI_AttributeInfo(newNodePart, HAPI_ATTROWNER_POINT, 3, HAPI_STORAGETYPE_FLOAT, HAPI_ATTRIBUTE_TYPE_NONE);
					SetAttributeData< Float, Vector3 >(session->Get(), outHDANode, newNodeInfo, "morph_offsets", meshView, chunkIndices, MST_MorphOffset_3F);
				}
				{
					HAPI_AttributeInfo newNodeInfo = Create_HAPI_AttributeInfo(newNodePart, HAPI_ATTROWNER_POINT, 1, HAPI_STORAGETYPE_INT, HAPI_ATTRIBUTE_TYPE_NONE);
					SetAttributeData< Uint32, Uint32 >(session->Get(), outHDANode, newNodeInfo, "morph_flags", meshView, chunkIndices, MST_GarmentFlags_U32);
				}
			}
			if (hasDestruction)
			{
				HAPI_AttributeInfo newNodeInfo = Create_HAPI_AttributeInfo(newNodePart, HAPI_ATTROWNER_POINT, 1, HAPI_STORAGETYPE_INT, HAPI_ATTRIBUTE_TYPE_NONE);
				SetAttributeData< Uint32, Uint32 >(session->Get(), outHDANode, newNodeInfo, "destruction_indicies", meshView, chunkIndices, MST_DestructionIndices_2U16);
			}
			if (hasVehicleDamage)
			{
				{
					HAPI_AttributeInfo newNodeInfo = Create_HAPI_AttributeInfo(newNodePart, HAPI_ATTROWNER_POINT, 3, HAPI_STORAGETYPE_FLOAT, HAPI_ATTRIBUTE_TYPE_NONE);
					SetAttributeData< Float, Vector3 >(session->Get(), outHDANode, newNodeInfo, "VDPF", meshView, chunkIndices, MST_VehicleDmgPosFront_3F);
				}
				{
					HAPI_AttributeInfo newNodeInfo = Create_HAPI_AttributeInfo(newNodePart, HAPI_ATTROWNER_POINT, 3, HAPI_STORAGETYPE_FLOAT, HAPI_ATTRIBUTE_TYPE_NONE);
					SetAttributeData< Float, Vector3 >(session->Get(), outHDANode, newNodeInfo, "VDNF", meshView, chunkIndices, MST_VehicleDmgNormalFront_3F);
				}
			}

			HAPI_AttributeInfo newMaterialPrimInfo = Create_HAPI_AttributeInfo(newNodePart, HAPI_ATTROWNER_PRIM, 1, HAPI_STORAGETYPE_STRING);
			HAPI_AttributeInfo newShaderPrimInfo = Create_HAPI_AttributeInfo( newNodePart, HAPI_ATTROWNER_PRIM, 1, HAPI_STORAGETYPE_STRING );
			HAPI_AttributeInfo newChunkFlagsPrimInfo = Create_HAPI_AttributeInfo(newNodePart, HAPI_ATTROWNER_PRIM, 1, HAPI_STORAGETYPE_INT);
			HAPI_AttributeInfo newChunkIdPrimInfo = Create_HAPI_AttributeInfo(newNodePart, HAPI_ATTROWNER_PRIM, 1, HAPI_STORAGETYPE_INT);
			HAPI_AttributeInfo newLodIdPrimInfo = Create_HAPI_AttributeInfo(newNodePart, HAPI_ATTROWNER_PRIM, 1, HAPI_STORAGETYPE_INT);

			cookingResult &= HAPIx(HAPI_AddAttribute(session->Get(), outHDANode, 0, "chunk_id", &newChunkIdPrimInfo), msg);
			cookingResult &= HAPIx(HAPI_AddAttribute(session->Get(), outHDANode, 0, "lod", &newLodIdPrimInfo), msg);
			cookingResult &= HAPIx(HAPI_AddAttribute(session->Get(), outHDANode, 0, "material", &newMaterialPrimInfo), msg);
			cookingResult &= HAPIx( HAPI_AddAttribute( session->Get(), outHDANode, 0, "shader", &newShaderPrimInfo ), msg );
			cookingResult &= HAPIx(HAPI_AddAttribute(session->Get(), outHDANode, 0, "chunk_flags", &newChunkFlagsPrimInfo), msg);

			char ** materials = new char *[newMaterialPrimInfo.tupleSize * newMaterialPrimInfo.count];
			char ** shaders = new char *[ newShaderPrimInfo.tupleSize * newShaderPrimInfo.count ];

			Int32* flags = RED_NEW_ARRAY(Int32, newChunkFlagsPrimInfo.tupleSize * newChunkFlagsPrimInfo.count, red::PoolBackend);
			Int32* chunks = RED_NEW_ARRAY(Int32, newChunkIdPrimInfo.tupleSize * newChunkIdPrimInfo.count, red::PoolBackend);
			Int32* lods = RED_NEW_ARRAY(Int32, newLodIdPrimInfo.tupleSize * newLodIdPrimInfo.count, red::PoolBackend);

			Int32* vertices = RED_NEW_ARRAY(Int32, indicesCount, red::PoolBackend);

			Uint32 index = 0;
			Int32 accum_indicies = 0;
			Uint16 chunk_id = 0;

			red::DynArray<Uint32> indices;
			
			auto appearanceIdx = meshView.GetAppearanceIndex( appearanceName );
			if ( appearanceIdx == -1 )
			{
				appearanceIdx = meshView.GetAppearanceIndex( "" ); // Get the default appearance
			}

			for (Uint32 c = 0; c < chunkIndices.Size(); ++c)
			{
				mesh::ChunkStreamDataRO chunkStreamI = meshView.GetChunkStreamData(chunkIndices[c], MST_Index_U16);
				mesh::StreamViewRO< Uint16 > indicesStreamView(chunkStreamI);
				
				const CName materialName = meshView.GetAppearanceMaterialName( appearanceIdx, c, 0 );
				
				String shaderName;
				if ( THandle< IMaterial > materialData = meshView.GetAppearanceMaterialData( appearanceIdx, c, 0 ) )
				{
					shaderName = materialData->GetMaterialDefinition()->GetShaderName();
				}
				Uint8 lod_id = chunkIndicesToLodIdx[chunk_id];

				mesh::ChunkFlags chunkFlags = meshView.GetChunkFlags(c);
				Uint32 packedFlags = meshView.PackFlags(chunkFlags);

				for (Uint32 j = 0; j < indicesStreamView.Size(); ++j)
				{
					vertices[index] = indicesStreamView[j] + accum_indicies;
					chunks[index / 3] = chunk_id;
					lods[index / 3] = lod_id;
					materials[index / 3] = _strdup( materialName.AsChar() );
					shaders[index / 3] = _strdup( shaderName.AsChar() );
					flags[index / 3] = packedFlags;
					indices.PushBack(indicesStreamView[j] + accum_indicies);
					index++;
				}
				accum_indicies += meshView.GetChunkVertexCount(chunkIndices[c]);
				chunk_id++;
			}
			cookingResult &= HAPIx(HAPI_SetAttributeIntData(session->Get(), outHDANode, 0, "chunk_id", &newChunkIdPrimInfo, chunks, 0, newNodePart.faceCount), msg);
			cookingResult &= HAPIx(HAPI_SetAttributeIntData( session->Get(), outHDANode, 0, "lod", &newLodIdPrimInfo, lods, 0, newNodePart.faceCount ), msg );
			cookingResult &= HAPIx(HAPI_SetAttributeStringData(session->Get(), outHDANode, 0, "material", &newMaterialPrimInfo, (const char**)materials, 0, newNodePart.faceCount), msg);
			cookingResult &= HAPIx( HAPI_SetAttributeStringData( session->Get(), outHDANode, 0, "shader", &newShaderPrimInfo, ( const char** )shaders, 0, newNodePart.faceCount ), msg );
			cookingResult &= HAPIx(HAPI_SetAttributeIntData(session->Get(), outHDANode, 0, "chunk_flags", &newChunkFlagsPrimInfo, flags, 0, newNodePart.faceCount), msg);
			cookingResult &= HAPIx(HAPI_SetVertexList(session->Get(), outHDANode, 0, vertices, 0, indicesCount), msg);

			// @brief  Set the array of faces where the nth integer in the array is
			//         the number of vertices the nth face has.
			Int32* face_counts = RED_NEW_ARRAY(Int32, newNodePart.faceCount, red::PoolBackend);
			for (Int32 c = 0; c < newNodePart.faceCount; ++c)
			{
				face_counts[c] = 3;
			}
			cookingResult &= HAPIx(HAPI_SetFaceCounts(session->Get(), outHDANode, 0, face_counts, 0, newNodePart.faceCount), msg);

			// store engine resource and import source path
			const red::StringView meshName = red::paths::GetFileStem(red::paths::GetFileName(entry.m_resourcePath));

			cookingResult &= AddDetailAttribute(session->Get(), outHDANode, "engine_path", entry.m_resourcePath);
			cookingResult &= AddDetailAttribute(session->Get(), outHDANode, "source_path", entry.m_sourceImportPath.AsString());
			cookingResult &= AddDetailAttribute(session->Get(), outHDANode, "name", meshName.Data());

			cookingResult &= HAPIx(HAPI_CommitGeo(session->Get(), outHDANode), msg);
			cookingResult = HAPIx(HAPI_CookNode(session->Get(), outHDANode, cookOptions), msg);

			// to avoid stomps, wait until cooking finishes
			if (HAPIc(session))
			{
				RED_DELETE_ARRAY(chunks, newChunkIdPrimInfo.tupleSize * newChunkIdPrimInfo.count);
				RED_DELETE_ARRAY(lods, newLodIdPrimInfo.tupleSize * newLodIdPrimInfo.count);
				RED_DELETE_ARRAY(vertices, indicesCount);
				RED_DELETE_ARRAY(face_counts, newNodePart.faceCount);
				RED_DELETE_ARRAY(flags, newChunkFlagsPrimInfo.tupleSize * newChunkFlagsPrimInfo.count);
				delete[] materials; 
				delete[] shaders;
			}
		}
		if (!cookingResult)
		{
			msg += red::String::Printf("\nCooking: %hs finished with errors.", nodeName.AsChar());
		}

		/*
		Int32 bufLength = 0;
		HAPI_ComposeNodeCookResult(session, outHDANode, HAPI_STATUSVERBOSITY_ALL, &bufLength);
		char* cMsg = new char[bufLength];
		HAPI_GetComposedNodeCookResult(session, cMsg, bufLength);
		msg += red::String::Printf("%s", cMsg);

		HAPI_ErrorCodeBits ebit = 1;
		HAPI_ErrorCodeBits errorsFound;
		HAPI_CheckForSpecificErrors(session, outHDANode, ebit, &errorsFound);
		*/

		// node was added (even if cooking errors occurred)
		// keeps scene dependency valid
		return true;
	}

	// set node transform
	Bool SetNodeTransform(const HAPI_Session* session, ResourceEntryElement& nodeEntry, HAPI_NodeId newNodeId, const Transform& rootTransform, const Vector3& rootScale)
	{
		HAPI_NodeInfo outNodeIdInfo;
		HAPIx(HAPI_GetNodeInfo(session, newNodeId, &outNodeIdInfo));

		HAPI_NodeId transformNodeId = outNodeIdInfo.type == HAPI_NODETYPE_SOP ? outNodeIdInfo.parentId : newNodeId;

		Matrix mat = nodeEntry.m_transform.ToMatrix();
		Matrix rootMat = rootTransform.ToInvMatrix();
		mat = mat * rootMat;

		const Vector3 nodePosition = mat.GetTranslation();

		HAPI_TransformEuler eulerTransform;
		eulerTransform.position[0] = nodePosition.X;
		eulerTransform.position[1] = nodePosition.Y;
		eulerTransform.position[2] = nodePosition.Z;

		eulerTransform.rotationOrder = HAPI_XYZ;
		eulerTransform.rstOrder = HAPI_RSTORDER_DEFAULT;

		const Quaternion nodeQuat = mat.ToQuat();
		eulerTransform.rotationEuler[0] = nodeQuat.ToEulerAngles().Pitch;
		eulerTransform.rotationEuler[1] = nodeQuat.ToEulerAngles().Roll;
		eulerTransform.rotationEuler[2] = nodeQuat.ToEulerAngles().Yaw;

		eulerTransform.scale[0] = nodeEntry.m_scale.X;
		eulerTransform.scale[1] = nodeEntry.m_scale.Y;
		eulerTransform.scale[2] = nodeEntry.m_scale.Z;

		eulerTransform.shear[0] = 0.0f;
		eulerTransform.shear[1] = 0.0f;
		eulerTransform.shear[2] = 0.0f;

		// set transform on object node
		return HAPIx(HAPI_SetObjectTransform(session, transformNodeId, &eulerTransform));
	}

	// add correct references to newly added node(s)
	Bool MapReferences(HoudiniEngineSession* session, ResourceEntryElement& nodeEntry, HAPI_NodeId newNodeId)
	{	
		red::String cookingMsg = red::String();
		const HAPI_NodeId nodeID = session->GetInterfaceWrapper()->GetNodeID();
		HAPI_NodeInfo nodeInfo;
		Bool result = HAPIx(HAPI_GetNodeInfo(session->Get(), nodeID, &nodeInfo));

		if (result && nodeInfo.parmCount > 0)
		{
			HAPI_ParmInfo* paramInfos = RED_NEW_ARRAY(HAPI_ParmInfo, nodeInfo.parmCount, red::PoolBackend);
			result &= HAPIx(HAPI_GetParameters(session->Get(), nodeID, paramInfos, 0, nodeInfo.parmCount));

			for (Int32 i = 0; i < nodeInfo.parmCount; ++i)
			{
				const red::String paramName = HAPIs(session->Get(), paramInfos[i].nameSH);
				if (strcmp( paramName.AsChar(), nodeEntry.m_attribName.AsChar()) == 0 )
				{
					if (paramInfos[i].type == HAPI_PARMTYPE_NODE)
					{
						result &= HAPIx(HAPI_SetParmNodeValue(session->Get(), nodeID, paramName.AsChar(), newNodeId));
					}
					else if (paramInfos[i].type == HAPI_PARMTYPE_PATH_FILE)
					{
						result &= HAPIx(HAPI_SetParmStringValue(session->Get(), nodeID, nodeEntry.m_attribValue.AsChar(), paramInfos[i].id, 0));
					}
				}
			}
			RED_DELETE_ARRAY(paramInfos, nodeInfo.parmCount);
		}

		// wait until cooking finishes
		if (!HAPIc(session))
		{
			session->m_msg += red::String::Printf("%hs - %hs\n", session->GetInterfaceWrapper()->GetNodeNamespace().AsChar(), cookingMsg.AsChar());
 			result = false;
		}
		return result;
	}

	Bool SetHeightfieldData(const HAPI_Session* session, const HAPI_NodeId volumeNodeId, const HAPI_PartId partId, 
		const red::String& name, Float* data, const Uint32 dataSize, const HAPI_VolumeInfo& VolumeInfo)
	{
		Bool result = true;
		red::String cookingMsg = String();
		
		result &= HAPIx(HAPI_CookNode(session, volumeNodeId, nullptr), cookingMsg);

		// Read the geo/part/volume info from the volume node
		HAPI_GeoInfo geoInfo = HAPI_GeoInfo_Create();
		result &= HAPIx(HAPI_GetGeoInfo(session, volumeNodeId, &geoInfo), cookingMsg);

		HAPI_PartInfo partInfo;
		result &= HAPIx(HAPI_GetPartInfo(session, geoInfo.nodeId, partId, &partInfo), cookingMsg);

		HAPI_VolumeInfo hapiVolumeInfo = HAPI_VolumeInfo_Create();
		result &= HAPIx(HAPI_GetVolumeInfo(session, volumeNodeId, partInfo.id, &hapiVolumeInfo), cookingMsg);

		result &= HAPIx(HAPI_SetVolumeInfo(session, volumeNodeId, partInfo.id, &VolumeInfo), cookingMsg);

		// Set the data on the volume
		result &= HAPIx(HAPI_SetHeightFieldData(session, geoInfo.nodeId, partInfo.id, name.AsChar(), data, 0, dataSize), cookingMsg);
		
		return result;
	}

	Bool AddHeightfieldSOPNode(HoudiniEngineSession* session, ResourceEntryElement& nodeEntry, TerrainDataHelper dataHelper)
	{
		Bool result = true;
		Bool updateHeightAndMask = true;

		const HAPI_Session* s = session->Get();
		red::String cookingMsg = red::String();

		const red::String nodeDepName = "import_heighfield";
		THandle<IHDAInterfaceWrapper> wrapper = session->GetInterfaceWrapper();
		HAPI_NodeId heightFieldId = wrapper->GetDependencyNode(nodeDepName);

		HAPI_NodeId heightId = -1;
		HAPI_NodeId maskId = -1;
		HAPI_NodeId mergeId = -1;

		// create from scratch
		if (heightFieldId == -1)
		{
			result &= HAPIx(HAPI_CreateHeightfieldInputNode(s, -1, "heightfield", dataHelper.m_heightSizex, dataHelper.m_heightSizey, 1.0f, &heightFieldId, &heightId, &maskId, &mergeId));
			result &= HAPIx(HAPI_CookNode(s, heightFieldId, nullptr));
			wrapper->AddDependencyNode(heightFieldId, nodeDepName);
			wrapper->AddDependencyNode(heightId, "heightId");
			wrapper->AddDependencyNode(maskId, "maskId");
			wrapper->AddDependencyNode(mergeId, "mergeId");
		}
		// re-use
		else
		{
			if (wrapper->hasOutputType(hOutput_Heightmap_InitializeOnly))
				updateHeightAndMask = false;

			heightId = wrapper->GetDependencyNode("heightId");
			maskId = wrapper->GetDependencyNode("maskId");
			mergeId = wrapper->GetDependencyNode("mergeId");
		}

		const Float heightSizeX = nodeEntry.m_heightfieldDataSizeX;
		const Float heightSizeY = nodeEntry.m_heightfieldDataSizeY;

		HAPI_Transform transform = HAPI_Transform_Create();
		InitArray<Float>(transform.position, 0.0f, HAPI_POSITION_VECTOR_SIZE);
		InitArray<Float>(transform.rotationQuaternion, 0.0f, HAPI_QUATERNION_VECTOR_SIZE);
		InitArray<Float>(transform.scale, 1.0f, HAPI_SCALE_VECTOR_SIZE);
		InitArray<Float>(transform.shear, 0.0f, HAPI_SHEAR_VECTOR_SIZE);
		transform.rstOrder = HAPI_RSTORDER_DEFAULT;
		transform.scale[0] = heightSizeX;
		transform.scale[1] = heightSizeY;

		HAPI_VolumeInfo volumeInfo = HAPI_VolumeInfo_Create();
		volumeInfo.hasTaper = false;
		volumeInfo.minX = 0;
		volumeInfo.minY = 0;
		volumeInfo.minZ = 0;
		volumeInfo.tileSize = 1;
		volumeInfo.xLength = dataHelper.m_heightSizex;
		volumeInfo.yLength = dataHelper.m_heightSizey;
		volumeInfo.zLength = 1;
		volumeInfo.storage = HAPI_STORAGETYPE_FLOAT;
		volumeInfo.tupleSize = 1;
		volumeInfo.type = HAPI_VOLUMETYPE_HOUDINI;
		volumeInfo.transform = transform;

		const Uint32 dataSize = dataHelper.m_heightSizex*dataHelper.m_heightSizey; //assuming all data has the same size as height!

		if (dataHelper.m_heightRawData != nullptr && updateHeightAndMask)
		{
			// set height
			HAPI_PartId partId = 0;
			result &= SetHeightfieldData(s, heightId, partId, "height", dataHelper.m_heightRawData, dataSize, volumeInfo);
			result &= HAPIx(HAPI_CommitGeo(s, heightId), cookingMsg);

			// required by HAPI
			if (dataHelper.m_maskRawData == nullptr)
			{
				// freed by parent
				dataHelper.m_maskRawData = RED_NEW_ARRAY(Float, dataHelper.m_heightSizex*dataHelper.m_heightSizey, red::PoolBackend);
				InitArray<Float>(dataHelper.m_maskRawData, 0.0f, dataHelper.m_heightSizex*dataHelper.m_heightSizey);
			}

			// set mask
			result &= SetHeightfieldData(s, maskId, partId, "mask", dataHelper.m_maskRawData, dataSize, volumeInfo);
			result &= HAPIx(HAPI_CommitGeo(s, maskId), cookingMsg);

		}

		//create mlMasks
		/*
		to do:
		optional - add proper transform for mlMask nodes
		*/
		if (dataHelper.mlMasksNames.Size() != 0)
		{
			for (Uint32 i = 0; i < dataHelper.mlMasksNames.Size(); i++)
			{
				Bool updateMlmasks = true;
				const String maskName = dataHelper.mlMasksNames[i];
				HAPI_NodeId mlMasksNodeId = wrapper->GetDependencyNode(maskName);

				if (mlMasksNodeId == -1)
				{
					//create new Heightfield node
					HAPI_CreateHeightfieldInputVolumeNode(s, -1, &mlMasksNodeId, maskName.AsChar(), dataHelper.m_heightSizex, dataHelper.m_heightSizey, 1.0f);
				}
				else
				{
					if (hOutput_MlMask_InitializeOnly) updateMlmasks = false;
				}

				if (updateMlmasks)
				{
					//copy data
					Float* mask = RED_NEW_ARRAY(Float, dataSize, red::PoolBackend);

					for (Uint32 p = 0; p < dataSize; p++)
					{
						mask[p] = (Float)(dataHelper.mlMasksRawData[i][p] * 255.f);
					}

					HAPI_PartId mlMaskPartId = 0;// one data per node - always set to 0 
					result &= SetHeightfieldData(s, mlMasksNodeId, mlMaskPartId, maskName, mask, dataSize, volumeInfo);
					result &= HAPIx(HAPI_CommitGeo(s, mlMasksNodeId), cookingMsg);
					RED_DELETE_ARRAY(mask, dataSize);
					wrapper->AddDependencyNode(mlMasksNodeId, maskName);
					MapReferences(session, nodeEntry, mlMasksNodeId);
				}
			}
		}

		result &= HAPIx(HAPI_CookNode(s, heightFieldId, nullptr), cookingMsg);

		HAPI_ParmId parmId = -1;
		if (GetCustomParameter(session, wrapper->GetNodeID(), "heightMin", parmId))
			result &= HAPIx(HAPI_SetParmFloatValue(s, wrapper->GetNodeID(), "heightMin", 0, dataHelper.m_heightMin));

		Transform rootTransform = Transform::IDENTITY();
		Vector3 rootScale = Vector3::ONES();
		wrapper->GetActivePrefabTransform(rootTransform, rootScale);
		rootScale *= nodeEntry.m_scale;
		SetNodeTransform(session->Get(), nodeEntry, heightFieldId, rootTransform, rootScale);

		if (result)
		{
			result &= MapReferences(session, nodeEntry, heightFieldId);
		}
		else
		{
			session->m_msg += red::String::Printf("HAPI: Unable to cook: %hs", wrapper->GetNodeNamespace().AsChar());
		}
		return result;
	}

	Bool AddSOPCurveNode(HoudiniEngineSession* session, ResourceEntryElement& nodeEntry, red::DynArray <Vector3> positions)
	{
		//we need update curve in every cook
		Bool result = true;
		HAPI_NodeId newNodeId = -1;
		red::String msg;
		THandle<IHDAInterfaceWrapper> wrapper = session->GetInterfaceWrapper();
		int curveCount = positions.Size();

		if (curveCount!=0)
		{
			Uint64 hash = ToStringDirect<Vector3>(nodeEntry.m_transform.GetPosition3()).CalcHash();
			hash += ToStringDirect<Vector4>(nodeEntry.m_transform.GetOrientation().AsVector()).CalcHash();
			hash += ToStringDirect<Vector3>(nodeEntry.m_scale).CalcHash();

			const red::String nodeName = red::String::Printf("%hs_%.0f", nodeEntry.m_attribName.AsChar(), (Double)hash);
			result = true;
			
			if (CreateNode(wrapper, session->Get(), -1, "sop/RedCurve", nodeName, true, newNodeId, true, msg)) 
			{
				HAPI_NodeInfo curveNodeInfo;
				HAPIx(HAPI_GetNodeInfo(session->Get(), newNodeId, &curveNodeInfo));
				HAPI_ParmInfo * parmInfos = new HAPI_ParmInfo[curveNodeInfo.parmCount];
				HAPIx(HAPI_GetParameters(session->Get(), newNodeId, parmInfos, 0, curveNodeInfo.parmCount));

				String curvePoints = "";
				for (Uint32 index = 0; index < positions.Size(); index++) {
					String pointPos = String::Printf("%f,%f,%f ", positions[index][0], positions[index][1], positions[index][2]);
					curvePoints.Append(pointPos.AsChar(), pointPos.Length());
				}

				for (int i = 0; i < curveNodeInfo.parmCount; i++)
				{
					red::String parmName = HAPIs(session->Get(),parmInfos[i].nameSH);
					if (parmName.BeginsWith("coords"))
					{
						HAPIx(HAPI_SetParmStringValue(session->Get(), newNodeId, curvePoints.AsChar(), i, 0));
					}
					if (parmName.BeginsWith("type"))
					{
						int typeValue = 1; //nurbs curve
						HAPIx(HAPI_SetParmIntValues(session->Get(), newNodeId, &typeValue, i, 1));
					}
					if (parmName.BeginsWith("nodeRef"))
					{
						HAPIx(HAPI_SetParmStringValue(session->Get(), newNodeId, nodeEntry.m_attribValue.AsChar(), i, 0));
					}
					if (parmName.BeginsWith("curveName"))
					{
						HAPIx(HAPI_SetParmStringValue(session->Get(), newNodeId, nodeEntry.m_curveData.m_curveName.AsChar(), i, 0));
					}
					if (parmName.BeginsWith("radiusMultiply"))
					{
						HAPIx(HAPI_SetParmFloatValue(session->Get(), newNodeId, "radiusMultiply", 0, nodeEntry.m_curveData.m_radiusMultiply));
					}
					if (parmName.BeginsWith("restLength"))
					{
						HAPIx(HAPI_SetParmFloatValue(session->Get(), newNodeId, "restLength", 0, nodeEntry.m_curveData.m_restlength));
					}
					if (parmName.BeginsWith("bendStiffness"))
					{
						HAPIx(HAPI_SetParmFloatValue(session->Get(), newNodeId, "bendStiffness", 0, nodeEntry.m_curveData.m_bendStiffness));
					}
					if (parmName.BeginsWith("sides"))
					{
						HAPIx(HAPI_SetParmIntValue(session->Get(), newNodeId, "sides", 0, nodeEntry.m_curveData.m_sides));
					}
					if (parmName.BeginsWith("curveQuality"))
					{
						HAPIx(HAPI_SetParmFloatValue(session->Get(), newNodeId, "curveQuality", 0, nodeEntry.m_curveData.m_curveQuality));
					}
					if (parmName.BeginsWith("curveQualityCollision"))
					{
						HAPIx(HAPI_SetParmFloatValue(session->Get(), newNodeId, "curveQualityCollision", 0, nodeEntry.m_curveData.m_curveQualityCollision));
					}
					if (parmName.BeginsWith("finalReduce"))
					{
						HAPIx(HAPI_SetParmFloatValue(session->Get(), newNodeId, "finalReduce", 0, nodeEntry.m_curveData.m_finalReduce));
					}
					if (parmName.BeginsWith("finalCollisionReduce"))
					{
						HAPIx(HAPI_SetParmFloatValue(session->Get(), newNodeId, "finalCollisionReduce", 0, nodeEntry.m_curveData.m_finalCollisionReduce));
					}
					if (parmName.BeginsWith("materialPath"))
					{
						HAPIx(HAPI_SetParmStringValue(session->Get(), newNodeId, nodeEntry.m_curveData.m_materialPath.AsChar(), i, 0));
					}
					if (parmName.BeginsWith("outputResourcePath"))
					{
						HAPIx(HAPI_SetParmStringValue(session->Get(), newNodeId, nodeEntry.m_curveData.m_outputResourcePath.AsChar(), i, 0));
					}
					if (parmName.BeginsWith("uvRangeStart"))
					{
						HAPIx(HAPI_SetParmIntValue(session->Get(), newNodeId, "uvRangeStart", 0, nodeEntry.m_curveData.m_uvRangeStart));
					}
					if (parmName.BeginsWith("uvRangeEnd"))
					{
						HAPIx(HAPI_SetParmIntValue(session->Get(), newNodeId, "uvRangeEnd", 0, nodeEntry.m_curveData.m_uvRangeEnd));
					}
					if (parmName.BeginsWith("destructionOwnerHashStart"))
					{
						HAPIx( HAPI_SetParmStringValue(session->Get(), newNodeId, nodeEntry.m_curveData.m_destructionOwnerHashStart.AsChar(), i, 0));
					}
					if (parmName.BeginsWith("destructionOwnerHashEnd"))
					{
						HAPIx( HAPI_SetParmStringValue(session->Get(), newNodeId, nodeEntry.m_curveData.m_destructionOwnerHashEnd.AsChar(), i, 0));
					}
				}

				//add transform after create node
				Transform rootTransform = Transform::IDENTITY();
				Vector3 rootScale = Vector3::ONES();
				wrapper->GetActivePrefabTransform(rootTransform, rootScale);
				SetNodeTransform(session->Get(), nodeEntry, newNodeId, rootTransform, rootScale);
			}
		}
		else
		{
			result = false;
		}

		if (result)
		{
			MapReferences(session, nodeEntry, newNodeId);
		}
		else
		{
			session->m_msg += red::String::Printf("HAPI: Unable to cook: %hs", wrapper->GetNodeNamespace().AsChar());
		}

		return result;
	}
	
	Bool AddSOPNode( HoudiniEngineSession* session, ResourceEntryElement& nodeEntry, const CName& appearanceName )
	{
		Bool result = true;
		HAPI_NodeId newNodeId = -1;
		THandle<IHDAInterfaceWrapper> wrapper = session->GetInterfaceWrapper();

		if (!nodeEntry.m_resourcePath.Empty() && IsMeshResource(nodeEntry.m_resourcePath, nodeEntry.m_resourceType) )
		{
			// mesh marshalling
			res::IssueLoadingRequestParameter param;
			param.path = res::ResourcePath::Build(nodeEntry.m_resourcePath);
			param.skipPostLoad = true;

			THandle< CMesh > mesh = GResourceLoader->IssueLoadingRequest(param)->WaitUntilLoaded< CMesh >();
			if (mesh)
			{
				result &= TranslateMeshToHDA( session, mesh, nodeEntry, newNodeId, session->GetCookingOptions(), appearanceName );
			}
			else
				result = false;
		}
		else
		{
			result = false;
		}

		Transform rootTransform = Transform::IDENTITY();
		Vector3 rootScale = Vector3::ONES();
		wrapper->GetActivePrefabTransform(rootTransform, rootScale);
		SetNodeTransform(session->Get(), nodeEntry, newNodeId, rootTransform, rootScale);

		// new node created
		if (result)
		{
			MapReferences(session, nodeEntry, newNodeId);
		}
		else
		{
			session->m_msg += red::String::Printf("HAPI: Unable to cook: %hs", wrapper->GetNodeNamespace().AsChar());
		}
		return result;
	}

	Bool AddFoliageBrushNode(HoudiniEngineSession* session, ResourceEntryElement& nodeEntry)
	{
		Bool result = true;
		HAPI_NodeId newNodeId =-1;
		red::String msg;
		THandle<IHDAInterfaceWrapper> wrapper = session->GetInterfaceWrapper();
		//get brush properties as json
		res::ResourcePath brushPath = res::ResourcePath::Build(nodeEntry.m_resourcePath);
		res::IssueLoadingRequestParameter loadingParams(brushPath);
		loadingParams.postLoadFlags.skipUploadRenderData = true;

		THandle<CResource> resource = GResourceLoader->IssueLoadingRequest(loadingParams)->WaitUntilLoaded();
		serialization::json::JsonSerializer serializer(resource);
		
		String jsonString;
		serializer.Serialize(jsonString);

		//save json on disk
		Uint64 hash = GetUniqueHash(nodeEntry);
		red::String nodeName = red::String::Printf("%hs_%.0f", nodeEntry.m_attribName.AsChar(), (Double)hash);

		const red::AbsolutePath jsonFileOutput = GFileManager->GetCacheDirectory().AddFilePath(nodeName + ".json");

		CBasicTextBuilder textBuilder;
		textBuilder.Write(jsonString.AsChar(),jsonString.Length());
		result &= textBuilder.SaveToFile(jsonFileOutput);


		if (CreateNode(wrapper, session->Get(), -1, "sop/null", nodeName, true, newNodeId, true, msg))
		{
			HAPI_PartInfo newNodePart = HAPI_PartInfo_Create();
			newNodePart.type = HAPI_PARTTYPE_MESH;
			newNodePart.faceCount = 1;
			newNodePart.vertexCount = 0;
			newNodePart.pointCount = 0;

			HAPIx(HAPI_SetPartInfo(session->Get(), newNodeId, 0, &newNodePart));
			//we have to add one point because HAPI_CommitGeo dodin't works
			HAPI_AttributeInfo newNodePointInfo = HAPI_AttributeInfo_Create();
			newNodePointInfo.count = 2;
			newNodePointInfo.tupleSize = 1;
			newNodePointInfo.exists = true;
			newNodePointInfo.storage = HAPI_STORAGETYPE_FLOAT;
			newNodePointInfo.owner = HAPI_ATTROWNER_POINT;

			result &= HAPIx(HAPI_AddAttribute(session->Get(), newNodeId, 0, "P", &newNodePointInfo));
			Float positions[3] = { 0.0f, 0.0f, 0.0f };
			result &= HAPIx(HAPI_SetAttributeFloatData(session->Get(), newNodeId, 0, "P", &newNodePointInfo, positions, 0, 3));

			AddDetailAttribute(session->Get(), newNodeId, "resourceType", nodeEntry.m_resourceType.AsStringView().ToString());
			AddDetailAttribute(session->Get(), newNodeId, "jsonPath", jsonFileOutput.AsChar());
			result &= HAPIx(HAPI_CommitGeo(session->Get(), newNodeId));
			MapReferences(session, nodeEntry, newNodeId);
		}
		else
		{
			session->m_msg += red::String::Printf("HAPI: Unable to save json file in : %hs for foliage brush", jsonFileOutput.AsChar());
		}

		return result;
	}

	BACKEND_ENGINE_API void ScanHDALibrary( HoudiniEngineSession* session, red::DynArray< String >& outHDANames)
	{
		red::DynArray< String > files{ red::PoolBackend() };
		String ext = String::Printf("*.%hs", session->GetConfig().m_libraryExtension);
		res::GetEditorDepot()->FindDepotFiles(session->GetConfig().m_libraryDepotLocation, ext.AsChar(), files);

		for (auto f : files)
		{
			outHDANames.PushBack(red::paths::ExtractFileStem(f));
		}
	}

	BACKEND_ENGINE_API Bool IsSessionValid( HoudiniEngineSession* session )
	{
		if ( session )
		{
			return HAPIx( HAPI_IsSessionValid( session->Get() ) );
		}
		else
		{
			return false;
		}
	}

	BACKEND_ENGINE_API Bool GetEnvironmentConfig(HoudiniEngineSession* session, red::DynArray<red::AbsolutePath>& outConfigPaths)
	{
		EnvConfig config = session->GetConfig();

		if (!config.m_dsoFiles.Empty()
			&& !config.m_envFiles.Empty()
			&& !config.m_otlFiles.Empty())
		{
			outConfigPaths.PushBack(config.m_dsoFiles);
			outConfigPaths.PushBack(config.m_envFiles);
			outConfigPaths.PushBack(config.m_otlFiles);
			outConfigPaths.PushBack( session->GetDebugPath() );
			return true;
		}
		return false;
	}

	void MapParametersToInterface(HoudiniEngineSession* session, HAPI_NodeId nodeID, Uint32 paramCount, HAPI_ParmInfo* paramInfos)
	{
		red::String status = red::String();
		THandle<IHDAInterfaceWrapper> wrapper = session->GetInterfaceWrapper();
		
		const Float offset = 50.0f / ((Float)(paramCount));

		for (Uint32 i = 0; i < paramCount; ++i)
		{
			status = red::String::Printf("Mapping interface %u/%u", i+1, paramCount);
			wrapper->AddProgress(offset, status);

			const red::String paramNameStr = HAPIs(session->Get(), paramInfos[i].nameSH);
			const CName paramName = RED_NAME(paramNameStr.AsChar());
			const red::String paramLabel = HAPIs(session->Get(), paramInfos[i].labelSH);

			if (paramInfos[i].invisible)
				continue;

			red::String parmValue;
			HAPI_StringHandle value;

			// toggle
			if (paramInfos[i].type == HAPI_PARMTYPE_TOGGLE)
			{
				int defaultVal = 0;
				HAPI_GetParmIntValue(session->Get(), nodeID, paramName.AsChar(), 0, &defaultVal);
				wrapper->AddAttribute< Bool >(paramName, paramLabel, (Bool)defaultVal);
			}

			// combo boxes (operator choice)
			else if (paramInfos[i].choiceListType == HAPI_CHOICELISTTYPE_NORMAL)
			{
				if( paramInfos[i].choiceCount == 0)
					continue;

				HAPI_ParmChoiceInfo* choiceInfo = RED_NEW_ARRAY(HAPI_ParmChoiceInfo, paramInfos[i].choiceCount, red::PoolBackend);
				HAPIx(HAPI_GetParmChoiceLists(session->Get(), nodeID, choiceInfo, paramInfos[i].choiceIndex, paramInfos[i].choiceCount));

				red::DynArray< String > values;
				for (Int32 choice = 0; choice < paramInfos[i].choiceCount; ++choice)
				{
					values.PushBack(HAPIs(session->Get(), choiceInfo[choice].labelSH));
				}

				RED_DELETE_ARRAY(choiceInfo, paramInfos[i].choiceCount);
				if (!values.Empty())
				{
					CName customTypeName = RED_NAME_CONSTEXPR("heChoice");
					if (!wrapper->m_context->m_map.KeyExist(paramName))
					{
						int defaultVal = 0;
						HAPI_GetParmIntValue(session->Get(), nodeID, paramName.AsChar(), 0, &defaultVal);
						wrapper->m_context->m_map.Insert(paramName, values);
						wrapper->AddAttribute< CName >(paramName, paramLabel, RED_NAME(values[defaultVal].AsChar()), false, customTypeName);
					}
					else
					{
						wrapper->m_context->m_map[paramName] = values;
					}
				}
			}
			// numerical float values
			else if (HAPI_ParmInfo_IsFloat(&paramInfos[i]))
			{
				Int32 parmCount = HAPI_ParmInfo_GetFloatValueCount(&paramInfos[i]);
				Float* parmValues = RED_NEW_ARRAY(Float, parmCount, red::PoolBackend);
				HAPIx(HAPI_GetParmFloatValues(session->Get(), nodeID, parmValues, paramInfos[i].floatValuesIndex, parmCount));

				// vector3
				if (paramInfos[i].size == 3)
				{
					// engine property page works with vec4 only :(
					const Vector4 value = Vector4(parmValues[0], parmValues[1], parmValues[2], 0.0f);
					wrapper->AddAttribute<Vector4>(paramName, paramLabel, value);
				}
				// float
				else if (paramInfos[i].size == 1)
				{
					wrapper->AddAttribute<Float>(paramName, paramLabel, parmValues[0], paramInfos[i].min, paramInfos[i].max);
				}
				RED_DELETE_ARRAY(parmValues, parmCount);
			}
			// integer values
			else if (HAPI_ParmInfo_IsInt(&paramInfos[i]))
			{
				Int32 parmCount = HAPI_ParmInfo_GetIntValueCount(&paramInfos[i]);
				Int32* parmValues = RED_NEW_ARRAY(Int32, parmCount, red::PoolBackend);

				HAPIx(HAPI_GetParmIntValues(session->Get(), nodeID, parmValues, paramInfos[i].intValuesIndex, parmCount));
				wrapper->AddAttribute<Int32>(paramName, paramLabel, parmValues[0], (Int32)paramInfos[i].min, (Int32)paramInfos[i].max);
				RED_DELETE_ARRAY(parmValues, parmCount);
			}
			// string (does not include string arrays)
			else if (HAPI_ParmInfo_IsString(&paramInfos[i]))
			{
				HAPIx(HAPI_GetParmStringValue(session->Get(), nodeID, HAPIs(session->Get(), paramInfos[i].nameSH).AsChar(), 0, true, &value));
				parmValue = HAPIs(session->Get(), value);

				res::ResourcePath resourcePath = res::ResourcePath::Build(parmValue);
				TResRef< CResource > resourceRef;
				if (resourcePath.IsValid())
					resourceRef.SetPath(resourcePath);

				if (paramInfos[i].type == HAPI_PARMTYPE_PATH_FILE)
				{
					wrapper->AddAttribute<TResRef< CResource >>(paramName, paramLabel, resourceRef);
				}
				else if (paramInfos[i].type == HAPI_PARMTYPE_NODE)
				{
					// asset browser resource files
					if (paramInfos[i].inputNodeType == HAPI_NODETYPE_SOP)
					{
						wrapper->AddAttribute<TResRef< CResource >>(paramName, paramLabel, resourceRef);
					}
					else
					{
						// world node references, required for cook!
						world::NodeRef nodeRef;
						wrapper->AddAttribute< world::NodeRef >(paramName, paramLabel, nodeRef, false);//temp disable required
					}
				}
				else
				{
					HAPIx(HAPI_GetParmStringValue(session->Get(), nodeID, HAPIs(session->Get(), paramInfos[i].nameSH).AsChar(), 0, true, &value));
					wrapper->AddAttribute<red::String>(paramName, paramLabel, parmValue);
				}
			}
			else
			{
				RED_REPORT_ERROR(RED_LOCATION(wrapper->GetPath()), "Houdini Engine: Skipping unrecognized parameter: %d", Int32(paramInfos[i].type));
			}
		}
	}

	Bool MapResource(const red::String& attribValue, const red::String& attribName, red::DynArray<ResourceEntryElement>& nodeEntries)
	{
		CName resourceTypeName = CName();
		if (IsMeshResource(attribValue, resourceTypeName))
		{
			ResourceEntryElement nodeEntry;
			nodeEntry.m_resourcePath = attribValue;
			nodeEntry.m_attribName = attribName;
			nodeEntry.m_attribValue = attribValue;
			nodeEntry.m_resourceType = resourceTypeName;
			nodeEntries.PushBack(nodeEntry);
			return true;
		}
		// assuming json == asset list
		else if (IsJsonResource(attribValue))
		{
			Uint32 resourcedFound = 0;
			res::ResourcePath resPath = res::ResourcePath::Build(attribValue);
			res::ResourceTokenHandle loadingToken = GResourceLoader->IssueLoadingRequest(resPath);
			loadingToken->WaitUntilLoaded();

			THandle< JsonResource > jsonResource = Cast< JsonResource >(loadingToken->GetResource());
			if (!jsonResource)
			{
				RED_LOG_ERROR("Houdini Engine: Unable to read file: %hs", attribValue.AsChar());
			}
			else if (THandle< ResourceListArray > resList = jsonResource->GetRoot< ResourceListArray >())
			{
				for (auto res : resList->m_entries)
				{
					if (IsMeshResource(res.m_resourcePath, res.m_resourceType))
					{
						res.m_attribName = attribName;
						res.m_attribValue = attribValue;
						nodeEntries.PushBack(res);
						++resourcedFound;
					}
				}
			}
			return (resourcedFound > 0);
		}
		else if ( IsFoliageBrushResource(attribValue, resourceTypeName))
		{
			ResourceEntryElement nodeEntry;
			nodeEntry.m_resourcePath = attribValue;
			nodeEntry.m_attribName = attribName;
			nodeEntry.m_attribValue = attribValue;
			nodeEntry.m_resourceType = resourceTypeName;
			nodeEntries.PushBack(nodeEntry);
		}
		return false;
	}

	Bool ValidateInterfaceParameters(HoudiniEngineSession* session)
	{
		HAPI_NodeInfo nodeInfo;
		THandle<IHDAInterfaceWrapper> wrapper = session->GetInterfaceWrapper();
		Bool result = HAPIx(HAPI_GetNodeInfo(session->Get(), wrapper->GetNodeID(), &nodeInfo));
		Uint32 propCount = wrapper->GetPropertiesCount();

		if (result && propCount > 0 && nodeInfo.parmCount > 0)
		{
			HAPI_ParmInfo* paramInfos = RED_NEW_ARRAY(HAPI_ParmInfo, nodeInfo.parmCount, red::PoolBackend);
			result &= HAPIx(HAPI_GetParameters(session->Get(), wrapper->GetNodeID(), paramInfos, 0, nodeInfo.parmCount));

			Bool required = false;
			for (Int32 i = 0; i < nodeInfo.parmCount; ++i)
			{
				if (!paramInfos[i].invisible)
				{
					const red::String attribName = HAPIs(session->Get(), paramInfos[i].nameSH);
					const red::String attribValue = wrapper->GetPropertyValue(RED_NAME(attribName.AsChar()), required);

					if (attribValue.Empty() && required)
					{
						session->m_msg = red::String::Printf("Missing required attribute: %hs", attribName.AsChar());
						result = false;
						break;
					}
				}
			}
			RED_DELETE_ARRAY(paramInfos, nodeInfo.parmCount);
		}
		return result;
	}

	Bool MapParametersFromInterface( HAPI_Session* session, THandle< IHDAInterfaceWrapper >& wrapper, Uint32 paramCount, 
		HAPI_ParmInfo* paramInfos, red::DynArray< houdini_legacy::ResourceEntryElement >& newNodes, world::INodeInstanceRegistry* registry)
	{
		Bool result = true;
		const HAPI_NodeId nodeID = wrapper->GetNodeID();

		for (Uint32 i = 0; i < paramCount; ++i)
		{
			if (paramInfos[i].invisible)
				continue;

			Bool required = false;
			const red::String attribName = HAPIs(session, paramInfos[i].nameSH);
			const red::String attribValue = wrapper->GetPropertyValue(RED_NAME(attribName.AsChar()), required);

			if (attribValue.Empty() && required)
			{
				if (!required)
				{
					RED_REPORT_ERROR(RED_LOCATION(wrapper->GetPath()), "Houdini Engine: property %, is empty! Skipping parameter!", attribName.AsChar());
					continue;
				}
				else
				{
					RED_REPORT_ERROR(RED_LOCATION(wrapper->GetPath()), "Houdini Engine: property %, is required! Cooking cancelled!", attribName.AsChar());
					return false;
				}
			}

			if (paramInfos[i].type == HAPI_PARMTYPE_TOGGLE)
			{
				result &= HAPIx(HAPI_SetParmIntValue(session, nodeID, attribName.AsChar(), 0, FromStringDirect<Bool>(attribValue)));
			}

			else if (HAPI_ParmInfo_IsFloat(&paramInfos[i]))
			{
				// float3
				if (paramInfos[i].size >= 3)
				{
					Vector4 newValue;
					const AnsiChar* stream = attribValue.AsChar();
					if (GParseVector4(stream, newValue))
					{
						HAPI_SetParmFloatValue(session, nodeID, attribName.AsChar(), 0, newValue.X);
						HAPI_SetParmFloatValue(session, nodeID, attribName.AsChar(), 1, newValue.Y);
						HAPI_SetParmFloatValue(session, nodeID, attribName.AsChar(), 2, newValue.Z);

						// float4
						if (paramInfos[i].size == 4)
						{
							HAPI_SetParmFloatValue(session, nodeID, attribName.AsChar(), 3, newValue.W);
						}
					}
				} // float
				else if (paramInfos[i].size == 1)
				{
					Float newValue = FromStringDirect<Float>(attribValue);
					result &= HAPIx(HAPI_SetParmFloatValue(session, nodeID, attribName.AsChar(), 0, newValue));
				}
			}
			else if (HAPI_ParmInfo_IsInt(&paramInfos[i]))
			{
				if (paramInfos[i].size == 1)
				{
					Int32 newValue = 0;

					if (paramInfos[i].choiceCount > 0)
					{
						// toggle handling
						if (attribValue == "on")
						{
							newValue = 1;
						}
						else if (attribValue == "off")
						{
							newValue = 0;
						}
						else //if (RED_NAME_CONSTEXPR(attribName.AsChar()) == outputTypeName)
						{
							HAPI_ParmChoiceInfo* choiceInfo = RED_NEW_ARRAY(HAPI_ParmChoiceInfo, paramInfos[i].choiceCount, red::PoolBackend);
							HAPIx(HAPI_GetParmChoiceLists(session, nodeID, choiceInfo, paramInfos[i].choiceIndex, paramInfos[i].choiceCount));

							for (Int32 choice = 0; choice < paramInfos[i].choiceCount; ++choice)
							{
								if (strcmp(HAPIs(session, choiceInfo[choice].labelSH).AsChar(), attribValue.AsChar()) == 0)
								{
									newValue = choice;
									break;
								}
							}
							RED_DELETE_ARRAY(choiceInfo, paramInfos[i].choiceCount);
						}	
					}
					else
					{
						newValue = FromStringDirect<Int32>(attribValue);
					}

					result &= HAPIx(HAPI_SetParmIntValue(session, nodeID, attribName.AsChar(), 0, newValue));
				}
			}
			else if (HAPI_ParmInfo_IsString(&paramInfos[i]))
			{
				if (paramInfos[i].type == HAPI_PARMTYPE_NODE)
				{
					if (paramInfos[i].inputNodeType == HAPI_NODETYPE_SOP)
					{
						result &= MapResource(attribValue, attribName, newNodes);
					}
					else
					{
						world::NodeRef nodeRef = world::NodeRef::Parse(attribValue.AsChar());
						const world::GlobalNodeRef globalNodeRef = nodeRef.Resolve(world::GlobalNodeID::GetRoot());

						// we treat first input object transform
						// as origin (0.0.0 of a local space of output object )
						world::NodeInstancePtr nodeInstance = globalNodeRef.GetNodeInstance(registry);

						if (nodeInstance)
						{
							ResourceEntryElement nodeEntry;
							nodeEntry.m_resourceType = nodeInstance->GetClass()->GetName();
							nodeEntry.m_resourcePath = nodeInstance->GetPath().ToString();
							nodeEntry.m_transform = nodeInstance->GetInitialTransform();
							nodeEntry.m_scale = nodeInstance->GetInitialScale();
							nodeEntry.m_attribName = attribName;
							nodeEntry.m_attribValue = attribValue;
							newNodes.PushBack(nodeEntry);
						}
						else
						{
							RED_LOG_ERROR("Houdini Engine: Unable to load instance: %hs", attribName.AsChar());
							//set empty string to gui if resource doesn't exist:
							if (attribValue.Empty()) {
								HAPIx(HAPI_SetParmStringValue(session, nodeID, "", paramInfos[i].id, 0));
							}
							result = false;
							continue;
						}
					}
				}
				else if (paramInfos[i].type == HAPI_PARMTYPE_PATH_FILE)
				{
					result &= MapResource(attribValue, attribName, newNodes);
				}
				else
				{
					result &= HAPIx(HAPI_SetParmStringValue(session, nodeID, attribValue.AsChar(), paramInfos[i].id, 0));
				}
			}
		}
		return result;
	}

	BACKEND_ENGINE_API Bool LoadHoudiniDigitalAsset( HoudiniEngineSession* session, const String& hdaName, Int32& outAssetCount, red::String& msg, Bool forceReload)
	{
		if (session->m_loadedHdaName != hdaName)
		{
			forceReload = true;
		}

		if (session->GetInterfaceWrapper()->GetNodeID() != -1)
		{
			if (forceReload)
			{
				session->CleanUp();
			}
			else
			{
				return true;
			}
		}

		// load HDA, currently supporting 1 asset = 1 OBJ network
		HAPI_AssetLibraryId assetLibId;
		
		red::AbsolutePath hdaAbsolutePath = session->GetConfig().m_otlFiles;
		RED_FATAL_ASSERT( hdaAbsolutePath.IsDirectoryPath() );
		red::String filename = red::String::Printf("%s.%s", hdaName.AsChar(), session->GetConfig().m_libraryExtension);
		hdaAbsolutePath.AppendFilePath( filename );
		const res::ResourcePath hdaPath = res::ResourcePath::Build(session->GetConfig().m_libraryDepotLocation + "\\" + filename);
		THandle<IHDAInterfaceWrapper> wrapper = session->GetInterfaceWrapper();

		if (!houdini_legacy::HAPIx(HAPI_LoadAssetLibraryFromFile(session->Get(), hdaAbsolutePath.ToDebugString(), true, &assetLibId), msg))
		{
			if( msg.Empty() )
				msg = "HAPIx: LoadAssetLibrary empty response!\n";

			msg += red::String::Printf("HAPIx: LoadAssetLibrary failed at: %hs", hdaAbsolutePath.ToDebugString());
			return false;
		}

		if( !houdini_legacy::HAPIx(HAPI_GetAvailableAssetCount(session->Get(), assetLibId, &outAssetCount), msg))
		{	
			if (msg.Empty())
			{
				msg = "HAPIx: GetAvailableAssetCount empty response!";
				wrapper->SetStatus(0.0f, msg);
			}	

			msg += red::String::Printf("HAPI: Unable to get avaialable asset count: %hs", hdaAbsolutePath.ToDebugString());
			wrapper->SetStatus(0.0f, msg);
			return false;
		}

		red::String status = "Loading asset";
		wrapper->AddProgress(1.0f, status);

		HAPI_StringHandle assetSh;
		if (!houdini_legacy::HAPIx(HAPI_GetAvailableAssets(session->Get(), assetLibId, &assetSh, outAssetCount), msg))
		{	
			if (msg.Empty())
				msg = "HAPIx: GetAvailableAssets empty response!\n";

			msg += red::String::Printf("HAPI: Unable get available asset from: %hs", hdaAbsolutePath.ToDebugString());

			wrapper->SetStatus(0.0f, msg);
			return false;
		}

		status = "Loading interface";
		wrapper->AddProgress(1.0f, status);
		wrapper->SetNodeNamespace( houdini_legacy::HAPIs(session->Get(), assetSh) );

		// fill the interface
		HAPI_NodeId nodeID;
		if (!houdini_legacy::HAPIx(HAPI_CreateNode(session->Get(), -1, wrapper->GetNodeNamespace().AsChar(), "templateNode", false, &nodeID), msg))
		{
			if (msg.Empty())
				msg = "HAPIx: CreateNode empty response!\n";

			msg += red::String::Printf("HAPI: Unable to create node: %hs", wrapper->GetNodeNamespace().AsChar());
			
			wrapper->SetStatus(0.0f, msg);
			return false;
		}
		wrapper->SetNodeID(session->Get(), nodeID);
		
		HAPI_NodeInfo nodeInfo;
		HAPIx( HAPI_GetNodeInfo(session->Get(), nodeID, &nodeInfo) );

		HAPI_ParmInfo* paramInfos = RED_NEW_ARRAY( HAPI_ParmInfo, nodeInfo.parmCount, red::PoolBackend );
		HAPI_GetParameters(session->Get(), nodeID, paramInfos, 0, nodeInfo.parmCount);
		houdini_legacy::MapParametersToInterface( session, nodeID, nodeInfo.parmCount, paramInfos );
		
		msg += red::String::Printf("HAPI: Found %d asset(s).", outAssetCount);
		RED_DELETE_ARRAY( paramInfos, nodeInfo.parmCount );

		wrapper->SetStatus(100.0f, msg);

		session->m_loadedHdaName = hdaName;
		return true;
	}
	
	BACKEND_ENGINE_API Bool MapExportNodes( HoudiniEngineSession* session, THandle< IHDAInterfaceWrapper >& wrapper, 
		red::DynArray< ResourceEntryElement >& newNodes, world::INodeInstanceRegistry* registry)
	{
		// apply user properties
		HAPI_NodeInfo nodeInfo;
		Bool result = HAPIx(HAPI_GetNodeInfo(session->Get(), wrapper->GetNodeID(), &nodeInfo));

		// nodes without user exposed params?
		if (result && nodeInfo.parmCount > 0)
		{
			HAPI_ParmInfo* paramInfos = RED_NEW_ARRAY(HAPI_ParmInfo, nodeInfo.parmCount, red::PoolBackend);
			result &= HAPIx(HAPI_GetParameters(session->Get(), wrapper->GetNodeID(), paramInfos, 0, nodeInfo.parmCount));

			// update parameters
			result &= MapParametersFromInterface(session->Get(), wrapper, nodeInfo.parmCount, paramInfos, newNodes, registry);

			RED_DELETE_ARRAY(paramInfos, nodeInfo.parmCount);
		}
		return result;
	}

	BACKEND_ENGINE_API Bool GetHeighfieldData(HoudiniEngineSession* session, Float* newData, Int32 newDataSize)
	{
		const HAPI_NodeId nodeId = session->GetInterfaceWrapper()->GetNodeID();
		//find partId with height data:
		HAPI_PartId	heightDataPartId = -1;
		HAPI_GeoInfo geoInfo;
		Bool result = HAPIx(HAPI_GetDisplayGeoInfo(session->Get(), nodeId, &geoInfo));

		if (result) {
			for (int partIndex = 0; partIndex < geoInfo.partCount; ++partIndex)
			{
				HAPI_PartInfo partInfo;
				result &= HAPIx(HAPI_GetPartInfo(session->Get(), nodeId, partIndex, &partInfo));

				if (result && partInfo.type == HAPI_PARTTYPE_VOLUME)
				{
					HAPI_VolumeInfo volumeInfo = HAPI_VolumeInfo_Create();
					Bool result = HAPIx(HAPI_GetVolumeInfo(session->Get(), nodeId, partIndex, &volumeInfo));
					red::String attribName = HAPIs(session->Get(), volumeInfo.nameSH).AsChar();

					if (attribName == "height")
					{
						heightDataPartId = partIndex;
						break;
					}
				}
			}
		}
		else
		{
			session->m_msg = red::String::Printf("height data doesn't exists. nodeID: %d", nodeId);
			return false;
		}

		HAPI_VolumeInfo volumeInfo = HAPI_VolumeInfo_Create();
		result &= HAPIx(HAPI_GetVolumeInfo(session->Get(), nodeId, heightDataPartId, &volumeInfo));
		
		if (result && heightDataPartId != -1)
		{
			HAPI_PartInfo partInfo;
			HAPI_GetPartInfo(session->Get(), nodeId, 0, &partInfo);

			if (volumeInfo.xLength*volumeInfo.yLength == newDataSize)
			{
				result &= HAPIx(HAPI_GetHeightFieldData(session->Get(), nodeId, heightDataPartId, newData, 0, newDataSize));
			}
			else
			{
				session->m_msg = red::String::Printf("Heightmap output: %d differs from the expected size of: %d", volumeInfo.xLength*volumeInfo.yLength, newDataSize);
				return false;
			}
		}
		else
			session->m_msg = red::String::Printf("Heightmap output does not contain volume data for nodeID: %d", nodeId);

		return result;
	}

	BACKEND_ENGINE_API Bool GetMlMaskData(HoudiniEngineSession* session, Uint8* newData, Int32 dataSize, String layerName)
	{
		const HAPI_NodeId nodeId = session->GetInterfaceWrapper()->GetNodeID();
		HAPI_GeoInfo geoInfo;
		bool result = HAPIx(HAPI_GetDisplayGeoInfo(session->Get(), nodeId, &geoInfo));

		if (result) {
			for (int partIndex = 0; partIndex < geoInfo.partCount; ++partIndex)
			{
				HAPI_PartInfo partInfo;
				HAPI_GetPartInfo(session->Get(), nodeId, partIndex, &partInfo);
				if (partInfo.type == HAPI_PARTTYPE_VOLUME)
				{
					HAPI_VolumeInfo volumeInfo = HAPI_VolumeInfo_Create();
					Bool result = HAPIx(HAPI_GetVolumeInfo(session->Get(), nodeId, partIndex, &volumeInfo));
					red::String attribName = HAPIs(session->Get(), volumeInfo.nameSH).AsChar();

					if (attribName == layerName)
					{
						if (volumeInfo.xLength*volumeInfo.yLength == dataSize)
						{
							Float* mask = RED_NEW_ARRAY(Float, dataSize, red::PoolBackend);
							result &= HAPIx(HAPI_GetHeightFieldData(session->Get(), nodeId, partIndex, mask, 0, dataSize));

							if (result)
							{
								for (Int32 i = 0; i < dataSize; i++)
								{
									newData[i] = static_cast<Uint8> (Clamp(mask[i] * 255.0f, 0.0f, 255.0f));
								}
							}
							RED_DELETE_ARRAY(mask, dataSize);
							return true;
						}
						else
						{
							session->m_msg = red::String::Printf("Heightmap output: %d differs from the expected size of: %d", volumeInfo.xLength*volumeInfo.yLength, dataSize);
							return false;
						}

					}
				}
			}
		}
		else
		{
			session->m_msg = red::String::Printf("Terrain geometry doesn't exists %d", nodeId);
			return false;
		}
		return result;
	}

	BACKEND_ENGINE_API Bool GetMlMaskCount(HoudiniEngineSession* session,  red::DynArray<Uint32> &mlMasksCount)
	{
		const HAPI_NodeId nodeId = session->GetInterfaceWrapper()->GetNodeID();
		HAPI_GeoInfo geoInfo;		
		bool result = HAPIx(HAPI_GetDisplayGeoInfo(session->Get(), nodeId, &geoInfo));

		if (result) {
			for (int partIndex = 0; partIndex < geoInfo.partCount; ++partIndex)
			{
				HAPI_PartInfo partInfo;
				result &= HAPIx(HAPI_GetPartInfo(session->Get(), nodeId, partIndex, &partInfo));
				if (result)
				{
					if (partInfo.type == HAPI_PARTTYPE_VOLUME)
					{
						HAPI_VolumeInfo volumeInfo = HAPI_VolumeInfo_Create();
						Bool result = HAPIx(HAPI_GetVolumeInfo(session->Get(), nodeId, partIndex, &volumeInfo));
						red::String attribName = HAPIs(session->Get(), volumeInfo.nameSH).AsChar();
						if (attribName.Contains("mlMask_"))
						{
							String maskNr = attribName.StringAfter("mlMask_");
							if (maskNr == "-1") continue;
							mlMasksCount.PushBack(FromStringDirect<Int32>(maskNr));
						}
					}
				}
			}
		}
		else
		{
			session->m_msg = red::String::Printf("Terrain geometry doesn't exists %d", nodeId);
			return false;
		}
		return (mlMasksCount.Size() ==0) ? false : true;
	}

	Bool CookHoudiniDigitalAsset(HoudiniEngineSession* session, Bool async)
	{
		const HAPI_NodeId nodeId = session->GetInterfaceWrapper()->GetNodeID();
		Bool result = nodeId != -1;
		if (result)
		{
			// force re-cook
			Bool result = HAPIx(HAPI_ResetSimulation(session->Get(), nodeId), session->m_msg);
			HAPI_SetCacheProperty(session->Get(), HAPI_CACHE_SOP, HAPI_CACHEPROP_CURRENT, 0);
			HAPI_SetCacheProperty(session->Get(), HAPI_CACHE_IMAGE, HAPI_CACHEPROP_CURRENT, 0);

			// debug - save scene for analysis
			if (session->m_saveDebugScene)
			{
				const red::String debugOutput = session->GetDebugPath().AsString();
				HAPIx(HAPI_SaveHIPFile(session->Get(), debugOutput.AsChar(), false));
			}

			// start cook
			result &= HAPIx(HAPI_CookNode(session->Get(), nodeId, session->GetCookingOptions()), session->m_msg);

			if (async)
			{
				job::Builder builder{ job::Priority::Immediate };
				builder.DispatchJob("HoudiniEngine/Cooking", [session](const job::RunContext&)
				{
					// monitor & callback for output data processing
					Bool cookingResult = HAPIc(session);
					session->GetInterfaceWrapper()->AddProgress(10.0);
					session->GetInterfaceWrapper()->SetCookingStatus(cookingResult);
				});
			}
			else
			{
				result = HAPIc(session);
			}
		}
		else
		{
			red::String status = "Unable to cook, session expired, please re-load the asset";
			session->GetInterfaceWrapper()->SetStatus(100.0f, status);
		}
		
		return result;
	}

	Bool GetGeometryBufferArray(HoudiniEngineSession* session, red::DynArray < GeometryBuffer> &outGeometryArray)
	{
		Bool result = true;

		HAPI_AssetInfo assetInfo;
		THandle<IHDAInterfaceWrapper> interfaceWrapper = session->GetInterfaceWrapper();
		const HAPI_NodeId nodeID = interfaceWrapper->GetNodeID();
		HAPIx(HAPI_GetAssetInfo(session->Get(), nodeID, &assetInfo));

		HAPI_ObjectInfo* objectInfos;
		Int32 objectCount = 0;
		ValidateNode(session->Get(), nodeID, objectInfos, objectCount);

		if (objectCount > 0 && objectInfos != nullptr)
		{
			// transfer geometry data to cmesh
			for (Int32 objectIndex = 0; objectIndex < objectCount; ++objectIndex)
			{
				HAPI_ObjectInfo &objectInfo = objectInfos[objectIndex];
				HAPI_GeoInfo geoInfo;
				HAPIx(HAPI_GetDisplayGeoInfo(session->Get(), objectInfo.nodeId, &geoInfo));

				for (int partIndex = 0; partIndex < geoInfo.partCount; ++partIndex)
				{
					GeometryBuffer outGeometry;
					result &= TranslateHDAToMesh(session, assetInfo, nodeID, geoInfo.nodeId, partIndex, outGeometry);

					//try get parentPath 
					AttributeData nodeRefData;
					if (GetAttributeData<String>(session->Get(), nodeID, partIndex, HAPI_ATTROWNER_PRIM, "nodeRef", nodeRefData))
					{
						HAPI_StringHandle* stringHandle =  (HAPI_StringHandle*)(nodeRefData.m_attribData) ;
						outGeometry.m_nodeRef = HAPIs(session->Get(), stringHandle[0]);
					}
					//try get material
					AttributeData materialPathData;
					if (GetAttributeData<String>(session->Get(), nodeID, partIndex, HAPI_ATTROWNER_PRIM, "materialPath", materialPathData))
					{
						HAPI_StringHandle* stringHandle = (HAPI_StringHandle*)(materialPathData.m_attribData);
						outGeometry.m_materialPath = HAPIs(session->Get(), stringHandle[0]);
					}
					//try get output_resourcepath
					AttributeData output_resourcepathData;
					if (GetAttributeData<String>(session->Get(), nodeID, partIndex, HAPI_ATTROWNER_PRIM, "output_resourcepath", output_resourcepathData))
					{
						HAPI_StringHandle* stringHandle = (HAPI_StringHandle*)(output_resourcepathData.m_attribData);
						outGeometry.m_outputResourcePath = HAPIs(session->Get(), stringHandle[0]);
					}
					//try get output_resourceName
					AttributeData output_resourceName;
					if (GetAttributeData<String>(session->Get(), nodeID, partIndex, HAPI_ATTROWNER_PRIM, "output_name", output_resourceName))
					{
						HAPI_StringHandle* stringHandle = (HAPI_StringHandle*)(output_resourceName.m_attribData);
						outGeometry.m_outputName = HAPIs(session->Get(), stringHandle[0]);
					}
					// try to acquire deformation data
					{
						AttributeData attributeDeformationData;
						if (GetAttributeData<Matrix>(session->Get(), nodeID, partIndex, HAPI_ATTROWNER_POINT, "deformationData", attributeDeformationData))
						{
							// no 'real' geometry, just the deformation matrices
							result = true;
							for (Uint32 i = 0; i < attributeDeformationData.m_atrribCount; ++i)
							{
								outGeometry.m_deformationData.PushBack(((Matrix*)attributeDeformationData.m_attribData)[i]);
							}

						}
					}

					{
						AttributeData outDestructionHash;
						if( GetAttributeData<String>( session->Get(), nodeID, partIndex, HAPI_ATTROWNER_PRIM, "destructionOwnerHashStart", outDestructionHash ) )
						{
							HAPI_StringHandle* rawDestructionHash = (HAPI_StringHandle*)( outDestructionHash.m_attribData );
							outGeometry.m_destructionOwnerHashStart = rawDestructionHash ? HAPIs( session->Get(), *rawDestructionHash ) : red::String::EMPTY();
						}
					}
					{
						AttributeData outDestructionHash;
						if( GetAttributeData<String>( session->Get(), nodeID, partIndex, HAPI_ATTROWNER_PRIM, "destructionOwnerHashEnd", outDestructionHash ) )
						{
							HAPI_StringHandle* rawDestructionHash = (HAPI_StringHandle*)( outDestructionHash.m_attribData );
							outGeometry.m_destructionOwnerHashEnd = rawDestructionHash ? HAPIs( session->Get(), *rawDestructionHash ) : red::String::EMPTY();
						}
					}

					//try get bendedMeshFile 
					AttributeData output_bendedMeshFile;
					if (GetAttributeData<String>(session->Get(), nodeID, partIndex, HAPI_ATTROWNER_PRIM, "bended_mesh_file_path", output_bendedMeshFile))
					{
						HAPI_StringHandle* stringHandle = (HAPI_StringHandle*)(output_bendedMeshFile.m_attribData);
						outGeometry.m_bendedMeshFile = HAPIs(session->Get(), stringHandle[0]);
					}


					{
						AttributeData attributeLengthData;
						if (GetAttributeData<Int32>(session->Get(), nodeID, partIndex, HAPI_ATTROWNER_PRIM, "length", attributeLengthData))
						{
							outGeometry.m_cableLength = ((Int32*)attributeLengthData.m_attribData)[0];
						}
					}

					{
						AttributeData attributeCable_radiushData;
						if (GetAttributeData<Int32>(session->Get(), nodeID, partIndex, HAPI_ATTROWNER_PRIM, "cable_radius", attributeCable_radiushData))
						{
							outGeometry.m_cableRadius = ((Int32*)attributeCable_radiushData.m_attribData)[0];
						}
					}


					Transform originTransform;
					Vector3 scale;
					interfaceWrapper->GetActivePrefabTransform(originTransform, scale);

					// OPTIONAL pivot shift
					// we prefer pivot of newly created mesh to be in its bbox center
					// so we transform vertex positions to origin, and then
					// offset the node by translating it to where it was 
					// generated rather than having vertices shifted there
					AttributeData transformData;
					if (GetAttributeData<Float>(session->Get(), nodeID, 0, HAPI_ATTROWNER_DETAIL, "transform_position", transformData)
						&& transformData.m_tupleSize == 3)
					{
						Float* rawData = (Float*)(transformData.m_attribData);

						Vector4 attribTransform = Vector4::ZEROS();
						attribTransform.X = rawData[0];
						attribTransform.Y = rawData[1];
						attribTransform.Z = rawData[2];

						originTransform.SetPosition(originTransform.GetPosition() + attribTransform);
						RED_DELETE_ARRAY(rawData, 3);
					}

					// apply transform back
					outGeometry.m_transform.SetTranslation(originTransform.GetPosition3());
					outGeometry.m_transform.SetRotation(originTransform.GetOrientation());
					outGeometry.m_transform.SetScale(scale);

					//pivot transform
					Transform pivotTransform;
					AttributeData transformPivotData;

					Bool hasPivot = false;

					if (GetAttributeData<Float>(session->Get(), nodeID, partIndex, HAPI_ATTROWNER_PRIM, "transform_pivot_position", transformPivotData)) //overwrite pivot if exist on prim
					{
						hasPivot = true;
					}


					if (!hasPivot && GetAttributeData<Float>(session->Get(), nodeID, 0, HAPI_ATTROWNER_DETAIL, "transform_pivot_position", transformPivotData))
					{
						hasPivot = true;
					}

					if (hasPivot && transformPivotData.m_tupleSize == 3)
					{
						Float* rawData = (Float*)(transformPivotData.m_attribData);

						Vector4 attribPivotTransform = Vector4::ZEROS();
						attribPivotTransform.X = rawData[0];
						attribPivotTransform.Y = rawData[1];
						attribPivotTransform.Z = rawData[2];

						pivotTransform.SetPosition(attribPivotTransform);
						RED_DELETE_ARRAY(rawData, 3);
					}
					outGeometry.m_pivotTransform.SetTranslation(pivotTransform.GetPosition3());

					outGeometryArray.PushBack(outGeometry);
				}
			}
			RED_DELETE_ARRAY(objectInfos, objectCount);
		}

		if (!result)
		{
			red::String status = "Unable to import, invalid geometry.";
			interfaceWrapper->SetStatus(0.0f, status);
		}

		return result;
	}

	Bool RequestCookedData(HoudiniEngineSession* session, GeometryBuffer& outGeometry)
	{
		Bool result = true;

		HAPI_AssetInfo assetInfo;
		THandle<IHDAInterfaceWrapper> interfaceWrapper = session->GetInterfaceWrapper();
		const HAPI_NodeId nodeID = interfaceWrapper->GetNodeID();
		HAPIx(HAPI_GetAssetInfo(session->Get(), nodeID, &assetInfo));

		HAPI_ObjectInfo* objectInfos;
		Int32 objectCount = 0;
		ValidateNode(session->Get(), nodeID, objectInfos, objectCount);

		if (objectCount > 0 && objectInfos != nullptr)
		{
			// transfer geometry data to cmesh
			for (Int32 objectIndex = 0; objectIndex < objectCount; ++objectIndex)
			{
				HAPI_ObjectInfo &objectInfo = objectInfos[objectIndex];
				HAPI_GeoInfo geoInfo;
				HAPIx(HAPI_GetDisplayGeoInfo(session->Get(), objectInfo.nodeId, &geoInfo));

				for (int partIndex = 0; partIndex < geoInfo.partCount; ++partIndex)
				{
					result &= TranslateHDAToMesh(session, assetInfo, nodeID, geoInfo.nodeId, partIndex, outGeometry);
					// try to acquire deformation data
					{
						AttributeData attributeDeformationData;
						if (GetAttributeData<Matrix>(session->Get(), nodeID, geoInfo.nodeId, HAPI_ATTROWNER_POINT, "deformationData", attributeDeformationData))
						{
							// no 'real' geometry, just the deformation matrices
							result = true;
							for (Uint32 i=0; i<attributeDeformationData.m_atrribCount; ++i)
							{
								outGeometry.m_deformationData.PushBack(((Matrix*)attributeDeformationData.m_attribData)[i]);
							}
						}
					}

					Transform originTransform;
					Vector3 scale;
					interfaceWrapper->GetActivePrefabTransform(originTransform, scale);
					
					// OPTIONAL pivot shift
					// we prefer pivot of newly created mesh to be in its bbox center
					// so we transform vertex positions to origin, and then
					// offset the node by translating it to where it was 
					// generated rather than having vertices shifted there
					AttributeData transformData;
					if (GetAttributeData<Float>(session->Get(), nodeID, 0, HAPI_ATTROWNER_DETAIL, "transform_position", transformData)
						&& transformData.m_tupleSize == 3)
					{
						Float* rawData = (Float*)(transformData.m_attribData);

						Vector4 attribTransform = Vector4::ZEROS();
						attribTransform.X = rawData[0];
						attribTransform.Y = rawData[1];
						attribTransform.Z = rawData[2];

						originTransform.SetPosition(originTransform.GetPosition() + attribTransform);
						RED_DELETE_ARRAY(rawData, 3);
					}

					// apply transform back
					outGeometry.m_transform.SetTranslation(originTransform.GetPosition3());
					outGeometry.m_transform.SetRotation(originTransform.GetOrientation());
					outGeometry.m_transform.SetScale(scale);

					//pivot transform
					Transform pivotTransform;
					AttributeData transformPivotData;
					if (GetAttributeData<Float>(session->Get(), nodeID, 0, HAPI_ATTROWNER_DETAIL, "transform_pivot_position", transformPivotData)
						&& transformPivotData.m_tupleSize == 3)
					{
						Float* rawData = (Float*)(transformPivotData.m_attribData);

						Vector4 attribPivotTransform = Vector4::ZEROS();
						attribPivotTransform.X = rawData[0];
						attribPivotTransform.Y = rawData[1];
						attribPivotTransform.Z = rawData[2];

						pivotTransform.SetPosition(attribPivotTransform);
						RED_DELETE_ARRAY(rawData, 3);
					}
					outGeometry.m_pivotTransform.SetTranslation(pivotTransform.GetPosition3());


				}
			}
			RED_DELETE_ARRAY(objectInfos, objectCount);
		}

		if (!result)
		{
			red::String status = "Unable to import, invalid geometry.";
			interfaceWrapper->SetStatus(0.0f, status);
		}

		return result;
	}

	Bool BakeAO( HoudiniEngineSession* session, const String& mesh_path, GeometryBuffer& outGeometry, Float maxDistance, Float spread, Float softness, Uint8 plocLods )
	{
		session->m_saveDebugScene = true;
		Bool result = true;
		Int32 assetCount = 0;
		THandle<IHDAInterfaceWrapper> wrapper = session->GetInterfaceWrapper();

		result &= LoadHoudiniDigitalAsset( session, "internal\\ambient_occlusion_mesh", assetCount );
		//result &= LoadHoudiniDigitalAsset( session, "internal\\terrain_convert", assetCount );
		result &= HAPIx( HAPI_SetParmFloatValue( session->Get(), wrapper->GetNodeID(), "max_distance", 0, maxDistance ) );
		result &= HAPIx( HAPI_SetParmFloatValue( session->Get(), wrapper->GetNodeID(), "spread", 0, spread ) );
		result &= HAPIx( HAPI_SetParmFloatValue( session->Get(), wrapper->GetNodeID(), "softness", 0, softness ) );
		result &= HAPIx( HAPI_SetParmIntValue( session->Get(), wrapper->GetNodeID(), "process_lods", 0, plocLods ) );
		
		ResourceEntryElement nodeEntry;
		nodeEntry.m_resourcePath = mesh_path;
		nodeEntry.m_resourceType = RED_NAME( ClassID<CMesh>()->GetDefaultObject<CResource>()->GetExtension() );
		nodeEntry.m_attribName = "input_mesh";

		const CName appearanceName = CName::NONE();
		result &= AddSOPNode( session, nodeEntry, appearanceName );
		result &= CookHoudiniDigitalAsset( session );
		result &= RequestCookedData( session, outGeometry );
		//result &= session->CleanUp();

		return result;
	}

	Bool CookLODs( HoudiniEngineSession* session, const String& meshPath, GeometryBuffer& outGeometry, const CName& appearanceName )
	{
		session->m_saveDebugScene = true;
		Bool result = true;
		Int32 assetCount = 0;

		ResourceEntryElement nodeEntry;
		nodeEntry.m_resourcePath = meshPath;
		nodeEntry.m_resourceType = RED_NAME(ClassID<CMesh>()->GetDefaultObject<CResource>()->GetExtension());
		nodeEntry.m_attribName = "input_mesh";

		result &= AddSOPNode( session, nodeEntry, appearanceName );

		red::DynArray< ResourceEntryElement > newNodes;
		MapExportNodes(session, session->GetInterfaceWrapper(), newNodes, nullptr);
		result &= CookHoudiniDigitalAsset(session, false);

		// debug
		//const red::String debugOutput = session->GetDebugPath().AsString();
		//HAPIx(HAPI_SaveHIPFile(session->Get(), debugOutput.AsChar(), false));

		result &= RequestCookedData(session, outGeometry);
		session->GetInterfaceWrapper()->UnloadDependencyNodes(session->Get());

		return result;

	}

	Bool RemoveFacingFaces( HoudiniEngineSession* session, THandle<CMesh> inMesh, GeometryBuffer& outGeometry, Uint8 ver /*= 0*/ )
	{
		static int update = 0;
		update++;
		Bool result = true;
		session->m_saveDebugScene = true;
		Int32 assetCount = 0;
		red::String msg;
		if (ver == 0)
		{
			result &= LoadHoudiniDigitalAsset( session, "internal\\remove_facing_polygons", assetCount, msg, true );
		}
		else
		{
			result &= LoadHoudiniDigitalAsset( session, "internal\\remove_facing_faces_prx", assetCount, msg, true );
		}

		HAPI_NodeId newNodeId = -1;
		ResourceEntryElement nodeEntry;
		nodeEntry.m_resourcePath = red::String::Printf("ThisIsAnUniqueStringSinceItIsTheOnlyOne%d",update);
		nodeEntry.m_resourceType = RED_NAME( ClassID<CMesh>()->GetDefaultObject<CResource>()->GetExtension() );
		nodeEntry.m_attribName = "input_mesh";

		THandle<IHDAInterfaceWrapper> wrapper = session->GetInterfaceWrapper();

		
		result &= TranslateMeshToHDA( session, inMesh, nodeEntry, newNodeId, session->GetCookingOptions(), RED_NAME_CONSTEXPR("default") );
		MapReferences( session, nodeEntry, newNodeId );
		result &= CookHoudiniDigitalAsset( session, false );
		result &= RequestCookedData( session, outGeometry );
		session->GetInterfaceWrapper()->UnloadDependencyNodes( session->Get() );
		return result;
	}

	Bool SimpleMergeCollisions( HoudiniEngineSession* session, red::ArraySpan<GeometryBuffer> inGeometry, Float percentage, const red::DynArray<float>& bones,GeometryBuffer& outGeometry,Bool roadVersion /* =false */, Int32 version /*=0 */ )
	{
		session->m_saveDebugScene = true;
		Bool result = true;
		HAPI_NodeId newNodeId = -1;
		red::String msg;
		red::String bonesString = "[";
		for (auto i : bones.Indices())
		{
			if (i != 0) bonesString.Append( ',' );
			bonesString += red::String::Printf( "%f", bones[i] );
		}
		bonesString.Append(']');

		Int32 assetCount = 0;
		THandle<IHDAInterfaceWrapper> wrapper = session->GetInterfaceWrapper();
		if (roadVersion)
		{
			result &= LoadHoudiniDigitalAsset( session, "internal\\merge_road_collisions", assetCount );
		}
		else
		{
			result &= LoadHoudiniDigitalAsset( session, "internal\\simpe_merge_collisions", assetCount );
			result &= HAPIx( HAPI_SetParmIntValue( session->Get(), wrapper->GetNodeID(), "version", 0, version ));
		}

		result &= HAPIx( HAPI_SetParmFloatValue( session->Get(), wrapper->GetNodeID(), "percentage", 0, percentage ) );
		result &= HAPIx( HAPI_SetParmStringValue( session->Get(), wrapper->GetNodeID(), bonesString.AsChar(), 1, 0 ) );

		ResourceEntryElement nodeEntry;
		nodeEntry.m_resourcePath = "";
		nodeEntry.m_resourceType = RED_NAME_CONSTEXPR( "mesh" );
		nodeEntry.m_attribName = "input_mesh";

		if (CreateNode( session->GetInterfaceWrapper(), session->Get(), -1, String(), "collGeo", true, newNodeId, false, msg ))
		{
			HAPI_PartInfo newNodePart = HAPI_PartInfo_Create();
			newNodePart.type = HAPI_PARTTYPE_MESH;
			newNodePart.faceCount = 0;
			newNodePart.vertexCount = 0;
			newNodePart.pointCount = 0;
			newNodePart.attributeCounts[HAPI_ATTROWNER_VERTEX] = 0;
			newNodePart.attributeCounts[HAPI_ATTROWNER_POINT] = 1;
			newNodePart.attributeCounts[HAPI_ATTROWNER_PRIM] = 1;
			newNodePart.attributeCounts[HAPI_ATTROWNER_DETAIL] = 0;

			for (Uint32 c = 0; c < inGeometry.Size(); ++c)
			{
				//validate chunk data
				const Uint32 numChunkVerts = inGeometry[c].m_verticesCollision.Size();
				const Uint32 numChunkIndices = inGeometry[c].m_indicesCollision.Size();

				newNodePart.faceCount += numChunkIndices / 3;
				newNodePart.vertexCount += numChunkIndices;
				newNodePart.pointCount += numChunkVerts;
			}

			HAPIx( HAPI_SetPartInfo( session->Get(), newNodeId, 0, &newNodePart ), msg );
			HAPI_AttributeInfo posNodeInfo = Create_HAPI_AttributeInfo( newNodePart, HAPI_ATTROWNER_POINT, 3, HAPI_STORAGETYPE_FLOAT, HAPI_ATTRIBUTE_TYPE_POINT );

			Bool result = HAPIx( HAPI_AddAttribute( session->Get(), newNodeId, 0, "P", &posNodeInfo ), msg );
			red::DynArray<Vector3> attribData;
			attribData.Reserve( posNodeInfo.count );

			for (const auto& geom : inGeometry)
			{
				attribData.PushBack( geom.m_verticesCollision );
			}

			result &= HAPIx( HAPI_SetAttributeFloatData( session->Get(), newNodeId, 0, "P", &posNodeInfo, static_cast<Float*>( attribData.Data() ), 0, posNodeInfo.count ), msg );

			HAPI_AttributeInfo newChunkIdPrimInfo = Create_HAPI_AttributeInfo( newNodePart, HAPI_ATTROWNER_PRIM, 1, HAPI_STORAGETYPE_INT );
			result &= HAPIx( HAPI_AddAttribute( session->Get(), newNodeId, 0, "chunk_id", &newChunkIdPrimInfo ), msg );

			HAPI_AttributeInfo newMaterialPrimInfo = Create_HAPI_AttributeInfo( newNodePart, HAPI_ATTROWNER_PRIM, 1, HAPI_STORAGETYPE_STRING );
			result &= HAPIx( HAPI_AddAttribute( session->Get(), newNodeId, 0, "material", &newMaterialPrimInfo ), msg );

			char ** materials = new char *[newMaterialPrimInfo.tupleSize * newMaterialPrimInfo.count];

			//red::DynArray<String> materials(newMaterialPrimInfo.count, red::PoolBackend());
			red::DynArray<Int32> chunks( newChunkIdPrimInfo.count, red::PoolBackend() );
			red::DynArray<Int32> vertices( newNodePart.vertexCount, red::PoolBackend() );

			Uint32 index = 0;
			Int32 accum_indicies = 0;
			Uint16 chunk_id = 0;

			red::DynArray<Uint32> indices;
			for (auto& geom : inGeometry)
			{
				for (Uint32 j = 0; j < geom.m_indicesCollision.Size(); ++j)
				{
					vertices[index] = geom.m_indicesCollision[j] + accum_indicies;
					chunks[index / 3] = chunk_id;
					materials[index / 3] = _strdup( geom.m_materialName.AsChar() );
					index++;
				}
				accum_indicies += geom.m_verticesCollision.Size();
				chunk_id++;
			}

			result &= HAPIx( HAPI_SetAttributeStringData( session->Get(), newNodeId, 0, "material", &newMaterialPrimInfo, (const char**)materials, 0, newNodePart.faceCount ), msg );
			result &= HAPIx( HAPI_SetAttributeIntData( session->Get(), newNodeId, 0, "chunk_id", &newChunkIdPrimInfo, chunks.TypedData(), 0, newNodePart.faceCount ), msg );
			result &= HAPIx( HAPI_SetVertexList( session->Get(), newNodeId, 0, vertices.TypedData(), 0, newNodePart.vertexCount ), msg );

			// @brief  Set the array of faces where the nth integer in the array is
			//         the number of vertices the nth face has.
			red::DynArray<Int32> face_counts;
			face_counts.Resize( newNodePart.faceCount, 3 );

			result &= HAPIx( HAPI_SetFaceCounts( session->Get(), newNodeId, 0, face_counts.TypedData(), 0, newNodePart.faceCount ), msg );
			result &= HAPIx( HAPI_CommitGeo( session->Get(), newNodeId ), msg );
			result = HAPIx( HAPI_CookNode( session->Get(), newNodeId, session->GetCookingOptions() ), msg );
		}

		if (result)
		{
			MapReferences( session, nodeEntry, newNodeId );
		}

		result &= CookHoudiniDigitalAsset( session, false );
		result &= RequestCookedData( session, outGeometry );
		result &= session->CleanUp();

		outGeometry.m_verticesCollision.Resize( outGeometry.m_vertices.Size() );
		outGeometry.m_indicesCollision.Resize( outGeometry.m_indices.Size() );

		std::transform( begin( outGeometry.m_vertices ), end( outGeometry.m_vertices ), begin( outGeometry.m_verticesCollision ), []( const DebugVertex& v ) -> Vector3 { return { v.x, v.y, v.z }; } );
		std::transform( begin( outGeometry.m_indices ), end( outGeometry.m_indices ), begin( outGeometry.m_indicesCollision ), []( Uint32 i ) -> Uint16 { return static_cast<Uint16>( i ); } );

		session->GetInterfaceWrapper()->UnloadDependencyNodes( session->Get() );
		if (outGeometry.m_indicesCollision.Size() < 3) { result = false; }

		return result;
	}

	Bool MergeCollisions(HoudiniEngineSession* session, red::ArraySpan<GeometryBuffer> inGeometry, Float percentage, GeometryBuffer& outGeometry)
	{
		session->m_saveDebugScene = true;
		Bool result = true;
		HAPI_NodeId newNodeId = -1;
		red::String msg;

		Int32 assetCount = 0;
		THandle<IHDAInterfaceWrapper> wrapper = session->GetInterfaceWrapper();
		result &= LoadHoudiniDigitalAsset(session, "internal\\merge_collisions", assetCount);

		result &= HAPIx(HAPI_SetParmFloatValue(session->Get(), wrapper->GetNodeID(), "percentage", 0, percentage));

		ResourceEntryElement nodeEntry;
		nodeEntry.m_resourcePath = "";
		nodeEntry.m_resourceType = RED_NAME_CONSTEXPR("mesh");
		nodeEntry.m_attribName = "input_mesh";

		if (CreateNode(session->GetInterfaceWrapper(), session->Get(), -1, String(), "collGeo", true, newNodeId, false, msg))
		{
			HAPI_PartInfo newNodePart = HAPI_PartInfo_Create();
			newNodePart.type = HAPI_PARTTYPE_MESH;
			newNodePart.faceCount = 0;
			newNodePart.vertexCount = 0;
			newNodePart.pointCount = 0;
			newNodePart.attributeCounts[HAPI_ATTROWNER_VERTEX] = 0;
			newNodePart.attributeCounts[HAPI_ATTROWNER_POINT] = 1;
			newNodePart.attributeCounts[HAPI_ATTROWNER_PRIM] = 1;
			newNodePart.attributeCounts[HAPI_ATTROWNER_DETAIL] = 0;

			for (Uint32 c = 0; c < inGeometry.Size(); ++c)
			{
				//validate chunk data
				const Uint32 numChunkVerts = inGeometry[c].m_verticesCollision.Size();
				const Uint32 numChunkIndices = inGeometry[c].m_indicesCollision.Size();

				newNodePart.faceCount += numChunkIndices / 3;
				newNodePart.vertexCount += numChunkIndices;
				newNodePart.pointCount += numChunkVerts;
			}

			HAPIx(HAPI_SetPartInfo(session->Get(), newNodeId, 0, &newNodePart), msg);
			HAPI_AttributeInfo posNodeInfo = Create_HAPI_AttributeInfo(newNodePart, HAPI_ATTROWNER_POINT, 3, HAPI_STORAGETYPE_FLOAT, HAPI_ATTRIBUTE_TYPE_POINT);

			Bool result = HAPIx(HAPI_AddAttribute(session->Get(), newNodeId, 0, "P", &posNodeInfo), msg);
			red::DynArray<Vector3> attribData;
			attribData.Reserve(posNodeInfo.count);

			for (const auto& geom : inGeometry)
			{
				attribData.PushBack(geom.m_verticesCollision);
			}

			result &= HAPIx(HAPI_SetAttributeFloatData(session->Get(), newNodeId, 0, "P", &posNodeInfo, static_cast<Float*>(attribData.Data()), 0, posNodeInfo.count), msg);
		
			HAPI_AttributeInfo newChunkIdPrimInfo = Create_HAPI_AttributeInfo(newNodePart, HAPI_ATTROWNER_PRIM, 1, HAPI_STORAGETYPE_INT);
			result &= HAPIx(HAPI_AddAttribute(session->Get(), newNodeId, 0, "chunk_id", &newChunkIdPrimInfo), msg);

			HAPI_AttributeInfo newMaterialPrimInfo = Create_HAPI_AttributeInfo(newNodePart, HAPI_ATTROWNER_PRIM, 1, HAPI_STORAGETYPE_STRING);
			result &= HAPIx(HAPI_AddAttribute(session->Get(), newNodeId, 0, "material", &newMaterialPrimInfo), msg);

			char ** materials = new char *[newMaterialPrimInfo.tupleSize * newMaterialPrimInfo.count];

			//red::DynArray<String> materials(newMaterialPrimInfo.count, red::PoolBackend());
			red::DynArray<Int32> chunks(newChunkIdPrimInfo.count, red::PoolBackend());
			red::DynArray<Int32> vertices(newNodePart.vertexCount, red::PoolBackend());

			Uint32 index = 0;
			Int32 accum_indicies = 0;
			Uint16 chunk_id = 0;

			red::DynArray<Uint32> indices;
			for( auto& geom : inGeometry )
			{
				for (Uint32 j = 0; j < geom.m_indicesCollision.Size(); ++j)
				{
					vertices[index] = geom.m_indicesCollision[j] + accum_indicies;
					chunks[index / 3] = chunk_id;
					materials[index / 3] = _strdup(geom.m_materialName.AsChar());
					index++;
				}
				accum_indicies += geom.m_verticesCollision.Size();
				chunk_id++;
			}

			result &= HAPIx(HAPI_SetAttributeStringData(session->Get(), newNodeId, 0, "material", &newMaterialPrimInfo, (const char**)materials, 0, newNodePart.faceCount), msg);
			result &= HAPIx(HAPI_SetAttributeIntData(session->Get(), newNodeId, 0, "chunk_id", &newChunkIdPrimInfo, chunks.TypedData(), 0, newNodePart.faceCount), msg);
			result &= HAPIx(HAPI_SetVertexList(session->Get(), newNodeId, 0, vertices.TypedData(), 0, newNodePart.vertexCount), msg);

			// @brief  Set the array of faces where the nth integer in the array is
			//         the number of vertices the nth face has.
			red::DynArray<Int32> face_counts;
			face_counts.Resize(newNodePart.faceCount, 3);

			result &= HAPIx(HAPI_SetFaceCounts(session->Get(), newNodeId, 0, face_counts.TypedData(), 0, newNodePart.faceCount), msg);
			result &= HAPIx(HAPI_CommitGeo(session->Get(), newNodeId), msg);
			result = HAPIx(HAPI_CookNode(session->Get(), newNodeId, session->GetCookingOptions()), msg);
		}

		if (result)
		{
			MapReferences(session, nodeEntry, newNodeId);
		}

		result &= CookHoudiniDigitalAsset(session, false);
		result &= RequestCookedData(session, outGeometry);
		result &= session->CleanUp();

		outGeometry.m_verticesCollision.Resize(outGeometry.m_vertices.Size());
		outGeometry.m_indicesCollision.Resize(outGeometry.m_indices.Size());

		std::transform(begin(outGeometry.m_vertices), end(outGeometry.m_vertices), begin(outGeometry.m_verticesCollision), [](const DebugVertex& v) -> Vector3 { return { v.x, v.y, v.z }; });
		std::transform(begin(outGeometry.m_indices), end(outGeometry.m_indices), begin(outGeometry.m_indicesCollision), [](Uint32 i) -> Uint16 { return static_cast<Uint16>(i); });

		session->GetInterfaceWrapper()->UnloadDependencyNodes(session->Get());

		return result;
	}

	BACKEND_ENGINE_API Bool CookTerrainNode(HoudiniEngineSession* session, const Float reductionRatio, const bool genOccluder,
		const TerrainDataHelper dataHelper, GeometryBuffer& outGeometry)
	{
		/* before usage make sure you initialize and start HE:
		if (!houdini_legacy::IsSessionValid(session))
		{
			if (!(Cast<CBackendEngine>(GEngine)->StartHoudiniEngine(msg)))
			{
				return;
			}
		}
		houdini_legacy::CookTerrainNode(...)
		*/

		Bool result = true;
		Int32 assetCount = 0;

		result &= LoadHoudiniDigitalAsset(session, "internal\\terrain_convert", assetCount);
		
		const Float x = (Float)dataHelper.m_heightSizex;
		const Float y = (Float)dataHelper.m_heightSizey;
		const Float z = math::Max((Float)dataHelper.m_heightSizex, (Float)dataHelper.m_heightSizey);

		ResourceEntryElement nodeEntry;
		nodeEntry.m_box = Box( Vector4(-x, -y, -z), Vector4(x, y, z) );
		nodeEntry.m_heightfieldDataSizeX = x;
		nodeEntry.m_heightfieldDataSizeY = y;

		const red::String reductionParam = "triangle_reduction_ratio";
		const red::String genOccluderParam = "gen_occluder";

		result &= HAPIx(HAPI_SetParmFloatValue(session->Get(), session->GetInterfaceWrapper()->GetNodeID(), reductionParam.AsChar(), 0, reductionRatio));
		result &= HAPIx( HAPI_SetParmIntValue(session->Get(), session->GetInterfaceWrapper()->GetNodeID(), genOccluderParam.AsChar(), 0, genOccluder?1:0 ) );
		result &= AddHeightfieldSOPNode(session, nodeEntry, dataHelper);
		result &= CookHoudiniDigitalAsset(session, false);
		result &= RequestCookedData(session, outGeometry);
		//result &= session->CleanUp(); //Keep it for reuse. LoadHoudiniDigitalAsset will drop it if different HDA is loaded

		return result;
	}

	BACKEND_ENGINE_API Bool GetRootObjectLocalTransform(HoudiniEngineSession* session, world::NodeTransform& transform)
	{
		THandle< houdini_legacy::IHDAInterfaceWrapper > wrapper = session->GetInterfaceWrapper();
		world::NodeTransform initialTransform, localTransform;
		Transform activePrefabTransform, rootObjectTransform;
		Vector3 activePrefabscale, rootObjectScale;

		wrapper->GetActivePrefabTransform(activePrefabTransform, activePrefabscale);
		wrapper->GetRootObjectTransform(rootObjectTransform, rootObjectScale);
		transform.SetTranslation(rootObjectTransform.GetPosition().AsVector3() - activePrefabTransform.GetPosition().AsVector3());
		return true;	
	}

	Uint64 GetUniqueHash(const ResourceEntryElement& entry)
	{
		Uint64 hash = entry.m_attribName.CalcHash();
		hash += entry.m_resourcePath.CalcHash();
		hash += entry.m_sourceImportPath.AsString().CalcHash();
		hash += entry.m_attribValue.CalcHash();
		hash += ToStringDirect<Vector3>(entry.m_transform.GetPosition3()).CalcHash();
		hash += ToStringDirect<Vector4>(entry.m_transform.GetOrientation().AsVector()).CalcHash();
		hash += ToStringDirect<Vector3>(entry.m_scale).CalcHash();
		return hash;
	}
} 
