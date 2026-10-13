/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "mesh.h"
#include "meshChunk.h"
#include "meshParameter.h"
#include "meshAppearance.h"
#include "meshDataViewRO.h"
#if !defined( VANGUARD_ASSET_HOST )
#include "meshParamPhysics.h"
#endif

#include "../../resourceMesh/include/meshUtils.h"

#include "../../resourceMaterial/include/material.h"
#include "../../resourceMaterial/include/materialInstance.h"

#include "../../redReflection/include/variant.h"
#include "../../redReflection/include/gatheredResource.h"
#include "../../redReflection/include/resourceMetricsBank.h"

#include "../../renderData/include/renderResource.h"
#include "../../renderData/include/renderResourceBlob.h"
#include "../../renderData/include/materialStructures.h"
#if !defined( VANGUARD_ASSET_HOST )
#include "../../renderData/include/renderer.h"
#endif

#include "../../renderData/include/gpuWrapApiVertexFormats.h"
#include "../../redReflection/include/resourceLoader.h"
#include "../../redReflection/include/postLoadContext.h"
#include "../../redJobs2/include/jobBuilder.h"
#include "../../redJobs2/include/jobCounterOwner.h"
#include "../../redReflection/include/serializationLoader.h"
#include "../../redCore/include/globalModeInfo.h"
#include "../../redCore/include/commandline.h"
#include "../../redReflection/include/rttiPathParser.h"
#include "../../redReflection/include/rttiEnum.h"
#include "../../redReflection/include/rttiSystem.h"
#if !defined( VANGUARD_ASSET_HOST )
#include "../../renderData/include/renderSettingsVars.h"
#endif

using red::DynArray;
using red::String;
using mesh::MeshAppearance;

namespace Version
{
	enum Enum
	{
		BASE,
		RENDER_OBJECT_TYPE_ADDED,							// Set ROP_Skinned for everything that have skinning information
		RENDER_IN_CASCADES_ADDED,							// Set MCR_Cascades on every mesh chunk rendermask
		MESH_CHUNK_MASK_CLEANUP,							// Change Uint16 mask to EMeshChunkRenderMask for all chunks

		NEWEST = MESH_CHUNK_MASK_CLEANUP
	};
}

RTTI_BEGIN_TYPE( CMesh );
{
	RTTI_PARENT_TYPE( res::StreamedResource );

#ifndef RED_CONFIGURATION_FINAL
	RTTI_PROPERTY( m_resourceVersion );
#endif // RED_CONFIGURATION_FINAL

	RTTI_PROPERTY_CATEGORY( "Mesh" );
	RTTI_PROPERTY( m_parameters );
	RTTI_PROPERTY( m_boundingBox ).editable();
	RTTI_PROPERTY(m_surfaceAreaPerAxis).readOnly();
#ifndef NO_EDITOR	
	RTTI_PROPERTY( m_navigationSetting ).editable().notCooked();
	RTTI_PROPERTY( m_trafficSetting ).editable().notCooked();
#endif

#ifndef RED_CONFIGURATION_FINAL
	RTTI_PROPERTY( m_consoleBias ).editable().tooltip( "Remove LOD on console during cook. 1 for dropping lod0, 2 for dropping LOD0 and 1. etc." );
#endif // RED_CONFIGURATION_FINAL

	RTTI_PROPERTY_CATEGORY( "Materials" );
	RTTI_PROPERTY( m_materialEntries );
	RTTI_PROPERTY( m_externalMaterials );
	RTTI_PROPERTY( m_localMaterialInstances ); // DEPRECATED, REMOVE AFTER RESAVE
	RTTI_PROPERTY( m_localMaterialBuffer );
	RTTI_PROPERTY( m_preloadExternalMaterials ).cookedOnly();
	RTTI_PROPERTY( m_preloadLocalMaterialInstances ).cookedOnly();
	RTTI_PROPERTY( m_inplaceResources ).cookedOnly();
	RTTI_PROPERTY( m_appearances );
	RTTI_PROPERTY( m_objectType ).editable();

	RTTI_PROPERTY_CATEGORY( "Rendering" );
#ifndef RED_CONFIGURATION_FINAL
	RTTI_PROPERTY( m_chunks ).notCooked();
#endif // RED_CONFIGURATION_FINAL
	RTTI_PROPERTY( m_renderResourceBlob );
	RTTI_PROPERTY( m_lodLevelInfo );

	RTTI_PROPERTY_CATEGORY( "Skinning" );
	RTTI_PROPERTY( m_floatTrackNames ).readOnly();
	RTTI_PROPERTY( m_boneNames );
	RTTI_PROPERTY( m_boneRigMatrices );
	RTTI_PROPERTY( m_boneVertexEpsilons );
	RTTI_PROPERTY( m_lodBoneMask );
#ifndef RED_CONFIGURATION_FINAL
	RTTI_PROPERTY( m_saveDateTime );							/// @todo DP: a HACK for E3 demo, set this back to .notCooked(); when garment up-to-date issue is fully resolved
#endif // RED_CONFIGURATION_FINAL

	RTTI_PROPERTY( m_constrainAutoHideDistanceToTerrainHeightMap ).editable();
	RTTI_PROPERTY( m_forceLoadAllAppearances ).editable().tooltip( "Force load all appearances in this mesh.\nConsider switching entity appearance instead of switching component appearances.\nOnly use as a last resort." );
	RTTI_PROPERTY( m_castGlobalShadowsCachedInCook );
	RTTI_PROPERTY( m_castLocalShadowsCachedInCook );

	RTTI_PROPERTY_CATEGORY( "Resource" );
#ifndef RED_CONFIGURATION_FINAL
	RTTI_PROPERTY( m_geometryHash ).readOnly();
#endif // RED_CONFIGURATION_FINAL

#ifndef RED_CONFIGURATION_FINAL
		RTTI_PROPERTY_CATEGORY( "GI" );
		RTTI_PROPERTY( m_isGIVisible ).editable().notCooked();
		RTTI_PROPERTY( m_isGITransparent ).editable().notCooked();
		RTTI_PROPERTY( m_generatedLOD ).readOnly().notCooked();
#if !defined( VANGUARD_ASSET_HOST )
	    RTTI_PROPERTY_CATEGORY( "WORLD TAG" );
		if ( ResolveRttiType< world::ObjectTagExt >() && ResolveRttiType< world::ObjectTag >() )
		{
			RTTI_PROPERTY( m_tagExt ).editable().notCooked();
			RTTI_PROPERTY( m_tag ).editable().notCooked();
		}
#endif
	#endif //  RED_CONFIGURATION_FINAL
}
RTTI_END_TYPE();


RTTI_BEGIN_TYPE_IN_NAMESPACE( MaterialEntry, CMesh );     // A hack, not actually inside a namespace, but inside a CMesh class.
	RTTI_PROPERTY( m_name );
	RTTI_PROPERTY( m_index );
	RTTI_PROPERTY( m_isLocalInstance );
RTTI_END_TYPE();

namespace hacks
{
	static Bool IsEditorForceLoadAllMeshAppearances()
	{
		// Temp P0 "get out of jail free card" until meshes sorted out
		const Bool forceLoadAllMeshAppearances = red::CommandLine::Get().HasOption("forceLoadAllMeshAppearances");

#ifndef NO_EDITOR
		return forceLoadAllMeshAppearances || !IsGameMode();
#else
		return forceLoadAllMeshAppearances || false;
#endif
	}
}

namespace
{
	res::TStaticResource< CMesh > s_fallbackMesh( RED_CONST_RESOURCEPATH( "engine\\fallback\\helper_no_mesh.w2mesh" ), GRF_Fallback | GRF_SkipOnHeadless | GRF_Startup );
	res::TStaticResource< IMaterial > s_fallbackMaterial( RED_CONST_RESOURCEPATH( "engine\\materials\\fallback.remt" ), GRF_Fallback | GRF_SkipOnHeadless | GRF_Startup );

	Uint32 GetFatVertexElementOffset(EMeshStreamType streamType)
	{
		switch (streamType)
		{
		case MST_Position_3F: return offsetof(SMeshVertex, m_position); break;
		case MST_SkinningIndices_4U8: return offsetof(SMeshVertex, m_indices); break;
		case MST_SkinningWeights_4F: return offsetof(SMeshVertex, m_weights); break;
		case MST_SkinningIndicesExt_4U8: return offsetof(SMeshVertex, m_indicesExt); break;
		case MST_SkinningWeightsExt_4F: return offsetof(SMeshVertex, m_weightsExt); break;
		case MST_Color_U32: return offsetof(SMeshVertex, m_color); break;
		case MST_TexCoord0_2F: return offsetof(SMeshVertex, m_uv0); break;
		case MST_TexCoord1_2F: return offsetof(SMeshVertex, m_uv1); break;
		case MST_Normal_3F: return offsetof(SMeshVertex, m_normal); break;
		case MST_Tangent_3F: return offsetof(SMeshVertex, m_tangent); break;
		case MST_Binormal_3F: return offsetof(SMeshVertex, m_binormal); break;
		case MST_DestructionIndices_2U16: return offsetof(SMeshVertex, m_destruction); break;
		case MST_Multilayer_1F: return offsetof(SMeshVertex, m_multilayer); break;
		case MST_VehicleDmgNormalFront_3F: return offsetof(SMeshVertex, m_vehicleDamageNormal); break;
		case MST_VehicleDmgNormalSides_3F: return offsetof(SMeshVertex, m_vehicleDmgNormalSides); break; // obsolete
		case MST_VehicleDmgPosFront_3F: return offsetof(SMeshVertex, m_vehicleDamagePosition); break;
		case MST_VehicleDmgPosSides_3F: return offsetof(SMeshVertex, m_vehicleDmgPosSides); break; // obsolete
		case MST_LightBlockerIntensity_1F: return offsetof(SMeshVertex, m_lightBlockerData); break;
		default: RED_FATAL("No such stream"); break;
		}

		return 0;
	}

	Bool IsSkinnedMesh( const CMesh* mesh )
	{
		mesh::DataViewRO meshData( *mesh );

		Bool isSkinnedMesh = false;
		for ( Uint32 lodIndex = 0; !isSkinnedMesh && lodIndex < meshData.GetNumLODs(); lodIndex++ )
		{
			red::DynArray< Uint16 > lodChunkIndices = meshData.GetLODChunkIndices( lodIndex );

			for ( Uint32 j = 0; j < lodChunkIndices.Size(); j++ )
			{
				Uint16 chunkIndex = lodChunkIndices[j];

				EMeshVertexType chunkVertexType = meshData.GetChunkVertexType( chunkIndex );
				if ( IsSkinnedMeshVertexType( chunkVertexType ) )
				{
					isSkinnedMesh = true;
					break;
				}
			}
		}

		return isSkinnedMesh;
	}
}

// Used to avoid returning THandle< MeshAppearance > by value, and using const& instead.
const THandle< mesh::MeshAppearance > s_nullAppearancePtr(nullptr);

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

THandle<IMaterial> CMesh::MaterialEntry::GetMaterial() const
{
	auto material = m_material.ToHandle();
	if (material)
	{
		return material;
	}

	return s_fallbackMaterial.Get();
}

THandle<IMaterial> CMesh::MaterialEntry::GetMaterialNoFallbackIfNull() const
{
	// NOTE: if set a material in the editor, then the weakhandle should not have expired because it's stored in pinnedMaterial (vs in some random MeshAppearance which could clear it).
	// The name here should probably better indicate this
	return m_material.ToHandle();
}

void CMesh::MaterialEntry::SetMaterial(const THandle<IMaterial>& material)
{
	m_pinnedMaterial = material;
	m_material = m_pinnedMaterial;
}

void CMesh::MaterialEntry::SetLoadedMaterial(const THandle<IMaterial>& material)
{
	// If set from editor, don't override weakptr from any pending load
	if (!m_pinnedMaterial)
	{
		m_material = material;
	}
}

CMesh::CMesh()
	: m_boundingBox( Box::EMPTY() )
	, m_surfaceAreaPerAxis( -1.0f, -1.0f, -1.0f )
	, m_renderResource( nullptr )
#ifndef RED_CONFIGURATION_FINAL
	, m_renderResourceBlobSize( 0 )
#endif // RED_CONFIGURATION_FINAL
	, m_lodLevelInfo( rend::PoolMesh() )
	, m_boneNames( rend::PoolMesh() )
	, m_boneRigMatrices( rend::PoolMesh() )
	, m_boneVertexEpsilons( rend::PoolMesh() )
#ifndef RED_CONFIGURATION_FINAL
	, m_geometryHash( 0 )
#endif // RED_CONFIGURATION_FINAL
	, m_objectType( ERenderObjectType::ROT_Static )
#ifndef RED_CONFIGURATION_FINAL
	, m_resourceVersion( Version::BASE )
	, m_consoleBias( 0 )
#endif // RED_CONFIGURATION_FINAL
	, m_constrainAutoHideDistanceToTerrainHeightMap( false )
	, m_forceLoadAllAppearances( false )
	, m_castGlobalShadowsCachedInCook( false )
	, m_castLocalShadowsCachedInCook( false )
{}

CMesh::~CMesh()
{
#ifdef USE_RESOURCE_METRICS_BANK
	if ( m_renderResource )
	{
		if ( GResourceLoader && GetPath().IsValid() )
		{
			auto* resourceMetricsBank = GResourceLoader->GetResourceMetricsBank();
			resourceMetricsBank->OnUnloadMesh( GetPath() );
		}
#endif
	}
}

Bool CMesh::OnPropertyPreChange( const rtti::AccessPath& path, red::SharedPtr< const rtti::ValueHolder >& inOutValue )
{
	Bool valid = true;
	TBaseClass::OnPropertyPreChange( path, inOutValue );
	CName propertyName;
	rtti::PathParser parser( path );
	parser.EatName( propertyName );

#if !defined( VANGUARD_ASSET_HOST )
		if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "tagExt" ) )
		{
		valid = false;
		const String valueStr = inOutValue->GetSingleValue();
		
		if ( const rtti::EnumType* enumType = GetRttiSystem().FindEnum( RED_NAME_CONSTEXPR_NOREG( "worldObjectTagExt" ) ) )
		{
			Int64 value;
			if ( enumType->FindValue( valueStr, value ) )
			{
				world::ObjectTagExt valueCasted = static_cast<world::ObjectTagExt>( value );

				if ( valueCasted != world::ObjectTagExt::Defa )
				{
					valid = true;
				}
			}
			}
		}
		else
#endif
		if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "consoleBias" ) )
	{
		const String valueStr = inOutValue->GetSingleValue();
		Int32 biasValue = 0;
		if ( !FromString( valueStr, biasValue ) )
		{
			return false;
		}

		if ( biasValue >= (Int32)m_lodLevelInfo.Size() )
		{
			biasValue = Max( 0, (Int32)m_lodLevelInfo.Size() - 1 );

			String newValueStr;
			if ( !ToString( newValueStr, biasValue ) )
			{
				return false;
			}

			if ( !newValueStr.EqualsNC( valueStr ) )
			{
				inOutValue = rtti::ValueHolder::CreateSingle( newValueStr.AsChar() );
			}
		}
		return true;
	}
	return valid;
}

void CMesh::OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& value )
{
	// Pass to base class
	TBaseClass::OnPropertyPostChange( path, oldValue, value );

	// Check the property
	const auto stringPath = path.ToString();
 	if (stringPath.BeginsWith("autoHideDistanceBoost"))
 	{
		NotifyPropertyChanged(rtti::AccessPath("autoHideDistance"));
		RecreateInternalResources(MeshInternalData_All, false);
 	}
	else if (stringPath.BeginsWith("autoHideDistance"))
	{
		RecreateInternalResources( MeshInternalData_All, false );		
	}
}

Bool CMesh::OnPropertyTypeMismatch(CName propertyName, const rtti::Property* existingProperty, const rtti::Variant& readValue)
{
	if (propertyName == RED_NAME_CONSTEXPR_NOREG("externalMaterials"))
	{
		red::DynArray< TResRef< IMaterial > > oldValue{ rend::PoolMesh() };
		if (!readValue.Get(oldValue))
		{
			return false;
		}

		m_externalMaterials.Resize(oldValue.Size());

		for (Uint32 i : oldValue.Indices())
		{
			RED_FATAL_ASSERT(oldValue[i].GetPath().IsValid());
			m_externalMaterials[i].SetPath(oldValue[i].GetPath());
		}

		return true;
	}

	return TBaseClass::OnPropertyTypeMismatch(propertyName, existingProperty, readValue);
}

Uint32 CMesh::GetRenderResourceSize() const
{
#ifdef RED_CONFIGURATION_FINAL
    Uint32 result = 0;
#else
	Uint32 result = m_renderResourceBlobSize;
#endif // RED_CONFIGURATION_FINAL
    if( m_renderResourceBlob )
    {
        if( const Uint32 sizeFromBlob = m_renderResourceBlob->GetDataSize() )
        {
            result = sizeFromBlob;
        }
    }
    return result;
	//return m_renderResourceBlob ? m_renderResourceBlob->GetDataSize() : m_renderResourceBlobSize;
}

Uint32 CMesh::GetGPUSize() const
{
	return GetRenderResourceSize();
}

void CMesh::GetCustomEditableProperties( rtti::EditableProperties& outRootProperties ) const
{
	TBaseClass::GetCustomEditableProperties( outRootProperties );

	rtti::ClassEditablePropertyInfo info;

	info.m_category = RED_NAME_CONSTEXPR( "Mesh" );
	info.m_name = RED_NAME_CONSTEXPR( "autoHideDistance" );
	info.m_type = GetTypeObject< Float >();
	info.m_isReadOnly = true;
	outRootProperties.Add( info );

	info.m_category = RED_NAME_CONSTEXPR( "Resource" );
	info.m_name = RED_NAME_CONSTEXPR( "saveDateTime" );
	info.m_type = GetTypeObject< String >();
	info.m_isReadOnly = true;
	outRootProperties.Add( info );

	info.m_category = RED_NAME_CONSTEXPR( "Resource" );
	info.m_name = RED_NAME_CONSTEXPR( "resourceGeometryHash" );
	info.m_type = GetTypeObject< Uint64 >();
	info.m_isReadOnly = true;
	outRootProperties.Add( info );
}

Bool CMesh::ReadCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, red::SharedPtr< rtti::ValueHolder >& outValue ) const
{
	if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "autoHideDistance" ) )
	{
		const Float autoHideDistance = GetAutoHideDistance();
		return GetTypeObject< Float >()->ReadValue( ctx, &autoHideDistance, restOfThePath, outValue );
	}
#ifndef RED_CONFIGURATION_FINAL
	else if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "saveDateTime" ) )
	{
		String saveDateTimeStr;
		if ( GetSaveDateTime().IsValid() )
			ToString( saveDateTimeStr, GetSaveDateTime() );
		else
			saveDateTimeStr = "Unavailable";
		return GetTypeObject< String >()->ReadValue( ctx, &saveDateTimeStr, restOfThePath, outValue );
	}
	else if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "geometryHash" ) )
	{
		Uint64 geometryHash = GetGeometryHash();
		return GetTypeObject< Uint64 >()->ReadValue( ctx, &geometryHash, restOfThePath, outValue );
	}
#endif // RED_CONFIGURATION_FINAL

	return TBaseClass::ReadCustomEditableProperty( ctx, propertyName, restOfThePath, outValue );
}

void CMesh::OnSerialize(IFile& file)
{
	TBaseClass::OnSerialize(file);

	if (file.IsResourceCollector())
	{
		RED_SCOPE_LOCK(m_materialEntriesLock);
		for (const auto& entry : m_materialEntries)
		{
			if (entry.IsLocalInstance())
			{
				auto material = entry.GetMaterialNoFallbackIfNull();
				if (material)
				{
					file << material;
				}
			}
		}
	}
}

// #tbd: could just use preload properties even if not cooking
#ifndef RED_CONFIGURATION_FINAL
void CMesh::ResaveMaterials(Bool isCooking)
{
	RED_SCOPE_LOCK(m_materialEntriesLock);

	// Resave all local materials. Why? Because ChunksListInterop gets the CMaterialInstance from the mesh and then modifies its properties directly.
	// Not even going to guess what gets modified, so update all local materials.
	// #tbd: if could somehow magically modify external materials, which used to be resrefs. But one P0 at a time.
	//
	// Gather all the instanced local materials from the materialEntries according in the correct order according to their entry index.
	// This order must correspond to the buffer order, since this is how the materials are referenced.
	if (m_chunks.Empty())
		return;
	
	red::DynArray<THandle<CMaterialInstance>> localMaterials{ red::PoolBackend() };
	localMaterials.Resize(m_localMaterialBuffer.GetNumMaterials());
	for (const CMesh::MaterialEntry& entry : m_materialEntries)
	{
		if (entry.IsLocalInstance())
		{
			const Uint32 materialIndex = entry.GetIndex();
			RED_FATAL_ASSERT(materialIndex < localMaterials.Size(), "Corrupt mesh data? Local materialIndex %u >= %u", materialIndex, localMaterials.Size());
			localMaterials[materialIndex] = Cast<CMaterialInstance>(entry.GetMaterialNoFallbackIfNull());
			RED_FATAL_ASSERT(localMaterials[materialIndex], "Local material should have existed. CMaterialInstance not deserialized properly or failed to property initialize entries??");
		}
	}

	// Sanity check material entries matched buffer size
	for (Uint32 i : localMaterials.Indices())
	{
		RED_FATAL_ASSERT(localMaterials[i], "Corrupt mesh data? Local material at %u missing from materialEntries array", i);
		RED_FATAL_ASSERT(localMaterials[i]->GetParent() == nullptr || localMaterials[i]->GetParent() == this,
			"Unexpected local material parent of type %hs", localMaterials[i]->GetParent()->GetClass()->GetName().AsChar());
	}

	RED_FATAL_ASSERT(m_preloadExternalMaterials.Empty());
	RED_FATAL_ASSERT(m_preloadLocalMaterialInstances.Empty());

	const Bool preloadAllAppearances = m_appearances.Size() == 1 || m_forceLoadAllAppearances;
	if (isCooking && preloadAllAppearances)
	{
		for (auto& it : localMaterials)
		{
			// Clear out dummy ".res" resource path or will assert when serializing
			it->Internal_SetPath(res::ResourcePath());
			m_preloadLocalMaterialInstances.PushBack(it);
		}

		m_preloadExternalMaterials.Resize(m_externalMaterials.Size());
		for (const CMesh::MaterialEntry& entry : m_materialEntries)
		{
			if (entry.IsLocalInstance())
			{
				continue;
			}
			auto material = entry.GetMaterialNoFallbackIfNull();
			if (material)
			{
				auto path = material->GetPath();
				RED_FATAL_ASSERT(path.IsValid());
				m_preloadExternalMaterials[entry.GetIndex()] = path;
			}
		}

		m_externalMaterials.Clear();
		localMaterials.Clear();
	}

	// Fixme: N^2 insert under the hood, should do in bulk, but P0 at the moment so...
	m_localMaterialBuffer.Editor_Clear();
	for (Uint32 i : localMaterials.Indices())
	{
		m_localMaterialBuffer.Editor_InsertAt(i, localMaterials[i]);
	}
}
#endif // RED_CONFIGURATION_FINAL

void CMesh::CleanupMeshParameters( Bool isCooking )
{
	// remove parameters not required in cook
	if ( isCooking )
	{
		for ( TParameters::reverse_iterator it = m_parameters.RBegin(); it != m_parameters.REnd(); ++it)
		{
			THandle< mesh::MeshParameter >& parameter = *it;
			if ( parameter->ShouldBeStrippedWhenCooking() )
			{
				RED_FATAL_ASSERT( parameter->GetParent() == this, "Mesh parameter is not owned by this mesh" );

				MarkModified();

				parameter->SetParent( nullptr );

				if ( !m_parameters.Remove( parameter ).IsSuccessful() )
				{
					RED_LOG_WARNING( "Mesh parameter was not found" );
				}
			}
		}
	}
}

void CMesh::OnPreSave( const PreSaveContext& context )
{
#ifndef RED_CONFIGURATION_FINAL
	m_resourceVersion = Version::NEWEST;
#endif // RED_CONFIGURATION_FINAL

	TBaseClass::OnPreSave( context );

#ifndef RED_CONFIGURATION_FINAL
	ResaveMaterials(context.isCooking);
#endif // RED_CONFIGURATION_FINAL

	CleanupMeshParameters( context.isCooking );

	if( m_surfaceAreaPerAxis[0] < 0.0f )
	{
		ComputeSurfaceAreaPerAxis();
	}
}

THandle< IMaterial > CMesh::StaticGetFallbackMaterialForAppearanceError()
{
	return s_fallbackMaterial.Get();
}

void CMesh::TempDoMaterialInstanceConversion()
{
	// Now that deserialization is complete and all export DDBs and imports have been synchronized on, NOW and only NOW can we
	// fix the data.

	// Note: you could be tempted to make m_localMaterialInstances notSerialized() and get the OnPropertyMissing() call, but
	// the material objects are just placeholder memory in the array and haven't actually been deserialized yet.
	//
	// OnSerialize() seems like another choice, but isn't. Because we still haven't finished synchronizing on all subobjects' imports or DDBs yet.
	// #tbd: fixme N^2 insert, should really be bulk assign or RESAVE the asset
	for (Uint32 i = 0; i < m_localMaterialInstances.Size(); ++i)
	{
		auto material = m_localMaterialInstances[i];
		RED_FATAL_ASSERT(material);
		material->SetParent(nullptr); // make sure not pointing at the mesh as direct parent
		m_localMaterialBuffer.Editor_InsertAt(i, material);
	}
	m_localMaterialInstances.Clear();
}

void CMesh::OnPostLoad( const PostLoadContext& context )
{
	TBaseClass::OnPostLoad( context );

	// #todo: remove hackLoadingContext and possibly just add a "shouldThrottle" to the PostLoadContext. LoadingContext just being used to create a fake
	// import context that we *could* live without, but makes tracking loading easier to debug.

	RED_FATAL_ASSERT(context.hackLoadingContext);
	RED_FATAL_ASSERT(context.hackLoadingContext->m_postLoadFlags.skipUploadRenderData == context.flags.skipUploadRenderData, "PostLoad flags mismatch");

	if (GetCookingPlatform() == PLATFORM_None)
	{
		TempDoMaterialInstanceConversion();
	}

	SetAppearanceBackPointers();    // Setup back pointers to deserialized appearances (from appearance to this mesh).

	if (m_appearances.Empty())    // Make sure there is always at least one appearance; created with a back pointer 
		CreateDefaultAppearance();

	// Set THandle's to materials in m_materialEntries array, copy them form m_localMaterialBuffer & m_externalMaterials arrays.
	RED_FATAL_ASSERT(
		m_materialEntries.Size() == 
		m_externalMaterials.Size() + m_preloadExternalMaterials.Size() +
		m_localMaterialBuffer.GetNumMaterials() + m_preloadLocalMaterialInstances.Size(),

		"Corrupted mesh data: materialEntries=%u: external=%u, preloadExternal=%u, localBuffer=%u, preloadLocal=%u: %hs",
		m_materialEntries.Size(),
		m_externalMaterials.Size(),
		m_preloadExternalMaterials.Size(),
		m_localMaterialBuffer.GetNumMaterials(),
		m_preloadLocalMaterialInstances.Size(),
		GetPath().ToDebugString()
	);

	// #tbd:-1,-1,-1... why do some road meshes have no area...?
	if (GetCookingPlatform() == PLATFORM_None)
	{
		if ( m_surfaceAreaPerAxis[ 0 ] < 0.0f )
		{
			ComputeSurfaceAreaPerAxis();
		}
	}

	if (!context.flags.skipUploadRenderData)
	{
		// rebuild mesh blob, excluding appearances
		CreateRenderResource();
	}

	if (!TryPreloadFromCookedProperties_StrictlyPrivateNeverCallFromFriends(context.hackLoadingContext))
	{
#if 0
		if (GetCookingPlatform() != PLATFORM_None)
		{
			if (m_localMaterialBuffer.GetNumMaterials() > 0 || m_externalMaterials.Size() > 0)
			{
				RED_FATAL_ASSERT(!m_forceLoadAllAppearances);
				RED_FATAL_ASSERT(m_appearances.Size() > 1);
			}
		}
#endif

		// #tbd: if somebody calls OnPostLoad() manually but doesn't wait for this counter... should have some assert if accessing the mesh
		if (IsRenderMaterialSetupForAllAppearancesPreloaded())
		{
			const Bool flushAppearanceWaitCounter = false; // don't flush, appearances aren't even externally accessible yet and we're not guaranteed on the main thread anyway
			PreloadAppearances_StrictlyPrivateNeverCallFromFriends(flushAppearanceWaitCounter, context.hackLoadingContext);
		}
	}

	/// Skinning LOD. For compatibility reasons, add default LOD masks
	if( m_lodBoneMask.Empty() )
	{
        m_lodBoneMask .Reserve( m_boneNames.Size() );
        for( Uint32 ibone = 0; ibone < m_boneNames.Size(); ++ibone )
        {
            m_lodBoneMask.PushBack( 1 << 0 );
        }
	}

	UpgradeVersion();
}

Bool CMesh::TryPreloadFromCookedProperties_StrictlyPrivateNeverCallFromFriends(const serialization::LoadingContext* optLoadingContext)
{
	Bool isPreloadedFromCookedProperties = false;
	if (!m_preloadLocalMaterialInstances.Empty())
	{
		isPreloadedFromCookedProperties = true;
		SetLocalMaterialEntriesMaterialPointers(m_preloadLocalMaterialInstances);
	}

	if (!m_preloadExternalMaterials.Empty())
	{
		isPreloadedFromCookedProperties = true;
		red::DynArray<THandle<IMaterial>> externalMaterials{ job::PoolJobScope() };
		externalMaterials.Reserve(m_preloadExternalMaterials.Size());
		for (const auto& it : m_preloadExternalMaterials)
		{
			externalMaterials.PushBack(it.Get());
		}
		SetExternalMaterialEntriesMaterialPointers(externalMaterials);
	}

	Bool skipUploadRenderData = optLoadingContext ? optLoadingContext->m_postLoadFlags.skipUploadRenderData : false;
	if (isPreloadedFromCookedProperties && !skipUploadRenderData)
	{
		// #tbd: can do this in a job per appearance template by using LoadRenderMaterialSetup() and a depBuilder
		// But there's no throttling here anymore
		for (auto& appearance : m_appearances)
		{
			appearance->ForceReloadRenderMaterialSetup(false, false /*flushAppearanceWaitCounter*/);
		}
	}

	return isPreloadedFromCookedProperties;
}

#ifndef NO_EDITOR
	TrafficGen::DynamicTrafficSetting CMesh::GetDynamicTrafficSetting() const
	{
		switch (m_trafficSetting.m_meshImpact)
		{
		case TrafficGen::MeshImpact::UseNavigation:
		{
			switch (m_navigationSetting.m_navmeshImpact)
			{
				case NavGen::NavmeshImpact::Blocking:
					return TrafficGen::DynamicTrafficSetting{ TrafficGen::DynamicImpact::Blocking };
				default:
					return TrafficGen::DynamicTrafficSetting{ TrafficGen::DynamicImpact::Ignored };
			};
		}
		break;
		case TrafficGen::MeshImpact::ForceIgnored:
		{
			return TrafficGen::DynamicTrafficSetting{ TrafficGen::DynamicImpact::Ignored };
		}
		break;
		case TrafficGen::MeshImpact::ForceBlocking:
		{
			return TrafficGen::DynamicTrafficSetting{ TrafficGen::DynamicImpact::Blocking };
		}
		break;
		default:
			RED_ASSERT(false);
		};

		return TrafficGen::DynamicTrafficSetting{ TrafficGen::DynamicImpact::Ignored };
	}
#endif

void CMesh::SetExternalMaterialEntriesMaterialPointers(const red::DynArray<THandle<IMaterial>>& externalMaterials)
{
	RED_FATAL_ASSERT(externalMaterials.Size() == m_externalMaterials.Size() + m_preloadExternalMaterials.Size());

	for (MaterialEntry& materialEntry : m_materialEntries)
	{
		if (!materialEntry.IsLocalInstance())
		{
			RED_FATAL_ASSERT(materialEntry.GetIndex() < externalMaterials.Size(), "Corrupted mesh data");

			const auto& preloadedMaterial = externalMaterials[materialEntry.GetIndex()];

			if (preloadedMaterial) // Test for missing external material, use some fallback when missing.
			{
				materialEntry.SetMaterial( externalMaterials[materialEntry.GetIndex()] );
			}
			else
			{
				if (materialEntry.GetIndex() < m_externalMaterials.Size())
				{
					RED_DATA_ERROR_BEGIN(red::LoggerCategory_Default, false, "Material file used by mesh was not found\r\n"
						"Mesh file '%hs'\r\nMissing material path '%hs'\r\nLocal name of the material inside mesh '%hs'",
						GetPath().ToDebugString(), m_externalMaterials[materialEntry.GetIndex()].GetPath().ToDebugString(), materialEntry.GetName().AsChar());
					RED_DATA_ERROR_ACTION_SHOW_ASSET(GetPath().ToDebugString());
					RED_DATA_ERROR_END();
				}

				materialEntry.SetMaterial( s_fallbackMaterial.Get() );
			}
		}
	}
}

void CMesh::SetLocalMaterialEntriesMaterialPointers(const red::DynArray<THandle<IMaterial>>& localMaterials)
{
	RED_FATAL_ASSERT(localMaterials.Size() == m_localMaterialBuffer.GetNumMaterials() + m_preloadLocalMaterialInstances.Size());

	for ( MaterialEntry& materialEntry : m_materialEntries )
	{
		if ( materialEntry.IsLocalInstance() )
		{
			RED_FATAL_ASSERT( materialEntry.GetIndex() < localMaterials.Size(), "Corrupted mesh data" );
			
			materialEntry.SetMaterial(localMaterials[materialEntry.GetIndex()] );
		}		
	}
}

void CMesh::InsertMaterialToInternalArray( red::DynArray< TResAsyncRef<IMaterial> >& externalMaterials,
										   mesh::MeshMaterialBuffer& localMeshMaterialBuffer,
                                           CMesh::MaterialEntry& materialEntry )
{
	const THandle<IMaterial>& material = materialEntry.GetMaterial();
	if ( materialEntry.IsLocalInstance() )
	{
		RED_FATAL_ASSERT( material->IsA<CMaterialInstance >(), "Mesh material '%hs' is marked as local instance, "
		                  "but it is not of CMaterialInstance class.\r\nThe actual class of this material is: '%hs'",
		                  materialEntry.GetName().AsChar(), material->GetClass()->GetName().AsChar() );

		THandle< CMaterialInstance > materialInstance = Cast<CMaterialInstance>( material );
		const Uint32 newIndex = localMeshMaterialBuffer.GetNumMaterials();
		materialEntry.SetIndex(static_cast<Uint16>(newIndex));
		localMeshMaterialBuffer.Editor_InsertAt( newIndex, materialInstance );
	}
	else
	{
		RED_FATAL_ASSERT( material->HasValidPath(), "Mesh material '%hs' is marked as external material, "
		                  "but it does not have a have resource path.", materialEntry.GetName().AsChar() );

		RED_FATAL_ASSERT(material->GetPath().IsValid());
		TResAsyncRef<IMaterial> materialRef( material->GetPath() );
		materialEntry.SetIndex( static_cast<Uint16>(externalMaterials.Size()) );
		externalMaterials.PushBack( materialRef );
	}
}

void CMesh::UpgradeVersion()
{
#ifndef RED_CONFIGURATION_FINAL
	if ( m_resourceVersion < Version::RENDER_OBJECT_TYPE_ADDED )
	{
		if ( IsSkinnedMesh( this ) )
			m_objectType = ERenderObjectType::ROT_Skinned;
	}

	if ( m_resourceVersion < Version::RENDER_IN_CASCADES_ADDED )
	{
		for ( auto& chunk : m_chunks )
		{
			*(Uint16*)&chunk.m_renderMask |= (Uint16)MCR_Cascades;		// Old files by default will have shadows enabled (heuristic and local shadow node toggle will take care of the rest)
		}
	}

	if ( m_resourceVersion < Version::MESH_CHUNK_MASK_CLEANUP )
	{
		for ( auto& chunk : m_chunks )
		{
			// To properly map old values to new values (EMeshChunkRenderMask_Deprecated -> EMeshChunkRenderMask) we need to cast it to the deprecated one first.
			const EMeshChunkRenderMask asRenderMaskDeprecated = EMeshChunkRenderMask( chunk.m_renderMask );
			EMeshChunkFlags outNewMask = EMeshChunkFlags( 0 );

			// Now, perform a remap
			if ( ( asRenderMaskDeprecated & MCR_Scene ) != 0 )									*(Uint16*)&outNewMask |= EMeshChunkFlags::MCF_RenderInScene;
			if ( ( asRenderMaskDeprecated & ( MCR_Cascades | MCR_LocalShadows | MCR_Cascade1 | MCR_Cascade2 | MCR_Cascade3 | MCR_Cascade4 | MCR_DistantShadows ) ) != 0 )
				*(Uint16*)&outNewMask |= EMeshChunkFlags::MCF_RenderInShadows;
			if ( ( asRenderMaskDeprecated & MCR_IsTwoSided ) != 0 )								*(Uint16*)&outNewMask |= EMeshChunkFlags::MCF_IsTwoSided;
			if ( ( asRenderMaskDeprecated & MCR_IsRayTracedEmissive ) != 0 )					*(Uint16*)&outNewMask |= EMeshChunkFlags::MCF_IsRayTracedEmissive;
			if ( ( asRenderMaskDeprecated & MCR_IsGIVisible ) != 0 )							*(Uint16*)&outNewMask |= EMeshChunkFlags::MCF_IsGIVisible;
			if ( ( asRenderMaskDeprecated & MCR_IsGITransparent ) != 0 )						*(Uint16*)&outNewMask |= EMeshChunkFlags::MCF_IsGITransparent;
			if ( ( asRenderMaskDeprecated & MCR_PrefabProxy ) != 0 )							*(Uint16*)&outNewMask |= EMeshChunkFlags::MCF_IsPrefabProxy;

			chunk.m_chunkRenderMask = outNewMask;
		}
	}
#endif // RED_CONFIGURATION_FINAL

	static_assert( Version::NEWEST == Version::MESH_CHUNK_MASK_CLEANUP, "Version has been bumped. Need to make upgrades?" );
}

void CMesh::ComputeSurfaceAreaPerAxis()
{
	mesh::DataViewRO dataView( *this );

	// surface area for each axis
	// Note: using doubles for better precision
	Double surfaceAreas[ 3 ][ 2 ] = { { 0.0, 0.0 }, { 0.0, 0.0 }, { 0.0, 0.0 } };

	Bool hasAnyTriangles = false;

	for ( Uint32 chunkIndex : dataView.GetLODChunkIndices( 0 ) )
	{
		const mesh::StreamViewRO<Vector3> dataPosition( dataView.GetChunkStreamData( chunkIndex, MST_Position_3F ) );
		const mesh::StreamViewRO<Uint16> dataIndices( dataView.GetChunkStreamData( chunkIndex, MST_Index_U16 ) );

		hasAnyTriangles = hasAnyTriangles || ( dataIndices.Size() > 0 );

		for ( Uint32 i = 0; i < dataIndices.Size(); i += 3 )
		{
			const Vector4 vertex0 = dataPosition[ dataIndices[ i + 0 ] ];
			const Vector4 vertex1 = dataPosition[ dataIndices[ i + 1 ] ];
			const Vector4 vertex2 = dataPosition[ dataIndices[ i + 2 ] ];

			for ( Uint32 axis = 0; axis < 3; ++axis )
			{
				Vector4 tempVertex0 = vertex0;
				Vector4 tempVertex1 = vertex1;
				Vector4 tempVertex2 = vertex2;

				tempVertex0[ axis ] = 0.0f;
				tempVertex1[ axis ] = 0.0f;
				tempVertex2[ axis ] = 0.0f;

				const Vector4 cross = Vector4::Cross( tempVertex0 - tempVertex1, tempVertex0 - tempVertex2, 0.0f );
				const Uint32 side = cross[ axis ] > 0.0f ? 0 : 1;

				const Float triangleArea = cross.Mag3() / 2.0f;
				surfaceAreas[ axis ][ side ] += triangleArea;
			}
		}
	}

	if ( hasAnyTriangles )
	{
		for ( Uint32 axis = 0; axis < 3; ++axis )
		{
			// take max of areas of positive and negative side
			const Double maxArea = Max( surfaceAreas[ axis ][ 0 ], surfaceAreas[ axis ][ 1 ] );
			m_surfaceAreaPerAxis[ axis ] = static_cast<Float>( maxArea );
		}
	}
}

// PLEASE, do not copy-paste this function
Float CMesh::CalculateAutoHideDistance( const MeshAutohideParams& params )
{	
	return mesh::utils::GetGlobalAutoHideDistance(params.boundingBox, params.surfaceAreaPerAxis, params.meshScale);
}

Float CMesh::GetAutoHideDistance( const Vector3& scale ) const
{
	const MeshAutohideParams params =
	{
		GetBoundingBox(),
		m_surfaceAreaPerAxis,
		scale
	};

	return CalculateAutoHideDistance( params );
}

Bool CMesh::GetCastGlobalShadows() const
{
	// On cooked version m_chunks are empty but we are caching GetCastGlobalShadows() results in
	// m_castGlobalShadowsCachedInCook before stripping off the data (see CacheShadowsSettingsForCooking).
	Bool castShadowsBasedOnChunks = m_castGlobalShadowsCachedInCook;
#ifndef RED_CONFIGURATION_FINAL
	for (const auto& chunk : m_chunks)
	{
		const Uint8 renderMaskAndCascades = (chunk.m_renderMask & ( MCF_RenderInShadows ));
		castShadowsBasedOnChunks = (renderMaskAndCascades != 0);
		if (castShadowsBasedOnChunks) break;
	}
#endif // RED_CONFIGURATION_FINAL

	return castShadowsBasedOnChunks;
}

Bool CMesh::GetCastLocalShadows() const
{
	// On cooked version m_chunks are empty but we are caching GetCastLocalShadows() results in
	// m_castLocalShadowsCachedInCook before stripping off the data (see CacheShadowsSettingsForCooking).
	Bool castLocalShadowsBasedOnChunks = m_castLocalShadowsCachedInCook;
#ifndef RED_CONFIGURATION_FINAL
	for (const auto& chunk : m_chunks)
	{
		const Uint8 renderMaskAndLocalShadows = (chunk.m_renderMask & MCF_RenderInShadows );
		castLocalShadowsBasedOnChunks = (renderMaskAndLocalShadows != 0);
		if (castLocalShadowsBasedOnChunks) break;
	}
#endif // RED_CONFIGURATION_FINAL

	return castLocalShadowsBasedOnChunks;
}

void CMesh::CacheShadowsSettingsForCooking()
{
	m_castGlobalShadowsCachedInCook = GetCastGlobalShadows();
	m_castLocalShadowsCachedInCook = GetCastLocalShadows();
}

void CMesh::CreateRenderResource()
{
#if defined( VANGUARD_ASSET_HOST )
	// AssetHost always loads/cooks meshes with skipUploadRenderData and never owns a GPU renderer.
	return;
#else
	PC_SCOPE(MeshCreateRenderResource);

	RED_DATA_ERROR_BEGIN(red::LoggerCategory_Engine, m_renderResourceBlob == nullptr || m_renderResourceBlob->IsVersionValid(),
		"%hs has an invalid render resource. Please reimport it or revert recent changes.", GetFriendlyName().AsChar());
	RED_DATA_ERROR_ACTION_SHOW_ASSET(GetPath().ToDebugString());
	RED_DATA_ERROR_END();

	if (GRender && !GRender->IsDeviceLost() && m_renderResourceBlob && m_renderResourceBlob->IsVersionValid())
	{
#ifdef USE_RESOURCE_METRICS_BANK
		auto* resourceMetricsBank = GResourceLoader->GetResourceMetricsBank();

		if (m_renderResource)
		{
			if (GetPath().IsValid())
			{
				resourceMetricsBank->OnUnloadMesh(GetPath());
			}
		}
#endif

#ifndef RED_CONFIGURATION_FINAL
		m_renderResourceBlobSize = m_renderResourceBlob->GetDataSize();
#endif // RED_CONFIGURATION_FINAL

		m_renderResource = GRender->UploadBlobMesh(SRenderDataSource{ GetPath() }, m_renderResourceBlob);

#ifdef USE_RESOURCE_METRICS_BANK
		if (m_renderResource && GetPath().IsValid())
		{
			res::SingleResourceMetrics meshMemoryMetrics{};
			meshMemoryMetrics.path = GetPath();
			
#ifndef RED_PLATFORM_CONSOLE
			if ( Config::GGlobalTextureSettings.UseConsoleBias )
			{
				// If "console mode" is being used on PC, then we want to emulate the console environment as best as possible.
				// By using blob size as actual GPU size, we imitate the tiny buffer alignment that consoles have.
				// This is very important considering that the 64kB alignment on PC happens to accumulate more than 100MB excess of memory consumption.
				meshMemoryMetrics.gpuBytesUsed = GetRenderResourceSize();
			}
			else
#endif
			{
				// If no console emulation happens OR we are simply running on console, then we use the actual video memory allocated.
				meshMemoryMetrics.gpuBytesUsed = m_renderResource->GetUsedVideoMemory();
			}
			
			meshMemoryMetrics.mesh.isSkinned = IsSkinnedMesh(this);
			meshMemoryMetrics.mesh.hasPhysics = (Bool)(GetParameter< mesh::MeshParamPhysics >());

			resourceMetricsBank->OnLoadMesh(meshMemoryMetrics);
		}
#endif
		}
#endif
}

// currently should only be used when modifying the mesh in the editor now
void CMesh::RecreateInternalResources(const Uint32 what/* = MeshInternalData_All*/, const Bool preloadResources /*=false*/ )
{
	PC_SCOPE(MeshRecreateInternalResources);

	// compile the rendering resource for the blob
	if ((what & MeshInternalData_RenderResource) || !m_renderResource)
	{
		CreateRenderResource();
	}

	// compile material setup
	if (what & MeshInternalData_RenderMaterialSetup)
	{
		PC_SCOPE(MeshCreateRenderMaterialSetup);

		AssertHasValidMaterialEntries();

		const Bool flushWaitCounter = false;
		for ( const THandle< MeshAppearance >& appearance : m_appearances )
		{		
			appearance->ForceReloadRenderMaterialSetup(preloadResources, flushWaitCounter);
		}
	}
}

void CMesh::AssertHasValidMaterialEntries()
{
	// #tbd: static analyzer about unreachable code
#ifndef RED_ASSERTS_ENABLED
	return;
#endif

	red::DynArray<CName> materialNames{ rend::PoolMesh() };
	for (const THandle< MeshAppearance >& appearance : m_appearances)
	{
		for ( const CName matName : appearance->m_chunkMaterials )
		{
			red::alg::PushBackUnique( materialNames, matName );
		}
	}

	red::DynArray<CName> missingMaterialsByName{ rend::PoolMesh() };
	{
		RED_SCOPE_SHARED_LOCK(m_materialEntriesLock);

		for (CName matName : materialNames)
		{
			Bool isEntryFound = false; // doesn't matter if actually has material or not
			for (const MaterialEntry& matEntry : m_materialEntries)
			{
				if (matEntry.GetName() == matName)
				{
					isEntryFound = true;
					break;
				}
			}
			if (!isEntryFound)
			{
				missingMaterialsByName.PushBack(matName);
			}
		}
	}

	struct NameJoiner
	{
		static String Join(const red::DynArray<CName>& names)
		{
			red::DynArray<String> tmp{ rend::PoolMesh() };
			for (const auto& it : names)
				tmp.PushBack(it.AsChar());
			return String::Join(tmp, ",");
		}
	};

	RED_FATAL_ASSERT(missingMaterialsByName.Empty(), "Corrupted mesh data! Were mesh chunks created manually and we haven't actually assigned materials yet? Missing required materials: %hs", NameJoiner::Join(missingMaterialsByName).AsChar());
}

void CMesh::CreateDefaultAppearance()
{
	THandle< MeshAppearance > defaultAppearance( CreateHandle< MeshAppearance >( this ) );

	m_appearances.Resize( 1u );
	m_appearances[0] = defaultAppearance;
}

void CMesh::SetAppearanceBackPointers()
{
	for ( const THandle< mesh::MeshAppearance >& appearance : m_appearances )
		appearance->m_meshOwner = this;
}

Bool CMesh::SetRenderResourceBlob( THandle< IRenderResourceBlob >& blob )
{
#ifdef USE_RESOURCE_METRICS_BANK
	auto* resourceMetricsBank = GResourceLoader->GetResourceMetricsBank();

	if ( m_renderResource )
	{
		if ( GetPath().IsValid() )
		{
			resourceMetricsBank->OnUnloadMesh( GetPath() );
		}
	}
#endif

	m_renderResourceBlob = blob;
#ifndef RED_CONFIGURATION_FINAL
	m_renderResourceBlobSize = GetRenderResourceSize();
#endif // RED_CONFIGURATION_FINAL

#ifdef USE_RESOURCE_METRICS_BANK
	if ( GetPath().IsValid() )
	{
		res::SingleResourceMetrics meshMemoryMetrics{};
		meshMemoryMetrics.path = GetPath();
		meshMemoryMetrics.gpuBytesUsed = m_renderResourceBlob->GetDataSize();
		meshMemoryMetrics.mesh.isSkinned = IsSkinnedMesh( this );

		resourceMetricsBank->OnLoadMesh( meshMemoryMetrics );
	}
#endif

	// after blob was regenerated we have to upload the mesh again
	m_renderResource = nullptr;	

	return true;
}

job::Counter* CMesh::HACK_GetPostLoadWaitCounter() const
{
	return m_preloadCounter.Get();
}

//----

THandle< mesh::MeshParameter > CMesh::GetParameter(const rtti::ClassType* parameterType) const
{
	for ( const auto& param : m_parameters )
		if ( param && param->GetClass() == parameterType )
			return param;

	return nullptr;
}

THandle< mesh::MeshParameter > CMesh::CreateParameter(const rtti::ClassType* parameterType)
{
	RED_FATAL_ASSERT(!parameterType->IsAbstract(), "Cannot create abstract mesh parameters");
	RED_FATAL_ASSERT(parameterType->IsA< mesh::MeshParameter >(), "Specified class is not a mesh parameter class");

	THandle< mesh::MeshParameter > param = GetParameter(parameterType);
	if (param)
		return param;

	param = parameterType->CreateHandle< mesh::MeshParameter >();
	AddParameter(param);

	return param;
}

void CMesh::AddParameter( THandle< mesh::MeshParameter > parameter )
{
	RED_FATAL_ASSERT(parameter != nullptr, "Null mesh parameter" );
	RED_FATAL_ASSERT(parameter->GetParent() == nullptr, "Mesh parameter should have NULL parent when added");

	// Check if added parameter is of unique type
	for ( auto& currentParamenter : m_parameters )
	{
		if ( parameter.Get()->GetClass() != currentParamenter.Get()->GetClass() )
			continue;
		
		RED_FATAL( "Mesh can have only one instance of each parameter type" );
		return;
	}

	MarkModified();

	m_parameters.PushBack( parameter );
	parameter->SetParent(this);

	parameter->OnMeshChanged( *this );
}

void CMesh::RemoveParameter( THandle< mesh::MeshParameter > parameter )
{
	RED_FATAL_ASSERT( parameter, "Null mesh parameter" );
	RED_FATAL_ASSERT(parameter->GetParent() == this, "Mesh parameter is not owned by this mesh");

	MarkModified();

	parameter->SetParent(nullptr);

	if ( !m_parameters.Remove( parameter ).IsSuccessful() )
	{
		RED_LOG_WARNING( "Mesh parameter was not found" );
	}
}

void CMesh::RemoveParameter(const rtti::ClassType* parameterType)
{
	auto iter = std::remove_if( m_parameters.Begin(), m_parameters.End(), 
		[parameterType]( const THandle< mesh::MeshParameter >& param ) -> bool
	{
		return !param || param->IsA( parameterType );
	} );
	if ( iter != m_parameters.End() )
	{
		MarkModified();
		m_parameters.Remove( iter, m_parameters.End() );
	}
}

void CMesh::PreloadAppearances_StrictlyPrivateNeverCallFromFriends(Bool flushAppearanceWaitCounter, const serialization::LoadingContext* optLoadingContext )
{
	job::Counter loadingCounter{ job::Priority::Latent };
	auto* resourceLoader = optLoadingContext ? optLoadingContext->m_resourceLoader : GResourceLoader;
	if (!resourceLoader)
	{
		return;
	}

	// #tbd: Check if should skip "imports"... for the sake of throttling now should use parent loading context and not a non-throttled context
	auto externalMaterials = red::CreateSharedPtr< MaterialArrayWrapper, rend::PoolMesh >();
	externalMaterials->arr.Resize(m_externalMaterials.Size());
	for (Uint32 i = 0; i < m_externalMaterials.Size(); ++i)
	{
		const auto& matRef = m_externalMaterials[i];

		// Especially important to use the loadingContext here in case it's unthrottled as in the case of gathered resources
		// because otherwise you could deadlock because of "WaitUntilLoaded" being called.
		res::IssueLoadingRequestParameter param;
		if (optLoadingContext)
		{
			param = res::ResourceLoader::GetImportLoadingRequestParameter(matRef.GetPath(), *optLoadingContext);
		}
		else
		{
			param = res::IssueLoadingRequestParameter(matRef.GetPath());
			param.priority = io::EAsyncPriority::eAsyncPriority_Normal;
		}

		auto token = resourceLoader->IssueLoadingRequest(param);
		loadingCounter += token->OnLoaded([externalMaterials, i](const THandle< CResource >& res) {
			externalMaterials->arr[i] = Cast< IMaterial >(res);
		});
	}

	auto localMaterials = red::CreateSharedPtr< MaterialArrayWrapper, rend::PoolMesh >();
	localMaterials->arr.Resize(m_localMaterialBuffer.GetNumMaterials());
	for (Uint32 i = 0, numMaterials = m_localMaterialBuffer.GetNumMaterials(); i < numMaterials; ++i)
	{
		auto token = m_localMaterialBuffer.IssueLoadMaterialByIndex_UnsafeLifetime((Uint16)i, optLoadingContext, io::eAsyncPriority_Normal);
		loadingCounter += token->OnLoaded([localMaterials, i](const THandle<CResource>& res) {
			localMaterials->arr[i] = Cast< IMaterial >(res);
		});
	}

	// #tbd: instead of checking loading context, just check if mesh has m_renderResource or not
	// Assumes you won't create mesh render material setup before the mesh itself if constructing meshes programmatically
	// This whole postloadContext vs loadingContext becomes a huge mess anyway once I actually need the loadingContext for throttling/priority etc
	// So the postloadContext itself should probably go away
	Bool skipUploadRenderData = optLoadingContext ? optLoadingContext->m_postLoadFlags.skipUploadRenderData : false;

	job::Builder builder{ job::Priority::Latent };
	builder.DispatchWait(loadingCounter);

	// NOTE: Can't assume serialization::BinaryLoader, so could be abandoned before finishing. E.g., serialization::LoadFromMemory with doActiveWaiting=false
	builder.DispatchJob("Mesh/FinishAppearancePreload", [self = HandleFromThis<CMesh>(), externalMaterials, localMaterials, skipUploadRenderData, flushAppearanceWaitCounter](const job::RunContext& runContext)
	{
		self->SetExternalMaterialEntriesMaterialPointers(externalMaterials->arr);
		self->SetLocalMaterialEntriesMaterialPointers(localMaterials->arr);

		if (!skipUploadRenderData)
		{
			// #tbd: can do this in a job per appearance template by using LoadRenderMaterialSetup() and a depBuilder
			// But there's no throttling here anymore
			for (auto& appearance : self->m_appearances)
			{
				appearance->ForceReloadRenderMaterialSetup(false, flushAppearanceWaitCounter);
			}
		}
	});

	if (!m_preloadCounter)
	{
		m_preloadCounter = red::CreateUniquePtr< job::Counter >( job::Priority::Latent );
	}

	*m_preloadCounter += builder.ExtractWaitCounter();
}

const THandle< mesh::MeshAppearance >& CMesh::GetAppearanceInternal( const CName appearanceName ) const
{
	for ( const THandle< mesh::MeshAppearance >& appearance : m_appearances )
	{
		if ( appearance && appearance->GetName() == appearanceName )
		{
			return appearance;
		}
	}

	if ( appearanceName.Empty() || ( appearanceName == MeshAppearance::c_DefaultAppearanceName ) )  // Empty name also falls back to default, just to be more robust.
	{
		return GetAppearanceInternal( 0 ); // First appearance is considered to be a default one.
	}

	return s_nullAppearancePtr;
}

const THandle< mesh::MeshAppearance >& CMesh::GetAppearanceInternal( const Uint32 appearanceIndex ) const
{
	if ( appearanceIndex < m_appearances.Size() )
	{
		return m_appearances[ appearanceIndex ];
	}
			
	RED_DATA_ERROR_BEGIN( red::LoggerCategory_Default, !m_appearances.Empty(), "An attempt to use an empty mesh (no appearances found): %hs", GetPath().ToDebugString() );
	RED_DATA_ERROR_ACTION_SHOW_ASSET( GetPath().ToDebugString() );
	RED_DATA_ERROR_END();

	if ( m_appearances.Empty() )					/// @todo DP: this is a hack, we have something broken in resource loading system and fallback meshes sometimes do not work
	{
		static THandle< mesh::MeshAppearance > emptyAppearance = CreateHandle< mesh::MeshAppearance >();
		return emptyAppearance;
	}
	return m_appearances[ 0 ];                  // First appearance is considered to be a default one.
}

namespace helper
{
	// Create a map where key=materialNames[i] -> value=outMaterialIndices[i]
	static void MapMaterialNamesToIndices(const red::DynArray< CMesh::MaterialEntry >& materialEntries, const red::DynArray< CName >& materialNames, red::DynArray<Uint32>& outMaterialIndices)
	{
		outMaterialIndices.Reserve(materialNames.Size());

		// #todo: shouldn't have to search for the indices, the appearance should have this info!
		for (CName materialName : materialNames)
		{
			for (Uint32 i : materialEntries.Indices())
			{
				const auto& entry = materialEntries[i];
				if (entry.GetName() == materialName)
				{
					outMaterialIndices.PushBack(i);
					break;
				}
			}
		}
	}
}

std::pair< job::Counter, red::SharedPtr< CMesh::MaterialArrayWrapper, rend::PoolMesh > > CMesh::IssueLoadMaterials(const red::DynArray< CName >& materialNames, io::EAsyncPriority priority)
{
	RED_SCOPE_LOCK(m_materialEntriesLock);

	red::DynArray<Uint32> materialIndices{ rend::PoolMesh() };
	helper::MapMaterialNamesToIndices(m_materialEntries, materialNames, materialIndices);

	job::Builder builder{ job::Priority::Latent };

	auto result = red::CreateSharedPtr< MaterialArrayWrapper, rend::PoolMesh >();
	result->arr.Resize(materialNames.Size());

	auto self = HandleFromThis<CMesh>();

	for ( Uint32 i : materialIndices.Indices())
	{
		const CName materialName = materialNames[i];
		const Uint32 materialIndex = materialIndices[i];
		auto& entry = m_materialEntries[materialIndex];
		
		// Get without fallback, so we know whether to try to actually try loading the resource
		// NOTE: Once finished loading, the entry will bet set with a non-null value whether successful or not. E.g., the fallback material.
		auto material = entry.GetMaterialNoFallbackIfNull();
		if (material)
		{
			result->arr[i] = std::move(material);
		}
		else
		{
			res::ResourceTokenHandle token;
			if (entry.IsLocalInstance())
			{
				token = m_localMaterialBuffer.IssueLoadMaterialByIndex_UnsafeLifetime(entry.GetIndex(), nullptr, priority);
			}
			else
			{
				res::LoadingOptions options;
				options.useGameCache = true;
				token = m_externalMaterials[entry.GetIndex()].IssueLoadingRequest( options, priority );
			}

            builder.DispatchJobAfterWait_NoFence( token->GetWaitCounter(), "CMesh/IssueLoadMaterials/OnLoaded", [self, token, materialName, materialIndex, result, i]( const job::RunContext& )
            {
                auto material = Cast<IMaterial>( token->GetResource() );
                if( !material )
                {
                    material = s_fallbackMaterial.Get();
                }

                // Update entry under lock
                {
                    auto& materialEntriesLock = self->m_materialEntriesLock;
                    RED_SCOPE_LOCK( materialEntriesLock );

                    // Ensure wasn't edited in any way that matters
                    if( materialIndex < self->m_materialEntries.Size() && self->m_materialEntries[materialIndex].GetName() == materialName )
                    {
                        auto& entry = self->m_materialEntries[materialIndex];
                        // just blindly set the material; rely on handle self-assignment check, since should be the same handle if multiple IssueloadingRequests anyway.
                        entry.SetLoadedMaterial( material );
                    }
                }

                // Don't get from the entry again in case the editor set the value too (e.g., even before loading was started).
                // And for consistency, the result should contain the material that was requested, not whatever it currently happens to be
                result->arr[i] = std::move( material );
            } );
		}
	}

	builder.DispatchFenceExplicitly();

	return std::make_pair(builder.ExtractWaitCounter(), result);
}


red::DynArray< res::ResourcePath > CMesh::CollectDependencies( const red::DynArray< CName >& materialNames ) const
{
	red::DynArray< Uint32 > materialIndices{red::PoolBackend()};
	red::DynArray< res::ResourcePath > result{red::PoolBackend()};

	helper::MapMaterialNamesToIndices( m_materialEntries, materialNames, materialIndices );

	for( Uint32 i : materialIndices.Indices() )
	{
		const Uint32 materialIndex = materialIndices[ i ];
		auto& entry = m_materialEntries[ materialIndex ];

		if( !entry.IsLocalInstance() )
		{
            const Uint32 index = entry.GetIndex();
            if( index < m_externalMaterials.Size() )
            {
                result.PushBack( m_externalMaterials[index].GetPath() );
            }
		}

	}

	return result;
}

red::DynArray< THandle<IMaterial> > CMesh::Cook_GetLocalMaterials( const red::DynArray< CName >& materialNames ) const
{
    red::DynArray< Uint32 > materialIndices{ red::PoolBackend() };
    red::DynArray< THandle<IMaterial> > result{ red::PoolBackend() };

    helper::MapMaterialNamesToIndices( m_materialEntries, materialNames, materialIndices );

    for( Uint32 i : materialIndices.Indices() )
    {
        const Uint32 materialIndex = materialIndices[i];
        auto& entry = m_materialEntries[materialIndex];

        if( entry.IsLocalInstance() )
        {
            if( auto material = entry.GetMaterialNoFallbackIfNull() )
            {
                result.PushBack( material );
            }
        }
    }
    return result;
}

Bool CMesh::IsRenderMaterialSetupForAllAppearancesPreloaded() const
{
	// For convenience to migrate systems
	const Bool onlySingleAppearance = m_appearances.Size() == 1;
	return m_forceLoadAllAppearances || onlySingleAppearance || hacks::IsEditorForceLoadAllMeshAppearances();
}

const THandle< mesh::MeshAppearance >& CMesh::GetAppearanceSafe( const CName appearanceName ) const
{
	const THandle< mesh::MeshAppearance >& appearance = GetAppearanceInternal( appearanceName );
	if ( appearance != nullptr )
		return appearance;
	return GetDefaultAppearance();
}

const THandle< mesh::MeshAppearance >& CMesh::GetAppearanceSafe( const Uint32 appearanceIndex ) const
{
	if ( const THandle< mesh::MeshAppearance >& appearance = GetAppearanceInternal( appearanceIndex ) )
	{
		return appearance;
	}
	return GetDefaultAppearance();
}

const THandle< mesh::MeshAppearance >& CMesh::GetDefaultAppearance() const
{
	return GetAppearanceInternal( MeshAppearance::c_DefaultAppearanceName );
}

res::GatheredResource * CMesh::GetDefaultResource() const
{
	return &s_fallbackMesh;
}
res::GatheredResource * CMesh::GetDefaultResourceStatic()
{
    return &s_fallbackMesh;
}

void CMesh::RecreateInternalRenderResources( Uint32 flag )
{
	res::StreamedResource::RecreateInternalRenderResources( flag );
	RecreateInternalResources( flag, true );
}

red::DynArray< THandle< IMaterial > > CMesh::GetMaterials() const
{
	RED_SCOPE_SHARED_LOCK(m_materialEntriesLock);
	red::DynArray< THandle< IMaterial > > materials{ rend::PoolMesh() };
	materials.Reserve(m_materialEntries.Size());
	for (const auto& matEntry : m_materialEntries)
	{
		materials.PushBack(matEntry.GetMaterial());
	}
	return materials;
}

Uint8 CMesh::GetConsoleMeshBias() const
{
#ifdef RED_CONFIGURATION_FINAL
	return 0;
#else
	return m_consoleBias;
#endif // RED_CONFIGURATION_FINAL
}

void CMesh::SetConsoleMeshBias(Uint8 bias)
{
#ifndef RED_CONFIGURATION_FINAL
	m_consoleBias = bias;
#endif // RED_CONFIGURATION_FINAL
}

void CMesh::AddInplaceResourceDependency( const res::ResourcePath& path )
{
	m_inplaceResources.PushBack( path );
}

namespace MeshUtilities
{
	Bool HasPosition( const EMeshVertexType vertexType ) { return true; }
	Bool HasInstancing( const EMeshVertexType vertexType ) { return true; }
	Bool HasTexCoord0( const EMeshVertexType vertexType ) { return vertexType != MVT_Terrain;; }
	Bool HasNormalAndTangent( const EMeshVertexType vertexType ) { return vertexType != MVT_Terrain;; }
	
	Bool HasSkinning( const EMeshVertexType vertexType )
	{
		switch ( vertexType )
		{
			case MVT_SkinnedMesh:
			case MVT_ExtSkinnedMesh:
			case MVT_GarmentSkinnedMesh:
			case MVT_ExtGarmentSkinnedMesh:
			case MVT_SkinnedMeshVehicle:
			case MVT_DestructibleMeshSkinned:
			case MVT_SkinnedMeshLightBlocker:
			case MVT_ExtSkinnedMeshLightBlocker:
			case MVT_GarmentSkinnedMeshLightBlocker:
			case MVT_ExtGarmentSkinnedMeshLightBlocker:
				return true;
		}
		return false;
	}

	Bool HasExtendedSkinning( const EMeshVertexType vertexType )
	{
		switch ( vertexType )
		{
			case MVT_ExtSkinnedMesh:
			case MVT_ExtGarmentSkinnedMesh:
			case MVT_ExtSkinnedMeshLightBlocker:
			case MVT_ExtGarmentSkinnedMeshLightBlocker:
				return true;
		}
		return false;
	}

	Bool HasSingleBoneSkinning( const EMeshVertexType vertexType )
	{
		switch( vertexType )
		{
		case MVT_SkinnedMeshSingleBone:
			return true;
		}
		return false;
	}

	Bool HasDestruction( const EMeshVertexType vertexType )
	{
		switch ( vertexType )
		{
			case MVT_DestructibleMesh:
			case MVT_DestructibleMeshSkinned:
				return true;
		}
		return false;
	}

	Bool HasGarmentExtraData( const EMeshVertexType vertexType )
	{
		switch ( vertexType )
		{
			case MVT_GarmentSkinnedMesh:
			case MVT_ExtGarmentSkinnedMesh:
			case MVT_GarmentSkinnedMeshLightBlocker:
			case MVT_ExtGarmentSkinnedMeshLightBlocker:
				return true;
		}
		return false;
	}

	Bool HasSpeedTreeExtraData( const EMeshVertexType vertexType )
	{
		switch ( vertexType )
		{
			case MVT_SpeedTreeMesh:
				return true;
		}
		return false;
	}

	Bool CanHaveVehicleDamageData( const EMeshVertexType vertexType )
	{
		switch ( vertexType )
		{
			case MVT_StaticMeshVehicle:
			case MVT_SkinnedMeshVehicle:
				return true;
		}
		return false;
	}

	Bool CanHaveLightBlockerIntensity( const EMeshVertexType vertexType )
	{
		switch ( vertexType )
		{
			case MVT_SkinnedMeshLightBlocker:
			case MVT_ExtSkinnedMeshLightBlocker:
			case MVT_GarmentSkinnedMeshLightBlocker:
			case MVT_ExtGarmentSkinnedMeshLightBlocker:
				return true;
		}
		return false;
	}

	using namespace GpuWrapApi::VertexPacking;

	red::DynArray< PackingElement > GetVertexPackingForVertexType( const EMeshVertexType vertexType )
	{
		RED_ASSERT( vertexType != MVT_Max, "Unsupported chunk vertex type" );

		// The m_slot argument to PackingElement is directly related to the enum ERenderMeshStream.
		// File: common/renderData/include/renderPublicEnums.h
		// For example, all positions and skinning should use slot 0, because RMS_PositionSkinning is 0-th element in ERenderMeshStreams.
		//
		// To access these streams in a shader, its FXMeta has to provide the enum value.
		// See the comment above ERenderMeshStream in renderPublicEnums.h.

		red::DynArray< PackingElement > res{ rend::PoolMesh() };
		if ( HasPosition( vertexType ) )
		{
			res.PushBack( PackingElement( {	PT_Short4N, PS_Position, 0, 0, ST_PerVertex } ) );
		}
		if ( HasSkinning( vertexType ) )
		{
			res.PushBack( PackingElement( { PT_UByte4, PS_SkinIndices, 0, 0, ST_PerVertex } ) );
		}
		if ( HasExtendedSkinning( vertexType ) )
		{
			res.PushBack( PackingElement( { PT_UByte4, PS_SkinIndices, 1, 0, ST_PerVertex } ) );
		}
		if( HasSingleBoneSkinning( vertexType ) )
		{
			res.PushBack( PackingElement( { PT_UInt1, PS_BoneIndex, 0, 0, ST_PerVertex } ) );
		}
		if ( HasSkinning( vertexType ) )
		{
			res.PushBack( PackingElement( { PT_UByte4N, PS_SkinWeights, 0, 0, ST_PerVertex } ) );
		}
		if ( HasExtendedSkinning( vertexType ) )
		{
			res.PushBack( PackingElement( { PT_UByte4N, PS_SkinWeights, 1, 0, ST_PerVertex } ) );
		}
		if ( HasTexCoord0( vertexType ) )
		{
			res.PushBack( PackingElement( { PT_Float16_2, PS_TexCoord, 0, 1, ST_PerVertex } ) );
		}
		if ( HasNormalAndTangent( vertexType ) )
		{
			res.PushBack( PackingElement( { PT_Dec4, PS_Normal, 0, 2, ST_PerVertex } ) );
			if ( vertexType != MVT_ProxyMesh && vertexType != MVT_ProxyWindowMesh )
			{
				res.PushBack( PackingElement( { PT_Dec4, PS_Tangent, 0, 2, ST_PerVertex } ) );
			}
		}
		if ( vertexType != MVT_Terrain && vertexType != MVT_ProxyMesh && vertexType != MVT_ProxyWindowMesh )
		{
			res.PushBack( PackingElement( { PT_Color, PS_Color, 0, 3, ST_PerVertex } ) );
			res.PushBack( PackingElement( { PT_Float16_2, PS_TexCoord, 1, 3, ST_PerVertex } ) );
		}
		if ( HasDestruction( vertexType ) )
		{
			res.PushBack( PackingElement( { PT_UShort2,	PS_DestructionIndices, 0, 4, ST_PerVertex } ) );
		} 
		else if ( vertexType == MVT_ProxyWindowMesh )
		{
			res.PushBack( PackingElement( { PT_UShort2,	PS_DestructionIndices, 0, 3, ST_PerVertex } ) );
		}


		if ( HasGarmentExtraData( vertexType ) )
		{
			res.PushBack( PackingElement( { PT_Float16_4, PS_ExtraData, 0, 0, ST_PerVertex } ) );
		}
		if ( HasSpeedTreeExtraData( vertexType ) )
		{
			res.PushBack( PackingElement( {	PT_Float16_4, PS_ExtraData, 0, 4, ST_PerVertex } ) );
			res.PushBack( PackingElement( {	PT_Float16_4, PS_ExtraData, 1, 4, ST_PerVertex } ) );
			res.PushBack( PackingElement( {	PT_Float16_4, PS_ExtraData, 2, 4, ST_PerVertex } ) );
		}
		if ( true )
		{
			// all are instanced, push instance transform data
			res.PushBack( PackingElement( {	PT_Float4, PS_InstanceTransform, 0, 7, ST_PerInstance } ) );
			res.PushBack( PackingElement( {	PT_Float4, PS_InstanceTransform, 1, 7, ST_PerInstance } ) );
			res.PushBack( PackingElement( {	PT_Float4, PS_InstanceTransform, 2, 7, ST_PerInstance } ) );
		}
		if ( HasSkinning( vertexType ) || HasSingleBoneSkinning( vertexType ) )
		{
			res.PushBack( PackingElement( { PT_UInt4, PS_InstanceSkinningData, 0, 7, ST_PerInstance } ) );
		}
		if ( CanHaveVehicleDamageData( vertexType ) )
		{
			res.PushBack( PackingElement( { PT_Dec4, PS_VehicleDmgNormal, 0, 4, ST_PerVertex } ) );
			res.PushBack( PackingElement( { PT_Float4, PS_VehicleDmgPosition, 0, 4, ST_PerVertex } ) );
		}
		if ( CanHaveLightBlockerIntensity( vertexType ) )
		{
			res.PushBack( PackingElement( { PT_Float1, PS_LightBlockerIntensity, 0, 4, ST_PerVertex } ) );
		}

		res.PushBack( PackingElement::END_OF_ELEMENTS );

		return res;
	}

	EMaterialVertexFactory GetVertexFactoryForVertexType( const EMeshVertexType vertexType )
	{
		switch ( vertexType )
		{
			case MVT_StaticMesh:						return MVF_MeshStatic;
			case MVT_DestructibleMesh:					return MVF_MeshDestructible;
			case MVT_DestructibleMeshSkinned:			return MVF_MeshDestructibleSkinned;
			case MVT_ProceduralMesh:					return MVF_MeshProcedural;
			case MVT_SkinnedMesh:						return MVF_MeshSkinned;
			case MVT_ExtSkinnedMesh:					return MVF_MeshExtSkinned;
			case MVT_GarmentSkinnedMesh:				return MVF_GarmentMeshSkinned;
			case MVT_ExtGarmentSkinnedMesh:				return MVF_GarmentMeshExtSkinned;
			case MVT_StaticMeshVehicle:					return MVF_MeshStaticVehicle;
			case MVT_SkinnedMeshVehicle:				return MVF_MeshSkinnedVehicle;
			case MVT_SpeedTreeMesh:						return MVF_MeshSpeedTree;
			case MVT_Terrain:							return MVF_Terrain;
			case MVT_SkinnedMeshLightBlocker:			return MVF_MeshSkinnedLightBlockers;
			case MVT_ExtSkinnedMeshLightBlocker:		return MVF_MeshExtSkinnedLightBlockers;
			case MVT_GarmentSkinnedMeshLightBlocker:	return MVF_GarmentMeshSkinnedLightBlockers;
			case MVT_ExtGarmentSkinnedMeshLightBlocker: return MVF_GarmentMeshExtSkinnedLightBlockers;
			case MVT_SkinnedMeshSingleBone:				return MVF_MeshSkinnedSingleBone;
			case MVT_ProxyMesh:							return MVF_MeshProxy;
			case MVT_ProxyWindowMesh:					return MVF_MeshWindowProxy;

			default:
				RED_HALT( "Unsupported chunk vertex type" );
		}

		return MVF_MeshStatic;
	}

	EMaterialVertexFactory GetVertexFactoryForMeshChunk( const SMeshChunkPacked& sourceChunk )
	{
		return GetVertexFactoryForVertexType( sourceChunk.m_vertexType );
	}

	red::DynArray< PackingElement > GetShadowMeshPositionOnlyVertexPacking()
	{
		red::DynArray< PackingElement > res{ rend::PoolMesh() };
		res.PushBack( PackingElement( {	PT_Short4N,		PS_Position,			0,	0,	ST_PerVertex	} ) );
		res.PushBack( PackingElement( {	PT_Float4,		PS_InstanceTransform,	0,	7,	ST_PerInstance	} ) );
		res.PushBack( PackingElement( {	PT_Float4,		PS_InstanceTransform,	1,	7,	ST_PerInstance	} ) );
		res.PushBack( PackingElement( {	PT_Float4,		PS_InstanceTransform,	2,	7,	ST_PerInstance	} ) );
		res.PushBack( PackingElement::END_OF_ELEMENTS );
		return res;
	}

	PackingVertexLayout GetVertexLayout()
	{
		PackingVertexLayout vertexLayout;
		vertexLayout.m_stride = sizeof( SMeshVertex );

		// Standard streams
		vertexLayout.Init( offsetof( SMeshVertex, m_position ),		PS_Position,			0, PT_Float3	);
		vertexLayout.Init( offsetof( SMeshVertex, m_normal ),		PS_Normal,				0, PT_Float3	);
		vertexLayout.Init( offsetof( SMeshVertex, m_tangent ),		PS_Tangent,				0, PT_Float3	);
		vertexLayout.Init( offsetof( SMeshVertex, m_binormal ),		PS_Binormal,			0, PT_Float3	);
		vertexLayout.Init( offsetof( SMeshVertex, m_uv0 ),			PS_TexCoord,			0, PT_Float2	);
		vertexLayout.Init( offsetof( SMeshVertex, m_uv1 ),			PS_TexCoord,			1, PT_Float2	);
		vertexLayout.Init( offsetof( SMeshVertex, m_color),			PS_Color,				0, PT_Color		);
		vertexLayout.Init( offsetof( SMeshVertex, m_indices ),		PS_SkinIndices,			0, PT_UByte4	);
		vertexLayout.Init( offsetof( SMeshVertex, m_weights ),		PS_SkinWeights,			0, PT_Float4	);
		vertexLayout.Init( offsetof( SMeshVertex, m_indicesExt ),	PS_SkinIndices,			1, PT_UByte4	);
		vertexLayout.Init( offsetof( SMeshVertex, m_weightsExt ),	PS_SkinWeights,			1, PT_Float4	);
		vertexLayout.Init( offsetof( SMeshVertex, m_destruction ),  PS_DestructionIndices,	0, PT_UShort2	);
		vertexLayout.Init( offsetof( SMeshVertex, m_multilayer ),	PS_MultilayerPaint,		0, PT_Float1	);
		vertexLayout.Init( offsetof( SMeshVertex, m_indices ),		PS_BoneIndex,			0, PT_UByte4 );

		// Special streams
		vertexLayout.Init( offsetof( SMeshVertex, m_extraData0 ),	PS_ExtraData,			0, PT_Float4	);
		vertexLayout.Init( offsetof( SMeshVertex, m_extraData1 ),	PS_ExtraData,			1, PT_Float4	);
		vertexLayout.Init( offsetof( SMeshVertex, m_extraData2 ),	PS_ExtraData,			2, PT_Float4	);
		vertexLayout.Init( offsetof( SMeshVertex, m_extraData3 ),	PS_ExtraData,			3, PT_Float4	);

		vertexLayout.Init( offsetof( SMeshVertex, m_vehicleDamageNormal ),		PS_VehicleDmgNormal,	0, PT_Float3	);
		vertexLayout.Init( offsetof( SMeshVertex, m_vehicleDamagePosition ),	PS_VehicleDmgPosition,	0, PT_Float3	);

		vertexLayout.Init( offsetof( SMeshVertex, m_lightBlockerData ),			PS_LightBlockerIntensity,	0, PT_Float1	);

		return vertexLayout;
	}
}

