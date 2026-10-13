/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "../../backendEngine/include/commandletEngineInstance.h"
#include "../../backendEngine/include/commandletUtils.h"
#include "../../backendEngine/include/entityTemplateDataView.h"

#include "../../../common/core/include/commandlet.h"

#include "../../../common/redReflection/include/resourceDepot.h"
#include "../../../common/resource/include/jsonSerializer.h"
#include "../../../common/redCore/include/absolutePath.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../common/redJobs2/include/jobBuilder.h"
#include "../../../common/redContainers/include/string/stringUtils.h"

#include "../../../common/gameTweakDB/include/tweakDB.h"
#include "../../../common/gameTweakDB/include/records/tweakDBCharacter.h"
#include "../../../common/gameTweakDB/include/records/tweakDBVehicle.h"
#include "../../../common/gameTweakDB/include/records/tweakDBSpawnableObjectPriority.h"
#include "../../../common/gameTweakDB/include/records/tweakDBItem.h"

#include "../../../common/game/include/visualTagsPreset.h"
#include "../../../common/game/include/spawnHelper.h"
#include "../../../common/game/include/gameVisualTagsPreset.h"
#include "../../../common/game/include/defaultAppearancePreset.h"
#include "../../../common/game/include/ethnicityComponent.h"

#include "../../../common/worldEntities/include/entityTemplate.h"
#include "../../../common/worldEntities/include/entityTemplateAppearance.h"
#include "../../../common/worldEntities/include/appearanceResource.h"
#include "../../../common/worldEntities/include/appearanceDefinition.h"
#include "../../../common/worldEntities/include/entityFactory.h"
#include "../../../common/worldEntities/include/entityFactoryGenericList.h"
#include "../../../common/worldEntities/include/animatedComponent.h"

namespace
{
	enum class MappingMode
	{
		TweakDBOnly,
		All
	};

	// data from Character_Record and Vehicle_Record
	struct RecordEntry
	{
		res::ResourcePath m_entityTemplatePath;
		red::TagList m_tagList { red::PoolBackend() };
		Uint32 m_templateResourceIndex;
	};
}

class VisualTagsCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE( VisualTagsCommandlet );

	struct DefaultEthnicity
	{
		CName appearanceName;
		CName visualTag;
	};

	typedef red::DynArray< THandle< ent::Factory > > FactoryMap;

public:
	VisualTagsCommandlet();
	~VisualTagsCommandlet();

	// ICommandlet interface
	virtual const char* GetOneLiner() const override { return "Create Entity-VisualTags-AppearanceNames map"; }
	virtual Bool Execute( const red::CommandLine& options ) override;
	virtual void PrintHelp() const override;

	virtual Bool InitializeEnvironment() override;
	virtual void TeardownEnvironment() override;

private:
	void CreateEntityFactories();
	Bool LoadTweakDBData( const red::DynArray< THandle< game::data::TweakDBRecord > >& tweakDBRecords );
	void LoadDefaultEthnicities();

	Bool InitializeResources();
	void ProcessResources_VisualTagsAppearanceNamesPreset( Bool matchTweakDbTags, red::DynArray< RecordEntry >& recordArray, THandle<game::VisualTagsAppearanceNamesPreset>& jsonPreset );
	void ProcessResources_AppearanceNameVisualTagsPreset( THandle<game::AppearanceNameVisualTagsPreset>& jsonPresetVisualTags, THandle<game::EntityTemplateDefaultAppearancePreset>& jsonPresetDefaultAppearances );

	commandlet::EngineInstance m_engineInstance;
	
	red::DynArray< RecordEntry > m_recordEntries{ red::PoolBackend() };
	red::DynArray< RecordEntry > m_itemRecordsEntries{ red::PoolBackend() };
	red::DynArray< THandle< ent::EntityTemplate > > m_templateResources{ red::PoolBackend() };

	res::ResourcePath m_defaultEntityPath;
	res::ResourcePath m_defaultEntityRigPath;
	DefaultEthnicity m_defaultMaleEthnicity;
	DefaultEthnicity m_defaultFemaleEthnicity;
	ent::GenericListFactory m_factory;
};

RTTI_BEGIN_TYPE( VisualTagsCommandlet )
	RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE()

//////////////////////////////////////////////////////////////////////////


VisualTagsCommandlet::VisualTagsCommandlet()
	: ICommandlet( RED_NAME_CONSTEXPR( "mapVisualTags" ) )
{ }

VisualTagsCommandlet::~VisualTagsCommandlet() = default;

Bool VisualTagsCommandlet::InitializeEnvironment()
{
	return m_engineInstance.Initialise();
}

void VisualTagsCommandlet::TeardownEnvironment()
{
	m_engineInstance.Shutdown();
}

//////////////////////////////////////////////////////////////////////////

namespace
{
	template < typename TArray, typename TElement >
	TElement& InsertUnique( TArray& arr, TElement && element )
	{
		auto it = std::find( arr.Begin(), arr.End(), element );
		if ( it == arr.End() )
		{
			return arr.EmplaceBack( std::forward< TElement >( element ) );
		}
		return *it;
	}

	void WriteLine( const red::StringView& line, FILE* stream = stdout )
	{
		fprintf( stream, "%hs\n", line.Data() );
		fflush( stream );
	}

	Bool SaveJsonResource( THandle<ISerializable> data, const res::ResourcePath& path )
	{
		serialization::json::JsonSerializer serializer( data );

		String jsonText;
		serializer.Serialize( jsonText );

		auto file = commandlet::GetEditorDepot()->CreateResourceWriter( path );

		if ( !file )
		{
			WriteLine( "\nCouldn't open file '" + path.ToString() + "'" );
			WriteLine( "\nDid you forget to checkout file in P4?" );
			return false;
		}

		file->Serialize( jsonText.AsChar(), jsonText.Length() );

		WriteLine( "Saved to file: " + path.ToString() );

		return true;
	}

	void PrintProgressBar( float percentage )
	{
		static const char* fullProgressBar = "||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||";
		int val = (int) ( percentage * 100 );
		int lpad = (int) ( percentage * 60 );
		int rpad = 60 - lpad;
		printf( "\r%3d%% [%.*s%*s]", val, lpad, fullProgressBar, rpad, "" );
		fflush( stdout );
	}
}

//////////////////////////////////////////////////////////////////////////


void VisualTagsCommandlet::CreateEntityFactories()
{
	const auto& counter = m_factory.Initialize( nullptr );
	job::FlushCounter( counter, true );
}

Bool VisualTagsCommandlet::LoadTweakDBData( const red::DynArray< THandle< game::data::TweakDBRecord > >& tweakDBRecords )
{
	// get data from Character_Record and Vehicle_Record
	m_recordEntries.Reserve( tweakDBRecords.Size() );
	m_itemRecordsEntries.Reserve( tweakDBRecords.Size() );

	for ( const auto& tweakDBRec : tweakDBRecords )
	{
		if ( const auto objectRec = Cast< game::data::SpawnableObject_Record >( tweakDBRec.Get() ) )
		{
			const res::ResourcePath entityTemplatePath = objectRec->entityTemplatePath().GetPath();
			if ( !entityTemplatePath.IsEmpty() )
			{
				const red::TagList TDBVisualTags = game::utils::CreateTaglist( objectRec->visualTags() );
				m_recordEntries.PushBack( { entityTemplatePath, TDBVisualTags, 0 } );	//index is initialized in LoadResources()
			}
		}
		else
		{
			if ( const auto objectRec = Cast< game::data::Item_Record >( tweakDBRec.Get() ) )
			{
				ent::GenericListFactory::EntryPublicData foundEntry = m_factory.LookupPublicData( objectRec->entityName() );
				if ( foundEntry.m_templatePath.IsValid() )
				{
					m_itemRecordsEntries.PushBack( { foundEntry.m_templatePath } );
				}
			}
		}
	}

	LoadDefaultEthnicities();

	return true;
}

void VisualTagsCommandlet::LoadDefaultEthnicities()
{
	m_defaultEntityPath = game::data::TweakDB::VGenericResourceReference( TDBID( "Ethnics.DefaultEthnicity.entityPath" ) )().GetPath();
	m_defaultEntityRigPath = res::ResourcePath::Build( "base/characters/base_entities/man_base/man_base.rig" );
	m_defaultMaleEthnicity.appearanceName = game::data::TweakDB::VCName( TDBID( "Ethnics.DefaultEthnicity.maleName" ) );
	m_defaultMaleEthnicity.visualTag = game::data::TweakDB::VCName( TDBID( "Ethnics.DefaultEthnicity.maleVisualTag" ) );
	m_defaultFemaleEthnicity.appearanceName = game::data::TweakDB::VCName( TDBID( "Ethnics.DefaultEthnicity.femaleName" ) );
	m_defaultFemaleEthnicity.visualTag = game::data::TweakDB::VCName( TDBID( "Ethnics.DefaultEthnicity.femaleVisualTag" ) );
}

void InitializeRecordArrayResources( red::DynArray< RecordEntry >& recordArray, red::DynArray< THandle< ent::EntityTemplate > >& templateResources,
									 red::DynArray< Uint64 >& loadedTemplatePaths, Uint32 alreadyInitializedRecordsCount, Uint32 totalRecordsCount )
{
	for( Uint32 i = 0; i < recordArray.Size(); ++i )
	{
		RecordEntry& recordEntry = recordArray[ i ];
		Uint64 entityTemplatePathHash = recordEntry.m_entityTemplatePath.GetHash();

		auto loadedTemplateIt = std::find( loadedTemplatePaths.Begin(), loadedTemplatePaths.End(), entityTemplatePathHash );
		if ( loadedTemplateIt == loadedTemplatePaths.End() )
		{
			RED_ASSERT( loadedTemplatePaths.Size() == templateResources.Size() );
			
			recordEntry.m_templateResourceIndex = loadedTemplatePaths.Size();
			loadedTemplatePaths.PushBack( entityTemplatePathHash );
			
			res::ResourceTokenHandle token = GResourceLoader->IssueLoadingRequest( recordEntry.m_entityTemplatePath );
			auto resource = token->WaitUntilLoaded();
			if ( token->GetError() == res::ResourceTokenErrorType::None )
			{
				THandle<ent::EntityTemplate> templateResource = Cast< ent::EntityTemplate >( resource );
				RED_ASSERT( templateResource, "Failed to load TemplateResource: %s", resource->GetPath().ToDebugString() );

				templateResources.PushBack( std::move( templateResource ) );
			}
			else
			{
				WriteLine( "\nOne of Records has invalid entityPath: " + recordEntry.m_entityTemplatePath.ToString() + "!" );

				templateResources.PushBack( nullptr );
			}
		}
		else
		{
			recordEntry.m_templateResourceIndex = red::Distance< Uint32 >( loadedTemplatePaths.Begin(), loadedTemplateIt );
		}

		PrintProgressBar( static_cast<Float>( alreadyInitializedRecordsCount + i + 1 ) / static_cast<Float>( totalRecordsCount ) );
	}
}

Bool VisualTagsCommandlet::InitializeResources()
{
	const Uint32 totalRecordsCount = m_recordEntries.Size() + m_itemRecordsEntries.Size();

	m_templateResources.Reserve( totalRecordsCount );

	red::DynArray< Uint64 > loadedTemplatePaths{ red::PoolBackend() };
	loadedTemplatePaths.Reserve( totalRecordsCount );

	// Start loading entity templates
	InitializeRecordArrayResources( m_recordEntries, m_templateResources, loadedTemplatePaths, 0, totalRecordsCount );
	InitializeRecordArrayResources( m_itemRecordsEntries, m_templateResources, loadedTemplatePaths, m_recordEntries.Size(), totalRecordsCount );

	return true;
}

CName FindTemplateAppearanceName( const red::DynArray< ent::TemplateAppearance >& appearanceTemplates, CName appearanceName, const Uint64 resourceHash )
{
	auto appNameIt = std::find_if( appearanceTemplates.Begin(), appearanceTemplates.End(), [ appearanceName, resourceHash ]( const ent::TemplateAppearance& templateAppearance )
	{
		return templateAppearance.GetAppearanceName() == appearanceName && templateAppearance.GetPath().GetHash() == resourceHash;
	} );

	if ( appNameIt != appearanceTemplates.End() )
	{
		return appNameIt->GetName();
	}

	return CName::NONE();
}
void VisualTagsCommandlet::ProcessResources_VisualTagsAppearanceNamesPreset( Bool matchTweakDbTags, red::DynArray< RecordEntry >& recordArray, THandle<game::VisualTagsAppearanceNamesPreset>& jsonPreset )
{
	jsonPreset->m_presets.Reserve( m_templateResources.Size() );

	const Uint32 entityTemplatesCount = recordArray.Size();
	for ( Uint32 i = 0; i < entityTemplatesCount; ++i )
	{
		const RecordEntry& recordEntry = recordArray[ i ];
		
		const res::ResourcePath entityPath = recordEntry.m_entityTemplatePath;
		const red::TagList& TDBVisualTags = recordEntry.m_tagList;
		const THandle< ent::EntityTemplate > resTemplate = m_templateResources[ recordEntry.m_templateResourceIndex ];

		if ( !resTemplate )
		{
			continue;
		}

		game::VisualTagsAppearanceNamesPreset_Entity& entityPreset = InsertUnique( jsonPreset->m_presets, game::VisualTagsAppearanceNamesPreset_Entity( entityPath ) );

		// load all entity appearances
		const red::DynArray< ent::TemplateAppearance > & appearanceTemplates = resTemplate->GetAppearances();
		red::Set< Uint64 > parsedAppearanceResources;
		for ( const auto & appearanceTempl : appearanceTemplates )
		{
			const auto& appearanceResRef = appearanceTempl.GetAppearanceResource();
			if ( !appearanceResRef.IsValid() )
			{
				continue;
			}
			
			// if already parsed through that appearanceResource, continue
			if ( !parsedAppearanceResources.Insert( appearanceResRef.GetPath().GetHash() ).IsSuccessful() )
			{
				continue;
			}

			// load AppearanceResource
			if ( auto appearanceRes = Cast< appearance::AppearanceResource >( appearanceResRef.IssueLoadingRequest()->WaitUntilLoaded() ) )
			{
				red::DynArray< THandle< appearance::AppearanceDefinition > > appearanceDefs = appearanceRes->GetAppearances();

				// iterate on AppearanceDefinitions and see if any matches tags from Character_Record or Vehicle_Record
				for ( auto & appearanceDef : appearanceDefs )
				{
					CName appearanceTemplatedName = FindTemplateAppearanceName( appearanceTemplates, appearanceDef->GetName(), appearanceRes->GetPath().GetHash() );

					// don't parse appearances which are not included in current entity
					if ( appearanceTemplatedName.Empty() )
					{
						continue;
					}

					red::TagList appearanceVisualTags { red::PoolBackend() };
					const Bool notEmpty = appearanceDef->Editor_GetAllVisualTags( appearanceVisualTags );
					if ( !matchTweakDbTags || red::TagList::MatchAll( TDBVisualTags, appearanceVisualTags ) )
					{
						CName TDBVisualTagNames = RED_NAME( TDBVisualTags.ToString() );

						game::VisualTagsAppearanceNamesPreset_TagsAppearances entryToFind( TDBVisualTagNames );
						auto exisitingVisualTagsEntry = std::find( entityPreset.m_tagsToAppearances.Begin(),
																   entityPreset.m_tagsToAppearances.End(),
																   entryToFind );

						if ( exisitingVisualTagsEntry != entityPreset.m_tagsToAppearances.End() )
						{
							auto& matchingAppearanceNames = exisitingVisualTagsEntry->m_appearanceNames;
							if ( !matchingAppearanceNames.Exist( appearanceTemplatedName ) )
							{
								matchingAppearanceNames.EmplaceBack( std::move( appearanceTemplatedName ) );
							}
						}
						else
						{
							entityPreset.m_tagsToAppearances.EmplaceBack( game::VisualTagsAppearanceNamesPreset_TagsAppearances{ TDBVisualTagNames, { appearanceTemplatedName } } );
						}
					}
				}
			}
		}

		PrintProgressBar( static_cast<Float>( i + 1 ) / static_cast<Float>( entityTemplatesCount ) );
	}
}

red::TagList FindCommonTags( const red::DynArray< game::AppearanceNameVisualTagsPreset_AppearanceTags >& appearanceEntries )
{
	RED_FATAL_ASSERT( !appearanceEntries.Empty(), "expected at least one entry" );
	
	red::DynArray< CName > commonTags{ appearanceEntries[ 0 ].m_visualTags.GetTagNames(), red::PoolBackend() };
	
	const auto removeIt = std::remove_if( commonTags.Begin(), commonTags.End(), [ &appearanceEntries ]( const CName& tag )
	{
		for ( const game::AppearanceNameVisualTagsPreset_AppearanceTags& appearanceEntry : appearanceEntries )
		{
			if ( !appearanceEntry.m_visualTags.HasTag( tag ) )
			{
				//this tag is not common, appearanceEntry doesn't have it
				return true;
			}
		}

		return false;
	} );
	commonTags.Remove( removeIt, commonTags.End() );
	
	return red::TagList( commonTags, game::PoolGMPL_Population_Data() );
}

void RemoveCommonTags( const red::TagList& commonTags, red::DynArray< game::AppearanceNameVisualTagsPreset_AppearanceTags >& appearanceEntries )
{
	for ( game::AppearanceNameVisualTagsPreset_AppearanceTags& appearanceEntry : appearanceEntries )
	{
		appearanceEntry.m_visualTags.RemoveTags( commonTags );
	}

	const auto removeIt = std::remove_if( appearanceEntries.Begin(), appearanceEntries.End(), []( const game::AppearanceNameVisualTagsPreset_AppearanceTags& entry )
	{
		return entry.m_visualTags.Empty();
	} );
	appearanceEntries.Remove( removeIt, appearanceEntries.End() );
}

void VisualTagsCommandlet::ProcessResources_AppearanceNameVisualTagsPreset( THandle<game::AppearanceNameVisualTagsPreset>& jsonPresetVisualTags, THandle<game::EntityTemplateDefaultAppearancePreset>& jsonPresetDefaultAppearances )
{
	const Uint32 entityTemplatesCount = m_templateResources.Size();
	jsonPresetVisualTags->m_presets.Reserve( entityTemplatesCount );

	for ( Uint32 i = 0; i < entityTemplatesCount; ++i )
	{
		const THandle< ent::EntityTemplate >& entityTempl = m_templateResources[ i ];
		if ( !entityTempl )
		{
			continue;
		}

		job::Builder builder;
		ent::TemplateDataView entityView;
		entityView.Reset( entityTempl, builder, true );
		entityView.Rebuild( builder );
		job::FlushCounter( builder.ExtractWaitCounter() );

		res::ResourcePath rigPath;

		const auto& components = entityView.GetResolvedComponents();
		for ( const auto& comp : components )
		{
			if ( THandle< ent::AnimatedComponent > animatedComponent = Cast< ent::AnimatedComponent >( comp ) )
			{
				if ( animatedComponent->GetName() == RED_NAME_CONSTEXPR_NOREG( "root" ) )
				{
					rigPath = animatedComponent->GetRig().GetPath();
					break;
				}
			}
		}

		game::AppearanceNameVisualTagsPreset_Entity entityTemplPreset( entityTempl->GetPath(), rigPath );
		red::Set< Uint64 > parsedAppearanceResources;
		Bool atLeastOneEmpty = false;
		const red::DynArray< ent::TemplateAppearance >& appearanceTemplates = entityTempl->GetAppearances();
		for ( const ent::TemplateAppearance& appearanceTempl : appearanceTemplates )
		{
			const auto& appearanceResRef = appearanceTempl.GetAppearanceResource();
			if ( !appearanceResRef.IsValid() )
			{
				continue;
			}

			// if already parsed through that appearanceResource, continue
			if ( !parsedAppearanceResources.Insert( appearanceResRef.GetPath().GetHash() ).IsSuccessful() )
			{
				continue;
			}

			if ( auto appearanceRes = Cast< appearance::AppearanceResource >( appearanceResRef.IssueLoadingRequest()->WaitUntilLoaded() ) )
			{
				red::DynArray< THandle< appearance::AppearanceDefinition > > appearanceDefs = appearanceRes->GetAppearances();
				entityTemplPreset.m_appearancesToTags.Reserve( entityTemplPreset.m_appearancesToTags.Size() + appearanceDefs.Size() );

				for ( const auto& appearanceDef : appearanceDefs )
				{
					const CName appearanceNameFromResource = appearanceDef->GetName();
					CName appearanceTemplatedName = FindTemplateAppearanceName( appearanceTemplates, appearanceNameFromResource, appearanceRes->GetPath().GetHash() );
					// don't parse appearances which are not included in current entity

					if ( appearanceTemplatedName.Empty() )
					{
						continue;
					}
					
					red::TagList appearanceVisualTags { red::PoolBackend() };
					const Bool notEmpty = appearanceDef->Editor_GetAllVisualTags( appearanceVisualTags );
					if ( !notEmpty )
					{
						atLeastOneEmpty = true;
					}
					else
					{
						game::AppearanceNameVisualTagsPreset_AppearanceTags& appNamePreset = entityTemplPreset.m_appearancesToTags.EmplaceBack( appearanceTemplatedName );
						appNamePreset.m_visualTags.AddTags( appearanceVisualTags );
					}
				}
			}
		}

		if ( !entityTemplPreset.m_appearancesToTags.Empty() )
		{
			if ( !atLeastOneEmpty )
			{
				const red::TagList commonVisualTags = FindCommonTags( entityTemplPreset.m_appearancesToTags );
				RemoveCommonTags( commonVisualTags, entityTemplPreset.m_appearancesToTags );
				entityTemplPreset.m_commonVisualTags = std::move( commonVisualTags );
			}
			
			jsonPresetVisualTags->m_presets.PushBack( std::move( entityTemplPreset ) );
		}

		const CName defaultAppearance = entityTempl->GetDefaultAppearanceName();

		if ( !defaultAppearance.Empty() && !red::IsAllWhiteSpace( defaultAppearance.AsChar() ) && defaultAppearance != ent::TemplateAppearance::c_RandomAppearanceName )
		{
			game::DefaultAppearancePreset_Entity defaultAppearancePreset( entityTempl->GetPath(), defaultAppearance );
			jsonPresetDefaultAppearances->m_defaultAppearancePresets.PushBack( std::move( defaultAppearancePreset ) );
		}

		PrintProgressBar( static_cast<Float>( i + 1 ) / static_cast<Float>( entityTemplatesCount ) );
	}

	// add defaults by hand
	
	game::AppearanceNameVisualTagsPreset_Entity defaultEntityTemplPreset( m_defaultEntityPath, m_defaultEntityRigPath );
	auto& defMaleAppearanceEntry = defaultEntityTemplPreset.m_appearancesToTags.EmplaceBack( m_defaultMaleEthnicity.appearanceName );
	defMaleAppearanceEntry.m_visualTags.AddTag( m_defaultMaleEthnicity.visualTag );
	auto& defFemaleAppearanceEntry = defaultEntityTemplPreset.m_appearancesToTags.EmplaceBack( m_defaultFemaleEthnicity.appearanceName );
	defFemaleAppearanceEntry.m_visualTags.AddTag( m_defaultFemaleEthnicity.visualTag );
	jsonPresetVisualTags->m_presets.PushBack( std::move( defaultEntityTemplPreset ) );
}

//----------------------------------------------------------------------------

Bool VisualTagsCommandlet::Execute( const red::CommandLine& options )
{
	WriteLine( "Running visualTags<->appearances mapping" );
	
	// 1. Load Records from TweakDB
	WriteLine( "Loading TweakDB..." );
	game::data::InitTweakDB();
	game::data::GetTweakDB().LoadRawFiles();
	red::DynArray< THandle< game::data::TweakDBRecord > > tweakDBRecords = game::data::GetTweakDB().GetRecords< game::data::Character_Record >();
	tweakDBRecords.PushBack( game::data::GetTweakDB().GetRecords< game::data::Vehicle_Record >() );
	tweakDBRecords.PushBack( game::data::GetTweakDB().GetInheritingRecords< game::data::Item_Record >() );

	if ( tweakDBRecords.Empty() )
	{
		WriteLine( "No character, no vehicle records!" );
	}
	else
	{
		WriteLine( "Character and Vehicle Records form TweakDB loaded! (" + ToStringDirect( tweakDBRecords.Size() ) + ")" );
	}

	CreateEntityFactories();

	LoadTweakDBData( tweakDBRecords );
	tweakDBRecords.Clear();
	game::data::DeinitTweakDB();

	// 2. Load TemplateResource (entities) referenced in loaded Records 
	WriteLine( "Loading resources... (" + ToStringDirect( m_recordEntries.Size() + m_itemRecordsEntries.Size() ) + ")" );
	InitializeResources();

	// 3. Combine TemplateResources with visualTags form Records
	WriteLine( "\nFinished loading resources." );
	
	WriteLine( "Creating visualTags->appearances map..." );
	THandle< game::VisualTagsAppearanceNamesPreset > processedData_VisualTagsAppearanceNames = CreateHandle< game::VisualTagsAppearanceNamesPreset >();
	ProcessResources_VisualTagsAppearanceNamesPreset( true, m_recordEntries, processedData_VisualTagsAppearanceNames );
	ProcessResources_VisualTagsAppearanceNamesPreset( false, m_itemRecordsEntries, processedData_VisualTagsAppearanceNames );

	WriteLine( "\nCreating appearances->visualTags map..." );
	THandle< game::AppearanceNameVisualTagsPreset > processedData_AppearanceNameVisualTags = CreateHandle< game::AppearanceNameVisualTagsPreset >();
	THandle< game::EntityTemplateDefaultAppearancePreset > processedData_DefaultAppearances = CreateHandle< game::EntityTemplateDefaultAppearancePreset >();
	ProcessResources_AppearanceNameVisualTagsPreset( processedData_AppearanceNameVisualTags, processedData_DefaultAppearances );

	// 4. Save to json
	WriteLine( "\nFinished entities iteration. Saving to file..." );
	
	const res::ResourcePath outputFileResourcePath_VisualTagsAppearanceNames = res::ResourcePath::Build( game::VisualTagHelper::s_visualTagAppearancesMapPath );
	SaveJsonResource( processedData_VisualTagsAppearanceNames, outputFileResourcePath_VisualTagsAppearanceNames );

	const res::ResourcePath outputFileResourcePath_AppearanceNameVisualTags = res::ResourcePath::Build( game::VisualTagHelper::s_appearanceVisualTagMapPath );
	SaveJsonResource( processedData_AppearanceNameVisualTags, outputFileResourcePath_AppearanceNameVisualTags );

	const res::ResourcePath outputFileResourcePath_DefaultAppearances = res::ResourcePath::Build( game::VisualTagHelper::s_defaultAppearancesMapPath );
	SaveJsonResource( processedData_DefaultAppearances, outputFileResourcePath_DefaultAppearances );

	// 5. Free resources
	m_templateResources.Clear();

	return true;
}

void VisualTagsCommandlet::PrintHelp() const
{
	RED_LOG( "Usage:" );
	RED_LOG( "    mapVisualTags" );
}