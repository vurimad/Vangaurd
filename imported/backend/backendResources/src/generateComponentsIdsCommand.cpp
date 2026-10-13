#include "build.h"
#include "generateComponentsIdsCommand.h"

#include "../../../common/worldEntities/include/entityComponent.h"
#include "../../../common/worldEntities/include/entityTemplate.h"
#include "../../../common/worldEntities/include/entityTemplateUnpackedView.h"
#include "../../../common/resource/include/resourceSaver.h"

#include "../../../common/redJobs2/include/jobBuilder.h"
#include "../../../common/engine/include/ruidGenerator.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( GenerateComponentsIdsCommand, tools );
	RTTI_PARENT_TYPE( tools::GeneralResourceCommand );
RTTI_END_TYPE();

namespace tools
{
	Bool GenerateComponentsIdsCommand::SupportsResource( const rtti::ClassType* resourceClass )
	{
		return resourceClass == ClassID< ent::EntityTemplate >();
	}

	CName GenerateComponentsIdsCommand::GetName() const
	{
		return RED_NAME_CONSTEXPR( "generateComponentsIds" );
	}
	
	THandle< ResourceCommandOptions > GenerateComponentsIdsCommand::CreateOptions( const THandle< ResourceCommandOptions > suggestedOptions ) const
	{
		return CreateHandle< ResourceCommandOptions >();
	}

	ResourceCommandResult GenerateComponentsIdsCommand::Execute( const THandle<ResourceCommandOptions>& options )
	{
		if ( !options )
		{
			return ResourceCommandResult::Error( "Core: Options are not passed" );
		}

		TResRef< ent::EntityTemplate > templateResRef( GetResourcePath() );
		THandle< ent::EntityTemplate > templateRes = templateResRef.Get();

		if ( !templateRes )
		{
			return ResourceCommandResult::Error( "Template %hs couldn't be loaded!", GetResourcePath().ToDebugString() );
		}

		job::Builder builder{ job::Priority::Latent };
		builder.DispatchWait( templateRes->Editor_GetCompilationCounter() );
		ent::TemplateUnpackedViewPtr view = templateRes->CreateUnpackedView( builder );
		job::FlushCounter( builder.ExtractWaitCounter() );

		for ( const THandle< ent::IComponent >& comp : view->GetComponents() )
		{
			comp->Internal_SetComponentId( RUIDGenerator().Next() );
		}
		view->SubmitChanges();

		res::ResourceSavingOptions saveOptions;
		saveOptions.m_targetPath = GetResourcePath();
		if ( !res::SaveResource( templateRes, saveOptions ) )
		{
			return ResourceCommandResult::Error( String::Printf(
				"Core: Unable to convert skinned mesh components to garments for '%hs' because saving failed",
				GetResourcePath().ToDebugString() ) );
		}

		return ResourceCommandResult::Success( templateRes );
	}
}