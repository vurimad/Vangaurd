/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "resaveCommandlet.h"
//#include "../../../common/core/include/commandlet.h"
//#include "../../../common/redCore/include/commandline.h"
//#include "../../../common/redFileSystem/include/fileSys.h"
//#include "../../../common/resource/include/basicTextBuilder.h"
//#include "../../backendEngine/include/commandletEngineInstance.h"
//#include "../../backendEngine/include/commandletRenderInstance.h"
//#include "../../backendEngine/include/commandletUtils.h"
//#include "../../backendEngine/include/importer.h"
//#include "../../backendEngine/include/resourceImportCommand.h"
//#include "../../backendEngine/include/environmentPresetsManager.h"

#include "../../../common/resourceMaterial/include/sourceTextureData.h"
#include "../../../common/resourceMaterial/include/multilayerMask.h"
#include "../../backendMaterial/include/multilayerMaskBuilder.h"



class MultilayerMaskResaveCommandlet : public ResaveCommandlet
{
	RTTI_DECLARE_TYPE(MultilayerMaskResaveCommandlet);

public:
	MultilayerMaskResaveCommandlet();
	~MultilayerMaskResaveCommandlet();

	virtual const char* GetOneLiner() const override;
	virtual void PrintHelp() const override;

private:
	virtual bool OnResourcePreSave( CResource & resource ) override;
};

RTTI_BEGIN_TYPE(MultilayerMaskResaveCommandlet);
RTTI_PARENT_TYPE(ResaveCommandlet);
RTTI_END_TYPE();


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MultilayerMaskResaveCommandlet::MultilayerMaskResaveCommandlet()
	: ResaveCommandlet( RED_NAME_CONSTEXPR("mlmaskResave") )
{
}

MultilayerMaskResaveCommandlet::~MultilayerMaskResaveCommandlet()
{
}

const char* MultilayerMaskResaveCommandlet::GetOneLiner() const
{
	return "Resave mlmasks, rebuilding render resources.";
}

void MultilayerMaskResaveCommandlet::PrintHelp() const
{
	ResaveCommandlet::PrintHelp();
}

bool MultilayerMaskResaveCommandlet::OnResourcePreSave( CResource & resource )
{
	if ( !resource.GetClass()->IsA<Multilayer_Mask>() )
	{
		return false;
	}

	if ( THandle< Multilayer_Mask > mlmask = Cast<Multilayer_Mask>( HandleFromPtr( &resource ) ) )
	{
		if ( THandle<rend::RenderMultilayerMaskBlob> blob = Cast<rend::RenderMultilayerMaskBlob>(mlmask->GetRenderMultilayerMaskResource().GetRenderResourceBlob()) )
		{
			if ( !blob->m_header.IsMultiresolution() )
			{
				RED_LOG( "MLMask %s uses old non-multiresolution layout. This is no longer supported. Asset must be reimported.", mlmask->GetPath().ToDebugString() );
				return false;
			}

			if ( blob->m_header.m_version < rend::RenderMultilayerMaskBlob::CURRENT_VERSION )
			{
				RED_LOG( "%s uses old version %d.", mlmask->GetPath().ToDebugString(), blob->m_header.m_version );
			}
		}

		red::DynArray<THandle<CSourceTexture>> layers { red::PoolBackend() };
		mlmask->GetSourceData( layers );

		MultilayerMaskBuilder builder;
		builder.ReserveLayers( layers.Size()+1 );
		for ( Uint32 i = 0; i < layers.Size(); ++i )
		{
			const auto& layerSource = layers[i];
			auto sourceImage = layerSource->GetImage();

			MultilayerMaskBuilder::LayerSource source;
			source.format = GpuWrapApi::TEXFMT_R8_Unorm;
			source.width = layerSource->GetWidth();
			source.height = layerSource->GetHeight();
			source.rowStride = source.width;
			source.imageData = sourceImage->GetPixelBuffer();
			source.bias = mlmask->GetLayerBias( i+1, ECookingPlatform::PLATFORM_PC );
			source.biasConsole = mlmask->GetLayerBias( i+1, ECookingPlatform::PLATFORM_PS4 );
			builder.SetLayerSource( i+1, source );
		}

		THandle<Multilayer_Mask> handle = mlmask;

		builder.Generate( handle );
		if ( builder.HasError() )
		{
			RED_LOG_ERROR( "Failed to generate mlmask %s: %s", mlmask->GetPath().ToDebugString(), builder.GetErrorString().AsChar() );
			return false;
		}

		return true;
	}

	return false;
}
