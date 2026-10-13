#include "build.h"

#include "../../backendEngine/include/commandletEngineInstance.h"

#include "../../../common/core/include/commandlet.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/redReflection/include/serializationLoader.h"
#include "../../../common/redReflection/include/serializationSaver.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../common/world/include/worldPrefab.h"
#include "../../../common/resource/include/resourceEditorDepotImpl.h"
#include "../../backendEngine/include/commandletUtils.h"
#include "../../backendWorld/include/prefabEditor.h"
#include "../../../common/world/include/worldPrefabNode.h"
#include "../../../common/resource/include/resourceSaver.h"
#include "../../../common/worldStatic/include/staticMeshNode.h"
#include "../../../common/world/include/worldNodeEditorData.h"

using namespace world;

#pragma optimize("",off)
class ProxyDependencyCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE(ProxyDependencyCommandlet);

public:

	ProxyDependencyCommandlet()
		: ICommandlet(RED_NAME_CONSTEXPR("proxyDependencyFlag"))
		, m_prefabPaths{ red::PoolBackend() }
		, m_editorDepot{ commandlet::GetEditorDepot() }
	{ }

	~ProxyDependencyCommandlet() = default;

	// interface
	virtual Bool InitializeEnvironment() override;
	virtual void TeardownEnvironment() override;

	virtual Bool Execute(const red::CommandLine& options);
	virtual const char* GetOneLiner() const override;
	virtual void PrintHelp() const override;

private:
	red::DynArray< THandle< world::Prefab > > ProxyDependencyCommandlet::LoadPrefabs() const; 
	red::DynArray< res::ResourcePath > m_prefabPaths;
	commandlet::EngineInstance m_engineInstance;
	res::ResourceEditorDepot* m_editorDepot;

	Bool ChangeDependencyFlag( THandle< world::Prefab > prefab, Int8 action, float thresholdP, float thresholdM, bool recursive = false );

};


RTTI_BEGIN_TYPE(ProxyDependencyCommandlet);
	RTTI_PARENT_TYPE(ICommandlet);
RTTI_END_TYPE();


Bool ProxyDependencyCommandlet::InitializeEnvironment()
{
	return m_engineInstance.Initialise();
}

void ProxyDependencyCommandlet::TeardownEnvironment()
{
	m_engineInstance.Shutdown();
}

red::DynArray< THandle< world::Prefab > > ProxyDependencyCommandlet::LoadPrefabs() const
{
	red::DynArray< THandle< world::Prefab > > loadedPrefabs{ red::PoolBackend() };
	for (const auto& prefabPath : m_prefabPaths)
	{
		const auto prefab = GResourceLoader->IssueLoadingRequest( prefabPath )->WaitUntilLoaded< world::Prefab >();
		if (!prefab)
		{
			RED_LOG_WARNING( "Cannot load following prefab: %s", prefabPath.ToDebugString() );
			continue;
		}

		loadedPrefabs.PushBack( prefab );
	}

	return loadedPrefabs;
}

Bool ProxyDependencyCommandlet::ChangeDependencyFlag( THandle< world::Prefab > prefab, Int8 action, float thresholdP, float thresholdM, bool recursive)
{
	Uint32 changedCnt = 0;
	Uint32 processedCnt = 0;
	const auto prefabFileName{ red::paths::ExtractFileName( prefab->GetFriendlyName().TrimRight( '"' ) ) };

	auto prefabDiag = prefab->GetStreamingData().m_boundingBox.CalcSize().Mag3();

	for (const auto& node : prefab->GetRootGroup()->GetAllNodes())
	{
		bool modify = false;
		auto nodeTransform = node->GetTransform();
		if (const auto& meshNode = Cast<world::StaticMeshNode>( node ))
		{
			auto diag = nodeTransform.TransformBox( meshNode->GetLocalBoundingBox() ).CalcSize().Mag3();
			if (diag < thresholdM || diag < 0.01f*thresholdP*prefabDiag)
			{
				modify = true;
			}
			processedCnt++;
		}

		if (modify)
		{
			changedCnt++;
			auto& data = node->GetEditorData();
			data.SetProxyMeshDependencyMode( action > 0 ? ProxyMeshDependencyMode::Discard : ProxyMeshDependencyMode::Auto );
		}
	}

	if (changedCnt != 0)
	{
		RED_LOG( "Saving %s... with %d changed nodes (of totall %d)", prefabFileName.AsChar(), changedCnt, processedCnt );

		if (!res::SaveResource( prefab ))
		{
			RED_LOG_ERROR( "Could not save the output prefab." );
		}
	}
	else
	{
		RED_LOG_WARNING( "Nothing to set in %s ...", prefabFileName.AsChar() );
	}


	//if (recursive) for (const auto& node : prefab->GetRootGroup()->GetNodes())
	//{
	//	if (const auto& prefabNode = Cast<world::PrefabNode>( node ))
	//	{
	//		const auto prefabPath = prefabNode->GetPrefab().GetPath();
	//		const auto prefab = GResourceLoader->IssueLoadingRequest( prefabPath )->WaitUntilLoaded< world::Prefab >();
	//		if (!prefab)
	//		{
	//			RED_LOG_WARNING( "Cannot load following prefab: %s", prefabPath.ToDebugString() );
	//			return false;
	//		}
	//		ChangeDependencyFlag( prefab, action, thresholdP, thresholdM, recursive );
	//	}
	//}

	return true;
}

Bool ProxyDependencyCommandlet::Execute(const red::CommandLine& options)
{
	Int8  action = -1;
	Float thresholdP = 0;
	Float thresholdM = 0;
	Bool actionGiven = false;
	Bool recursive = false;

	if (options.HasOption(("thresholdPercent")))
	{
		options.GetFirstParam("thresholdPercent", thresholdP );
	}
	if (options.HasOption( ( "thresholdMeters" ) ))
	{
		options.GetFirstParam( "thresholdMeters", thresholdM );
	}

	if (options.HasOption(("set")))
	{
		action = 1;
		actionGiven = true;
		
	}
	else if (options.HasOption(("clear" )))
	{
		action = 0;
		actionGiven = true;
	}
	
	if ( !actionGiven || ( thresholdM == 0 && thresholdP == 0 ))
	{
		RED_LOG_ERROR( "Missing arguments!");
		PrintHelp();
		return false;
	}
	
	if (options.HasOption( ("file") ) && options.HasOption( ("csv") ))
	{
		RED_LOG_ERROR( "Wrong arguments! Only one input method csv of file" );
		return false;
	}

	if (options.HasOption( "file" ))
	{
		m_prefabPaths = commandlet::ParseResourcePathsFromCommandLine( *m_editorDepot, options, { "*.prefab" } );
		if (m_prefabPaths.Empty())
		{
			RED_LOG_ERROR( "Failed to fetch prefab resources! No valid prefab resource was passed." );
			return false;
		}
	}
	else if (options.HasOption( "csv" ))
	{
		String path;
		if (!options.GetFirstParam( "csv", path ))
		{
			return false;
		}
		red::AbsolutePath prafabListFilePath = red::AbsolutePath::ParseFilePath( path );
		if (!prafabListFilePath.IsFilePath() || !red::paths::ExtractExtension( prafabListFilePath ).EqualsNC( "csv" ))
		{
			return false;
		}
		String pathsStr{ red::PoolBackend() };
		if (red::LoadFileToString( prafabListFilePath, pathsStr ))
		{
			red::DynArray< String > paths = pathsStr.Split( "\n" );
			for (auto& path : paths)
			{
				m_prefabPaths.PushBack( res::ResourcePath::Build( path.TrimRight()) );
			}
		}
	}
	

	const auto loadedPrefabs{ LoadPrefabs() };
	if (loadedPrefabs.Empty())
	{
		RED_LOG_ERROR( "Commandlet failed! No valid prefabs loaded." );
		return false;
	}

	for (auto& prefab : loadedPrefabs)
	{
		ChangeDependencyFlag( prefab, action, thresholdP, thresholdM );
	}


	return true;
}


const char* ProxyDependencyCommandlet::GetOneLiner() const
{
	return "Simple setting of proxy dependency flag.";
}


void ProxyDependencyCommandlet::PrintHelp() const
{
	RED_LOG("Usage:");
	RED_LOG(" proxyDependencyFlag -set|clear -thresholdPercent=<float> -thresholdMeters=<float>");
	RED_LOG( "  -file=<path(s) to prefab(s) in the depot>|-csv=<csv file with deopot prefab paths>\n" );
	RED_LOG(" Will set|clear the flag on meshes with diagonal lower then meters specified and smaller relative to prefab diagonal percentage");
	RED_LOG(" You can specify both (will set if at least one is matched) or just one of thresholds." );
}



#pragma optimize("",on)