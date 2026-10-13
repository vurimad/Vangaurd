/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "renderSelectionResult.h"

namespace rend
{
	void SelectionResult::Sort()
	{
		std::sort( m_selectedObjects.Begin(), m_selectedObjects.End(), []( SelectedObjectInfo& a, SelectedObjectInfo& b ) 
		{
			if( a.GetPos().x == b.GetPos().x )
			{
				if( a.GetPos().y == b.GetPos().y )
				{
					if( a.GetId() == b.GetId() )
					{
						return a.GetZOrder() < b.GetZOrder();
					}

					return a.GetId() < b.GetId();
				}

				return a.GetPos().y < b.GetPos().y;
			}

			return a.GetPos().x < b.GetPos().x;
		} );
	}

	red::DynArray<SelectedObjectInfo> SelectionResult::GetUniqueSelectedObjects( const Rect* rect ) const
	{
		red::DynArray<SelectedObjectInfo> uniqueObjects{ rend::PoolImageProcessing() };

		// Just a quick way to remove the huge amounts of duplicates missed by the "!= lastID" way
		red::HashSet< IRenderSelectable::GlobalID > uniqueLookup{ rend::PoolImageProcessing() };
		 
		for ( const SelectedObjectInfo& info : m_selectedObjects )
		{
			if ( rect )
			{
				if ( !rect->Contains( info.GetPos() ) )
				{
					continue;
				}
			}

			if ( !uniqueLookup.Insert( info.GetId() ).IsSuccessful() )
			{
				continue;
			}

			// prevent adding object with the same id under the same pixel (for instance if picked many particles from one particle system)
			Bool duplicatedId = false;
			for ( Uint32 i = 0; i < uniqueObjects.Size(); i++ )
			{
				if ( uniqueObjects[i].GetPos() == info.GetPos() && uniqueObjects[i].GetId() == info.GetId() )
				{
					duplicatedId = true;
					break;
				}
			}

			if ( !duplicatedId )
			{
				uniqueObjects.PushBack( info );
			}
		}

		return uniqueObjects;
	}

	//---

	Int32 DepthResult::FindRegionForPixel( const Int32 pixelX, const Int32 pixelY ) const
	{
		if ( pixelX < 0 || pixelY < 0 )
		{
			return -1;
		}

		Int32 bestRegionIndex = -1;
		for ( Uint32 region_i=0; region_i<m_regions.Size(); ++region_i )
		{
			const SRegion &region = m_regions[region_i];
			RED_ASSERT( !(-1 != bestRegionIndex && region.m_priority == m_regions[bestRegionIndex].m_priority && region.m_area.Intersects(m_regions[bestRegionIndex].m_area)), "Two regions with the same priority" );
			if ( -1 != bestRegionIndex && region.m_priority < m_regions[bestRegionIndex].m_priority )
				continue;
			if ( !region.m_area.Contains( math::Point ( pixelX, pixelY ) ) )
				continue;
			bestRegionIndex = (Int32)region_i;
		}

		return bestRegionIndex;
	}

	//---

	Bool DepthResult::CalcWorldSpacePoint( const Int32 pixelX, const Int32 pixelY, Vector3& outWorldPoint, const Float maxZ ) const
	{
		// out data bounds
		if ( pixelX < m_dataRect.m_left || pixelY < m_dataRect.m_top || pixelX >= m_dataRect.m_right || pixelY >= m_dataRect.m_bottom )
			return false;

		// make sure data buffer is large enough
		const auto pixelOffX = pixelX - m_dataRect.m_left;
		const auto pixelOffY = pixelY - m_dataRect.m_top;
		const Uint32 pixelIndex = (pixelOffY * m_dataRect.Width()) + pixelOffX;
		if ( pixelIndex >= m_data.Size() )
			return false;

		// find region
		const Int32 regionIndex = FindRegionForPixel( pixelX, pixelY );
		if ( -1 == regionIndex )
			return false;
		const SRegion &region = m_regions[regionIndex];

		// extract depth
		const Float viewZ = m_data[ pixelIndex ];
		RED_LOG_SPAM( "Depth[%d,%d]: %f", pixelX, pixelY, viewZ );
		if ( viewZ > maxZ ) // do not allow stuff to be inserted very far away
			return false; 

		// we have the view Z, compute the screen Z and W
		const Float screenZ = (region.m_camera.GetViewToScreen()[2].Z * viewZ) + region.m_camera.GetViewToScreen()[3].Z;
		const Float screenW = viewZ;

		// calculate projection coordinates
		const Float screenX = -1.0f + 2.0f * ( ( pixelX - region.m_area.m_left + 0.5f ) / (Float)region.m_area.Width() );
		const Float screenY = 1.0f - 2.0f * ( ( pixelY - region.m_area.m_top + 0.5f ) / (Float)region.m_area.Height() );

		// compute projected vector
		const Vector4 projectedScreen( screenX * screenW, screenY * screenW, screenZ, screenW );

		// move back to world space
		const auto worldPos = region.m_camera.GetScreenToWorld().TransformVectorWithW( projectedScreen );
		if ( worldPos.W < 0.0001f )
			return false;

		// compute final position
		outWorldPoint.X = worldPos.X / worldPos.W;
		outWorldPoint.Y = worldPos.Y / worldPos.W;
		outWorldPoint.Z = worldPos.Z / worldPos.W;
		RED_LOG_SPAM( "WorldPos[%d,%d]: [%f,%f.%f]", pixelX, pixelY, outWorldPoint.X, outWorldPoint.Y, outWorldPoint.Z );
		return true;
	}

	void DepthResult::SetViewport( const Uint32 width, const Uint32 height, const Rect& dataRect )
	{
		m_width		= width;
		m_height	= height;
		m_dataRect	= dataRect;

		RED_FATAL_ASSERT( m_width * m_height > 0 );
	}

	void DepthResult::AddCameraRegion( Uint32 priority, const Rect &area, const CRenderCamera &camera )
	{
		const red::ScopedLock<red::SpinLock> lock ( m_regionsLock );

		for ( Uint32 i=0; i <m_regions.Size(); ++i )
		{
			if( m_regions[i].m_priority == priority && m_regions[i].m_area.Intersects(area) ) // Ambiguity : overlapping regions should have different priorities
				return;
		}

		m_regions.Grow(1);
		m_regions.Back().m_area = area;
		m_regions.Back().m_priority = priority;
		m_regions.Back().m_camera = camera;
	}

	DepthResult::DepthResult( const red::memory::Pool& pool )
		: m_width( 0 )
		, m_height( 0 )
		, m_dataRect( 0, 0, 0, 0 )
		, m_regions( pool )
		, m_data( pool )
	{	}

	DepthResult::~DepthResult()
	{	}

	//---

	ColorResult::ColorResult( const red::memory::Pool& pool )
	{
		m_width = 0;
		m_height = 0;
	}

	ColorResult::~ColorResult()
	{	}

	HdrResult::HdrResult( const red::memory::Pool& pool )
		: m_image( pool )
	{
		m_width = 0;
		m_height = 0;
	}

	GIVolumeResult::GIVolumeResult( const red::memory::Pool& pool )
	{
		m_width = 0;
		m_height = 0;
	}

	GIVolumeResult::~GIVolumeResult()
	{	}

	HdrResult::~HdrResult()
	{	}

	Color10Result::Color10Result( const red::memory::Pool& pool )
	{
		m_width = 0;
		m_height = 0;
	}

	Color10Result::~Color10Result()
	{	}

}	// namespace rend
