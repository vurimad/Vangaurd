/**
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "createOnePropEntityCommand.h"

#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/redReflection/include/resourceDepot.h"
#include "../../../common/resource/include/resourceSaver.h"
#include "../../../common/redJobs2/include/jobBuilder.h"
#include "../../../common/worldEntities/include/entityTemplateUnpackedView.h"
#include "../../../common/worldEntities/include/externalComponent.h"
#include "../../../common/worldEntities/include/meshComponent.h"
#include "../../../common/worldEntities/include/transformBinding.h"
#include "../../../common/worldEntities/include/appearanceResource.h"
#include "../../../common/worldEntities/include/appearanceDefinition.h"
#include "../../../common/worldEntities/include/appearancePart.h"
#include "../../../common/worldEntities/include/entityTemplateAppearance.h"
#include "../../../common/resourceMesh/include/meshDataViewRO.h"
#include "../../backendEntities/include/backendEntitiesPublic.h"
#include "../../backendEntities/include/entityResourceEditor.h"
#include "../../../common/engine/include/ruidGenerator.h"
#include "../../../common/worldEntities/include/skinnedMeshComponent.h"


RTTI_BEGIN_TYPE_IN_NAMESPACE( CreateOnePropEntityCommand, tools );
	RTTI_PARENT_TYPE( tools::GeneralResourceCommand );
RTTI_END_TYPE();

namespace tools
{
	class CreateOnePropEntityOption : public ResourceCommandOptions
	{
		RTTI_DECLARE_TYPE( CreateOnePropEntityOption );

	public:
		CreateOnePropEntityOption() {}

		virtual Bool NeedOptions() const override { return true; }

		String m_name;
	};

	//-------------------------------------------------------------------------------------------------------------------------

	Bool CreateOnePropEntityCommand::SupportsResource( const rtti::ClassType* resourceClass )
	{
		return resourceClass == ClassID< CMesh >();
	}

	CName CreateOnePropEntityCommand::GetName() const
	{
		return RED_NAME_CONSTEXPR( "createOnePropEntity" );
	}

	THandle< ResourceCommandOptions > CreateOnePropEntityCommand::CreateOptions( const THandle< ResourceCommandOptions > suggestedOptions ) const
	{
		THandle< CreateOnePropEntityOption > options = CreateHandle< CreateOnePropEntityOption >();

		String fileName;
		String category;

		CreateFileName( fileName, category );

		options->m_name = fileName;

		return options;
	}

	void CreateOnePropEntityCommand::SetOptionalParameter( const String& param )
	{
		m_mainDir = param;
	}

	void CreateOnePropEntityCommand::CreateFileName( String& fileName, String& category ) const
	{
		const red::DynArray< res::ResourcePath >& paths = GetAdditionalPaths();
		const res::ResourcePath& path = paths[ 0 ];

		fileName = red::utils::ExtractFileName( path.ToStringView() );
		fileName = fileName.StringBefore( "." );

		String directory = path.ToString().StringAfter( "base\\environment\\decoration\\" );
		directory = directory.StringBefore( fileName );

		category = directory.StringBefore( "\\" );
		fileName = String::Printf( "int_%s_001__%s", category.AsChar(), fileName.AsChar() );
	}

	ResourceCommandResult CreateOnePropEntityCommand::Execute( const THandle<ResourceCommandOptions>& options )
	{
		if ( !options )
		{
			return ResourceCommandResult::Error( "Core: Options are not passed" );
		}

		THandle< CreateOnePropEntityOption > generationOptions = Cast< CreateOnePropEntityOption >( options );
		if ( !generationOptions )
		{
			return ResourceCommandResult::Error( "Core: Invalid options type: %s ", options->GetFriendlyName().AsChar() );
		}

		String mainDir;
		String partDir;
		String fileName;

		Bool customName = true;

		{
			String category;

			CreateFileName( fileName, category );

			if ( !generationOptions->m_name.Empty() && fileName != generationOptions->m_name )
			{
				fileName = generationOptions->m_name;
			}

			mainDir = m_mainDir.Empty() ? String::Printf( "base\\items\\interactive\\%hs", category.AsChar() ) : m_mainDir;
			partDir = String::Printf( "%hs\\%hs", mainDir.AsChar(), fileName.AsChar() );
		}

		THandle< ent::EntityTemplate > partTemplate;
		THandle< appearance::AppearanceResource > appearance;
		THandle< ent::EntityTemplate > onePropTemplate;

		CreatePartEntity( partDir, fileName, partTemplate );
		CreateAppearance( partDir, fileName, partTemplate, appearance );
		CreateOnePropEntity( mainDir, fileName, appearance, onePropTemplate );

		return ResourceCommandResult::Success( { onePropTemplate, partTemplate, appearance } );
	}

	res::ResourcePath CreateOnePropEntityCommand::CreateResourcePath( const String& destinationDir, const String& fileName, const String& postFix, const String& fileExtension ) const
	{
		String path = String::Printf( "%hs\\%hs%hs.%hs", destinationDir.AsChar(), fileName.AsChar(), postFix.AsChar(), fileExtension.AsChar() );
		return res::ResourcePath::Build( path );
	}

	void CreateOnePropEntityCommand::CreateEntityTemplate( const res::ResourcePath& filePath, THandle< ent::EntityTemplate >& entityTemplate,
		ent::TemplateUnpackedViewPtr& view, const red::Set< const rtti::ClassType* >* componentsClassesToRemove ) const
	{
		// Open existing resource, if present. We will clear it and create all the components from scratch, but we can use it to retain metadata and tags.
		if ( GetResourceDepot()->ResourceExists( filePath ) )
		{
			res::ResourceTokenHandle resourceToken = GResourceLoader->IssueLoadingRequest( filePath );
			THandle< CResource > existingResource = resourceToken->WaitUntilLoaded();
			entityTemplate = Cast< ent::EntityTemplate >( existingResource );

			entityTemplate->Editor_WaitUntilCompiled();
		}

		Bool newTemplate = false;
		if ( entityTemplate == nullptr )
		{
			newTemplate = true;
			entityTemplate = CreateHandle< ent::EntityTemplate >();    // The entity template does not exist, or is of different type.
		}

		{
			job::Builder builder{ job::Priority::Latent };
			view = entityTemplate->CreateUnpackedView( builder );
			job::FlushCounter( builder.ExtractWaitCounter() );
		}

		if ( !newTemplate )
		{
			if ( componentsClassesToRemove == nullptr )
			{
				view->ClearComponents();
			}
			else if ( !componentsClassesToRemove->Empty() )
			{
				const red::DynArray< ent::ComponentHandle > components = view->GetComponents();
				for ( const ent::ComponentHandle& comp : components )
				{
					if ( componentsClassesToRemove->Exist( comp->GetClass() ) )
					{
						view->RemoveComponent( comp );
					}
				}
			}
			view->SubmitChanges();
		}
	}

	Bool CreateOnePropEntityCommand::CreateMeshComponents( ent::TemplateUnpackedViewPtr& view, const red::DynArray< res::ResourcePath >& paths ) const
	{
		for ( const res::ResourcePath& path : paths )
		{
			if ( !GetResourceDepot()->ResourceExists( path ) )
			{
				continue;
			}

			THandle< ent::MeshComponent > meshComponent = CreateHandle< ent::MeshComponent >();
			meshComponent->SetMesh( path );

			String meshComponentName = red::paths::ExtractFileStem( path.ToStringView() );
			meshComponentName += GenerateComponentNameRandomSuffixString( 4 );
			meshComponent->SetMesh( GetResourcePath() );
			meshComponent->SetName( RED_NAME( meshComponentName ) );
			meshComponent->Internal_SetComponentId( RUIDGenerator().Next() );

			// set transform binding to base template root component
			THandle< ent::HardTransformBinding > transformBinding = CreateHandle< ent::HardTransformBinding >();
			transformBinding->SetBindName( RED_NAME_CONSTEXPR( "Slot5946_Ext" ) );
			transformBinding->SetSlotName( RED_NAME_CONSTEXPR( "Base" ) );

			const auto* prop = meshComponent->GetClass()->FindProperty( RED_NAME_CONSTEXPR_NOREG( "parentTransform" ) );
			prop->Set( meshComponent.Get(), &transformBinding );

			view->AddComponent( meshComponent );
		}

		return true;
	}

	ResourceCommandResult CreateOnePropEntityCommand::CreatePartEntity( const String& destinationDir, const String& fileName, THandle< ent::EntityTemplate >& entityTemplate )
	{
		// Create an entity for appearance name
		res::ResourcePath entityPath = CreateResourcePath( destinationDir, fileName, "_part", "ent" );

		ent::TemplateUnpackedViewPtr view;

		CreateEntityTemplate( entityPath, entityTemplate, view );

		// Create an External component
		{
			THandle< ent::ExternalComponent > externalComponent = CreateHandle< ent::ExternalComponent >();
			externalComponent->SetName( RED_NAME_CONSTEXPR( "Slot5946_Ext" ) );
			externalComponent->SetExternalComponentName( RED_NAME_CONSTEXPR( "Slot5946" ) );

			view->AddComponent( externalComponent );
		}

		CreateMeshComponents( view, GetAdditionalPaths() );

		view->SubmitChanges();

		res::ResourceSavingOptions options;
		options.m_targetPath = entityPath;

		if ( !res::SaveResource( entityTemplate, options ) )
		{
			return ResourceCommandResult::Error( String::Printf( "Core: Unable to generate '%hs' because saving failed", entityPath.ToDebugString() ) );
		}

		entityTemplate->Internal_SetPath( entityPath );

		return ResourceCommandResult::Success( entityTemplate );
	}

	void CreateOnePropEntityCommand::CreateAppearanceResource( const res::ResourcePath& filePath, THandle< appearance::AppearanceResource >& appearance ) const
	{
		// Open existing resource, if present. We will clear it and create all the components from scratch, but we can use it to retain metadata and tags.
		if ( GetResourceDepot()->ResourceExists( filePath ) )
		{
			res::ResourceTokenHandle resourceToken = GResourceLoader->IssueLoadingRequest( filePath );
			THandle< CResource > existingResource = resourceToken->WaitUntilLoaded();
			appearance = Cast< appearance::AppearanceResource >( existingResource );
		}

		if ( appearance == nullptr )
		{
			appearance = CreateHandle< appearance::AppearanceResource >();    // The entity template does not exist, or is of different type.
		}
		appearance->SetBaseType( RED_NAME_CONSTEXPR( "Custom" ) );
	}

	ResourceCommandResult CreateOnePropEntityCommand::CreateAppearance( const String& destinationDir, const String& fileName, THandle< ent::EntityTemplate >& partTemplate, THandle< appearance::AppearanceResource >& appearance )
	{
		// Create an entity for appearance name
		res::ResourcePath path = CreateResourcePath( destinationDir, fileName, "", "app" );

		CreateAppearanceResource( path, appearance );
		CreateAppearanceDefinitions( appearance, partTemplate );

		res::ResourceSavingOptions options;
		options.m_targetPath = path;

		appearance->MarkModified();

		if ( !res::SaveResource( appearance, options ) )
		{
			return ResourceCommandResult::Error( String::Printf( "Core: Unable to generate '%hs' because saving failed", path ) );
		}

		appearance->Internal_SetPath( path );

		return ResourceCommandResult::Success( appearance );		
	}

	void CreateOnePropEntityCommand::CreateAppearanceDefinitions( const THandle< appearance::AppearanceResource >& appearance, const THandle< ent::EntityTemplate >& partEntity )
	{
		red::DynArray< CName > appearancesName;

		const red::DynArray< res::ResourcePath > additionalPaths = GetAdditionalPaths();
		const res::ResourcePath& meshPath = additionalPaths[ 0 ];

		{
			THandle< CMesh > mesh;

			res::ResourceTokenHandle resourceToken = GResourceLoader->IssueLoadingRequest( meshPath );
			THandle< CResource > existingResource = resourceToken->WaitUntilLoaded();
			mesh = Cast< CMesh >( existingResource );

			const red::ArraySpan< const THandle< mesh::MeshAppearance > >& appearances = mesh->GetAllAppearances();

			for ( const THandle< mesh::MeshAppearance > appearance : appearances )
			{
				appearancesName.PushBack( appearance->GetName() );
			}
		}

		job::Builder builder{ job::Priority::Latent };
		builder.DispatchWait( partEntity->Editor_GetCompilationCounter() );
		ent::TemplateUnpackedViewPtr view = partEntity->CreateUnpackedView( builder );
		job::FlushCounter( builder.ExtractWaitCounter() );

		CName componentName;
		for ( const ent::ComponentHandle& comp : view->GetComponents() )
		{
			if ( THandle< ent::MeshComponent > meshComp = Cast< ent::MeshComponent >( comp ) )
			{
				if ( meshComp->GetMeshReference().GetPath() == meshPath )
				{
					componentName = meshComp->GetName();
					break;
				}
			}
		}

		if ( componentName.Empty() )
		{
			RED_LOG_WARNING( "Component using mesh %hs not found in part entity %hs",
				meshPath.ToDebugString(), partEntity->GetPath().ToDebugString() );
			return;
		}

		for ( Uint32 i = 0; i < appearancesName.Size(); ++i )
		{
			appearance::PartComponentOverrides componentOverride( componentName );
			componentOverride.SetMeshAppearance( appearancesName[ i ] );

			appearance::AppearancePartOverrides overrides( TResAsyncRef< ent::EntityTemplate >( partEntity->GetPath() ) );
			overrides.SetComponentOverrides( componentOverride );

			THandle< appearance::AppearanceDefinition > defaultDefinition = CreateHandle< appearance::AppearanceDefinition >();
			defaultDefinition->SetName( appearancesName[i] );
			defaultDefinition->AddPart( { RED_NAME_CONSTEXPR( "body" ) }, partEntity->GetPath(), true );
			defaultDefinition->SetPartOverrides( overrides );

			appearance->AddAppearance( *defaultDefinition );
		}
	}

	ResourceCommandResult CreateOnePropEntityCommand::CreateOnePropEntity( const String& destinationDir, const String& fileName, const THandle< appearance::AppearanceResource >& appearance, THandle< ent::EntityTemplate >& entityTemplate )
	{
		// Create an entity for appearance name
		res::ResourcePath entityPath = CreateResourcePath( destinationDir, fileName, "", "ent" );

		ent::TemplateUnpackedViewPtr view;

		red::Set< const rtti::ClassType* > componentsTypeToRemove;
		componentsTypeToRemove.Insert( ClassID< ent::MeshComponent >() );
		componentsTypeToRemove.Insert( ClassID< ent::SkinnedMeshComponent >() );
		CreateEntityTemplate( entityPath, entityTemplate, view, &componentsTypeToRemove );

		{
			job::Builder builder;
			ent::TemplateDataView templateView;

			templateView.Reset( entityTemplate, builder );
			templateView.AddInclude( RED_NAME_CONSTEXPR( "Base" ), res::ResourcePath::Build( "base\\items\\prop_template.ent" ) );
			templateView.Rebuild( builder );

			job::FlushCounter( builder.ExtractWaitCounter() );
		}

		ent::EntityTemplate::TAppearances& appearances = entityTemplate->GetAppearances();
		appearances.Clear();

		const red::DynArray< THandle< appearance::AppearanceDefinition > >& definitions = appearance->GetAppearances();
		for ( const THandle< appearance::AppearanceDefinition >& definition : definitions )
		{
			CName defintionName = definition->GetName();
			appearances.PushBack( ent::TemplateAppearance( defintionName, appearance->GetPath(), defintionName ) );
		}

		entityTemplate->SetDefaultApperanceName( definitions[ 0 ]->GetName() );

		res::ResourceSavingOptions options;
		options.m_targetPath = entityPath;

		if ( !res::SaveResource( entityTemplate, options ) )
		{
			return ResourceCommandResult::Error( String::Printf( "Core: Unable to generate '%hs' because saving failed", entityPath.ToDebugString() ) );
		}

		entityTemplate->Internal_SetPath( entityPath );

		return ResourceCommandResult::Success( { entityTemplate, appearance } );
	}
}

RTTI_BEGIN_TYPE_IN_NAMESPACE( CreateOnePropEntityOption, tools );
	RTTI_PARENT_TYPE( ResourceCommandOptions );
	RTTI_PROPERTY( m_name ).editable();
RTTI_END_TYPE();
