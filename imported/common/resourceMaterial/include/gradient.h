/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redReflection/include/resource.h"
#include "../../renderData/include/renderTextureBlob.h"
#include "../../renderData/include/renderObjectPtr.h"
#include "../../renderData/include/renderGradientEntryData.h"
#include "../../renderData/include/renderGradientData.h"

class RESOURCEMATERIAL_API CGradient : public CResource
{
	RTTI_DECLARE_TYPE( CGradient );
	IMPLEMENT_RESOURCE_INTERFACE( "gradient", "gradient", "Gradient resource" );
	RED_USE_MEMORY_POOL( rend::PoolRenderResources );

protected:
	red::DynArray< rend::GradientEntry >	m_gradientEntries{ rend::PoolRenderResources() };
	TRenderPtr< IRenderResource >			m_renderResource;
	mutable red::RWSpinLock					m_resourceCreationLock;

public:
	CGradient();
	virtual ~CGradient();

	// Create rendering resource for material
	virtual void CreateRenderResource();

	// Update rendering resource for material
	virtual void UpdateRenderResource();

	// Get the rendering proxy for this material
	const TRenderPtr< IRenderResource> & GetRenderResource() const;

	// Does the material currently have a render resource created?
	RED_INLINE Bool HasRenderResource() const { return m_renderResource != nullptr; }

	RED_INLINE const red::DynArray< rend::GradientEntry >& GetGradientEntries() const { return m_gradientEntries; }

	RED_INLINE rend::GradientData GetGradientData( const Bool normalized = true ) const
	{
		rend::GradientData result;
		result.gradientEntries = normalized ? Normalize(m_gradientEntries) : m_gradientEntries;
		return result;
	}

private:
	red::DynArray< rend::GradientEntry > Normalize( const red::DynArray< rend::GradientEntry >& entries ) const;
};