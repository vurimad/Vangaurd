/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "meshDataView.h"
#if !defined( VANGUARD_ASSET_HOST )
#include "meshCookingArtifact.h"
#endif

#include "../../backendData/include/cooker.h"
#include "../../backendData/include/dataTypesImpl.h"
#include "../../backendData/include/resourceDependency.h"
#include "../../backendData/include/resourceDatabase.h"
#include "../../backendData/include/dependencyUtils.h"
#include "../../backendData/include/dataGlobalCacheReference.h"

#include "../../backendEngine/include/resourceMetadata.h"

#include "../../../common/resourceMesh/include/mesh.h"
#if defined( VANGUARD_ASSET_HOST )
#include "../../../common/redReflection/include/serializationFileTables.h"
#endif
#if !defined( VANGUARD_ASSET_HOST )
#include "../../../common/resourceMesh/include/meshParamCompiledPhysics.h"
#include "../../../common/resourceMesh/include/meshParamImportedSnapPoint.h"
#include "../../../common/resourceMesh/include/meshParamShadowMeshCreationData.h"
#include "../../../common/resourceMesh/include/meshParamDestructionStepsData.h"
#include "../../../common/resourceMesh/include/meshParamOccluderData.h"
#include "../../../common/resourceMesh/include/meshParamBendedRoad.h"
#include "../../../common/resourceMesh/include/meshParamPhysics.h"
#endif
#include "../../../common/resourceMaterial/include/material.h"
#include "../../../common/resourceMaterial/include/texture.h"
#include "../../../common/resourceMaterial/include/materialinstance.h"
#include "../../../common/redReflection/include/cloningUtils.h"

#if !defined( VANGUARD_ASSET_HOST )
#include "../../../common/physics/include/physXNatives.h"
#include "../../../common/physics/include/physicsSystemResource.h"
#include "../../../common/physics/include/physicsSystemBody.h"
#include "../../../common/physics/include/physicsCollider.h"
#endif

#include "../../../common/redJobs2/include/jobBuilder.h"

#if !defined( VANGUARD_ASSET_HOST )
#include "../../../common/worldEntities/include/garmentUtils.h"
#include "../../../common/worldEntities/include/garmentMeshParameter.h"
#include "../../../common/resourceMesh/include/morphTargetMesh.h"
#include "../../../common/resourceMaterial/include/multilayerMask.h"
#endif

namespace mesh
{
#if !defined( VANGUARD_ASSET_HOST )
	red::ArraySpan<res::ResourcePath> GetMeshArtifactPaths()
	{
		static const res::ResourcePath nightCityWorldPath = RED_CONST_RESOURCEPATH( "base\\worlds\\03_night_city\\sectors\\_generated\\ma.mesh_artifact" );
		static const res::ResourcePath mainMenuWorldPath = RED_CONST_RESOURCEPATH( "base\\worlds\\04_main_menu\\sectors\\_generated\\ma.mesh_artifact" );
		static res::ResourcePath paths[ 2 ] = { nightCityWorldPath, mainMenuWorldPath };
		return paths;
	}

	class MeshCookingCache
	{
	public:
		MeshCookingCache();
		~MeshCookingCache();

		job::Counter& LoadArtifacts( data::Context& system );
		void Shutdown();
		Bool Exist( const res::ResourcePath& path );

		static MeshCookingCache& GetInstance();

		red::SpinLock m_guard;
		red::Set< Uint64 > m_staticMeshes;
	private:
		Bool m_isDataInitialized = false;
		job::Counter m_initCounter;
		struct InitContext
		{
			RED_USE_MEMORY_POOL( red::PoolBackend );

			red::DynArray< res::ResourceTokenHandle > dependencyTokens{ red::PoolBackend() };

			red::Set< Uint64 > m_dynamicMeshes;
		};
	};

	class MeshCookingCacheReference : public data::GlobalCacheReference
	{
		RTTI_DECLARE_TYPE( MeshCookingCacheReference );
	public:
		MeshCookingCacheReference();
		~MeshCookingCacheReference();

		virtual void Initialise( job::Counter& counter ) override;
		virtual void Shutdown( job::Counter& counter ) override;

		virtual Uint32 GetLifetime() const override;
	};

	MeshCookingCache::MeshCookingCache()
	{}

	MeshCookingCache::~MeshCookingCache()
	{}

	job::Counter& MeshCookingCache::LoadArtifacts( data::Context& system )
	{
		RED_SCOPE_LOCK( m_guard );
		if( m_isDataInitialized )
			return m_initCounter;

		red::SharedPtr< InitContext > data = red::CreateSharedPtr< InitContext >();

		job::Builder builder;

		auto pathSpan = GetMeshArtifactPaths();
		for( const auto& path : pathSpan )
		{
			res::IssueLoadingRequestParameter param;
			param.path = path;
			auto token = system.SourceLoader().IssueLoadingRequest( param );;
			data->dependencyTokens.PushBack( token );
			builder.DispatchWait( token->GetWaitCounter() );
		}

		builder.DispatchJob( "OnArtifactsLoaded", [ data, this ]( const job::RunContext& runContext )
		{
			RED_SCOPE_LOCK( m_guard );
			for( const auto& token : data->dependencyTokens )
			{
				if( token->IsLoaded() )
				{
					THandle< mesh::MeshCookingArtifact> artifact = Cast< mesh::MeshCookingArtifact >( token->GetResource() );

					for( auto& resPath : artifact->m_staticMeshes )
					{
						m_staticMeshes.Insert( resPath );
					}
					for( auto& resPath : artifact->m_dynamicMeshes )
					{
						data->m_dynamicMeshes.Insert( resPath );
					}
				}
			}

			m_staticMeshes.Difference( data->m_dynamicMeshes );
		} );
		m_isDataInitialized = true;
		m_initCounter += builder.ExtractWaitCounter();
		return m_initCounter;
	}

	void MeshCookingCache::Shutdown()
	{
		RED_SCOPE_LOCK( m_guard );
		m_staticMeshes.Clear();
	}

	Bool MeshCookingCache::Exist( const res::ResourcePath& path )
	{
		return m_staticMeshes.Exist( path.GetHash() );
	}

	MeshCookingCache& MeshCookingCache::GetInstance()
	{
		static MeshCookingCache s_instance;
		return s_instance;
	}

	MeshCookingCacheReference::MeshCookingCacheReference()
	{}

	MeshCookingCacheReference::~MeshCookingCacheReference()
	{}

	void MeshCookingCacheReference::Initialise( job::Counter& counter )
	{
	}

	void MeshCookingCacheReference::Shutdown( job::Counter& counter )
	{
		MeshCookingCache::GetInstance().Shutdown();
	}

	Uint32 MeshCookingCacheReference::GetLifetime() const
	{
		return data::GCL_Cooking;
	}
#endif

	class MeshCooker : public ICooker
	{
	public:
		RTTI_DECLARE_TYPE( MeshCooker );

		virtual ~MeshCooker() override = default;

		virtual bool CanCook( const res::ResourcePath& resourcePath ) const override;
		virtual bool CollectDependencies( const res::ResourcePath& path, job::Builder& builder, dependency::Collector& collector, data::Context& system ) const override;
		virtual bool Cook( const res::ResourcePath& path, job::Builder& builder, data::Context& system ) const override;
		virtual Uint32 GetVersion() const override;

	private:
		static bool RemoveUncookedParameters( THandle< CMesh >& mesh );
		static bool ConvertProxyMeshStreams( THandle< CMesh >& mesh, res::ResourceTokenHandle prxMat );
		static bool NeedsProxyMeshStramsConvert( THandle< CMesh >& mesh );
	};

	static red::DynArray< const char* > GetMeshLikeFileExtensions()
	{
		red::DynArray< const rtti::ClassType* > meshClasses;
		GetRttiSystem().EnumClasses( ClassID< CMesh >(), meshClasses );

		red::DynArray< const char* > result;
		result.Reserve( meshClasses.Size() * 2 ); // extension and deprecated extensions

		for( const rtti::ClassType* meshClass : meshClasses )
		{
			CMesh* defaultMesh = meshClass->GetDefaultObject< CMesh >();
			if( defaultMesh )
			{
				const char* ext = defaultMesh->GetExtension();
				if( ext )
				{
					result.PushBack( ext );
				}
				const char* depExt = defaultMesh->GetDeprecatedExtension();
				if( depExt && red::Strlen( depExt ) > 0 && red::Strcmp( ext, depExt ) != 0 )
				{
					result.PushBack( depExt );
				}
			}
		}

		return result;
	}

	bool ShouldMergeMeshDependencies( const res::ResourcePath& path, data::TargetPlatform platform )
	{
		if( platform == data::TargetPlatform::WINDOWS_SERVER || platform == data::TargetPlatform::LINUX_SERVER )
		{
			return false;
		}

		return true;
	}

#if defined( VANGUARD_ASSET_HOST )
	static bool ValidateAssetHostMeshExportTypes( const res::ResourcePath& path, data::Context& system )
	{
		auto file = system.SourceDepot().CreateResourceReader( path );
		if( !file )
		{
			system.ErrorHandler().ReportError(
				"MeshCooker: failed to inspect source mesh before cooking: '%hs'",
				path.ToDebugString() );
			return false;
		}

		Uint32 version = 0;
		serialization::FileTables tables;
		if( !tables.Load( *file, version ) )
		{
			system.ErrorHandler().ReportError(
				"MeshCooker: failed to read source mesh serialization tables: '%hs'",
				path.ToDebugString() );
			return false;
		}

		for( Uint32 exportIndex = 0; exportIndex < tables.m_exports.Size(); ++exportIndex )
		{
			const Uint16 classNameIndex = tables.m_exports[ exportIndex ].m_className;
			if( classNameIndex >= tables.m_names.Size() )
			{
				system.ErrorHandler().ReportError(
					"MeshCooker: invalid export class index %u in '%hs'",
					classNameIndex,
					path.ToDebugString() );
				return false;
			}

			const Uint32 classNameStringOffset = tables.m_names[ classNameIndex ].m_string;
			if( classNameStringOffset >= tables.m_strings.Size() )
			{
				system.ErrorHandler().ReportError(
					"MeshCooker: invalid export class string offset %u in '%hs'",
					classNameStringOffset,
					path.ToDebugString() );
				return false;
			}

			const char* className = &tables.m_strings[ classNameStringOffset ];
			if( !GetRttiSystem().FindClass( RED_NAME_NOREG( className ) ) )
			{
				system.ErrorHandler().ReportError(
					"MeshCooker: unsupported serialized export type '%hs' in VanguardAssetHost mesh '%hs'",
					className,
					path.ToDebugString() );
				return false;
			}
		}

		return true;
	}
#endif

	bool MeshCooker::CanCook( const res::ResourcePath& resourcePath ) const
	{
		static red::DynArray< const char* > s_meshLikeExtensions = GetMeshLikeFileExtensions();
		auto extension = red::paths::GetExtension( resourcePath.ToStringView() );
		return std::any_of( s_meshLikeExtensions.Begin(), s_meshLikeExtensions.End(), [ extension ]( const char* meshLikeExtension )
		{
			return meshLikeExtension == extension;
		} );
	}

	bool MeshCooker::CollectDependencies( const res::ResourcePath& path, job::Builder& builder, dependency::Collector& collector, data::Context& system ) const
	{
		collector.Add( MakeSourceDependency( path, dependency::Requirement::Required ) );
		if( ShouldMergeMeshDependencies( path, system.TargetPlatform() ) )
		{
			// Add them all as dependencies, because cooking could change them from localMaterialBuffer "soft imports" to inplaceResources
			resdb::Database& database = system.SourceResourceDatabase();
			const auto resourceDependencies = database.GetForwardResourcePaths( path );
			for( const res::ResourcePath& dependencyPath : resourceDependencies )
			{
				collector.Add( MakeSourceDependency( dependencyPath ) );
				collector.Add( MakeBuiltDependency( dependencyPath, data::DepotLocation::Cooked ) );
			}

			const auto recursiveDependencies = dependency::LookupRecursiveHard( database, { path } );
			for( const res::ResourcePath& dep : recursiveDependencies )
			{
				if( dep != path )
				{
					collector.Add( MakeSourceDependency( dep ) );
					collector.Add( MakeBuiltDependency( dep, data::DepotLocation::Cooked ) );
				}
			}

#if !defined( VANGUARD_ASSET_HOST )
			auto tstPath = path.ToString();
			if( tstPath.Contains( "prx" ) && tstPath.Contains( "\\road_meshes" ) )
			{
				collector.Add( MakeSourceDependency( RED_CONST_RESOURCEPATH( "engine\\materials\\metal_base_proxy.mt" ) ) );
			}
#endif
		}

#if !defined( VANGUARD_ASSET_HOST )
		auto pathSpan = GetMeshArtifactPaths();
		for( const auto& artifactPath : pathSpan )
		{
			collector.Add( MakeSourceDependency( artifactPath, dependency::Requirement::Optional ) );
		}
#endif
		return true;
	}


	struct MeshCookerContext
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

		res::ResourceTokenHandle meshToken;

		THandle< CMesh > mesh;
		red::DynArray< res::ResourceTokenHandle > dependencyTokens{ red::PoolBackend() };
		data::Context* system;
		res::ResourcePath path;
		res::ResourceTokenHandle proxyMatToken;
	};

	bool MeshCooker::Cook( const res::ResourcePath& path, job::Builder& builder, data::Context& system ) const
	{
#if defined( VANGUARD_ASSET_HOST )
		// RED's normal loader skips exports whose RTTI class is unavailable when
		// verifyExportTypes is false. That is acceptable for runtime compatibility
		// but not for this authoring host: omitted specialized mesh parameters must
		// fail closed instead of disappearing silently during a cook.
		if( !ValidateAssetHostMeshExportTypes( path, system ) )
		{
			return false;
		}
#endif

		// Load with all imports and call OnPostLoad
		res::IssueLoadingRequestParameter param;
		param.path = path;
		param.postLoadFlags.skipUploadRenderData = true;

		auto meshContext = red::CreateSharedPtr< MeshCookerContext >();
		meshContext->system = &system;
		meshContext->path = path;
		meshContext->meshToken = system.SourceLoader().IssueLoadingRequest( param );
		
#if !defined( VANGUARD_ASSET_HOST )
		builder.DispatchWait( MeshCookingCache::GetInstance().LoadArtifacts(system) );
#endif
		builder.DispatchWait( meshContext->meshToken->GetWaitCounter() );

		builder.DispatchJob( "MeshCooker_MeshLoaded", [ meshContext ]( const job::RunContext& runContext )
		{
			if( !meshContext->meshToken->IsLoaded() )
			{
				meshContext->system->ErrorHandler().ReportError( "MeshCooker: Failed to load '%hs'", meshContext->path.ToDebugString() );
				meshContext->system->ErrorHandler().Fail();
				return;
			}

				meshContext->mesh = Cast< CMesh >( meshContext->meshToken->GetResource() );

#if defined( VANGUARD_ASSET_HOST )
				if( NeedsProxyMeshStramsConvert( meshContext->mesh ) )
				{
					meshContext->system->ErrorHandler().ReportError(
						"MeshCooker: road proxy mesh conversion is unsupported in VanguardAssetHost: '%hs'",
						meshContext->path.ToDebugString() );
					meshContext->system->ErrorHandler().Fail();
					return;
				}
#endif

				job::Builder depBuilder( runContext );

			Uint8 consoleBias = meshContext->mesh->GetConsoleMeshBias();
			const data::TargetPlatform platform = meshContext->system->TargetPlatform();
			if( ( platform == data::TargetPlatform::XBOX1 || platform == data::TargetPlatform::PS4 )
				&& consoleBias != 0
				&& consoleBias < meshContext->mesh->GetNumberOfLODs()
				&& meshContext->mesh->GetNumberOfLODs() <= 4 ) // ctremblay: magic number here. Some mesh got insane amount of lod and it makes modifying it crash if more than 4.
			{
				mesh::DataView meshDataView = mesh::DataView( *meshContext->mesh );
				Float lodDistanceShift = 0.0f;
				while( consoleBias-- != 0 )
				{
					if( meshDataView.GetNumLODs() > 1 )
					{
						lodDistanceShift += meshDataView.GetLODDistance( 1u );
					}
					meshDataView.RemoveLODWithData( 0, RemoveLODMode::ClearUnusedChunks );					
					meshDataView.SetLODDistance( 0, 0.0f );
				}

				// shift the lod distances by the amount of distance dropped
				for(Uint32 i=1; i<meshDataView.GetNumLODs(); i+=1 )
				{
					meshDataView.SetLODDistance(i, Max( meshDataView.GetLODDistance(i) - lodDistanceShift, 0.0f ) );
				}

				meshDataView.Apply( true );
			}

			if( ShouldMergeMeshDependencies( meshContext->path, meshContext->system->TargetPlatform() ) )
			{
				depBuilder.DispatchJob( "MeshCooker/GatherDependencies", [ meshContext ]( const job::RunContext& runContext )
				{

					// !!! FIXME HACK: this is predicting how the mesh will be RESAVED when cooking!!!
					// Kind of hope this is correct and doesn't emplace any resources it shouldn't.
					// What we really need is a cooked version of the resourceDB.
					Bool preloadAppearances = false;
					if( meshContext->mesh->GetAllAppearances().Size() == 1 || meshContext->mesh->IsForceLoadAllAppearances() )
					{
						preloadAppearances = true;
					}

					job::Builder depBuilder{ runContext };
					const Bool includeSoftImports = preloadAppearances;
					resdb::Database& database = meshContext->system->SourceResourceDatabase();
					auto resourceDependencies = dependency::CollectMergeableDependencies( meshContext->path, meshContext->system->TargetPlatform(), database, includeSoftImports );

					for( const res::ResourcePath& dep : dependency::LookupRecursiveHard( database, { meshContext->path } ) )
					{
						// Include tiny files without deps
						const auto str = dep.ToStringView();
						if( str.EndsWith( ".mi" ) || str.EndsWith( ".fp" ) )
						{
							red::alg::PushBackUnique( resourceDependencies, dep );
						}
						else if( str.EndsWith( ".mlmask" ) )
						{
							auto mlmask = database.LookupResource( dep );
							if( mlmask && mlmask->totalSize <= RED_KILO_BYTE( 32 ) )
							{
								red::alg::PushBackUnique( resourceDependencies, dep );
							}

						}

					}

					const auto pathView = meshContext->path.ToStringView();
					const bool isProxy = ( pathView.StartsWith( "base\\characters\\appearances" ) || pathView.StartsWith( "base\\vehicles\\appearances" ) ) && pathView.Find( "\\proxy\\" ) != red::StringView::npos;

					if( isProxy )
					{
						for( const res::ResourcePath& dep : dependency::LookupRecursive( database, {meshContext->path} ) )
						{
							const auto depView = dep.ToStringView();

							if( depView.EndsWith( ".xbm" ) && ( depView.StartsWith( "base\\characters\\appearances" ) || depView.StartsWith( "base\\vehicles\\appearances" ) ) )
							{
								red::alg::PushBackUnique( resourceDependencies, dep );
							}

						}
					}

					for( const res::ResourcePath& dependencyPath : resourceDependencies )
					{
						res::IssueLoadingRequestParameter param;
						param.path = dependencyPath;
						param.postLoadFlags.skipUploadRenderData = true;
						auto token = meshContext->system->CookedLoader().IssueLoadingRequest( param );
						meshContext->dependencyTokens.PushBack( token );
						depBuilder.DispatchWait( token->GetWaitCounter() );
					}
				} );
			}

			for ( const THandle< IMaterial > mat : meshContext->mesh->GetMaterials() )
			{
				if ( THandle< CMaterialInstance > matInstance = Cast< CMaterialInstance >( mat ) )
				{
					matInstance->EnsureGlassMaskUpdated( depBuilder );
				}
			}

#if !defined( VANGUARD_ASSET_HOST )
				if( NeedsProxyMeshStramsConvert( meshContext->mesh ) )
				{
				depBuilder.DispatchJob( "MeshCooker/PreloadProxyMaterial", [ meshContext ]( const job::RunContext& runContext )
				{
					job::Builder depBuilder{ runContext };

					res::IssueLoadingRequestParameter param;
					param.path = RED_CONST_RESOURCEPATH( "engine\\materials\\metal_base_proxy.mt" );
					param.postLoadFlags.skipUploadRenderData = true;
					res::ResourceTokenHandle token = meshContext->system->SourceLoader().IssueLoadingRequest( param );
					meshContext->proxyMatToken = token;
					depBuilder.DispatchWait( token->GetWaitCounter() );
					} );
				}
#endif

			depBuilder.DispatchJob( "MeshCooker/Cook", [ meshContext ]( const job::RunContext& )
			{
				/// @todo DP: test tag for player garments, add garment mesh param
				Bool isThisPlayerGarmentMesh = false;
				THandle< res::ResourceMetadata > metadata = res::ResourceMetadata::Get( meshContext->mesh );
				if( metadata != nullptr )
				{
					red::DynArray< CName > fileTags = metadata->GetTags();
					for( const CName& tag : fileTags )
					{
						if( tag == RED_NAME_CONSTEXPR_NOREG( "player_item_mesh" ) )
						{
							isThisPlayerGarmentMesh = true;
							break;
						}
					}
				}

#if defined( VANGUARD_ASSET_HOST )
				if( isThisPlayerGarmentMesh )
				{
					meshContext->system->ErrorHandler().ReportError(
						"MeshCooker: player garment meshes are unsupported in VanguardAssetHost: '%hs'",
						meshContext->path.ToDebugString() );
					meshContext->system->ErrorHandler().Fail();
					return;
				}
#endif

				meshContext->mesh->CacheShadowsSettingsForCooking();

#if !defined( VANGUARD_ASSET_HOST )
				if( THandle<MeshParamPhysics>& physics = meshContext->mesh->GetParameter<MeshParamPhysics>() )
				{
					if( THandle<physics::SystemResource>& physicsData = physics->m_physicsData )
					{
						physics::Bodies& bodies = physicsData->GetBodies();
						for( THandle<physics::SystemBody>& body : bodies )
						{
							for( THandle<physics::ICollider>& collider : body->m_collisionShapes )
							{
								collider->CookCollision();
							}
						}
					}
				}

				if( isThisPlayerGarmentMesh )
				{
					Bool hasMorphOffsets = false;
					Bool hasGarmentFlags = false;
					Bool garmentDataIsValid = garment::CheckMeshGarmentStreams( *meshContext->mesh, &hasMorphOffsets, &hasGarmentFlags );

					RED_LOG_WARNING( "Cooking player mesh resource '%hs', has valid garment data %u", meshContext->path.ToDebugString(), garmentDataIsValid );

					if( garmentDataIsValid && hasGarmentFlags )
					{
						THandle< garment::MeshParamGarment > param = meshContext->mesh->CreateParameter< garment::MeshParamGarment >();
						param->SetFromMesh( *meshContext->mesh );
					}
				}
#endif

#if !defined( VANGUARD_ASSET_HOST )
					MeshCooker::ConvertProxyMeshStreams( meshContext->mesh, meshContext->proxyMatToken );
#endif
				MeshCooker::RemoveUncookedParameters( meshContext->mesh );

				red::DynArray< THandle< CResource > > inplaceResources{ red::PoolBackend() };
				inplaceResources.Reserve( meshContext->dependencyTokens.Size() );
				for( const res::ResourceTokenHandle& token : meshContext->dependencyTokens )
				{
					// ??? Should ANY fail since we got them from the resDB
					// Suppose it's possible if it's a broken reference, but should have cleared these sooner during cooking?
					RED_FATAL_ASSERT( !token->HasFailed() || token->GetError() == res::ResourceTokenErrorType::ResourceNotFound, "Unexpected resource error type %u: %hs", (Uint32)token->GetError(), token->GetPath().ToDebugString() );
					if( !token->HasFailed() )
					{
						THandle< CResource > resource = token->GetResource();
						inplaceResources.PushBack( resource );
						meshContext->mesh->AddInplaceResourceDependency( token->GetPath() );
					}
				}

#if !defined( VANGUARD_ASSET_HOST )
				// If it's destructible, update destruction params in case chunks changed during cooking
				{
					if( auto& meshChunks = meshContext->mesh->GetParameter<mesh::MeshParamDestructionChunkIndicesOffsets>() )
					{
						meshChunks->OnMeshChanged( *meshContext->mesh );
					}

					if( auto& meshChunks = meshContext->mesh->GetParameter<mesh::MeshParamDestructionBoneChunkMapping>() )
					{
						meshChunks->OnMeshChanged( *meshContext->mesh );
					}
				}
#endif

				bool saveResult = cooker::SaveResource( meshContext->path, meshContext->mesh, meshContext->system->CookedDepot(), meshContext->system->TargetPlatform(), inplaceResources );
				if( !saveResult )
				{
					meshContext->system->ErrorHandler().ReportError( "MeshCooker: Failed to save '%hs'", meshContext->path.ToDebugString() );
					meshContext->system->ErrorHandler().Fail();
					return;
				}
			} );

		} );


		return true;
	}

	Uint32 MeshCooker::GetVersion() const
	{
		return 23;
	}

#if !defined( VANGUARD_ASSET_HOST )
	Bool ShouldRemovePhysics( THandle< CMesh >& mesh )
	{
		res::ResourcePath meshPath = mesh->GetPath();
		auto meshString = meshPath.ToString();
		return meshString.EndsWith( "s0_re.mesh" ) // road intersection mesh
			|| meshString.EndsWith( "_mc.mesh" ) // merged collision mesh
			|| MeshCookingCache::GetInstance().Exist( meshPath );
	}

	bool MeshCooker::RemoveUncookedParameters( THandle< CMesh >& mesh )
	{
		// Remove MeshParameter classes that don't need to be in the cooked data here
		mesh->RemoveParameter( mesh::MeshParamImportedSnapPoint::GetStaticClass() );
		mesh->RemoveParameter( mesh::MeshParamShadowMeshCreationData::GetStaticClass() );
		mesh->RemoveParameter( mesh::MeshParamCompiledPhysics::GetStaticClass() );

		// bent road meshes
		if( mesh->GetParameter< mesh::MeshParamBendedRoad >() )
		{
			mesh->RemoveParameter( mesh::MeshParamBendedRoad::GetStaticClass() );
			mesh->RemoveParameter( mesh::MeshParamOccluderData::GetStaticClass() ); //road occluders merged into groups and placed in prefabs as separate mesh.
			mesh->RemoveParameter( mesh::MeshParamPhysics::GetStaticClass() ); //already generated and not used outside BendedMeshNode
		}

		if( ShouldRemovePhysics( mesh ) ) 
		{
			mesh->RemoveParameter( mesh::MeshParamPhysics::GetStaticClass() ); // already generated and not used directly from mesh
		}

		// If the mesh has no MeshParamDestructionChunkIndicesOffsets, it's defintiely not destructible so remove the destruction params
		if( !mesh->GetParameter<mesh::MeshParamDestructionChunkIndicesOffsets>() )
		{
			mesh->RemoveParameter( mesh::MeshParamDestructionBonds::GetStaticClass() );
			mesh->RemoveParameter( mesh::MeshParamDestructionBoneChunkMapping::GetStaticClass() );
			mesh->RemoveParameter( mesh::MeshParamDestructionStepData::GetStaticClass() );
		}

		return true;
	}
#else
		bool MeshCooker::RemoveUncookedParameters( THandle< CMesh >& mesh )
		{
			// AssetHost admits only meshes whose serialized export classes are all
			// present in its narrow RTTI closure. Unsupported specialized parameters
			// are therefore rejected before load instead of being silently discarded.
			// Selected topology parameters use ShouldBeStrippedWhenCooking() and are
			// removed by CMesh::OnPreSave.
			RED_UNUSED( mesh );
			return true;
		}
#endif

	template < typename _T >
	void copyParameter( const THandle< CMaterialInstance >& source, THandle< CMaterialInstance >& target, const CName& parameter )
	{
		_T v;
		_T v_;

		source->ReadParameter( parameter, v );
		target->ReadParameter( parameter, v_ );
		if( v != v_ )
		{
			target->WriteParameter_NoDefinitionCheck( parameter, v, false );
		}
	}

	bool MeshCooker::NeedsProxyMeshStramsConvert( THandle< CMesh >& mesh )
	{
		//Just road proxy meshes
		auto path = mesh->GetPath().ToString();
		return path.Contains( "prx" ) && path.Contains( "\\road_meshes" );
	}

	bool MeshCooker::ConvertProxyMeshStreams( THandle< CMesh >& mesh, res::ResourceTokenHandle prxMat )
	{
		static res::ResourcePath c_metalBasePath = RED_CONST_RESOURCEPATH( "engine\\materials\\metal_base.remt" );
		static res::ResourcePath c_windowParallaxProxyPath = RED_CONST_RESOURCEPATH( "base\\materials\\window_parallax_interior_proxy.mt" );
		static res::ResourcePath c_windowVeryLongDistance = RED_CONST_RESOURCEPATH( "base\\materials\\window_very_long_distance.mt" );
		static res::ResourcePath c_metalBaseProxyPath = RED_CONST_RESOURCEPATH( "engine\\materials\\metal_base_proxy.mt" );

		if( NeedsProxyMeshStramsConvert( mesh ) )
		{
			Bool resourceModified = false;
			THandle< CResource > resource = prxMat->GetResource();
			THandle< IMaterial > metalBaseProxyMt = Cast< IMaterial >( resource );

			mesh::DataView view = mesh::DataView( *mesh );
			for( Uint8 chunkIndex = 0; chunkIndex < view.GetNumChunks(); chunkIndex++ )
			{
				bool skipChunk = false;
				if( view.GetChunkVertexType( chunkIndex ) == MVT_ProxyMesh || view.GetChunkVertexType( chunkIndex ) == MVT_ProxyWindowMesh )
				{
					continue;
				}

				for( Uint8 i = 0; i < view.GetChunkMaterialCount( chunkIndex ); i++ )
				{
					auto materialName = view.GetChunkMaterialName( chunkIndex, i );
					if( THandle<IMaterial> material = view.GetMaterialDataByName( materialName ) )
					{
						Uint8 materialIndex = view.GetMaterialIndex( materialName );
						res::ResourcePath basePath = material->GetBaseMaterialPath();
						if( basePath != c_metalBasePath && basePath != c_windowParallaxProxyPath && basePath != c_windowVeryLongDistance && basePath != c_metalBaseProxyPath)
						{
							skipChunk = true;
							break;
						}

						if( basePath == c_metalBasePath )
						{
							if( THandle< CMaterialInstance > mbInstance = Cast< CMaterialInstance >( material ) )
							{
								THandle< CMaterialInstance > metalBaseProxy = CreateHandle< CMaterialInstance >( view.GetMesh(), metalBaseProxyMt );
								metalBaseProxy->SetMaskEnabled( mbInstance->IsMasked() );

								copyParameter< TResRef< ITexture > >( mbInstance, metalBaseProxy, RED_NAME_CONSTEXPR( "BaseColor" ) );
								copyParameter< TResRef< ITexture > >( mbInstance, metalBaseProxy, RED_NAME_CONSTEXPR( "Roughness" ) );
								copyParameter< TResRef< ITexture > >( mbInstance, metalBaseProxy, RED_NAME_CONSTEXPR( "Normal" ) );
								copyParameter< TResRef< ITexture > >( mbInstance, metalBaseProxy, RED_NAME_CONSTEXPR( "Metalness" ) );
								copyParameter< TResRef< ITexture > >( mbInstance, metalBaseProxy, RED_NAME_CONSTEXPR( "Emissive" ) );
								copyParameter< Vector4 >( mbInstance, metalBaseProxy, RED_NAME_CONSTEXPR( "BaseColorScale" ) );
								copyParameter< Float >( mbInstance, metalBaseProxy, RED_NAME_CONSTEXPR( "MetalnessScale" ) );
								copyParameter< Float >( mbInstance, metalBaseProxy, RED_NAME_CONSTEXPR( "RoughnessScale" ) );
								copyParameter< Float >( mbInstance, metalBaseProxy, RED_NAME_CONSTEXPR( "RoughnessBias" ) );
								copyParameter< Float >( mbInstance, metalBaseProxy, RED_NAME_CONSTEXPR( "NormalStrength" ) );
								copyParameter< Float >( mbInstance, metalBaseProxy, RED_NAME_CONSTEXPR( "EmissiveEV" ) );
								copyParameter< Float >( mbInstance, metalBaseProxy, RED_NAME_CONSTEXPR( "EmissiveEVRaytracingBias" ) );
								copyParameter< Float >( mbInstance, metalBaseProxy, RED_NAME_CONSTEXPR( "EmissiveLift" ) );
								copyParameter< Float >( mbInstance, metalBaseProxy, RED_NAME_CONSTEXPR( "EmissiveDirectionality" ) );
								copyParameter< Float >( mbInstance, metalBaseProxy, RED_NAME_CONSTEXPR( "EnableRaytracedEmissive" ) );
								copyParameter< Float >( mbInstance, metalBaseProxy, RED_NAME_CONSTEXPR( "AlphaThreshold" ) );
								copyParameter< Float >( mbInstance, metalBaseProxy, RED_NAME_CONSTEXPR( "LayerTile" ) );
								copyParameter< Color >( mbInstance, metalBaseProxy, RED_NAME_CONSTEXPR( "EmissiveColor" ) );

								view.SetMaterialData( materialIndex, metalBaseProxy );
							}
						}

					}
				}

				if( skipChunk ) continue;

				resourceModified = true;
				view.RemoveChunkStream( chunkIndex, MST_Binormal_3F );
				view.RemoveChunkStream( chunkIndex, MST_Tangent_3F );
				view.RemoveChunkStream( chunkIndex, MST_TexCoord1_2F );
				view.RemoveChunkStream( chunkIndex, MST_Color_U32 );
				view.RemoveChunkStream( chunkIndex, MST_SkinningIndices_4U8 );
				view.RemoveChunkStream( chunkIndex, MST_SkinningWeights_4F );
				view.RemoveChunkStream( chunkIndex, MST_SkinningIndicesExt_4U8 );
				view.RemoveChunkStream( chunkIndex, MST_SkinningWeightsExt_4F );
				switch( view.GetChunkVertexType( chunkIndex ) )
				{
				case MVT_DestructibleMesh:
					view.SetChunkVertexType( chunkIndex, MVT_ProxyWindowMesh );
					break;
				default:
					view.SetChunkVertexType( chunkIndex, MVT_ProxyMesh );
				}
			}

			if( resourceModified )
			{
				view.ForceRecreateBlob();
				view.ForceRecreateRenderResource();
				view.Apply( false );
			}
		}

		return true;
	}

#if !defined( VANGUARD_ASSET_HOST )
	class MorphTargetMeshCooker : public ICooker
	{
	public:
		RTTI_DECLARE_TYPE( MorphTargetMeshCooker );

		virtual ~MorphTargetMeshCooker() override = default;

		virtual bool CanCook( const res::ResourcePath& resourcePath ) const override;
		virtual bool Cook( const res::ResourcePath& path, job::Builder& builder, data::Context& system ) const override;
		virtual bool CollectDependencies( const res::ResourcePath& path, job::Builder& builder, dependency::Collector& collector, data::Context& system ) const override;

		virtual Uint32 GetVersion() const override;
	};

	bool MorphTargetMeshCooker::CanCook( const res::ResourcePath& resourcePath ) const
	{
		auto extension = red::paths::GetExtension( resourcePath.ToStringView() );
		return extension == MorphTargetMesh::GetFileExtension();
	}

	bool MorphTargetMeshCooker::Cook( const res::ResourcePath& path, job::Builder& builder, data::Context& system ) const
	{
		// Load with all imports and call OnPostLoad
		res::IssueLoadingRequestParameter param;
		param.path = path;
		param.postLoadFlags.skipUploadRenderData = true;
		auto token = system.SourceLoader().IssueLoadingRequest( param );

		builder.DispatchWait( token->GetWaitCounter() );
		builder.DispatchJob( "MorphTargetMeshCooker/Cook", [ token, path, &system ]( const job::RunContext& )
		{
			if( !token->IsLoaded() )
			{
				system.ErrorHandler().ReportError( "MorphTargetMeshCooker: Failed to load '%hs'", path.ToDebugString() );
				system.ErrorHandler().Fail();
				return;
			}
			auto morphTargetMesh = Cast< MorphTargetMesh >( token->GetResource() );
			RED_FATAL_ASSERT( morphTargetMesh );

			bool saveResult = cooker::SaveResource( path, morphTargetMesh, system.CookedDepot(), system.TargetPlatform() );
			if( !saveResult )
			{
				system.ErrorHandler().ReportError( "MorphTargetMeshCooker: Failed to save '%hs'", path.ToDebugString() );
				system.ErrorHandler().Fail();
				return;
			}
		} );

		return true;
	}

	bool MorphTargetMeshCooker::CollectDependencies( const res::ResourcePath& path, job::Builder& builder, dependency::Collector& collector, data::Context& system ) const
	{
		collector.Add( MakeSourceDependency( path, dependency::Requirement::Required ) );

		static red::DynArray< const char* > s_meshLikeExtensions = GetMeshLikeFileExtensions();

		// Add all meshes from morphTargetEntries as dependencies, because cooking could change them and we should potentially cache bones and rig matrices
		resdb::Database& database = system.SourceResourceDatabase();
		const auto resourceDependencies = database.GetForwardResourcePaths( path );
		for( const res::ResourcePath& dependencyPath : resourceDependencies )
		{
			auto extension = red::paths::GetExtension( dependencyPath.ToStringView() );
			if( std::any_of( s_meshLikeExtensions.Begin(), s_meshLikeExtensions.End(), [ extension ]( const char* meshLikeExtension ) { return meshLikeExtension == extension; } ) )
			{
				collector.Add( MakeSourceDependency_NoRescan( dependencyPath ) );
			}
		}

		return true;
	}

	Uint32 MorphTargetMeshCooker::GetVersion() const
	{
		return 1;
	}
#endif

} // mesh

RTTI_BEGIN_TYPE_IN_NAMESPACE( MeshCooker, mesh );
	RTTI_PARENT_TYPE( ICooker );
RTTI_END_TYPE();

#if !defined( VANGUARD_ASSET_HOST )
RTTI_BEGIN_TYPE_IN_NAMESPACE( MorphTargetMeshCooker, mesh );
	RTTI_PARENT_TYPE( ICooker );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( MeshCookingCacheReference, mesh )
	RTTI_PARENT_TYPE( data::GlobalCacheReference )
RTTI_END_TYPE()
#endif
