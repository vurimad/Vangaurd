/**
* Copyright (c) 2007-2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "materialFeatureFlags.h"


RTTI_BEGIN_ENUM( EFeatureFlag );
	RTTI_ENUM_OPTION( FEATFLAG_Default );
	RTTI_ENUM_OPTION( FEATFLAG_Shadows );
	RTTI_ENUM_OPTION( FEATFLAG_HitProxies );
	RTTI_ENUM_OPTION( FEATFLAG_Selection );
	RTTI_ENUM_OPTION( FEATFLAG_Wireframe );
	RTTI_ENUM_OPTION( FEATFLAG_Overdraw );
	RTTI_ENUM_OPTION( FEATFLAG_VelocityBuffer );
	RTTI_ENUM_OPTION( FEATFLAG_DebugDraw_BlendOff );
	RTTI_ENUM_OPTION( FEATFLAG_DebugDraw_BlendOn );
	RTTI_ENUM_OPTION( FEATFLAG_DynamicDecals );
	RTTI_ENUM_OPTION( FEATFLAG_Highlights );
	RTTI_ENUM_OPTION( FEATFLAG_IndirectInstancedGrass );
	RTTI_ENUM_OPTION( FEATFLAG_DecalsOnStaticObjects );
	RTTI_ENUM_OPTION( FEATFLAG_DecalsOnDynamicObjects );
	RTTI_ENUM_OPTION( FEATFLAG_MaskParticlesInsideCar );
	RTTI_ENUM_OPTION( FEATFLAG_MaskParticlesInsideInterior );
	RTTI_ENUM_OPTION( FEATFLAG_MaskTXAA );
	RTTI_ENUM_OPTION( FEATFLAG_DistantShadows );
	RTTI_ENUM_OPTION( FEATFLAG_FloatTracks );
	RTTI_ENUM_OPTION( FEATFLAG_Rain );
	RTTI_ENUM_OPTION( FEATFLAG_NumLights );
	RTTI_ENUM_OPTION( FEATFLAG_DepthPrepass );
RTTI_END_ENUM();

RTTI_BEGIN_TYPE( FeatureFlagsMask );
	RTTI_PROPERTY( m_flags );
RTTI_END_TYPE();

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

#define FOR_BITSCAN_LOOP_BEGIN( idxVarName, maskValue )	\
	do {												\
	for ( Uint64 _loopMask=(maskValue); _loopMask; )	\
	{													\
		const Uint32 idxVarName = (Uint32)red::BitUtils::BitScanForward<Uint64>( _loopMask );		\
		RED_FATAL_ASSERT( _loopMask & (1ull << idxVarName) );										\
		_loopMask ^= 1ull << idxVarName;

#define FOR_BITSCAN_LOOP_END()					\
	}} while (0)

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void FeatureFlagsCompiler::Clear()
{
	for ( auto &e : m_flagEntries )
		e.Clear();
	for ( auto &e : m_fastAccessGroups )
		e = 0;
	m_fastAccessAvailability = 0;
	m_groups.Clear();
}	

FeatureFlagsTechniques FeatureFlagsCompiler::GetCompiledFlagsByMaskImpl( Uint64 flagsMask ) const
{
	FeatureFlagsTechniques result;
	Uint64 requiredFlagsMask = 0;

	Uint32 compiledIndex = 0;
	FOR_BITSCAN_LOOP_BEGIN( flag_i, flagsMask );
	{
		const FlagEntry &entry = m_flagEntries[flag_i];
		result.m_techniquesMask |= entry.m_standaloneTechniquesMask;
		requiredFlagsMask |= entry.m_requiredFeatureFlagsMask;
		compiledIndex |= entry.m_compiledIndexMask;
	}
	FOR_BITSCAN_LOOP_END();

	if ( compiledIndex > 0 )
	{
		const CompressedGroup &compressedGroup = m_groups[compiledIndex];
		result.m_techniquesMask |= compressedGroup.m_techniquesMask;
		requiredFlagsMask |= compressedGroup.m_requiredFlagsMask;
	}

	if ( requiredFlagsMask != (requiredFlagsMask & flagsMask) )
	{
		result.Clear();
	}
		
	return result;
}

template< typename _Type, int _Capacity >
struct GuardedArray
{
	GuardedArray ( Uint32 size )
		: m_size ( size )
	{
		RED_ASSERT( size <= _Capacity );
	}

	_Type& operator[]( Uint32 idx )
	{
		RED_ASSERT( idx < m_size );
		return m_values[idx];
	}

	void Clear()
	{
		red::Memset( m_values, 0, sizeof(m_values) );
	}

	void* Ptr() 
	{
		return m_values;
	}

private:
	const Uint32 m_size;
	_Type m_values[_Capacity];
};

void FeatureFlagsCompiler::Init( Uint32 numTechniques, const TechniqueFeatures *techniques, Bool detectStandaloneTechniques )
{
	struct Local
	{		
		RED_INLINE static Uint32 NumBitsSet( Uint64 v )
		{
			Uint32 num = 0;
			FOR_BITSCAN_LOOP_BEGIN( i, v );
			{
				++num;
			}
			FOR_BITSCAN_LOOP_END();
			return num;
		}
	};

	RED_ASSERT( numTechniques <= 64 );		
	RED_ASSERT( FEATFLAG_MAX <= 64 );	

	Clear();

	// Builds flag->techniques mask
	GuardedArray<Uint64, FEATFLAG_MAX> mandatoryFlagTechniques ( FEATFLAG_MAX );
	for ( Uint32 flag_i=0; flag_i<FEATFLAG_MAX; ++flag_i )
	{
		mandatoryFlagTechniques[flag_i] = 0;
		for ( Uint32 tech_i=0; tech_i<numTechniques; ++tech_i )
		{
			const TechniqueFeatures &tech = techniques[tech_i];
			if ( tech.m_mandatoryFlags & (1ull << flag_i) )
				mandatoryFlagTechniques[flag_i] |= (1ull << tech_i);			
		}
	}

	// Build flags usage mask
	Uint64 flagsUsageMask = 0;
	for ( Uint32 flag_i=0; flag_i<FEATFLAG_MAX; ++flag_i )
	{
		if ( mandatoryFlagTechniques[flag_i] )
		{
			flagsUsageMask |= (1ull << flag_i);
		}
	}	

	// Build flag group mapping
	GuardedArray<Uint32, FEATFLAG_MAX> flagGroupMapping ( FEATFLAG_MAX );
	{
		flagGroupMapping[0] = 0;
		Int32 lastUsedFlagIndex = (flagsUsageMask & (1ull << 0)) ? 0 : -1;
		for ( Uint32 flag_i=1; flag_i<FEATFLAG_MAX; ++flag_i )
		{
			Uint32 currIndex = flagGroupMapping[flag_i-1];

			if ( (1ull << flag_i) & flagsUsageMask )
			{
				if ( -1 != lastUsedFlagIndex && (mandatoryFlagTechniques[lastUsedFlagIndex] != mandatoryFlagTechniques[flag_i]) )
				{
					++currIndex;
				}

				lastUsedFlagIndex = (Int32)flag_i;
			}

			flagGroupMapping[flag_i] = currIndex;
		}
	}
	const Uint32 numFlagGroups = flagGroupMapping[FEATFLAG_MAX - 1] + 1;

	// Build group flags mask
	GuardedArray<Uint64,FEATFLAG_MAX> groupFlagsMask ( FEATFLAG_MAX );
	groupFlagsMask.Clear();
	for ( Uint32 flag_i=0; flag_i<FEATFLAG_MAX; ++flag_i )
	{
		if ( flagsUsageMask & (1ull << flag_i) )
		{
			groupFlagsMask[ flagGroupMapping[flag_i] ] |= (1ull << flag_i);
		}
	}

	// Build group standalone technique masks
	GuardedArray<Uint64, FEATFLAG_MAX> groupStandaloneTechMasks( numFlagGroups );
	for ( Uint32 group_i=0; group_i<numFlagGroups; ++group_i )
	{		
		Int32 techIndex = -1;
		
		if ( detectStandaloneTechniques )
		{
			Bool isCovered = false;
			for ( Uint32 tech_i=0; tech_i<numTechniques; ++tech_i )
			{
				const TechniqueFeatures &tech = techniques[tech_i];
				const Uint64 techFlags = tech.m_mandatoryFlags;

				RED_ASSERT( groupFlagsMask[group_i] );

				if ( groupFlagsMask[group_i] != (techFlags & groupFlagsMask[group_i]) )
					continue;

				if ( isCovered )
				{
					techIndex = -1;
					break;
				}

				isCovered = true;

				if ( techFlags != groupFlagsMask[group_i] )
					continue;

				techIndex = (Int32)tech_i;
			}
		}

		groupStandaloneTechMasks[group_i] = -1 != techIndex ? (1ull << techIndex) : 0;
	}

	// Build groups compaction
	Uint32 numCompactedGroups = 0;
	GuardedArray<Int32, FEATFLAG_MAX> groupsCompaction ( numFlagGroups );
	for ( Uint32 group_i=0; group_i<numFlagGroups; ++group_i )
	{
		if ( groupStandaloneTechMasks[group_i] )
		{
			groupsCompaction[group_i] = -1;
		}
		else
		{
			groupsCompaction[group_i] = (Int32)numCompactedGroups;
			++numCompactedGroups;
		}
	}

	// Build compacted groups techniques
	GuardedArray<Uint64, FEATFLAG_MAX> compactedGroupsTechMaskMandatory ( FEATFLAG_MAX );
	compactedGroupsTechMaskMandatory.Clear();
	for ( Uint32 flag_i=0; flag_i<FEATFLAG_MAX; ++flag_i )
	{
		if ( !(flagsUsageMask & (1ull << flag_i)) )
			continue;
		const Uint32 group_i = flagGroupMapping[flag_i];
		const Int32 compacted_i = groupsCompaction[group_i];
		if ( -1 == compacted_i )
			continue;

		RED_ASSERT( (Uint32)compacted_i < numCompactedGroups );
		compactedGroupsTechMaskMandatory[compacted_i] |= mandatoryFlagTechniques[flag_i];
	}

	// Build techniques compacted group masks
	GuardedArray<Uint64, 64> techniquesCompactedGroupsMaskMandatory ( 64 );
	techniquesCompactedGroupsMaskMandatory.Clear();
	for ( Uint32 flag_i=0; flag_i<FEATFLAG_MAX; ++flag_i )
	{
		if ( !(flagsUsageMask & (1ull << flag_i)) )
			continue;
		const Uint32 group_i = flagGroupMapping[flag_i];
		const Int32 compacted_i = groupsCompaction[group_i];
		if ( -1 == compacted_i )
			continue;
		const Uint64 compactedMask = 1ull << compacted_i;
		
		for ( Uint32 tech_i=0; tech_i<numTechniques; ++tech_i )
		{
			const Uint64 techMask = 1ull << tech_i;
			if ( compactedGroupsTechMaskMandatory[compacted_i] & techMask )
				techniquesCompactedGroupsMaskMandatory[tech_i] |= compactedMask;
		}
	}	

	// Fill output standalone techniques mask
	for ( Uint32 flag_i=0; flag_i<FEATFLAG_MAX; ++flag_i )
	{
		if ( !(flagsUsageMask & (1ull << flag_i)) )
			continue;
		const Uint32 group_i = flagGroupMapping[flag_i];
		m_flagEntries[flag_i].m_standaloneTechniquesMask |= groupStandaloneTechMasks[group_i];
	}

	// Fill output flags mapping
	for ( Uint32 flag_i=0; flag_i<FEATFLAG_MAX; ++flag_i )
	{
		if ( !(flagsUsageMask & (1ull << flag_i)) )
			continue;
		const Uint32 group_i = flagGroupMapping[flag_i];
		const Int32 compacted_i = groupsCompaction[group_i];
		if ( -1 == compacted_i )
			continue;
		RED_ASSERT( compacted_i < 32 );
		m_flagEntries[flag_i].m_compiledIndexMask = (1 << compacted_i);
	}

	// Build techniques mask for compacted index
	if ( numCompactedGroups > 0 )
	{
		m_groups.Resize( (1 << numCompactedGroups) );
		RED_FATAL_ASSERT(  numCompactedGroups <= 12, "This is getting insane" );
		for ( Uint64 compacted_mask=0; compacted_mask<(1ull << numCompactedGroups); ++compacted_mask )
		{
			Uint64 bestTechniquesMask = 0;
			Uint64 bestGroupsRedundancy = (Uint64)-1;

			for ( Uint32 tech_i0=0; tech_i0<numTechniques; ++tech_i0 )
			for ( Uint32 tech_i1=tech_i0; tech_i1<numTechniques; ++tech_i1 )
			for ( Uint32 tech_i2=tech_i1; tech_i2<numTechniques; ++tech_i2 )
			{
				Uint32 numTestTechniques = 0;
				Uint32 testTechniques[8];
				testTechniques[numTestTechniques++] = tech_i0;
				if ( tech_i1 != tech_i0 )
					testTechniques[numTestTechniques++] = tech_i1;
				if ( tech_i2 != tech_i1 )
					testTechniques[numTestTechniques++] = tech_i2;

				Uint64 testMandatoryMask = 0;
				for ( Uint32 i=0; i<numTestTechniques; ++i )
				{ 
					testMandatoryMask |= techniquesCompactedGroupsMaskMandatory[ testTechniques[i] ];
				}

				if ( !compacted_mask )
					continue;
				if ( testMandatoryMask & (~compacted_mask) )
					continue;
				if ( compacted_mask != (compacted_mask & testMandatoryMask) )
					continue;

				Uint64 currTechniquesMask = 0;
				for ( Uint32 i=0; i<numTestTechniques; ++i )
				{
					currTechniquesMask |= 1ull << testTechniques[i];
				}

				Uint64 accumMandatoryMask = 0;
				Uint32 numRedundantGroups = 0;
				{
					Bool isValid = true;
					for ( Uint32 i=0; i<numTestTechniques; ++i )
					{
						const Uint64 currMandatoryMask = techniquesCompactedGroupsMaskMandatory[ testTechniques[i] ];
						
						// invalid if there is more than one pass for mandatory feature
						if ( accumMandatoryMask & currMandatoryMask )
						{
							isValid = false;
							break;
						}

						accumMandatoryMask |= currMandatoryMask;
					}			

					if ( !isValid )
						continue;
				}
			
				Uint64 currGroupsRedundancy = numRedundantGroups;
				if ( currGroupsRedundancy < bestGroupsRedundancy || (currGroupsRedundancy == bestGroupsRedundancy && Local::NumBitsSet(currTechniquesMask) < Local::NumBitsSet(bestTechniquesMask)) )
				{
					bestTechniquesMask = currTechniquesMask;
					bestGroupsRedundancy = currGroupsRedundancy;
				}
			}

			//
			FeatureFlagsCompiler::CompressedGroup &compressedGroup = m_groups[(Uint32)compacted_mask];

			// Set the techniques mask
			compressedGroup.m_techniquesMask = bestTechniquesMask;

			// Set the required feature flags
			RED_ASSERT( !compressedGroup.m_requiredFlagsMask );
			FOR_BITSCAN_LOOP_BEGIN( tech_i, bestTechniquesMask );
			{
				const TechniqueFeatures &tech = techniques[tech_i];
				compressedGroup.m_requiredFlagsMask |= tech.m_mandatoryFlags;
			}
			FOR_BITSCAN_LOOP_END();
		}
	}

	// Fill output required flags mask (for standalone flags)
	for ( Uint32 flag_i=0; flag_i<FEATFLAG_MAX; ++flag_i )
	{
		if ( !(flagsUsageMask & (1ull << flag_i)) )
			continue;
		
		const Uint32 group_i = flagGroupMapping[flag_i];
		const Uint64 techMask = groupStandaloneTechMasks[group_i];			
		RED_ASSERT( (-1 == groupsCompaction[group_i]) == (0 != techMask) );
		if ( 0 != techMask )
		{				
			Uint64 result = 0;

			FOR_BITSCAN_LOOP_BEGIN( tech_i, techMask );
			result |= techniques[tech_i].m_mandatoryFlags;
			FOR_BITSCAN_LOOP_END();
			
			m_flagEntries[flag_i].m_requiredFeatureFlagsMask = result;
			RED_ASSERT( 0 != (result & (1ull << flag_i)) );
		}
	}

	// Fill fast access groups
	for ( Uint32 flag_i=0; flag_i<FEATFLAG_MAX; ++flag_i )
	{
		Uint64 requiredFlagsMask = 0;
		Uint64 resultTechniquesMask = 0;
		Uint32 compiledIndex = 0;

		const FlagEntry &entry = m_flagEntries[flag_i];
		resultTechniquesMask |= entry.m_standaloneTechniquesMask;
		requiredFlagsMask |= entry.m_requiredFeatureFlagsMask;
		compiledIndex |= entry.m_compiledIndexMask;

		if ( compiledIndex > 0 )
		{
			const CompressedGroup &compressedGroup = m_groups[compiledIndex];
			resultTechniquesMask |= compressedGroup.m_techniquesMask;
			requiredFlagsMask |= compressedGroup.m_requiredFlagsMask;
		}
		
		const Uint64 flagsMask = 1ull << flag_i;
		if ( requiredFlagsMask == (requiredFlagsMask & flagsMask) )
		{
			m_fastAccessGroups[flag_i] = resultTechniquesMask;
			m_fastAccessAvailability |= 1ull << flag_i;
		}
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
