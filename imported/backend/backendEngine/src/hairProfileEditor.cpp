/**
* Copyright (c) 2017-2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "hairProfileEditor.h"

//////////////////////////////////////////////////////////////////////////

RTTI_BEGIN_TYPE_IN_NAMESPACE( HairProfileEditor, tools );
	RTTI_PARENT_TYPE( tools::IResourceEditor );
RTTI_END_TYPE();

namespace tools
{
	HairProfileEditor::HairProfileEditor()
	{}

	HairProfileEditor::~HairProfileEditor()
	{}

	Bool HairProfileEditor::Initialize( const THandle< CResource >& resource )
	{
		m_hairProfileResource = Cast< CHairProfile >( resource );
		if ( !m_hairProfileResource )
		{
			return false;
		}
		
		m_serializableNotifier.SetCallback( [this]( const SerializableID& id, const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& targetValue )
			{ OnNotifyPropertyChange( id, path ); } );

		return true;
	}

	void HairProfileEditor::OnPreClose()
	{
		IResourceEditor::OnPreClose();
	}

	const bool HairProfileEditor::SupportsResourceClass( const rtti::ClassType* resourceClass ) const
	{
		return resourceClass == ClassID< CHairProfile >();
	}
	
	const THandle< CResource > HairProfileEditor::GetResource() const
	{
		return m_hairProfileResource;
	}

	void HairProfileEditor::SetupInterop( HairProfileEditor* editor, ::interop::utils::Object& op )
	{
	}

	void HairProfileEditor::OnNotifyPropertyChange( const SerializableID& id, const rtti::AccessPath& path )
	{
		if ( m_hairProfileResource->GetID() == id )
		{
			m_hairProfileResource->UpdateRenderResource();
		}
	}
}

