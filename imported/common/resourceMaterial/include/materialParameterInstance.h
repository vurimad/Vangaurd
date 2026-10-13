/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redReflection/include/variant.h"

#define DEFAULT_MATERIAL_PARAMETER_INSTANCE_ALIGNMENT 16

/// Instance of material parameter
class RESOURCEMATERIAL_API MaterialParameterInstance
{
	RTTI_DECLARE_TYPE(MaterialParameterInstance);

protected:
	CName				m_name;			//!< Name of the parameter
	rtti::Variant		m_data;

public:
	MaterialParameterInstance();
	MaterialParameterInstance( const CName& paramName, const rtti::IType* dataType, const void* data = nullptr );
	MaterialParameterInstance( const CName& paramName, const CName& dataTypeName, const void* data = nullptr );

	RED_INLINE const CName& GetName() const { return m_name; }
	RED_INLINE void SetName( const CName& val ) { m_name = val; }

	RED_INLINE const rtti::IType* GetType() const { return m_data.GetRTTIType(); }

	RED_INLINE const void* GetData() const { return m_data.GetData(); }

	void SetData( const rtti::IType* dataType, const void* data );

public:
	// Serialize parameter manually. Only serializes the value, assumes the correct name and type are already set.
	// #TODO: Can probably phase this out. Material serialization does the parameter instances manually, instead of
	// through RTTI properties, but with this being nicely RTTI-friendly now, that could be changed. Leaving this here
	// so we don't need to do a huge resave right now.
	Bool SerializeDirectly( IFile& file );
};
