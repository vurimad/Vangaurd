/*
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */
#pragma once

#ifdef RED_PLATFORM_ORBIS

#include <playgo.h>

namespace playgo {

class ModuleHandle
{
public:
	ModuleHandle();
	~ModuleHandle();

	ModuleHandle( const ModuleHandle& ) = delete;
	ModuleHandle( ModuleHandle&& ) = delete;
	ModuleHandle& operator=( const ModuleHandle& ) = delete;
	ModuleHandle& operator=( ModuleHandle&& ) = delete;

	bool Load();
	void Unload();
	bool IsLoaded();

private:
	bool m_loaded;
};

class System
{
public:
	System();
	~System();

	System( const System& ) = delete;
	System( System&& ) = delete;
	System& operator=( const System& ) = delete;
	System& operator=( System&& ) = delete;

	bool Initialise();
	void Shutdown();
	bool IsInititalised() const;

private:
	red::UniqueBuffer m_playGoHeap;
	bool m_initialised;
};

class Handle
{
public:
	Handle();
	~Handle();

	Handle( const Handle& ) = delete;
	Handle( Handle&& ) = delete;
	Handle& operator=( const Handle& ) = delete;
	Handle& operator=( Handle&& ) = delete;

	bool Open();
	void Close();
	bool IsOpen() const;

	::ScePlayGoHandle Get() const;

private:
	::ScePlayGoHandle m_handle;
	bool m_opened;
};

::ScePlayGoLanguageMask GetLanguageMask( const Handle& handle );
red::DynArray< Int8 > GetLocusForChunks( const Handle& handle, const red::ArraySpan< Uint16 >& chunkIds, Int32 &systemErrorCode );

} // namespace playgo

namespace res
{
namespace prv
{

class ArchiveLanguageMap;

red::DynArray< red::String > GetInstalledLanguages( prv::ArchiveLanguageMap &languageMap );
Bool RefreshInstalledLanguages( prv::ArchiveLanguageMap &languageMap );

} // namespace prv
} // namespace res

#endif // RED_PLATFORM_ORBIS