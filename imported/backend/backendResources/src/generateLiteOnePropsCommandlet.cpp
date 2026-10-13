#include "build.h"
#include "resaveCommandlet.h"
#include "../../backendEngine/include/streamingManagerInstance.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../backendEngine/include/commandletUtils.h"
#include "../../../common/worldEntities/include/entity.h"
#include "../../../common/worldEntities/include/entityTemplate.h"
#include "../../../common/worldEntities/include/entityTemplateUnpackedView.h"
#include "../../../common/worldEntities/include/entityComponent.h"
#include "../../../common/redJobs2/include/jobBuilder.h"
#include "../../../common/worldEntities/include/slotComponent.h"
#include "../../../common/redReflection/include/handle.h"
#include "../../../common/worldEntities/include/entityTemplateAppearance.h"
#include "../../../common/worldEntities/include/rootTransformComponent.h"
#include "../../../common/worldEntities/include/transformComponent.h"
#include "../../../common/worldEntities/include/animatedComponent.h"
#include "../../../common/engine/include/ruidGenerator.h"
#include "../../../common/worldEntities/include/transformBinding.h"
#include "../../../common/resource/include/resourceSaver.h"
#include "../../../common/engine/include/ruidGenerator.h"
#include "../../../common/redCore/include/absolutePath.h"
#include "../../backendEngine/include/entityTemplateDataView.h"
#include "../../../common/worldEntities/include/skinnedMeshComponent.h"
#include "../../../common/worldEntities/include/meshComponent.h"
#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/worldEntities/include/entityTemplateEditorController.h"

class BACKEND_RESOURCES_API GenereteLiteOnePropCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE( GenereteLiteOnePropCommandlet );

public:
	GenereteLiteOnePropCommandlet();

	virtual const char* GetOneLiner() const override { return "Generate simple one prop from complex"; }
	virtual void PrintHelp() const override;
	virtual bool Execute( const red::CommandLine& options ) override;

	virtual Bool InitializeEnvironment() override;
	virtual void TeardownEnvironment() override;

private:
	Bool ParseParams( const red::CommandLine& options );

	THandle< ent::EntityTemplate > LoadEntity( const res::ResourcePath& path );
	Bool GetTransformation( const THandle< ent::EntityTemplate >& sourceEntity, red::DynArray< THandle< ent::IComponent > >& components, Vector3& relativePostion, Quaternion& relativeRotation );
	Bool GenerateLiteEntity( const res::ResourcePath& path, const String& destinationDir, const String& fileName );

	res::StreamingManagerInstance m_streamingInstance;
	commandlet::EngineInstance m_engineInstance;
	
	red::DynArray< String > m_paths;
};

RTTI_BEGIN_TYPE( GenereteLiteOnePropCommandlet )
	RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE()

GenereteLiteOnePropCommandlet::GenereteLiteOnePropCommandlet()
	: ICommandlet( RED_NAME_CONSTEXPR( "generateLiteOneProp" ) )
{
}

Bool GenereteLiteOnePropCommandlet::InitializeEnvironment()
{
	return m_engineInstance.Initialise() && m_streamingInstance.Initialize();
}

void GenereteLiteOnePropCommandlet::TeardownEnvironment()
{
	m_streamingInstance.Shutdown();
	m_engineInstance.Shutdown();
}

void GenereteLiteOnePropCommandlet::PrintHelp() const
{
	RED_LOG( "Usage:" );
	RED_LOG( " generateLiteOneProp -items=<dir or an one prop ent file>" );
}

Bool GenereteLiteOnePropCommandlet::ParseParams( const red::CommandLine& options )
{
	if ( !options.GetAllParams( "items", m_paths ) )
	{
		printf( "ReplaceWorldNodeCommandlet: sourceAppearance name or appearanceMapping not provided\n" );
		return false;
	}

	return true;
}

THandle< ent::EntityTemplate > GenereteLiteOnePropCommandlet::LoadEntity( const res::ResourcePath& path )
{
	THandle< ent::EntityTemplate > sourceEntity;

	// Open existing resource, if present. We will clear it and create all the components from scratch, but we can use it to retain metadata and tags.
	if ( GetResourceDepot()->ResourceExists( path ) )
	{
		res::ResourceTokenHandle resourceToken = GResourceLoader->IssueLoadingRequest( path );
		THandle< CResource > existingResource = resourceToken->WaitUntilLoaded();
		sourceEntity = Cast< ent::EntityTemplate >( existingResource );

		sourceEntity->Editor_WaitUntilCompiled();

		return sourceEntity;
	}

	return nullptr;
}

Bool GenereteLiteOnePropCommandlet::GetTransformation( const THandle< ent::EntityTemplate >& sourceEntity, red::DynArray< THandle< ent::IComponent > >& components, Vector3& relativePostion, Quaternion& relativeRotation )
{
	// TemplateUnpackedViewPtr
	
	ent::TemplateDataView view;
	{
		job::Builder builder{ job::Priority::Latent };
		view.Reset( sourceEntity, builder );
		view.Rebuild( builder, true );

		job::FlushCounter( builder.ExtractWaitCounter() );
	}

	Bool found = false;

	components = view.GetTemplateComponents();

	const auto& resolvedComponents = view.GetResolvedComponents();
	for ( auto& component : resolvedComponents )
	{
		if ( component->GetName() == RED_NAME_CONSTEXPR_NOREG( "Slot5946" ) )
		{
			THandle< ent::SlotComponent > slot = Cast< ent::SlotComponent >( component );
			if ( slot == nullptr )
			{
				found = true;
				break;
			}

			red::DynArray< ent::Slot > slots = slot->Editor_GetSlots();

			if ( slots.Size() > 0 )
			{
				relativePostion = slots[ 0 ].m_relativePosition;
				relativeRotation = slots[ 0 ].m_relativeRotation;

				found = true;
				break;
			}
		}
	}

	if ( found )
	{
		for ( auto& component : resolvedComponents )
		{
			if ( THandle< ent::AnimatedComponent > animated = Cast< ent::AnimatedComponent >( component ) )
			{
				Transform animationTransform;
				if ( animated->TEMP_GetReferenceTransformMS( RED_NAME_CONSTEXPR( "base" ), animationTransform ) )
				{
					Transform slot;
					slot.m_position = relativePostion;
					slot.m_orientation = relativeRotation;

					Transform result = slot * animationTransform;

					relativePostion = result.m_position;
					relativeRotation = result.m_orientation;

					found = true;
				}
			}
		}
	}

	return found;
}

Bool GenereteLiteOnePropCommandlet::GenerateLiteEntity( const res::ResourcePath& path, const String& destinationDir, const String& fileName )
{
	THandle< ent::EntityTemplate > sourceEntity = LoadEntity( path );
	if ( !sourceEntity )
	{
		return false;
	}

	Vector3 relativePostion;
	Quaternion relativeRotation;

	red::DynArray< THandle< ent::IComponent > > sourceComponents;
	if ( !GetTransformation( sourceEntity, sourceComponents, relativePostion, relativeRotation ) )
	{
		return false;
	}

	////////////////////////////////////////////////////

	String extracted = red::paths::RemoveExtension( fileName ).ToString();

	String tmpPath = String::Printf( "%hs\\%hs_lite.ent", destinationDir.AsChar(), extracted.Data() );
	res::ResourcePath liteEntityPath = res::ResourcePath::Build( tmpPath );

	THandle< ent::EntityTemplate > liteEntity = LoadEntity( liteEntityPath );

	Bool newTemplate = false;
	if ( !liteEntity )
	{
		newTemplate = true;
		liteEntity = CreateHandle< ent::EntityTemplate >();    // The entity template does not exist, or is of different type.
	}

	ent::TemplateUnpackedViewPtr view;
	{
		job::Builder builder{ job::Priority::Latent };
		view = liteEntity->CreateUnpackedView( builder );
		job::FlushCounter( builder.ExtractWaitCounter() );
	}

	if ( !newTemplate )
	{
		view->ClearComponents();
	}

	////////////////////////////////////////////////////

	THandle< ent::PlaceholderComponent > placeholderlComponent = CreateHandle< ent::PlaceholderComponent >();
	placeholderlComponent->Internal_SetComponentId( tools::RUIDGenerator().Next() );
	view->AddComponent( placeholderlComponent );

	THandle< ent::TransformComponent > transformComponent = CreateHandle< ent::TransformComponent >();
	transformComponent->Internal_SetComponentId( tools::RUIDGenerator().Next() );
	transformComponent->SetName( RED_NAME_CONSTEXPR( "Slot5946" ) );
	//transformComponent->SetLocalPosition( relativePostion );
	//transformComponent->SetLocalOrientation( relativeRotation );
	transformComponent->SetInitialPlacement( relativePostion, relativeRotation );
	view->AddComponent( transformComponent );

	// set transform binding to base template root component
	THandle< ent::HardTransformBinding > transformBinding = CreateHandle< ent::HardTransformBinding >();
	transformBinding->SetBindName( placeholderlComponent->GetName() );

	const auto* prop = transformComponent->GetClass()->FindProperty( RED_NAME_CONSTEXPR_NOREG( "parentTransform" ) );
	prop->Set( transformComponent.Get(), &transformBinding );

	////////////////////////////////////////////////////

	auto& targetAppearances = liteEntity->GetAppearances();
	targetAppearances.Clear();

	if ( sourceEntity->GetAppearances().Size() > 0 )
	{
		ent::EntityTemplate::TAppearances& sourceAppearances = sourceEntity->GetAppearances();
		for ( const auto& definition : sourceAppearances )
		{
			CName defintionName = definition.GetName();
			targetAppearances.PushBack( ent::TemplateAppearance( defintionName, definition.GetPath(), definition.GetAppearanceName() ) );
		}

		if ( targetAppearances.Size() > 0 )
		{
			liteEntity->SetDefaultApperanceName( targetAppearances[ 0 ].GetName() );
		}
	}
	else
	{
		for ( const THandle< ent::IComponent >& sourceComponent : sourceComponents )
		{
			res::ResourcePath meshPath;
			if ( THandle< ent::SkinnedMeshComponent > skinnedMesh = Cast< ent::SkinnedMeshComponent >( sourceComponent ) )
			{
				meshPath = skinnedMesh->GetMeshReference().GetPath();
			}
			else if ( THandle< ent::MeshComponent > meshComponent = Cast< ent::MeshComponent >( sourceComponent ) )
			{
				meshPath = meshComponent->GetMeshReference().GetPath();
			}
			else
			{
				continue;
			}

			THandle< ent::MeshComponent > meshComponent = CreateHandle< ent::MeshComponent >();
			meshComponent->Internal_SetComponentId( tools::RUIDGenerator().Next() );
			meshComponent->SetMesh( meshPath );
			view->AddComponent( meshComponent );

			// set transform binding to base template root component
			THandle< ent::HardTransformBinding > transformBinding = CreateHandle< ent::HardTransformBinding >();
			transformBinding->SetBindName( transformComponent->GetName() );

			const auto* prop = meshComponent->GetClass()->FindProperty( RED_NAME_CONSTEXPR_NOREG( "parentTransform" ) );
			prop->Set( meshComponent.Get(), &transformBinding );
		}
	}

	view->SubmitChanges();

	red::SharedPtr< ent::EntityTemplateEditorController > controller = liteEntity->Editor_AcquireEditorController();
	controller->Save();
	liteEntity->Editor_BindEntityType( sourceEntity->Editor_GetEntityClassType()->GetName() );

	/////////

	res::ResourceSavingOptions options;
	options.m_targetPath = liteEntityPath;
	return res::SaveResource( liteEntity, options );
}

bool GenereteLiteOnePropCommandlet::Execute( const red::CommandLine& options )
{
	if ( !ParseParams( options ) )
	{
		return false;
	}

	for ( const String& path : m_paths ) 
	{
		String fileName = red::paths::ExtractFileName( path );
		String extension = red::paths::ExtractExtension( path );
		String distantionDir = red::paths::ExtractParentPath( path );

		res::ResourcePath resourcePath = res::ResourcePath::Build( path );

		GenerateLiteEntity( resourcePath, distantionDir, fileName );
	}

	return true;
}