/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "materialLibraryDataProvider.h"

#include "materialLayersTree.h"
#include "../../../common/redReflection/include/resourcePath.h"
#include "../../../common/redReflection/include/resourceUtils.h"
#include "../../../common/redReflection/include/resourceDepot.h"

#include "../../../common/resource/include/jsonDataBuilder.h"
#include "../../../common/resource/include/jsonFileHelper.h"
#include "../../../common/redFileSystem/include/rawFileWriter.h"
#include "../../../common/interop/include/interopNotifier.h"
#include "../../backendEngine/include/resourceBackend.h"
#include "../../../common/resourceMaterial/include/texture.h"

namespace tools
{

	MaterialLibraryDataProvider::MaterialLibraryDataProvider( THandle< CMaterialLayerLibrary >& materialLibrary, tools::ResourceBackend* backend, const String& rootDirectory )
		: m_backend( backend )
		, m_rootDirectory( rootDirectory )
		, m_materialLibrary( materialLibrary )
	{
	}

	void MaterialLibraryDataProvider::AttachListener( IMaterialLibraryDataProviderListener* listener )
	{
		if ( listener )
		{
			m_listeners.PushBack( listener );
		}
	}

	void MaterialLibraryDataProvider::DetachListener( IMaterialLibraryDataProviderListener* listener )
	{
		if ( listener )
		{
			m_listeners.Remove( listener );
		}
	}

	void MaterialLibraryDataProvider::CreateLayer( const String& name )
	{
		// dirty and hacky
		auto& layers = m_materialLibrary->GetMaterialLayers();
		layers.EmplaceBack();
		auto& newLayer = layers.Back();
		newLayer.m_name = RED_NAME( name );
		newLayer.m_size = 0;
		String path = m_rootDirectory + "\\global\\" + name + ".w2mi";
		res::ResourcePath resourcePath = res::ResourcePath::Build( path );
		GetBackend()->CreateResource( resourcePath, "" );
		newLayer.m_material = TResRef< CMaterialInstance >( resourcePath );
		res::ResourcePath baseMaterial = res::ResourcePath::Build( "base\\materials\\pbr_layer.remt" );

		auto layerDesc = red::CreateSharedPtr< MaterialLayerDesc >( name, path );
		auto pbrLayerMaterial = Cast< CMaterialTemplate >( GResourceLoader->IssueLoadingRequest( baseMaterial )->WaitUntilLoaded() );
		auto materialInstance = Cast< CMaterialInstance >( GResourceLoader->IssueLoadingRequest( resourcePath )->WaitUntilLoaded() );
		materialInstance->SetBaseMaterial( Cast< IMaterial >( pbrLayerMaterial ) );
		layerDesc->SetResource( materialInstance );
		// save base materials
		GetBackend()->SaveResource( resourcePath );
		m_layersMap[name] = layerDesc;
		m_materialLibrary->MarkModified();

		for ( auto listener : m_listeners )
		{
			listener->OnCreateNode( layerDesc );
		}
		//Rescan();
	}

	void MaterialLibraryDataProvider::RemoveLayer( const String& name )
	{
		// Slow
		auto& layers = m_materialLibrary->GetMaterialLayers();
		CName cname = RED_NAME( name );
		for ( Uint32 i = 0; i < layers.Size(); ++i )
		{
			if ( layers[i].m_name == cname )
			{
				layers.RemoveAt( i, i + 1 );
				m_materialLibrary->MarkModified();

				for ( auto listener : m_listeners )
				{
					listener->OnRemoveNode( name );
				}
				return;
			}
		}

	}

	void MaterialLibraryDataProvider::Rescan()
	{
		const auto& layers = m_materialLibrary->GetMaterialLayers();
		String category = "global";
		m_categoriesMap.Clear();
		m_layersMap.Clear();
		for ( auto l : layers )
		{
			red::DynArray< String > files;
			auto& filePath = l.m_material.GetPath();
			auto layerDesc = red::CreateSharedPtr< MaterialLayerDesc >( l.m_name.AsChar(), filePath.ToString() );
			m_categoriesMap[category].PushBack( layerDesc );
			m_layersMap[layerDesc->GetName()] = layerDesc;

			for ( auto listener : m_listeners )
			{
				listener->OnCreateNode( layerDesc );
			}
		}
	}

	Bool MaterialLibraryDataProvider::Save()
	{
		Bool success = true;
		for ( auto& category : m_categoriesMap )
		{
			for ( auto& layer : category.Value() )
			{
				if ( layer->IsLoaded() && layer->GetResource()->IsModified() )
				{
					if ( !m_backend->SaveResource( layer->GetResource() ) )
					{
						success = false;
					}
				}
			}
		}
		return success;
	}

	const TMaterialLayersArray  MaterialLibraryDataProvider::GetLayersByCategory( const String& category )
	{
		auto ptr = m_categoriesMap.FindPtr( category );
		if ( ptr == nullptr )
		{
			return red::DynArray< red::SharedPtr< MaterialLayerDesc > >();
		}
		else
		{
			return *ptr;
		}
	}

	MaterialLayerDescPtr MaterialLibraryDataProvider::GetLayerDescByName( const String& name )
	{
		auto layerIter = m_layersMap.Find( name );
		if ( layerIter != m_layersMap.End() )
		{
			return layerIter.Value();
		}
		else
		{
			return nullptr;
		}
	}

	const red::DynArray< String > MaterialLibraryDataProvider::GetCategories()
	{
		red::DynArray< String > keys;
		m_categoriesMap.GetKeys( keys );
		return keys;
	}

	Bool MaterialLibraryDataProvider::UpdateJson( const red::AbsolutePath& absoluteJsonPath, const red::AbsolutePath& matlibAbsolutePath )
	{
		THandle< CMaterialLayerLibrary > library = Cast<CMaterialLayerLibrary>( m_materialLibrary );		

		auto file = fs::RawFileWriter::Create( absoluteJsonPath, false );
		if ( file )
		{
			JSONFileHelper::JSONFileStream fileStream( file.Get() );
			JSONFileHelper::JSONWriter writer( fileStream );

			auto& microblends = library->GetMicroblends();
			auto& materials = library->GetMaterialLayers();
			writer.StartObject();
			{
				writer.String( "material_library" );
				writer.StartObject();
				{
					writer.String( "global_settings" );
					writer.StartObject();
					{
						writer.String( "substance_shelf_folder" );
						writer.String( "surfaces/Substance" );
						writer.String( "layer_defaults" );
						writer.StartObject();
						{
							writer.String( "uv" );
							writer.Double( library->GetMatTiling() );
							writer.String( "mb" );
							writer.Double( library->GetMicroblendTiling() );
							writer.String( "contrast" );
							writer.Double( library->GetMicroblendContrast() );
							writer.String( "palette_color_index" );
							writer.Uint( library->GetPaletteColorIndex() );
						}
						writer.EndObject();
					}
					writer.EndObject();
					writer.String( "microblend_sets" );
					writer.StartArray();
					{
						writer.StartObject();
						{
							writer.String( "microblend_set_name" );
							writer.String( "global" );
							writer.String( "microblends" );
							writer.StartArray();
							{
								for ( auto& m : microblends )
								{
									writer.String( m.m_name.AsChar() );
								}
							}
							writer.EndArray();
						}
						writer.EndObject();
					}
					writer.EndArray();
					writer.String("microblends");
					writer.StartObject();
					{
						for ( auto& m : microblends )
						{
							writer.String( m.m_name.AsChar() );
							writer.StartObject();
							{
								WriteMicroblendsPath( writer, m.m_texture.GetPath() );
							}
							writer.EndObject();
						}
					}
					writer.EndObject();
					writer.String( "material_sets" );
					writer.StartArray();
					{
						writer.StartObject();
						{
							writer.String( "material_set_name" );
							writer.String( "global" );
							writer.String( "materials" );
							writer.StartArray();
							{
								for ( auto& m : materials )
								{
									writer.String( m.m_name.AsChar() );
								}
							}
							writer.EndArray();
						}
						writer.EndObject();
					}
					writer.EndArray();

					writer.String( "materials" );
					writer.StartObject();
					{
						for ( auto& m : materials )
						{
							writer.String( m.m_name.AsChar() );
							writer.StartObject();
							{
								writer.String( "color_palette" );
								writer.StartArray();
								{
									for ( auto& colorValue : m.m_colorPalette )
									{
										writer.StartArray();
										{
											writer.Double( colorValue.R );
											writer.Double( colorValue.G );
											writer.Double( colorValue.B );
										}
										writer.EndArray();
									}
								}
								writer.EndArray();								
																
								WriteCategoryTag(writer, m.m_material);

								TResRef<ITexture> texture;
								WriteTexturePath( writer, m.m_material, "Normal", "texture_normal" );
								WriteTexturePath( writer, m.m_material, "RoughMetalBlend", "texture_rough_metal" );
								WriteTexturePath( writer, m.m_material, "Diffuse", "texture_diffuse" );
								const red::AbsolutePath thumbnailPath = GetThumbnailPath( m.m_material.GetPath(), CName::NONE() );
								String relativeThumbnailPath = GetThumbnailRelativePath( matlibAbsolutePath, thumbnailPath );
								ConvertPaths( relativeThumbnailPath );

								writer.String( "thumbnail" );
								writer.String( relativeThumbnailPath.AsChar() );
							}
							writer.EndObject();
						}
					}
					writer.EndObject();
				}
				writer.EndObject();
			}
			writer.EndObject();
			return true;
		}
		return false;
	}

	void MaterialLibraryDataProvider::WriteCategoryTag(JSONFileHelper::JSONWriter& writer, TResRef< CMaterialInstance >& material)
	{
		String categoryTag = GetTagsFromMaterial(material);		

		writer.String( "materialType" );
		writer.String( categoryTag.AsChar() );
	}

	void MaterialLibraryDataProvider::WriteTexturePath(JSONFileHelper::JSONWriter& writer, TResRef< CMaterialInstance >& material, const String& propName, const String& keyName)
	{
		CName cname = RED_NAME( propName );
		TResRef< ITexture > texture;
		String path;
		Bool textureFound = false;

		THandle< CMaterialInstance > materialInstance = material.Get();
		if( materialInstance != nullptr )
		{
			materialInstance->ReadParameter( cname, texture );
			path = texture.GetPath().ToString();

			if ( GetResourceDepot()->ResourceExists( texture.GetPath() ) )
			{
				// currently there is no way to get texture source path, no metadata for them?
				String depotStr = path.StringBefore( "surfaces" );
				path = path.StringAfter( depotStr );
				textureFound = true;
			}
		}
		
		if( !textureFound )
		{
			RED_LOG_ERROR("Matlib error! Texture not found: %hs", path.AsChar());

			// hook up correct debug texture based on the missing type
			Uint32 foundAtIndex = 0;
			String debugTexTypeAppendinx = "_d";
			
			if( !path.Empty() )
			{
				if (path.IndexOf( "_n", foundAtIndex )) debugTexTypeAppendinx = "_n";
				else if (path.IndexOf( "_rm", foundAtIndex )) debugTexTypeAppendinx = "_rm";
			}

			// fallback to debug texture for the .json
			path = "\\surfaces\\materials\\debug\\multilayer_debug" + debugTexTypeAppendinx + ".xbm";
		}

		ConvertPaths(path);
		red::utils::ReplaceExtension(path, "tif");

		writer.String( keyName.AsChar() );
		writer.String( path.AsChar() );
	}

	void MaterialLibraryDataProvider::WriteMicroblendsPath( JSONFileHelper::JSONWriter& writer, const res::ResourcePath& resPath )
	{
		// currently there is no way to get texture source path, no metadata for them?
		String path = resPath.ToString().StringAfter("base\\");
		red::paths::SetExtension( path, "tif" );
		ConvertPaths( path );

		writer.String( "texture" );
		writer.String( path.AsChar() );
	}

	String MaterialLibraryDataProvider::GetThumbnailRelativePath( const red::AbsolutePath& rootPath, const red::AbsolutePath& thumbnailPath )
	{
		const auto rootPathStr = rootPath.AsStringView();
		const auto thumbPathStr = thumbnailPath.AsStringView();

		Uint32 indx = 0;
		Uint32 len = Min(rootPathStr.Length(), thumbPathStr.Length());

		Uint32 i;
		for ( i = 0; i < len && rootPathStr[i] == thumbPathStr[i]; i++ )
		{}

		auto pathDelta = rootPathStr.SubView( i );
		Uint32 dirDepth = 0;
		for ( Uint32 j = 0; j < pathDelta.Length(); j++ )
		{
			if ( pathDelta[j] == '\\' )
			{
				dirDepth++;
			}
		}

		// Generate relative path
		String relativePath;
		for ( Uint32 k = 0; k < dirDepth; k++ )
		{
			relativePath += "..\\";
		}

		auto relativeThumbPath = thumbPathStr.SubView( i );
		relativePath.Append( relativeThumbPath.Data(), relativeThumbPath.Length() );
		return relativePath;
	}

	void MaterialLibraryDataProvider::ConvertPaths( String& path )
	{
		path.ReplaceAll( "\\", "/" );
		if (!path.BeginsWith("/")) path = "/" + path;
	}

	String MaterialLibraryDataProvider::GetTagsFromMaterial( TResRef< CMaterialInstance >& material )
	{	
		if (material.Get() != nullptr)
		{
			TResRef< ITexture > texture;

			CName cname = RED_NAME_CONSTEXPR("Diffuse");
			material.Get()->ReadParameter(cname, texture);
			if (texture.Get() != nullptr)
			{
				return GetCategoryFromPath( texture->GetPath() );
			}

			cname = RED_NAME_CONSTEXPR("Normal");
			material.Get()->ReadParameter(cname, texture);
			if (texture.Get() != nullptr)
			{
				return GetCategoryFromPath( texture->GetPath() );
			}

			cname = RED_NAME_CONSTEXPR("RoughMetalBlend");
			material.Get()->ReadParameter(cname, texture);
			if (texture.Get() != nullptr)
			{
				return GetCategoryFromPath( texture->GetPath() );
			}
		}

		return "debug";
	}

	String MaterialLibraryDataProvider::GetCategoryFromPath( const res::ResourcePath& resourcePath )
	{
		String category = resourcePath.ToString().StringAfter( "surfaces\\materials\\" );
		Uint32 endOfCategoryIdx;
		if ( category.IndexOf( "\\", endOfCategoryIdx ) )
		{
			return category.LeftString( endOfCategoryIdx );
		}
		else
		{
			return "debug";
		}
	}

	MaterialLayerDesc::MaterialLayerDesc( const String& name, const String& path )
		: m_name( name )
		, m_path( path )
		, m_resource()
	{
		m_tags.PushBack( "global" );
	}

	THandle< CMaterialInstance >& MaterialLayerDesc::GetResource()
	{
		if ( !m_resource )
		{
			Reload();
		}
		return m_resource;
	}

	void MaterialLayerDesc::Reload()
	{
		res::ResourcePath resourcePath = res::ResourcePath::Build( m_path );
		auto res = GResourceLoader->IssueLoadingRequest( resourcePath )->WaitUntilLoaded();
		m_resource = Cast< CMaterialInstance >( res );
	}

}