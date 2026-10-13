/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#pragma once

#include "mathVector3.h"
#include "mathPlane.h"
#include "mathBox.h"

namespace red
{

	/// Build 3D convex from list of points
	/// Uses the integer based arithmetic and thus is 100% deterministic regardless to the position of vertices (no floating point errors)
	/// If building 
	class RED_REFLECTION_API ConvexBuilder
	{
	public:
		ConvexBuilder();
		~ConvexBuilder();

		/// Do we contain a valid data inside (valid convex)? 
		RED_FORCE_INLINE const Bool IsValid() const { return m_planes.Size() >= 4; }

		/// Build convex from list of points, expects a table of XYZ triplets (positions), supports optional quantization range
		/// Returns false if convex could not be built
		/// Overrides previous content of builder, data should be extracted in different formats via the Extract() methods
		Bool Build( const Vector3* vertices, const Uint32 vertexCount );
		
		/// Extract computed data into a convex hull object
		/// Returns false if convex could not be extracted
		Bool Extract( struct ConvexHull& outHull ) const;

		/// Extract computed data into a extended convex hull object
		/// Returns false if convex could not be extracted
		Bool ExtractEx( struct ConvexHullEx& outHull ) const;

	private:
		struct Edge
		{
			Uint32 m_next;
			Uint32 m_reverse;
			Uint32 m_targetVertex;
		};

		red::DynArray< Plane >			m_planes{ red::PoolEngine() };
		red::DynArray< Vector3 >		m_vertices{ red::PoolEngine() };
		red::DynArray< Uint32 >			m_faces{ red::PoolEngine() };
		red::DynArray< Edge >			m_edges{ red::PoolEngine() };
	};

} // red