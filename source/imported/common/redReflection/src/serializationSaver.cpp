/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#include "build.h"

#include "serializationSaver.h"
#include "serializationBinarySaver.h"
#include "../../redFileSystem/include/memoryFileWriter.h"

using red::StaticArray;

namespace serialization
{

	StaticArray< ISaver::FormatPair, ISaver::MAX_FORMATS > ISaver::st_formats;

	SavingContext::SavingContext( const SerializableHandle& singleObject )
		: m_gpuSize( 0 )
		, m_isCooker( false )
		, m_isCloner( false )
		, m_debugVerifyBufferCompression( false )
		, m_isGenerator( false )
		, m_magicFlagForTerrain( false )
		, m_magicFlagForAudioDefaultObject( false )
	{
		if ( singleObject )
		{
			auto ser = Cast< ISerializable >( singleObject );
			RED_ASSERT( ser != nullptr, "Object you are trying to save is not ISerializable. Most likely there's a mistake in your code." );
			m_initialExports.PushBack( ser );
		}
	}

	ISaver::ISaver()
	{
	}

	ISaver::~ISaver()
	{
	}

#ifndef NO_EDITOR
	Bool ISaver::SaveObjects( IFile& file, const SavingContext& context, job::Builder& builder, bool* saveResult ) const
	{
		return SaveObjects( file, context );
	}
#endif

	void ISaver::RegisterCustomFactory( const AnsiChar* ext, const TSaverFactory& factoryFunc )
	{
		RED_FATAL_ASSERT( !st_formats.Full(), "To many custom serialization formats registered" );

		FormatPair pair;
		pair.m_constImmutableExt = ext;
		pair.m_saverFactory = factoryFunc;
		st_formats.PushBack( pair );
	}

	SaverPtr ISaver::CreateSaver( const AnsiChar* formatExtension /*= nullptr*/ )
	{
		// try custom format
		if ( formatExtension && formatExtension[0] )
		{
			for ( const auto& it : st_formats )
			{
				if ( 0 == red::StrcmpNC( formatExtension, it.m_constImmutableExt ) )
				{
					return SaverPtr( it.m_saverFactory() );
				}
			}
		}

		// use binary serialization as default
		return SaverPtr( RED_NEW( BinarySaver )() );
	}

	const Bool SaveToMemory( const SavingContext& context, IFile& memoryWriter )
	{
		if( memoryWriter.IsWriter() )
		{
			BinarySaver saver;
			return saver.SaveObjects( memoryWriter, context );
		}

		return false;
	}

} // serialization