/**
* Copyright (c) 2016-2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "mlmaskViewer.h"

#include "../../backendEngine/include/genericPreviewPanelSharedData.h"
#include "../../../common/redReflection/include/util.h"
#include "../../backendEngine/include/resourceBackend.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( MLMaskViewerResourceEditor, tools );
RTTI_PARENT_TYPE( tools::IResourceEditor );
RTTI_END_TYPE();

namespace tools
{
	MLMaskViewerResourceEditor::MLMaskViewerResourceEditor()
		: m_controller( nullptr )
	{
	}

	MLMaskViewerResourceEditor::~MLMaskViewerResourceEditor()
	{
	}

	Bool MLMaskViewerResourceEditor::Initialize( const THandle< CResource >& resource )
	{
		m_resource = Cast< Multilayer_Mask >( resource );
		m_sharedData = red::CreateSharedPtr< GenericPreviewPanelSharedData>( *this );
		m_controller = red::CreateSharedPtr<MLMaskController>( this );
		m_controller->SetMLMask( m_resource );
		return true;
	}

	void MLMaskViewerResourceEditor::OnPreClose()
	{
		IResourceEditor::OnPreClose();
	}

	const Bool MLMaskViewerResourceEditor::SupportsResourceClass( const rtti::ClassType* resourceClass ) const
	{
		return resourceClass->IsA( ClassID< Multilayer_Mask >() );
	}

	const THandle< CResource > MLMaskViewerResourceEditor::GetResource() const
	{
		return Cast< CResource >( (m_controller->GetMLMask()) );
	}

	void MLMaskViewerResourceEditor::SetupInterop( MLMaskViewerResourceEditor* editor, ::interop::utils::Object& op )
	{
		op.RegisterObject( "mlmaskView", editor->m_controller->CreateInterop() );

		op.RegisterFunctor< void >("increaseLayerBias",
			[editor](interop::IContext& ctx)
			{
				editor->m_controller->ChangeLayerBiasOnAllLayers(1);
			}
		);

		op.RegisterFunctor< void >("decreaseLayerBias",
			[editor](interop::IContext& ctx)
			{
				editor->m_controller->ChangeLayerBiasOnAllLayers(-1);
			}
		);
	}

	void MLMaskViewerResourceEditor::OnPreviewPanelRequest( const String& type, RenderingWindowPtr renderingWindowPtr, PreviewPanelPtr& outPanel )
	{
		m_maskPreviewPanel = red::CreateSharedPtr<MLMaskPreviewPanel>( renderingWindowPtr, m_sharedData );
		m_maskPreviewPanel->SetController( m_controller );
		outPanel = m_maskPreviewPanel;
	}

	void MLMaskViewerResourceEditor::OnPreviewParameterChanged()
	{
		if (m_maskPreviewPanel)
		{
			m_maskPreviewPanel->OnParametersChanged();
		}
	}

	bool MLMaskViewerResourceEditor::Save( red::DynArray< res::ResourcePath >& successfullySaved, red::DynArray< res::ResourcePath >& failedToSave )
	{
		return IResourceEditor::Save( successfullySaved, failedToSave );
	}

} // namespace tools