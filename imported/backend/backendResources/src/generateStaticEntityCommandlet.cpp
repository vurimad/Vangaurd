/**
* Copyright c 2020 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "../../../common/core/include/commandlet.h"
#include "../../backendEngine/include/commandletEngineInstance.h"
#include "../../backendEngine/include/factory.h"
#include "../../../common/resource/include/resourceSaver.h"
#include "../../../common/redReflection/include/resourceClassLookup.h"
#include "../../../common/worldEntities/include/entityTemplate.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/redJobs2/include/jobBuilder.h"
#include "../../../common/worldEntities/include/entityTemplateUnpackedView.h"
#include "../../../common/worldEntities/include/entityTemplateAppearance.h"
#include "../../../common/worldEntities/include/appearanceResource.h"
#include "../../../common/worldEntities/include/appearanceDefinition.h"
#include "../../../common/worldEntities/include/appearancePart.h"
#include "../../../common/worldEntities/include/animatedComponent.h"
#include "../../../common/worldEntities/include/animationControllerComponent.h"
#include "../../../common/worldEntities/include/slotComponent.h"
#include "../../../common/worldEntities/include/entityTemplateInclude.h"
#include "../../backendEngine/include/entityTemplateDataView.h"
#include "../../../common/worldEntities/include/transformBinding.h"
#include "../../../common/game/include/interactionComponent.h"

enum class MessageType
{
	Log,
	Warning,
	Error
};

/// Commandlet that generate static entity (non-animated & lightweight) version of target entity
class GenerateStaticEntityCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE( GenerateStaticEntityCommandlet );

public:
	GenerateStaticEntityCommandlet();
	~GenerateStaticEntityCommandlet();

	// ICommandlet interface
	virtual const char* GetOneLiner() const override { return "Generate static entity"; }
	virtual Bool Execute( const red::CommandLine& options ) override;
	virtual void PrintHelp() const override;

	virtual Bool InitializeEnvironment() override;
	virtual void TeardownEnvironment() override;
private:
	void ParseParams(  const red::CommandLine& options );
	Bool GenerateStaticEntity( const res::ResourcePath sourcePath, Bool force );
	void GenerateStaticEntityTemplate( THandle<ent::EntityTemplate> source, THandle<ent::EntityTemplate> target ); // remove unused components and update iplacedComponent positions
	Bool EnsureResourceExist( const res::ResourcePath path, THandle<CResource> source);// generate new entity file and copy original file
	Bool ContainsRemoveableComponents( THandle<ent::EntityTemplate> source );
	Bool IsComponentCanBeRemoved( ent::ComponentHandle component );
	void ParseInputs( const red::String& paramName, const red::CommandLine& options, red::HashSet<String>& results );

	const res::ResourcePath GetStaticPath( const res::ResourcePath path );
	void Log(const String& message, const MessageType type );
	
	commandlet::EngineInstance m_engineInstance;

	red::DynArray<res::ResourcePath> m_assetToProcessPaths;
	red::HashSet<res::ResourcePath> m_generatedResourcePaths;
	red::HashSet<res::ResourcePath> m_processedEntityPaths;
	red::HashSet<String > m_componentsToRemove;
	red::HashSet<String > m_componentNamesToKeep;
	red::HashSet<String > m_componentNamesToRemove;
	red::DynArray<std::pair< red::String, MessageType>> m_logMessages;

	static const red::String s_resourceFileParamName;
	static const red::String s_componentsToRemoveFileParamName;
	static const red::String s_nameToKeepFileParamName;//component with this name will be retained
	static const red::String s_nameToRemoveFileParamName;//component with this name will be removed

};

RTTI_BEGIN_TYPE( GenerateStaticEntityCommandlet )
	RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE()

GenerateStaticEntityCommandlet::GenerateStaticEntityCommandlet()
	: ICommandlet( RED_NAME_CONSTEXPR( "generateStaticEntity" ) )
{
}

GenerateStaticEntityCommandlet::~GenerateStaticEntityCommandlet()
{
}

const red::String GenerateStaticEntityCommandlet::s_resourceFileParamName = "assetlist";
const red::String GenerateStaticEntityCommandlet::s_componentsToRemoveFileParamName = "componentsToRemoveList";
const red::String GenerateStaticEntityCommandlet::s_nameToKeepFileParamName = "componentNamesToKeepList";
const red::String GenerateStaticEntityCommandlet::s_nameToRemoveFileParamName = "componentNamesToRemoveList";

Bool GenerateStaticEntityCommandlet::Execute( const red::CommandLine& options )
{
	m_componentsToRemove.Clear();
	m_componentNamesToKeep.Clear();
	m_componentNamesToRemove.Clear();
	m_assetToProcessPaths.Clear();
	m_generatedResourcePaths.Clear();
	m_processedEntityPaths.Clear();
	m_logMessages.Clear();

	//parse params
	ParseParams( options );
	
	for (const res::ResourcePath resourcePath : m_assetToProcessPaths)
	{
		GenerateStaticEntity( resourcePath , true );
	}

	for (const auto& log : m_logMessages)
	{
		switch ( log.second )
		{
		case MessageType::Error:
			RED_LOG_ERROR( log.first.AsChar() );
			break;
		case MessageType::Warning:
			RED_LOG_WARNING( log.first.AsChar() );
			break;
		default:
			RED_LOG( log.first.AsChar() );
			break;
		}
	}

	return true;
}

void GenerateStaticEntityCommandlet::PrintHelp() const
{
	RED_LOG( "Usage:"  );
	RED_LOG( "    -%s= path to list of assets to process" , s_resourceFileParamName );
	RED_LOG( "    -%s= optional path to additional list of components to remove" , s_componentsToRemoveFileParamName );
	RED_LOG( "    -%s= optional path to additional list of component names to keep" , s_nameToKeepFileParamName );
	RED_LOG( "    -%s= optional path to additional list of component names to remove" , s_nameToRemoveFileParamName );
	RED_LOG( "    (note: animated components will be removed as default)" );
}

Bool GenerateStaticEntityCommandlet::InitializeEnvironment()
{
	return m_engineInstance.Initialise();
}

void GenerateStaticEntityCommandlet::TeardownEnvironment()
{
	m_engineInstance.Shutdown();
}

void GenerateStaticEntityCommandlet::ParseParams(const red::CommandLine& options)
{
	

	if (options.HasOption(s_resourceFileParamName))
	{
		red::String inputFilePath;//path list of file
		if (options.GetFirstParam(s_resourceFileParamName, inputFilePath))
		{
			const red::AbsolutePath assetListPath = red::AbsolutePath::ParseFilePath(inputFilePath);
			if (assetListPath.IsFilePath())
			{
				String contents;
				if (red::LoadFileToString(assetListPath, contents))
				{
					const red::DynArray<red::String> parsedContents = contents.Split(red::DynArray< String >{ ";", ",","\t","\n"});

					m_assetToProcessPaths.Reserve(parsedContents.Size());

					for (const auto& str : parsedContents)
					{
						const res::ResourcePath path = res::ResourcePath::Build(str);
						if (path.IsValid())
						{
							m_assetToProcessPaths.PushBack(path);
						}
					}
				}
			}
		}
	}
	else
	{
		Log("Fatal Error, no asset list found", MessageType::Error);
	}

	ParseInputs( s_componentsToRemoveFileParamName, options, m_componentsToRemove );

	ParseInputs( s_nameToKeepFileParamName, options, m_componentNamesToKeep );

	ParseInputs( s_nameToRemoveFileParamName, options, m_componentNamesToRemove );

}

Bool GenerateStaticEntityCommandlet::GenerateStaticEntity( const res::ResourcePath sourcePath , Bool force )
{
	if (m_processedEntityPaths.Exist(sourcePath) )
	{
		return true;
	}

	res::ResourceTokenHandle sourceEntityToken = GResourceLoader->IssueLoadingRequest( sourcePath );
	sourceEntityToken->WaitUntilLoaded();
	THandle<ent::EntityTemplate> sourceEntityTemplate = Cast<ent::EntityTemplate>(sourceEntityToken->GetResource());

	if ( force == false && ContainsRemoveableComponents(sourceEntityTemplate) == false)
	{
		return false;
	}

	const res::ResourcePath staticEntityPath = GetStaticPath(sourcePath);

	//try create resource if not exist also copy source to this new resource
	if ( EnsureResourceExist(staticEntityPath, sourceEntityToken->GetResource() ) == false)
	{
		//something goes wrong
		return false;
	}
	
	res::ResourceTokenHandle staticEntityToken = GResourceLoader->IssueLoadingRequest( staticEntityPath );
	staticEntityToken->WaitUntilLoaded();

	THandle<ent::EntityTemplate> staticEntityTemplate = Cast<ent::EntityTemplate>(staticEntityToken->GetResource());
	
	for (Uint32 i = 0; i < sourceEntityTemplate->GetIncludes().Size(); ++i)
	{
		const ent::TemplateInclude& sourceInclude = sourceEntityTemplate->GetIncludes()[i];
		//generate static version of include only if needed
		if ( GenerateStaticEntity(sourceInclude.GetTemplate().GetPath(), false ))
		{
			staticEntityTemplate->GetIncludes()[i].SetTemplate(GetStaticPath(sourceInclude.GetTemplate().GetPath()));
		}
	}

	//clean entity
	GenerateStaticEntityTemplate( sourceEntityTemplate, staticEntityTemplate );

	//generate appearance copy
	for( Uint32 i = 0; i < sourceEntityTemplate->GetAppearances().Size();++i)
	{
		const ent::TemplateAppearance& sourceTemplateAppearance = sourceEntityTemplate->GetAppearances()[i];

		const res::ResourcePath staticAppearancePath = GetStaticPath(sourceTemplateAppearance.GetPath());

		res::ResourceTokenHandle appearanceToken = sourceTemplateAppearance.GetAppearanceResource().IssueLoadingRequest();
		appearanceToken->WaitUntilLoaded();

		if (EnsureResourceExist(staticAppearancePath, appearanceToken->GetResource() ) )
		{
			
			ent::TemplateAppearance& staticTemplateAppearance = staticEntityTemplate->GetAppearances()[i];

			staticTemplateAppearance.SetAppearanceResource(staticAppearancePath);
			
			const THandle<appearance::AppearanceResource> sourceAppearance = Cast<appearance::AppearanceResource>( appearanceToken->GetResource() );

			res::ResourceTokenHandle staticAppearanceToken = staticTemplateAppearance.GetAppearanceResource().IssueLoadingRequest();
			staticAppearanceToken->WaitUntilLoaded();
			THandle<appearance::AppearanceResource> staticAppearance = Cast<appearance::AppearanceResource>( staticAppearanceToken->GetResource() );

			if( sourceAppearance->GetBaseEntity().IsValid() )
			{
				if ( GenerateStaticEntity(sourceAppearance->GetBaseEntity(), false) )
				{
					staticAppearance->SetBaseEntity( GetStaticPath(sourceAppearance->GetBaseEntity()) );
				}
			}

			for (Uint32 j = 0; j < sourceAppearance->GetAppearancesCount(); ++j)
			{
				const THandle<appearance::AppearanceDefinition> sourceAppearanceDef = sourceAppearance->GetAppearances()[j];
				THandle<appearance::AppearanceDefinition> staticAppearanceDef = staticAppearance->GetAppearances()[j];

				for (Uint32 k = 0; k < sourceAppearanceDef->GetParts().Size(); ++k)
				{
					const appearance::AppearancePart& sourceAppearancePart = sourceAppearanceDef->GetParts()[k];
					
					res::ResourcePath sourceAppearancePartPath = sourceAppearancePart.GetPath();
					if (sourceAppearancePartPath.IsValid())
					{
						if (GenerateStaticEntity(sourceAppearancePartPath, false))
						{
							staticAppearanceDef->UpdatePart( k, GetStaticPath(sourceAppearancePartPath));
						}
					}
				}
			}

			//save appearances
			res::ResourceSavingOptions saveOptions;
			saveOptions.m_targetPath = staticAppearancePath;
			res::SaveResource( staticAppearance, saveOptions );
		}
	}

	res::ResourceSavingOptions saveOptions;
	saveOptions.m_targetPath = staticEntityPath;
	if ( !res::SaveResource( staticEntityTemplate, saveOptions ) )
	{
		Log( red::String::Printf("Static entity failed to save %s", staticEntityPath.ToDebugString()), MessageType::Error );
		return false;
	}

	m_processedEntityPaths.Insert(sourcePath);
	return true;
}

void GenerateStaticEntityCommandlet::GenerateStaticEntityTemplate( THandle<ent::EntityTemplate> source, THandle<ent::EntityTemplate> target )
{
	ent::TemplateDataView originalTemplateView;
	{
		job::Builder builder{ job::Priority::Latent };
		originalTemplateView.Reset( source, builder );
		originalTemplateView.Rebuild( builder, true );
		job::FlushCounter( builder.ExtractWaitCounter() );
	}

	ent::TemplateDataView targetTemplateView;
	{
		job::Builder builder{ job::Priority::Latent };
		targetTemplateView.Reset( target, builder );
		targetTemplateView.Rebuild( builder, true );
		job::FlushCounter( builder.ExtractWaitCounter() );
	}

	//source entity resolved components
	const red::DynArray<ent::ComponentHandle> originalEntityResolvedComponents = originalTemplateView.GetResolvedComponents();
	const red::DynArray<ent::ComponentHandle> targetEntityResolvedComponents = targetTemplateView.GetResolvedComponents();
	
	//mapping contains included components before resolving
	red::HashMap<CName, ent::ComponentHandle> originalNameComponentRelationMap;
	for (ent::ComponentHandle comp : originalEntityResolvedComponents)
	{
		originalNameComponentRelationMap.Insert( comp->GetName(), comp );
	}
	
	red::HashMap<CName, ent::ComponentHandle> originalNameIncludeComponentRelationMap;
	for ( ent::TemplateInclude& include : source->GetIncludes() )
	{
		res::ResourceTokenHandle includeToken = include.GetTemplate().IssueLoadingRequest();
		includeToken->WaitUntilLoaded();

		if (THandle<ent::EntityTemplate> includeTemplate = Cast<ent::EntityTemplate>(includeToken->GetResource()))
		{
			job::Builder builder{ job::Priority::Latent };
			ent::TemplateUnpackedViewPtr includeEntityView = source->CreateUnpackedView( builder );
			job::FlushCounter( builder.ExtractWaitCounter() );

			for ( ent::ComponentHandle comp : includeEntityView->GetComponents())
			{
				originalNameIncludeComponentRelationMap.Insert(comp->GetName(), comp );
			}
		}
	}

	red::DynArray<ent::ComponentHandle> compToRemove;

	for ( const ent::ComponentHandle& comp : targetEntityResolvedComponents )
	{
		
		Bool compRemoved = false;
		if (THandle<ent::SlotComponent> slotComp = Cast<ent::SlotComponent>(comp))
		{
			if (THandle<ent::HardTransformBinding> hardTransformBinding = Cast<ent::HardTransformBinding>( slotComp->GetParentTransform() ))
			{
				if ( originalNameComponentRelationMap.KeyExist(hardTransformBinding->GetBindName() ))
				{
					if (originalNameComponentRelationMap[hardTransformBinding->GetBindName()]->IsA<ent::AnimatedComponent>())
					{
						//if slot component connected to animated component, remove it!
						compToRemove.PushBack(comp);
						compRemoved = true;
					}
					else if (THandle<game::interactions::Component> interactionComp = Cast<game::interactions::Component>(originalNameComponentRelationMap[hardTransformBinding->GetBindName()]))
					{
						//if slot component connected to interaction component, copy it's initial transform!
						const Transform initialTransform = interactionComp->GetInitialTransform();
						slotComp->SetInitialPlacement( initialTransform.GetPosition().AsVector3(), initialTransform.GetOrientation(), false );	
					}
				}
			}
		}
		else
		{
			if (THandle<ent::IPlacedComponent> placedComp = Cast<ent::IPlacedComponent>(comp))
			{
				if (THandle<ent::HardTransformBinding> compTransformBinding = Cast<ent::HardTransformBinding>( placedComp->GetParentTransform() ))
				{
					if (originalNameComponentRelationMap.KeyExist(compTransformBinding->GetBindName()))
					{
						//if connected to slot component & this slot component is connected to animated component, recalculate initial position to match rig position
						if (THandle<ent::SlotComponent> slotComp = Cast<ent::SlotComponent>( originalNameComponentRelationMap[compTransformBinding->GetBindName()] ))
						{
							if (THandle<ent::HardTransformBinding> slotTransformBinding = Cast<ent::HardTransformBinding>( slotComp->GetParentTransform() ))
							{
								if (THandle<ent::AnimatedComponent> animComp = Cast<ent::AnimatedComponent>(originalNameComponentRelationMap[slotTransformBinding->GetBindName()]))
								{
									if (animComp->GetRig() && animComp->GetRig()->IsDefaultResource() == false)
									{
										ent::SlotId slotId = slotComp->Find(compTransformBinding->GetSlotName());

										if (slotId >= 0)
										{
											const ent::Slot& slot = slotComp->Editor_GetSlots()[slotId];

											Int32 boneIndex = animComp->GetRig()->FindBoneByName( slot.m_boneName );

											if(boneIndex >= 0)
											{
												
												const Transform initialPlacement = placedComp->GetInitialTransform();
												const Transform slotOffset = initialPlacement * Transform( slot.m_relativePosition , slot.m_relativeRotation );
												const Transform transformLs = WorldTransform( AnimTransformToTransform(animComp->GetRig()->GetReferencePoseMS(boneIndex))).TransformXForm(slotOffset)._ToXForm();

												placedComp->SetInitialPlacement(transformLs.GetPosition3() , transformLs.GetOrientation());
												if(targetTemplateView.IsOverridenIncludedComponent(slotComp))
												{
													//if the slot is overriden apply this instance data
													target->Editor_WriteComponentIncludeInstanceData(comp, originalNameIncludeComponentRelationMap[comp->GetName()]);
												}
											}
										}
									}
								}
							}
						}
					}
				}
			}
		}


		if ( compRemoved == false && IsComponentCanBeRemoved( comp ) )
		{
			compToRemove.PushBack(comp);
		}
	}

	for (const ent::ComponentHandle& comp : compToRemove)
	{
		targetTemplateView.RemoveComponent(comp);
		Log( red::String::Printf("%s component removed, %s", comp->GetClass()->GetName().AsChar(), target->GetPath().ToDebugString() ), MessageType::Warning );
	}

	targetTemplateView.SubmitChanges();
	targetTemplateView.Reset();
	originalTemplateView.Reset();
}

Bool GenerateStaticEntityCommandlet::EnsureResourceExist(const res::ResourcePath path, THandle<CResource> source )
{
	if (m_generatedResourcePaths.Exist(path) )
	{
		return true;
	}
	// lookup resource class for given extension
	const rtti::ClassType* resourceClass = res::prv::ResourceClassLookup::GetInstance().GetResourceClassForFile( path );
	if ( !resourceClass )
	{
		Log( red::String::Printf( "Core: Unable to create '%hs' because it's using unknown extension", path.ToString().AsChar() ).AsChar(), MessageType::Error );
		return false;
	}

	// find resource factory for given extension (we are not allowed to create resources without a factory)
	IFactory* factory = IFactory::FindFactory( resourceClass );
	if ( nullptr == factory )
	{
		Log( red::String::Printf( "Core: Unable to create '%hs' because there's no factory for resource '%hs'", path.ToString().AsChar(), resourceClass->GetName().AsChar() ).AsChar(), MessageType::Error);
		return false;
	}

	// use the resource factory to create a new resource
	THandle< FactoryOptions > factoryOptions = factory->CreateOptions( path );
	if ( !factory->DoCreate( source, factoryOptions ) )
	{
		Log( red::String::Printf( "Core: Unable to create '%hs' because factory for resource '%hs' failed", path.ToString().AsChar(), resourceClass->GetName().AsChar()).AsChar() , MessageType::Error);
		return false;
	}

	res::ResourceSavingOptions savingOptions;
	savingOptions.m_targetPath = path;
	if (res::SaveResource(source, savingOptions))
	{
		Log( red::String::Printf( "File created successfully in '%hs'", path.ToString().AsChar() ).AsChar(), MessageType::Log );
		m_generatedResourcePaths.Insert(path);
		return true;
	}
	return false;
}

Bool GenerateStaticEntityCommandlet::ContainsRemoveableComponents( THandle<ent::EntityTemplate> source )
{
	job::Builder builder{ job::Priority::Latent };

	ent::TemplateUnpackedViewPtr entityView = source->CreateUnpackedView( builder );
	job::FlushCounter( builder.ExtractWaitCounter() );

	for (const ent::ComponentHandle& comp : entityView->GetComponents())
	{
		if ( IsComponentCanBeRemoved( comp ) )
		{
			return true;
		}
	}

	for (ent::TemplateInclude include : source->GetIncludes())
	{
		res::ResourceTokenHandle sourceIncludeToken = include.GetTemplate().IssueLoadingRequest();
		sourceIncludeToken->WaitUntilLoaded();
		if ( ContainsRemoveableComponents( Cast<ent::EntityTemplate>(sourceIncludeToken->GetResource())) )
		{
			return true;
		}
	}

	return false;
}

Bool GenerateStaticEntityCommandlet::IsComponentCanBeRemoved( ent::ComponentHandle component)
{
	if( component->IsA<ent::AnimationControllerComponent>() || component->IsA<ent::AnimatedComponent>() )
	{
		return true;
	}
	const String className = component->GetClass()->GetName().AsStringView().ToString().ToLower();
	const String componentName = component->GetName().AsStringView().ToString().ToLower();

	if (m_componentsToRemove.Exist( className ))
	{
		return m_componentNamesToKeep.Exist( componentName ) == false;
	}
	return m_componentNamesToRemove.Exist( componentName );
}

void GenerateStaticEntityCommandlet::ParseInputs(const red::String& paramName, const red::CommandLine& options, red::HashSet<String>& results)
{
	if (options.HasOption( paramName ))
	{
		red::String inputFilePath;//path list of file
		if (options.GetFirstParam(paramName, inputFilePath))
		{
			const red::AbsolutePath path = red::AbsolutePath::ParseFilePath(inputFilePath);
			if (path.IsFilePath())
			{
				String contents;
				if (red::LoadFileToString(path, contents))
				{
					const red::DynArray<red::String> parsedContents = contents.Split(red::DynArray< String >{ ";", ",","\t","\n"});

					results.Reserve(parsedContents.Size());

					for (auto str : parsedContents)
					{
						results.Insert(str.ToLower());
					}
				}
			}
		}
	}
}

const res::ResourcePath GenerateStaticEntityCommandlet::GetStaticPath(const res::ResourcePath path)
{
	red::StringView pathStr = path.ToStringView();
	red::String extension = red::paths::ExtractExtension( pathStr );
	red::String filename = red::paths::ExtractFileStem( pathStr );
	red::String folderPath = red::paths::ExtractParentPath( pathStr );
	return res::ResourcePath::Build( red::String::Printf( "base\\static\\%s%s_static.%s", folderPath.AsChar(), filename.AsChar(), extension.AsChar() ) );
}

void GenerateStaticEntityCommandlet::Log(const String& message, const MessageType type)
{
	m_logMessages.PushBack({message, type});
}

