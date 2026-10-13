/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/
#include "../../backendEngine/include/importer.h"

class CMesh;

class SpeedTreeMeshImporter : public IImporter
{
	//RTTI_DECLARE_TYPE( SpeedTreeMeshImporter );

public:
	SpeedTreeMeshImporter()
	{
		m_resourceClass = ClassID< CMesh >();
	};
	virtual ImportResult DoImport( THandle< CResource > inputResource, const THandle<ResourceImportOptions>& options ) = 0;

protected:

	inline String GetDestTexturesDirectory(String resourcePath)
	{
		return red::paths::ExtractParentPath(resourcePath) + "textures\\";
	}

	enum ESpeedTreeWindType
	{
		STW_None = 0,
		STW_Fastest = 1,
		STW_Fast = 2,
		STW_Better = 3,
		STW_Best = 4,
		STW_Palm = 5,
		STW_BestPlus = 6
	};

	enum ESpeedTreeLODType
	{
		STLOD_Pop = 0,
		STLOD_Smooth = 1
	};
};

//RTTI_BEGIN_TYPE( SpeedTreeMeshImporter )
//	RTTI_PARENT_TYPE( IImporter )
//RTTI_END_TYPE()