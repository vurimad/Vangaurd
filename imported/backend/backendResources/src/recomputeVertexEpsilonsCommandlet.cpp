#include "build.h"
#include "resaveCommandlet.h"
#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../backend/backendMesh/include/meshDataView.h"


class RecomputeVertexEpsilonsCommandlet : public ResaveCommandlet
{
	RTTI_DECLARE_TYPE(RecomputeVertexEpsilonsCommandlet);
public:
	RecomputeVertexEpsilonsCommandlet();
	~RecomputeVertexEpsilonsCommandlet() {} ;

	virtual bool OnResourcePreSave(CResource& resource) override;
};

RTTI_BEGIN_TYPE(RecomputeVertexEpsilonsCommandlet)
	RTTI_PARENT_TYPE(ResaveCommandlet)
RTTI_END_TYPE()

RecomputeVertexEpsilonsCommandlet::RecomputeVertexEpsilonsCommandlet()
	: ResaveCommandlet(RED_NAME_CONSTEXPR("recomputeVertexEpsilons"))
{
}

bool RecomputeVertexEpsilonsCommandlet::OnResourcePreSave(CResource& resource)
{
	CMesh* mesh = Cast<CMesh>(&resource);
	if (mesh)
	{
		mesh::DataView view(*mesh);
		return view.RecomputeVertexEpsilons();
	}
	return false;
}
