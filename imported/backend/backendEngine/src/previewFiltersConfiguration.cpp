/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "previewFiltersConfiguration.h"
#include "debugFilterTree.h"
#include "filterDescriptionManager.h"
#include "treeInterop.h"

#include "../../../common/renderData/include/debugFilter.h"

#include "../../../common/redConfig/include/configVarSystem.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/redReflection/include/resourceUtils.h"
#include "../../../common/resource/include/jsonParser.h"
#include "../../../common/resource/include/jsonSerializer.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( FilterDataContainer, tools );
	RTTI_PARENT_TYPE( ISerializable );
	RTTI_PROPERTY( m_parentFilename );
	RTTI_PROPERTY( m_filters );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( PredefinedFilterFile, tools );
	RTTI_PARENT_TYPE( ISerializable );
	RTTI_PROPERTY( m_filters );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( EditableFilterFile, tools );
	RTTI_PARENT_TYPE( ISerializable );
	RTTI_PROPERTY( m_parentFilename );
	RTTI_PROPERTY( m_filters );
RTTI_END_TYPE();

namespace tools
{
	const String PreviewFilterFileConfiguration::c_defaultUserFilename = "user";
	const String PreviewFilterFileConfiguration::c_defaultPredefineFilename = "default";
	const String PreviewFilterFileConfiguration::c_predefinedFileIndicator = "Predefine: ";

	PreviewFilterFileConfiguration::PreviewFilterFileConfiguration()
	{
		m_predefinedFilterFile = CreateHandle<PredefinedFilterFile>();
		m_userDefinedFilterFile = CreateHandle<EditableFilterFile>();

		ReadFilterFileFolders();
	}

	PreviewFilterFileConfiguration::~PreviewFilterFileConfiguration() = default;

	void PreviewFilterFileConfiguration::ReadFilterFileFolders()
	{
		const String filter = "*.json";

		m_userFiles.Clear();
		m_predefinedFiles.Clear();

		GFileManager->FindFiles( res::GetPreDefinedFilterFilePath( String::EMPTY() ), filter, m_predefinedFiles, false );

		GFileManager->FindFiles( res::GetUserDefinedFilterFilePath( String::EMPTY() ), filter, m_userFiles, false );
	}

	Bool PreviewFilterFileConfiguration::RenameUserDefinedFilterFile( const String& newFilename )
	{
		red::DynArray<String> changedFilters = m_userDefinedFilterFile->GetFilters();
		m_userDefinedFilterFile = CreateHandle<EditableFilterFile>( newFilename, m_userDefinedFilterFile->GetParentFilename() );

		if ( m_userDefinedFilterFile->Save( changedFilters ) )
		{
			m_currentPresetFilename = res::GetUserDefinedFilterFilePath( m_userDefinedFilterFile->GetFilename() ).AsString();
			ReadFilterFileFolders();
			return true;
		}
		
		return false;
	}

	void PreviewFilterFileConfiguration::CreatePredefinedPresetFile( const String& filename )
	{
		red::DynArray < String > filterNames;
		m_debugFilters.FindFiltersTurnedOn( filterNames );
		
		THandle< FilterDataContainer > filterData = CreateHandle<FilterDataContainer>();
		filterData->m_filters = filterNames;

		serialization::json::JsonSerializer jsonSerializer( filterData );
		String result = String::EMPTY();
		jsonSerializer.Serialize( result );
 		
		SaveStringToFile( res::GetPreDefinedFilterFilePath( filename ), result );
	}

	Bool PreviewFilterFileConfiguration::SaveFilterFile()
	{
		red::DynArray<String> changedFilters;

		// This is ugly but this will be refactored when filter are moved to tree control. Then all filter will be 
		// in one tree and m_debugFilters, m_renderFilters and m_gamefilters will go away. All three contain same information
		// but parts of it are hidden. m_gamefilters is basically a copy of m_debugFilters, m_renderFilters is a copy but with different root
		m_debugFilters.GetChangedFilters( changedFilters );
		m_renderFilters.GetChangedFilters( changedFilters );
		m_gameFilters.GetChangedFilters( changedFilters, "Game:" );

		if ( m_userDefinedFilterFile->Save( changedFilters ) )
		{
			m_currentPresetFilename = res::GetUserDefinedFilterFilePath( m_userDefinedFilterFile->GetFilename() ).AsString();
			return true;
		}

		return false;
	}

	Uint32 PreviewFilterFileConfiguration::GetNextUserFileNumber()
	{
		Uint32 current = 1;
		for ( const red::AbsolutePath& path : m_userFiles )
		{
			String filename = red::paths::ExtractFileStem( path );
			if ( filename.BeginsWith( c_defaultUserFilename) )
			{
				String valueString = filename.RightString( filename.Length() - c_defaultUserFilename.Length() );
				Uint32 value = FromStringDirect<Uint32>( valueString );
				if ( value > current )
				{
					current = value;
				}
				else if ( value == current )
				{
					current++;
				}
			}
		}

		return current;
	}

	Bool PreviewFilterFileConfiguration::LoadFilterFile( const String& filename )
	{
		for ( const red::AbsolutePath& path : m_predefinedFiles )
		{
			if ( path.AsString() == filename )
			{
				if ( !LoadPredefinedFilterFile( red::paths::ExtractFileStem( path ) ) )
				{
					return false;
				}

				m_currentPresetFilename = filename;

				String newFilename = c_defaultUserFilename + ToStringDirect( GetNextUserFileNumber() );
				m_userDefinedFilterFile = CreateHandle<EditableFilterFile>( newFilename, red::paths::ExtractFileStem( path ) );

				return true;
			}
		}

		for ( const red::AbsolutePath& path : m_userFiles )
		{
			if ( path.AsString() == filename )
			{
				if ( !LoadUserFilterFile( red::paths::ExtractFileStem( path ) ) )
				{
					return false;
				}

				m_currentPresetFilename = path.AsString();
				return true;
			}
		}

		// as fallback, use default file
		if ( LoadPredefinedFilterFile( c_defaultPredefineFilename ) )
		{
			m_currentPresetFilename = res::GetPreDefinedFilterFilePath( c_defaultPredefineFilename ).AsString();
			return true;
		}

		return false;
	}

	Bool PreviewFilterFileConfiguration::LoadPredefinedFilterFile( const String& filename )
	{
		if ( m_predefinedFilterFile->Load( filename ) )
		{
			m_debugFilters.ResetFilters();
			m_debugFilters.SetFilters( m_predefinedFilterFile->GetFilters(), true );

			return true;
		}

		return false;
	}

	Bool PreviewFilterFileConfiguration::LoadUserFilterFile( const String& filename )
	{
		if ( m_userDefinedFilterFile->Load( filename ) )
		{
			LoadPredefinedFilterFile( m_userDefinedFilterFile->GetParentFilename() );
			m_debugFilters.ToggleFilter( m_userDefinedFilterFile->GetFilters() );
			return true;
		}

		return false;
	}

	const red::DynArray<String> PreviewFilterFileConfiguration::GetFileList( const Bool refresh )
	{
		if ( refresh )
		{
			ReadFilterFileFolders();
		}

		red::DynArray<String> fileList;

		for ( const red::AbsolutePath& path : m_predefinedFiles )
		{
			fileList.PushBack( path.AsString() );
		}

		for ( const red::AbsolutePath& path : m_userFiles )
		{
			fileList.PushBack( path.AsString() );
		}

		return fileList;
	}

	void PreviewFilterFileConfiguration::GetCurrentPresetFile( String& value )
	{
		value = m_currentPresetFilename;
	}

	////////////////////////////////////////////////////////////////////////////////////

	const char* PreviewFiltersInterop::c_filterConfigGroup = "FilterPresetFile";
	const char* PreviewFiltersInterop::c_filterConfigValueKey = "Value";
	const char* PreviewFiltersInterop::c_filterConfigTypeKey = "Type";

	PreviewFiltersInterop::PreviewFiltersInterop( ShortcutManager* shortcutManager )
		: m_treesCreated( false )
	{
		m_filterFileHandler = red::CreateUniquePtr<PreviewFilterFileConfiguration>();
		m_descriptionManager = red::CreateSharedPtr<FilterDescriptionManager>();
		
		// editor and game filters
		m_editorFilterTree = red::CreateSharedPtr< DebugFilterTree >( m_filterFileHandler->m_debugFilters, m_filterFileHandler->m_gameFilters, m_filterFileHandler->m_renderFilters, shortcutManager, false );
 		auto editorRoot = red::CreateSharedPtr< utils::NamedNode >( "Root", m_editorFilterTree.Get() );
 		m_editorFilterTree->SetRoot( editorRoot );
 		m_editorFilterTreeInterop = red::CreateSharedPtr<TreeContainerInterop>( m_editorFilterTree.Get(), nullptr );
		m_editorFilterTree->SetDescriptionManager( m_descriptionManager.Get() );
		m_editorFilterTree->SetFavoritesChangedCallback( [ this ]( ITreeNode* node )
		{
			UpdateFavoritesTree( node, false );
		} );
	
 		// render filters
 		m_renderFilterTree = red::CreateSharedPtr< DebugFilterTree >( m_filterFileHandler->m_renderFilters, m_filterFileHandler->m_gameFilters, m_filterFileHandler->m_renderFilters, shortcutManager, true );
 		auto renderRoot = red::CreateSharedPtr< utils::NamedNode >( "Root", m_renderFilterTree.Get() );
 		m_renderFilterTree->SetRoot( renderRoot );
 		m_renderFilterTreeInterop = red::CreateSharedPtr<TreeContainerInterop>( m_renderFilterTree.Get(), nullptr );
		m_renderFilterTree->SetDescriptionManager( m_descriptionManager.Get() );
		m_renderFilterTree->SetFavoritesChangedCallback( [ this ]( ITreeNode* node )
		{
			UpdateFavoritesTree( node, true );
		} );

 		// favorites
 		m_favoriteFilterTree = red::CreateSharedPtr< DebugFilterTree >( m_filterFileHandler->m_debugFilters, m_filterFileHandler->m_gameFilters, m_filterFileHandler->m_renderFilters, shortcutManager, false );
 		auto filterRoot = red::CreateSharedPtr< utils::NamedNode >( "Root", m_favoriteFilterTree.Get() );
 		m_favoriteFilterTree->SetRoot( filterRoot );
 		m_favoriteTreeInterop = red::CreateSharedPtr<TreeContainerInterop>( m_favoriteFilterTree.Get(), nullptr );
		m_favoriteFilterTree->SetDescriptionManager( m_descriptionManager.Get() );
		m_favoriteFilterTree->SetFavoritesChangedCallback( [ this ]( ITreeNode* node )
		{
			RemoveFavorite( node );
		} );
		
		m_editorRoot = red::CreateSharedPtr< utils::NamedNode >( "Editor", m_favoriteFilterTree.Get() );
		m_renderRoot = red::CreateSharedPtr< utils::NamedNode >( "Rendering", m_favoriteFilterTree.Get() );
		
		m_favoriteFilterTree->AddNodeToRoot( m_editorRoot );
		m_favoriteFilterTree->AddNodeToRoot( m_renderRoot );
	}

	PreviewFiltersInterop::~PreviewFiltersInterop()
	{
		SaveToConfig();
	}

	void PreviewFiltersInterop::SetupInterop( PreviewFiltersInterop* ptr, ::interop::utils::Object& op )
	{
		op.RegisterObject( "debugFilters", ptr->m_filterFileHandler->m_debugFilters );
		op.RegisterObject( "renderFilters", ptr->m_filterFileHandler->m_renderFilters );
		op.RegisterObject( "gameFilters", ptr->m_filterFileHandler->m_gameFilters );

		op.RegisterObject( "editorFilterTree", interop::ObjectPtr( ptr->m_editorFilterTreeInterop ) );
		op.RegisterObject( "renderFilterTree", interop::ObjectPtr( ptr->m_renderFilterTreeInterop ) );
		op.RegisterObject( "favoriteFilterTree", interop::ObjectPtr( ptr->m_favoriteTreeInterop ) );

		// New filter file created or existing renamed
		op.RegisterNotifier( "filterFilesChanged", ptr->m_onFilterFilesChanged );

		// Filter file loaded successfully
		op.RegisterNotifier( "filterFileLoaded", ptr->m_onFilterFileLoaded );

		op.RegisterFunctor<void, String>( "renameFilterPreset", [ ptr ]( interop::IContext& ctx, const String& filename )
		{
			ptr->m_filterFileHandler->RenameUserDefinedFilterFile( filename );
			ptr->m_onFilterFilesChanged.Notify();
		} );

		op.RegisterFunctor<void, String>( "createPredefinedPresetFile", [ ptr ]( interop::IContext& ctx, const String& filename )
		{
			ptr->m_filterFileHandler->CreatePredefinedPresetFile( filename );
			ptr->m_onFilterFilesChanged.Notify();
		} );

		op.RegisterFunctor<red::DynArray<String>, Bool>( "filterPresetFilenames", [ ptr ]( interop::IContext& ctx, const Bool refresh )
		{
			return ptr->m_filterFileHandler->GetFileList( refresh );
		} );

		op.RegisterValue<String>( "filterPresetFile",
		/*GET*/ [ ptr ] ( interop::IContext& ctx, String& outValue ) { ptr->m_filterFileHandler->GetCurrentPresetFile( outValue ); },
		/*SET*/ [ ptr ] ( interop::IContext& ctx, const String& newValue ) { ptr->LoadFilterFile( newValue );
		} );

		op.RegisterFunctor< void, String, Bool >( "setFilterValue", [ ptr ] ( interop::IContext& ctx, const String& path, Bool value )
		{
			ptr->m_editorFilterTree->GetEditorFilters()->SetFilter( path, value );
		} );

		op.RegisterFunctor< Bool, String >( "getFilterValue", [ ptr ]( interop::IContext& ctx, const String& path )
		{
			return ptr->m_editorFilterTree->GetEditorFilters()->IsFilterSet( path );
		} );
	}

	void PreviewFiltersInterop::Bind( const String& editorName )
	{
		String editorNameNoSpaces = editorName;
		editorNameNoSpaces.RemoveWhiteSpaces();
		m_editorConfigKey = String::Printf( "editor/%hs/%hs", c_filterConfigGroup, editorNameNoSpaces.AsChar() );

		// the three way separation of filters below will go away when filters are moved from property page to a tree control

		// bind debug filters: exclude rendering
		m_filterFileHandler->m_debugFilters.AddExcludedCategory( "Rendering" );
		m_filterFileHandler->m_debugFilters.Bind( nullptr, editorName,
			[ this ]( const rend::DebugFilterMask& mask, TreeNodeID nodeId, TreeNodeID favoriteId )
		{ 
			SaveFilterFile(); 
			m_editorFilterTree->NotifyNodeInvalidated( nodeId );
			m_favoriteFilterTree->NotifyNodeInvalidated( favoriteId );

		} );

		// root is from rendering node
		m_filterFileHandler->m_renderFilters.Bind( "Rendering", editorName,
			[ this ]( const rend::DebugFilterMask& mask, TreeNodeID nodeId, TreeNodeID favoriteId )
		{
			SaveFilterFile();
			m_renderFilterTree->NotifyNodeInvalidated( nodeId );
			m_favoriteFilterTree->NotifyNodeInvalidated( favoriteId );

		} );
		m_filterFileHandler->m_gameFilters.Bind( nullptr, rend::GetGameFilterName(),
			[ this ]( const rend::DebugFilterMask& mask, TreeNodeID nodeId, TreeNodeID favoriteId )
		{ 
			rend::DebugFilterMask::GetFilterMask( rend::GetGameFilterName() ) = mask; 
			m_editorFilterTree->NotifyNodeInvalidated( nodeId );
			m_favoriteFilterTree->NotifyNodeInvalidated( favoriteId );
		} );

		String loadedFilename;
		String readOnlyFile;

		if ( Config::GetConfigSystem().GetValue( m_editorConfigKey.AsChar(), c_filterConfigValueKey, loadedFilename ) &&
			 Config::GetConfigSystem().GetValue( m_editorConfigKey.AsChar(), c_filterConfigTypeKey, readOnlyFile ))
		{
			if ( FromStringDirect<Bool>( readOnlyFile ) )
			{
				LoadFilterFile( res::GetPreDefinedFilterFilePath( loadedFilename ).AsString() );
			}
			else
			{
				LoadFilterFile( res::GetUserDefinedFilterFilePath( loadedFilename ).AsString() );
			}
		}
		else
		{
			LoadFilterFile( res::GetPreDefinedFilterFilePath( PreviewFilterFileConfiguration::c_defaultPredefineFilename ).AsString() );
		}

		m_onFilterFilesChanged.Notify();
	}

	void PreviewFiltersInterop::SetShortcutManager( ShortcutManager& manager )
	{
		m_shortcutManager = &manager;

		const String idPredefined = "idPredefinedFilterPreset";
		const String idUser = "idUserFilterPreset";
		const String namePrefixPredefined = "Filter Preset, Predefined: ";
		const String namePrefix = "Filter Preset: ";
		const String group = "Preview Panel";

		for ( const red::AbsolutePath& path : m_filterFileHandler->GetPredefinedFiles() )
		{
			const String stem = red::paths::ExtractFileStem( path );
			const String id = idPredefined + stem;
			const String name = namePrefixPredefined + stem;

			ShortcutCallback callback = [ this, path ]( const KeyEventData& )
			{
				LoadFilterFile( path.AsString() );
				return true;
			};

			ShortcutBuilder shortcut = ShortcutBuilder().IdName( id ).Name( name ).Callback( callback );

			manager.Register( shortcut );
		}
	}

	void PreviewFiltersInterop::SaveToConfig() const
	{
		String filename;
		m_filterFileHandler->GetCurrentPresetFile( filename );
		Config::GetConfigSystem().SetValue( m_editorConfigKey.AsChar(), c_filterConfigTypeKey, ToStringDirect( filename.BeginsWith( GFileManager->GetEngineRoot().AsString() ) ) );
		Config::GetConfigSystem().SetValue( m_editorConfigKey.AsChar(), c_filterConfigValueKey, red::paths::ExtractFileName( filename ) );
	}

	void PreviewFiltersInterop::SaveFilterFile()
	{
		if ( m_filterFileHandler->SaveFilterFile() )
		{
			m_onFilterFilesChanged.Notify();

			SaveToConfig();
		}
	}

	void PreviewFiltersInterop::LoadFilterFile( const String& filename )
	{
		if ( m_filterFileHandler->LoadFilterFile( filename ) )
		{

 			if ( !m_treesCreated )
 			{
 				CreateTrees();
 				m_treesCreated = true;
 			}

			SaveToConfig();

			m_onFilterFileLoaded.Notify();

			m_onFilterFilesChanged.Notify();
		}
	}

	void PreviewFiltersInterop::CreateTrees()
	{
		m_filterFileHandler->m_debugFilters.CreateFilterTree( m_editorFilterTree, false );
		m_filterFileHandler->m_renderFilters.CreateFilterTree( m_renderFilterTree, true );

		CreateFavoritesTree();

		m_filterFileHandler->m_gameFilters.SetNodeMap( m_filterFileHandler->m_debugFilters.GetNodeMap() );
		m_filterFileHandler->m_gameFilters.SetFavoriteMap( m_filterFileHandler->m_debugFilters.GetFavoriteMap() );

		m_favoriteFilterTree->ExpandAllNodes();
	}

	void PreviewFiltersInterop::RemoveFavorite( ITreeNode* node )
	{
		FavoriteFilterTreeNode* favoriteNode = static_cast< FavoriteFilterTreeNode* >( node );
		if ( favoriteNode )
		{
			if ( favoriteNode->IsRenderNode() )
			{
				m_renderFilterTree->SetNodeFavoriteStatus( favoriteNode->GetHostId(), false );
				m_renderFilterTree->NotifyNodeInvalidated( favoriteNode->GetHostId() );
			}
			else
			{
				m_editorFilterTree->SetNodeFavoriteStatus( favoriteNode->GetHostId(), false );
				m_editorFilterTree->NotifyNodeInvalidated( favoriteNode->GetHostId() );
			}

			m_favoriteFilterMap.Remove( favoriteNode->GetPath() );
			m_favoriteFilterTree->RemoveNode( favoriteNode->GetId() );

			//m_filterFileHandler->m_gameFilters.SetFavoriteMap( m_filterFileHandler->m_debugFilters.GetFavoriteMap() );

			SaveFavoriteFilters();
		}
	}

	void PreviewFiltersInterop::UpdateFavoritesTree( ITreeNode* node, const Bool renderNode )
	{
		red::ScopedLock< red::LightMutex > lock( m_lock );

 		DebugFilterTreeNode* debugNode = static_cast< DebugFilterTreeNode* >( node );
 		if ( debugNode )
 		{
 			FavoriteFilterTreeNode* favoriteNode = m_favoriteFilterMap[ debugNode->GetPath() ];
 			if ( favoriteNode == nullptr )
 			{
 				CreateFavoritesTreeNode( node, renderNode );
				m_filterFileHandler->m_gameFilters.SetFavoriteMap( m_filterFileHandler->m_debugFilters.GetFavoriteMap() );
 			}
 			else
 			{
				m_favoriteFilterTree->RemoveNode( favoriteNode->GetId() );
				m_favoriteFilterMap.Remove( debugNode->GetPath() );
 			}

			SaveFavoriteFilters();
 		}
	}

	void PreviewFiltersInterop::SaveFavoriteFilters()
	{
		THandle< FilterDataContainer > filterData = CreateHandle<FilterDataContainer>();
		filterData->m_filters = m_favoriteFilterMap.GetKeys();
		filterData->m_parentFilename = String::EMPTY();

		serialization::json::JsonSerializer jsonSerializer( filterData );
		String result = String::EMPTY();
		jsonSerializer.Serialize( result );

		SaveStringToFile( res::GetUserDefinedFilterFilePath( "Favorites" ), result );
	}

	void PreviewFiltersInterop::CreateFavoritesTreeNode( ITreeNode* node, const Bool renderNode )
	{
		DebugFilterTreeNode* debugNode = static_cast< DebugFilterTreeNode* >( node );
		
		red::DynArray<String> pathArray;
		pathArray.PushBack( debugNode->GetPath() );
		red::DynArray< red::SharedPtr< FavoriteFilterTreeNode > > favoriteNodes;
		utils::NamedNode* parent;

		if ( renderNode )
		{
			m_filterFileHandler->m_renderFilters.CreateFavoritesTreeNodes( m_favoriteFilterTree, favoriteNodes, pathArray );
			parent = m_renderRoot.Get();
		}
		else
		{
			m_filterFileHandler->m_debugFilters.CreateFavoritesTreeNodes( m_favoriteFilterTree, favoriteNodes, pathArray );
			parent = m_editorRoot.Get();
		}
		
		for ( const red::SharedPtr< FavoriteFilterTreeNode >& favoriteNode : favoriteNodes )
		{
			favoriteNode->SetFavorite( true );
			favoriteNode->SetRenderNode( renderNode );
			m_favoriteFilterMap[ favoriteNode->GetPath() ] = favoriteNode.Get();
			favoriteNode->SetShortcut( m_favoriteFilterTree->GetShortcut( favoriteNode->GetPath() ) );
			m_favoriteFilterTree->AddNode( favoriteNode, *parent );
		}
	}

	void PreviewFiltersInterop::CreateFavoritesTree()
	{
		THandle< FilterDataContainer > filterData;

		String jsonData;
		if ( !LoadFileToString( res::GetUserDefinedFilterFilePath( "Favorites" ), jsonData ) )
			return;

		if ( !res::json::ParseObjectInline( std::move( jsonData ), filterData ) )
			return;

		red::DynArray<String> renderFavorites;
		red::DynArray<String> editorFavorites;
		for ( const String& path : filterData->m_filters )
		{
			if ( path.BeginsWith( "Rendering" ) )
			{
				renderFavorites.PushBack( path );
			}
			else
			{
				editorFavorites.PushBack( path );
			}
		}

		red::DynArray< red::SharedPtr< FavoriteFilterTreeNode > > favoriteNodes;
		m_filterFileHandler->m_debugFilters.CreateFavoritesTreeNodes( m_favoriteFilterTree, favoriteNodes, editorFavorites );
		for ( const red::SharedPtr< FavoriteFilterTreeNode >& favoriteNode : favoriteNodes )
		{
			favoriteNode->SetFavorite( true );
			favoriteNode->SetRenderNode( false );
			favoriteNode->SetShortcut( m_favoriteFilterTree->GetShortcut( favoriteNode->GetPath() ) );
			m_favoriteFilterMap[ favoriteNode->GetPath() ] = favoriteNode.Get();
			m_favoriteFilterTree->AddNode( favoriteNode, *m_editorRoot );

			m_editorFilterTree->SetNodeFavoriteStatus( favoriteNode->GetHostId(), true );
		}
		
		favoriteNodes.Clear();
		m_filterFileHandler->m_renderFilters.CreateFavoritesTreeNodes( m_favoriteFilterTree, favoriteNodes, renderFavorites );
		for ( const red::SharedPtr< FavoriteFilterTreeNode >& favoriteNode : favoriteNodes )
		{
			favoriteNode->SetFavorite( true );
			favoriteNode->SetRenderNode( true );
			favoriteNode->SetShortcut( m_favoriteFilterTree->GetShortcut( favoriteNode->GetPath() ) );
			m_favoriteFilterMap[ favoriteNode->GetPath() ] = favoriteNode.Get();
			m_favoriteFilterTree->AddNode( favoriteNode, *m_renderRoot );

			m_renderFilterTree->SetNodeFavoriteStatus( favoriteNode->GetHostId(), true );
		}
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	PredefinedFilterFile::PredefinedFilterFile() = default;
	PredefinedFilterFile::~PredefinedFilterFile() = default;

	PredefinedFilterFile::PredefinedFilterFile( const red::DynArray<String>& filters )
		: m_filters( filters )
	{
	}

	Bool PredefinedFilterFile::Load( const String& filename )
	{
		THandle< FilterDataContainer > filterData;

		String jsonData;
		if ( !LoadFileToString( res::GetPreDefinedFilterFilePath( filename ), jsonData ) )
			return false;

		if ( !res::json::ParseObjectInline( std::move( jsonData ), filterData ) )
			return false;

		m_filters = std::move( filterData->m_filters );

		return true;
	}

	Bool EditableFilterFile::Load( const String& filename )
	{
		THandle< FilterDataContainer > filterData;

		String jsonData;
		if ( !LoadFileToString( res::GetUserDefinedFilterFilePath( filename ), jsonData ) )
			return false;

		if ( !res::json::ParseObjectInline( std::move( jsonData ), filterData ) )
			return false;

		m_filters.Clear();

		m_filename = filename;
		m_parentFilename = filterData->m_parentFilename;
		for ( String& name : filterData->m_filters )
		{
			if ( name.BeginsWith( "Game:" ) )
			{
				name.Replace( "Game:", "" );
				m_gameFilters.PushBack( name );
			}
			else
			{
				m_filters.PushBack( name );
			}
		}

		return true;
	}

	Bool EditableFilterFile::Save( const red::DynArray<String>& changedFilters )
	{
		for ( const String& filter : changedFilters )
		{
			if ( !m_filters.Remove( filter ).IsSuccessful() )
				m_filters.PushBack( filter );
		}

		THandle< FilterDataContainer > filterData = CreateHandle<FilterDataContainer>();
		filterData->m_filters = m_filters;
		filterData->m_parentFilename = m_parentFilename;

		serialization::json::JsonSerializer jsonSerializer( filterData );
		String result = String::EMPTY();
		jsonSerializer.Serialize( result );

		if ( !SaveStringToFile( res::GetUserDefinedFilterFilePath( m_filename ), result ) )
		{
			return false;
		}

		return true;
	}

	FilterDataContainer::FilterDataContainer() = default;
	FilterDataContainer::~FilterDataContainer() = default;

	EditableFilterFile::EditableFilterFile() = default;

	EditableFilterFile::EditableFilterFile( const String& filename, const String& parentFilename )
		: m_filename( filename )
		, m_parentFilename( parentFilename )
	{
	}

	EditableFilterFile::~EditableFilterFile() = default;
}