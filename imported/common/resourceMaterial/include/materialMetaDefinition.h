/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../renderData/include/materialFeatureFlags.h"
#include "../../renderData/include/materialModifier.h"
#include "../../renderData/include/materialDataProvider.h"

enum ERenderMaterialType : Uint8;

struct ShaderDefine
{
	RTTI_DECLARE_TYPE( ShaderDefine );

	red::String m_name { rend::PoolMaterials() };
	red::String m_value { rend::PoolMaterials() };

	RED_FORCE_INLINE Bool operator==( const ShaderDefine& other ) const
	{
		return m_name == other.m_name && m_value == other.m_value;
	}
};

struct MaterialPass
{
	RTTI_DECLARE_TYPE( MaterialPass );

	CName							m_stagePassNameRegular;
	CName							m_stagePassNameDiscarded;

#ifndef RED_CONFIGURATION_FINAL
	red::StaticArray< red::DynArray< ShaderDefine >, MSH_MAX >	m_shaderDefines; // defines per shader type
	red::String						m_shaderSourcePath[ MSH_MAX ];
#endif // RED_CONFIGURATION_FINAL

	DepthStencilModeDesc			m_depthStencilMode;
	RasterizerModeDesc				m_rasterizerMode;
	BlendModeDesc					m_blendMode;
	Uint8							m_stencilReadMask;
	Uint8							m_stencilWriteMask;
	Uint8							m_stencilRef;
	Uint8							m_orderIndex;
	Bool							m_enablePixelShader;

	MaterialPass()
#ifndef RED_CONFIGURATION_FINAL
		: m_shaderDefines( MSH_MAX, rend::PoolMaterials()  )
#endif // RED_CONFIGURATION_FINAL
	{
#ifndef RED_CONFIGURATION_FINAL
		Int32 targets = MSH_PixelShader | MSH_VertexShader;
		for ( Int32 target = MSH_First; target < MSH_MAX; ++target )
		{
			if ( targets & target )
			{
				m_shaderSourcePath[ target ] = "default.fx";
			}
		}
#endif // RED_CONFIGURATION_FINAL
	}

	MaterialPass( const CName defaultName )
#ifndef RED_CONFIGURATION_FINAL
		: m_shaderDefines( MSH_MAX, rend::PoolMaterials() )
#endif // RED_CONFIGURATION_FINAL
	{
#ifndef RED_CONFIGURATION_FINAL
		Int32 targets = MSH_PixelShader | MSH_VertexShader;
		for ( Int32 target = MSH_First; target < MSH_MAX; ++target )
		{
			if ( targets & target )
			{
				m_shaderSourcePath[ target ] = String::Printf( "%hs.fx", defaultName.AsChar() );
			}
		}
#endif // RED_CONFIGURATION_FINAL
	}
	
	RED_FORCE_INLINE Bool operator==( const MaterialPass& other ) const
	{
#ifndef RED_CONFIGURATION_FINAL
		for ( Uint32 i = MSH_First; i < MSH_MAX; ++i )
		{
			if ( m_shaderDefines[ i ] != other.m_shaderDefines[ i ] )
			{
				return false;
			}
			if ( m_shaderSourcePath[ i ] != other.m_shaderSourcePath[ i ] )
			{
				return false;
			}
		}
#endif // RED_CONFIGURATION_FINAL

		return m_stagePassNameRegular	== other.m_stagePassNameRegular
			&& m_stagePassNameDiscarded == other.m_stagePassNameDiscarded
			&& m_depthStencilMode		== other.m_depthStencilMode 
			&& m_rasterizerMode			== other.m_rasterizerMode
			&& m_blendMode				== other.m_blendMode
			&& m_stencilReadMask		== other.m_stencilReadMask
			&& m_stencilWriteMask		== other.m_stencilWriteMask
			&& m_stencilRef				== other.m_stencilRef
			&& m_orderIndex				== other.m_orderIndex
			&& m_enablePixelShader		== other.m_enablePixelShader;
	}
};

struct MaterialTechnique
{
	RTTI_DECLARE_TYPE( MaterialTechnique );

	red::DynArray< MaterialPass >	m_passes { rend::PoolMaterials() };
	FeatureFlagsMask				m_featureFlagsEnabledMask;
	Uint32							m_streamsToBind { 0 };
	Bool							m_excludeOnFinal { false };

	RED_FORCE_INLINE Bool operator==( const MaterialTechnique& other ) const
	{
		return m_passes == other.m_passes 
			&& m_featureFlagsEnabledMask == other.m_featureFlagsEnabledMask 
			&& m_streamsToBind == other.m_streamsToBind
			&& m_excludeOnFinal == other.m_excludeOnFinal;
	}
};

struct MaterialMetaDefinition
{
public:
	RESOURCEMATERIAL_API MaterialMetaDefinition();

	RESOURCEMATERIAL_API MaterialMetaDefinition( const CName name );

public:
	typedef red::HashMap< red::String, MaterialTechnique >      TechniquesLibrary;
	
	struct Param
	{
		Float										m_min;
		Float										m_max;
		IMaterialDataProviderDesc::EParameterType	m_type;
		Uint8										m_register;
		Bool										m_hidden;
		
		Bool										m_forceNoIndirection;						// valid only for textures
		GpuWrapApi::eTextureGroup					m_textureGroup;								// valid only for textures
		red::String									m_textureDataType{ rend::PoolMaterials() };	// valid only for textures

		red::String									m_name{ rend::PoolMaterials() };
		red::String									m_tooltip{ rend::PoolMaterials() };
		red::String									m_customEditorName{ rend::PoolMaterials() };

		Param()
		{
			red::Memzero( this, sizeof( Param ) );
		}
	};

	RESOURCEMATERIAL_API Bool ContainsParameter( const red::String& name, IMaterialDataProviderDesc::EParameterType type, Int32 target ) const;
	RESOURCEMATERIAL_API Bool ContainsUsedParameter( const red::String& name, Int32 target ) const;
	RESOURCEMATERIAL_API Bool ContainsParameterWithMatchingNameButNotType( const red::String& name, IMaterialDataProviderDesc::EParameterType type, Int32 target, IMaterialDataProviderDesc::EParameterType& outType ) const;

	RED_FORCE_INLINE const String GetDefaultShaderPath() const
	{
		RED_ASSERT( !m_name.Empty() );
		return String::Printf(rend::PoolRenderingCore(), "%hs.fx", m_name.AsChar() );
	}

	CName								m_name;
	TechniquesLibrary                   m_techniquesLibrary{ rend::PoolMaterials() };
	red::StaticArray< red::DynArray< Param >, MSH_MAX > m_parameters;
	red::StaticArray< TSamplerStateInfoArray, MSH_MAX >	m_samplerStates;
	red::DynArray< MaterialTechnique >	m_techniques{ rend::PoolMaterials() };
	MaterialModifierDescArray			m_globalModifiers{ rend::PoolMaterials() };
	EMaterialPriority					m_materialPriority;
	ERenderMaterialType					m_materialType;
	Bool								m_isFallback{ false };

	Uint64								m_crc{0};
	Bool                                m_useShaderModel51{false};
};

typedef red::DynArray< MaterialTechnique > MaterialTechniqueArray;

//////////////////////////////////////////////////////////////////////////
