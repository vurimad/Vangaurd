#include "build.h"
#include "TextureThumbnailGenerator.h"

#include "../../../common/renderData/include/renderSelectionResult.h"
#include "thumbnailRendering.h"
#include "../../common/redReflection/include/resource.h"
#include "../../common/resourceMaterial/include/bitmapTexture.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( TextureThumbnailGenerator, res );
	RTTI_PARENT_TYPE( IThumbnailGenerator );
RTTI_END_TYPE()

namespace res
{
	TextureThumbnailGenerator::TextureThumbnailGenerator()
	{
		const auto & defaultObject = ClassID<CBitmapTexture>()->GetDefaultObject< CResource >();
		const String & extension = defaultObject->GetExtension();
		const String & deprecatedExtension = defaultObject->GetDeprecatedExtension();

		m_formatsExtensions.PushBack( extension );
		if ( !deprecatedExtension.Empty() && extension != deprecatedExtension )
		{
			m_formatsExtensions.PushBack( deprecatedExtension );
		}
	}

	void TextureThumbnailGenerator::Generate( const THandle<CResource>& resource, ThumbGenContext& ctx, red::SharedPtr<rend::ColorResult>& result )
	{
		if ( THandle< CBitmapTexture > texture = Cast<CBitmapTexture>( resource ) )
		{
			if ( texture->GetTextureSetup().m_group != GpuWrapApi::TEXG_Generic_Data && texture->GetRenderResource() )
			{
				ThumbnailRenderingManager rendering( ctx.m_size );
				rendering.SetupTexture( texture.Get() );
				rendering.Render( result, ctx.m_presetsManager );
			}
		}
	}

	void TextureThumbnailGenerator::OnReset()
	{

	}

}
