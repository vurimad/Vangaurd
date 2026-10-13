/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#pragma once


#include "../../../common/redReflection/include/resourcePath.h"
#include "../../../common/redReflection/include/backendData.h"
#include "../../../common/core/include/tagList.h"

class ResourceImportOptions;

namespace img
{
	class Image;
	typedef red::SharedPtr<Image> ImagePtr;
}

namespace res
{
	class ResourceImportMetadata;

	struct BACKEND_ENGINE_API AnimationMetadata
	{
		RTTI_DECLARE_TYPE( AnimationMetadata );

		CName m_animationName;
		red::TagList m_tags;
	};

	class BACKEND_ENGINE_API ResourceMetadata : public ISerializable
	{
		RTTI_DECLARE_TYPE( ResourceMetadata );

	public:
		ResourceMetadata();
		virtual ~ResourceMetadata() override;

		RED_INLINE Bool HasThumbnail() const { return !m_imageData.Empty(); }
		RED_INLINE const Uint8* GetImageData() const { return m_imageData.TypedData(); }

		RED_INLINE const String& GetDescription() const { return m_description; }

		typedef red::DynArray<CName> TFileTags;
		RED_INLINE const TFileTags& GetTags() const { return m_tags; }
		RED_INLINE TFileTags& GetTags() { return m_tags; }

		RED_INLINE const THandle<ResourceImportMetadata>& GetImportData() const { return m_importData; }

		void SetThumbnail( const img::ImagePtr& image );
		void ClearThumbnail();
		void SetDescription( const String& desc );
		void SetTags( const TFileTags& tags );
		void SetAnimationTags( const CName& animation, const red::TagList& tags );
		void SetAnimationTags( const CName& animation, red::TagList&& tags );
		void SetImportData( const THandle<ResourceImportMetadata>& data );

		Bool HasAnimationMetadata() const;
		const red::DynArray< res::AnimationMetadata >& GetAnimationMetadata() const;
		AnimationMetadata& GetAnimationMetadata( const CName& animationName );

		static THandle<res::ResourceMetadata> Get( const res::ResourcePath& path, Bool forceLoadIfNecessary = false, IResourceDepot* depot = nullptr );
		static THandle<res::ResourceMetadata> Get( const THandle<CResource>& resource );

		static void Set( THandle<CResource> resource, const THandle<res::ResourceMetadata> metadataHandle );
		static void SetThumbnail( THandle<CResource> resource, const img::ImagePtr& image );
		static void ClearThumbnail( THandle<CResource> resource );
		static void SetDescription( THandle<CResource> resource, const String& description );
		static void SetTags( THandle<CResource> resource, const TFileTags& newTags );
		static void SetAnimationTags( THandle<CResource> resource, const CName& animation, const red::TagList& tags );
		static void SetAnimationTags( THandle<CResource> resource, const CName& animation, red::TagList&& tags );
		static void SetAnimationMetadata( THandle<CResource> resource, const red::DynArray< AnimationMetadata >& metadata );
		static void SetAnimationMetadata( THandle<CResource> resource, red::DynArray< AnimationMetadata >&& metadata );
		static void SetImportData( THandle<CResource> resource, const THandle<ResourceImportMetadata>& data );
		static void SetImportOptions( const THandle<CResource>& resource, const THandle<ResourceImportOptions>& options );

	private:
		static THandle<res::ResourceMetadata> GetOrCreate( const THandle<CResource>& resource );

		red::DynArray<Uint8> m_imageData;
		String m_description;
		TFileTags m_tags;
		red::DynArray< AnimationMetadata > m_animationMetadata;
		THandle<ResourceImportMetadata> m_importData;
	};
} // namespace res
