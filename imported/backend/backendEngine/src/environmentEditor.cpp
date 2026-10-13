/**
* Copyright (c) 2017-2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "resourceEditor.h"
#include "environmentEditor.h"
#include "curveEditorSettingsInterop.h"
#include "resourceBackend.h"
#include "../../../common/redReflection/include/resourceMonitor.h"
#include "../../common/worldEnvironment/include/environmentDefinition.h"
#include "../../common/worldEnvironment/include/environmentAreaParameters.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( EnvironmentEditor, tools );
	RTTI_PARENT_TYPE( tools::IResourceEditor );
RTTI_END_TYPE();

namespace tools
{
	EnvironmentEditor::EnvironmentEditor()
	{
		m_curveSettings = RED_NEW( CurveSettingsInterop )( "", Vector2(0.0f, 24.0f) );
		m_curveSettingsObjectPtr = interop::ObjectPtr( m_curveSettings );
	}

	EnvironmentEditor::~EnvironmentEditor()
	{}

	Bool EnvironmentEditor::Initialize( const THandle< CResource >& resource ) 
	{
		m_environmentResource = Cast< world::EnvironmentDefinition >( resource );
		if ( !m_environmentResource )
			return false;
		return true;
	}

	const bool EnvironmentEditor::SupportsResourceClass( const rtti::ClassType* resourceClass ) const
	{
		return resourceClass == ClassID< world::EnvironmentDefinition >();
	}
	
	const THandle< CResource > EnvironmentEditor::GetResource() const
	{
		return m_environmentResource;
	}

	void EnvironmentEditor::SetupInterop( EnvironmentEditor* editor, ::interop::utils::Object& op )
	{

		op.RegisterFunctor< void, String >( "importEnvAreaParams", [editor]( ::interop::IContext& ctx, const String& resourceStringPath )
		{			
			const res::ResourcePath resourcePath = res::ResourcePath::Build( resourceStringPath.AsChar() );

			THandle< CResource > resource{ GResourceLoader->TryAcquiringLoadedResource( resourcePath ) };
			res::ResourceTokenHandle token{ nullptr };

			if (!resource)
			{
				token = GResourceLoader->IssueLoadingRequest( resourcePath );
				resource = token->WaitUntilLoaded();
			}

			THandle<world::EnvironmentAreaParameters> environmentAreaParameters = Cast< world::EnvironmentAreaParameters >( resource );
			const WorldRenderAreaSettings& worldRenderAreaSettings = environmentAreaParameters->GetWorldRenderAreaSettings();
			THandle<world::EnvironmentDefinition> environmentDefinition = Cast<world::EnvironmentDefinition>( editor->GetResource() );
			environmentDefinition->CopyFromAreaSettings( worldRenderAreaSettings );
		} );
	}
}
