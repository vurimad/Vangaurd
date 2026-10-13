#include "build.h"
#include "createResourceCommand.h"

#include "../../backendEngine/include/factory.h"
#include "../../../common/redReflection/include/resourceClassLookup.h"
#include "../../../common/resource/include/resourceSaver.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( CreateResourceCommand, tools );
RTTI_PARENT_TYPE( tools::GeneralResourceCommand );
RTTI_END_TYPE();

namespace tools
{
	class CreateResourceOptions : public ResourceCommandOptions
	{
		RTTI_DECLARE_TYPE( CreateResourceOptions );

	public:
		CreateResourceOptions() {}

		const String& GetDestinationPath() const { return m_destinationPath; }
		void SetDestinationPath( const String& path ) { m_destinationPath = path; }

		const THandle< FactoryOptions >& GetFactoryOptions() const { return m_factoryOptions; }
		void SetFactoryOptions( const THandle< FactoryOptions >& options ) { m_factoryOptions = options; }

		virtual Bool NeedOptions() const { return m_factoryOptions && m_factoryOptions->NeedOptions(); }

	private:
		String m_destinationPath;
		THandle< FactoryOptions > m_factoryOptions;
	};

	//-------------------------------------------------------------------------------------------------------------------------

	Bool CreateResourceCommand::SupportsResource( const rtti::ClassType* resourceClass )
	{
		if ( IFactory::FindFactory( resourceClass ) )
		{
			return true;
		}
		return false;
	}

	CName CreateResourceCommand::GetName() const
	{
		return RED_NAME_CONSTEXPR( "create" );
	}

	THandle< ResourceCommandOptions > CreateResourceCommand::CreateOptions( const THandle< ResourceCommandOptions > suggestedOptions ) const
	{
		THandle< CreateResourceOptions > options = CreateHandle< CreateResourceOptions >();;
		options->SetFactoryOptions( GetFactory()->CreateOptions( GetResourcePath() ) );

		if ( suggestedOptions && suggestedOptions->GetClass() == ClassID<CreateResourceOptions>()
			&& Cast<CreateResourceOptions>( suggestedOptions )->GetFactoryOptions()->GetClass() == options->GetFactoryOptions()->GetClass() )
		{
			suggestedOptions->GetClass()->Copy( options.Get(), suggestedOptions.Get() );
		}

		if( options->GetFactoryOptions() )
		{
			options->GetFactoryOptions()->m_path = GetResourcePath().ToString();
		}

		options->SetDestinationPath(  GetResourcePath().ToString() );
		return options;
	}

	IFactory* CreateResourceCommand::GetFactory() const
	{
		const rtti::ClassType* resourceClass = res::prv::ResourceClassLookup::GetInstance().GetResourceClassForFile( GetResourcePath() );
		if ( !resourceClass )
		{
			return nullptr;
		}

		// find resource factory for given extension (we are not allowed to create resources without a factory)
		return IFactory::FindFactory( resourceClass );
	}

	ResourceCommandResult CreateResourceCommand::Execute( const THandle<ResourceCommandOptions>& options )
	{
		// find resource factory for given extension (we are not allowed to create resources without a factory)
		auto* factory = GetFactory();
		if ( nullptr == factory )
		{
			return ResourceCommandResult( String::Printf( "Core: Unable to create '%hs' because there's no factory for resource '%hs'",
				GetResourcePath().ToString().AsChar(), red::utils::ExtractExtension( GetResourcePath().ToStringView() ).AsChar() ) );
		}

		// create an empty resource
		const rtti::ClassType* resourceClass = res::prv::ResourceClassLookup::GetInstance().GetResourceClassForFile( GetResourcePath() );
		THandle< CResource > resource = resourceClass->CreateHandle< CResource >();
		if ( !resource )
		{
			return ResourceCommandResult( String::Printf( "Core: Unable to create '%hs' because resource '%hs' could not be created",
				GetResourcePath().ToString().AsChar(), resourceClass->GetName().AsChar() ) );
		}

		// use the resource factory to create a new resource
		if ( !factory->DoCreate( resource, Cast<CreateResourceOptions >( options )->GetFactoryOptions() ) )
		{
			return ResourceCommandResult( String::Printf( "Core: Unable to create '%hs' because factory for resource '%hs' failed",
				GetResourcePath().ToString().AsChar(), resourceClass->GetName().AsChar() ) );
		}

		// save
		res::ResourceSavingOptions savingOptions;
		savingOptions.m_targetPath = GetResourcePath();
		if ( !res::SaveResource( resource, savingOptions ) )
		{
			return ResourceCommandResult::Error( "Core: Unable to create '%hs' because saving failed", GetResourcePath().ToDebugString() );
		}

		return ResourceCommandResult::Success( resource );
	}
}

RTTI_BEGIN_TYPE_IN_NAMESPACE( CreateResourceOptions, tools );
	RTTI_PARENT_TYPE( ResourceCommandOptions );
	RTTI_PROPERTY( m_destinationPath ).readOnly();
	RTTI_PROPERTY( m_factoryOptions ).inlined().clearable( false ).selectable( false ).resetable( false );
RTTI_END_TYPE();