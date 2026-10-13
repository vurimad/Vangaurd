#include "build.h"
#include "timeOfDayVisibilityResource.h"
#include "../../redJobs2/include/jobUtils.h"


RTTI_BEGIN_TYPE( TimeOfDayVisibilityResource );
	RTTI_PARENT_TYPE( CResource );
RTTI_END_TYPE();

void TimeOfDayVisibilityResource::OnSerialize( IFile& file )
{
	RED_ASSERT( m_runtimeLUT.Empty(), "Runtime data should be cleared by calling FlushRuntimeLUT" );

	TBaseClass::OnSerialize( file );

	file << m_version;
	file << ArrayBulkSerializer< decltype( m_data ) >( m_data );
}

void TimeOfDayVisibilityResource::InsertEntryToRuntimeLUT( Uint64 hash, Float timeOfDay )
{
	m_runtimeLUT[ hash ].m_nonVisPerHour[ ( Uint8 ) timeOfDay ]++;
}

void TimeOfDayVisibilityResource::FlushRuntimeLUT( Uint8 minorTickPerHour )
{
	for ( const auto& k : m_runtimeLUT )
	{
		const Uint64 meshHash = k.Key();
		const auto& rawHoursData = k.Value();
		Uint16 packedInfo { 0 };

		// We pack every 2h together in order to fit data in Uint16.
		for ( Uint8 h = 0; h < 23; h += 2 )
		{
			// Invisibility data for two consecutive hours read from runtime LUT structure.
			// >= because it might turn out during baking process that there are actually 
			// meshes with the same path & transform placed on the world and thei redundandly bump the
			// counter for given time.
			const Bool invisAtH = ( rawHoursData.m_nonVisPerHour[ h ] >= minorTickPerHour );
			const Bool invisAtHplus1 = ( rawHoursData.m_nonVisPerHour[ h + 1 ] >= minorTickPerHour );

			// If mesh is not visible at two consecutive hours we can safely mark it as 
			// invisible in the output structure where each bit represents two hours.
			if ( invisAtH && invisAtHplus1 )
			{
				packedInfo |= ( 1u << HourToBit( h ) );
			}
		}

		// If there is at least one two-hours period where the mesh is invisible - move data to
		// serializable structure. Otherwise it will be assumed as always visible.
		if ( packedInfo != 0 )
		{
			m_data.PushBack( { packedInfo, meshHash } );
		}
	}

	// Clear the data, we do not need it anymore.
	m_runtimeLUT.Clear();
}

void TimeOfDayVisibilityResource::InsertBakedEntry( const TimeOfDayVisibilityResourceEntry& todVisResEntry )
{
	RED_ASSERT( todVisResEntry.m_todBitmask != 0, "Do not push empty Time of Day bitmask." );

	// There is no entry with 'meshHash' value - add one.
	m_data.PushBack( todVisResEntry );
}

const Uint16* TimeOfDayVisibilityResource::GetBakedEntry( Uint64 meshHash ) const
{
	if (m_data.Empty())
		return nullptr;

	TimeOfDayVisibilityResourceEntry fakeEntry{ 0, meshHash };
	auto entry = std::lower_bound
	(
		m_data.Begin(),
		m_data.End(), 
		fakeEntry, 
		[](const TimeOfDayVisibilityResourceEntry& a, const TimeOfDayVisibilityResourceEntry& b) { return a.m_hash < b.m_hash; }
	);

	if( entry != m_data.End() && entry->m_hash == meshHash )
	{
		return &entry->m_todBitmask;
	}
	
	return nullptr;
}

void TimeOfDayVisibilityResource::ParallelSortBakedEntries( job::Builder& builder )
{
	job::ParallelSort
	(
		builder, m_data.Begin(),
		m_data.End(), 
		[](const TimeOfDayVisibilityResourceEntry& a, const TimeOfDayVisibilityResourceEntry& b) { return a.m_hash < b.m_hash; }
	);
}

Uint64 TimeOfDayVisibilityResource::CalculateHash( const res::ResourcePath& meshResourcePath, const Transform& meshTransform, const Vector3& meshScale )
{
	const Uint64 hash = meshResourcePath.GetHash();
	const Matrix initialL2W = meshTransform.ToMatrix().SetScale33( meshScale );
	return red::CalculateHash64( &initialL2W, sizeof( Matrix ), hash );
}
