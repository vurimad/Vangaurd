/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../external/houdini/include/HAPI/HAPI.h"
#include "../../../common/core/include/message.h"

namespace houdini_legacy
{
	static CName rootObjectName = RED_NAME_CONSTEXPR("root_object");
	static CName outputTypeName = RED_NAME_CONSTEXPR("output_type");
	static CName outputFilePath = RED_NAME_CONSTEXPR("output_filepath");// obsolete. Use outputPrefabJson instead?
	static CName outputResourcePath = RED_NAME_CONSTEXPR("output_resourcepath");
	static CName outputResourceName = RED_NAME_CONSTEXPR("output_resourcename");
	static CName outputActionName = RED_NAME_CONSTEXPR("output_action");
	static CName outputFoliageJson = RED_NAME_CONSTEXPR("output_foliage_json");
	static CName outputPrefabJson = RED_NAME_CONSTEXPR("output_prefab_json");
	static CName outputName = RED_NAME_CONSTEXPR("output_name");
	static CName outputResourceDir = RED_NAME_CONSTEXPR("output_resource_dir");
	static CName togglePropName = RED_NAME_CONSTEXPR("toggle");
	static CName comboBoxPropName = RED_NAME_CONSTEXPR("heChoice");
	static CName requiresFullGeo = RED_NAME_CONSTEXPR( "requires_full_geo" );

	enum outputType
	{
		hOutput_None,
		hOutput_Mesh,
		hOutput_Prefab,
		hOutput_Json,
		hOutput_Png,
		hOutput_Heightmap,
		hOutput_Heightmap_InitializeOnly,
		hOutput_Foliage,
		hOutput_Foliage_JsonOnly,
		hOutput_MlMask,
		hOutput_MlMask_InitializeOnly,
		hOutput_BendedStripeMesh
	};

	enum terrainMlMasksUpdateMode
	{
		mlMaskUpdateAlways,
		mlMaskUpdateOnce,
		mlMaskUpdateNone
	};

	enum terrainHeightUpdateMode
	{
		heightUpdateAlways,
		heightUpdateOnce,
		heightUpdateNone
	};

	enum outputAction
	{
		hOutput_DoNothing,
		hOutput_AsNewResource,
		hOutput_InPlace
	};

	struct AttributeData
	{
		Uint32 m_atrribCount;
		Uint32 m_tupleSize;
		void* m_attribData;

		AttributeData()
			: m_atrribCount(0)
			, m_tupleSize(1)
			, m_attribData(nullptr)
		{}
	};

	struct TerrainDataHelper
	{
		Float* m_heightRawData;

		Uint32 m_heightSizex;
		Uint32 m_heightSizey;

		Float* m_maskRawData;
		Float m_heightRange;
		Float m_heightScale;
		Float m_heightOffset;
		Float m_heightMin;

		//mlMasks
		Uint64 multilayerChannelsMasks;
		red::DynArray<Uint8*> mlMasksRawData;
		red::DynArray<String> mlMasksNames;
	};

	struct CurveDataHelper
	{
		String m_presetName = "";
		String m_curveName = "";
		Float m_radiusMultiply = -1;
		Float m_restlength = -1;
		Float m_bendStiffness = -1;
		Uint8 m_sides = -1;
		Float m_curveQuality = -1;
		Float m_curveQualityCollision = 1;
		Float m_finalReduce = -1;
		Float m_finalCollisionReduce = -1;
		String m_materialPath = "";
		String m_outputResourcePath = "";
		Uint8 m_uvRangeStart = 1;
		Uint8 m_uvRangeEnd = 1;
		String m_hash = "";
		String m_destructionOwnerHashStart = String::EMPTY();
		String m_destructionOwnerHashEnd = String::EMPTY();
	};

	// import / export helpers
	class BACKEND_ENGINE_API ResourceEntryElement : public ISerializable
	{
		RTTI_DECLARE_TYPE(ResourceEntryElement);
		RED_BASE_CLASS(ISerializable);
	public:
		red::String m_resourcePath = red::String();
		red::AbsolutePath m_sourceImportPath = red::AbsolutePath();
		CName m_resourceType = CName();

		Transform m_transform = Transform::IDENTITY();
		Vector3 m_scale = Vector3::ONES();
		Box m_box = Box();
		red::String m_attribName = red::String();
		red::String m_attribValue = red::String();
		Float m_heightfieldDataSizeX = 0;
		Float m_heightfieldDataSizeY = 0;

		CurveDataHelper m_curveData;
	};

	class BACKEND_ENGINE_API ResourceListArray : public ISerializable
	{
		RTTI_DECLARE_TYPE(ResourceListArray);
		RED_BASE_CLASS(ISerializable);
	public:
		red::DynArray< ResourceEntryElement > m_entries;
	};

	class BACKEND_ENGINE_API IHDACustomPropetyContext : public ISerializable
	{
		RTTI_DECLARE_TYPE(IHDACustomPropetyContext);
		RED_BASE_CLASS(ISerializable);

	public:
		IHDACustomPropetyContext();
		virtual ~IHDACustomPropetyContext() { }

		red::Map< CName, red::DynArray<String> > m_map;
	};

	class BACKEND_ENGINE_API IHDALogReporter
	{
		RTTI_DECLARE_TYPE(IHDALogReporter);
	public:
		IHDALogReporter(tools::IMessageReporter& messageReporter);
		void Report(const red::String& message, const res::ResourcePath& resourcePath = res::ResourcePath() ) const;
		void ReportError(const red::String& message, const res::ResourcePath& = res::ResourcePath()) const;

	private:
		tools::IMessageReporter& m_messageReporter;
		void DoReport(const red::String& message, const tools::MessageSeverity severity, const res::ResourcePath& resourcePath ) const;
	};

	// houdini digital asset interface wrapper
	class BACKEND_ENGINE_API IHDAInterfaceWrapper : public ISerializable
	{
		struct IHDAProperty
		{
			Bool m_required = false;
			std::pair< rtti::ClassEditablePropertyInfo,
				red::SharedPtr< rtti::ValueHolder > > m_property;
		};

		RTTI_DECLARE_TYPE( IHDAInterfaceWrapper );
		RED_BASE_CLASS( ISerializable );

	public:
		virtual void OnPropertyPostChange(const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& newValue);
		virtual void GetCustomEditableProperties(rtti::EditableProperties& outRootProperties) const override;
		virtual Bool ReadCustomEditableProperty(IRTTIContext& ctx, const CName propertyName,
			const rtti::AccessPath& restOfThePath, red::SharedPtr< rtti::ValueHolder >& outValue) const override;
		virtual Bool WriteCustomEditableProperty(IRTTIContext& ctx, const CName propertyName,
			const rtti::AccessPath& restOfThePath, const red::SharedPtr< rtti::ValueHolder >& newValue) override;

		template <class T>void AddAttribute(const CName& attrName, const red::String& attrLabel, const T& value,
			Bool required = false, const CName& customTypeName = CName())
		{
			rtti::ClassEditablePropertyInfo info;
			info.m_name = attrName;
			info.m_type = GetTypeObject< T >();
			info.m_tooltip = attrLabel;

			rtti::ValuePtr valueHolder;
			info.m_type->ReadValue(IRTTIContext::GetDefault(), &value, rtti::AccessPath(), valueHolder);
			info.m_customTypeName = customTypeName;

			IHDAProperty p;
			p.m_property.first = info;
			p.m_property.second = valueHolder;
			p.m_required = required;

			m_properties.PushBack(p);
		}

		template <class T>void AddAttribute(const CName& attrName, const red::String& attrLabel, const T& value,
			const T& min, const T& max, Bool required = false)
		{
			AddAttribute(attrName, attrLabel, value, required);
			m_properties.Back().m_property.first.m_maxValue = (Float)max;
			m_properties.Back().m_property.first.m_minValue = (Float)min;
		}

		template <class T>Bool SetPropertyValue(const CName& propertyName, const T& value)
		{
			for (auto& p : m_properties)
			{
				if (p.m_property.first.m_name == propertyName
					&& p.m_property.first.m_type == GetTypeObject<T>())
				{
					rtti::ValuePtr valueHolder;
					const rtti::ValuePtr oldValue = p.m_property.second;
					p.m_property.first.m_type->ReadValue(IRTTIContext::GetDefault(), &value, rtti::AccessPath(), valueHolder);
					p.m_property.second = valueHolder;

					const rtti::AccessPath path(propertyName.AsChar());
					NotifyPropertyChanged(path, oldValue, valueHolder);
					return true;
				}
			}
			return false;
		}

		template <class T>void GetPropertyByType( red::DynArray<CName>& propertyNames )
		{
			for (auto& p : m_properties)
			{
				if (p.m_property.first.m_type == GetTypeObject<T>())
				{
					propertyNames.PushBack(p.m_property.first.m_name);
				}
			}
		}

		Bool LoadPreset(String filePath);
		Bool SavePreset(String filePath);
		red::DynArray< String > GetPresetsList(String presetsPath);

		const red::String GetPropertyValue(const CName& propertyName);
		const red::String GetPropertyValue(const CName& propertyName, Bool& outRequired, red::String& outType = String());

		void GetRootObjectTransform(Transform& originTransform, Vector3& scale);
		void SetRootObjectTransform(const Transform& originTransform, const  Vector3& scale);

		Vector3 GetPivotPosition();
		void SetPivotPosition(Vector3 pivotTransform);

		void GetActivePrefabTransform(Transform& originTransform, Vector3& scale);
		void SetActivePrefabTransform(const Transform& originTransform, const Vector3& scale);

		const red::DynArray<outputType> GetOutputType();
		const Bool hasOutputType(houdini_legacy::outputType);		

		const red::AbsolutePath GetOutputPath();
		const outputAction GetOutputAction();

		const HAPI_NodeId GetNodeID();
		void SetNodeID(const HAPI_Session* session, HAPI_NodeId nodeID);

		const red::String& GetNodeNamespace() { return m_nodeNamespace; }
		void SetNodeNamespace(const red::String& nodeNamespace );

		void AddDependencyNode(const HAPI_NodeId& node, const red::String& name);
		const HAPI_NodeId GetDependencyNode(const red::String& name);
		const red::DynArray<HAPI_NodeId> GetDependencyNodes();

		Bool UnloadNodes(const HAPI_Session* session);
		Bool UnloadDependencyNodes(const HAPI_Session* session);
		void UnloadDependencyNode(const HAPI_Session* session, const HAPI_NodeId nodeId);

		Uint32 GetPropertiesCount() const { return m_properties.Size(); };

		// status & progress handling
		void SetStatus(const Float prog, red::String& status = red::String() );
		void AddProgress(const Float progToAdd = 1.0f, red::String& status = red::String());
		const Float GetProgress() const { return Clamp<Float>(m_statusProgress, 0.0f, 100.0f); };
		void SetCookingStatus(Bool status);

		// TEMP
		red::DynArray< ResourceEntryElement >	m_nodeInstances;
		THandle< IHDACustomPropetyContext >		m_context;
		interop::InteropNotifier				m_propertyChangedNotifier;
		interop::InteropNotifier				m_statusChangedNotifier;
		interop::InteropNotifier				m_cookingNotifier;
		red::DynArray< res::ResourcePath >		m_depotResourcesToAdd;

		IHDAInterfaceWrapper();
		virtual ~IHDAInterfaceWrapper() {};

	private:
		red::DynArray < IHDAProperty >			m_properties;
		HAPI_NodeId								m_nodeID;
		red::String								m_nodeNamespace;

		Float									m_statusProgress;

		Transform								m_rootObjectTransform = Transform::IDENTITY();
		Vector3									m_pivotTransform = Vector3::ZEROS();

		Vector3									m_rootObjecScale = Vector3::ONES();

		Transform								m_activePrefabTransform = Transform::IDENTITY();
		Vector3									m_activePrefabScale = Vector3::ONES();

		red::DynArray< std::pair< HAPI_NodeId, red::String > >	m_dependencyNodes;

		const red::DynArray<outputType> OutputTypeFromString(const red::String& str);
		const outputAction OutputActionFromString(const red::String& str);
		const terrainMlMasksUpdateMode terrainMlMasksUpdateModeFromString(const red::String& str);
	};

} // houdini
