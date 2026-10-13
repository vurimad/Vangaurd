/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

#include "build.h"
#include "thumbnailGenerator.h"
#include "resourceMetadata.h"

#include "../../backendEntities/include/thumbnailCamera.h"
#include "../../../common/redReflection/include/resourceDepot.h"
#include "../../../common/renderData/include/renderSelectionResult.h"
#include "../../../common/redFileSystem/include/rawFileWriter.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/redReflection/include/resourceUtils.h"
#include "../../../common/redReflection/include/resource.h"
#include "../../../common/redJobs2/include/jobBuilder.h"


RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( IThumbnailGenerator, res );
RTTI_END_TYPE();

namespace res
{

	IThumbnailGenerator::~IThumbnailGenerator() = default;

	ThumbnailPresentation IThumbnailGenerator::PresentationDefaults() const
	{
		return ThumbnailPresentation();
	}

	Bool IThumbnailGenerator::CanAutoGenerate() const
	{
		return true;
	}

	Bool IThumbnailGenerator::SupportsFormat( const String& formatExtension ) const
	{
		for ( const String& ext : m_formatsExtensions )
		{
			if ( ext.EqualsNC( formatExtension ) )
				return true;
		}
		return false;
	}

	void IThumbnailGenerator::ResetAllGenerators()
	{
		// Request generators classes
		red::DynArray< const rtti::ClassType* > generatorClasses{ red::PoolBackend() };
		GetRttiSystem().EnumClasses( ClassID<IThumbnailGenerator>(), generatorClasses );

		for ( Uint32 i = 0; i < generatorClasses.Size(); i++ )
		{
			IThumbnailGenerator* generator = generatorClasses[i]->GetDefaultObject< IThumbnailGenerator >();
			generator->OnReset();
		}
	}

	namespace prv
	{

	red::DynArray< IThumbnailGenerator* > EnumerateThumbnailGenerators()
	{
		// Request generators classes
		red::DynArray< const rtti::ClassType* > generatorClasses{ red::PoolBackend() };
		GetRttiSystem().EnumClasses( ClassID<IThumbnailGenerator>(), generatorClasses );

		red::DynArray< IThumbnailGenerator* > generators;
		generators.Reserve( generatorClasses.Size() );

		for ( Uint32 i = 0; i < generatorClasses.Size(); i++ )
		{
			IThumbnailGenerator* generator = generatorClasses[i]->GetDefaultObject< IThumbnailGenerator >();
			if ( generator )
			{
				generators.PushBack( generator );
			}
		}

		return generators;
	}

	} // prv

	IThumbnailGenerator* IThumbnailGenerator::FindGenerator( const String& extension )
	{
		static red::DynArray< IThumbnailGenerator* > thumbnailGenerators{ prv::EnumerateThumbnailGenerators() };

		for ( IThumbnailGenerator* generator : thumbnailGenerators )
		{
			if ( generator->SupportsFormat( extension ) )
			{
				return generator;
			}
		}

		return nullptr;
	}

	void IThumbnailGenerator::EnumGenerationFormats( red::DynArray< String >& formatsExtensions )
	{
		static red::DynArray< IThumbnailGenerator* > thumbnailGenerators{ prv::EnumerateThumbnailGenerators() };

		for ( IThumbnailGenerator* generator : thumbnailGenerators )
		{
			if ( generator && !generator->m_formatsExtensions.Empty() )
			{
				red::alg::PushBackUnique( formatsExtensions, generator->m_formatsExtensions.Begin(), generator->m_formatsExtensions.End() );
			}
		}
	}

	void IThumbnailGenerator::OnReset()
	{
	}

	//////////////////////////////////////////////////////////////////////////
	// ThumbnailGeneratorManager
	//////////////////////////////////////////////////////////////////////////


	Uint32 ThumbnailSize()
	{
		static constexpr Uint32 s_thumbnailSize = 256;
		return s_thumbnailSize;
	}

	Uint32 IconSize()
	{
		static constexpr Uint32 s_iconSize = 1024;
		return s_iconSize;
	}

	Bool CanGenerateThumbnail( const res::ResourcePath& resourcePath )
	{
		String extension = red::paths::ExtractExtension( resourcePath.ToStringView() );
		IThumbnailGenerator* generator = IThumbnailGenerator::FindGenerator( extension );
		return generator != nullptr;
	}

	Bool ShouldAutoGenerateThumbnail( const res::ResourcePath& resourcePath )
	{
		String extension = red::paths::ExtractExtension( resourcePath.ToStringView() );
		if ( IThumbnailGenerator* generator = IThumbnailGenerator::FindGenerator( extension ) )
		{
			return generator->CanAutoGenerate();
		}

		return false;
	}

	bool IsThumbnailInCacheUpToDate( const res::ResourcePath& resourcePath, CName appearanceName )
	{
		red::AbsolutePath thumbnailPath = res::GetThumbnailPath( resourcePath, appearanceName );
		if ( GFileManager->FileExist( thumbnailPath ) )
		{
			// Check that the filename is up to date
			if ( GetResourceDepot()->GetResourceTimestamp( resourcePath ) <= GFileManager->GetFileTimeStamp( thumbnailPath ) )
			{
				return true;
			}
		}

		return false;
	}

	Bool IsThumbnailInResourceUpToDate( const THandle<CResource>& resource )
	{
		// NOTE: Since we save thumbnails for all resources on save, this just checks that a resource has a thumbnail
		// Previously this was just returning false with a todo for later, but it was changed for CYB-257581.
		const auto& metadata = ResourceMetadata::Get( resource );
		return metadata && metadata->HasThumbnail();
	}

	Bool GenerateThumbnailInResource( const THandle<CResource>& resource, tools::EnvironmentPresetsManager & envPresetsManager, CName prefferedAppearanceName )
	{
		if ( resource != nullptr )
		{
			const res::ResourcePath& resourcePath = resource->GetPath();

			img::ImagePtr image = prv::GenerateThumbnail( resourcePath, resource, envPresetsManager, prefferedAppearanceName );

			if ( image && image->GetPixelBufferSize() != 0 )
			{
				ResourceMetadata::SetThumbnail( resource, image );
				return true;
			}
		}
		return false;
	}

	Bool UpdateThumbnailInResource( const THandle<CResource>& resource, tools::EnvironmentPresetsManager& envPresetsManager, CName prefferedAppearanceName )
	{
		RED_FATAL_ASSERT( resource != nullptr );

		// check if thumbnail is stored in resource metadata
		return GenerateThumbnailInResource( resource, envPresetsManager, prefferedAppearanceName );
	}

	Bool UpdateThumbnailInCache( const THandle< CResource >& resource, CName appearanceName, interop::InteropNotifier& notifier )
	{
		RED_FATAL_ASSERT( resource != nullptr );

		THandle<res::ResourceMetadata> meta = ResourceMetadata::Get( resource );
		if ( meta && meta->HasThumbnail() )
		{
			img::ImagePtr image = prv::BuildThumbnailImage( meta->GetImageData(), ThumbnailSize(), ThumbnailSize() );
			if ( image && image->GetPixelBufferSize() != 0 )
			{
				prv::ScheduleSaveThumbnailToCache( image, resource->GetPath(), appearanceName, notifier );
				return true;
			}
		}

		return false;
	}

	Bool GenerateIcon(const THandle< CResource >& resource, tools::EnvironmentPresetsManager& envPresetsManager, const red::AbsolutePath& path, const Vector3& camForward, const Vector3& camUp)
	{
		Bool result = false;
		if ( resource )
		{
			const ResourcePath& resourcePath = resource->GetPath();
			const String extension = red::paths::ExtractExtension( resourcePath.ToStringView() );
			IThumbnailGenerator*const generator = IThumbnailGenerator::FindGenerator( extension );
			if ( generator )
			{
				RED_LOG( "ICON: Generating '%s'.", resourcePath.ToDebugString() );

				auto colorResult = red::CreateSharedPtr<rend::ColorResult>( red::PoolBackend() );

				res::ThumbnailPresentation presentation;
				presentation.forward = camForward;
				presentation.up = camUp;
				presentation.fov = 0.f;

				res::ThumbGenContext ctx{ envPresetsManager, IconSize(), CName::NONE(), WorldTransform::IDENTITY(), presentation };
				generator->Generate( resource, ctx, colorResult );

				const Bool validThumb = colorResult->GetWidth() != 0 && colorResult->GetHeight() != 0;
				if ( validThumb )
				{
					const auto image = res::prv::BuildThumbnailImage( colorResult->GetColors(), colorResult->GetWidth(), colorResult->GetHeight() );

					GFileManager->CreatePath( red::paths::ParentAbsolutePath( path ) );
					
					res::prv::SaveThumbnailToCache( image, path );
					result = true;
				}
				else
				{
					RED_LOG( "ICON: Generated icon was invalid!" );
				}
			}
			else
			{
				RED_LOG( "ICON: No generator could be found for resource with extension '%s'.", extension );
			}
		}
		else
		{
			RED_LOG( "ICON: Given invalid resource.");
		}
	
		return result;
	}


	namespace prv
	{
		img::ImagePtr BuildThumbnailImage( const void* imageData, Uint32 width, Uint32 height )
		{
			const img::Image::SizeInfo imageSizeInfo{ 4, static_cast<Uint16>( width ), static_cast<Uint16>( height ) };
			img::ImagePtr image = img::Image::CreateEmpty( img::PixelFormat::Format_Uint8, imageSizeInfo );
			red::Memcpy( image->GetPixelBuffer(), imageData, image->GetPixelBufferSize() );
			return image;
		}

		img::ImagePtr GenerateThumbnail( const res::ResourcePath& resourcePath, const THandle< CResource >& resource, tools::EnvironmentPresetsManager& envPresetsManager, CName prefferedAppearanceName )
		{
			String extension = red::utils::ExtractExtension( resourcePath.ToString() );

			IThumbnailGenerator* generator = IThumbnailGenerator::FindGenerator( extension );
			if ( !generator )
			{
				RED_LOG_WARNING( "Can't create thumbnail for type: [%hs]", extension.AsChar() );
				return img::ImagePtr();
			}

			// generate thumbnail...
			auto colorResult = red::CreateSharedPtr<rend::ColorResult>( red::PoolBackend() );
			ThumbGenContext ctx{ envPresetsManager, ThumbnailSize(), prefferedAppearanceName };
			ctx.m_presentation = generator->PresentationDefaults();

			generator->Generate( resource, ctx, colorResult );
			if ( colorResult->GetHeight() == 0 || colorResult->GetWidth() == 0 )
			{
				RED_LOG_WARNING( "Thumbnail generation failed for resource: [%hs]", resourcePath.ToDebugString() );
				return img::ImagePtr();
			}

			const Color* colorsBuffer = colorResult->GetColors();
			const Uint32 size = colorResult->GetWidth() * colorResult->GetHeight();
			const Uint32 minimumFilledPixelCount = size / 100; // 1% needs to be filled to be a valid thumbnail
			Uint32 filledPixelCount = 0;

			for ( Uint32 i = 0; i < size; ++i )
			{
				filledPixelCount += colorsBuffer[i].A != 0;
				if ( filledPixelCount > minimumFilledPixelCount )
				{
					break;
				}
			}

			if ( filledPixelCount < minimumFilledPixelCount )
			{
				float percentageBlank = static_cast<float>(filledPixelCount) / size;
				RED_LOG_WARNING( "Thumbnail generated for resource: [%hs] is mostly (%.2f) blank",
					resourcePath.ToDebugString(), percentageBlank );
				return img::ImagePtr();
			}

			return prv::BuildThumbnailImage( colorResult->GetColors(), colorResult->GetWidth(), colorResult->GetHeight() );
		}

		bool SaveThumbnailToCache( img::ImagePtr image, const red::AbsolutePath& path )
		{
			img::GenericWriter writer;
			writer.Reserve( image->GetPixelBufferSize() );
			if ( img::BaseImageSaver::Save( writer, image, "png" ) )
			{
				RED_FATAL_ASSERT( writer.GetSize() == writer.GetOffset() );
				if ( writer.GetSize() > 0 )
				{
					auto fileWriter = fs::RawFileWriter::Create( path, false );
					if ( fileWriter )
					{
						fileWriter->Serialize( writer.GetData(), writer.GetSize() );
						return true;
					}
				}
			}

			return false;
		}

		void ScheduleSaveThumbnailToCache( const img::ImagePtr& image, const res::ResourcePath& path, CName appearanceName, interop::InteropNotifier& notifier )
		{
			job::Builder builder;
			builder.DispatchJob( "ThumbnailsGeneration", [notifier, image, path, appearanceName](const job::RunContext&)
			{
				red::AbsolutePath outputPath = res::GetThumbnailPath( path, appearanceName );
				if ( SaveThumbnailToCache( image, outputPath ) )
				{
					notifier.Notify( String::Printf( "%hs;%hs", path.ToString().AsChar(), appearanceName.AsChar() ) );
				}
				else
				{
					RED_LOG_WARNING( "Failed to save cached thumbnail for %hs", path.ToString().AsChar() );
				}
			} );
		}

		bool EnsureThumbnailCacheDirExists()
		{
			red::AbsolutePath rootThumbPath = GFileManager->GetCacheDirectory().AddDirPath( "thumbs" );
			if ( GFileManager->FileExist( rootThumbPath ) )
			{
				return true;
			}

			if ( GFileManager->CreatePath( rootThumbPath ) )
			{
				auto wcharPath = rootThumbPath.ToUtf16String();
				DWORD attr = ::GetFileAttributesW( wcharPath.AsChar() );

				if ( ( attr & FILE_ATTRIBUTE_HIDDEN ) == 0 )
				{
					if ( ::SetFileAttributesW( wcharPath.AsChar(), attr | FILE_ATTRIBUTE_HIDDEN ) != 0 )
					{
						RED_LOG_WARNING( "Failed to set the thumbnail cache directory %hs to hidden", rootThumbPath.AsChar() );
					}
				}

				return true;
			}

			return false;
		}

	} // prv

} //res
