/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../common/resource/include/jsonFileHelper.h"
#include "../../../common/resourceMaterial/include/materialInstance.h"
#include "../../../common/resourceMaterial/include/materialTemplate.h"
#include "../../../common/resourceMaterial/include/materialLayerLibrary.h"

namespace tools
{
	class ResourceBackend;
	class MaterialLayersTree;
	class MaterialLayerDesc;

	class MaterialLayerDesc
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		MaterialLayerDesc( const String& name, const String& path );
		virtual ~MaterialLayerDesc() {}

		const String& GetName() const { return m_name; }
		const String& GetPath() const { return m_path; }
		const red::DynArray< String > GetTags() { return m_tags; };
		Bool IsLoaded() { return static_cast< Bool >( m_resource ); }
		THandle< CMaterialInstance >& GetResource();
		void SetResource( THandle< CMaterialInstance >&  resource ) { m_resource = resource; }
		void Reload();
	private:
		String m_name;
		String m_path;
		red::DynArray< String > m_tags;
		THandle<CMaterialInstance> m_resource;
	};

	typedef red::SharedPtr< MaterialLayerDesc > MaterialLayerDescPtr;
	typedef red::HashMap< String, red::DynArray< red::SharedPtr< MaterialLayerDesc > > > TCategoryMap;
	typedef red::HashMap< String, MaterialLayerDescPtr > TLayerMap;
	typedef red::DynArray< MaterialLayerDescPtr > TMaterialLayersArray;

	class IMaterialLibraryDataProviderListener
	{
	public:
		virtual ~IMaterialLibraryDataProviderListener() {}

		virtual void OnReset() = 0;
		virtual void OnCreateNode( MaterialLayerDescPtr& layerDesc ) = 0;
		virtual void OnRemoveNode( const String& name ) = 0;

	};

	class BACKEND_MATERIAL_API MaterialLibraryDataProvider
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		MaterialLibraryDataProvider( THandle< CMaterialLayerLibrary >& materialLibrary, tools::ResourceBackend* backend, const String& rootDirectory );
		~MaterialLibraryDataProvider() {}

		void AttachListener( IMaterialLibraryDataProviderListener* listener );
		void DetachListener( IMaterialLibraryDataProviderListener* listener );

		void CreateLayer( const String& name );
		void RemoveLayer( const String& name );
		void Rescan();
		Bool Save();
		const TMaterialLayersArray GetLayersByCategory( const String& category );
		MaterialLayerDescPtr GetLayerDescByName( const String& name );
		const red::DynArray< String > GetCategories();
		Bool UpdateJson( const red::AbsolutePath& absoluteJsonPath, const red::AbsolutePath& matlibAbsolutePath );

		void WriteTexturePath( JSONFileHelper::JSONWriter& writer, TResRef< CMaterialInstance >& material, const String& propName, const String& keyName );
		void WriteMicroblendsPath( JSONFileHelper::JSONWriter& writer, const res::ResourcePath& path );
		void WriteCategoryTag(JSONFileHelper::JSONWriter& writer, TResRef< CMaterialInstance >& material);

		String GetThumbnailRelativePath(const red::AbsolutePath& rootPath, const red::AbsolutePath& thumbnailPath);
		void ConvertPaths( String& path );
		String GetTagsFromMaterial( TResRef< CMaterialInstance >& material );
		String GetCategoryFromPath( const res::ResourcePath& resourcePath );

	private:
		RED_FORCE_INLINE tools::ResourceBackend* GetBackend() const { return m_backend; }

		red::DynArray< IMaterialLibraryDataProviderListener* > m_listeners;
		tools::ResourceBackend* m_backend;
		red::String	m_rootDirectory;
		THandle< CMaterialLayerLibrary > m_materialLibrary;

		TCategoryMap m_categoriesMap;
		TLayerMap m_layersMap;
	};

	typedef red::SharedPtr< MaterialLibraryDataProvider > MaterialLibraryDataProviderPtr;
}