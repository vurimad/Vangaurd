/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "houdiniEngineInterface.h"
#include "../../../common/resource/include/jsonSerializer.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/resource/include/resourceEditorDepotImpl.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE(IHDACustomPropetyContext, houdini_legacy);
	RTTI_PARENT_TYPE(ISerializable);
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE(IHDAInterfaceWrapper, houdini_legacy);
	RTTI_PARENT_TYPE(ISerializable);
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE(ResourceEntryElement, houdini_legacy);
	RTTI_PARENT_TYPE(ISerializable);
	RTTI_PROPERTY(m_resourcePath);
	RTTI_PROPERTY(m_sourceImportPath);
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE(ResourceListArray, houdini_legacy);
	RTTI_PARENT_TYPE(ISerializable);
	RTTI_PROPERTY(m_entries);
RTTI_END_TYPE();

namespace houdini_legacy
{
	IHDALogReporter::IHDALogReporter(tools::IMessageReporter& messageReporter)
		: m_messageReporter{ messageReporter }
	{
	}
	void IHDALogReporter::Report(const red::String& message, const res::ResourcePath& resourcePath) const
	{
		DoReport(message, tools::MessageSeverity::Info, resourcePath);
	}
	void IHDALogReporter::ReportError(const red::String& message, const res::ResourcePath& resourcePath) const
	{
		DoReport(message, tools::MessageSeverity::Error, resourcePath);
	}

	void IHDALogReporter::DoReport(const red::String& message, const tools::MessageSeverity severity, const res::ResourcePath& resourcePath) const
	{
		m_messageReporter.Report(severity, tools::IMessageLocation::Build(resourcePath), std::move(message.AsChar()));
	}

	IHDACustomPropetyContext::IHDACustomPropetyContext()
	{
		m_map.Clear();
	};

	IHDAInterfaceWrapper::IHDAInterfaceWrapper()
		: m_context{ red::CreateUniquePtr<IHDACustomPropetyContext>() }
		, m_statusProgress(0.0f)
		, m_nodeID(-1)
	{
		m_properties.Clear();
	};

	void IHDAInterfaceWrapper::GetCustomEditableProperties(rtti::EditableProperties& outRootProperties) const
	{
		for (auto p : m_properties)
		{
			outRootProperties.Add(p.m_property.first);
		}
	};

	void IHDAInterfaceWrapper::OnPropertyPostChange(const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& newValue)
	{
		TBaseClass::OnPropertyPostChange(path, oldValue, newValue);

		if (oldValue != newValue)
		{
			m_propertyChangedNotifier.Notify();
		}
	}

	Bool IHDAInterfaceWrapper::ReadCustomEditableProperty(IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, red::SharedPtr< rtti::ValueHolder >& outValue) const
	{
		for (auto p : m_properties)
		{
			if (p.m_property.first.m_name == propertyName)
			{
				outValue = p.m_property.second;
				return true;
			}
		}

		if (m_properties.Empty())
		{
			outValue = rtti::ValueHolder::CreateEmpty();
		}
		return true;
	};

	Bool IHDAInterfaceWrapper::WriteCustomEditableProperty(IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, const red::SharedPtr< rtti::ValueHolder >& newValue)
	{
		for (auto& p : m_properties)
		{
			if (p.m_property.first.m_name == propertyName)
			{
				p.m_property.second = newValue;
				return true;
			}
		}
		return false;
	};

	void IHDAInterfaceWrapper::SetStatus( const Float prog, red::String& status )
	{
		if( !status.Empty() )
		{
			m_statusChangedNotifier.Notify(status);
		}

		if (m_statusProgress != prog)
		{
			m_statusProgress = prog;
			m_statusChangedNotifier.Notify(m_statusProgress);
		}
	}

	void IHDAInterfaceWrapper::AddProgress(const Float progToAdd, red::String& status)
	{
		SetStatus(m_statusProgress + progToAdd, status);
	};

	void IHDAInterfaceWrapper::SetCookingStatus(Bool status)
	{
		m_cookingNotifier.Notify(status);
	}

	const red::String IHDAInterfaceWrapper::GetPropertyValue(const CName& propertyName)
	{
		Bool required = false;
		return GetPropertyValue(propertyName, required);
	}
	const red::String IHDAInterfaceWrapper::GetPropertyValue(const CName& propertyName, Bool& outRequired, red::String& outType)
	{
		for (auto& p : m_properties)
		{
			const rtti::ClassEditablePropertyInfo propertyInfo = p.m_property.first;
			const red::SharedPtr< rtti::ValueHolder > propertyValue = p.m_property.second;

			if (propertyName == propertyInfo.m_name)
			{
				outRequired = p.m_required;
				outType = propertyInfo.m_type->GetERTTITypeString();

				if (propertyValue->IsSingle())
				{
					return propertyValue->GetSingleValue();
				}
				else if (propertyValue->IsArray())
				{
					red::String valueStr = "[";
					const Uint32 numElements = propertyValue->GetNumElements();
					for (Uint32 i = 0; i < numElements; ++i)
					{
						valueStr += propertyValue->GetStructElement(i)->GetSingleValue();

						if (i != (numElements - 1))
							valueStr += ", ";
					}
					valueStr += "]";
					return valueStr;
				}
				else if (propertyValue->IsHandle())
				{
					return propertyValue->GetHandleValue();
				}
				else
					return String();
			}
		}
		return String();
	}


	Bool IHDAInterfaceWrapper::LoadPreset(String filePath)
	{
		Bool result = true;
		red::AbsolutePath jsonFilePath = red::AbsolutePath::CreateFilePath(filePath+".json");
		if (!jsonFilePath.Empty())
		{
			using JSONDocument = rapidjson::JSONDocument;
			JSONFileHelper::JSONDocument importedDocument;
			using JSONValue = rapidjson::JSONValue;
			red::DynArray< Uint8 > fileData{ red::PoolBackend() };

			if (red::LoadFileToBuffer(jsonFilePath, fileData))
			{
				importedDocument.Parse< rapidjson::kParseStopWhenDoneFlag >((AnsiChar*)fileData.Data());
				if (importedDocument.HasMember("params"))
				{
					if (importedDocument["params"].IsArray())
					{
						const auto& parms = importedDocument["params"].GetArray();

						for (Uint32 i = 0; i < parms.Size(); ++i)
						{
							const rapidjson::JSONValue& parm = parms[i];
							CName parmName = RED_NAME(parm["name"].GetString());
							String parmType = parm["type"].GetString();
							String parmValue = parm["value"].GetString();

							if (parmType.EqualsNC("Float") )
							{
								const Float value = FromStringDirect<Float>(parmValue);
								SetPropertyValue<Float>(parmName, value);
							}
							else if  (parmType.EqualsNC("rRef:CResource"))
							{
								const res::ResourceReference value(res::ResourcePath::Build(parmValue)); // = parm[parmName.AsChar()].GetString();
								TResRef< CResource > res(value.GetPath());

								result &= SetPropertyValue<TResRef< CResource >>(parmName, res);
							}
							else if (parmType.EqualsNC("String"))
							{
								const String &value = parmValue;
								result &= SetPropertyValue<String>(parmName, value);
							}
							else if (parmType.EqualsNC("CName"))
							{
								const CName value = RED_NAME(parmValue);
								result &= SetPropertyValue<CName>(parmName, value);
							}
							else if (parmType.EqualsNC("Int32"))
							{
								const Int32 value = FromStringDirect<Int32>(parmValue);
								result &= SetPropertyValue<Int32>(parmName, value);
							}
							else if (parmType.EqualsNC("Bool"))
							{
								const Bool value = FromStringDirect<Bool>(parmValue);
								result &= SetPropertyValue<Bool>(parmName, value);
							}

						}
					}
				}
			}
		}
		return result;
	}

	Bool IHDAInterfaceWrapper::SavePreset(String filePath)
	{
		Bool result = true;
		const red::AbsolutePath targetPath = red::AbsolutePath::CreateFilePath(filePath);
		red::UniquePtr< IFile > file = GFileManager->CreateFileWriter(targetPath, FOF_AbsolutePath);

		if (file)
		{
			JSONFileHelper::JSONFileStream fileStream(file.Get());
			JSONFileHelper::JSONWriter writer(fileStream);

			writer.StartObject();
			writer.Key("params");
			writer.StartArray();

			for (auto it : m_properties)
			{
				const rtti::IType* type = GetTypeObject(it.m_property.second.Get()->GetHandleValue());
				if (it.m_property.second->IsArray())
				{
					const Uint32 numElements = it.m_property.second->GetNumElements();

					for (Uint32 i = 0; i < numElements; i++)
					{
						auto data = it.m_property.second->GetArrayElement(i);
						String value = data->GetSingleValue();
					}
				}
				CName typeName = it.m_property.first.m_type->GetName();
				CName propertyName = it.m_property.first.m_name;
				String value = it.m_property.second.Get()->GetSingleValue();

				if (propertyName == RED_NAME_CONSTEXPR_NOREG( "output_resourcename" ) || typeName == RED_NAME_CONSTEXPR_NOREG( "NodeRef" ) ) continue;

				writer.StartObject();
				writer.Key("type");
				writer.String(typeName.AsChar());
				writer.Key("name");
				writer.String(propertyName.AsChar());
				writer.Key("value");
				writer.String(value.AsChar());
				writer.EndObject();
			}

			writer.EndArray();
			writer.EndObject();
			result &= writer.IsComplete();
		}


		return result;
	}

	red::DynArray<String> IHDAInterfaceWrapper::GetPresetsList(String presetsPath)
	{
		red::DynArray< String > files{ red::PoolBackend() };
		const String searchPattern = "*.json";
		res::GetEditorDepot()->FindDepotFiles(presetsPath, searchPattern.AsChar(), files);

		//delete extensions
		red::DynArray< String > outFiles{ red::PoolBackend() };
		for (String file : files)
		{
			file = red::paths::RemoveExtension(file).ToString();
			outFiles.PushBack(file);
		}

		return outFiles;
	}

	void IHDAInterfaceWrapper::SetNodeID(const HAPI_Session* session, HAPI_NodeId nodeID)
	{
		UnloadNodes(session);
		UnloadDependencyNodes(session);
		m_context->m_map.Clear();

		m_nodeID = nodeID;
	}

	void IHDAInterfaceWrapper::SetNodeNamespace(const red::String& nodeNamespace)
	{
		m_nodeNamespace = nodeNamespace;
	}

	Bool IHDAInterfaceWrapper::UnloadNodes(const HAPI_Session* session)
	{
		Bool result = true;
		result &= UnloadDependencyNodes(session);

		if (m_nodeID != -1)
		{
			HAPI_DeleteNode(session, m_nodeID);
			m_properties.Clear();
			m_nodeID = -1;
		}
		return result;
	}

	Bool IHDAInterfaceWrapper::UnloadDependencyNodes(const HAPI_Session* session)
	{
		Bool result = true;
		for (auto n : m_dependencyNodes)
		{
			HAPI_DeleteNode(session, n.first);
		}
		m_dependencyNodes.Clear();
		return result;
	}

	void IHDAInterfaceWrapper::UnloadDependencyNode(const HAPI_Session* session, const HAPI_NodeId nodeId)
	{
		for (Uint32 i = 0; i < m_dependencyNodes.Size(); ++i)
		{
			if (m_dependencyNodes[i].first == nodeId)
			{
				m_dependencyNodes.RemoveAt(i);
				break;
			}
		}

		HAPI_DeleteNode(session, nodeId);
	}

	void IHDAInterfaceWrapper::AddDependencyNode(const HAPI_NodeId& node, const red::String& name)
	{
		m_dependencyNodes.PushBack(std::pair<HAPI_NodeId, red::String>(node, name));
	}

	const HAPI_NodeId IHDAInterfaceWrapper::GetDependencyNode(const red::String& name)
	{
		if (!m_dependencyNodes.Empty())
		{
			for (auto n : m_dependencyNodes)
			{
				if ( strcmp( n.second.AsChar(), name.AsChar() ) == 0 )
					return n.first;
			}
		}
		return -1;
	}
	const red::DynArray<HAPI_NodeId> IHDAInterfaceWrapper::GetDependencyNodes()
	{
		red::DynArray<HAPI_NodeId> outNodes;
		for (auto n : m_dependencyNodes)
		{
			outNodes.PushBack(n.first);
		}
		return outNodes;
	}

	// WIP
	const red::DynArray<outputType> IHDAInterfaceWrapper::OutputTypeFromString(const red::String& str)
	{
		red::DynArray<outputType> outTypes;
		const red::DynArray<red::String> outputSplit = str.Split("+");

		for (auto s : outputSplit)
		{
			if (s == "Mesh") outTypes.PushBack(hOutput_Mesh);
			else if (s == "Prefab") outTypes.PushBack(hOutput_Prefab);
			else if (s == "Json") outTypes.PushBack(hOutput_Json);
			else if (s == "Png") outTypes.PushBack(hOutput_Png);
			else if (s == "Heightmap") outTypes.PushBack(hOutput_Heightmap);
			else if (s == "Heightmap_InitializeOnly") outTypes.PushBack(hOutput_Heightmap_InitializeOnly);
			else if (s == "Foliage") outTypes.PushBack(hOutput_Foliage);
			else if (s == "Foliage_JsonOnly") outTypes.PushBack(hOutput_Foliage_JsonOnly);
			else if (s == "MlMask") outTypes.PushBack(hOutput_MlMask);
			else if (s == "BendedStripeMesh") outTypes.PushBack(hOutput_BendedStripeMesh);
			else outTypes.PushBack(hOutput_None);
		}

		return outTypes;
	}

	const outputAction IHDAInterfaceWrapper::OutputActionFromString(const red::String& str)
	{
		if (str == "New_Resource") return hOutput_AsNewResource;
		else if (str == "In_Place") return hOutput_InPlace;
		else return hOutput_DoNothing;
	}

	const terrainMlMasksUpdateMode IHDAInterfaceWrapper::terrainMlMasksUpdateModeFromString(const red::String& str)
	{
		if (str == "mlMaskUpdateAlways") return mlMaskUpdateAlways;
		else if (str == "mlMaskUpdateOnce") return mlMaskUpdateOnce;
		else return mlMaskUpdateNone;
	}

	const red::DynArray<outputType> IHDAInterfaceWrapper::GetOutputType()
	{
		return OutputTypeFromString(GetPropertyValue(outputTypeName));
	}

	const Bool IHDAInterfaceWrapper::hasOutputType(houdini_legacy::outputType name)
	{
		const red::DynArray<outputType> outTypes = OutputTypeFromString(GetPropertyValue(outputTypeName));
		for (auto s : outTypes)
		{
			if (s == name) return true;
		}
		return false;
	}

	const outputAction IHDAInterfaceWrapper::GetOutputAction()
	{
		return OutputActionFromString(GetPropertyValue(outputActionName));
	}

	const red::AbsolutePath IHDAInterfaceWrapper::GetOutputPath()
	{
		return red::AbsolutePath::CreateFilePath(GetPropertyValue(outputFilePath));
	}

	const HAPI_NodeId IHDAInterfaceWrapper::GetNodeID()
	{
		return m_nodeID;
	}

	void IHDAInterfaceWrapper::SetRootObjectTransform(const Transform& originTransform,const  Vector3& scale)
	{
		 m_rootObjectTransform = originTransform;
		 m_rootObjecScale = scale;
	}
	void IHDAInterfaceWrapper::GetRootObjectTransform(Transform& originTransform, Vector3& scale)
	{
		originTransform = m_rootObjectTransform;
		scale = m_rootObjecScale;
	}

	void IHDAInterfaceWrapper::SetPivotPosition(Vector3 pivotTransform)
	{
		if (m_pivotTransform.Mag() != 0 && pivotTransform.Mag()!=0)
		{
			m_pivotTransform = Vector3::Lerp(m_pivotTransform, pivotTransform, 0.5);
		}
		else
		{
			m_pivotTransform = pivotTransform;
		}

	}
	Vector3  IHDAInterfaceWrapper::GetPivotPosition()
	{
		return m_pivotTransform;

	}

	void IHDAInterfaceWrapper::SetActivePrefabTransform(const Transform& originTransform, const Vector3& scale)
	{
		m_activePrefabTransform = originTransform;
		m_activePrefabScale = scale;
	}

	void IHDAInterfaceWrapper::GetActivePrefabTransform(Transform& originTransform, Vector3& scale)
	{
		originTransform = m_activePrefabTransform;
		scale = m_activePrefabScale;
	}
}