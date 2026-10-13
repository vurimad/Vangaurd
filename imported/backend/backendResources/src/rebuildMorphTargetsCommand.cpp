/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "rebuildMorphTargetsCommand.h"

#include "../../../common/resourceMesh/include/morphTargetMesh.h"

#include "../../backendMesh/include/meshBackendUtils.h"
#include "../../../common/resource/include/resourceSaver.h"

namespace tools
{
	class RebuildMorphTargetsOptions : public ResourceCommandOptions
	{
		RTTI_DECLARE_TYPE( RebuildMorphTargetsOptions );

	public:
		virtual Bool NeedOptions() const override { return false; }
	};

	//-------------------------------------------------------------------------------------------------------------------------

	Bool RebuildMorphTargetsCommand::SupportsResource( const rtti::ClassType* resourceClass )
	{
		return resourceClass == ClassID< MorphTargetMesh >();
	}

	CName RebuildMorphTargetsCommand::GetName() const
	{
		return RED_NAME_CONSTEXPR( "rebuildMorphTargets" );
	}

	THandle< ResourceCommandOptions > RebuildMorphTargetsCommand::CreateOptions( const THandle< ResourceCommandOptions > suggestedOptions ) const
	{
		return CreateHandle< RebuildMorphTargetsOptions >();
	}

	ResourceCommandResult RebuildMorphTargetsCommand::Execute( const THandle<ResourceCommandOptions>& options )
	{
		const res::ResourcePath& resourcePath = GetResourcePath();

		res::LoadingOptions loadingOptions;
		loadingOptions.skipPostLoad = false;
		if ( THandle< MorphTargetMesh > morphTargetMesh = GResourceLoader->IssueLoadingRequest( Convert( resourcePath, loadingOptions ) )->WaitUntilLoaded< MorphTargetMesh >() )
		{
			morphTargetMesh->Rebuild();

			res::ResourceSavingOptions saveOptions;
			saveOptions.m_targetPath = resourcePath;
			if ( !res::SaveResource( morphTargetMesh, saveOptions ) )
			{
				return ResourceCommandResult::Error( String::Printf("Unable to save morph target mesh '%hs' after rebuilding", resourcePath.ToDebugString() ) );
			}
		}
		return ResourceCommandResult( red::DynArray< THandle< CResource > >() );
	}
}

RTTI_BEGIN_TYPE_IN_NAMESPACE( RebuildMorphTargetsCommand, tools );
RTTI_PARENT_TYPE( tools::GeneralResourceCommand );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( RebuildMorphTargetsOptions, tools);
	RTTI_PARENT_TYPE( ResourceCommandOptions );
RTTI_END_TYPE();
