/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once 

#include "materialTemplate.h"

#ifndef NO_RUNTIME_MATERIAL_COMPILATION

class IShaderCache;

namespace tick
{
	struct Info;
}

struct AsyncMaterialCompilationEntry
{
	enum EAsyncMaterialCompilationState
	{
		EAMCS_Pending,
		EAMCS_Scheduled,
		EAMCS_Compiling,
		EAMCS_Compiled,
		EAMCS_Merged,
		EAMCS_CleanedUp,
	};

	AsyncMaterialCompilationEntry()
		: m_state( EAsyncMaterialCompilationState::EAMCS_Pending )
		, m_isValid( false )
	{}

	THandle< CMaterialTemplate >	m_material;
	red::AbsolutePath				m_cachePath;

	red::DynArray< Uint32 >			m_techniques { rend::PoolShaderCompilation() };

	EAsyncMaterialCompilationState	m_state;
	Bool							m_isValid;
};

struct MaterialRecompilationInfo
{
	res::ResourcePath												path;
	AsyncMaterialCompilationEntry::EAsyncMaterialCompilationState	state;
	Bool															result;
	Bool															isHighPriority;

	static Color MapStateToColor(const AsyncMaterialCompilationEntry::EAsyncMaterialCompilationState state, const Bool result)
	{
		switch (state)
		{
		case AsyncMaterialCompilationEntry::EAMCS_Pending:		return Color::WHITE();
		case AsyncMaterialCompilationEntry::EAMCS_Scheduled:	return Color::BLUE();
		case AsyncMaterialCompilationEntry::EAMCS_Compiling:	return Color::YELLOW();
		case AsyncMaterialCompilationEntry::EAMCS_Compiled:		return result ? Color::GREEN() : Color::RED();
		case AsyncMaterialCompilationEntry::EAMCS_Merged:		return result ? Color::DARK_GREEN() : Color::DARK_RED();
		case AsyncMaterialCompilationEntry::EAMCS_CleanedUp:	return result ? Color::BLUE() : Color::DARK_RED();
		}
		return Color::WHITE();
	}
};

typedef red::HashMap< res::ResourcePath, AsyncMaterialCompilationEntry > TAsyncMaterialCollection;

class RESOURCEMATERIAL_API MaterialCompilationManager : red::NonCopyable
{
	RED_USE_MEMORY_POOL( rend::PoolShaderCompilation );

	static const Uint8 optimalBatchSize = 5;

public:
	MaterialCompilationManager();

	virtual ~MaterialCompilationManager();

public:
	RED_INLINE Bool IsRunning() const { return m_isRunning; }

public:
	void Initialize();
	void Shutdown();


	void Register( const THandle< CMaterialTemplate >& material, Bool onlyHighPriority = false );
	void Run( Bool includesMatch );
	void Tick( const Float engineTimeDelta, const tick::Info& tickInfo );

	void GetMaterialsBeingRecompiled( red::DynArray< MaterialRecompilationInfo >& materialsPaths ) const;

private:
	Bool MergeFinishedMaterial( AsyncMaterialCompilationEntry& material, IShaderCache* cache );
	void CleanupFinishedMaterial( AsyncMaterialCompilationEntry& material );

	Bool PrepareNextBatch();
	void RunPreparedBatch();
	void FlushFinishedBatch();

private:
	Bool										m_isRunning;
	Bool										m_forceUnroll;
	red::Atomic< Int32 >						m_currentBatchCounter;

	TAsyncMaterialCollection					m_asyncMaterialsCollection_HighPriority { rend::PoolShaderCompilation() };
	TAsyncMaterialCollection					m_asyncMaterialsCollection_LowPriority { rend::PoolShaderCompilation() };
	
	red::AbsolutePath							m_shaderCachePath;
	CDateTime									m_startTime;
	CDateTime									m_endTime;
};

RESOURCEMATERIAL_API MaterialCompilationManager& GetMaterialCompilationManager();

#endif // NO_RUNTIME_MATERIAL_COMPILATION