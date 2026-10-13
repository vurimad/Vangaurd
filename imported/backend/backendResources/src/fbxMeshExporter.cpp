/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "fbxCommon.h"
#include "../../../backend/backendEngine/include/exporter.h"
#include "../../../common/resourceMesh/include/mesh.h"

class CFBXMeshExporter : public IExporter
{
	RTTI_DECLARE_TYPE( CFBXMeshExporter );

public:
	CFBXMeshExporter();

	// Export resource
	virtual Bool DoExport( const ExportOptions& options );
};

RTTI_BEGIN_TYPE( CFBXMeshExporter );
RTTI_PARENT_TYPE( IExporter );
RTTI_END_TYPE();



//-----------------------------------------------------------------------------

CFBXMeshExporter::CFBXMeshExporter()
{
	m_resourceClass = ClassID<CMesh>();
	m_formats.EmplaceBack(  "fbx", "Autodesk FBX" );
}

Bool CFBXMeshExporter::DoExport( const ExportOptions& options )
{
	FBXExportScene exportScene( options.m_saveFilePath.AsChar() );

	// Export bones
	const auto srcModel = Cast< CMesh >( options.m_resource );
	exportScene.ExportMesh(Matrix::IDENTITY(), true, srcModel.Get(), "Mesh");

	// Save the scene
	return exportScene.Save( options.m_saveFilePath.AsChar() );
}

//-----------------------------------------------------------------------------
