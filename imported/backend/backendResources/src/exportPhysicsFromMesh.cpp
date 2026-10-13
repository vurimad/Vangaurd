/**
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"

#include "../../backendEngine/include/exporter.h"
#include "../../../common/resource/include/resourceSaver.h"
#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMesh/include/meshParamPhysics.h"
#include "../../../common/physics/include/physicsSystemResource.h"

class ExportPhysicsFromMesh : public IExporter
{
	RTTI_DECLARE_TYPE( ExportPhysicsFromMesh );
	RED_BASE_CLASS( IExporter );

public:
	ExportPhysicsFromMesh();
	~ExportPhysicsFromMesh() = default;

	virtual Bool DoExport( const ExportOptions& options ) override;
};

ExportPhysicsFromMesh::ExportPhysicsFromMesh()
{
	m_resourceClass = ClassID< CMesh >();
	m_formats.PushBack( CFileFormat( "phys", "Extracted mesh' physics data." ) );
}

Bool ExportPhysicsFromMesh::DoExport( const ExportOptions& options )
{
	const THandle< CMesh > mesh = Cast< CMesh >( options.m_resource );

	if ( !mesh )
		return false;

	const auto outputPath = res::ResourcePath::Build( options.m_saveFilePath );

	if ( !outputPath.IsValid() )
		return false;

	if ( options.m_saveFileFormat.GetExtension() != String( "phys" ) )
		return false;

	const auto physicsParam = mesh->GetParameter< mesh::MeshParamPhysics >();

	if ( !physicsParam )
		return false;

	const THandle< physics::SystemResource > sysRes = physicsParam->GetPhysicsData();

	res::ResourceSavingOptions savingOptions;
	savingOptions.m_targetPath = outputPath;
	savingOptions.m_loadDeferredDataBuffers = true;

	return res::SaveResource( sysRes, savingOptions );
}


RTTI_BEGIN_TYPE( ExportPhysicsFromMesh );
RTTI_PARENT_TYPE( IExporter );
RTTI_END_TYPE();