/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "convertSkinnedMeshComponentsToGarmentsCommand.h"

#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/worldEntities/include/entityTemplate.h"
#include "../../../common/worldEntities/include/entityTemplateUnpackedView.h"
#include "../../../common/worldEntities/include/skinnedMeshComponent.h"
#include "../../../common/worldEntities/include/garmentSkinnedMeshComponent.h"
#include "../../../common/resource/include/resourceSaver.h"
#include "../../../common/redReflection/include/cloningUtils.h"

#include "../../../common/redJobs2/include/jobBuilder.h"
#include "../../../common/engine/include/ruidGenerator.h"

using ent::IComponent;
using ent::SkinnedMeshComponent;
using ent::GarmentSkinnedMeshComponent;

namespace tools
{
	//-------------------------------------------------------------------------------------------------------------------------

	class ConvertSkinnedMeshComponentsToGarmentsOptions : public ResourceCommandOptions
	{
		RTTI_DECLARE_TYPE( ConvertSkinnedMeshComponentsToGarmentsOptions );

	public:
		ConvertSkinnedMeshComponentsToGarmentsOptions();

		virtual Bool NeedOptions() const override;

	private:
	};

	ConvertSkinnedMeshComponentsToGarmentsOptions::ConvertSkinnedMeshComponentsToGarmentsOptions()
	{
	}

	Bool ConvertSkinnedMeshComponentsToGarmentsOptions::NeedOptions() const
	{
		return false;
	}

	//-------------------------------------------------------------------------------------------------------------------------

	Bool ConvertSkinnedMeshComponentsToGarmentsCommand::SupportsResource( const rtti::ClassType* resourceClass )
	{
		return resourceClass == ClassID< ent::EntityTemplate >();
	}

	CName ConvertSkinnedMeshComponentsToGarmentsCommand::GetName() const
	{
		return RED_NAME_CONSTEXPR( "convertSkinnedMeshComponentsToGarments" );
	}

	THandle< ResourceCommandOptions > ConvertSkinnedMeshComponentsToGarmentsCommand::CreateOptions( const THandle< ResourceCommandOptions > suggestedOptions ) const
	{
		THandle< ConvertSkinnedMeshComponentsToGarmentsOptions > options = CreateHandle< ConvertSkinnedMeshComponentsToGarmentsOptions >();
		if ( suggestedOptions && suggestedOptions->GetClass() == options->GetClass() )
		{
			suggestedOptions->GetClass()->Copy( options.Get(), suggestedOptions.Get() );
		}

		return options;
	}

	ResourceCommandResult ConvertSkinnedMeshComponentsToGarmentsCommand::Execute( const THandle<ResourceCommandOptions>& options )
	{
		if ( !options )
		{
			return ResourceCommandResult::Error( "Core: Options are not passed" );
		}

		THandle< ConvertSkinnedMeshComponentsToGarmentsOptions > generationOptions = Cast< ConvertSkinnedMeshComponentsToGarmentsOptions >( options );
		if ( !generationOptions )
		{
			return ResourceCommandResult::Error( "Core: Invalid options type: %s ", options->GetFriendlyName().AsChar() );
		}

		res::ResourcePath entityPath = GetResourcePath();

		THandle< CResource > existingResource = GResourceLoader->IssueLoadingRequest( entityPath )->WaitUntilLoaded();
		THandle< ent::EntityTemplate > entityTemplate = Cast< ent::EntityTemplate >( existingResource );

		job::Builder builder{ job::Priority::Latent };
		builder.DispatchWait( entityTemplate->Editor_GetCompilationCounter() );
		ent::TemplateUnpackedViewPtr view = entityTemplate->CreateUnpackedView( builder );
		job::FlushCounter( builder.ExtractWaitCounter() );

		const red::DynArray< THandle< IComponent > >& components = view->GetComponents();

		Bool anyIDsWereCorrected = CorrectComponendIDs( components );
		Bool anyComponentsWereConverted = ConvertSkinnedMeshComponents( view );

		if ( !anyIDsWereCorrected && !anyComponentsWereConverted )
		{
			// Nothing changed, early exit.
			return ResourceCommandResult::Success( existingResource );
		}

		view->SubmitChanges();
		view.Reset();

		existingResource.Reset();

		res::ResourceSavingOptions saveOptions;
		saveOptions.m_targetPath = entityPath;
		if ( !res::SaveResource( entityTemplate, saveOptions ) )
		{
			return ResourceCommandResult::Error( String::Printf( "Core: Unable to convert skinned mesh components to garments for '%hs' because saving failed", entityPath.ToDebugString() ) );
		}

		// the template that we just saved is not tied to a proxy, hence won't propagate a proper resource path
		// - reset it and load through resource loader
		entityTemplate.Reset();
		THandle< CResource > savedResource = GResourceLoader->IssueLoadingRequest( entityPath )->WaitUntilLoaded();
		return ResourceCommandResult::Success( savedResource );
	}

	Bool ConvertSkinnedMeshComponentsToGarmentsCommand::CorrectComponendIDs( const red::DynArray< THandle< IComponent > >& components )
	{
		Uint32 numCorrectedComponents = 0;

		for ( const THandle< IComponent >& comp : components )
		{
			const red::RUID& compID = comp->GetComponentId();
			if ( compID.IsZero() )
			{
				comp->Internal_SetComponentId( RUIDGenerator().Next() );
				++numCorrectedComponents;
			}
		}

		return (numCorrectedComponents != 0);
	}

	Bool ConvertSkinnedMeshComponentsToGarmentsCommand::ConvertSkinnedMeshComponents( const red::SharedPtr< ent::TemplateUnpackedView >& view )
	{
		red::DynArray< THandle< IComponent > > components = view->GetComponents();       // Make a copy, since we will be removing those components.

		Uint32 numConvertedComponents = 0;

		for ( const THandle< IComponent >& comp : components )
		{
			if ( comp->IsExactlyA< SkinnedMeshComponent >() )
			{
				THandle< SkinnedMeshComponent > meshComp = Cast< SkinnedMeshComponent >( comp );
				THandle< GarmentSkinnedMeshComponent > garmentComp = CreateHandle< GarmentSkinnedMeshComponent >();

				CopyObjectProperties( meshComp, garmentComp );

				view->RemoveComponent( meshComp );
				view->AddComponent( garmentComp );
				++numConvertedComponents;
			}
		}

		return (numConvertedComponents != 0);
	}

} // tools

RTTI_BEGIN_TYPE_IN_NAMESPACE( ConvertSkinnedMeshComponentsToGarmentsCommand, tools );
	RTTI_PARENT_TYPE( tools::GeneralResourceCommand );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( ConvertSkinnedMeshComponentsToGarmentsOptions, tools );
	RTTI_PARENT_TYPE( ResourceCommandOptions );
RTTI_END_TYPE();
