/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

#include "scriptDataSaver.h"
#include "../../../common/redContainers/include/redContainersPublic.h"

/// File based saver for scripts data
class CScriptDataEnvironmentMapper : public IScriptDataSaver
{
public:
	CScriptDataEnvironmentMapper( class CScriptDataFormat& outData );
	virtual ~CScriptDataEnvironmentMapper() override final;

	// get internal data tables
	RED_INLINE CScriptDataFormat* GetDataTable() const { return m_data; }

	// get objects 
	RED_INLINE const red::DynArray< const IScriptDataObject* >& GetObjects() const { return m_objects; }

	// read only access
	NameIndex GetMappedNameIndex( const CName name ) const;
	TweakDBIDIndex GetMappedTweakDBIDIndex( const TweakDBID id ) const;
	ResRefIndex GetMappedResRefIndex( const red::ResourceReferenceScriptToken& resRef ) const;
	ObjectIndex GetMappedObjectIndex( const IScriptDataObject* object ) const;

private:
	// saving interface
	virtual NameIndex MapName( const CName name ) override final;
	virtual TweakDBIDIndex MapTweakDBID( const TweakDBID id ) override final;
	virtual ResRefIndex MapResRef( const red::ResourceReferenceScriptToken& resRef ) override final;
	virtual ObjectIndex MapObject( const IScriptDataObject* object ) override final;
	virtual void WriteData( const void* data, const Uint32 dataSize ) override final;

	// map string
	Uint32 MapString( const red::String& str );

	// mapped tables
	red::HashMap< red::String, Uint32 > m_mapStrings{ red::PoolScript() };
	red::HashMap< CName, NameIndex > m_mapNames{ red::PoolScript() };
	red::HashMap< TweakDBID, TweakDBIDIndex > m_mapTweakDBIDs{ red::PoolScript() };
	red::HashMap< red::String, ResRefIndex > m_mapResRefs{ red::PoolScript() };
	red::HashMap< const IScriptDataObject*, ObjectIndex > m_mapObjects{ red::PoolScript() };

	// objects to save
	red::DynArray< const IScriptDataObject* > m_objects{ red::PoolScript() };

	// output data (tables)
	CScriptDataFormat* m_data;
};

/// Final saver
class CScriptDataEnvironmentSaver
{
public:
	CScriptDataEnvironmentSaver( const CScriptDataEnvironmentMapper& map );
	~CScriptDataEnvironmentSaver();

	// save mapped objects to given file
	Bool Save( IFile& outputFile );

private:
	const CScriptDataEnvironmentMapper* m_map;
};
