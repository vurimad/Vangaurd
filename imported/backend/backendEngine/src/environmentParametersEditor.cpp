/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "environmentParametersEditor.h"
#include "../../common/worldEnvironment/include/environmentAreaParameters.h"
#include "curveEditorSettingsInterop.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( EnvironmentParametersEditor, tools );
	RTTI_PARENT_TYPE( tools::IResourceEditor );
RTTI_END_TYPE();

namespace tools
{
	EnvironmentParametersEditor::EnvironmentParametersEditor()
	{
		m_curveSettings = RED_NEW( CurveSettingsInterop )( "", Vector2( 0.0f, 24.0f ) );
		m_curveSettingsObjectPtr = interop::ObjectPtr( m_curveSettings );
	}

	EnvironmentParametersEditor::~EnvironmentParametersEditor()
	{
	}

	Bool EnvironmentParametersEditor::Initialize( const THandle< CResource >& resource )
	{
		m_environmentResource = Cast< world::EnvironmentAreaParameters >( resource );
		if( !m_environmentResource )
			return false;

		return true;
	}

	const bool EnvironmentParametersEditor::SupportsResourceClass( const rtti::ClassType* resourceClass ) const
	{
		return resourceClass == ClassID< world::EnvironmentAreaParameters >();
	}
	
	const THandle< CResource > EnvironmentParametersEditor::GetResource() const
	{
		return m_environmentResource;
	}

	void EnvironmentParametersEditor::SetupInterop( EnvironmentParametersEditor* editor, ::interop::utils::Object& op )
	{
		op.RegisterObject( "curveSettings", editor->m_curveSettingsObjectPtr );
	}
}

