#include "build.h"
#include "generatePrefabsCommand.h"

#include "../../backendEngine/include/objectSerializablePersistentCollection.h"
#include "../../backendEngine/include/factory.h"

#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/world/include/worldPrefab.h"
#include "../../../common/worldStatic/include/staticMeshNode.h"
#include "../../../common/redReflection/include/cloningUtils.h"
#include "../../../common/redReflection/include/resourceDepot.h"
#include "../../../common/resource/include/resourceSaver.h"


RTTI_BEGIN_TYPE_IN_NAMESPACE( GeneratePrefabsCommand, tools );
	RTTI_PARENT_TYPE( tools::GeneralResourceCommand );
RTTI_END_TYPE();

namespace tools
{
	class GeneratePrefabsOptions : public ResourceCommandOptions
	{
		RTTI_DECLARE_TYPE( GeneratePrefabsOptions );

	public:

		GeneratePrefabsOptions();

		const THandle< world::MeshNode >& GetMeshNode() const { return m_meshNode; }
		const String& GetDestinationDirectory() const { return m_destinationDirectory; }
		void SetDestinationDirectory( const String& path ) { m_destinationDirectory = path; }
		Bool CreateLockedPrefab() const { return m_locked; }

		virtual Bool NeedOptions() const { return true; }

	private:
		String m_destinationDirectory;

		THandle< world::MeshNode > m_meshNode;

		// TODO expose more prefab properties (use custom properties)
		Bool m_locked;
	};

	//////////////////////////////////////////////////////////////////////////

	GeneratePrefabsOptions::GeneratePrefabsOptions()
	{
		m_meshNode = world::Node::CreateNode< world::StaticMeshNode >( world::NodeTransform(), RED_NAME_CONSTEXPR( "mesh" ) );
		m_locked = false;
	}

	//////////////////////////////////////////////////////////////////////////

	Bool GeneratePrefabsCommand::SupportsResource( const rtti::ClassType* resourceClass )
	{
		return resourceClass == ClassID< CMesh >();
	}

	CName GeneratePrefabsCommand::GetName() const
	{
		return RED_NAME_CONSTEXPR( "generatePrefabs" );
	}

	THandle< ResourceCommandOptions > GeneratePrefabsCommand::CreateOptions( const THandle< ResourceCommandOptions > suggestedOptions ) const
	{
		const THandle< GeneratePrefabsOptions > options = CreateHandle< GeneratePrefabsOptions >();
		options->SetDestinationDirectory( red::paths::ExtractParentPath( GetResourcePath().ToStringView() ) );
	
		// copy options from previous run
		if ( suggestedOptions && suggestedOptions->GetClass() == options->GetClass() )
		{
			suggestedOptions->GetClass()->Copy( options.Get(), suggestedOptions.Get() );
		}

		return options;
	}

	ResourceCommandResult GeneratePrefabsCommand::Execute( const THandle<ResourceCommandOptions>& options )
	{
		if ( !options )
		{
			return ResourceCommandResult::Error( "Core: Options are not passed" );
		}

		THandle< GeneratePrefabsOptions > generationOptions = Cast< GeneratePrefabsOptions >( options );
		if ( !generationOptions )
		{
			return ResourceCommandResult::Error( "Core: Invalid options type: %s ", options->GetFriendlyName().AsChar() );
		}

		const String& directoryPath = generationOptions->GetDestinationDirectory();
		const Bool outputIsDirectory = ( directoryPath.Back() == '/' || directoryPath.Back() == '\\' );
		if ( !outputIsDirectory )
		{
			return ResourceCommandResult::Error( "Core: Output directory path must end with '\\'" );
		}

		String filename = red::paths::ExtractFileName( GetResourcePath().ToStringView() );
		red::paths::ReplaceExtension( filename, "prefab" );

		String templatePath = String::Printf( "%hs%hs", generationOptions->GetDestinationDirectory().AsChar(), filename.AsChar() );
		const res::ResourcePath destinationPath = res::ResourcePath::Build( templatePath );

		if ( GetResourceDepot() && GetResourceDepot()->ResourceExists( destinationPath ) )
		{
			return ResourceCommandResult::Error( "Core: Output resource '%hs' already exists", destinationPath.ToDebugString() );
		}

		return GeneratePrefab( destinationPath, *generationOptions );
	}

	ResourceCommandResult GeneratePrefabsCommand::GeneratePrefab( const res::ResourcePath& destinationPath, const GeneratePrefabsOptions& options ) const
	{
		using namespace world;

		THandle< Prefab > prefab = CreateHandle< Prefab >();
		if ( !prefab )
		{
			return ResourceCommandResult::Error( "Failed to create prefab object" );
		}
		prefab->SetLockState( options.CreateLockedPrefab() );

		// clone the mesh node so we can reuse the original object in batch generation
		THandle< MeshNode > meshNode = Cast< MeshNode >( tools::CloneObject( options.GetMeshNode() ) );
		if ( !meshNode )
		{
			return ResourceCommandResult::Error( "Failed to create mesh node object" );
		}

		Prefab::IncomingNode incomingNode;
		incomingNode.m_node = meshNode;
		prefab->PlaceNodes( red::ArraySpan< Prefab::IncomingNode >( &incomingNode, 1 ) );

		meshNode->SetMesh( GetResourcePath() );

		res::ResourceSavingOptions savingOptions;
		savingOptions.m_targetPath = destinationPath;
		if ( !res::SaveResource( prefab, savingOptions ) )
		{
			return ResourceCommandResult::Error( String::Printf( "Core: Unable to generate '%hs' because saving failed", destinationPath.ToDebugString() ) );
		}

		prefab.Reset();
		return ResourceCommandResult::Success( GResourceLoader->IssueLoadingRequest( destinationPath )->WaitUntilLoaded() );
	}
}

//////////////////////////////////////////////////////////////////////////

RTTI_BEGIN_TYPE_IN_NAMESPACE( GeneratePrefabsOptions, tools );
	RTTI_PARENT_TYPE( ResourceCommandOptions );

	RTTI_PROPERTY( m_destinationDirectory ).editable().tooltip( "Directory, where generated prefabs will be placed" );
	RTTI_PROPERTY( m_meshNode ).inlined().clearable( false ).tooltip( "Properties applied to the mesh node inside the generated prefab" );

	RTTI_PROPERTY_CATEGORY( "Prefab resource properties" );
	RTTI_PROPERTY( m_locked ).editable().tooltip( "Create locked prefab" );
RTTI_END_TYPE();
