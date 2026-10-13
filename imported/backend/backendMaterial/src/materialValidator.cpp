
#include "build.h"
#include "materialValidator.h"

#include "../../../common/resourceMaterial/include/multilayerMask.h"
#include "../../../common/resourceMaterial/include/bitmapTexture.h"
#include "../../../common/redReflection/include/gatheredResource.h"
#include "../../../common/resource/include/2darray.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( MultilayerMaskValidator, tools );
	RTTI_PARENT_TYPE( IValidator );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( BitmapTextureValidator, tools );
	RTTI_PARENT_TYPE( IValidator );
RTTI_END_TYPE();

res::TStaticResource< C2dArray > g_memoryLimits( RED_CONST_RESOURCEPATH( "engine\\editor\\mlm_limits.csv" ), GRF_Backend | GRF_Startup );

namespace tools
{
	MultilayerMaskValidator::MultilayerMaskValidator()
		: IValidator( ClassID< Multilayer_Mask >() )
	{
		for ( Uint32 rowIndex = 0; rowIndex < g_memoryLimits->GetNumberOfRows(); ++rowIndex )
		{
			const String path = g_memoryLimits->GetValue( "path", rowIndex );
			const Float softLimit = FromStringDirect<Float>( g_memoryLimits->GetValue( "softLimit", rowIndex ) );
			const Float hardLimit = FromStringDirect<Float>( g_memoryLimits->GetValue( "hardLimit", rowIndex ) );
			const String unit = g_memoryLimits->GetValue( "unit", rowIndex );

			if ( unit == "MB" )
			{
				m_limits.PushBack( std::make_tuple( path, (Uint32)( softLimit * 1024 * 1024 ), (Uint32)( hardLimit * 1024 * 1024 ) ) );
			}
			else // ( unit == "KB" )
			{
				m_limits.PushBack( std::make_tuple( path, (Uint32)( softLimit * 1024 ), (Uint32)( hardLimit * 1024 ) ) );
			}
		}
	}

	Bool MultilayerMaskValidator::Validate( const THandle<CResource>& resource, tools::IMessageReporter& errorHandler ) const
	{
		const THandle< Multilayer_Mask > maskResource = Cast< Multilayer_Mask >( resource );
		const res::ResourcePath& resourcePath = resource->GetPath();
		red::StringView pathView = resourcePath.ToStringView();
		Bool hasErrors = false;

		for ( const auto& element : m_limits )
		{
			const String& prefix = std::get<0>( element );
			if ( pathView.StartsWith( prefix ) )
			{
				const Uint32 softLimit = std::get<1>( element );
				const Uint32 hardLimit = std::get<2>( element );
				const Uint32 size = maskResource->GetGPUSize();

				if ( hardLimit < size )
				{
					errorHandler.Report( tools::MessageSeverity::Error, resourcePath, RED_LOCATION( resourcePath ), String::Printf( "GPU size hard limit is exceeded. Available %d KB, used %d KB  (%1.2f MB)", hardLimit / 1024, size / 1024, (float)size/(1024 * 1024) ).AsChar() );
					hasErrors = true;
				}
				else if ( softLimit < size )
				{
					errorHandler.Report( tools::MessageSeverity::Warning, resourcePath, RED_LOCATION( resourcePath ), String::Printf( "GPU size soft limit is exceeded. Available %d KB, used %d KB (%1.2f MB)", softLimit / 1024, size / 1024, ( float ) size / ( 1024 * 1024 ) ).AsChar() );
				}

				break;
			}
		}

		return !hasErrors;
	}

	//////////////////////////////////////////////////////////////////////////

	BitmapTextureValidator::BitmapTextureValidator()
		: IValidator( ClassID< CBitmapTexture >())
	{
	}

	Bool BitmapTextureValidator::Validate( const THandle<CResource>& resource, tools::IMessageReporter& errorHandler ) const
	{
		const THandle< CBitmapTexture > textureResource = Cast< CBitmapTexture >( resource );
		const res::ResourcePath& resourcePath = resource->GetPath();

		Bool hasErrors = false;

		const STextureGroupSetup& setup = textureResource->GetTextureSetup();
		if ( setup.m_group != GpuWrapApi::TEXG_Generic_Data && setup.m_group != GpuWrapApi::TEXG_Generic_LUT && setup.m_group != GpuWrapApi::TEXG_Generic_Grayscale && !red::paths::ExtractParentPath( resourcePath.ToString() ).EndsWith( "sectors\\_global\\terrain\\" ) )
		{
			if ( setup.m_compression == TCM_None )
			{
				errorHandler.Report( tools::MessageSeverity::Error, resourcePath, RED_LOCATION( resourcePath ), "Textures outside of 'Generic_Data' or 'Generic_LUT' or 'Generic_Grayscale' group must be compressed (compression type other than 'TCM_None')" );
				hasErrors = true;
			}
		}

		if ( setup.m_compression != TCM_None )
		{
			const ITexture::SizeDesc desc = textureResource->GetSizeDesc();
			if ( ( desc.width % 4 != 0 ) || ( desc.height % 4 != 0 ) )
			{
				errorHandler.Report( tools::MessageSeverity::Error, resourcePath, RED_LOCATION( resourcePath ), String::Printf( "Compressed textures must be have resolution that is multiply of 4 (current width: %d, height: %d)", desc.width, desc.height ).AsChar() );
				hasErrors = true;
			}
		}

		return !hasErrors;
	}

}