/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "cloningUtils.h"

#include "serializationSaver.h"
#include "serializationLoader.h"

#include "rttiProperty.h"

#include "../../redFileSystem/include/memoryFileWriter.h"
#include "../../redJobs2/include/jobCounter.h"
#include "../../redJobs2/include/jobSystem.h"


namespace tools
{
	THandle< ISerializable > CopyObject( const serialization::SavingContext & context )
	{
		// save to memory blob
		red::DynArray< Uint8 > buffer{ red::PoolBackend() };
		CMemoryFileWriter writer( buffer );

		auto saver = serialization::ISaver::CreateSaver();
		if ( saver->SaveObjects( writer, context ) )
		{
			// setup loading context
			serialization::LoadingContext context;
			context.m_replication = true;
			serialization::LoadingResult result;

			// deserialize the objects
			if ( serialization::LoadFromMemory( buffer.Data(), buffer.Size(), context, result, false ) )
			{
				THandle< ISerializable > object = result.m_loadedRootObjects[0];
				const rtti::ClassType * objectClassType = object->GetClass();
				objectClassType->RebuildParentHierarchy( object.Get(), nullptr );

				return object;
			}
		}
		return nullptr;
	}
	
	THandle< ISerializable > CloneObject( const THandle< ISerializable >& sourceObject )
	{
		serialization::SavingContext context( sourceObject );
		context.m_isCloner = true;
		return CopyObject( context );
	}

	THandle< ISerializable > CopyObject( const THandle< ISerializable >& sourceObject )
	{
		serialization::SavingContext context( sourceObject );
		context.m_isCloner = false;
		return CopyObject( context );
	}

	void CopyObjectProperties( const THandle< ISerializable >& sourceObject, const THandle< ISerializable >& destinationObject )
	{
		RED_FATAL_ASSERT( (sourceObject != nullptr) && (destinationObject != nullptr) );

		const rtti::ClassType* sourceClass = sourceObject->GetClass();
		const rtti::ClassType* destinationClass = destinationObject->GetClass();

		RED_FATAL_ASSERT( destinationClass->IsA( sourceClass ), "Unable to copy object properties, classes are incompatible!"
		                  "Source class is '%hs', destination class is '%hs'", sourceClass->GetName().AsChar(), destinationObject->GetClass()->GetName().AsChar() );

		const rtti::ClassType::TPropertyList& propertyList = sourceClass->GetCachedProperties();
		for ( const rtti::Property* prop : propertyList )
		{
			const rtti::IType* propertyType = prop->GetType();
			const void* propData = prop->GetOffsetPtr( sourceObject.Get() );
			void* destPropData = prop->GetOffsetPtr( destinationObject.Get() );

			propertyType->Copy( destPropData, propData );
		}
	}
}

namespace world
{
	CloneContext::CloneContext( const tools::EditorObjectIDPath& sourceObjectIDPath, const tools::EditorObjectIDPath& targetObjectIDPath, const red::DynArray< tools::EditorObjectIDPath >& sourceObjectIDPaths, const red::DynArray< tools::EditorObjectIDPath >& targetObjectIDPaths )
		: m_sourceObjectIDPath( sourceObjectIDPath )
		, m_targetObjectIDPath( targetObjectIDPath )
		, m_sourceObjectIDPaths( sourceObjectIDPaths )
		, m_targetObjectIDPaths( targetObjectIDPaths )
	{}
}
