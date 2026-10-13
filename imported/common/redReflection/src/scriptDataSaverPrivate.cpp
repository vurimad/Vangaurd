/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#include "build.h"

#include "scriptDataSaverPrivate.h"

#include "scriptDataFormat.h"
#include "scriptDataObject.h"
#include "resourceReferenceScriptToken.h"
#include "../../redFileSystem/include/file.h"

//--------------------------------------------

CScriptDataEnvironmentMapper::CScriptDataEnvironmentMapper( class CScriptDataFormat& outData )
	: m_data( &outData )
{
	// add empty string
	m_data->m_strings.Clear();
	m_data->m_strings.Reserve( 256*1024 );
	m_data->m_strings.PushBack( 0 ); // the empty string has index 0

	// add empty name
	m_data->m_names.Clear();
	m_data->m_names.Reserve( 16000 );

	CScriptDataFormat::Name emptyName;
	emptyName.m_string = 0;
	m_data->m_names.PushBack( emptyName );

	// add NULL object
	m_data->m_objects.Clear();
	m_data->m_objects.Reserve( 16000 );

	CScriptDataFormat::Object emptyObject;
	emptyObject.m_parent = 0;
	emptyObject.m_name = 0;
	emptyObject.m_dataOffset = 0;
	emptyObject.m_dataSize = 0;
	emptyObject.m_type = 0;
	m_data->m_objects.PushBack( emptyObject );

	m_objects.PushBack( nullptr );
}

CScriptDataEnvironmentMapper::~CScriptDataEnvironmentMapper() = default;

IScriptDataSaver::NameIndex CScriptDataEnvironmentMapper::GetMappedNameIndex( const CName name ) const
{
	// empty name
	if ( name.Empty() )
		return 0;

	// find in map
	NameIndex index = 0;
	if ( m_mapNames.Find( name, index ) )
		return index;

	// not mapped
	RED_FATAL( "Name '%hs' was not mapped and now its saved", name.AsChar() );
	return 0;
}

IScriptDataSaver::TweakDBIDIndex CScriptDataEnvironmentMapper::GetMappedTweakDBIDIndex( const TweakDBID id ) const
{
	if ( !id.IsValid() )
	{
		return 0;
	}

	TweakDBIDIndex index = 0;
	if ( m_mapTweakDBIDs.Find( id, index ) )
	{
		return index;
	}

	// not mapped
	RED_FATAL( "TweakDBID '%hs' was not mapped and now its saved", id.ToString().AsChar() );
	return 0;
}

IScriptDataSaver::ResRefIndex CScriptDataEnvironmentMapper::GetMappedResRefIndex( const red::ResourceReferenceScriptToken& resRef ) const
{
	auto path = resRef.ToResourcePath();
	if ( !path.IsValid() )
	{
		return 0;
	}

	ResRefIndex index = 0;
	if ( m_mapResRefs.Find( path.ToString(), index ) )
	{
		return index;
	}

	// not mapped
	RED_FATAL( "ResRef '%hs' was not mapped and now its saved", path.ToDebugString() );
	return 0;
}

IScriptDataSaver::ObjectIndex CScriptDataEnvironmentMapper::GetMappedObjectIndex( const IScriptDataObject* object ) const
{
	// empty object
	if ( !object )
		return 0;

	// find in map
	ObjectIndex index = 0;
	if ( m_mapObjects.Find( object, index ) )
		return index;

	// not mapped
	RED_FATAL( "Object '%hs' was not mapped and now its saved", object->GetName().AsChar() );
	return 0;
}

IScriptDataSaver::NameIndex CScriptDataEnvironmentMapper::MapName( const CName name )
{
	// empty name
	if ( name.Empty() )
		return 0;

	// find in map
	NameIndex index = 0;
	if ( m_mapNames.Find( name, index ) )
		return index;

	// map the string first
	CScriptDataFormat::Name nameInfo;
	nameInfo.m_string = MapString( name.AsChar() );

	// add the name to data tables
	index = m_data->m_names.Size();
	m_data->m_names.PushBack( nameInfo );

	// add new name to local map
	m_mapNames.Insert( name, index );
	return index;
}

IScriptDataSaver::TweakDBIDIndex CScriptDataEnvironmentMapper::MapTweakDBID( const TweakDBID id )
{
	if ( !id.IsValid() )
	{
		return 0;
	}

	TweakDBIDIndex index = 0;
	if ( m_mapTweakDBIDs.Find( id, index ) )
	{
		return index;
	}

	CScriptDataFormat::TweakDBID tweakDBIDInfo;
	tweakDBIDInfo.m_string = MapString( id.ToString() );

	// add tweakIDBID to data tables
	index = m_data->m_tweakDBIDIs.Size();
	m_data->m_tweakDBIDIs.PushBack( tweakDBIDInfo );

	// add new tweakDBID to local map
	m_mapTweakDBIDs.Insert( id, index );
	return index;
}

IScriptDataSaver::ResRefIndex CScriptDataEnvironmentMapper::MapResRef( const red::ResourceReferenceScriptToken& resRef )
{
	auto path = resRef.ToResourcePath();
	if ( !path.IsValid() )
	{
		return 0;
	}

	ResRefIndex index = 0;
	if ( m_mapResRefs.Find( path.ToString(), index ) )
	{
		return index;
	}

	CScriptDataFormat::ResRef resRefInfo;
	resRefInfo.m_string = MapString( path.ToString() );

	// add tweakIDBID to data tables
	index = m_data->m_resRefs.Size();
	m_data->m_resRefs.PushBack( resRefInfo );

	// add new tweakDBID to local map
	m_mapResRefs.Insert( path.ToString(), index );
	return index;
}

IScriptDataSaver::ObjectIndex CScriptDataEnvironmentMapper::MapObject( const IScriptDataObject* object )
{
	// empty object
	if ( !object )
		return 0;

	// find in map
	ObjectIndex index = 0;
	if ( m_mapObjects.Find( object, index ) )
		return index;

	// map parent object
	const ObjectIndex parentObjectIndex = MapObject( object->GetParent() );

	// map base object
	MapObject( object->GetBase() );

	// parent object mapping might already add this object
	if ( m_mapObjects.Find( object, index ) )
		return index;

	// MAP THE PARENT
	CScriptDataFormat::Object objectInfo;
	objectInfo.m_parent = parentObjectIndex;
	objectInfo.m_name = MapName( object->GetName() );
	objectInfo.m_type = static_cast< Uint16 >( object->GetMetaType() );
	objectInfo.m_dataSize = 0;
	objectInfo.m_dataOffset = 0;

	// store in the data tables
	index = m_data->m_objects.Size();
	m_data->m_objects.PushBack( objectInfo );

	// add to the local map
	m_mapObjects.Insert( object, index );

	// add to the list of the objects to SAVE
	m_objects.PushBack( object );

	// save object internals
	object->Save( *this );

	// return final object index
	return index;
}

void CScriptDataEnvironmentMapper::WriteData( const void*, const Uint32 )
{
	// nothing happens here
}

Uint32 CScriptDataEnvironmentMapper::MapString( const red::String& str )
{
	// empty string
	if ( str.Empty() )
		return 0;

	// find in map
	Uint32 index = 0;
	if ( m_mapStrings.Find( str, index ) )
		return index;

	// add to buffer
	index = m_data->m_strings.Size();
	m_data->m_strings.Grow( str.Length()+1 );
	red::Memcpy( &m_data->m_strings[index], str.Data(), str.DataSize() );

	// add to map
	m_mapStrings.Insert( str, index );
	return index;
}

//--------------------------------------------

CScriptDataEnvironmentSaver::CScriptDataEnvironmentSaver( const CScriptDataEnvironmentMapper& map )
	: m_map( &map )
{
}

CScriptDataEnvironmentSaver::~CScriptDataEnvironmentSaver() = default;

Bool CScriptDataEnvironmentSaver::Save( IFile& outputFile )
{
	// base offset for whole saving
	const Uint64 baseOffset = outputFile.GetOffset();

	// save the current header
	if ( !m_map->GetDataTable()->Save( outputFile ) )
		return false;

	// helper saver
	class LocalFileSaver : public IScriptDataSaver
	{
	public:
		LocalFileSaver( const CScriptDataEnvironmentMapper* map, IFile& file )
			: m_map( map )
			, m_file( &file )
		{}

		virtual ~LocalFileSaver() override final = default;

		virtual NameIndex MapName( const CName name ) override final
		{
			return m_map->GetMappedNameIndex( name );
		}

		virtual TweakDBIDIndex MapTweakDBID(const TweakDBID id) override final
		{
			return m_map->GetMappedTweakDBIDIndex( id );
		}

		virtual TweakDBIDIndex MapResRef( const red::ResourceReferenceScriptToken& resRef ) override final
		{
			return m_map->GetMappedResRefIndex( resRef );
		}

		virtual ObjectIndex MapObject( const IScriptDataObject* object ) override final
		{
			return m_map->GetMappedObjectIndex( object );
		}

		virtual void WriteData( const void* data, const Uint32 dataSize ) override final
		{
			m_file->Serialize( (void*)data, dataSize );
		}

	private:
		const CScriptDataEnvironmentMapper*		m_map;
		IFile*									m_file;
	};

	// build wrapper for saving
	LocalFileSaver fileWrapper( m_map, outputFile );

	// store the objects, update the object informations
	const auto& objects = m_map->GetObjects();
	for ( Uint32 index=1; index<objects.Size(); ++index )
	{
		// object saved data
		auto& objectData = m_map->GetDataTable()->m_objects[index];

		// update position of object
		objectData.m_dataOffset = (Uint32)( outputFile.GetOffset() - baseOffset );

		// save object data
		const auto* objectPtr = objects[index];
		objectPtr->Save( fileWrapper );

		// update object size
		objectData.m_dataSize = (Uint32)( outputFile.GetOffset() - objectData.m_dataOffset );
	}

	// update the tables
	const Uint64 currentOffset = outputFile.GetOffset();
	outputFile.Seek( baseOffset );
	if ( !m_map->GetDataTable()->Save( outputFile ) )
		return false;
	outputFile.Seek( currentOffset );

	// saved
	return true;
}

//--------------------------------------------
