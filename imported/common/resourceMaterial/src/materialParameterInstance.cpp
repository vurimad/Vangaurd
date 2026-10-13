/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "materialParameterInstance.h"
#include "materialParameter.h"
#include "materialTemplate.h"


RTTI_BEGIN_TYPE(MaterialParameterInstance);
{
	RTTI_PROPERTY(m_name);
	RTTI_PROPERTY(m_data);
}
RTTI_END_TYPE();


MaterialParameterInstance::MaterialParameterInstance()
{
}

MaterialParameterInstance::MaterialParameterInstance( const CName& paramName, const rtti::IType* dataType, const void* data /*= nullptr*/ )
	: m_name( paramName )
	, m_data( dataType, data )
{
}

MaterialParameterInstance::MaterialParameterInstance( const CName& paramName, const CName& dataTypeName, const void* data /*= nullptr*/ )
	: m_name( paramName )
	, m_data( dataTypeName, data )
{
	RED_WARNING( m_data.GetRTTIType() != nullptr, "MaterialParameterInstance %hs has invalid dataType %hs", paramName.AsChar(), dataTypeName.AsChar() );
}


void MaterialParameterInstance::SetData( const rtti::IType* dataType, const void* data )
{
	m_data.Set( dataType, data );
}


Bool MaterialParameterInstance::SerializeDirectly( IFile& file )
{
	return m_data.SerializeData( file );
}
