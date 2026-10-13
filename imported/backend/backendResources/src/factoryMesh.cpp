/**
 * Copyright (c) 2008 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "../../backendEngine/include/factory.h"
#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMesh/include/morphTargetMesh.h"

/// Factory for the editor world
class CMeshFactory : public IFactory
{
	RTTI_DECLARE_TYPE( CMeshFactory );

public:
	CMeshFactory()
	{
		m_resourceClass = ClassID< CMesh >();
	}

	virtual Bool DoCreate( THandle< CResource > res, const THandle< FactoryOptions >& options ) override
	{
		return true;
	}
};

RTTI_BEGIN_TYPE( CMeshFactory );
	RTTI_PARENT_TYPE( IFactory );
RTTI_END_TYPE();

//////////////////////////////////////////////////////////////////////////

class CMorphTargetMeshFactory : public IFactory
{
	RTTI_DECLARE_TYPE( CMorphTargetMeshFactory );

public:
	CMorphTargetMeshFactory()
	{
		m_resourceClass = ClassID< MorphTargetMesh >();
	}

	virtual Bool DoCreate( THandle< CResource > res, const THandle< FactoryOptions >& options ) override
	{
		return true;
	}
};

RTTI_BEGIN_TYPE( CMorphTargetMeshFactory );
	RTTI_PARENT_TYPE( IFactory );
RTTI_END_TYPE();
