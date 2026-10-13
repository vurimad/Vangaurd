/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "resourceMetadata.h"
#include "resourceImportMetadata.h"
#include "resourceUtils.h"

#include "../../../common/redReflection/include/resourceDepot.h"
#include "../../../common/redReflection/include/serializationBinaryUtilities.h"
#include "../../../common/redReflection/include/serializationAsyncSource.h"
#include "../../../common/redReflection/include/serializationLoader.h"
#include "../../../common/redFileSystem/include/memoryFileWriter.h"
#include "../../../common/redReflection/include/serializationSaver.h"
#include "../../../common/redImage/include/image.h"
#include "../../../common/redReflection/include/deferredDataBuffer.h"
#include "../../../common/redReflection/include/resource.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( AnimationMetadata, res );
	RTTI_PROPERTY( m_animationName ).editable();
	RTTI_PROPERTY( m_tags ).editable();
RTTI_END_TYPE();


RTTI_BEGIN_TYPE_IN_NAMESPACE( ResourceMetadata, res );
	RTTI_PARENT_TYPE( ISerializable );
	RTTI_PROPERTY( m_imageData );
	RTTI_PROPERTY( m_description ).editable();
	RTTI_PROPERTY( m_tags ).editable().inlined().resizable();
	RTTI_PROPERTY( m_animationMetadata ).editable();
	RTTI_PROPERTY( m_importData ).readOnly();
RTTI_END_TYPE();

namespace res
{
	static THandle<res::ResourceMetadata> LoadMetadataFromBuffer( const serialization::BufferHandleRO& buffer )
	{
		if ( buffer.Data() )
		{
			serialization::LoadingContext context;
			serialization::LoadingResult result;

			if ( serialization::LoadFromMemory( buffer.Data(), buffer.GetSize(), context, result ) )
			{
				THandle<ISerializable> loadedObject = result.m_loadedRootObjects.Empty() ? nullptr : result.m_loadedRootObjects[0];
				if ( loadedObject && loadedObject->IsA<ResourceMetadata>() )
				{
					return Cast<res::ResourceMetadata >( loadedObject );
				}
			}
		}

		return nullptr;
	}

	ResourceMetadata::ResourceMetadata() = default;
	ResourceMetadata::~ResourceMetadata() = default;

	void ResourceMetadata::SetThumbnail( const img::ImagePtr& image )
	{
		if ( image->GetPixelFormat() != img::PixelFormat::Format_Uint8 )
			return;

		Uint32 size = image->GetPixelBufferSize();
		m_imageData.Resize( size );
		red::Memcpy( m_imageData.TypedData(), image->GetPixelBuffer(), size );
	}

	void ResourceMetadata::ClearThumbnail()
	{
		m_imageData.Clear();
	}

	void ResourceMetadata::SetDescription( const String& desc )
	{
		m_description = desc;
	}

	void ResourceMetadata::SetTags( const TFileTags& tags )
	{
		m_tags = tags;
	}
	
	void ResourceMetadata::SetAnimationTags( const CName& animation, const red::TagList& tags )
	{
		AnimationMetadata& metadata = GetAnimationMetadata( animation );
		metadata.m_tags = tags;
	}

	void ResourceMetadata::SetAnimationTags( const CName& animation, red::TagList&& tags )
	{
		AnimationMetadata& metadata = GetAnimationMetadata( animation );
		metadata.m_tags = std::move( tags );
	}

	void ResourceMetadata::SetImportData( const THandle<ResourceImportMetadata>& data )
	{
		m_importData = data;
	}

	Bool ResourceMetadata::HasAnimationMetadata() const
	{
		for ( const AnimationMetadata& item : m_animationMetadata )
		{
			if ( item.m_tags.Size() > 0 )
				return true;
		}

		return false;
	}

	AnimationMetadata& ResourceMetadata::GetAnimationMetadata( const CName& animationName )
	{
		for ( AnimationMetadata& metadata : m_animationMetadata )
		{
			if ( metadata.m_animationName == animationName )
				return metadata;
		}

		AnimationMetadata metadata;
		metadata.m_animationName = animationName;
		m_animationMetadata.PushBack( std::move( metadata ) );

		return m_animationMetadata.Back();
	}

	const red::DynArray< res::AnimationMetadata >& ResourceMetadata::GetAnimationMetadata() const
	{
		return m_animationMetadata;
	}

	//----

	THandle< ResourceMetadata > ResourceMetadata::Get( const ResourcePath& path, Bool forceLoadIfNecessary, IResourceDepot* depot )
	{
		#if !defined( VANGUARD_ASSET_HOST )
			// Quick Hackfix to prevent crashing due to json/BINK resources being created
			if( IsJsonResource( path ) || IsBinkResource( path ) )
			{
				return nullptr;
			}
		#endif

		if ( GResourceLoader->IsResourceLoaded( path ) )
		{
			TResRef< CResource > resourceRef( path );
			return Get( resourceRef.Get() );
		}

		if( depot == nullptr )
		{
			depot = GetResourceDepot();
		}

		red::SharedPtr< serialization::IAsyncSource > asyncSource = depot->CreateResourceAsyncSource( path );
		if ( asyncSource )
		{
			auto buffer = serialization::LoadMetadataBuffer( *asyncSource );
			if ( buffer.Data() )
			{
				return LoadMetadataFromBuffer( buffer );
			}
		}

 		if ( forceLoadIfNecessary )
 		{
 			TResRef< CResource > resRef( path );
 			if ( THandle< CResource > resource = resRef.Get() )
 			{
 				return Get( resource );
 			}
 		}
		return nullptr;
	}

	// When will it get called exactly? E.g., if updating some resource that's already opened...?
	// GetOrCreate kind of dodgy
 	THandle<res::ResourceMetadata> ResourceMetadata::Get( const THandle< CResource>& resource )
 	{
 		return LoadMetadataFromBuffer( resource->GetMetadataBuffer().Editor_LoadContentDirect() );
 	}

	void ResourceMetadata::Set( THandle< CResource> resource, const THandle<res::ResourceMetadata> metadataHandle )
	{
		serialization::DeferredDataBuffer& metadataBuffer = resource->GetMetadataBuffer();
		metadataBuffer.Clear();

		red::DynArray< Uint8 > writeBuffer;
		CMemoryFileWriter writer( writeBuffer );

		serialization::SavingContext savingContext( metadataHandle );
		Bool saveResult = serialization::SaveToMemory( savingContext, writer );
		metadataBuffer.Editor_SetContentDirect( writeBuffer.TypedData(), writeBuffer.DataSize() );

		resource->MarkModified();
	}

	void ResourceMetadata::SetThumbnail( THandle<CResource> resource, const img::ImagePtr& thumbnail )
	{
		THandle<res::ResourceMetadata> metadataHandle = GetOrCreate( resource );
		metadataHandle->SetThumbnail( thumbnail );
		Set( resource, metadataHandle );
	}

	void ResourceMetadata::ClearThumbnail( THandle<CResource> resource )
	{
		THandle<res::ResourceMetadata> metadataHandle = GetOrCreate( resource );
		if ( metadataHandle->HasThumbnail() )
		{
			metadataHandle->ClearThumbnail();
			Set( resource, metadataHandle );
		}
	}

	void ResourceMetadata::SetDescription( THandle<CResource> resource, const String& description )
	{
		THandle<res::ResourceMetadata> metadataHandle = GetOrCreate( resource );
		metadataHandle->SetDescription( description );
		Set( resource, metadataHandle );
	}

	void ResourceMetadata::SetTags( THandle<CResource> resource, const TFileTags& newTags )
	{
		THandle<res::ResourceMetadata> metadataHandle = GetOrCreate( resource );
		metadataHandle->SetTags( newTags );
		Set( resource, metadataHandle );
	}

	void ResourceMetadata::SetAnimationTags( THandle<CResource> resource, const CName& animation, const red::TagList& tags )
	{
		THandle<res::ResourceMetadata> metadataHandle = GetOrCreate( resource );
		metadataHandle->SetAnimationTags( animation, tags );
		Set( resource, metadataHandle );
	}

	void ResourceMetadata::SetAnimationTags( THandle<CResource> resource, const CName& animation, red::TagList&& tags )
	{
		THandle<res::ResourceMetadata> metadataHandle = GetOrCreate( resource );
		metadataHandle->SetAnimationTags( animation, std::move( tags ) );
		Set( resource, metadataHandle );
	}

	void ResourceMetadata::SetAnimationMetadata( THandle<CResource> resource, red::DynArray< AnimationMetadata >&& metadata )
	{
		THandle<res::ResourceMetadata> metadataHandle = GetOrCreate( resource );
		metadataHandle->m_animationMetadata = std::move( metadata );
		Set( resource, metadataHandle );
	}

	void ResourceMetadata::SetAnimationMetadata( THandle<CResource> resource, const red::DynArray< AnimationMetadata >& metadata )
	{
		THandle<res::ResourceMetadata> metadataHandle = GetOrCreate( resource );
		metadataHandle->m_animationMetadata = metadata;
		Set( resource, metadataHandle );
	}

	void ResourceMetadata::SetImportData( THandle<CResource> resource, const THandle<ResourceImportMetadata> & data )
	{
		THandle<res::ResourceMetadata> metadataHandle = GetOrCreate( resource );
		metadataHandle->SetImportData( data );
		Set( resource, metadataHandle );
	}

	void ResourceMetadata::SetImportOptions( const THandle<CResource>& resource, const THandle<ResourceImportOptions>& options )
	{
		THandle< ResourceMetadata > metadataHandle = GetOrCreate( resource );
		THandle< ResourceImportMetadata > importMetadata = metadataHandle->GetImportData();

		if ( !importMetadata )
		{
			importMetadata = CreateHandle< ResourceImportMetadata >();
			metadataHandle->SetImportData( importMetadata );
		}

		importMetadata->SetImportOptions( options );
		Set( resource, metadataHandle );
	}
	
	THandle<res::ResourceMetadata> ResourceMetadata::GetOrCreate( const THandle<CResource>& resource )
	{
		THandle<res::ResourceMetadata> metadataHandle = Get( resource );
		if ( !metadataHandle )
		{
			metadataHandle = CreateHandle<res::ResourceMetadata>();
		}
		return metadataHandle;
	}
}
