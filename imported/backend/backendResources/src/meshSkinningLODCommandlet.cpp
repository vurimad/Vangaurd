#include "build.h"
#include "resaveCommandlet.h"

#include "../../../common/resourceMesh/include/mesh.h"
#include "../../backendMesh/include/meshDataView.h"

class BACKEND_RESOURCES_API MeshSkinningLODsCommandlet : public ResaveCommandlet
{
    RTTI_DECLARE_TYPE( MeshSkinningLODsCommandlet );
public:
    MeshSkinningLODsCommandlet()
        : ResaveCommandlet( RED_NAME_CONSTEXPR( "meshSkinningLOD" ) )
    {}

protected:
    virtual bool OnResourcePreSave( CResource & resource ) override
    {
        const char expectedExt[] = "mesh";
        if( strcmp( expectedExt, resource.GetExtension() ) )
        {
            return false;
        }

        CMesh& mesh = (CMesh&)resource;
        mesh::DataView dataView( mesh );

        if( dataView.CreateSkinningLODs() )
        {
            dataView.Apply();
            return true;
        }

        return false;
    }
};


RTTI_BEGIN_TYPE( MeshSkinningLODsCommandlet )
RTTI_PARENT_TYPE( ResaveCommandlet )
RTTI_END_TYPE()