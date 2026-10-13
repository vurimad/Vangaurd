/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "findResourceCommandlet.h"
#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMaterial/include/material.h"
#include "../../../common/redCore/include/commandline.h"

class FindMeshesWithMaterial : public FindResourceCommandlet
{
	RTTI_DECLARE_TYPE( FindMeshesWithMaterial );

public:
	FindMeshesWithMaterial()
		: FindResourceCommandlet( RED_NAME_CONSTEXPR( "find-meshes-with-material" ) )
	{
	}

	// FindResourceCommandlet interface
	virtual bool ParseOptions( const red::CommandLine& options ) override;
	virtual void ApplyResourceLoadingParams( res::IssueLoadingRequestParameter& param ) const override;
	virtual bool SupportsResourcePath( const res::ResourcePath& path ) const override;
	virtual bool CheckResource( const CResource& resource ) override;
	virtual const char* GetOneLiner() const override;

private:

	res::ResourcePath m_includeMaterial;
	res::ResourcePath m_excludeMaterial;
};
//////////////////////////////////////////////////////////////////////////

RTTI_BEGIN_TYPE( FindMeshesWithMaterial )
RTTI_PARENT_TYPE( FindResourceCommandlet )
RTTI_END_TYPE()

//////////////////////////////////////////////////////////////////////////

bool FindMeshesWithMaterial::CheckResource( const CResource& resource )
{
	if ( !resource.IsA< CMesh >() )
	{
		return false;
	}

	const CMesh& meshResource = static_cast<const CMesh&>( resource );

	Bool includeMaterialFound = m_includeMaterial.IsEmpty();

	for ( const auto& material : meshResource.GetMaterials() )
	{
		const res::ResourcePath& materialPath = material->GetBaseMaterialPath();

		if ( m_excludeMaterial.IsValid() && m_excludeMaterial == materialPath )
		{
			return false;
		}

		if ( m_includeMaterial.IsValid() && m_includeMaterial == materialPath )
		{
			includeMaterialFound = true;
		}
	}

	return includeMaterialFound;
}

bool FindMeshesWithMaterial::ParseOptions( const red::CommandLine& options )
{
	String pathString;

	if ( options.GetFirstOption( "include-material", pathString ) )
	{
		m_includeMaterial = res::ResourcePath::Build( pathString );
	}

	if ( options.GetFirstOption( "exclude-material", pathString ) )
	{
		m_excludeMaterial = res::ResourcePath::Build( pathString );
	}

	return FindResourceCommandlet::ParseOptions( options );
}

void FindMeshesWithMaterial::ApplyResourceLoadingParams( res::IssueLoadingRequestParameter& param ) const
{
	param.postLoadFlags.skipUploadRenderData = true;
	param.skipBuffers = true;
}

bool FindMeshesWithMaterial::SupportsResourcePath( const res::ResourcePath& path ) const
{
	return path.ToStringView().EndsWith( CMesh::GetFileExtension() ) || path.ToStringView().EndsWith( CMesh::GetDeprecatedFileExtension() );
}

const char* FindMeshesWithMaterial::GetOneLiner() const
{
	return "Finds meshes that do not have a snap point";
}