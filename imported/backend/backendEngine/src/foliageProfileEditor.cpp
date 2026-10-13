/**
* Copyright (c) 2017-2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "foliageProfileEditor.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( FoliageProfileEditor, tools );
	RTTI_PARENT_TYPE( tools::IResourceEditor );
RTTI_END_TYPE();

namespace tools
{
	FoliageProfileEditor::FoliageProfileEditor()
	{}

	FoliageProfileEditor::~FoliageProfileEditor()
	{}

	Bool FoliageProfileEditor::Initialize( const THandle< CResource >& resource )
	{
		m_foliageProfileResource = Cast< CFoliageProfile >( resource );
		if ( !m_foliageProfileResource )
		{
			return false;
		}
		
		m_serializableNotifier.SetCallback( [this]( const SerializableID& id, const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& targetValue )
			{ OnNotifyPropertyChange( id, path ); } );

		return true;
	}

	void FoliageProfileEditor::OnPreClose()
	{
		IResourceEditor::OnPreClose();
	}

	const bool FoliageProfileEditor::SupportsResourceClass( const rtti::ClassType* resourceClass ) const
	{
		return resourceClass == ClassID< CFoliageProfile >();
	}
	
	const THandle< CResource > FoliageProfileEditor::GetResource() const
	{
		return m_foliageProfileResource;
	}

	void FoliageProfileEditor::SetupInterop( FoliageProfileEditor* editor, ::interop::utils::Object& op )
	{
	}

	void FoliageProfileEditor::OnNotifyPropertyChange( const SerializableID& id, const rtti::AccessPath& path )
	{
		if ( m_foliageProfileResource->GetID() == id )
		{
			m_foliageProfileResource->UpdateRenderResource();
		}
	}
}

