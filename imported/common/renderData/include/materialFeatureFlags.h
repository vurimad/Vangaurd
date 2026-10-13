/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/
#pragma once


//////////////////////////////////////////////////////////////////////////

// !!!IMPORTANT !!!
// do not change values of already existing enums
enum EFeatureFlag : Uint8
{
	// Main flags
	FEATFLAG_Default,
	FEATFLAG_Shadows,
	FEATFLAG_HitProxies,
	FEATFLAG_Selection,
	FEATFLAG_Wireframe,

	// Additional flags
	FEATFLAG_VelocityBuffer,

	// DebugDraw flags
	FEATFLAG_DebugDraw_BlendOff,
	FEATFLAG_DebugDraw_BlendOn,

	FEATFLAG_DynamicDecals,
	FEATFLAG_Highlights,

	FEATFLAG_Overdraw,

	FEATFLAG_IndirectInstancedGrass,
	FEATFLAG_DecalsOnStaticObjects,
	FEATFLAG_DecalsOnDynamicObjects,

	FEATFLAG_MaskParticlesInsideCar,
	FEATFLAG_MaskParticlesInsideInterior,
	FEATFLAG_MaskTXAA,

	FEATFLAG_DistantShadows,
	FEATFLAG_FloatTracks,

	FEATFLAG_Rain,

	FEATFLAG_NumLights,

	FEATFLAG_DepthPrepass,

	//
	FEATFLAG_MAX
};

RTTI_DECLARE_ENUM( EFeatureFlag );

//////////////////////////////////////////////////////////////////////////

struct RENDER_DATA_API FeatureFlagsMask
{
	RTTI_DECLARE_TYPE( FeatureFlagsMask );

	RED_FORCE_INLINE FeatureFlagsMask ()
	{
		RED_STATIC_ASSERT( FEATFLAG_MAX <= 64 );
		ClearAll();
	}

	RED_FORCE_INLINE FeatureFlagsMask& Set( EFeatureFlag flag_i )
	{
		m_flags |= 1ull << flag_i;
		return *this;
	}

	RED_FORCE_INLINE FeatureFlagsMask& Clear( EFeatureFlag flag_i )
	{
		m_flags &= ~(1ull << flag_i);
		return *this;
	}

	RED_FORCE_INLINE FeatureFlagsMask& ClearAll()
	{
		m_flags = 0;
		return *this;
	}

	RED_FORCE_INLINE Bool IsAnySet() const
	{
		return 0 != m_flags;
	}

	RED_FORCE_INLINE Bool Get( EFeatureFlag flag_i ) const
	{
		return 0 != (m_flags & (1ull << flag_i));
	}

	RED_FORCE_INLINE Bool operator==( const FeatureFlagsMask& other ) const
	{
		return m_flags == other.m_flags;
	}

public:
	RED_FORCE_INLINE Uint64 InternalGetFlagsMask() const
	{
		return m_flags;
	}

private:
	Uint64		m_flags;
};

//////////////////////////////////////////////////////////////////////////

struct RENDER_DATA_API FeatureFlagsTechniques
{
	RED_FORCE_INLINE FeatureFlagsTechniques ()
	{
		Clear();
	}

	RED_FORCE_INLINE FeatureFlagsTechniques ( Uint64 techniquesMask )
		: m_techniquesMask ( techniquesMask )
	{}

	RED_FORCE_INLINE void Clear()
	{
		m_techniquesMask = 0;
	}

	RED_FORCE_INLINE Bool operator==( const FeatureFlagsTechniques &other ) const
	{
		return m_techniquesMask == other.m_techniquesMask;
	}

	RED_FORCE_INLINE Bool operator!=( const FeatureFlagsTechniques &other ) const
	{
		return !operator== ( other );
	}

	Uint64 m_techniquesMask;
};

//////////////////////////////////////////////////////////////////////////

struct RENDER_DATA_API TechniqueFeatures
{
	RED_FORCE_INLINE TechniqueFeatures ()
		: m_mandatoryFlags ( 0 )
	{}

	RED_FORCE_INLINE TechniqueFeatures& SetMandatory( Uint32 idx )
	{
		RED_ASSERT( idx < 64 );
		m_mandatoryFlags |= 1ull << idx;
		return *this;
	}

	Uint64	m_mandatoryFlags;
};

//////////////////////////////////////////////////////////////////////////

struct RENDER_DATA_API FeatureFlagsCompiler
{
	// - - - - - - - - - - - - -
	struct FlagEntry
	{
		FlagEntry ()
		{
			Clear();
		}

		void Clear()
		{
			m_requiredFeatureFlagsMask = 0;
			m_standaloneTechniquesMask = 0;
			m_compiledIndexMask = 0;
		}

		Uint64	m_requiredFeatureFlagsMask;
		Uint64	m_standaloneTechniquesMask;
		Uint32	m_compiledIndexMask;
	};
	// - - - - - - - - - - - - -
	struct CompressedGroup
	{
		CompressedGroup ()
		{
			Clear();
		}

		void Clear()
		{
			m_requiredFlagsMask = 0;
			m_techniquesMask = 0;
		}

		Uint64				m_requiredFlagsMask;		//< Mask for feature flags that need to be set so that we're able to resolve the request without including any unwanted feature (because flags are being grouped during compression)
		Uint64				m_techniquesMask;
	};
	// - - - - - - - - - - - - -

public:
	RED_FORCE_INLINE FeatureFlagsCompiler()
	{}

	void Init( Uint32 numTechniques, const TechniqueFeatures *techniques, Bool detectStandaloneTechniques = true );
	void Clear();

	FeatureFlagsTechniques GetCompiledFlags( Uint64 flagsMask ) const
	{
		const Bool isSingleBitSet = ((flagsMask != 0) && !(flagsMask & (flagsMask - 1)));
		if ( isSingleBitSet && (m_fastAccessAvailability & flagsMask) )
		{
			const Uint64 flagIndex = red::BitUtils::BitScanReverse<Uint64>( flagsMask );
			RED_FATAL_ASSERT( flagIndex < 64 );
			RED_FATAL_ASSERT( (1ull << flagIndex) == flagsMask );
			const FeatureFlagsTechniques result ( m_fastAccessGroups[flagIndex] );
			RED_FATAL_ASSERT( result == GetCompiledFlagsByMaskImpl( flagsMask ) );
			return result;
		}

		return GetCompiledFlagsByMaskImpl( flagsMask );
	}	

	RED_FORCE_INLINE FeatureFlagsTechniques GetCompiledFlags( FeatureFlagsMask flagsMask ) const
	{
		return GetCompiledFlags( flagsMask.InternalGetFlagsMask() );
	}	

private:
	FeatureFlagsTechniques GetCompiledFlagsByMaskImpl( Uint64 flagsMask ) const;
	
public:
	FlagEntry						m_flagEntries[FEATFLAG_MAX];
	Uint64							m_fastAccessGroups[FEATFLAG_MAX];
	Uint64							m_fastAccessAvailability;
	red::DynArray<CompressedGroup>	m_groups{ rend::PoolMaterialTechniques() };
};

//////////////////////////////////////////////////////////////////////////
