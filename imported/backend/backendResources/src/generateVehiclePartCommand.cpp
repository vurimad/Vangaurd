/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "generateVehiclePartCommand.h"

#include <regex>

#include "../../backendEntities/include/backendEntitiesPublic.h"
#include "../../backendEntities/include/entityResourceEditor.h"
#include "../../../common/generalEditorBackend/include/appearancesConfig.h"
#include "../../../common/redReflection/include/gatheredResource.h"
#include "../../../common/redReflection/include/resourceDepot.h"
#include "../../../common/resource/include/resourceSaver.h"
#include "../../../common/resource/include/jsonResource.h"
#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMesh/include/meshDataViewRO.h"
#include "../../../common/worldEntities/include/entityTemplateUnpackedView.h"
#include "../../../common/worldEntities/include/externalComponent.h"
#include "../../../common/worldEntities/include/garmentSkinnedMeshComponent.h"
#include "../../../common/worldEntities/include/meshComponent.h"
#include "../../../common/worldEntities/include/physicalMeshComponent.h"
#include "../../../common/worldEntities/include/slotComponent.h"
#include "../../../common/worldEntities/include/transformBinding.h"

#include "../../../common/redJobs2/include/jobBuilder.h"
#include "../../../common/engine/include/ruidGenerator.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( GenerateVehiclePartCommand, tools );
RTTI_PARENT_TYPE( tools::GeneralResourceCommand );
RTTI_END_TYPE();

namespace
{
	res::TStaticResource< JsonResource > c_appearancesMappingsFile( RED_CONST_RESOURCEPATH( "base\\appearances_mappings.json" ), GRF_Default | GRF_Startup ); // NOTE: Resource should be moved to base\characters or something
}

namespace tools
{
	class GenerateVehiclePartOptions : public ResourceCommandOptions
	{
		RTTI_DECLARE_TYPE( GenerateVehiclePartOptions );

	public:
		GenerateVehiclePartOptions() {}

		virtual Bool NeedOptions() const override { return true; }

		const String& GetDestinationDirectory() const { return m_destinationDirectory; }
		void SetDestinationDirectory( const String& path ) { m_destinationDirectory = path; }

	private:
		String m_destinationDirectory;
	};

	//-------------------------------------------------------------------------------------------------------------------------

	Bool GenerateVehiclePartCommand::SupportsResource( const rtti::ClassType* resourceClass )
	{
		return resourceClass == ClassID< CMesh >();
	}

	CName GenerateVehiclePartCommand::GetName() const
	{
		return RED_NAME_CONSTEXPR( "generateVehiclePart" );
	}

	THandle< ResourceCommandOptions > GenerateVehiclePartCommand::CreateOptions( const THandle< ResourceCommandOptions > suggestedOptions ) const
	{
		THandle< GenerateVehiclePartOptions > options = CreateHandle< GenerateVehiclePartOptions >();
		if ( suggestedOptions && suggestedOptions->GetClass() == options->GetClass() )
		{
			suggestedOptions->GetClass()->Copy( options.Get(), suggestedOptions.Get() );
			return options;
		}

		red::StringView directoryPath = red::utils::GetParentPath( GetResourcePath().ToStringView() );
		red::StringView parentDirectoryPath = red::utils::GetParentPath( directoryPath );
		options->SetDestinationDirectory( parentDirectoryPath.Empty() ? directoryPath.ToString() : parentDirectoryPath.ToString() );
		return options;
	}

	ResourceCommandResult GenerateVehiclePartCommand::Execute( const THandle<ResourceCommandOptions>& options )
	{
		if ( !options )
		{
			return ResourceCommandResult::Error( "Core: Options are not passed" );
		}

		THandle< GenerateVehiclePartOptions > generationOptions = Cast< GenerateVehiclePartOptions >( options );
		if ( !generationOptions )
		{
			return ResourceCommandResult::Error( "Core: Invalid options type: %s ", options->GetFriendlyName().AsChar() );
		}

		String filename = red::utils::ExtractFileName( GetResourcePath().ToStringView() );
		red::utils::ReplaceExtension( filename, "ent" );

		String destinationDir = generationOptions->GetDestinationDirectory();
		if ( !red::paths::IsDirectoryPath( destinationDir ) )
		{
			destinationDir += "\\";
		}

		String templatePath = String::Printf( "%hs%hs", destinationDir.AsChar(), filename.AsChar() );
		res::ResourcePath destinationPath = res::ResourcePath::Build( templatePath );

		return GenerateTemplate( destinationPath );
	}

	THandle< AppearanceNamingParser > GenerateVehiclePartCommand::LoadParsingRulesForFile() const
	{
		const THandle< AppearancesConfig > mappings = c_appearancesMappingsFile->GetRoot< AppearancesConfig >();
		RED_ASSERT( mappings, "Unable to load rules for generating body parts" );

		red::StringView filePath = GetResourcePath().ToStringView();

		const AppearanceConfigMapping* fileMapping = nullptr;
		for ( const auto& mapping : mappings->GetMappings() )
		{
			if ( filePath.StartsWith( mapping.m_scanDirectory ) )
			{
				fileMapping = &mapping;
				break;
			}
		}

		if ( !fileMapping )
		{
			RED_LOG_ERROR( "[GenerateVehiclePartCommand]: Unable to find appropriate entity type" );
			return nullptr;
		}

		res::ResourceTokenHandle resourceToken = GResourceLoader->IssueLoadingRequest( res::ResourcePath::Build( fileMapping->m_filenameParsingRules ) );
		THandle< JsonResource > existingResource = Cast<JsonResource>( resourceToken->WaitUntilLoaded() );
		if ( !existingResource )
		{
			RED_LOG_ERROR( "[GenerateVehiclePartCommand]: File with parsing rules doesn't exist." );
			return nullptr;
		}

		return existingResource->GetRoot< AppearanceNamingParser >();
	}

	/*void GenerateVehiclePartCommand::FindExternalComponentName( CName& outExternalComponentName ) const
	{
		THandle< AppearanceNamingParser >  parsingRules = LoadParsingRulesForFile();
		if ( !parsingRules )
		{
			outExternalComponentName = RED_NAME_CONSTEXPR( "deformation_rig" );
			return;
		}

		outExternalComponentName = RED_NAME_CONSTEXPR( parsingRules->GetDefaulExtenalComponent().AsChar() );

		String fileName = red::paths::ExtractFileName( GetResourcePath().ToStringView() );
		fileName.ReplaceAll( ".mesh", ".ent" );

		std::regex base_regex( parsingRules->GetRegex().AsChar() );
		std::cmatch base_match;
		if ( !std::regex_match( fileName.AsChar(), base_match, base_regex ) )
		{
			RED_LOG_ERROR( "Filename %hs doesn't match naming convention.", fileName.AsChar() );
			return;
		}

		const auto& partsOfFileName = parsingRules->GetCaptureGroups();
		if ( (base_match.size() - 1) < partsOfFileName.Size() )
		{
			RED_LOG_ERROR( "Filename %hs doesn't match naming convention. Some required fields of filename were not found.", fileName.AsChar() );
			return;
		}

		for ( Uint32 i = 0u; i < partsOfFileName.Size(); ++i )
		{
			String fileNamePart = base_match[i + 1].str().c_str();

			for ( const auto& nameMap : partsOfFileName[i].GetExternalComponentNames() )
			{
				RED_ASSERT( nameMap.Size() == 2u );
				if ( nameMap[0] == fileNamePart )
				{
					outExternalComponentName = RED_NAME_CONSTEXPR( nameMap[1].AsChar() );
					return;
				}
			}
		}
	}*/

	ResourceCommandResult GenerateVehiclePartCommand::GenerateTemplate( const res::ResourcePath& destinationPath ) const
	{
		TResRef< CMesh > meshRef = GetResourcePath();
		THandle< CMesh > mesh = meshRef.Get();

		CName externalComponentName = RED_NAME_CONSTEXPR("deformation_rig");
		/*FindExternalComponentName( externalComponentName );*/

		// Open existing resource, if present. We will clear it and create all the components from scratch, but we can use it to retain metadata and tags.
		THandle< ent::EntityTemplate > entityTemplate;
		if ( GetResourceDepot()->ResourceExists( destinationPath ) )
		{
			res::ResourceTokenHandle resourceToken = GResourceLoader->IssueLoadingRequest( destinationPath );
			THandle< CResource > existingResource = resourceToken->WaitUntilLoaded();
			entityTemplate = Cast< ent::EntityTemplate >( existingResource );
			entityTemplate->Editor_WaitUntilCompiled();
		}

		if ( entityTemplate == nullptr )
		{
			entityTemplate = CreateHandle< ent::EntityTemplate >();    // The entity template does not exist, or is of different type.
		}

		{
			job::Builder builder{ job::Priority::Latent };
			ent::TemplateUnpackedViewPtr view = entityTemplate->CreateUnpackedView( builder );
			job::FlushCounter( builder.ExtractWaitCounter() );

			view->ClearComponents();

			THandle< ent::ExternalComponent > externalComponent = CreateHandle< ent::ExternalComponent >();
			externalComponent->SetExternalComponentName( externalComponentName );
			externalComponent->Internal_SetComponentId( RUIDGenerator().Next() );
			view->AddComponent( externalComponent );

			bool skinning = false;

			if ( skinning )
			{
				THandle< ent::SkinnedMeshComponent > meshComponent = CreateProperSkinnedMeshComponent( mesh );
				String meshComponentName = red::paths::ExtractFileStem( GetResourcePath().ToStringView() );
				meshComponentName += GenerateComponentNameRandomSuffixString( 4 );
				meshComponent->SetMesh( GetResourcePath() );
				meshComponent->SetName( RED_NAME( meshComponentName ) );
				meshComponent->Internal_SetComponentId( RUIDGenerator().Next() );

				// set transform binding to base template root component
				THandle< ent::HardTransformBinding > transformBinding = CreateHandle< ent::HardTransformBinding >();
				transformBinding->SetBindName( externalComponent->GetName() );

				const auto* prop = meshComponent->GetClass()->FindProperty( RED_NAME_CONSTEXPR_NOREG( "parentTransform" ) );
				prop->Set( meshComponent.Get(), &transformBinding );

				// set skinning binding to base template root component
				meshComponent->CreateSkinningBinding( externalComponent->GetName() );

				view->AddComponent( meshComponent );
			}
			else
			{
				CName slotName = CName::NONE();

				THandle< ent::PhysicalMeshComponent> meshComponent = CreateProperMeshComponent( mesh );
				String meshComponentName = red::paths::ExtractFileStem( GetResourcePath().ToStringView() );
				meshComponentName += GenerateComponentNameRandomSuffixString( 4 );
				meshComponent->SetMesh( GetResourcePath() );
				meshComponent->SetName( RED_NAME( meshComponentName ) );
				meshComponent->Internal_SetComponentId( RUIDGenerator().Next() );
				meshComponent->SetFilterDataSource( physics::FilterDataSource::Collider );

				THandle< ent::SlotComponent> slotComponent = CreateHandle< ent::SlotComponent >();
				slotComponent->Internal_SetComponentId( RUIDGenerator().Next() );
				String slotComponentName = "vehicle_slots";
				// slotComponentName += GenerateComponentNameRandomSuffixString( 4 );
				slotComponent->SetName( RED_NAME( slotComponentName ) );

				// set transform binding to base template root component
				THandle< ent::HardTransformBinding > transformBinding = CreateHandle< ent::HardTransformBinding >();
				transformBinding->SetBindName( externalComponent->GetName() );

				// set transform binding to base template root component
				THandle< ent::HardTransformBinding > transformBinding2 = CreateHandle< ent::HardTransformBinding >();
				transformBinding2->SetBindName( slotComponent->GetName() );
				// transformBinding2->SetSlotName( slotName );

				const auto* slotParentTransformProp = slotComponent->GetClass()->FindProperty( RED_NAME_CONSTEXPR_NOREG( "parentTransform" ) );
				slotParentTransformProp->Set( slotComponent.Get(), &transformBinding );

				const auto* meshPatentTransformProp = meshComponent->GetClass()->FindProperty( RED_NAME_CONSTEXPR_NOREG( "parentTransform" ) );
				meshPatentTransformProp->Set( meshComponent.Get(), &transformBinding2 );

				ent::Slot slot;

				slot.m_slotName = slotName;
				slot.m_boneName = slotName;

				slotComponent->Editor_AddSlot( slot );

				view->AddComponent( slotComponent );
				view->AddComponent( meshComponent );
			}
			view->SubmitChanges();
		}

		res::ResourceSavingOptions options;
		options.m_targetPath = destinationPath;
		if ( !res::SaveResource( entityTemplate, options ) )
		{
			return ResourceCommandResult::Error( String::Printf( "Core: Unable to generate '%hs' because saving failed", destinationPath.ToDebugString() ) );
		}

		if ( !entityTemplate->HasValidPath() )
		{
			// the template that we just saved is not tied to a proxy, hence won't propagate a proper resource path
			// - reset it and load through resource loader
			// This happens for newly created entities, the ones that were not existing prior to execution of this command.
			entityTemplate.Reset();
			res::ResourceTokenHandle resourceToken = GResourceLoader->IssueLoadingRequest( destinationPath );
			THandle< CResource > existingResource = resourceToken->WaitUntilLoaded();
			return ResourceCommandResult::Success( existingResource );
		}

		return ResourceCommandResult::Success( entityTemplate );
	}

	THandle<ent::PhysicalMeshComponent> GenerateVehiclePartCommand::CreateProperMeshComponent( const THandle<CMesh>& mesh )
	{
		mesh::DataViewRO meshView( *mesh );

		Bool hasGarmentStreams = false;

		for ( Uint32 chunkIndex = 0; chunkIndex < meshView.GetNumChunks(); ++chunkIndex )
		{
			mesh::ChunkStreamDataRO garmentFlagsStream = meshView.GetChunkStreamData( chunkIndex, MST_GarmentFlags_U32 );
			if ( !garmentFlagsStream.Empty() )
				hasGarmentStreams = true;
		}

		//if ( hasGarmentStreams )
		//	return CreateHandle< ent::GarmentSkinnedMeshComponent >();

		return CreateHandle< ent::PhysicalMeshComponent >();
	}

	THandle< ent::SkinnedMeshComponent > GenerateVehiclePartCommand::CreateProperSkinnedMeshComponent( const THandle< CMesh >& mesh )
	{
		/// @todo DP: we could also choose regular (non-skinned) MeshComponent here
		mesh::DataViewRO meshView( *mesh );

		Bool hasGarmentStreams = false;

		for ( Uint32 chunkIndex = 0; chunkIndex < meshView.GetNumChunks(); ++chunkIndex )
		{
			mesh::ChunkStreamDataRO garmentFlagsStream = meshView.GetChunkStreamData( chunkIndex, MST_GarmentFlags_U32 );
			if ( !garmentFlagsStream.Empty() )
				hasGarmentStreams = true;
		}

		if ( hasGarmentStreams )
			return CreateHandle< ent::GarmentSkinnedMeshComponent >();

		return CreateHandle< ent::SkinnedMeshComponent >();
	}
}

RTTI_BEGIN_TYPE_IN_NAMESPACE( GenerateVehiclePartOptions, tools );
RTTI_PARENT_TYPE( ResourceCommandOptions );
RTTI_PROPERTY( m_destinationDirectory ).editable();
RTTI_END_TYPE();
