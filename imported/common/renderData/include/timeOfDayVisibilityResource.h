/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/
#pragma once
#include "../../redContainers/include/dynArray.h"
#include "../../redReflection/include/resource.h"

#pragma pack(push)
#pragma pack(2)	
struct TimeOfDayVisibilityResourceEntry
{
	// If bit is set, the entry is not visible at this time of day. 
	// TODO: in-depth bitset layout  description.
	Uint16 m_todBitmask;

	// Mesh resource name hashed together with transformation.
	Uint64 m_hash;
};
#pragma pack(pop)

RED_STATIC_ASSERT(sizeof(TimeOfDayVisibilityResourceEntry) == 10);

struct TimeOfDayVisibilityRuntimeEntry
{
	// Counter for each hour that represents number of sub-intervals that the entry was mot visible at.
	Uint8 m_nonVisPerHour[ 24 ];
};

class RENDER_DATA_API TimeOfDayVisibilityResource : public CResource
{
	RTTI_DECLARE_TYPE( TimeOfDayVisibilityResource );
	IMPLEMENT_RESOURCE_INTERFACE( "todvis", "todvis", "Time of Day Visibility Resource" );
	RED_BASE_CLASS( CResource );
	RED_USE_MEMORY_POOL( rend::PoolLighting );

public:

	virtual void OnSerialize( IFile& file ) override;

	static RED_INLINE Uint16 HourToBit( Float h ) 
	{
		RED_ASSERT( h >= 0.0f && h <= 24.0f, "Wrong hour specified." );
		return ( Uint16 ) ( static_cast< Uint8 >( h ) / 2 );
	}

	RED_INLINE const red::DynArray< TimeOfDayVisibilityResourceEntry >& GetBakedData() const { return m_data; }

	// Used by generator to collect intermediate data. Please do not rely on this method outside
	// of the generator as it assumes certain logic of calling it.
	void InsertEntryToRuntimeLUT( Uint64 hash, Float timeOfDay );

	// Pack data to serializable member. Must be called before SaveResource is used. 
	void FlushRuntimeLUT( Uint8 );
	
	// Insert data to packed/baked structure. In case meshHash already exists it will or'ed its bitmask with the provided one.
	void InsertBakedEntry( const TimeOfDayVisibilityResourceEntry& todVisResEntry );

	// Returns packed, baked visibility data for given mesh hash. nullptr if not available.
	const Uint16* GetBakedEntry( Uint64 meshHash ) const;

	// Number of baked entries in the structure.
	RED_FORCE_INLINE Uint32 GetNumOfBakedEntries() { return m_data.Size(); }

	// One place to access hash creation for this kind of resource.
	static Uint64 CalculateHash( const res::ResourcePath& meshResourcePath, const Transform& m_initialTransform, const Vector3& scale );

	// Sort baked entries based on hash value. 
	// NOTE: It spawns a parallel job(s) on provided builder so the results will probably not be ready after the procedures exists.
	//		 It's up to the caller to sync it properly.
	void ParallelSortBakedEntries( job::Builder& builder );

private:
	Uint32 m_version { 1 };

	// Intermediate, runtime struct that is not serialized. It maps mesh hash (resource path 
	// combined with transformation) to 32 bitmask, where first 24 bits represent hours of day.
	// Calling FlushRuntimeLUT will pack the data to m_data.
	red::Map<Uint64, TimeOfDayVisibilityRuntimeEntry> m_runtimeLUT { rend::PoolLighting() };

	// Serialized, packed data. Built via calling FlushRuntimeLUT(). It holds 16 bitmask per 
	// mesh hash (mesh path hashed with its transformation). Only its first 12 bits are used.
	// Each bit represents visibility of the hashed mesh in period of 2 consecutive hours starting
	// from hour 0.0. If bit is marked it means that during that period mesh was not visible at all.
	red::DynArray< TimeOfDayVisibilityResourceEntry > m_data { rend::PoolLighting() };

	// Hours between we are actually calculating visibility data. Everything out of the bounds
	// will assume that the mesh is not visible.
	Uint8	m_startHour { 5 };
	Uint8	m_endHour { 19 };
};

using TODVisResHandle = THandle< TimeOfDayVisibilityResource >;
