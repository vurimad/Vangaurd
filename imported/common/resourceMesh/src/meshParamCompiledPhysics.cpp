#include "build.h"
#include "meshParamCompiledPhysics.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE(MeshParamCompiledPhysics, mesh);
RTTI_PARENT_TYPE(mesh::MeshParameter);
    RTTI_PROPERTY(m_collection);
RTTI_END_TYPE();

namespace mesh
{
    MeshParamCompiledPhysics::MeshParamCompiledPhysics() = default;
    MeshParamCompiledPhysics::~MeshParamCompiledPhysics() = default;

	void MeshParamCompiledPhysics::OnMeshChanged(const CMesh& mesh)
    {}
}