#pragma once

#include "../../common/triggers/include/geometryShape.h"

namespace tools
{
	class ObjImporter
	{
	public:
		struct Mesh
		{
			String m_name;
			red::DynArray< Vector3 > m_vertices{ red::PoolBackend() };
			red::DynArray< GeometryShapeFace > m_faces{ red::PoolBackend() };
		};

		ObjImporter();
		~ObjImporter();

		Bool LoadFile( const red::AbsolutePath& path );

		red::DynArray< Mesh > m_meshes{ red::PoolBackend() };
		red::DynArray< Vector3 > m_vertices{ red::PoolBackend() };
		red::DynArray< GeometryShapeFace > m_faces{ red::PoolBackend() };
	};
}