/**
* Copyright (c) 2007-16 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "serializationUtils.h"
#include "serializationMapping.h"
#include "stringSerialization.h"
#include "rttiSystem.h"


#include "../../redContainers/include/string/stringBuffer.h"
#include "../../redFileSystem/include/file.h"
#include "../../redFileSystem/include/fileVersionList.h"
#include "../../redFileSystem/include/compressedNumSerializer.h"
#include "resourceReferenceScriptToken.h"

namespace
{
	constexpr Uint32 c_stringReadBufferSize = 512;
}

namespace serialization
{

	void SerializeName( IFile& file, class CName &name )
	{
		if ( file.HasMapper() )
		{
			if ( file.IsReader() )
			{
				IMapper::NameIndex index = 0;
				file << index;

				file.GetMapper()->UnmapName( index, name );
			}
			else if ( file.IsWriter() )
			{
				IMapper::NameIndex index = 0;
				file.GetMapper()->MapName( name, index );

				file << index;
			}
		}
		else
		{
			if ( file.IsWriter() )
			{
				const auto buf = name.AsStringView();
				const auto countNegative = -Int32( buf.Size() );

				WriteVarint_LEB128_Signed( file, countNegative );
				file.Serialize( const_cast< char* >( buf.Data() ), buf.Size() * sizeof( AnsiChar ) );
			}
			else if ( file.IsReader() )
			{
				red::StringBuffer< c_stringReadBufferSize > uniName;
				file << uniName.AsArraySpan();

				// Initialize from string
				name = RED_NAME( uniName.AsStringView() ); // FIXME call to AsStringView is recalculating length which is available at read time
			}
		}
	}

	void SerializeTypeRef( IFile& file, const rtti::IType*& typeRef )
	{
		if ( file.HasMapper() )
		{
			if ( file.IsReader() )
			{
				IMapper::TypeIndex index = 0;
				file << index;

				file.GetMapper()->UnmapType( index, static_cast< const rtti::IType*& >( typeRef ) );
			}
			else if ( file.IsWriter() )
			{
				IMapper::TypeIndex index = 0;
				file.GetMapper()->MapType( typeRef, index );

				file << index;
			}
		}
		else
		{
			if ( file.IsReader() )
			{
				// load type name
				CName typeName;
				SerializeName( file, typeName );

				// find type
				typeRef = GetRttiSystem().FindType( typeName );
			}
			else if ( file.IsWriter() )
			{
				// save type name
				CName typeName = typeRef ? typeRef->GetName() : CName();
				SerializeName( file, typeName );
			}
		}
	}

	// TODO: this is going away, we are switching to handles
	void SerializePointer( IFile& file, const rtti::ClassType* pointerClass, void*& serializablePtr )
	{
		if ( file.HasMapper() )
		{
			if ( file.IsReader() )
			{
				IMapper::ObjectIndex index = 0;
				file << index;

				THandle< ISerializable > object;
				file.GetMapper()->UnmapPointer( index, object );

				if ( object && object->IsA( pointerClass ) )
					serializablePtr = object.Get();
				else
					serializablePtr = nullptr;
			}
			else if ( file.IsWriter() )
			{
				THandle< ISerializable > object;
				if ( serializablePtr && pointerClass->IsSerializable() )
					object.Reset( static_cast< ISerializable* >( serializablePtr ) );

				IMapper::ObjectIndex index = 0;
				file.GetMapper()->MapPointer( object, index );

				file << index;
			}
		}
		else
		{
			// nothing, pointers are NOT saved
		}
	}

	void SerializeGUID( IFile& file, CGUID& guid )
	{
		static_assert( sizeof( guid ) == 16, "" );
		file.Serialize( &guid, sizeof( guid ) );
	}

	namespace prv
	{
		// For friends with RUID, less messy than friend function with DLL linkages
		class SerializeRUIDHelper
		{
		public:
			static RED_FORCE_INLINE void SerializeRUID( IFile& file, CRUID& ruid )
			{
				if (file.IsWriter())
				{
					if (ruid.IsTransient())
					{
						RED_FATAL( "Cannot serialize transient RUID!" );
						// Make sure don't write out the transient value
						Uint64 zero = 0;
						file << zero;
						return;
					}
					file << ruid.m_value;
				}
				else
				{
					// Go through ctor for additional validation. #tbd: if shipping build, could just serialize directly if needed
					Uint64 value = 0;
					file << value;
					ruid = CRUID( value );
					RED_FATAL_ASSERT( !ruid.IsTransient(), "Cannot transient RUID!" );
				}
			}

			static RED_FORCE_INLINE void SerializeRUIDRef( IFile& file, CRUIDRef& ruidRef )
			{
				SerializeRUID( file, ruidRef.m_ruid );
			}
		};
	}

	void SerializeRUID( IFile& file, CRUID& ruid )
	{
		prv::SerializeRUIDHelper::SerializeRUID( file, ruid );
	}

	void SerializeRUIDRef( IFile& file, CRUIDRef& ruidRef )
	{
		prv::SerializeRUIDHelper::SerializeRUIDRef( file, ruidRef );
	}

	void SerializeTweakDBID( IFile& file, game::data::TweakDBID& tweakDBID )
	{
		if ( file.IsWriter() )
		{
			Uint64 hashNum = tweakDBID.ToNumber();
			file << hashNum;
		}
		else if ( file.IsReader() )
		{
			// backwards compatibility for string based serialization
			if ( file.GetVersion() < VER_TWEAKDB_ID_HASH_SERIALIZATION )
			{
				red::StringBuffer< c_stringReadBufferSize > uniName;
				file << uniName.AsArraySpan();

				// Initialize from string
				tweakDBID = TDBID( uniName.AsChar() ); // TODO use length
			}
			else
			{
				Uint64 id;
				file.Serialize( &id, sizeof( Uint64 ) );
				tweakDBID = TweakDBID::FromNumber( id );
			}
		}
	}

} // serialization