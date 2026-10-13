/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "gradient.h"
#include "../../renderData/include/renderResource.h"
#include "../../renderData/include/renderGradientData.h"
#include "../../renderData/include/renderer.h"

RTTI_BEGIN_TYPE( CGradient );
	RTTI_PARENT_TYPE( CResource );
	RTTI_PROPERTY( m_gradientEntries ).editable().resizable();
RTTI_END_TYPE();

CGradient::CGradient()
{
	m_gradientEntries.Reserve( 2 );

	rend::GradientEntry entryLeft;
	entryLeft.m_value = 0.f;
	entryLeft.m_color = Color::BLACK();

	rend::GradientEntry entryRight;
	entryRight.m_value = 1.f;
	entryRight.m_color = Color::WHITE();

	m_gradientEntries.PushBack( entryLeft );
	m_gradientEntries.PushBack( entryRight );
}

CGradient::~CGradient()
{
}

void CGradient::UpdateRenderResource()
{
	if ( !GRender || GRender->IsDeviceLost() )
	{
		return;
	}

	RED_SCOPE_LOCK( m_resourceCreationLock );

	if ( m_renderResource )
	{
		rend::GradientData data;
		data.gradientEntries = Normalize( m_gradientEntries );
		GRender->UpdateGradient( m_renderResource, &data );
	}
}

void CGradient::CreateRenderResource()
{
	// Release current one
	m_renderResource = nullptr;

	if ( !GRender || GRender->IsDeviceLost() )
	{
		return;
	}

	// Create rendering resource for gradient
	rend::GradientData data = GetGradientData();
	m_renderResource = GRender->UploadGradient( &data );
}

const TRenderPtr< IRenderResource >& CGradient::GetRenderResource() const
{
	RED_SCOPE_LOCK( m_resourceCreationLock );
	if ( !m_renderResource )
	{
		CGradient* nonConstThis = const_cast< CGradient* >( this );
		nonConstThis->CreateRenderResource();
	}
	return m_renderResource;
}

red::DynArray< rend::GradientEntry > CGradient::Normalize( const red::DynArray< rend::GradientEntry >& entries ) const
{
	red::DynArray< rend::GradientEntry > result = entries;

	struct GradientEntrySorter
	{
		RED_INLINE Bool operator()( const rend::GradientEntry& entry1, const rend::GradientEntry& entry2 )
		{
			return entry1.m_value < entry2.m_value;
		}
	};

	std::sort( result.Begin(), result.End(), GradientEntrySorter() );

	for ( const auto& entry : result )
	{
		RED_ASSERT( entry.m_value >= 0.f );
		RED_ASSERT( entry.m_value <= 1.f );
	}

	Float minValue = result.Front().m_value;
	Float maxValue = result.Back().m_value;

	Bool isSingleValue = math::Abs( maxValue - minValue ) < 0.001f;

	// remap to [0-1]
	for ( Uint32 i = 0; i < result.Size(); ++i )
	{
		if ( isSingleValue )
		{
			result[ i ].m_value = (Float)i / ( result.Size() - 1 );
		}
		else
		{
			Float oldValue = result[ i ].m_value;
			Float newValue = ( oldValue - minValue ) / ( maxValue - minValue );
			result[ i ].m_value = newValue;
		}
	}

	return result;
}