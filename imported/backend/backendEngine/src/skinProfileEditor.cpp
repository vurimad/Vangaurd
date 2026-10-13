/**
* Copyright (c) 2017-2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "skinProfileEditor.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( SkinProfileEditor, tools );
	RTTI_PARENT_TYPE( tools::IResourceEditor );
RTTI_END_TYPE();

namespace tools
{
	SkinProfileEditor::SkinProfileEditor()
	{}

	SkinProfileEditor::~SkinProfileEditor()
	{}

	Bool SkinProfileEditor::Initialize( const THandle< CResource >& resource )
	{
		m_skinProfileResource = Cast< CSkinProfile >( resource );
		if ( !m_skinProfileResource )
		{
			return false;
		}
		
		m_serializableNotifier.SetCallback( [this]( const SerializableID& id, const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& targetValue )
			{ OnNotifyPropertyChange( id, path ); } );

		return true;
	}

	void SkinProfileEditor::OnPreClose()
	{
		IResourceEditor::OnPreClose();
	}

	const bool SkinProfileEditor::SupportsResourceClass( const rtti::ClassType* resourceClass ) const
	{
		return resourceClass == ClassID< CSkinProfile >();
	}
	
	const THandle< CResource > SkinProfileEditor::GetResource() const
	{
		return m_skinProfileResource;
	}

	void SkinProfileEditor::SetupInterop( SkinProfileEditor* editor, ::interop::utils::Object& op )
	{
	}

	void SkinProfileEditor::OnNotifyPropertyChange( const SerializableID& id, const rtti::AccessPath& path )
	{
		if ( m_skinProfileResource->GetID() == id )
		{
			m_skinProfileResource->UpdateRenderResource();
		}
	}
}

