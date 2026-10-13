/*
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 * Originally written by Peter Sikachev
 */

#include "build.h"
#include "../../backendData/include/cooker.h"
#include "../../backendData/include/dataTypesImpl.h"
#include "../../../common/particles/include/particleSystem.h"
#include "../../../common/particles/include/particleEmitter.h"

//-----------------------------------------------------------------------------

class ParticleCooker : public cooker::BasicCooker
{
	RTTI_DECLARE_TYPE( ParticleCooker );
public:
	virtual ~ParticleCooker() override;

	virtual Uint32 GetVersion() const override;
	virtual bool CanCook( const res::ResourcePath& resourcePath ) const override;
	virtual bool CookLoadedResource( THandle< CResource >& resource, data::Context& context ) const override;
};

RTTI_BEGIN_TYPE( ParticleCooker );
RTTI_PARENT_TYPE( cooker::BasicCooker );
RTTI_END_TYPE();

ParticleCooker::~ParticleCooker() = default;

Uint32 ParticleCooker::GetVersion() const
{
	return 0;
}

bool ParticleCooker::CanCook( const res::ResourcePath& resourcePath ) const
{
	auto extension = red::paths::GetExtension( resourcePath.ToStringView() );
	return extension == "particle";
}

bool ParticleCooker::CookLoadedResource( THandle< CResource >& resource, data::Context& context ) const
{
	bool result = false;

	if ( THandle< CParticleSystem > particleSystem = Cast< CParticleSystem >( resource ) )
	{
		auto& emitters = particleSystem->GetEmitters();

		red::alg::EraseIf( emitters, []( const THandle<CParticleEmitter>& emitter )
		{
			return emitter == nullptr || !emitter->IsEnabled();
		} );
		
		result = true;
	}

	return result;
}

//-----------------------------------------------------------------------------

