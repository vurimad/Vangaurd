/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "meshAppearance.h"
#include "meshDataViewRO.h"
#if !defined( VANGUARD_ASSET_HOST )
#include "materialSetupParamsProvider.h"
#endif
#include "../../renderData/include/renderResource.h"
#if !defined( VANGUARD_ASSET_HOST )
#include "../../renderData/include/renderer.h"
#endif
#include "../../resourceMaterial/include/material.h"
#include "../../redJobs2/include/jobBuilder.h"
#include "mesh.h"
#include "../../redCore/include/globalModeInfo.h"
#include "../../resourceMaterial/include/materialDefinition.h"
#include "../../redReflection/include/postLoadContext.h"
#include "../../redReflection/include/serializationLoader.h"
#include "../../redReflection/include/variant.h"

using red::DynArray;
using red::String;

RTTI_BEGIN_TYPE_IN_NAMESPACE( ChunkMaterials, mesh );
	RTTI_PROPERTY( m_materialNames ).editable().resizable().minSize( 1u ).customInnerTypeEditor( "MeshAppearanceMaterialName" );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( MeshAppearance, mesh );
	RTTI_PARENT_TYPE( ISerializable );
	RTTI_PROPERTY( m_name ).readOnly();
	RTTI_PROPERTY( m_chunkMaterials ).editable().inlined();
	RTTI_PROPERTY( m_tags ).editable().resizable();
RTTI_END_TYPE();


namespace mesh
{
	const char MeshAppearance::c_DefaultAppearanceNameString[] = "default";

	const CName MeshAppearance::c_DefaultAppearanceName = RED_NAME( c_DefaultAppearanceNameString );

	AppearanceMaterialsRef::AppearanceMaterialsRef() = default;

	AppearanceMaterialsRef::AppearanceMaterialsRef(const THandle<MeshAppearance>& appearance)
		: m_appearance(appearance)
	{
		RED_FATAL_ASSERT(m_appearance);
	}

	AppearanceMaterialsRef::~AppearanceMaterialsRef()
	{
		if (m_appearance)
		{
			m_appearance->ReleaseRenderMaterialSetup();
		}
	}

	const job::Counter& AppearanceMaterialsRef::GetWaitCounter() const
	{
		RED_FATAL_ASSERT(m_appearance);
		return m_appearance->GetWaitCounter();
	}

	Bool AppearanceMaterialsRef::MustWaitForCounter() const
	{
		return !GetWaitCounter().Internal_IsZeroSnapshot();
	}

	mesh::AppearanceMaterialsRef& AppearanceMaterialsRef::operator=(AppearanceMaterialsRef&& rhs)
	{
		if (this != &rhs)
		{
			if (m_appearance)
			{
				m_appearance->ReleaseRenderMaterialSetup();
			}
			m_appearance = std::move(rhs.m_appearance);
		}
		return *this;
	}

	void AppearanceMaterialsRef::Clear()
	{
		m_appearance.Reset();
	}

	MeshAppearance::MeshAppearance()
		: m_name( MeshAppearance::c_DefaultAppearanceName )
		, m_meshOwner( nullptr )
		, m_materialsCache( rend::PoolMesh() )
		, m_renderMaterialSetupRefCount( 0 )
		, m_isRenderMaterialSetupPinned()
		, m_waitCounter( job::Priority::Latent )
	{
	}

	// #tbd: why without copying renderMaterialSetup?
	MeshAppearance::MeshAppearance( const MeshAppearance& other, CName name, CMesh* meshOwner )
		: m_name( name )
		, m_chunkMaterials( other.m_chunkMaterials )
		, m_meshOwner( meshOwner ? meshOwner : other.m_meshOwner )
		, m_materialsCache( rend::PoolMesh() )
		, m_renderMaterialSetupRefCount( 0 )
		, m_isRenderMaterialSetupPinned()
		, m_waitCounter( job::Priority::Latent )
	{
		RED_FATAL_ASSERT(other.m_waitCounter.Internal_IsZeroSnapshot());
	}

	MeshAppearance::MeshAppearance( CMesh* meshOwner, CName name )
		: m_name( name )
		, m_meshOwner( meshOwner )
		, m_materialsCache( rend::PoolMesh() )
		, m_renderMaterialSetupRefCount( 0 )
		, m_isRenderMaterialSetupPinned()
		, m_waitCounter( job::Priority::Latent )
	{
		mesh::DataViewRO meshView( *meshOwner );

		m_chunkMaterials.Resize( meshView.GetNumChunks() );
		for ( Uint32 chunkIndex = 0; chunkIndex < meshView.GetNumChunks(); ++chunkIndex )
		{
			m_chunkMaterials[ chunkIndex ] = meshView.GetChunkMaterialNames( chunkIndex )[ 0 ];
		}
	}

	MeshAppearance::MeshAppearance( const red::DynArray< CName >& matNames, CMesh* meshOwner, CName name )
		: m_name( name )
		, m_meshOwner( meshOwner )
		, m_materialsCache( rend::PoolMesh() )
		, m_renderMaterialSetupRefCount( 0 )
		, m_isRenderMaterialSetupPinned()
		, m_waitCounter( job::Priority::Latent )
	{
		Uint32 numMats = matNames.Size();
		m_chunkMaterials.Resize( numMats );
		for( Uint32 chunkIndex = 0; chunkIndex < numMats; ++chunkIndex )
		{
			m_chunkMaterials[ chunkIndex ] = matNames[ chunkIndex ];
		}
	}

	MeshAppearance::~MeshAppearance()
	{
	}

	THandle< IMaterial > MeshAppearance::GetChunkMaterialData( Uint32 chunkIndex ) const
	{
		const CName& materialName = m_chunkMaterials[ chunkIndex ];
		mesh::DataViewRO meshView( *m_meshOwner );
		return meshView.GetMaterialDataByName( materialName );
	}

	void MeshAppearance::SetTags( const red::DynArray<CName>& tags )
	{
		m_tags = tags;
	}

	namespace helper
	{
		static void CollectAppearanceMaterialNamesToEnsureLoaded(const red::DynArray< CName >& chunkMaterials, red::DynArray<CName>& outMaterialNames)
		{
			// NOTE: This can also be done offline and saved in the apperance!

			// Magic if m_materialNames.Size() > 1
			// see CMaterialSetupParamsProvider, if multiple materials it will use the always loaded legacy_multilayer_fallback.

			outMaterialNames.Reserve( chunkMaterials.Size() );

			// Collect unique materials.
			for ( const auto& it : chunkMaterials )
			{
				red::alg::PushBackUnique( outMaterialNames, it );
			}
		}
	}

	AppearanceMaterialsRef MeshAppearance::IssueLoadRenderMaterialSetup(io::EAsyncPriority priority)
	{
		const io::EAsyncPriority ioPriorityReadFromTLS = io::GetThreadLocalIOPriority();
		if ( ioPriorityReadFromTLS != io::eAsyncPriority_INVALID )
		{
			// this takes precedence over manually set priority
			priority = ioPriorityReadFromTLS;
		}

		// Nothing will update this MeshAppearance probably (welcome to try)
		// and we've loaded the mesh anyway at this point, so streaming is sort of pointless
		if (priority == io::eAsyncPriority_Streaming)
		{
			priority = io::eAsyncPriority_Normal;
		}

		// NOTE: we also check if the counter is > 0, which means loading is already in progress.
		// Jobs are only run on the counter under the lock, so no race with renderMaterialSetup being set or not.

		AppearanceMaterialsRef token{ HandleFromThis<MeshAppearance>() };

		// Fast path if was already preloaded, avoids exclusive lock especially. Do it here instead of requiring convoluted checks in the callers.
		{
#ifndef NO_EDITOR
			// Pinned state can only update in the editor after ForceReloadRenderMaterialSetup() is called.
			RED_SCOPE_SHARED_LOCK(m_loadRenderMaterialSetupLock);
#endif

			// NOTE: Not incrementing m_renderMaterialSetupRefCount which would require an exclusive lock
			// The token checks if pinned to know that the refcount wasn't incremented and relies on this pinned state
			// being one way
			if (m_isRenderMaterialSetupPinned)
			{
				RED_FATAL_ASSERT( m_renderMaterialSetup || !m_waitCounter.Internal_IsZeroSnapshot() );
				return token;
			}
		}

		// Exclusive lock; can't use R/RMW pattern since always need to increment renderMaterialSetupRefCount
		// And making renderMaterialSetupRefCount atomic won't help since it needs to be synchronized with setting or clearing m_renderMaterialSetup
		//
		// Could check again here if pinned and do the above fast path, but generally not worth the complication
		// since it will never happen in the game without editor. Since it'll either be pinned or not pinned when the mesh is loaded.
		{
			RED_SCOPE_LOCK(m_loadRenderMaterialSetupLock);

			m_renderMaterialSetupRefCount += 1;

			if (m_renderMaterialSetup || !m_waitCounter.Internal_IsZeroSnapshot())
			{
				// DEBUG NOTE: waitCounter can still be > 0 if the last job hasn't returned yet, but this set under lock so OK
				// However, to ensure propery synchronization publically accessing renderMaterialSetup has an assert that waitCounter == 0
				return token;
			}

			if (ShouldPinRenderMaterialSetup())
			{
				m_isRenderMaterialSetupPinned.Set();
			}

			red::DynArray< CName > materialNamesToEnsureLoaded{ rend::PoolMesh() }; // should be job/frame memory!
			helper::CollectAppearanceMaterialNamesToEnsureLoaded(m_chunkMaterials, materialNamesToEnsureLoaded);

            auto weakSelf = WeakHandleFromThis<MeshAppearance>();
            job::Builder builder{ job::Priority::Latent };
            auto asyncResult = m_meshOwner->IssueLoadMaterials( materialNamesToEnsureLoaded, priority );
            builder.DispatchWait( asyncResult.first );
            builder.DispatchJob( "MeshAppearance/IssueLoadRenderMaterialSetup/ProcessResults", [weakSelf, materialNamesToEnsureLoaded = std::move( materialNamesToEnsureLoaded ), materialsResult = asyncResult.second]( const job::RunContext& ) {
                auto& loadedMaterials = materialsResult->arr;
                auto self = weakSelf.ToHandle();
                if( !self )
                {
                    return;
                }

                auto& loadRenderMaterialSetupLock = self->m_loadRenderMaterialSetupLock;
                RED_SCOPE_LOCK( loadRenderMaterialSetupLock );

                // Check if abandoned while loading; but if pinned then finish loading it anyway, less surprise
                if( self->m_renderMaterialSetupRefCount == 0 && !self->m_isRenderMaterialSetupPinned )
                {
                    return;
                }

                RED_FATAL_ASSERT( materialNamesToEnsureLoaded.Size() == loadedMaterials.Size() );
                self->m_materialsCache.Reserve( materialNamesToEnsureLoaded.Size() );
                for( Uint32 i : materialNamesToEnsureLoaded.Indices() )
                {
                    RED_FATAL_ASSERT( loadedMaterials[i], "Should have been assigned the fallback material" );
                    self->m_materialsCache.InsertUnsorted( materialNamesToEnsureLoaded[i], std::move( loadedMaterials[i] ) );
                }
                self->m_materialsCache.MakeClean();

                self->FinishRecreateRenderMaterialSetup_NoLock( false );
            } );

            m_waitCounter += builder.ExtractWaitCounter();
		}

		return token;
	}

	red::DynArray< res::ResourcePath > MeshAppearance::CollectDependencies() const
	{
		red::DynArray< CName > materialNamesToEnsureLoaded{red::PoolBackend()}; 
		helper::CollectAppearanceMaterialNamesToEnsureLoaded( m_chunkMaterials, materialNamesToEnsureLoaded );
		return m_meshOwner->CollectDependencies( materialNamesToEnsureLoaded );
	}

    red::DynArray< THandle<IMaterial> > MeshAppearance::Cook_GetLocalMaterials() const
    {
        red::DynArray< CName > materialNamesToEnsureLoaded{ red::PoolBackend() };
        helper::CollectAppearanceMaterialNamesToEnsureLoaded( m_chunkMaterials, materialNamesToEnsureLoaded );
        return m_meshOwner->Cook_GetLocalMaterials( materialNamesToEnsureLoaded );
    }

    const THandle< IMaterial >& MeshAppearance::GetMaterialDataByName( CName materialName ) const
	{
		// #tbd: assert was loaded first...
		auto* ptr = m_materialsCache.FindPtr(materialName);
		RED_FATAL_ASSERT(ptr, "Material '%hs' not found. Was GetLoadedRenderMaterialSetup() called and synchronized on first?", materialName.AsChar());
		return *ptr;
	}

	const job::Counter& MeshAppearance::GetWaitCounter() const
	{
		return m_waitCounter;
	}

	Bool MeshAppearance::ShouldPinRenderMaterialSetup() const
	{
		RED_FATAL_ASSERT(m_meshOwner);
		return m_meshOwner->IsRenderMaterialSetupForAllAppearancesPreloaded();
	}

	void MeshAppearance::ReleaseRenderMaterialSetup()
	{
		// Fast path if pinned, don't take exclusive lock
		// In the editor, this could lead to "leaking" m_renderMaterialSetupRefCount > 0
		// if pinned after loading renderMaterialSetup. But doesn't matter, since the resource should be reloaded
		// eventually.
		{
#ifndef NO_EDITOR
			// Editor could have pinned it meanwhile, shared lock to ensure some consistency if the appearance is being modified
			RED_SCOPE_SHARED_LOCK(m_loadRenderMaterialSetupLock);
#endif
			if (m_isRenderMaterialSetupPinned)
			{
				return;
			}
		}

		RED_SCOPE_LOCK(m_loadRenderMaterialSetupLock);
		RED_FATAL_ASSERT(m_renderMaterialSetupRefCount > 0);
		m_renderMaterialSetupRefCount -= 1;

		if (m_renderMaterialSetupRefCount == 0 && !m_isRenderMaterialSetupPinned)
		{
			m_renderMaterialSetup = nullptr;
			m_materialsCache.Clear();
		}
	}

	void MeshAppearance::ForceReloadRenderMaterialSetup(Bool preloadResources, Bool flushWaitCounter)
	{
		// Race mitigation if loading while editing. Must flush outside of lock or else can deadlock, since the job locks to update renderMaterialSetup
		if (flushWaitCounter)
		{
			job::FlushCounter(m_waitCounter);
		}

		RED_SCOPE_LOCK(m_loadRenderMaterialSetupLock);

		RED_FATAL_ASSERT(m_waitCounter.Internal_IsZeroSnapshot());

		m_renderMaterialSetup = nullptr;
		m_materialsCache.Clear();

		if (ShouldPinRenderMaterialSetup())
		{
			m_isRenderMaterialSetupPinned.Set();
		}

		red::DynArray< CName > materialNamesToEnsureLoaded{ rend::PoolMesh() }; // should be job/frame memory!
		helper::CollectAppearanceMaterialNamesToEnsureLoaded(m_chunkMaterials, materialNamesToEnsureLoaded);

		auto asyncResult = m_meshOwner->IssueLoadMaterials(materialNamesToEnsureLoaded, io::eAsyncPriority_Normal);
		RED_FATAL_ASSERT(asyncResult.first.Internal_IsZeroSnapshot(), "All materials should have been preloaded!");

		auto& loadedMaterials = asyncResult.second->arr;
		RED_FATAL_ASSERT(materialNamesToEnsureLoaded.Size() == loadedMaterials.Size());
		m_materialsCache.Reserve(materialNamesToEnsureLoaded.Size());
		for (Uint32 i : materialNamesToEnsureLoaded.Indices())
		{
			RED_FATAL_ASSERT(loadedMaterials[i], "Should have been assigned the fallback material");
			m_materialsCache.InsertUnsorted(materialNamesToEnsureLoaded[i], std::move(loadedMaterials[i]));
		}
		m_materialsCache.MakeClean();

		FinishRecreateRenderMaterialSetup_NoLock(preloadResources);
	}

		// NOTE: do NOT add any IsRenderMaterialSetupValid() function.
		// It'd be a race condition to check then retrieve because the AppearanceMaterialsRef is what controls its lifetime.
		const TRenderPtr<IRenderResource>& MeshAppearance::GetRenderMaterialSetup() const
		{
#if defined( VANGUARD_ASSET_HOST )
			return m_renderMaterialSetup;
#else
			{
			RED_SCOPE_SHARED_LOCK(m_loadRenderMaterialSetupLock);

			// #tbd: can't really assert on this, because some caller might have started proper loading
			// better to just log a data error if no renderMaterialSetup yet and return the fallback material.
			//RED_FATAL_ASSERT(m_waitCounter.Internal_IsZeroSnapshot(), "Did not properly sync ");

			if (m_renderMaterialSetup)
			{
				return m_renderMaterialSetup;
			}

			RED_FATAL_ASSERT(m_meshOwner);

			// #tbd: hard to pinpoint if programmer or asset error at this point
			// But make it a data error so somebody will hopefully at least see it
			RED_DATA_ERROR_BEGIN(red::LoggerCategory_Default, false,
				"RenderMaterialSetup used by mesh apperance was not ready\r\n"
				"NOTE: this could also be a programmer error\r\nOnly as a last resort, consider setting 'Force Load All Appearances' in the mesh\r\n"
				"Mesh file '%hs'\r\nAppearance '%hs'\r\n",
				m_meshOwner->GetPath().ToDebugString(),
				m_name.AsChar());
			RED_DATA_ERROR_ACTION_SHOW_ASSET(m_meshOwner->GetPath().ToDebugString());
			RED_DATA_ERROR_END();
		}

		// Alternately, we could create the fallback for this appearance.
		// However, we should try to return an error material only for what used the appearance incorrectly, and not break it for everything
			// which may make the cause harder to diagnose.
			return StaticGetFallbackMaterialForAppearanceError();
#endif
		}

#if !defined( VANGUARD_ASSET_HOST )
		static red::RWSpinLock s_fallbackRenderMaterialSetupLock;
		static TRenderPtr<IRenderResource> s_fallbackRenderMaterialSetup;

	static void EnsureExistsFallbackRenderMaterialSetup()
	{
		// Don't check if some other thread beat us creating renderMaterialSetup between R/X lock
		// We failed to get the renderMaterialSetup, so try not to hide it if something else saved us at the last microsecond
		{
			RED_SCOPE_SHARED_LOCK(s_fallbackRenderMaterialSetupLock);
			if (s_fallbackRenderMaterialSetup)
			{
				return;
			}
		}

		{
			RED_SCOPE_LOCK(s_fallbackRenderMaterialSetupLock);
			if (s_fallbackRenderMaterialSetup)
			{
				return;
			}

			if (GRender)
			{
				const THandle<IMaterial>& fallbackMaterial = CMesh::StaticGetFallbackMaterialForAppearanceError();
				RED_FATAL_ASSERT(fallbackMaterial->GetMaterialDefinition());
				SMaterialSetupEntry setupEntryParam;
				setupEntryParam.m_materialResource = fallbackMaterial->GetMaterialDefinition()->GetRenderResource();

				// The renderer side clamps chunk indices to use the first entry, so add a dummy entry that's just the fallback material
				red::Map< CName, SMaterialSetupEntry > materialSetupParams{ rend::PoolMesh() };
				materialSetupParams.Insert(CName::NONE(), setupEntryParam);
				red::DynArray< CName > chunkMaterialNames{ rend::PoolMesh() };
				chunkMaterialNames.PushBack(CName::NONE());
				s_fallbackRenderMaterialSetup = GRender->UploadMaterialSetup(materialSetupParams, chunkMaterialNames);
				RED_FATAL_ASSERT(s_fallbackRenderMaterialSetup);
			}
		}
	}

	// Unexpected error path
	RED_NOINLINE const TRenderPtr<IRenderResource>& MeshAppearance::StaticGetFallbackMaterialForAppearanceError()
	{
		// If the editor, will explode on local material asserts or other random issues. Shouldn't happen anyway.
		RED_FATAL_ASSERT(IsGameMode(), "Backend should have had all mesh appearances pinned. Failed to create renderMaterialSetup?");
			EnsureExistsFallbackRenderMaterialSetup();
			return s_fallbackRenderMaterialSetup;
		}
#else
		RED_NOINLINE const TRenderPtr<IRenderResource>& MeshAppearance::StaticGetFallbackMaterialForAppearanceError()
		{
			static TRenderPtr<IRenderResource> s_nullRenderMaterialSetup;
			return s_nullRenderMaterialSetup;
		}
#endif

		tools::EditorObjectID MeshAppearance::GetEditorObjectID() const
	{
		return tools::EditorObjectID::Build( GetID().Get(), GetClass()->GetName(), GetName() );
	}

		void MeshAppearance::FinishRecreateRenderMaterialSetup_NoLock( Bool preloadResources )
		{
#if defined( VANGUARD_ASSET_HOST )
			RED_UNUSED( preloadResources );
#else
			if (GRender)
			{
			CMaterialSetupParamsProvider materialSetupParamsProvider(*this, preloadResources);
			m_renderMaterialSetup = GRender->UploadMaterialSetup(materialSetupParamsProvider.GetMaterialSetupEntries(),
				materialSetupParamsProvider.GetChunkMaterialNames());
				RED_FATAL_ASSERT(m_renderMaterialSetup);
			}
#endif
		}

	void MeshAppearance::OnPropertyPostChange(const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& value)
	{
		ISerializable::OnPropertyPostChange( path, oldValue, value );

		// When we add new material to a chunk, it will be NONE initially. We need to correct that, we want to always have valid materials.
		/*
		for ( ChunkMaterials& chunk : m_chunkMaterials )
		{
			for ( Uint32 materialIndex = 0; materialIndex < chunk.m_materialNames.Size(); ++materialIndex )
			{
				if ( chunk.m_materialNames[materialIndex].Empty() )
				{
					RED_ASSERT( materialIndex > 0, "MeshAppearance error, we have NONE material in chunk at index 0, this should never happen" );
					chunk.m_materialNames[materialIndex] = chunk.m_materialNames[materialIndex - 1u];    // Just copy previous material.
				}
			}
		}
		*/

		// Fine to call now, since all materials should still be loaded since we only copied the previous material
		const Bool flushWaitCounter = true;
		ForceReloadRenderMaterialSetup(false, flushWaitCounter);
	}

	Bool MeshAppearance::OnPropertyTypeMismatch(CName propertyName, const rtti::Property* existingProperty, const rtti::Variant& readValue)
	{
		if ( propertyName == RED_NAME_CONSTEXPR( "chunkMaterials" ) )
		{
			auto chunkMaterials = readValue.Get< red::DynArray< ChunkMaterials > >();
			m_chunkMaterials.Reserve( chunkMaterials.Size() );
			for ( Uint32 i = 0; i < chunkMaterials.Size(); ++i )
			{
				m_chunkMaterials.PushBack( chunkMaterials[ i ].m_materialNames[ 0 ] );
			}
			return true;
		}
		return ISerializable::OnPropertyTypeMismatch( propertyName, existingProperty, readValue );
	}
}
