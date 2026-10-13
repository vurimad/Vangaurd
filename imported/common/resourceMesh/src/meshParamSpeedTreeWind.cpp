#include "build.h"
#include "meshParamSpeedTreeWind.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE(MeshParamSpeedTreeWind, mesh);
	RTTI_PARENT_TYPE(mesh::MeshParameter);
RTTI_END_TYPE();

namespace mesh
{
	MeshParamSpeedTreeWind::MeshParamSpeedTreeWind() = default;
	MeshParamSpeedTreeWind::~MeshParamSpeedTreeWind() = default;

	void MeshParamSpeedTreeWind::OnMeshChanged(const CMesh&)
	{}

	void MeshParamSpeedTreeWind::OnSerialize(IFile& file)
	{
		mesh::MeshParameter::OnSerialize( file );
		SerializeWindParams( file, m_speedTreeWind );
	}

} // mesh
