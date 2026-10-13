/**
* Copyright (c) 2017-2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "meshMultilayerMaterialProxy.h"
#include "meshDataView.h"
#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMesh/include/meshAppearance.h"
#include "../../../common/resourceMaterial/include/material.h"
#include "../../../common/resourceMaterial/include/materialDefinition.h"
#include "../../../common/resourceMaterial/include/materialInstance.h"
#include "../../../common/resourceMaterial/include/materialParameter.h"
#include "../../../common/resourceMaterial/include/materialTemplate.h"
#include "../../../common/redReflection/include/rttiPathParser.h"
#include "../../backendEngine/include/backendEnginePublic.h"
#include "../../backendEngine/include/editableCollectionCustomPropertyEditor.h"


RTTI_BEGIN_TYPE_IN_NAMESPACE( MeshMultilayerMaterialProxy, tools );
	RTTI_PARENT_TYPE( ISerializable );
	RTTI_PROPERTY_CATEGORY( "Layers" );
	RTTI_PROPERTY( m_currentLayer ).editable().customEditor( "MeshMultilayerMaterialLayerName" );
	RTTI_PROPERTY_CATEGORY( "Global params" );
RTTI_END_TYPE();


namespace tools
{
	//////////////////////////////////////////////////////////////////////////////////////////////
	MeshMultilayerMaterialProxy::MeshMultilayerMaterialProxy()
	{
	}

	MeshMultilayerMaterialProxy::MeshMultilayerMaterialProxy( const THandle< CMesh >& mesh, const mesh::ChunkMaterials& chunkMaterials,
	                                                          const std::function< void() >& layerSelectionChangedCallback )
		: m_mesh( mesh )
		, m_layerSelectionChangedCallback( layerSelectionChangedCallback )
		, m_layers( red::PoolBackend() )
		, m_layerNames( red::PoolBackend() )
	{
		m_view = red::CreateUniquePtr< mesh::DataView >( *m_mesh );

		for ( Uint32 i = 0; i < chunkMaterials.m_materialNames.Size(); ++i )
		{
			m_layerNames.PushBack( chunkMaterials.m_materialNames[i].AsChar() );
			THandle<IMaterial> data = m_view->GetMaterialDataByName( chunkMaterials.m_materialNames[i] );
			m_layers.PushBack( data );
		}
		if (chunkMaterials.m_materialNames.Size() > 0)
		{
			m_currentLayer = chunkMaterials.m_materialNames.Front();
			m_currentMaterial = m_view->GetMaterialDataByName(m_currentLayer);
		}
	}

	MeshMultilayerMaterialProxy::~MeshMultilayerMaterialProxy()
	{
	}

	void MeshMultilayerMaterialProxy::OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& value )
	{
		ISerializable::OnPropertyPostChange( path, oldValue, value );

		CName propName;
		rtti::PathParser parser( path );

		if ( parser.EatName( propName ) )
		{
			if ( propName == RED_NAME_CONSTEXPR_NOREG( "currentLayer" ) )
			{
				CName newLayerName = RED_NAME( value->GetSingleValue() );
				THandle< IMaterial > newLayerMaterial = m_view->GetMaterialDataByName( newLayerName );
				if ( newLayerMaterial != nullptr )
				{
					m_currentMaterial = newLayerMaterial;
					if ( m_layerSelectionChangedCallback )
						m_layerSelectionChangedCallback();
				}
			}
		}
	}

	void MeshMultilayerMaterialProxy::GetCustomEditableProperties( rtti::EditableProperties& outRootProperties ) const
	{
		rtti::EditableProperties layerProps;
		if (m_currentMaterial == nullptr)
			return;
		m_currentMaterial->GetCustomEditableProperties( layerProps );
		Bool isCurrentMaterialLocalIntance = IsMaterialLocalInstance( m_currentMaterial );

		for ( const auto& sourceProp : layerProps.Get() )
		{
			rtti::ClassEditablePropertyInfo prop( sourceProp );
			if ( IsLayerMaterialProperty( prop.m_name ) )
			{
				prop.m_category = RED_NAME( (String( "Layer " ) + prop.m_category.AsChar()) );
				if ( !IsMaterialPropertyEditable( prop.m_name ) )
					prop.m_isReadOnly = true;
				if ( !isCurrentMaterialLocalIntance )
					prop.m_isReadOnly = true;														/// @todo DP: what about this? we should do auto instancing here
				outRootProperties.Add( prop );
			}
			else if ( IsGlobalMaterialProperty( prop.m_name ) )
			{
				prop.m_category = RED_NAME_CONSTEXPR( "Global params" );
				outRootProperties.Add( prop );
			}
		}
	}

	Bool MeshMultilayerMaterialProxy::ReadCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, red::SharedPtr< rtti::ValueHolder >& outValue ) const
	{
		// We can just read from the current layer. All global properties should be identical across layer materials,
		// so it should not matter which layer materials we are reading from.
		if (m_currentMaterial == nullptr)
			return false;
		return m_currentMaterial->ReadCustomEditableProperty( ctx, propertyName, restOfThePath, outValue );
	}

	Bool MeshMultilayerMaterialProxy::WriteCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, const red::SharedPtr< rtti::ValueHolder >& newValue )
	{
		if ( IsGlobalMaterialProperty( propertyName ) )
		{
			// Replicate value of global property to all layer materials.
			// This will screw with the undo mechanism, but since all layer materials should have the same values for global properties
			// we should be fine here. When some multilayer materials has different values for a property in some layers,
			// the undo will not be able to restore that values.
			Bool result = true;
			for ( const THandle<IMaterial>& material : m_layers )
			{
				Bool layerResult = material->WriteCustomEditableProperty( ctx, propertyName, restOfThePath, newValue );
				result &= layerResult;
			}

			return result;
		}
		else
		{
			if (m_currentMaterial == nullptr)
				return false;
			return m_currentMaterial->WriteCustomEditableProperty(ctx, propertyName, restOfThePath, newValue);
		}
	}

	Bool MeshMultilayerMaterialProxy::IsMaterialLocalInstance( const THandle<IMaterial>& material ) const
	{
		return (material != nullptr) && m_view->IsMaterialLocalInstance( material.Get() );
	}

	Bool MeshMultilayerMaterialProxy::IsGlobalMaterialProperty( CName propertyName )
	{
		return 
			propertyName == RED_NAME_CONSTEXPR_NOREG( "GlobalNormal" ) ||
			propertyName == RED_NAME_CONSTEXPR_NOREG( "Mask" ) ||
			propertyName == RED_NAME_CONSTEXPR_NOREG( "is_df" );								/// @todo DP: optimize use hash map here
	}

	Bool MeshMultilayerMaterialProxy::IsLayerMaterialProperty( CName propertyName )
	{
		return 
			propertyName == RED_NAME_CONSTEXPR_NOREG( "MicroBlends" ) ||						/// @todo DP: optimize use hash map here
			propertyName == RED_NAME_CONSTEXPR_NOREG( "TintColor" ) ||
			propertyName == RED_NAME_CONSTEXPR_NOREG( "LayerTile" ) ||
			propertyName == RED_NAME_CONSTEXPR_NOREG( "MicroblendTile" ) ||
			propertyName == RED_NAME_CONSTEXPR_NOREG( "MicroblendContrast" ) ||
			propertyName == RED_NAME_CONSTEXPR_NOREG( "MicroblendNormalStrength" ) ||
			propertyName == RED_NAME_CONSTEXPR_NOREG( "LayerOpacity" ) ||
			propertyName == RED_NAME_CONSTEXPR_NOREG( "LayerOffsetU" ) ||
			propertyName == RED_NAME_CONSTEXPR_NOREG( "LayerOffsetV" );
	}

	Bool MeshMultilayerMaterialProxy::IsMaterialPropertyEditable( CName propertyName )
	{																							/// @todo DP: optimize use hash map here
		return
			propertyName == RED_NAME_CONSTEXPR_NOREG( "MicroBlends" ) ||
			propertyName == RED_NAME_CONSTEXPR_NOREG( "TintColor" ) ||
			propertyName == RED_NAME_CONSTEXPR_NOREG( "LayerTile" ) ||
			propertyName == RED_NAME_CONSTEXPR_NOREG( "MicroblendTile" ) ||
			propertyName == RED_NAME_CONSTEXPR_NOREG( "MicroblendContrast" ) ||
			propertyName == RED_NAME_CONSTEXPR_NOREG( "MicroblendNormalStrength" ) ||
			propertyName == RED_NAME_CONSTEXPR_NOREG( "LayerOpacity" ) ||
			propertyName == RED_NAME_CONSTEXPR_NOREG( "LayerOffsetU" ) ||
			propertyName == RED_NAME_CONSTEXPR_NOREG( "LayerOffsetV" );
	}

	//////////////////////////////////////////////////////////////////////////////////////////////
	class CustomPropertyEditor_MeshMultilayerMaterialProxyLayerName : public IEditableCustomProperty
	{
		RTTI_DECLARE_TYPE( CustomPropertyEditor_MeshMultilayerMaterialProxyLayerName );

	public:
		virtual const Bool HandlesCustomType( const CName customTypeName, const prv::EditableCollectionRootItem* root ) const override final;
		virtual Bool HasOptions() const override final;
		virtual const red::DynArray<String>& GetOptions() const override final;
		virtual CName GetControlType() const override final;

	private:
		static red::DynArray<red::String> s_noLayers;
	};

	red::DynArray<red::String> CustomPropertyEditor_MeshMultilayerMaterialProxyLayerName::s_noLayers = { "No layers" };

	const Bool CustomPropertyEditor_MeshMultilayerMaterialProxyLayerName::HandlesCustomType( const CName customTypeName, const prv::EditableCollectionRootItem* /*root*/ ) const
	{
		return customTypeName == RED_NAME_CONSTEXPR_NOREG( "MeshMultilayerMaterialLayerName" );
	}

	Bool CustomPropertyEditor_MeshMultilayerMaterialProxyLayerName::HasOptions() const
	{
		return true;
	}

	const red::DynArray<String>& CustomPropertyEditor_MeshMultilayerMaterialProxyLayerName::GetOptions() const
	{
		THandle< MeshMultilayerMaterialProxy > parent = FindParentObject< MeshMultilayerMaterialProxy >();
		return ( parent != nullptr ) ? parent->m_layerNames : s_noLayers;
	}

	CName CustomPropertyEditor_MeshMultilayerMaterialProxyLayerName::GetControlType() const
	{
		return RED_NAME( red::String::Printf( "%hs:Restricted", ::GetTypeName<CName>().AsChar() ) );
	}

} // tools


RTTI_BEGIN_TYPE_IN_NAMESPACE( CustomPropertyEditor_MeshMultilayerMaterialProxyLayerName, tools );
	RTTI_PARENT_TYPE( IEditableCustomProperty );
RTTI_END_TYPE();

