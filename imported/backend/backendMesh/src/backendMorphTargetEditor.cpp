/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "backendMorphTargetEditor.h"
#include "backendMorphTargetPreviewPanel.h"
#include "../../backendEngine/include/editAction.h"
#include "../../backendEngine/include/genericPreviewPanel.h"
#include "../../../common/resourceMesh/include/morphTargetMesh.h"
#include "../../../common/core/include/message.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( MorphTargetResourceEditor, tools );
	RTTI_PARENT_TYPE( tools::IResourceEditor );
RTTI_END_TYPE();

using namespace MorphTargets;

namespace tools
{
	MorphTargetResourceEditor::MorphTargetResourceEditor()
		: IResourceEditor()
		, m_previewTargets( red::PoolBackend() )
	{
	}

	MorphTargetResourceEditor::~MorphTargetResourceEditor() = default;

	Bool MorphTargetResourceEditor::Initialize( const THandle< CResource >& resource )
	{
		m_resource			= Cast< MorphTargetMesh >( resource );
		m_previewSharedData = red::CreateSharedPtr< GenericPreviewPanelSharedData >( *this );
		m_appearance		= m_resource->GetAppearance();
		
		CachePreviewTargets();

		return static_cast< Bool >( m_resource );
	}

	const Bool MorphTargetResourceEditor::SupportsResourceClass( const rtti::ClassType* resourceClass ) const
	{
		return resourceClass->IsA( ClassID< MorphTargetMesh >() );
	}

	const THandle< CResource > MorphTargetResourceEditor::GetResource() const
	{
		return m_resource;
	}

	void MorphTargetResourceEditor::OnPreviewPanelRequest( const String& type, tools::RenderingWindowPtr renderingWindowPtr, tools::PreviewPanelPtr& outPanel )
	{
		m_previewPanel = red::CreateSharedPtr< MorphTargetPreviewPanel >( renderingWindowPtr, m_previewSharedData );
		m_previewPanel->SetupRendering( m_resource );
		outPanel = m_previewPanel;
	}

	void MorphTargetResourceEditor::OnActionExecute( const IEditAction& action )
	{
		IResourceEditor::OnActionExecute( action );
		if ( action.IsModification() )
		{
			CachePreviewTargets();

			const Bool appearanceChanged = m_appearance != m_resource->GetAppearance();
			m_appearance = m_resource->GetAppearance();

			OnResourceModified( appearanceChanged );
		}
	}

	void MorphTargetResourceEditor::OnUpdate( const UpdateContext& ctx )
	{
		const auto& targets = m_resource->GetTargets();

		RED_ASSERT( targets.Size() == m_previewTargets.Size(), "Size mismatch, targets: %u, previewTargets: %u", targets.Size(), m_previewTargets.Size() );

		red::DynArray< MorphTargetWeight > weights{ red::PoolBackend() };
		weights.Reserve( targets.Size() );

		for ( Uint32 i = 0; i < targets.Size(); ++i )
		{
			Float debugWeight = targets[ i ].GetDebugWeight();
			if ( targets[ i ].IsPreviewEnabled() )
			{
				m_previewTargets[ i ] += ctx.m_timeDelta;
				debugWeight = Clamp( MSin( m_previewTargets[ i ] ) * 0.6f + 0.5f, 0.0f, 1.0f );
			}
			else
			{
				m_previewTargets[ i ] = debugWeight;
			}

			if ( debugWeight > 0.0f )
			{
				auto& weight			= weights.EmplaceBack();
				weight.m_targetIndex	= i;
				weight.m_weight			= debugWeight;
			}
		}

		if ( !weights.Empty() )
		{
			m_previewPanel->SetMorphWeights( weights );
		}
	}

	Bool MorphTargetResourceEditor::CachePreviewTargets()
	{
		const Bool sizeChanged = m_previewTargets.Size() != m_resource->GetTargets().Size();
		m_previewTargets.Resize( m_resource->GetTargets().Size() );
		return sizeChanged;
	}

	// Todo[PB]: If this editor grows, it should be definitely moved out of this class
	void MorphTargetResourceEditor::OnResourceModified( Bool shouldRecreateRenderProxy /*=false*/ )
	{
		if ( shouldRecreateRenderProxy )
		{
			m_previewPanel->SetupRendering( m_resource );
		}
	}

	void MorphTargetResourceEditor::SetupInterop( MorphTargetResourceEditor* ptr, ::interop::utils::Object& op )
	{
		op.RegisterFunctor< void >( "rebuild", [ptr]( interop::IContext& ctx )
		{
			if ( THandle< MorphTargetMesh > morphTargetMesh = Cast< MorphTargetMesh >( ptr->GetResource() ) )
			{
				if ( morphTargetMesh->Rebuild() )
				{
					morphTargetMesh->MarkModified();
					RED_REPORT_SUCCESS( RED_LOCATION( morphTargetMesh->GetPath() ), "Successfully rebuilt morph target!" );
				}
				else
				{
					RED_REPORT_ERROR( RED_LOCATION( morphTargetMesh->GetPath() ), "Failed to rebuild morph target, check logs and data errors." );
				}				
			}
		});

		ptr->m_previewSharedData->ExposeInterop( op );
	}
}

