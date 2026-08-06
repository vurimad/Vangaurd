/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

#include "scriptDataLoader.h"

class CScriptDataLoader : public IScriptDataLoader
{
public:
	CScriptDataLoader( CScriptDataFormat& dataTables );
	virtual ~CScriptDataLoader() override final;

	// load data from given file
	Bool Load( IFile& file );

	Bool ValidateHeader( IFile& file );

	// get loaded objects
	RED_INLINE const red::DynArray< IScriptDataObject* >& GetObjects() const { return m_objects; }

#ifndef RED_CONFIGURATION_FINAL
	red::DynArray< res::ResourcePath > GetResourcePaths() const;
#endif 

private:
	// saving interface
	virtual CName MapName( const NameIndex index ) override final;
	virtual TweakDBID MapTweakDBID( const TweakDBIDIndex index ) override final;
	virtual red::ResourceReferenceScriptToken MapResRef( const ResRefIndex index ) override final;
	virtual IScriptDataObject* MapObject( const ObjectIndex index ) override final;
	virtual void ReadData( void* data, const Uint32 dataSize ) override final;

	// resolve loaded tables
	Bool ResolveNames();
	Bool ResolveTweakDBIDs();
	Bool ResolveResRefs();
	Bool ResolveObjects();

	// internal loading
	Bool LoadObjects( IFile& file, const Uint64 baseOffset );

	// created (loaded) objects
	red::DynArray< IScriptDataObject* >	m_objects;

	// created (resolved) names
	red::DynArray< CName >				m_names;

	// created (resolved) tweakDBIDs
	red::DynArray< TweakDBID >			m_tweakDBIDs;

	// created (resolved) resRefs
	red::DynArray< red::ResourceReferenceScriptToken > m_resRefs;

	// tables
	class CScriptDataFormat*			m_data;
};
