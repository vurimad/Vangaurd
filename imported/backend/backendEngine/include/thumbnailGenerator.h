/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

#pragma once

#include "backendMemory.h"
#include "thumbnailRendering.h"

namespace tools
{
	class ResourceBackend;
	class EnvironmentPresetsManager;
}

namespace interop
{
	class InteropNotifier;
}

namespace img
{
	class Image;
	typedef red::SharedPtr<Image> ImagePtr;
}

namespace res
{
	struct ThumbGenContext
	{
		tools::EnvironmentPresetsManager& m_presetsManager;
		Uint32 m_size;
		CName m_prefferedAppearanceName;
		WorldTransform m_transform;
		ThumbnailPresentation m_presentation;
	};

	class BACKEND_ENGINE_API IThumbnailGenerator
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		RTTI_DECLARE_POLYMORPHIC_TYPE( IThumbnailGenerator );

	public:
		IThumbnailGenerator()
			: m_formatsExtensions{ red::PoolBackend() }
		{}

		virtual ~IThumbnailGenerator();

		// Default view direction for generating thumbs with this generator
		virtual ThumbnailPresentation PresentationDefaults() const;

		// Generate thumbnail
		virtual void Generate( const THandle< CResource >& resource, ThumbGenContext& ctx, red::SharedPtr<rend::ColorResult>& result ) = 0;

		// Returns false if thumbnails must be generated manually (using context menu option) for this type of resource
		virtual Bool CanAutoGenerate() const;

		// Check support for resource format
		Bool SupportsFormat( const String& formatExtension ) const;

		// Reset all the generators to initial state
		// Note: this is used to avoid memory leaks
		static void ResetAllGenerators();

		// Find generator by resource extension
		static IThumbnailGenerator* FindGenerator( const String& extension );

		// Enumerate all resource extensions that are supported by thumbnails generators
		static void EnumGenerationFormats( red::DynArray< String >& formatsExtensions );

	protected:

		// called when internal generate state should be reset
		virtual void OnReset();

		red::DynArray< String > m_formatsExtensions; // Resource extensions this generator supports
	};

	//////////////////////////////////////////////////////////////////////////
	// Thumbnail Generation
	//////////////////////////////////////////////////////////////////////////

	BACKEND_ENGINE_API Uint32 ThumbnailSize();
	BACKEND_ENGINE_API Uint32 IconSize();

	BACKEND_ENGINE_API Bool CanGenerateThumbnail( const res::ResourcePath& resourcePath );
	BACKEND_ENGINE_API Bool ShouldAutoGenerateThumbnail( const res::ResourcePath& resourcePath );
	BACKEND_ENGINE_API Bool IsThumbnailInCacheUpToDate( const res::ResourcePath& resourcePath, CName appearanceName );
	BACKEND_ENGINE_API Bool IsThumbnailInResourceUpToDate( const THandle< CResource >& resource );

	BACKEND_ENGINE_API Bool UpdateThumbnailInResource( const THandle< CResource >& resource, tools::EnvironmentPresetsManager& envPresetsManager, CName prefferedAppearanceName = CName::NONE() );
	BACKEND_ENGINE_API Bool UpdateThumbnailInCache( const THandle< CResource >& resource, CName appearanceName, interop::InteropNotifier& notifier );
	BACKEND_ENGINE_API Bool GenerateIcon(const THandle< CResource >& resource, tools::EnvironmentPresetsManager& envPresetsManager, const red::AbsolutePath& path, const Vector3& camForward = Vector3::EY(), const Vector3& camUp = Vector3::EZ() );

	// Private implementation functions that are also used in commandlets
	namespace prv
	{
		BACKEND_ENGINE_API img::ImagePtr BuildThumbnailImage( const void* imageData, Uint32 width, Uint32 height );
		BACKEND_ENGINE_API img::ImagePtr GenerateThumbnail( const res::ResourcePath& resourcePath, const THandle< CResource >& resource, tools::EnvironmentPresetsManager& envPresetsManager, CName prefferedAppearanceName = CName::NONE() );
		BACKEND_ENGINE_API Bool SaveThumbnailToCache( img::ImagePtr image, const red::AbsolutePath& path );
		BACKEND_ENGINE_API void ScheduleSaveThumbnailToCache( const img::ImagePtr& image, const res::ResourcePath& path, CName appearanceName, interop::InteropNotifier& notifier );

		// Helper function that ensures the top level thumbs directory exists (and create it properly if not)
		BACKEND_ENGINE_API Bool EnsureThumbnailCacheDirExists();
	}

} // res