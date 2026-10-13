/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "previewPanelSharedData.h"
#include "simpleGridSnapper.h"
#include "resourceEditor.h"
#include "previewPanel.h"
#include "previewEditMode.h"
#include "previewFiltersConfiguration.h"
#include "selectionIterator.h"
#include "selectionState.h"
#include "editActionHistory.h"
#include "gizmoTransformHandler.h"
#include "../../../common/redConfig/include/configVarSystem.h"
#include "../../../common/renderData/include/renderer.h"
#include "../../../common/renderData/include/renderFrameInfo.h"
#include "../../../common/renderDraw/include/fragmentBuilder.h"

namespace tools
{

	PreviewPanelSharedData::PreviewPanelSharedData( IResourceEditor& editor, const TRenderPtr<IRenderScene> renderScene, IRenderScene::Type sceneType )
		: m_resourceEditor( editor )
		, m_editMode( nullptr )
		, m_renderScene( renderScene )
	{
		if ( !m_renderScene && sceneType != IRenderScene::NO_SCENE )
		{
			m_renderScene = CreateRenderScene( sceneType );
		}

		// create the snap control system
		{
			m_snapManager = red::CreateUniquePtr< SnapManager >();

			// create default grid based snapping system
			m_simpleGridSnapper = red::CreateUniquePtr< SimpleGridSnapper >();
			m_snapManager->AttachSolver( m_simpleGridSnapper.Get() );
		}
	}
	
	PreviewPanelSharedData::~PreviewPanelSharedData()
	{
		m_snapManager->DetachSolver( m_simpleGridSnapper.Get() );

		DeleteRenderScene();

		for( auto ptr : m_editModeCache.GetValues() )
		{
			RED_DELETE( ptr );
			ptr = nullptr;
		}
	}

	IEditActionHistory& PreviewPanelSharedData::GetActionHistory()
	{
		return m_resourceEditor.GetAccessHistory();
	}

	const IEditActionHistory& PreviewPanelSharedData::GetActionHistory() const
	{
		return m_resourceEditor.GetAccessHistory();
	}

	// Get resource backend - edition interface
	ResourceBackend& PreviewPanelSharedData::GetResourceBackend()
	{
		return *m_resourceEditor.GetBackend();
	}

	void PreviewPanelSharedData::Update( const UpdateContext& ctx )
	{
		if ( m_editMode )
		{
			m_editMode->OnUpdate( ctx );
		}

		OnUpdateSharedState( ctx );
	}

	void PreviewPanelSharedData::OnUpdateSharedState( const UpdateContext& ctx )
	{
		// implemented in derived classes
	}

	void PreviewPanelSharedData::OnRenderSharedState(CRenderFrameInfo& info, CRenderCameraFrameInfo& cameraInfo)
	{
		m_snapManager->Render( cameraInfo.GetDebugDrawer() );
	}

	void PreviewPanelSharedData::Render( CRenderFrameInfo& info, CRenderCameraFrameInfo& cameraInfo )
	{
		OnRenderSharedState(info, cameraInfo);

		// render the edit mode crap
		if ( m_editMode )
			m_editMode->OnRender(info, cameraInfo);

		// show the active edit mode
		RenderEditModeFrame(cameraInfo.GetDebugDrawer());
	}

	void PreviewPanelSharedData::SendPreviewMessage(const String& message, const void* userData /*= nullptr*/)
	{
		for (auto* panel : m_previewPanels)
			panel->OnEditorMessage(message, userData);
	}

	void PreviewPanelSharedData::NotifyPanelCreated(PreviewPanel* panel)
	{
		m_previewPanels.PushBack( panel );

		m_previewFilterHandler = red::CreateUniquePtr<PreviewFiltersInterop>( panel->GetShortcutManager().Get() );
		m_previewFilterHandler->Bind( GetEditor().GetEditorTypeName() );
	}

	void PreviewPanelSharedData::NotifyPanelDestroyed(PreviewPanel* panel)
	{
		m_previewPanels.Remove(panel);
	}

	CName PreviewPanelSharedData::GetEditModeName() const
	{
		return m_editModeName;
	}

	TRenderPtr<IRenderScene> PreviewPanelSharedData::CreateRenderScene( IRenderScene::Type type )
	{
		return GRender->CreateScene( type );
	}

	void PreviewPanelSharedData::DeleteRenderScene()
	{
		m_renderScene = nullptr;
	}

	Bool PreviewPanelSharedData::ChangeEditMode(CName editModeName)
	{
		// already there
		if (editModeName == m_editModeName)
			return true;

		// get the already created edit mode
		IPreviewEditMode* newEditMode = nullptr;
		m_editModeCache.Find(editModeName, newEditMode);

		// create if not cached
		if (!newEditMode)
		{
			// find new class
			const TEditModeFactory* editModeFactoryFunc = m_editModeClasses.FindPtr(editModeName);
			if (!editModeFactoryFunc)
			{
				RED_LOG_ERROR("Core: Unable to find edit mode class for edit mode '%hs'", editModeName.AsChar());
				return false;
			}

			// create the edit mode
			newEditMode = (*editModeFactoryFunc)();
			if (!newEditMode)
			{
				RED_LOG_ERROR("Core: Unable to create edit mode '%hs'", editModeName.AsChar());
				return false;
			}

			// initialize edit mode
			if (!newEditMode->OnInitialize(SharedFromThis()))
			{
				RED_DELETE( newEditMode );
				RED_LOG_ERROR("Core: Unable to initialize edit mode '%hs'", editModeName.AsChar());
				return false;
			}

			// add to cache so the edit mode does not get recreated
			m_editModeCache.Insert(editModeName, newEditMode);
		}

		// deactivate old mode
		IPreviewEditMode* oldMode = m_editMode;
		CName oldModeName = m_editModeName;
		if (m_editMode)
		{
			//CancelInputHandler();
			//CancelActiveDragDropOperation();

			m_editMode->OnDeactivate();
			m_editMode = nullptr;
			m_editModeName = CName();
			m_editModeChangedNotifier.Notify();

			RED_LOG("Core: Edit mode '%hs' deactivated", m_editModeName.AsChar());
		}

		// activate new mode
		if (newEditMode->OnActivate(oldMode))
		{
			RED_LOG("Core: Edit mode '%hs' activated", editModeName.AsChar());

			// set new edit mode
			m_editMode = newEditMode;
			m_editModeName = editModeName;
			m_editModeChangedNotifier.Notify(m_editModeName);
			return true;
		}
		else if (oldMode)
		{
			if (oldMode->OnActivate(nullptr))
			{
				RED_LOG("Core: Edit mode '%hs' reactivated (mode '%hs' failed to activate)", m_editModeName.AsChar(), editModeName.AsChar());

				// restore old mode
				m_editMode = oldMode;
				m_editModeName = oldModeName;
				m_editModeChangedNotifier.Notify(m_editModeName);
			}
			else
			{
				RED_LOG_ERROR("Core: Edit mode '%hs' failed to reactivate (mode '%hs' failed to activate)", m_editModeName.AsChar(), editModeName.AsChar());

				// no edit mode
				m_editMode = nullptr;
				m_editModeName = CName();
			}
		}

		// error switching to edit mode
		return false;
	}

	Bool PreviewPanelSharedData::GetConfigValue( const ConfigScope& scope, const String& group, const String& key, String& value )
	{
		String fullGroupName;
		if ( scope == ConfigScope::Resource )
		{
			fullGroupName = ConstructResourceRelatedConfigGroupName( group, m_resourceEditor );
		}
		else if ( scope == ConfigScope::Editor )
		{
			fullGroupName = ConstructEditorRelatedConfigGroupName( group, m_resourceEditor );
		}
		else
		{
			fullGroupName = ConstructGlobalEditorConfigGroupName( group );
		}
		return Config::GetConfigSystem().GetValue( fullGroupName.AsChar(), key.AsChar(), value );
	}

	Bool PreviewPanelSharedData::SetConfigValue( const ConfigScope& scope, const String& group, const String& key, const String& value )
	{
		String fullGroupName;
		if ( scope == ConfigScope::Resource )
		{
			fullGroupName = ConstructResourceRelatedConfigGroupName( group, m_resourceEditor );
		}
		else if ( scope == ConfigScope::Editor )
		{
			fullGroupName = ConstructEditorRelatedConfigGroupName( group, m_resourceEditor );
		}
		else
		{
			fullGroupName = ConstructGlobalEditorConfigGroupName( group );
		}
		return Config::GetConfigSystem().SetValue( fullGroupName.AsChar(), key.AsChar(), value );
	}

	void PreviewPanelSharedData::RegisterEditMode(const CName editName, TEditModeFactory factoryFunc)
	{
		RED_FATAL_ASSERT(editName, "Edit mode name should not be empty");
		RED_FATAL_ASSERT(factoryFunc, "Class name should not be empty");

		// set new mapping, override any existing
		m_editModeClasses[editName] = factoryFunc;

		// if it's currently active mode, deactivate it
		if( editName == m_editModeName )
		{
			m_editMode->OnDeactivate();
			m_editMode = nullptr;
			m_editModeName = CName();
			m_editModeChangedNotifier.Notify();

			RED_LOG( "Core: Edit mode '%hs' deactivated", m_editModeName.AsChar() );
		}

		// if the mode with this name was cached, remove it from cache so it gets properly recreated on next activation
		m_editModeCache.Remove( editName );		
	}

	void PreviewPanelSharedData::ClearEditMode( const CName editName )
	{
		RED_FATAL_ASSERT( editName, "Edit mode name should not be empty" );
		// if it's currently active mode, deactivate it
		if( editName == m_editModeName )
		{
			m_editMode->OnDeactivate();
			m_editMode = nullptr;
			m_editModeName = CName();
			m_editModeChangedNotifier.Notify();

			RED_LOG( "Core: Edit mode '%hs' deactivated", m_editModeName.AsChar() );
		}

		// if the mode with this name was cached, remove it from cache so it gets properly recreated on next activation
		m_editModeCache.Remove( editName );
	}

	void PreviewPanelSharedData::RenderEditModeFrame( rend::IDebugDrawer& dd )
	{
		static rend::DebugFilter DF_EditMode("Editor/Window/EditModeInfo");

		if (m_editMode)
		{
			const auto displayName = m_editMode->GetDisplayName();
			if (displayName && *displayName)
			{
				rend::FragmentBuilder builder(dd);

				const auto w = builder.GetScreenWidth();
				const auto h = builder.GetScreenHeight();

				Color color = m_editMode->GetDisplayColor();
				builder.SetColor(color);
				builder.AddFlatRectWH(0, 0, w - 2, 2);
				builder.AddFlatRectWH(0, 0, 2, h - 2);
				builder.AddFlatRectWH(0, h - 2, w - 1, h - 1);
				builder.AddFlatRectWH(w - 2, 0, w - 1, h - 1);

				if (dd.GetMask().Test(DF_EditMode))
				{
					auto font = builder.GetDefaultFont();
					if (font)
					{
						const Int32 tw = 10 + (Int32)builder.MeasureText( displayName ).X;
						const Int32 th = 8 + builder.GetFontLineHeight();

						builder.SetColor(color);
						builder.AddFlatRectWH(w - 2 - tw, 2, tw, th);
						builder.SetColor(Color::WHITE());
						builder.AddFlatText(w - 2 - tw + 5, 2 + th - 4, displayName);
					}
				}
			}
		}
	}

	void PreviewPanelSharedData::SetupInterop( PreviewPanelSharedData* ptr, ::interop::utils::Object& op )
	{
		op.RegisterObject( "snap", ptr->m_snapManager.Get() );
		op.RegisterObject( "grid", ptr->m_simpleGridSnapper.Get() );
		op.RegisterObject( "previewFilter", ptr->m_previewFilterHandler.Get() );

		op.RegisterNotifier( "fileEditCheckout", ptr->m_fileEditCheckout );

		// Edit mode
		op.RegisterValue<String>("editMode",
			/*GET*/	[ptr](interop::IContext& ctx, String& outValue) { outValue = ptr->GetEditModeName().AsChar(); },
			/*SET*/	[ptr](interop::IContext& ctx, const String& newValue)
		{
			if (!ptr->ChangeEditMode(RED_NAME(newValue)))
			{
				ctx.ThrowException("Unable to set selected edit mode");
			}
		});
		
		// Notification about edit mode change
		op.RegisterNotifier("onEditModeChanged", ptr->m_editModeChangedNotifier);

		// Additional stuff from action
		if (ptr->m_editMode)
			ptr->m_editMode->OnSetupInterop(op);
	}

	void PreviewPanelSharedData::OnFileEdited( const res::ResourcePath& path ) const
	{
		m_fileEditCheckout.Notify( path.ToString() );
	}

	void PreviewPanelSharedData::OnResourceEdited( const THandle<CResource>& resource )
	{
		RED_UNUSED( resource );
	}

	Bool PreviewPanelSharedData::MoveSelection( SelectionStatePtr selectionState, const Transform& targetTransform )
	{
		if ( selectionState->IsEmpty() )
		{
			return true;
		}

		EditableProxyPtr proxy = selectionState->Iterate().GetCurrentPtr();
		const Transform proxyInverted = proxy->GetWorldTransform().GetInverse();
		const Vector3& pivot = proxy->GetWorldTransform().GetPosition3();
		const Vector3 translation = targetTransform.GetPosition3() - pivot;

		const TranslationSnapParams snapParams =
		{
			proxy->GetWorldTransform(),
			translation.Normalized(),
			translation.Mag(),
		};

		red::UniquePtr< IEditAction > postMoveAction;
		Transform movement = Transform( translation );
		GetSnapManager().ProcessTranslationalSnap( snapParams, movement, postMoveAction );

		auto action = red::CreateUniquePtr< Action_SetTransform >();
		for ( auto& it = selectionState->Iterate(); it; ++it )
		{
			Transform transform = it->GetWorldTransform();
			transform.SetPosition( transform.GetPosition3() - pivot );
			transform = transform * movement;
			transform.SetPosition( transform.GetPosition3() + pivot );

			action->AddObjectMovement( it.GetCurrentPtr(), it->GetWorldTransform(), transform );
		}

		GetActionHistory().Execute( std::move( action ) );

		if ( postMoveAction )
		{
			GetActionHistory().Execute( std::move( postMoveAction ) );
		}

		return true;
	}

	void PreviewPanelSharedData::OnGainFocus()
	{

	}

	void PreviewPanelSharedData::OnLoseFocus()
	{

	}

} // tools