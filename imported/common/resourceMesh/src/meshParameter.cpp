#include "build.h"
#include "meshParameter.h"

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( MeshParameter, mesh );
	RTTI_PARENT_TYPE(ISerializable);
RTTI_END_TYPE();



namespace mesh
{ 
	MeshParameter::~MeshParameter()
	{}


} // mesh

