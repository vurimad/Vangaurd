#include "build.h"
#include "iesThumbnailGenerator.h"

#include "../../common/redReflection/include/resource.h"
#include "../../common/renderData/include/iesResource.h"
#include "../../common/renderData/include/renderSelectionResult.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( IESThumbnailGenerator, res );
{
	RTTI_PARENT_TYPE( IThumbnailGenerator );
}
RTTI_END_TYPE()

//*********************************************************************************************************************************************//
//*********************************************************************************************************************************************//

namespace res
{
	IESThumbnailGenerator::IESThumbnailGenerator()
	{
		const auto & defaultObject = ClassID<CIESDataResource>()->GetDefaultObject< CResource >();
		const String & extension = defaultObject->GetExtension();
		const String & deprecatedExtension = defaultObject->GetDeprecatedExtension();

		m_formatsExtensions.PushBack( extension );
		if( !deprecatedExtension.Empty() && extension != deprecatedExtension )
		{
			m_formatsExtensions.PushBack( deprecatedExtension );
		}
	}

	void IESThumbnailGenerator::Generate( const THandle<CResource>& resource, ThumbGenContext& ctx, red::SharedPtr<rend::ColorResult>& result )
	{
		if( THandle< CIESDataResource > ies = Cast<CIESDataResource>( resource ) )
		{
			result = red::CreateSharedPtr< rend::ColorResult >( red::PoolBackend() );

			const Uint32 W = ctx.m_size;

			result->ReserveMemory( W, W );

			for( Uint32 y = 0; y < W; ++y )
			{
				for( Uint32 x = 0; x < W; ++x )
				{
					Vector3 pos( x * 2.0f / W - 1.0f, y * 2.0f / W, -0.2f );
					const Float dst = pos.Mag();

					pos /= dst;

					const Float f = ies->Sample( pos.Y );

					const Uint32 r = Min<Uint32>( Uint32( math::ToGammaAccu( f / (0.05f + dst * dst) ) * 255 ), 255 );

					result->SetColor( Color( r, r, r ), x, y );
				}
			}
		}
	}

	void IESThumbnailGenerator::OnReset()
	{

	}

}

//*********************************************************************************************************************************************//
//*********************************************************************************************************************************************//
