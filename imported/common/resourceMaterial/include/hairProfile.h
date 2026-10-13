/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redReflection/include/resource.h"
#include "../../renderData/include/renderTextureBlob.h"
#include "../../renderData/include/renderHairData.h"
#include "../../renderData/include/renderGradientEntryData.h"
#include "../../renderData/include/renderObjectPtr.h"

class RESOURCEMATERIAL_API CHairProfile : public CResource
{
	RTTI_DECLARE_TYPE( CHairProfile );
	IMPLEMENT_RESOURCE_INTERFACE( "hp", "hp", "Hair profile" );
	RED_USE_MEMORY_POOL( rend::PoolRenderResources );

protected:
	Uint16									m_sampleCount;
	red::DynArray< rend::GradientEntry >	m_gradientEntriesID{ rend::PoolRenderResources() };
	red::DynArray< rend::GradientEntry >	m_gradientEntriesRootToTip{ rend::PoolRenderResources() };
	TRenderPtr<IRenderResource>				m_renderResource;
	mutable red::RWSpinLock					m_resourceCreationLock;

public:
	CHairProfile();
	virtual ~CHairProfile();

	// Create rendering resource for material
	virtual void CreateRenderResource();

	// Update rendering resource for material
	virtual void UpdateRenderResource();

	virtual Bool OnPropertyTypeMismatch( CName propertyName, const rtti::Property* existingProperty, const rtti::Variant& readValue ) override;

	// Get the rendering proxy for this material
	const TRenderPtr<IRenderResource>& GetRenderResource() const;

	// Does the material currently have a render resource created?
	RED_INLINE Bool HasRenderResource() const { return m_renderResource != nullptr; }

	red::DynArray< rend::GradientEntry > GetGradientEntriesID() const { return m_gradientEntriesID; }
	red::DynArray< rend::GradientEntry > GetGradientEntriesRootToTip() const { return m_gradientEntriesRootToTip; }

private:
	red::DynArray< rend::GradientEntry > Normalize( const red::DynArray< rend::GradientEntry >& entries );
};