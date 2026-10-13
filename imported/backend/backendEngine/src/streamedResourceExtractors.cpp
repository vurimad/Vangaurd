/*
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "streamedResourceExtractors.h"

#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/world/include/worldPrefab.h"
#include "../../../common/world/include/worldPrefabNode.h"
#include "../../../common/world/include/worldNodeEditorData.h"
#include "../../../common/world/include/meshNode.h"
#include "../../../common/world/include/prefabUtils.h"
#include "../../../common/particles/include/particleSystem.h"
#include "../../../common/worldStatic/include/staticParticleNode.h"
#include "../../../common/worldEntities/include/entityTemplate.h"
#include "../../../common/redReflection/include/serializationLoader.h"
#include "../../../common/worldEntities/include/entityTemplateUnpackedView.h"
#include "../../../common/resourceMesh/include/mergedMesh.h"
#include "../../../common/resourceMesh/include/meshDataViewRO.h"
#include "../../../common/resourceMesh/include/meshUtils.h"
#include "../../../common/resourceMesh/include/meshParamOccluderData.h"
#include "../../../common/engine/include/physicsDecorationResource.h"
#include "../../../common/worldEntities/include/entityTemplateEditorController.h"
#include "../../../common/redReflection/include/streamedResourceManager.h"
#include "../../common/worldEntities/include/worldNodeEntity.h"
#include "../../common/worldEntities/include/appearanceResource.h"
#include "../../common/worldEntities/include/effect.h"
#include "../../common/worldEntities/include/trackItemParticles.h"
#include "../../common/worldEntities/include/worldNodeEffect.h"
#include "../../common/worldEntities/include/instancedDestructibleNode.h"
#include "../../common/world/include/worldNodeGroup.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( StreamingMeshDataExtractor, res )
	RTTI_PARENT_TYPE( res::IStreamedResourceDataExtractor )
RTTI_END_TYPE()

RTTI_BEGIN_TYPE_IN_NAMESPACE( StreamingPrefabDataExtractor, res )
	RTTI_PARENT_TYPE( res::IStreamedResourceDataExtractor )
RTTI_END_TYPE()

RTTI_BEGIN_TYPE_IN_NAMESPACE( StreamingParticleDataExtractor, res )
	RTTI_PARENT_TYPE( res::IStreamedResourceDataExtractor )
RTTI_END_TYPE()

RTTI_BEGIN_TYPE_IN_NAMESPACE( StreamingEffectDataExtractor, res )
	RTTI_PARENT_TYPE(res::IStreamedResourceDataExtractor)
RTTI_END_TYPE()

RTTI_BEGIN_TYPE_IN_NAMESPACE( StreamingEntityDataExtractor, res )
	RTTI_PARENT_TYPE( res::IStreamedResourceDataExtractor )
RTTI_END_TYPE()

RTTI_BEGIN_TYPE_IN_NAMESPACE( StreamingAppearanceDataExtractor, res )
	RTTI_PARENT_TYPE( res::IStreamedResourceDataExtractor )
RTTI_END_TYPE()

namespace res
{

	Bool StreamingMeshDataExtractor::SupportsResource( const res::ResourcePath& path ) const
	{
		auto extension = red::paths::GetExtension( path.ToStringView() );
		return extension == CMesh::GetFileExtension() || extension == CMesh::GetDeprecatedFileExtension() ||
			extension == MergedMesh::GetFileExtension() ||
			extension == CPhysicsDecorationResource::GetFileExtension() || extension == CPhysicsDecorationResource::GetDeprecatedFileExtension();
	}

	Bool StreamingMeshDataExtractor::SupportsResource( const res::StreamedResource& res ) const
	{
		return res.IsA< CMesh >();
	}

	void StreamingMeshDataExtractor::ExtractStreamingData( const res::ResourcePath& path, CName optionalName, const res::StreamedResource& res, StreamingData& outData ) const
	{
		const CMesh* const mesh = Cast< const CMesh >( &res );
		RED_FATAL_ASSERT( mesh, "Valid mesh resource expected" );

		outData.m_surfaceAreaPerAxis = mesh->GetSurfaceAreaPerAxis();
		outData.m_visibleBoundingBox = outData.m_boundingBox = mesh->GetBoundingBox();
        outData.m_autoHideDistance = 0.0f;
        outData.m_streamingDistance = 1.0f;
		outData.m_navmeshImpact = (Uint16)mesh->GetNavigationSetting().m_navmeshImpact;
		outData.m_hasEmbeddedOccluder = mesh->GetParameter< mesh::MeshParamOccluderData >() != nullptr;

		const tools::TerrainMeshResourceInfo terrainMeshInfo = tools::IsTerrainMeshResource( path );
		outData.m_hasTerrainNodes = terrainMeshInfo.isTerrainMeshResource;

		if( !outData.m_boundingBox.IsEmpty() )
		{
			if( terrainMeshInfo.isTerrainMeshResource )
			{
                const tools::TerrainDistances terrainDistances = tools::CalculateTerrainMeshDistances( outData.m_boundingBox, terrainMeshInfo );
                outData.m_boundingBox = terrainDistances.adjustedBounds;
                outData.m_autoHideDistance = terrainDistances.autoHide;
                outData.m_streamingDistance = terrainDistances.streaming;
			}
			else
			{
				outData.m_streamingDistance = outData.m_autoHideDistance = mesh->GetAutoHideDistance();
			}

			outData.m_isGIVisible = mesh->GetIsGIVisible();
		}

		RED_FATAL_ASSERT( !std::isinf( outData.m_autoHideDistance ) && !std::isnan( outData.m_autoHideDistance ) && (outData.m_autoHideDistance >= 0.0f) );
		outData.m_castShadows = mesh->GetCastGlobalShadows();
		outData.m_castLocalShadows = mesh->GetCastLocalShadows();
		outData.m_streamingRefPosition = outData.m_boundingBox.CalcCenter();

		// calculate total number of LOD0 triangles
		{
			mesh::DataViewRO view(*mesh);
			outData.m_triangleCount = mesh::utils::CountLODTriangles(view, 0);
		}
	}


	Bool StreamingPrefabDataExtractor::SupportsResource( const res::ResourcePath& path ) const
	{
		auto extension = red::paths::GetExtension( path.ToStringView() );
		return extension == "prefab" || extension == "world";
	}

	Bool StreamingPrefabDataExtractor::SupportsResource( const res::StreamedResource& res ) const
	{
		return res.IsA< world::Prefab >();
	}

	void StreamingPrefabDataExtractor::ExtractStreamingData( const res::ResourcePath& path, CName optionalName, const res::StreamedResource& res, StreamingData& outData ) const
	{
		const world::Prefab* const prefab = Cast< const world::Prefab >( &res );
		RED_FATAL_ASSERT( prefab, "Prefab resource expected" );

		// without this we can't read nodes' transforms
		// Note: this is thread safe, because the editor data is already aquired in Prefab::OnPostLoad, or we are inspecting
		// a resource loaded via ExtractSingleObject
		prefab->AcquireNodesEditorData();

		res::StreamingResourceManager* streamingManager = GetStreamingResourceManager();

		res::StreamingData data;
		data.m_boundingBox = Box::EMPTY();
		data.m_visibleBoundingBox = Box::EMPTY();
		data.m_streamingDistance = 1.0f;

		data.m_hasBuildingPrefabNodes = prefab->GetPrefabType() == world::PrefabType::Building;

		math::Box streamingBox = math::Box::EMPTY();

		// We can't use node iterator here, because it requires all groups to have parent pointer updated, which
		// is performed in OnPostLoad. This method will be called from streaming cache, which skips OnPostLoad call after resource loading.
		world::Prefab::TNodes allNodes;
		prefab->ExtractNodes( allNodes );

		red::DynArray< math::Sphere > streamingSpheres;
		streamingSpheres.Reserve( allNodes.Size() );

		if ( allNodes.Empty() )
		{
			// in case of an empty prefab, give it some default bounding box
			data.m_boundingBox.AddPoint( Vector3::ZEROS() );
		}

		for ( const THandle<world::Node>& node : allNodes )
		{
			if ( node->GetUsedResourcePath() == path )
			{
				// prefab recursion, skip the node
				continue;
			}

			data.m_boundingBox.AddBox( node->GetEditorData().GetBoundingBox() );
			data.m_visibleBoundingBox.AddBox( node->GetEditorData().GetDirectlyVisibleBoundingBox() );

			const world::NodeStreamingData nodeStreamingData = node->GetStreamingData();

			// validate node streaming data
			{
				const AnsiChar* prefabPath = path.ToDebugString();
				const AnsiChar* nodeName = node->GetName().AsChar();
				const AnsiChar* nodeTypeName = node->GetClass()->GetName().AsChar();

				RED_FATAL_ASSERT( nodeStreamingData.streamingReferencePoint.IsOk(),
					"Invalid streaming reference point, value=[%f,%f,%f], prefab=%hs, nodeName=%hs, nodeClass=%hs",
					nodeStreamingData.streamingReferencePoint.X, nodeStreamingData.streamingReferencePoint.Y, nodeStreamingData.streamingReferencePoint.Z,
					prefabPath, nodeName, nodeTypeName );

				RED_FATAL_ASSERT( std::isnormal( nodeStreamingData.streamingDistance ) && nodeStreamingData.streamingDistance < 1.0e+8f,
					"Invalid streaming distance, value=%f, prefab=%hs, nodeName=%hs, nodeClass=%hs",
					nodeStreamingData.streamingDistance, prefabPath, nodeName, nodeTypeName );
			}

			streamingSpheres.PushBack( math::Sphere( nodeStreamingData.streamingReferencePoint, nodeStreamingData.streamingDistance ) );

			if ( node->IsA< world::PrefabNode >() || node->IsA< world::MeshNode >() )
			{
				const res::ResourcePath& usedResource = node->GetUsedResourcePath();
				if ( usedResource.IsValid() )
				{
					const res::StreamingData childResourceData = streamingManager->GetStreamingData( usedResource );

					Uint32 multiplier = 1;
					if ( node->IsA< world::InstancedDestructibleMeshNode >() )
					{
						auto instancedDestructibleMeshNode = Cast< world::InstancedDestructibleMeshNode >( node );
						multiplier = instancedDestructibleMeshNode->GetInstanceTransforms().Size();
					}

					data.m_triangleCount += childResourceData.m_triangleCount * multiplier;
					data.m_hasTerrainNodes |= childResourceData.m_hasTerrainNodes;

					data.m_hasBuildingPrefabNodes |= childResourceData.m_hasBuildingPrefabNodes;
				}
			}
			// A bit of a hack, although I can't include this class' header here, because of reverse dependency, maybe extractors should be in backendWorld?
			else if ( node->GetClass()->GetName() == RED_NAME_CONSTEXPR_NOREG( "worldTerrainNode" ) || node->GetClass()->GetName() == RED_NAME_CONSTEXPR_NOREG( "worldTerrainMeshNode" ) )
			{
				data.m_hasTerrainNodes = true;
			}
		}

        // terrain is a special case. Make sure if nicely fit into quad-tree-like structure.
		if( prefab->IsTerrainPrefab() && prefab->GetProxyMesh().IsValid() )
        {
            const tools::TerrainMeshResourceInfo meshInfo = tools::IsTerrainMeshResource( prefab->GetProxyMesh().GetPath() );
            const tools::TerrainDistances terrainData = tools::CalculateTerrainMeshDistances( data.m_boundingBox, meshInfo );
            data.m_boundingBox = terrainData.adjustedBounds;
            streamingSpheres.PushBack( math::Sphere( data.m_boundingBox.CalcCenter(), terrainData.streaming ) );
        }

        // determine overall streaming distance and reference point offset based on all nodes spheres
        for( const math::Sphere& sphere : streamingSpheres )
        {
            streamingBox.AddBox( math::Box( sphere.GetCenter(), sphere.GetRadius() ) );
        }

		data.m_streamingRefPosition = streamingBox.CalcCenter();
		RED_FATAL_ASSERT( data.m_streamingRefPosition.IsOk() );

		for ( const math::Sphere& sphere : streamingSpheres )
		{
			data.m_streamingDistance = Max( data.m_streamingDistance, ( data.m_streamingRefPosition - sphere.GetCenter() ).Mag() + sphere.GetRadius() );
		}

		const Bool isRootGeneratedPrefab = red::paths::GetFileName( path.ToStringView() ) == "_generated.prefab";
		if ( isRootGeneratedPrefab )
		{
			// workaround: we don't want the bounding box of the generated prefab to propagate to world bounding box
			// because generators are relying on that to determine number of nodes to generate
			data.m_boundingBox = Box::FULL();
			data.m_visibleBoundingBox = Box::EMPTY();
		}

		// if the prefab has proxy, override the streaming reference point, because it's used as
		// secondary reference point for child nodes so it must be the same as the bounding box center
		if ( prefab->GetProxyMesh().IsValid() )
		{	
			const res::StreamingData meshData = streamingManager->GetStreamingData( prefab->GetProxyMesh().GetPath() );
			if (meshData.IsValid() )
			{
				data.m_streamingRefPosition = meshData.m_streamingRefPosition;

				MeshAutohideParams proxyMeshParams;
                proxyMeshParams.surfaceAreaPerAxis = meshData.m_surfaceAreaPerAxis;
                proxyMeshParams.meshScale = prefab->GetProxyMeshScale();

                proxyMeshParams.boundingBox = meshData.m_visibleBoundingBox;
                const float proxyAutoHide = CMesh::CalculateAutoHideDistance( proxyMeshParams );
                
				// Prefabs can't have a shorter streaming distance than their proxy, or else the proxy will be unstreamed too soon in the editor.
				// #tbd: performance once we have prefabs with area proxies: the instanced nodes won't stream in, but we'll have a lot more streaming proxies in the streaming grid
				data.m_streamingDistance = Max(data.m_streamingDistance, proxyAutoHide);
			}
		}

		const THandle< world::NodesGroup >& mainGroup =  prefab->GetRootGroup();
		if( mainGroup )
		{
			for( auto& subGroup : mainGroup->GetSubGroups() )
			{
				const TResAsyncRef< CMesh >& proxy = subGroup->GetProxyMesh();

				if( proxy.IsValid() )
				{
					const res::StreamingData meshData = streamingManager->GetStreamingData( proxy.GetPath() );
					if( meshData.IsValid() )
					{
						MeshAutohideParams proxyMeshParams;
						proxyMeshParams.boundingBox = meshData.m_visibleBoundingBox;
						proxyMeshParams.surfaceAreaPerAxis = meshData.m_surfaceAreaPerAxis;
						proxyMeshParams.meshScale = prefab->GetProxyMeshScale();
						const float proxyAutoHide = CMesh::CalculateAutoHideDistance( proxyMeshParams );

						data.m_streamingDistance = Max(data.m_streamingDistance, proxyAutoHide);
					}
				}
			}
		}
		
		outData = data;
	}

	void StreamingPrefabDataExtractor::ExtractDependencies( const res::ResourcePath& path, const res::StreamedResource& res, DependenciesList& outDependencies ) const
	{
		const world::Prefab* const prefab = Cast< const world::Prefab >( &res );
		RED_FATAL_ASSERT( prefab, "Prefab resource expected" );

		world::Prefab::TNodes allNodes;
		prefab->ExtractNodes( allNodes );

		for ( const world::NodePtr& node : allNodes )
		{
			if ( const auto prefabNode = Cast< world::PrefabNode >( node ) )
			{
				if ( prefabNode->GetPrefab().IsValid() )
					outDependencies.PushBack( prefabNode->GetPrefab().GetPath() );
			}
			else if( const auto meshNode = Cast< world::MeshNode >( node ) )
			{
				if ( meshNode->GetMesh().IsValid() )
					outDependencies.PushBack( meshNode->GetMesh().GetPath() );
			}
			else if ( const auto particleSystemNode = Cast< world::StaticParticleNode >( node ) )
			{
				if ( particleSystemNode->GetParticleSystem().IsValid() )
					outDependencies.PushBack( particleSystemNode->GetParticleSystem().GetPath() );
			}
			else if (const auto entityNode = Cast< world::EntityNode >(node))
			{
				if ( entityNode->GetEntityTemplate().IsValid() )
					outDependencies.PushBack( entityNode->GetEntityTemplate().GetPath());
			}
			else if (const auto effectNode = Cast<world::EffectNode>(node))
			{
				// #tbd: load and get particle system resource paths??
				if (effectNode->GetUsedResourcePath().IsValid())
					outDependencies.PushBack(effectNode->GetUsedResourcePath());
			}
			// TODO more resource types as needed
		}

		if ( prefab->GetProxyMesh().IsValid() )
		{
			outDependencies.PushBack( prefab->GetProxyMesh().GetPath() );
		}

		const THandle< world::NodesGroup >& mainGroup = prefab->GetRootGroup();
		if( mainGroup )
		{
			for( auto& subGroup : mainGroup->GetSubGroups() )
			{
				const TResAsyncRef< CMesh >& proxy = subGroup->GetProxyMesh();

				if( proxy.IsValid() )
				{
					outDependencies.PushBack( proxy.GetPath() );
				}
			}
		}
	}


	Bool StreamingParticleDataExtractor::SupportsResource( const res::ResourcePath& path ) const
	{
		auto extension = red::paths::GetExtension( path.ToStringView() );
		return extension == CParticleSystem::GetFileExtension() || extension == CParticleSystem::GetDeprecatedFileExtension();
	}

	Bool StreamingParticleDataExtractor::SupportsResource( const res::StreamedResource& res ) const
	{
		return res.IsA< CParticleSystem >();
	}

	void StreamingParticleDataExtractor::ExtractStreamingData( const res::ResourcePath& path, CName optionalName, const res::StreamedResource& res, StreamingData& outData ) const
	{
		const CParticleSystem* const particleSystem = Cast< const CParticleSystem >( &res );
		RED_FATAL_ASSERT( particleSystem, "Valid particle system resource expected" );

		outData.m_visibleBoundingBox = outData.m_boundingBox = particleSystem->GetBoundingBox();
		outData.m_autoHideDistance = particleSystem->GetAutoHideDistance();
		outData.m_streamingDistance = Max( 1.0f, outData.m_autoHideDistance ); // TODO streaming factor
	}

	Bool StreamingEffectDataExtractor::SupportsResource(const res::ResourcePath& path) const
	{
		auto extension = red::paths::GetExtension(path.ToStringView());
		return extension == world::Effect::GetFileExtension() || extension == world::Effect::GetDeprecatedFileExtension();
	}

	Bool StreamingEffectDataExtractor::SupportsResource(const res::StreamedResource& res) const
	{
		return res.IsA< world::Effect >();
	}

	void StreamingEffectDataExtractor::ExtractStreamingData(const res::ResourcePath& path, CName optionalName, const res::StreamedResource& res, StreamingData& outData) const
	{
		const Float minStreamingDistance = 20.f; // #tbd, slightly higher than the current 17.x autocalculated if no valid resource

		const world::Effect* const effect = Cast< const world::Effect >(&res);
		RED_FATAL_ASSERT(effect, "Valid effect resource expected");

		res::StreamingResourceManager* streamingManager = GetStreamingResourceManager();

		// #tbd: could have audio that needs to be heard from far away
		outData.m_visibleBoundingBox = Box::EMPTY();
		outData.m_boundingBox = Box::EMPTY();
		outData.m_streamingDistance = minStreamingDistance;

		// #tbd: best effort, since can't really know for sure where everything will be placed
		for (const auto& it : effect->GetEvents())
		{
			const auto& particles = Cast<effect::TrackItemParticles>(it);
			if (particles && particles->GetResourcePath().IsValid())
			{
				const StreamingData particleStreamingData = streamingManager->GetStreamingData(particles->GetResourcePath());

				outData.m_visibleBoundingBox = outData.m_boundingBox.AddBox(particleStreamingData.m_boundingBox);
				outData.m_autoHideDistance = Max(outData.m_autoHideDistance, particleStreamingData.m_autoHideDistance);

				const Float streamingDistanceFactor = 1.f;  // TODO streaming factor
				outData.m_streamingDistance = Max(outData.m_streamingDistance, streamingDistanceFactor * outData.m_autoHideDistance);
			}
		}
	}


	void StreamingEffectDataExtractor::ExtractDependencies(const res::ResourcePath& path, const res::StreamedResource& resource, DependenciesList& outDependencies) const
	{
		const world::Effect* const effect = Cast< const world::Effect >(&resource);
		RED_FATAL_ASSERT(effect, "Valid effect resource expected");

		// #tbd: best effort, since can't really know for sure where everything will be placed
		for (const auto& it : effect->GetEvents())
		{
			it->CollectUsedResourcePaths(outDependencies);
		}

		red::alg::RemoveDuplicates(outDependencies);
	}

	Bool StreamingEntityDataExtractor::SupportsResource(const res::ResourcePath& path) const
	{
		auto extension = red::paths::GetExtension( path.ToStringView() );
		return extension == "ent";
	}

	Bool StreamingEntityDataExtractor::SupportsResource( const res::StreamedResource& res ) const
	{
		return res.IsA< ent::EntityTemplate >();
	}

	void StreamingEntityDataExtractor::ExtractStreamingData( const res::ResourcePath& path, CName optionalName, const res::StreamedResource& res, StreamingData& outData ) const
	{
		const ent::EntityTemplate * entityTemplate = Cast< const ent::EntityTemplate >( &res );
		RED_FATAL_ASSERT( entityTemplate, "Entity Template resource expected" );

		auto controller = entityTemplate->Editor_AcquireEditorController();

		controller->ComputeStreamingData( outData, optionalName );
	}

	void StreamingEntityDataExtractor::ExtractDependencies( const res::ResourcePath& path, const res::StreamedResource& res, DependenciesList& outDependencies ) const
	{
		const ent::EntityTemplate * entityTemplate = Cast< const ent::EntityTemplate >( &res );
		RED_FATAL_ASSERT( entityTemplate, "Entity Template resource expected" );

		auto controller = entityTemplate->Editor_AcquireEditorController();
		controller->GetAllResourceDependencies( outDependencies );
	}


	//
	Bool StreamingAppearanceDataExtractor::SupportsResource( const res::ResourcePath& path ) const
	{
		auto extension = red::paths::GetExtension( path.ToStringView() );
		return extension == "app";
	}

	Bool StreamingAppearanceDataExtractor::SupportsResource( const res::StreamedResource& res ) const
	{
		return res.IsA< appearance::AppearanceResource >();
	}

	void StreamingAppearanceDataExtractor::ExtractStreamingData( const res::ResourcePath& path, CName optionalName, const res::StreamedResource& res, StreamingData& outData ) const
	{
		const appearance::AppearanceResource * resource = Cast< const appearance::AppearanceResource  >( &res );
		resource->ComputeStreamingData( outData, optionalName );
	}

	void StreamingAppearanceDataExtractor::ExtractDependencies( const res::ResourcePath& path, const res::StreamedResource& res, DependenciesList& outDependencies ) const
	{
		const appearance::AppearanceResource * resource = Cast< const appearance::AppearanceResource  >( &res );
		resource->GetAllResourceDependencies( outDependencies );
	}

} // res
