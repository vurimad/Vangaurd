/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../renderData/include/particleEmitterEnum.h"
#include "../../renderData/include/renderPublicEnums.h"

namespace rend
{
	struct EmitterLOD;
	struct RenderParticleUpdaterData;
}

namespace tools
{
	class IParticleCompilationSource
	{
	public:
		virtual ~IParticleCompilationSource() {}

		virtual const res::ResourcePath& GetParticleSystemName() const = 0;

		virtual Bool IsGPUEmitter() const = 0;

		virtual Uint64 GetInitializerSetMask() const = 0;

		virtual Uint64 GetModifierSetMask() const = 0;

		virtual Uint64 GetSimulationHash() const = 0;

		virtual Uint16 GetEventSetMask() const = 0;

		virtual red::DynArray< Uint32 > GetSeeds() const = 0;

		virtual Uint32 GetNumInitializers() const = 0;

		virtual Uint32 GetNumModifiers() const = 0;

		virtual Int8 GetEmitterLoops() const = 0;

		virtual Uint32 GetMaxParticles() const = 0;

		virtual Uint8 GetInternalPriority() const = 0;

		virtual EEnvColorGroup GetEnvColorGroup() const = 0;
		
		virtual EEmitterGroup GetEmitterGroup() const = 0;

		virtual ERenderObjectType GetRenderObjectType() const = 0;

		virtual Bool GetMaskInsideCar() const = 0;

		virtual Bool GetMaskInsideInterior() const = 0;

		virtual Bool GetMaskAboveWater() const = 0;
		
		virtual Bool GetMaskUnderWater() const = 0;

		virtual Bool GetKeepSimulationLocal() const = 0;

		virtual Bool GetKillOnCollision() const = 0;

		virtual Uint8 GetInitialParticleCount() const = 0;

		virtual Float GetBackLightingFactor() const = 0;

		virtual Uint32 GetLightingMipBias() const = 0;

		virtual Float GetDiffuseWrapFactor() const = 0;

		virtual Bool GetUseSubFrameEmission() const = 0;

		virtual EParticleType GetParticleType() const = 0;

		virtual EParticleVertexDrawerType GetVertexDrawerType() const = 0;

		virtual Float GetWindInfluence() const = 0;

		virtual Uint32 GetLODCount() const = 0;

		virtual void ExtractUpdaterData( rend::RenderParticleUpdaterData& renderParticleUpdaterData ) const = 0;

		virtual void ExtractLODs( red::DynArray< rend::EmitterLOD >& lods ) const = 0;

		virtual Bool GetVolumetricParticleEnabled() const = 0;

		virtual Bool GetVolumetricParticleRelative() const = 0;

		virtual Bool GetVolumetricParticleUseFogColor() const = 0;

		virtual HDRColor GetVolumetricParticleColor() const = 0;

		virtual Float GetVolumetricParticleSize() const = 0;

		virtual Float GetVolumetricParticleDensity() const = 0;

		virtual Float GetVolumetricParticleFalloff() const = 0;
		
		virtual Float GetVolumetricParticleNoiseScale() const = 0;
		
		virtual Float GetVolumetricParticleNoiseThreshold() const = 0;
		
		virtual Vector3 GetVolumetricParticleNoiseVelocity() const = 0;

		virtual Float GetCollisionAngularVelocityDamp() const = 0;
	};
}