/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "../../backendResources/include/resaveCommandlet.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/worldEntities/include/entityTemplate.h"
#include "../../../common/worldEntities/include/entityComponent.h"
#include "../../../common/worldEntities/include/entityTemplateEditorController.h"
#include "../../../common/redCore/include/commandline.h"


class BACKEND_RESOURCES_API FindDuplicatedComponentsCommandlet : public ResaveCommandlet
{
	RTTI_DECLARE_TYPE(FindDuplicatedComponentsCommandlet);
	RED_BASE_CLASS(ResaveCommandlet);

public:
	FindDuplicatedComponentsCommandlet();
	virtual ~FindDuplicatedComponentsCommandlet() = default;

	virtual const char* GetOneLiner() const override final;
	virtual Bool Execute(const red::CommandLine& options) final override;
	virtual void PrintHelp() const final override;

private:
	Bool ProcessResource(const res::ResourcePath& path);
	Bool ParseOptions(const red::CommandLine& options);

private:
	CName m_componentName = CName::NONE();
};

RTTI_BEGIN_TYPE(FindDuplicatedComponentsCommandlet);
RTTI_PARENT_TYPE(ResaveCommandlet);
RTTI_END_TYPE();

//////////////////////////////////////////////////////////////////////////

FindDuplicatedComponentsCommandlet::FindDuplicatedComponentsCommandlet()
: ResaveCommandlet( RED_NAME_CONSTEXPR( "find-duplicated-components" ) )
{
}

const char* FindDuplicatedComponentsCommandlet::GetOneLiner() const
{
	return "find-duplicated-components";
}

Bool FindDuplicatedComponentsCommandlet::Execute( const red::CommandLine & options )
{
	if( !ParseOptions( options ) )
	{
		return false;
	}

	red::SaveStringToFile( m_outputListingFile, "" );

	for( const auto& path : m_resources )
	{
		if( path.ToString().EndsWith( ".ent" ) )
		{
			if( !ProcessResource(path) )
			{
				ReportFailure( path );
			}
		}
	}

	return true;
}

Bool FindDuplicatedComponentsCommandlet::ProcessResource(const res::ResourcePath & path)
{
	THandle< CResource > resource;
	Uint32 componentCounter = 0;	

	if( !LoadResource_ResourceLoaderMethod( path, resource ) )
	{
		return false;
	}
	else
	{
		if( resource->IsA< ent::EntityTemplate >() )
		{
			THandle< ent::EntityTemplate > entityResourceHandle = Cast< ent::EntityTemplate >( resource );

			const auto components = entityResourceHandle->Editor_AcquireEditorController()->GetComponents();
			red::HashMap< CName, Uint32 > componentNamesOccurances{ red::PoolBackend() };
			componentNamesOccurances.Reserve(  components.Size() );
			
			for( const auto& component : components )
			{
				componentNamesOccurances.Emplace( component.Get()->GetName(), 0u );
			}

			for( const auto& component : components )
			{
				const auto componentName = component.Get()->GetName();

				if( componentName == m_componentName )
				{
					++componentCounter;
				}

				if( m_componentName == CName::NONE() )
				{
					const auto it = componentNamesOccurances.Find( componentName );
					if( it != componentNamesOccurances.End() )
					{
						++it.Value();
					}
				}
			}

			if( componentCounter > 1 )
			{
				red::AppendStringToFile( m_outputListingFile, String::Printf( "%s is duplicated %d times in %s\n", m_componentName.AsChar(), componentCounter, path.ToDebugString() ) );
			}

			for( const auto& componentNameOccurance : componentNamesOccurances )
			{
				if( componentNameOccurance.Value() > 1 )
				{
					red::AppendStringToFile( m_outputListingFile, String::Printf( "%s is duplicated %d times in %s\n", componentNameOccurance.Key().AsChar(), componentNameOccurance.Value(), path.ToDebugString() ) );
				}
			}
		}		
	}
	return true; 
}

Bool FindDuplicatedComponentsCommandlet::ParseOptions(const red::CommandLine & options)
{
	if (options.HasOption("absoluteFile"))
	{
		if (!ParseAbsolutePaths(options))
		{
			return false;
		}
	}
	else
	{
		if (!ParseResourcePaths(options))
		{
			return false;
		}
	}

	String str;
	if (options.GetFirstOption("output", str))
	{
		m_outputListingFile = red::AbsolutePath::ParseFilePath(str);
	}

	if (options.GetFirstOption("component", str))
	{
		m_componentName = RED_NAME(str);
	}

	return true;
}

void FindDuplicatedComponentsCommandlet::PrintHelp() const
{
/*
	Parameters:

	-file=<resource path> (<resource path> ...)
		Specify individual files

	-dir=<resource dir path> (<resource dir path> ...)
		Specify directories

	-all
		All resources in depo

	-output=<output_file_path>

	-component=<component_name>
		name of the component that we want to find duplicates of

	*/
}