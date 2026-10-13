/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "materialLibraryEditor.h"
#include "../../backendEngine/include/genericPreviewPanelSharedData.h"
#include "../../backendEngine/include/previewPanel.h"
#include "../../../common/resourceMaterial/include/materialLayerLibrary.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE(MaterialLibraryEditor, tools);
RTTI_PARENT_TYPE(tools::IResourceEditor);
RTTI_END_TYPE();

namespace tools
{
	MaterialLibraryEditor::MaterialLibraryEditor()
	{}

	MaterialLibraryEditor::~MaterialLibraryEditor()
	{}

	bool MaterialLibraryEditor::Initialize(const THandle< CResource >& resource)
	{
		m_materialLib = Cast< CMaterialLayerLibrary >( resource );

		m_sharedData = red::CreateSharedPtr< GenericPreviewPanelSharedData >( *this );

		MaterialControllerFactory factory;
		m_materialController = factory.CreateMaterialController( resource, GetBackend(), &GetAccessHistory(), GetSharedSelection() );
		if ( !m_materialController )
			return false;


		return static_cast< Bool >( m_materialLib );
	}
	
	const bool MaterialLibraryEditor::SupportsResourceClass(const rtti::ClassType* resourceClass) const 
	{
		return resourceClass == ClassID< CMaterialLayerLibrary >();
	}

	const THandle< CResource > MaterialLibraryEditor::GetResource() const
	{
		return m_materialLib;
	}

	void MaterialLibraryEditor::SetupInterop( MaterialLibraryEditor* editor, ::interop::utils::Object& op )
	{
		if ( editor->m_materialController )
		{
			op.RegisterObject( "materialController", editor->m_materialController->CreateInterop() );
		}
	}

	void MaterialLibraryEditor::OnPreviewPanelRequest( const String& type, RenderingWindowPtr renderingWindowPtr, PreviewPanelPtr& outPanel )
	{
		auto panel = red::CreateSharedPtr< MaterialPreviewPanel >( renderingWindowPtr, m_sharedData );
		m_materialController->SetPreview( panel );
		outPanel = panel;
	}
}

