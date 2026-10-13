/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "materialMetaDefinition.h"
#include "../../renderData/include/renderPublicEnums.h"

//////////////////////////////////////////////////////////////////////////

RTTI_BEGIN_TYPE( ShaderDefine );
{
	RTTI_PROPERTY( m_name ).readOnly();
	RTTI_PROPERTY( m_value ).readOnly();
}
RTTI_END_TYPE();

RTTI_BEGIN_TYPE( MaterialPass );
{
#ifndef RED_CONFIGURATION_FINAL
	RTTI_PROPERTY( m_shaderDefines ).readOnly().notCooked();
#endif // RED_CONFIGURATION_FINAL
	RTTI_PROPERTY( m_stagePassNameRegular ).readOnly();
	RTTI_PROPERTY( m_stagePassNameDiscarded ).readOnly();
#ifndef RED_CONFIGURATION_FINAL
	RTTI_PROPERTY( m_shaderSourcePath ).readOnly().notCooked();
#endif // RED_CONFIGURATION_FINAL
	RTTI_PROPERTY( m_depthStencilMode ).readOnly();
	RTTI_PROPERTY( m_rasterizerMode ).readOnly();
	RTTI_PROPERTY( m_blendMode ).readOnly();
	RTTI_PROPERTY( m_stencilReadMask ).readOnly();
	RTTI_PROPERTY( m_stencilWriteMask ).readOnly();
	RTTI_PROPERTY( m_stencilRef ).readOnly();
	RTTI_PROPERTY( m_orderIndex ).readOnly();
	RTTI_PROPERTY( m_enablePixelShader ).readOnly();
}
RTTI_END_TYPE()

RTTI_BEGIN_TYPE( MaterialTechnique );
{
	RTTI_PROPERTY( m_passes ).readOnly();
	RTTI_PROPERTY( m_featureFlagsEnabledMask ).readOnly();
	RTTI_PROPERTY( m_streamsToBind ).readOnly();
}
RTTI_END_TYPE();

//////////////////////////////////////////////////////////////////////////

MaterialMetaDefinition::MaterialMetaDefinition()
 : m_materialPriority( EMaterialPriority::EMP_Normal )
 , m_materialType( ERenderMaterialType::RMT_Standard )
{}

MaterialMetaDefinition::MaterialMetaDefinition( const CName name )
	: m_name( name )
	, m_useShaderModel51( false )
	, m_parameters( MSH_MAX, rend::PoolMaterials() )
	, m_samplerStates( MSH_MAX, rend::PoolMaterials() )
	, m_materialPriority( EMaterialPriority::EMP_Normal )
	, m_materialType( ERenderMaterialType::RMT_Standard )
{}

Bool MaterialMetaDefinition::ContainsParameter( const red::String& name, IMaterialDataProviderDesc::EParameterType type, Int32 target ) const
{
	for ( const Param& param : m_parameters[ target ] )
	{
		if ( param.m_name == name && param.m_type == type )
		{
			return true;
		}
	}
	return false;
}

Bool MaterialMetaDefinition::ContainsUsedParameter( const red::String& name, Int32 target ) const
{
	for ( const Param& param : m_parameters[ target ] )
	{
		if ( param.m_name == name )
		{
			return true;
		}
	}
	return false;
}

Bool MaterialMetaDefinition::ContainsParameterWithMatchingNameButNotType(const red::String& name, IMaterialDataProviderDesc::EParameterType type, Int32 target, IMaterialDataProviderDesc::EParameterType& outType) const
{
	for (const Param& param : m_parameters[target])
	{
		if (param.m_name == name && param.m_type != type)
		{
			outType = param.m_type;
			return true;
		}
	}

	return false;
}
