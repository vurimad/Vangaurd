/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "findResourceCommandlet.h"
#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMesh/include/meshParamImportedSnapPoint.h"

class FindMeshesWithoutSnapPointsCommandlet : public FindResourceCommandlet
{
	RTTI_DECLARE_TYPE( FindMeshesWithoutSnapPointsCommandlet );

public:
	FindMeshesWithoutSnapPointsCommandlet()
		: FindResourceCommandlet( RED_NAME_CONSTEXPR( "find-meshes-without-snap-points" ) )
	{ }

	// FindResourceCommandlet interface
	virtual void ApplyResourceLoadingParams( res::IssueLoadingRequestParameter& param ) const override;
	virtual bool SupportsResourcePath( const res::ResourcePath& path ) const override;
	virtual bool CheckResource( const CResource& resource ) override;
	virtual const char* GetOneLiner() const override;
};
//////////////////////////////////////////////////////////////////////////

RTTI_BEGIN_TYPE( FindMeshesWithoutSnapPointsCommandlet )
	RTTI_PARENT_TYPE( FindResourceCommandlet )
RTTI_END_TYPE()

//////////////////////////////////////////////////////////////////////////

bool FindMeshesWithoutSnapPointsCommandlet::CheckResource( const CResource& resource )
{
	if ( !resource.IsA< CMesh >() )
	{
		return false;
	}

	const CMesh& meshResource = static_cast<const CMesh&>( resource );

	const THandle< mesh::MeshParamImportedSnapPoint > param = meshResource.GetParameter< mesh::MeshParamImportedSnapPoint >();
	if ( !param )
	{
		return true;
	}

	red::DynArray< mesh::MeshImportedSnapPointPtr > importedSnapPoints;
	param->GetSnapPoints( importedSnapPoints );

	return importedSnapPoints.Empty();
}

void FindMeshesWithoutSnapPointsCommandlet::ApplyResourceLoadingParams( res::IssueLoadingRequestParameter& param ) const
{
	param.skipImports = true;
	param.skipPostLoad = true;
	param.postLoadFlags.skipUploadRenderData = true;
	param.skipBuffers = true;
}

bool FindMeshesWithoutSnapPointsCommandlet::SupportsResourcePath( const res::ResourcePath& path ) const
{
	return path.ToStringView().EndsWith( CMesh::GetFileExtension() ) || path.ToStringView().EndsWith( CMesh::GetDeprecatedFileExtension() );
}

const char* FindMeshesWithoutSnapPointsCommandlet::GetOneLiner() const
{
	return "Finds meshes that do not have a snap point";
}