/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "helperObject.h"
#include "objectHandler.h"

#include "../../../common/redReflection/include/resource.h"

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( IHelperObject, tools );
	RTTI_PARENT_TYPE( tools::IObjectResolver );
RTTI_END_TYPE();

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( IHelperObjectSubobject, tools );
	RTTI_PARENT_TYPE( ISerializable );
RTTI_END_TYPE();


namespace Config
{
	TConfigVar< Color > cvHelperObjectDefaultColor( "Developer/HelperObject", "DefaultColor", Color( 160, 40, 40 ), eConsoleVarFlag_Save );
	TConfigVar< Color > cvHelperObjectSelectedColor( "Developer/HelperObject", "SelectedColor", Color( 193, 184, 54 ), eConsoleVarFlag_Save );
	TConfigVar< Color > cvHelperObjectHoveredColor( "Developer/HelperObject", "HoveredColor", Color( 106, 57, 124 ), eConsoleVarFlag_Save );
}


namespace tools
{
	
	IHelperObject::IHelperObject( const EditableProxyPtr& parentObjectProxy )
		: m_parentObject( parentObjectProxy->GetSerializable() )
		, m_proxy( parentObjectProxy )
		, m_worldTransform( parentObjectProxy->GetWorldTransform() )
		, m_scale( parentObjectProxy->GetScale() )
		, m_parentPath( parentObjectProxy->GetEditorObjectIDPath() )
		, m_previewPanelSharedData( nullptr )
	{
		RED_FATAL_ASSERT( parentObjectProxy );
		RED_FATAL_ASSERT( m_parentObject );
	}

	void IHelperObject::BindSharedData( PreviewPanelSharedData* sharedData )
	{
		m_previewPanelSharedData = sharedData;
	}

	Color IHelperObject::GetColor( ColorMode mode ) const
	{
		if ( mode == ColorMode::Selected )
		{
			return Config::cvHelperObjectSelectedColor.Get();
		}
		else if ( mode == ColorMode::Hover )
		{
			return Config::cvHelperObjectHoveredColor.Get();
		}

		return Config::cvHelperObjectDefaultColor.Get();
	}

	const String& IHelperObject::GetName() const
	{
		const static String name( "none" );
		return name;
	}

	Bool IHelperObject::RegisterInteropObject( ::interop::utils::Object& op )
	{
		return false;
	}

	const EditorObjectID& IHelperObject::GetParentEditorObjectID() const
	{
		return m_parentPath.GetFinalChildID();
	}

} // tools
