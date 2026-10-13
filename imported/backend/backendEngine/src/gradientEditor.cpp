/**
* Copyright (c) 2017-2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "gradientEditor.h"

//////////////////////////////////////////////////////////////////////////

RTTI_BEGIN_TYPE_IN_NAMESPACE( GradientEditor, tools );
	RTTI_PARENT_TYPE( tools::IResourceEditor );
RTTI_END_TYPE();

namespace tools
{
	GradientEditor::GradientEditor()
	{}

	GradientEditor::~GradientEditor()
	{}

	Bool GradientEditor::Initialize( const THandle< CResource >& resource )
	{
		m_gradientResource = Cast< CGradient >( resource );
		if ( !m_gradientResource )
		{
			return false;
		}

		m_serializableNotifier.SetCallback( [ this ]( const SerializableID& id, const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& targetValue )
		{ OnNotifyPropertyChange( id, path ); } );

		return true;
	}

	void GradientEditor::OnPreClose()
	{
		IResourceEditor::OnPreClose();
	}

	const bool GradientEditor::SupportsResourceClass( const rtti::ClassType* resourceClass ) const
	{
		return resourceClass == ClassID< CGradient >();
	}

	const THandle< CResource > GradientEditor::GetResource() const
	{
		return m_gradientResource;
	}

	void GradientEditor::SetupInterop( GradientEditor* editor, ::interop::utils::Object& op )
	{
	}

	void GradientEditor::OnNotifyPropertyChange( const SerializableID& id, const rtti::AccessPath& path )
	{
		if ( m_gradientResource->GetID() == id )
		{
			m_gradientResource->UpdateRenderResource();
		}
	}
}

