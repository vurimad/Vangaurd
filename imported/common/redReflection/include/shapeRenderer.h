/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#pragma once

namespace red
{

	/// Helper class for generating a renderable geometrical representation of any debug/offline shape 
	/// NOTE: this is NOT meant to be used for any runtime code
	class RED_REFLECTION_API IShapeRenderer
	{
	public:
		IShapeRenderer();
		virtual ~IShapeRenderer();

		/// Reserve geometry (optional)
		virtual void Reserve( const Uint32 numVeritces ) = 0;

		/// Add line - only when generating wireframe shapes, normally the wireframe can be generated from the triangles only
		virtual void AddLine( const Vector3& start, const Vector3& end ) = 0;

		/// Add triangle, CCW
		virtual void AddTriangle( const Vector3& a, const Vector3& b, const Vector3& c ) = 0;

		/// Add PLANAR quad, CCW
		virtual void AddQuad( const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d ) = 0;
	};

} // red