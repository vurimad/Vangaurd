/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "previewFilters.h" 
#include "../../../common/renderData/include/debugFilterMask.h"
#include "../../../common/interop/include/interopBase.h"
#include "../../../common/generalEditorBackend/include/generalEditorBackendPublic.h"

namespace tools
{
	namespace utils
	{
		class NamedNode;
	}

	class PreviewPanel;
	class FavoriteFilterTreeNode;
	class FilterDescriptionManager;
	class TreeContainerInterop;

	class FilterDataContainer : public ISerializable
	{
		RTTI_DECLARE_TYPE( FilterDataContainer );
		RED_BASE_CLASS( ISerializable );

	public:
		FilterDataContainer();
		virtual ~FilterDataContainer();

		String m_parentFilename;
		red::DynArray< String > m_filters;
	};

	class PredefinedFilterFile : public ISerializable
	{
		RTTI_DECLARE_TYPE( PredefinedFilterFile )
		RED_BASE_CLASS( ISerializable )

	public:
		PredefinedFilterFile();
		PredefinedFilterFile( const red::DynArray<String>& filters );
		~PredefinedFilterFile();

		const red::DynArray<String>& GetFilters() const { return m_filters; }

		// only load functionality, can not be saved
		Bool Load( const String& filename );
	private:

		// list of filters turned on;
		red::DynArray<String> m_filters;
	};

	class EditableFilterFile : public ISerializable
	{
		RTTI_DECLARE_TYPE( EditableFilterFile )
		RED_BASE_CLASS( ISerializable )

	public:
		EditableFilterFile();
		EditableFilterFile( const String& filename, const String& parentFilename );
		~EditableFilterFile();
	
		const String& GetFilename() const { return m_filename; }
		const String& GetParentFilename() const { return m_parentFilename; }
		const red::DynArray<String>& GetFilters() const { return m_filters; }
		const red::DynArray<String>& GetGameFilters() const { return m_gameFilters; }

		Bool Load( const String& filename );
		Bool Save( const red::DynArray<String>& changedFilters );

	private:
		String m_parentFilename;
		String m_filename;

		// list of filters that are different that in parent;
		red::DynArray<String> m_filters;
		red::DynArray<String> m_gameFilters;
	};

	class BACKEND_ENGINE_API PreviewFilterFileConfiguration
	{

		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		PreviewFilterFileConfiguration();
		~PreviewFilterFileConfiguration();

		Bool SaveFilterFile();
		Bool RenameUserDefinedFilterFile( const String& newFilename );

		Bool LoadFilterFile( const String& filename );

		const red::DynArray<String> GetFileList( const Bool refresh );

		void GetCurrentPresetFile( String& value );

		void CreatePredefinedPresetFile( const String& filename );

		static const String c_defaultPredefineFilename;

		PreviewFilters m_debugFilters;
		PreviewFilters m_renderFilters;
		PreviewFilters m_gameFilters;

		red::DynArray<red::AbsolutePath> GetPredefinedFiles() const { return m_predefinedFiles; }
		red::DynArray<red::AbsolutePath> GetUserFiles() const { return m_userFiles; }

	private:
		Bool LoadPredefinedFilterFile( const String& filename );
		Bool LoadUserFilterFile( const String& filename );

		Uint32 GetNextUserFileNumber();

		void ReadFilterFileFolders();

		static const String c_defaultUserFilename;
		static const String c_predefinedFileIndicator;

		THandle<PredefinedFilterFile> m_predefinedFilterFile;
		THandle<EditableFilterFile> m_userDefinedFilterFile;

		red::DynArray<red::AbsolutePath> m_predefinedFiles;
		red::DynArray<red::AbsolutePath> m_userFiles;

		red::DynArray<String> m_userSetFilters;

		String m_currentPresetFilename;
	};

	class BACKEND_ENGINE_API PreviewFiltersInterop
	{
		DECLARE_INTEROP( PreviewFiltersInterop );
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		PreviewFiltersInterop( ShortcutManager* shortcutManager );
		virtual ~PreviewFiltersInterop();

		void Bind( const String& editorName );
		void SetShortcutManager( ShortcutManager& manager );

	private:
		void LoadFilterFile( const String& filename );
		void SaveFilterFile();

		void CreateTrees();
		void RemoveFavorite( ITreeNode* node );
		void CreateFavoritesTree();
		void CreateFavoritesTreeNode( ITreeNode* node, const Bool renderNode );
		void UpdateFavoritesTree( ITreeNode* node, const Bool renderNode );

		void SaveFavoriteFilters();
		
		void SaveToConfig() const;

		static const char* c_filterConfigGroup;
		static const char* c_filterConfigValueKey;
		static const char* c_filterConfigTypeKey;

		interop::InteropNotifier m_onFilterFilesChanged;
		interop::InteropNotifier m_onFilterFileLoaded;

		ShortcutManager* m_shortcutManager;

		using DescriptionManagerPtr = red::SharedPtr<FilterDescriptionManager>;
		DescriptionManagerPtr m_descriptionManager;

		String m_editorConfigKey;

		Bool m_treesCreated;

		red::UniquePtr< PreviewFilterFileConfiguration > m_filterFileHandler;

		red::SharedPtr< utils::NamedNode > m_editorRoot;
		red::SharedPtr< utils::NamedNode > m_renderRoot;

		red::SharedPtr< DebugFilterTree > m_editorFilterTree;
		red::SharedPtr< DebugFilterTree > m_renderFilterTree;
		red::SharedPtr< DebugFilterTree > m_favoriteFilterTree;

		red::HashMap< String, FavoriteFilterTreeNode* > m_favoriteFilterMap;

		red::SharedPtr<TreeContainerInterop> m_editorFilterTreeInterop;
		red::SharedPtr<TreeContainerInterop> m_renderFilterTreeInterop;
		red::SharedPtr<TreeContainerInterop> m_favoriteTreeInterop;

		red::LightMutex m_lock;
	};
}