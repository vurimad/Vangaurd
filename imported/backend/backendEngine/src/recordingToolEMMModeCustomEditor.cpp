/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "recordingToolEMMModeCustomEditor.h"
#include "../../../common/renderData/include/renderPublicEnums.h"

namespace tools
{
	static EEnvManagerModifier modifiers[] =
	{
		EMM_None,
		EMM_GlobalIllumination,
		EMM_SurfaceBaseColor,
		EMM_SurfaceMetalness,
		EMM_SurfaceEmissive,
		EMM_SurfaceNormalsWorldSpace,
		EMM_ConeAOAngle,
		EMM_Depth,
		EMM_DiffuseLight,
		EMM_PureGreyscaleView,
		EMM_SurfaceMaterialID,
		EMM_SurfaceObjectID,
	};

	CustomPropertyEditor_EMMMode::CustomPropertyEditor_EMMMode()
		: m_emmModes( red::PoolBackend() )
	{
		auto rttiClass = static_cast< const rtti::EnumType* >( ::GetTypeObject< EEnvManagerModifier >() );

		for(auto mod : modifiers)
		{
			CName name;
			RED_VERIFY( rttiClass->FindName( mod, name ) );
			m_emmModes.PushBack( name.AsChar() );
		}
	}

	const Bool CustomPropertyEditor_EMMMode::HandlesCustomType( const CName customTypeName, const prv::EditableCollectionRootItem* /*root*/ ) const
	{
		return customTypeName == RED_NAME_CONSTEXPR_NOREG( "RecordingToolEMMMode" );
	}

	Bool CustomPropertyEditor_EMMMode::HasOptions() const
	{
		return true;
	}

	const red::DynArray< String >& CustomPropertyEditor_EMMMode::GetOptions() const
	{
		return m_emmModes;
	}

	CName CustomPropertyEditor_EMMMode::GetControlType() const
	{
		return RED_NAME( ::GetTypeName< EEnvManagerModifier >() );
	}

	//////////////////////////////////////////////////////////////////////////////////////////////

} // tools

RTTI_BEGIN_TYPE_IN_NAMESPACE( CustomPropertyEditor_EMMMode, tools );
{
	RTTI_PARENT_TYPE( IEditableCustomProperty );
}
RTTI_END_TYPE();

