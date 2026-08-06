/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#include "build.h"
#include "scriptDataFormat.h"
#include "scriptDataLoaderPrivate.h"
#include "scriptDataObject.h"
#include "../../redFileSystem/include/file.h"

CScriptDataLoader::CScriptDataLoader( CScriptDataFormat& dataTables )
	: m_objects( red::PoolScript() )
	, m_names( red::PoolScript() )
	, m_tweakDBIDs( red::PoolScript() )
	, m_resRefs( red::PoolScript() )
	, m_data( &dataTables )
{
}

CScriptDataLoader::~CScriptDataLoader() = default;

Bool CScriptDataLoader::Load( IFile& file )
{
	// remember base file offset
	const Uint64 baseOffset = file.GetOffset();

	// load data tables
	if ( !m_data->Load( file ) )
	{
		return false;
	}

	// resolve names
	if ( !ResolveNames() )
	{
		return false;
	}

	// resolve tweakDBIDS
	if ( !ResolveTweakDBIDs() )
	{
		return false;
	}

	// resolve tweakDBIDS
	if ( !ResolveResRefs() )
	{
		return false;
	}

	// resolve objects
	if ( !ResolveObjects() )
	{
		return false;
	}

	// load internal objects data
	if ( !LoadObjects( file, baseOffset ) )
	{
		return false;
	}

	// objects loaded
	return true;
}

Bool CScriptDataLoader::ValidateHeader( IFile& file )
{
	return m_data->ValidateHeader( file );
}

CName CScriptDataLoader::MapName( const NameIndex index )
{
	RED_FATAL_ASSERT( index < m_names.Size(), "Invalid name index" );
	return m_names[ index ];
}

TweakDBID CScriptDataLoader::MapTweakDBID( const TweakDBIDIndex index )
{
	RED_FATAL_ASSERT( index < m_tweakDBIDs.Size(), "Invalid tweakDBID index" );
	return m_tweakDBIDs[ index ];
}

red::ResourceReferenceScriptToken CScriptDataLoader::MapResRef( const ResRefIndex index )
{
	RED_FATAL_ASSERT( index < m_resRefs.Size(), "Invalid resRef index" );
	return m_resRefs[ index ];
}

IScriptDataObject* CScriptDataLoader::MapObject( const ObjectIndex index )
{
	RED_FATAL_ASSERT( index < m_objects.Size(), "Invalid object index" );
	return m_objects[ index ];
}

void CScriptDataLoader::ReadData( void*, const Uint32 )
{
	RED_FATAL( "This function should not be called directly" );
}

Bool CScriptDataLoader::ResolveNames()
{
	// create table
	m_names.Resize( m_data->m_names.Size() );

	// create names
	for ( Uint32 i=0; i<m_data->m_names.Size(); ++i )
	{
		const auto& nameInfo = m_data->m_names[i];
		const AnsiChar* nameText = &m_data->m_strings[ nameInfo.m_string ];

		m_names[i] = RED_NAME( nameText );
	}

	// names resolved
	return true;
}

Bool CScriptDataLoader::ResolveTweakDBIDs()
{
	m_tweakDBIDs.Resize( m_data->m_tweakDBIDIs.Size() );

	for ( Uint32 i = 0; i < m_data->m_tweakDBIDIs.Size(); ++i )
	{
		const auto& tweakDBIDInfo = m_data->m_tweakDBIDIs[i];
		const AnsiChar* text = &m_data->m_strings[ tweakDBIDInfo.m_string ];
		m_tweakDBIDs[i] = TweakDBID( text );
	}

	return true;
}

Bool CScriptDataLoader::ResolveResRefs()
{
	m_resRefs.Resize( m_data->m_resRefs.Size() );

	for ( Uint32 i = 0; i < m_data->m_resRefs.Size(); ++i )
	{
		const auto& resRefInfo = m_data->m_resRefs[i];
		const AnsiChar* text = &m_data->m_strings[ resRefInfo.m_string ];
		m_resRefs[i] = red::ResourceReferenceScriptToken( text );
	}

	return true;
}

Bool CScriptDataLoader::ResolveObjects()
{
	// create objects
	m_objects.Resize( m_data->m_objects.Size() );
	for ( Uint32 i=1; i<m_data->m_objects.Size(); ++i )
	{
		const auto& info = m_data->m_objects[i];

		// get parent object
		IScriptDataObject* parentObject = MapObject( info.m_parent );

		// get object name
		const CName name = MapName( info.m_name );

		// get object type and create it
		IScriptDataObject* object = nullptr;
		const IScriptDataObject::EMetaType metaType = (const IScriptDataObject::EMetaType) info.m_type;
		switch (  metaType )
		{
			case IScriptDataObject::EMetaType::TypeRef: 
			{
				RED_FATAL_ASSERT( parentObject == nullptr, "Type ref objects are only valid at global scope" );
				object = RED_NEW( CScriptedDataTypeRef )( name );
				break;
			}

			case IScriptDataObject::EMetaType::Class:
			{
				RED_FATAL_ASSERT( parentObject == nullptr, "Class are only valid at global scope (for now)" );
				object = RED_NEW( CScriptedDataClass )( name );
				break;
			}

			case IScriptDataObject::EMetaType::NamedValue:
			{
				RED_FATAL_ASSERT( parentObject && (parentObject->GetMetaType() == IScriptDataObject::EMetaType::Enum || parentObject->GetMetaType() == IScriptDataObject::EMetaType::Bitfield), "NamedValues are only valid at enum/bitfield scope" );
				object = RED_NEW( CScriptedDataNamedValue )( name, parentObject );
				break;
			}

			case IScriptDataObject::EMetaType::Enum:
			{
				RED_FATAL_ASSERT( parentObject == nullptr, "Enums are only valid at global scope (for now)" );
				object = RED_NEW( CScriptedDataEnum )( name );
				break;
			}

			case IScriptDataObject::EMetaType::Bitfield:
			{
				RED_FATAL_ASSERT( parentObject == nullptr, "Bitfields are only valid at global scope (for now)" );
				object = RED_NEW( CScriptedDataEnum )( name );
				break;
			}

			case IScriptDataObject::EMetaType::Function:
			{
				RED_FATAL_ASSERT( parentObject == nullptr || parentObject->GetMetaType() == IScriptDataObject::EMetaType::Class ,"Functions are only allowed in global scope and classes" );
				CName familyName = CScriptedDataFunction::CreateFamilyName( name );
				// function access modifier was already checked so we can safely make it public
				object = RED_NEW( CScriptedDataFunction )( name, familyName, parentObject, IScriptDataObject::EVisibility::Public );
				break;
			}

			case IScriptDataObject::EMetaType::FunctionParam:
			{
				RED_FATAL_ASSERT( parentObject && parentObject->GetMetaType() == IScriptDataObject::EMetaType::Function,"Function parameters are only allowed in functions" );
				object = RED_NEW( CScriptedDataFunctionParam )( name, static_cast< CScriptedDataFunction* >( parentObject ) );
				break;
			}

			case IScriptDataObject::EMetaType::FunctionLocal:
			{
				RED_FATAL_ASSERT( parentObject && parentObject->GetMetaType() == IScriptDataObject::EMetaType::Function, "Function locals are only allowed in functions" );
				object = RED_NEW( CScriptedDataFunctionLocal )( name, static_cast< CScriptedDataFunction* >( parentObject ) );
				break;
			}

			case IScriptDataObject::EMetaType::Property:
			{
				RED_FATAL_ASSERT( parentObject && parentObject->GetMetaType() == IScriptDataObject::EMetaType::Class, "Properties are only allowed in classes" );
				// property access modifier was already checked so we can safely make it public
				object = RED_NEW( CScriptedDataProperty )( name, static_cast< CScriptedDataClass* >( parentObject ), IScriptDataObject::EVisibility::Public );
				break;
			}

			case IScriptDataObject::EMetaType::FileInfo:
			{
				RED_FATAL_ASSERT( parentObject == nullptr, "Script file objects are only valid at global scope" );
				object = RED_NEW( CScriptedDataFileInfo );
				break;
			}				
		}

		// object not resolved
		RED_FATAL_ASSERT( object != nullptr, "Object not resolved" );
		if ( !object )
			return false;

		// save in map
		m_objects[i] = object;
	}

	// objects resolved
	return true;
}

Bool CScriptDataLoader::LoadObjects( IFile& file, const Uint64 baseOffset )
{
	// create local wrapper
	class LocalFileWrapper : public IScriptDataLoader
	{
	public:
		LocalFileWrapper( CScriptDataLoader* baseLoader, IFile& file )
			: m_baseLoader( baseLoader )
			, m_file( &file )
		{}

		virtual ~LocalFileWrapper() override final = default;

		virtual CName MapName( const NameIndex index ) override final
		{
			return m_baseLoader->MapName( index );
		}

		virtual TweakDBID MapTweakDBID( const TweakDBIDIndex index ) override final
		{
			return m_baseLoader->MapTweakDBID( index );
		}

		virtual red::ResourceReferenceScriptToken MapResRef( const ResRefIndex index ) override final
		{
			return m_baseLoader->MapResRef( index );
		}

		virtual IScriptDataObject* MapObject( const ObjectIndex index ) override final
		{
			return m_baseLoader->MapObject( index );
		}

		virtual void ReadData( void* data, const Uint32 dataSize ) override final
		{
			m_file->Serialize( data, dataSize );
		}

	private:
		CScriptDataLoader*		m_baseLoader;
		IFile*					m_file;
	};

	// load content for each object (object 0 is NULL object)
	LocalFileWrapper fileWrapper( this, file );
	for ( Uint32 i=1; i<m_data->m_objects.Size(); ++i )
	{
		IScriptDataObject* object = m_objects[i];
		if ( object )
		{
			const auto& info = m_data->m_objects[i];

			// move to relative file position and load object data
			file.Seek( info.m_dataOffset + baseOffset );
			object->Load( fileWrapper );
		}
	}

	// objects loaded
	return true;
}

#ifndef RED_CONFIGURATION_FINAL

red::DynArray< res::ResourcePath > CScriptDataLoader::GetResourcePaths() const
{
	red::DynArray< res::ResourcePath > result{ red::PoolBackend() };
	result.Reserve( m_resRefs.Size() );

	for ( const auto resRef : m_resRefs )
	{
		result.PushBack( resRef.ToResourcePath() );
	}

	return result;
}

#endif
