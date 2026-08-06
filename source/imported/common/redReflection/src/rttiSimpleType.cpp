/**
* Copyright (c) 2007-16 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "rttiSimpleType.h"
#include "rttiValueHolder.h"
#include "stringSerialization.h"
#include "../../redFileSystem/include/file.h"
#include "rttiInternalTypeName.h"
#include "serializationUtils.h"
#include "stringRTTI.h"
#include "rttiFundamentalTypes.h"

/// GUID, CName and String are a little bit hacked here.... We will live with that, for now...

namespace temp
{
	//// TEMP, will be removed soon
	template < class T  >
	class TSimpleTypeRawSerialization : public rtti::IType
	{
	public:
		virtual ERTTITypeType GetType() const override
		{
			return RT_Simple;
		}

		virtual const CName GetName() const override final
		{
			return TTypeName< T >::GetTypeName(); // requires the RTTI_DECLARE_TYPE_NAME macro to be defined already
		}

		virtual Uint32 GetSize() const override final
		{
			return sizeof(T);
		}

		virtual Uint32 GetAlignment() const override final
		{
			return rtti::TTypeAlignment< T >::Alignment;
		}

		virtual void Construct( void* object ) const override final
		{
			new (object) T();
		}

		virtual void Destruct( void* data ) const override final
		{
			((T*) data)->~T();
		}

		virtual Bool Compare( const void* data1, const void* data2, Uint32 /*flags*/ ) const override final
		{
			return *(const T*) data1 == *(const T*) data2;
		}

		virtual void Copy( void* dest, const void* src ) const override final
		{
			*(T*) dest = *(const T*) src;
		}

		virtual Bool Serialize( IFile& file, void* data, ISerializable* owner = nullptr ) const override final
		{
			file << *(T*) data;
			return true;
		}

		virtual Bool NeedsCleaning() const override
		{		
			return false;
		}

		virtual const Bool WriteValue( IRTTIContext& ctx, void* data, const rtti::AccessPath& path, const rtti::ValueHolder& newValue, bool clone) const override
		{
			if ( rtti::IType::WriteValue( ctx, data, path, newValue, clone ) )
			{
				if ( newValue.IsEmpty() )
				{
					// if value holder is empty, rtti::IType Destructs() the object under data pointer,
					// but some classes that use TSimpleTypeRawSerialization don't have d-tors
					// - hence we Construct() default object
					Construct( data );
				}
				return true;
			}
			return false;
		}

		virtual const red::memory::Pool & GetInnerTypeMemoryPool() const override final { return red::PoolRTTI::GetInstance(); }
	};

	class SimpleTypeRawSerialization_CRUID : public TSimpleTypeRawSerialization< CRUID >
	{
	public:
		virtual Bool ToString( const void* data, String& valueString ) const override final
		{
			char buffer[ RED_RUID_STRING_BUFFER_SIZE ];
			const CRUID* ruid = static_cast<const CRUID*>( data );
			ruid->ToString( buffer, RED_RUID_STRING_BUFFER_SIZE );

			valueString = buffer;
			return true;
		}

		virtual Bool FromString( void* data, const String& valueString ) const override final
		{
			CRUID* ruid = static_cast<CRUID*>( data );
			return ruid->FromString( valueString.AsChar() );
		}
	};

	class SimpleTypeRawSerialization_CRUIDRef : public TSimpleTypeRawSerialization< CRUIDRef >
	{
	public:
		virtual Bool ToString( const void* data, String& valueString ) const override final
		{
			char buffer[ RED_RUID_STRING_BUFFER_SIZE ];
			const CRUIDRef* ruidRef = static_cast<const CRUIDRef*>( data );
			ruidRef->ToString( buffer, RED_RUID_STRING_BUFFER_SIZE );

			valueString = buffer;
			return true;
		}

		virtual Bool FromString( void* data, const String& valueString ) const override final
		{
			CRUIDRef* ruidRef = static_cast<CRUIDRef*>( data );
			return ruidRef->FromString( valueString.AsChar() );
		}
	};

	class SimpleTypeRawSerialization_CGUID : public TSimpleTypeRawSerialization< CGUID >
	{
	public:
		virtual Bool ToString( const void* data, String& valueString ) const override final
		{
			char buffer[ RED_GUID_STRING_BUFFER_SIZE ];
			const red::GUID* guid = static_cast< const red::GUID* >( data );
			guid->ToString( buffer, RED_GUID_STRING_BUFFER_SIZE );

			valueString = buffer;
			return true;
		}

		virtual Bool FromString( void* data, const String& valueString ) const override final
		{
			red::GUID* guid = static_cast< red::GUID* >( data );
			return guid->FromString( valueString.AsChar() );
		}
	};

	class SimpleTypeRawSerialization_CName : public TSimpleTypeRawSerialization< CName >
	{
	public:
		virtual ERTTITypeType GetType() const override final
		{
			return RT_Name;
		}

		virtual Bool ToString( const void* data, String& valueString ) const override final
		{
			const auto valueName = ((const CName*)data)->AsStringView();
			valueString = { valueName.Data(), valueName.Length() };
			return true;
		}

		virtual Bool FromString( void* data, const String& valueString ) const override final
		{
			*((CName*)data) = RED_NAME( valueString );
			return true;
		}
	};

	class SimpleTypeRawSerialization_String : public TSimpleTypeRawSerialization< String >
	{
	public:
		virtual Bool ToString( const void* data, String& valueString ) const override final
		{
			valueString = *reinterpret_cast<const String*>( data );
			return true;
		}

		virtual Bool FromString( void* data, const String& valueString ) const override final
		{
			*reinterpret_cast<String*>( data ) = valueString;
			return true;
		}

		virtual Bool NeedsCleaning() const override
		{
			return true;
		}
	};

	class SimpleTypeRawSerialization_TweakDBID : public TSimpleTypeRawSerialization< TweakDBID >
	{
	public:
		virtual Bool ToString( const void* data, String& valueString ) const override final
		{
			valueString = ( ( const TweakDBID* ) data )->ToString();
			return true;
		}

		virtual Bool FromString( void* data, const String& valueString ) const override final
		{
			*reinterpret_cast< TweakDBID* >( data ) = TweakDBID( valueString );
			return true;
		}
	};

} // temp

void RegisterTypeCGUID()
{ 
	RTTI_REGISTER_AUTO_TYPE_ALIAS( CGUID , temp::SimpleTypeRawSerialization_CGUID );
}

void RegisterTypeCRUID()
{
	RTTI_REGISTER_AUTO_TYPE_ALIAS( CRUID, temp::SimpleTypeRawSerialization_CRUID );
}

void RegisterTypeCRUIDRef()
{
	RTTI_REGISTER_AUTO_TYPE_ALIAS( CRUIDRef, temp::SimpleTypeRawSerialization_CRUIDRef );
}

void RegisterTypeCName()
{ 
	RTTI_REGISTER_AUTO_TYPE_ALIAS( CName, temp::SimpleTypeRawSerialization_CName );
}

void RegisterTypeString()
{ 
	RTTI_REGISTER_AUTO_TYPE_ALIAS( String, temp::SimpleTypeRawSerialization_String );
}

void RegisterTypeTweakDBID()
{ 
	RTTI_REGISTER_AUTO_TYPE_ALIAS( TweakDBID, temp::SimpleTypeRawSerialization_TweakDBID );
}
