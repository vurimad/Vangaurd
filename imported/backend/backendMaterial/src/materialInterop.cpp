/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "materialInterop.h"
#include "materialController.h"

#include "../../../common/interop/include/interopNotifier.h"

namespace tools
{

	void MaterialInterop::SetupInterop( MaterialInterop* object, ::interop::utils::Object& op )
	{
		op.RegisterRestrictedValue< String >( "geometry", object->GetSupportedGeometryTypes(),
			[object]( interop::IContext& ctx, String& outValue ) { outValue = object->GetCurrentType(); },
			[object]( interop::IContext& ctx, const String& newValue ) { object->SetCurrentType( newValue ); },
			&object->m_notifier );
	}

	MaterialInterop::MaterialInterop( MaterialController* materialController )
		: m_materialController( materialController )
	{
	}

	MaterialInterop::~MaterialInterop()
	{
	}

	void MaterialInterop::SetGeometryTypes( const red::DynArray<String>& values )
	{
		m_geometryTypes = values;
		if ( m_geometryTypes.Size() > 0 )
		{
			m_currentType = m_geometryTypes[0];
			m_fallbackType = m_currentType;
		}
	}

	const red::DynArray<String>& MaterialInterop::GetSupportedGeometryTypes() const
{
		return m_geometryTypes;
	}

	void tools::MaterialInterop::SetCurrentType( const String& type )
	{
		if ( m_geometryTypes.FindPtr( type ) )
		{
			if ( m_materialController->ChangeGeometry( type ) )
			{
				m_fallbackType = type;
				m_currentType = type;
				return;
			}
		}
		m_currentType = m_fallbackType;
	}

}