#pragma once

#include "resourceBackend.h"
#include "resourceEditor.h"
#include "../../common/issueReporter/include/jiraService.h"

namespace Config
{
	BACKEND_ENGINE_API extern TConfigVar< Bool > cvJiraIntegrationEnabled;
}

namespace world
{
	class RuntimeScene;
}

namespace tools
{
	struct JiraIssue;
	struct JiraEditIssueBody;

	class BugFlagItemContext : public ISerializable
	{
		RTTI_DECLARE_TYPE( BugFlagItemContext );

	public:
		BugFlagItemContext() = default;
		BugFlagItemContext( const PreviewPanelPtr& previewPAnel );

		const PreviewPanelPtr& GetPreviewPanel() const;
		String GetCurrentCameraLocation() const;
		void TakeScreenshot( red::AbsolutePath& outPath );

	private:
		PreviewPanelPtr m_panel;
	};

	class BugFlagsService : public interop::utils::Object, public IResourceBackendListener, public IResourceEditorListener
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		BugFlagsService( ResourceBackend* backend );
		virtual ~BugFlagsService() override;

		void ToggleAttachEditor( interop::IContext& ctx, const String& editorName );

		void JumpToIssue( const Transform& transform );

		void EditIssue( const JiraIssue& issue );

		// IResourceBackendListener interface
		virtual void OnEditorOpened( const String& editorName ) override;
		virtual void OnEditorClosed( const String& editorName ) override;
		virtual void OnAllEditorsClosed() override;

		// IResourceEditorListener interface
		virtual void OnPreviewPanelCreated( const String& editorName, Uint32 panelID ) override;
		virtual void OnPreviewPanelDestroyed( const String& editorName, Uint32 panelID ) override;
		virtual void OnAllPreviewPanelsDestroyed( const String& editorName ) override;
		virtual void OnBugFlagAdded( const String& editorName, const res::ResourcePath& resourcePath, const String& cameraLocation, const Vector3& flagPosition ) override;

	private:
		class AttachedEditor
		{
			DECLARE_INTEROP( AttachedEditor );
			RED_USE_MEMORY_POOL( red::PoolBackend );

		public:
			AttachedEditor( const red::SharedPtr< IResourceEditor >&, const red::SharedPtr< PreviewPanel >&, const red::SharedPtr< world::RuntimeScene >& );
			virtual ~AttachedEditor();

			Bool IsAttached() const { return m_isAttached; }
			void SetAttached( Bool value );

			void OnBugFlagAdded( const res::ResourcePath& resourcePath, const String& cameraLocation, const Vector3& flagPosition );

			const red::SharedPtr< IResourceEditor >& GetEditor() const { return m_editor; }
			const red::SharedPtr< PreviewPanel >& GetPreviewPanel() const { return m_previewPanel; }
			const red::SharedPtr< world::RuntimeScene >& GetScene() const { return m_runtimeScene; }

			void OpenBugInBrowser( const String& key ) const;

			void NotifyJiraOAuthChanged( const oauth::AuthenticationToken &token );

		private:
			void InitializeBugFlagsSystem();
			void DeinitializeBugFlagsSystem();

			red::SharedPtr< IResourceEditor > m_editor;
			red::SharedPtr< PreviewPanel > m_previewPanel;
			red::SharedPtr< world::RuntimeScene > m_runtimeScene;

			red::UniquePtr< class BugFlagsTree > m_issuesTree;
			red::SharedPtr< class TreeContainerInterop > m_issuesTreeInterop;

			THandle< JiraCreateIssueBody > m_jiraCreateRequest;
			THandle< JiraEditIssueBody > m_jiraEditRequest;
			red::UniquePtr< IEditActionHistory > m_jiraIssueHistory;
			red::UniquePtr< ObjectCollection > m_jiraIssueCollection;

			oauth::AuthenticationToken m_token{};

			SerializableValueNotifier m_currentFlagNotifier;

			interop::InteropNotifier m_jiraIssueCreationNotifier;
			interop::InteropNotifier m_jiraIssueEditNotifier;
			interop::InteropNotifier m_isAttachedChanged;
			interop::InteropNotifier m_jiraAuthorizationChanged;
			Bool m_isAttached;
		};

		void SetupInterop();
		void SetJiraOauthToken( const oauth::AuthenticationToken &token );

		red::UniquePtr< BugFlagsTree > m_issuesTree;
		red::SharedPtr< TreeContainerInterop > m_issuesTreeInterop;

		ResourceBackend* m_backend;

		//red::HashMap < std::pair< String, Uint32 >, red::SharedPtr< world::RuntimeScene > > m_editorAndPanelIDToScene;
		red::HashMap< String, red::UniquePtr< AttachedEditor > > m_attachedEditors;

		oauth::AuthenticationToken m_jiraOauth{};

		interop::InteropNotifier m_availableEditorsChanged;
	};
}