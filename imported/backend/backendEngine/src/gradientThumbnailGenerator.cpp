#include "build.h"
#include "gradientThumbnailGenerator.h"

#include "../../common/redReflection/include/resource.h"
#include "../../common/renderData/include/renderSelectionResult.h"
#include "../../common/resourceMaterial/include/gradient.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( GradientThumbnailGenerator, res );
{
	RTTI_PARENT_TYPE( IThumbnailGenerator );
}
RTTI_END_TYPE()


//*********************************************************************************************************************************************//
//*********************************************************************************************************************************************//

namespace res
{
	GradientThumbnailGenerator::GradientThumbnailGenerator()
	{
		const auto & defaultObject = ClassID< CGradient >()->GetDefaultObject< CResource >();
		const String & extension = defaultObject->GetExtension();
		const String & deprecatedExtension = defaultObject->GetDeprecatedExtension();

		m_formatsExtensions.PushBack( extension );
		if ( !deprecatedExtension.Empty() && extension != deprecatedExtension )
		{
			m_formatsExtensions.PushBack( deprecatedExtension );
		}
	}

	void GradientThumbnailGenerator::Generate( const THandle<CResource>& resource, ThumbGenContext& ctx, red::SharedPtr<rend::ColorResult>& result )
	{
		if ( THandle< CGradient > gradient = Cast< CGradient >( resource ) )
		{
			result = red::CreateSharedPtr< rend::ColorResult >( red::PoolBackend() );

			const Uint32 W = ctx.m_size;

			result->ReserveMemory( W, W );

			rend::GradientData gradientData = gradient->GetGradientData();

			for ( Uint32 x = 0; x < W; ++x )
			{
				const Float factor = (Float)x / W;
				Vector4 sampledGradient = gradientData.Sample( factor, true );
				for ( Uint32 y = 0; y < W; ++y )
				{
					result->SetColor( Color( sampledGradient ), x, y );
				}
			}
		}
	}

	void GradientThumbnailGenerator::OnReset()
	{

	}


}

//*********************************************************************************************************************************************//
//*********************************************************************************************************************************************//
