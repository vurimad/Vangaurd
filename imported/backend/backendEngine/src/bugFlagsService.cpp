#include "build.h"
#include "bugFlagsService.h"

#include "treeInterop.h"
#include "bugFlagsList.h"
#include "resourceBackend.h"
#include "resourceEditor.h"
#include "previewPanel.h"
#include "previewPanelSharedData.h"

#include "../../common/world/include/worldRuntimeScene.h"
#include "renderingWindowCamera.h"
#include "../../common/issueReporter/include/jiraService.h"
#include "../../common/renderData/include/captureParameters.h"
#include "../../common/redFileSystem/include/fileSys.h"

#include <shellapi.h>
#include "../../common/redReflection/include/version.h"
#include "../../common/redReflection/include/rttiPathParser.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( BugFlagItemContext, tools );
	RTTI_PARENT_TYPE( ISerializable );
RTTI_END_TYPE();

namespace Config
{
	TConfigVar< String > cvDefaultProjectKey( "Editor/BugFlags", "DefaultProject", "CYB", eConsoleVarFlag_Save );
	TConfigVar< Bool > cvJiraIntegrationEnabled( "Editor/BugFlags", "JiraIntegration", true, eConsoleVarFlag_Save );
}

namespace tools
{
	Bool GenerateScreenshotPath( red::AbsolutePath& outPath )
	{
		red::DateTime dateTime;
		red::Clock::GetInstance().GetLocalTime( dateTime );
		red::String dateTimeString = String::Printf( "%04d_%02d_%02d__%02d_%02d_%02d.png",
			dateTime.GetYear(), dateTime.GetMonth() + 1, dateTime.GetDay() + 1,
			dateTime.GetHour(), dateTime.GetMinute(), dateTime.GetSecond() );

		red::AbsolutePath absViewPath = GFileManager->GetCacheDirectory().AddFilePath( dateTimeString );
		if ( GFileManager->CreatePath( absViewPath ) )
		{
			outPath = absViewPath;
			return true;
		}
		return false;
	}

	void ReportError( const res::ResourcePath editorResourcePath, red::DynArray<String> errorMessages )
	{
		if ( errorMessages.Empty() )
		{
			RED_REPORT_ERROR( RED_LOCATION( editorResourcePath ), "BugFlagsService: Unable to add jira issue. Unspecified error" );
		}
		else
		{
			String errors;
			for ( const String& message : errorMessages )
			{
				errors += message;
			}
			RED_REPORT_ERROR( RED_LOCATION( editorResourcePath ), "BugFlagsService: Unable to add jira issue. %hs", errors.AsChar() );
		}
	}

	BugFlagItemContext::BugFlagItemContext( const PreviewPanelPtr& previewPanel )
		: m_panel( previewPanel )
	{
	}

	const PreviewPanelPtr& BugFlagItemContext::GetPreviewPanel() const
	{
		return m_panel;
	}

	String BugFlagItemContext::GetCurrentCameraLocation() const
	{
		return ToStringDirect( m_panel->GetCamera()->GetCameraLocation() );
	}

	void BugFlagItemContext::TakeScreenshot( red::AbsolutePath& outPath )
	{
		if ( GenerateScreenshotPath( outPath ) )
		{
			rend::SingleScreenShotData data;
			data.m_requested = true;
			data.m_mode = rend::ScreenshotMode::NORMAL;
			data.m_outputPath = outPath;
			m_panel->TakeFullScreenshot( data, true );
		}
	}

	BugFlagsService::BugFlagsService( ResourceBackend* backend )
		: interop::utils::Object( "BugFlagsService" )
		, m_backend( backend )
	{
// 		m_issuesTree = red::CreateUniquePtr< BugFlagsTree >();
// 		m_issuesTreeInterop = red::CreateSharedPtr< TreeContainerInterop >( m_issuesTree.Get(), nullptr );
//
// 		m_issuesTree->NodeActivated += { [ this ]( const TTreeNodeIDs& activated, const TTreeNodeIDs& deactivated )
// 		{
// 			if ( !activated.Empty() )
// 			{
// 				const TreeNodeID activatedNodeID = activated.Back();
// 				ITreeNode* activatedNode = m_issuesTree->GetNode( activatedNodeID );
// 				if ( activatedNode->GetClass()->IsA< BugFlagsIssueNode >() )
// 				{
// 					BugFlagsIssueNode* issueNode = static_cast<BugFlagsIssueNode*>( activatedNode );
// 					EditIssue( issueNode->GetJiraIssue() );
// 				}
// 			}
// 		} };

		SetupInterop();

		m_backend->AttachListener( this );
	}

	BugFlagsService::~BugFlagsService()
	{
		m_backend->DetachListener( this );

		red::DynArray< ResourceEditorPtr > editors;
		m_backend->GetResourceEditors( editors );
		for ( const ResourceEditorPtr& editor : editors )
		{
			editor->DetachListener( this );
		}
	}

	void BugFlagsService::ToggleAttachEditor( interop::IContext& ctx, const String& editorName )
	{
		auto it = m_attachedEditors.Find( editorName );
		if ( it == m_attachedEditors.End() )
		{
			ctx.ThrowException( "There's no editor with name: '%hs'", editorName.AsChar() );
			return;
		}

		AttachedEditor* attachedEditor = it.Value().Get();
		attachedEditor->SetAttached( !attachedEditor->IsAttached() );
	}

	void BugFlagsService::JumpToIssue( const Transform& transform )
	{
// 		for ( auto& element : m_editorAndPanelIDToScene )
// 		{
// 			const std::pair< String, Uint32 >& editorNameAndPanelID = element.Key();
// 			ResourceEditorPtr editor = m_backend->GetResourceEditorByName( editorNameAndPanelID.first );
// 			PreviewPanelPtr previewPanel = editor->GetPreviewPanel( editorNameAndPanelID.second );
//
// 			const Box issueArea( transform.GetPosition(), 1 );
//
// 			RenderingWindowCamera* camera = previewPanel->GetCamera();
// 			camera->SetFocalArea( issueArea );
// 			camera->FocusOn( false );
// 		}
	}

	void BugFlagsService::EditIssue( const JiraIssue& issue )
	{
// 		m_jiraIssueRequest = CreateHandle< JiraEditIssueBody >( issue );
// 		m_jiraIssueCollection->SetObject( m_jiraIssueRequest.Get() );
// 		m_jiraIssueCreationNotifier.Notify();
	}

	void BugFlagsService::OnEditorOpened( const String& editorName )
	{
		ResourceEditorPtr editor = m_backend->GetResourceEditorByName( editorName );
		if ( editor )
		{
			editor->AttachListener( this );
		}
	}

	void BugFlagsService::OnEditorClosed( const String& editorName )
	{
		if ( m_attachedEditors.Remove( editorName ).IsSuccessful() )
		{
			m_availableEditorsChanged.Notify();
		}
	}

	void BugFlagsService::OnAllEditorsClosed()
	{
		m_attachedEditors.Clear();
		m_availableEditorsChanged.Notify();
	}

	void BugFlagsService::OnPreviewPanelCreated( const String& editorName, Uint32 panelID )
	{
		if ( !Config::cvJiraIntegrationEnabled.Get() )
		{
			return;
		}

		ResourceEditorPtr editor = m_backend->GetResourceEditorByName( editorName );
		if ( editor )
		{
			const PreviewPanelPtr& previewPanel = editor->GetPreviewPanel( panelID );
			if ( const red::SharedPtr< world::RuntimeScene > scene = previewPanel->GetRuntimeScene() )
			{
				m_attachedEditors.Insert( editorName, red::CreateUniquePtr< AttachedEditor >( editor, previewPanel, scene ) );
				m_availableEditorsChanged.Notify();
			}
		}
	}

	void BugFlagsService::OnPreviewPanelDestroyed( const String& editorName, Uint32 panelID )
	{
		auto it = m_attachedEditors.Find( editorName );
		if ( it != m_attachedEditors.End() )
		{
			const AttachedEditor* attachedEditor = it.Value().Get();
			if ( attachedEditor->GetPreviewPanel()->GetID() == panelID )
			{
				m_attachedEditors.Remove( editorName );
				m_availableEditorsChanged.Notify();
			}
		}
	}

	void BugFlagsService::OnAllPreviewPanelsDestroyed( const String& editorName )
	{
		if ( m_attachedEditors.Remove( editorName ).IsSuccessful() )
		{
			m_availableEditorsChanged.Notify();
		}
	}

	void BugFlagsService::OnBugFlagAdded( const String& editorName, const res::ResourcePath& resourcePath, const String& cameraLocation, const Vector3& flagPosition )
	{
		auto it = m_attachedEditors.Find( editorName );
		if ( it != m_attachedEditors.End() )
		{
			AttachedEditor* attachedEditor = it.Value().Get();
			attachedEditor->SetAttached( true );
			attachedEditor->OnBugFlagAdded( resourcePath, cameraLocation, flagPosition );
		}
	}

	void BugFlagsService::SetupInterop()
	{
//		RegisterObject( "tree", interop::ObjectPtr( m_issuesTreeInterop ) );

		RegisterMap( "attachedEditors", [ this ]( interop::IContext& ctx, const String& name )
		{
			auto it = m_attachedEditors.Find( name );
			return it != m_attachedEditors.End() ? it.Value()->CreateInterop() : interop::ObjectPtr();
		} );

		RegisterFunctor< void, String >( "toggleAttachEditor", [ this ]( interop::IContext& ctx, const String& editorName )
		{
			ToggleAttachEditor( ctx, editorName );
		} );

		RegisterFunctor< void, String >( "detachEditor", [ this ]( interop::IContext& ctx, const String& editorName )
		{
			//DetachEditor( ctx, editorName );
		} );

		RegisterFunctor< void, oauth::AuthenticationToken >( "jiraOauthChanged", [ this ]( interop::IContext& ctx, const oauth::AuthenticationToken &token )
		{
			SetJiraOauthToken( token );
		} );

		RegisterFunctor< void, String >( "queryJiraOauthStatus", [ this ]( interop::IContext& ctx, const String &editorName )
		{
			auto it = m_attachedEditors.Find( editorName );
			if ( it == m_attachedEditors.End() )
			{
				ctx.ThrowException( "There's no editor with name: '%hs'", editorName.AsChar() );
				return;
			}

			AttachedEditor* attachedEditor = it.Value().Get();
			attachedEditor->NotifyJiraOAuthChanged( m_jiraOauth );
		} );
	}

	void BugFlagsService::SetJiraOauthToken( const oauth::AuthenticationToken &token )
	{
		m_jiraOauth = token;

		for ( const auto &editor : m_attachedEditors )
		{
			editor.Value()->NotifyJiraOAuthChanged( token );
		}
	}

	BugFlagsService::AttachedEditor::AttachedEditor( const red::SharedPtr< IResourceEditor >& editor, const red::SharedPtr< PreviewPanel >& previewPanel, const red::SharedPtr< world::RuntimeScene >& scene )
		: m_editor( editor )
		, m_previewPanel( previewPanel )
		, m_runtimeScene( scene )
		, m_jiraIssueHistory( red::CreateUniquePtr< IEditActionHistory >( nullptr ) )
		, m_jiraIssueCollection( red::CreateUniquePtr< ObjectCollection >( *m_jiraIssueHistory ) )
		, m_isAttached( false )
	{
		m_issuesTree = red::CreateUniquePtr< BugFlagsTree >( m_editor->GetPath() );
		m_issuesTreeInterop = red::CreateSharedPtr< TreeContainerInterop >( m_issuesTree.Get(), nullptr );

		m_issuesTree->NodeActivated += { [ this ]( const TTreeNodeIDs& activated, const TTreeNodeIDs& deactivated )
		{
			if ( activated.Empty() )
			{
				return;
			}

			ITreeNode* node = m_issuesTree->GetNode( activated.Back() );
			if ( node->IsA< BugFlagsIssueNode >() )
			{
				const JiraIssue& issue = static_cast<BugFlagsIssueNode*>( node )->GetJiraIssue();
				m_previewPanel->GetCamera()->SetCameraLocation( FromStringDirect< CameraLocation >( issue.m_fields.m_cameraLocation ) );
			}
		} };

		m_issuesTree->EditIssue += { [ this ]( BugFlagsIssueNode* issueNode )
		{
			SetAttached( true );

			if ( BugFlagsSystem* system = m_runtimeScene->GetSystem< BugFlagsSystem >() )
			{
				THandle< BugFlag > bugFlag = system->GetFlag( issueNode->GetId() );
				if ( !bugFlag )
				{
					RED_REPORT_ERROR( RED_LOCATION( this->GetEditor()->GetPath() ), "Unable to edit issue. Unspecified internal error." );
					return;
				}

				bugFlag->SetReadOnly( false );

				// refresh proxy
				m_editor->SelectObject( EditorObjectIDPath( bugFlag->GetEditorObjectID() ) );

				m_currentFlagNotifier.SetCallback( [ this, bugId = bugFlag->GetID() ]( const SerializableID& id, const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& targetValue )
				{
					if ( bugId == id )
					{
						rtti::PathParser parser( path );
						CName name;
						if ( parser.EatName( name ) )
						{
							if ( name == RED_NAME_CONSTEXPR_NOREG( "position" ) )
							{
								Vector3 position;
								GetTypeObject< Vector3 >()->WriteValue( IRTTIContext::GetDefault(), &position, rtti::AccessPath(), *targetValue );

								m_jiraEditRequest->m_fields.m_flagPosition = ToStringDirect< Vector3 >( position );
								m_jiraEditRequest->NotifyPropertyChanged( rtti::AccessPath( "fields.flagPosition" ) );
							}
						}
					}
				} );
			}

			const JiraIssue& issueToEdit = issueNode->GetJiraIssue();
			m_jiraEditRequest = CreateHandle< tools::JiraEditIssueBody >( issueToEdit );
			m_previewPanel->GetCamera()->SetCameraLocation( FromStringDirect< CameraLocation >( issueToEdit.m_fields.m_cameraLocation ) );

			red::AbsolutePath absViewPath;
			if ( GenerateScreenshotPath( absViewPath ) )
			{
				rend::SingleScreenShotData data;
				data.m_requested = true;
				data.m_mode = rend::ScreenshotMode::NORMAL;
				data.m_outputPath = absViewPath;
				m_previewPanel->TakeFullScreenshot( data, true );

				m_jiraEditRequest->m_cameraViewPath = absViewPath.AsString();
			}

			if ( !issueToEdit.m_fields.m_attachments.Empty() )
			{
				JiraService service;
				service.SetOAuthToken( m_token );

				JiraAttachment attachment = issueToEdit.m_fields.m_attachments.Back();
				service.DownloadAttachment( attachment.m_id, attachment.m_filename, [this, attachment] ( Bool result )
				{
					m_jiraEditRequest->m_screenshotPath = GFileManager->GetCacheDirectory().AsString() + attachment.m_filename;
					m_jiraEditRequest->NotifyPropertyChanged( rtti::AccessPath( "screenshot" ) );
				} );
			}

			m_jiraIssueCollection->SetObject( m_jiraEditRequest.Get(), false, CreateHandle< BugFlagItemContext >( m_previewPanel ) );
			m_jiraIssueEditNotifier.Notify();
		} };

		m_issuesTree->OpenInBrowser += { [this]( BugFlagsIssueNode* issueNode )
		{
			OpenBugInBrowser( issueNode->GetJiraIssue().m_key );
		} };
	}

	BugFlagsService::AttachedEditor::~AttachedEditor() = default;

	void BugFlagsService::AttachedEditor::SetAttached( Bool value )
	{
		if ( m_isAttached != value )
		{
			m_isAttached = value;
			m_isAttachedChanged.Notify( m_isAttached );

			if ( m_isAttached )
			{
				InitializeBugFlagsSystem();
			}
			else
			{
				DeinitializeBugFlagsSystem();
			}
		}
	}

	void BugFlagsService::AttachedEditor::OnBugFlagAdded( const res::ResourcePath& resourcePath, const String& cameraLocation, const Vector3& flagPosition )
	{
		if ( !m_token.IsValid() )
		{
			RED_REPORT_ERROR( RED_LOCATION( resourcePath ), "BugFlag not placed: jira authentication failed. Try logging in." );
			return;
		}

		m_jiraCreateRequest = CreateHandle< tools::JiraCreateIssueBody >();

		red::AbsolutePath absViewPath;
		if ( GenerateScreenshotPath( absViewPath ) )
		{
			rend::SingleScreenShotData data;
			data.m_requested = true;
			data.m_mode = rend::ScreenshotMode::NORMAL;
			data.m_outputPath = absViewPath;
			m_previewPanel->TakeFullScreenshot( data, true );

			m_jiraCreateRequest->m_cameraViewPath = absViewPath.AsString();
			m_jiraCreateRequest->m_screenshotPath = absViewPath.AsString();
		}

		m_jiraCreateRequest->m_fields.m_summary = "Bug Flag";
		m_jiraCreateRequest->m_fields.m_project.m_key = Config::cvDefaultProjectKey.Get();
		m_jiraCreateRequest->m_fields.m_priority.m_name = "P3";
		m_jiraCreateRequest->m_fields.m_issuetype.m_name = "Bug";
		m_jiraCreateRequest->m_fields.m_frequency.m_value = "Random";
		m_jiraCreateRequest->m_fields.m_cameraLocation = cameraLocation;
		m_jiraCreateRequest->m_fields.m_flagPosition = ToStringDirect<Vector3>( flagPosition );
		m_jiraCreateRequest->m_fields.m_assetBrowserPath = resourcePath.ToString();
		m_jiraCreateRequest->m_fields.m_buildVersion = APP_VERSION_NUMBER;
		m_jiraCreateRequest->m_fields.m_bugsComponents.PushBack( JiraCustomFieldValue{ "Flag" } );
		m_jiraCreateRequest->m_fields.m_assignee.m_name = "Committee";

		m_jiraIssueCollection->SetObject( m_jiraCreateRequest.Get(), false, CreateHandle< BugFlagItemContext >( m_previewPanel ) );
		m_jiraIssueCreationNotifier.Notify();
	}

	void BugFlagsService::AttachedEditor::OpenBugInBrowser( const String& key ) const
	{
		String url = JiraService().GetServerURL() + "/browse/" + key;
		ShellExecuteA( nullptr, "open", url.AsChar(), nullptr, nullptr, SW_SHOWNORMAL );
	}

	void BugFlagsService::AttachedEditor::NotifyJiraOAuthChanged( const oauth::AuthenticationToken &token )
	{
		m_token = token;
		m_issuesTree->SetOAuthToken( m_token );
		m_jiraAuthorizationChanged.Notify( m_token );
	}

	void BugFlagsService::AttachedEditor::InitializeBugFlagsSystem()
	{
		BugFlagsSystem* bugFlagsSystem = m_runtimeScene->GetSystem< BugFlagsSystem >();
		if ( !bugFlagsSystem )
		{
			RED_LOG_ERROR( "BugFlagsService: No BugFlagsSystem present in the runtime scene" );
			return;
		}

		TTreeNodeIDs children;
		m_issuesTree->GetNodeChildren( m_issuesTree->GetRootID(), children );
		for ( const TreeNodeID id : children )
		{
			const BugFlagsIssueNode* issueNode = static_cast<const BugFlagsIssueNode*>( m_issuesTree->GetNode( id ) );
			THandle< BugFlag > bugFlag = bugFlagsSystem->AddFlag( id, issueNode->GetFlagPosition(), issueNode->GetColor() );
			bugFlag->SetSummary( issueNode->GetJiraIssue().m_fields.m_summary );
		}

		bugFlagsSystem->FlagSelected += { [ this ]( BugFlag* flag )
		{
			m_issuesTree->SetSelection( flag->GetEditorObjectID().GetInternalID() );
		} };
	}

	void BugFlagsService::AttachedEditor::DeinitializeBugFlagsSystem()
	{
		BugFlagsSystem* bugFlagsSystem = m_runtimeScene->GetSystem< BugFlagsSystem >();
		if ( !bugFlagsSystem )
		{
			RED_LOG_ERROR( "BugFlagsService: No BugFlagsSystem present in the runtime scene" );
			return;
		}

		bugFlagsSystem->ClearFlags();
	}

	void BugFlagsService::AttachedEditor::SetupInterop( AttachedEditor* ptr, ::interop::utils::Object& op )
	{
		op.RegisterObject( "tree", interop::ObjectPtr( ptr->m_issuesTreeInterop ) );
		op.RegisterObject( "addJiraForm", ptr->m_jiraIssueCollection->GetRootInterop() );

		op.RegisterFunctor<void, Bool>( "addBugFlag", [ ptr ]( interop::IContext& ctx, Bool openInBrowser )
		{
			JiraService service;
			if ( ptr->m_jiraCreateRequest->m_fields.m_project.m_key.EqualsNC( "CYB" ) )
			{
				ptr->m_jiraCreateRequest->m_fields.m_fixVersions.PushBack( JiraFixVersion{ "14314" } ); //Backlog
			}

			service.SetOAuthToken( ptr->m_token );

			service.CreateIsssue( ptr->m_jiraCreateRequest, [ptr, openInBrowser] ( Bool success, THandle< tools::JiraCreateIssueResult > result )
			{
				if ( success && result )
				{
					const JiraIssue issue{ result->m_id, result->m_key, "", ptr->m_jiraCreateRequest->m_fields };

					auto newIssueNode = red::CreateSharedPtr< BugFlagsIssueNode >( issue, ptr->m_issuesTree.Get() );
					ptr->m_issuesTree->AddNodeToRoot( newIssueNode );

					ptr->GetScene()->GetSystem< BugFlagsSystem >()->AddFlag( newIssueNode->GetId(), newIssueNode->GetFlagPosition(), newIssueNode->GetColor() );

					red::DynArray< red::AbsolutePath > attachmentPaths = { red::AbsolutePath::CreateFilePath( ptr->m_jiraCreateRequest->m_screenshotPath ) };

					JiraService service;
					service.SetOAuthToken( ptr->m_token );

					service.AddAttachments( result->m_key, attachmentPaths, [ptr, openInBrowser, result] ( Bool attachmentSucess, THandle< JiraAddAttachmentsResult > attachmentResult )
					{
						if ( attachmentSucess && openInBrowser )
						{
							ptr->OpenBugInBrowser( result->m_key );
						}
					} );

					Config::cvDefaultProjectKey.Set( issue.m_fields.m_project.m_key );
				}
				else
				{
					const res::ResourcePath editorResourcePath = ptr->GetEditor()->GetPath();
					ReportError( editorResourcePath, result ? result->m_errorMessages : red::DynArray< String >() );
				}
			} );
		} );

		op.RegisterFunctor<void, Bool>( "editBugFlag", [ ptr ]( interop::IContext& ctx, Bool openInBrowser )
		{
			Uint64 id = FromStringDirect<Uint64>( ptr->m_jiraEditRequest->m_id );

			if ( BugFlagsSystem* system = ptr->m_runtimeScene->GetSystem< BugFlagsSystem >() )
			{
				THandle< BugFlag > bugFlag = system->GetFlag( id );
				bugFlag->SetReadOnly( true );

				// refresh proxy
				ptr->m_editor->SelectObject( EditorObjectIDPath( bugFlag->GetEditorObjectID() ) );
			}

			BugFlagsIssueNode* issueNode = ptr->m_issuesTree->GetNode< BugFlagsIssueNode >( id );

			JiraService service;
			service.SetOAuthToken( ptr->m_token );
			service.EditIssue( ptr->m_jiraEditRequest, issueNode->GetJiraIssue().m_fields, [ ptr, id, openInBrowser ]( Bool success, THandle< tools::JiraEditIssueResult > result )
			{
				if ( success )
				{
					JiraService service;
					service.SetOAuthToken( ptr->m_token );

					red::DynArray< red::AbsolutePath > attachmentPaths = {};
					if ( !ptr->m_jiraEditRequest->m_screenshotPath.Empty() )
					{
						attachmentPaths.PushBack( red::AbsolutePath::CreateFilePath( ptr->m_jiraEditRequest->m_screenshotPath ) );
					}

					service.AddAttachments( ptr->m_jiraEditRequest->m_key, attachmentPaths, [ptr, openInBrowser, result] ( Bool attachmentSuccess, THandle< JiraAddAttachmentsResult > attachmentResult )
					{
						if ( attachmentSuccess && openInBrowser )
						{
							ptr->OpenBugInBrowser( ptr->m_jiraEditRequest->m_key );
						}
					} );

					BugFlagsIssueNode* issueNode = ptr->m_issuesTree->GetNode< BugFlagsIssueNode >( id );

					const JiraIssue issue{ ptr->m_jiraEditRequest->m_id, ptr->m_jiraEditRequest->m_key, "", ptr->m_jiraEditRequest->m_fields };

					issueNode->SetJiraIssue( issue );

					if ( BugFlagsSystem* system = ptr->m_runtimeScene->GetSystem< BugFlagsSystem >() )
					{
						THandle< BugFlag > bugFlag = system->GetFlag( id );
						bugFlag->SetColor( issueNode->GetColor() );
						bugFlag->SetSummary( issue.m_fields.m_summary );
					}
				}
				else
				{
					const res::ResourcePath editorResourcePath = ptr->GetEditor()->GetPath();
					ReportError( editorResourcePath, result ? result->m_errorMessages : red::DynArray< String >() );
				}
			} );
		} );

		op.RegisterFunctor<void>( "cancel", [ ptr ]( interop::IContext& ctx )
		{
			if ( BugFlagsSystem* system = ptr->m_runtimeScene->GetSystem< BugFlagsSystem >() )
			{
				Uint64 id = FromStringDirect<Uint64>( ptr->m_jiraEditRequest->m_id );

				THandle< BugFlag > bugFlag = system->GetFlag( id );
				bugFlag->SetReadOnly( true );

				BugFlagsIssueNode* issueNode = ptr->m_issuesTree->GetNode< BugFlagsIssueNode >( id );
				bugFlag->SetPosition( issueNode->GetFlagPosition() );

				// refresh proxy
				ptr->m_editor->SelectObject( EditorObjectIDPath( bugFlag->GetEditorObjectID() ) );
			}
		} );

		op.RegisterNotifier( "_onCreateJira", ptr->m_jiraIssueCreationNotifier );
		op.RegisterNotifier( "_onEditJira", ptr->m_jiraIssueEditNotifier );
		op.RegisterNotifier( "_isAttachedChanged", ptr->m_isAttachedChanged );
		op.RegisterNotifier( "_jiraAuthorizationChanged", ptr->m_jiraAuthorizationChanged );
	}

}