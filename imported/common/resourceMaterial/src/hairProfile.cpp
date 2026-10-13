/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "hairProfile.h"
#include "../../renderData/include/renderResource.h"
#include "../../renderData/include/renderHairData.h"
#include "../../renderData/include/renderGradientEntryData.h"
#include "../../renderData/include/renderer.h"
#include "../../redReflection/include/variant.h"

using red::DynArray;
using red::HashMap;
using red::HashSet;
using red::Map;

RTTI_BEGIN_TYPE( CHairProfile );
	RTTI_PARENT_TYPE( CResource );
	RTTI_PROPERTY( m_sampleCount ).editable().range( 1, rend::HairProfileData::HairProfile_MaxSampleCount - 1 );
	RTTI_PROPERTY( m_gradientEntriesID ).editable().resizable();
	RTTI_PROPERTY( m_gradientEntriesRootToTip ).editable().resizable();
RTTI_END_TYPE();

CHairProfile::CHairProfile()
	: m_sampleCount( rend::HairProfileData::HairProfile_DefaultSampleCount )
{
	{
		m_gradientEntriesID.Reserve( 2 );

		rend::GradientEntry entryLeft;
		entryLeft.m_value = 0.f;
		entryLeft.m_color = Color( 245, 222, 179 ); // light brown, "wheat"

		rend::GradientEntry entryRight;
		entryRight.m_value = 1.f;
		entryRight.m_color = Color( 139, 69, 19 ); // darker brown, "saddlebrown"

		m_gradientEntriesID.PushBack( entryLeft );
		m_gradientEntriesID.PushBack( entryRight );
	}
	{
		m_gradientEntriesRootToTip.Reserve( 2 );

		rend::GradientEntry entryLeft;
		entryLeft.m_value = 0.f;
		entryLeft.m_color = Color( 51, 51, 51 ); 

		rend::GradientEntry entryRight;
		entryRight.m_value = 1.f;
		entryRight.m_color = Color( 99, 99, 99 );

		m_gradientEntriesRootToTip.PushBack( entryLeft );
		m_gradientEntriesRootToTip.PushBack( entryRight );
	}
}

CHairProfile::~CHairProfile()
{
}

void CHairProfile::UpdateRenderResource()
{
	if ( !GRender || GRender->IsDeviceLost() )
	{
		return;
	}

	RED_SCOPE_LOCK( m_resourceCreationLock );

	if ( m_renderResource )
	{
		rend::HairProfileData data;
		data.sampleCount = m_sampleCount;
		data.gradientEntriesID = Normalize( m_gradientEntriesID );
		data.gradientEntriesRootToTip = Normalize( m_gradientEntriesRootToTip );
		GRender->UpdateHairProfile( m_renderResource, &data );
	}
}

void CHairProfile::CreateRenderResource()
{
	// Release current one
	m_renderResource = nullptr;

	if ( !GRender || GRender->IsDeviceLost() )
	{
		return;
	}

	// Create rendering resource for hairProfile
	rend::HairProfileData data;
	data.sampleCount = m_sampleCount;
	data.gradientEntriesID = Normalize( m_gradientEntriesID );
	data.gradientEntriesRootToTip = Normalize( m_gradientEntriesRootToTip );
	m_renderResource = GRender->UploadHairProfile( &data );
}

Bool CHairProfile::OnPropertyTypeMismatch( CName propertyName, const rtti::Property* existingProperty, const rtti::Variant& readValue )
{
	if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "gradientEntriesID" ) )
	{
		red::DynArray< rend::HairProfileGradientEntry >	oldGradientEntries{ job::PoolJobScope() };
		readValue.Get( oldGradientEntries );
		m_gradientEntriesID.Clear();
		m_gradientEntriesID.Reserve( oldGradientEntries.Size() );
		for ( const rend::HairProfileGradientEntry& oldEntry : oldGradientEntries )
		{
			rend::GradientEntry gradientEntry;
			gradientEntry.m_value = oldEntry.m_value;
			gradientEntry.m_color = oldEntry.m_color;
			m_gradientEntriesID.PushBack( gradientEntry );
		}
		return true;
	}
	else if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "gradientEntriesRootToTip" ) )
	{
		red::DynArray< rend::HairProfileGradientEntry >	oldGradientEntries{ job::PoolJobScope() };
		readValue.Get( oldGradientEntries );
		m_gradientEntriesRootToTip.Clear();
		m_gradientEntriesRootToTip.Reserve( oldGradientEntries.Size() );
		for ( const rend::HairProfileGradientEntry& oldEntry : oldGradientEntries )
		{
			rend::GradientEntry gradientEntry;
			gradientEntry.m_value = oldEntry.m_value;
			gradientEntry.m_color = oldEntry.m_color;
			m_gradientEntriesRootToTip.PushBack( gradientEntry );
		}
		return true;
	}

	return TBaseClass::OnPropertyTypeMismatch( propertyName, existingProperty, readValue );
}

const TRenderPtr<IRenderResource>& CHairProfile::GetRenderResource() const
{
	RED_SCOPE_LOCK( m_resourceCreationLock );
	if ( !m_renderResource )
	{
		CHairProfile* nonConstThis = const_cast< CHairProfile* >( this );
		nonConstThis->CreateRenderResource();
	}
	return m_renderResource;
}

red::DynArray< rend::GradientEntry > CHairProfile::Normalize( const red::DynArray< rend::GradientEntry >& entries )
{
	red::DynArray< rend::GradientEntry > result = entries;
	
	struct HairProfileGradientEntrySorter
	{
		RED_INLINE Bool operator()( const rend::GradientEntry& entry1, const rend::GradientEntry& entry2 )
		{
			return entry1.m_value < entry2.m_value;
		}
	};

	std::sort( result.Begin(), result.End(), HairProfileGradientEntrySorter() );

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