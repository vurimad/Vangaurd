/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "packageTypeSerializerDictionary.h"
#include "packageTypeSerializer.h"

#include "packageNameSerializer.h"
#include "packageArraySerializer.h"
#include "packageFundamentalSerializer.h"
#include "packageObjectSerializer.h"
#include "packageEnumSerializer.h"
#include "packageResourceSerializer.h"
#include "packageHandleSerializer.h"
#include "packageCustomTypeSerializer.h"
#include "packageBitFieldSerializer.h"

#include "rttiCommon.h"
#include "rttiType.h"
#include "rttiClass.h"
#include "rttiSystem.h"
#include "singleChannelCurve.h"

namespace red
{
	struct PackageCustomSerializerDictionaryProxy
	{
		PackageCustomSerializerDictionaryProxy()
			: dictionary( PoolEngine() )
		{
			red::DynArray< const rtti::ClassType* > customSerializerClassContainer{ red::PoolEngine() };
			const rtti::ClassType * customSerializerBaseClass = PackageCustomTypeSerializer::GetStaticClass();
			GetRttiSystem().EnumDerivedClasses( customSerializerBaseClass, customSerializerClassContainer );
	
			dictionary.Reserve( customSerializerClassContainer.Size() );

			for( const rtti::ClassType * customSerializerClass : customSerializerClassContainer )
			{
				red::UniquePtr< PackageCustomTypeSerializer > serializer( customSerializerClass->CreateObject< PackageCustomTypeSerializer >() );
				const rtti::IType * key = serializer->GetType();
				RED_FATAL_ASSERT( !dictionary.KeyExist( key ), "Cannot have more than one custom serializer for any given type." );
				dictionary[ key ] = std::move( serializer );
			}
		}
		
		PackageCustomSerializerDictionary dictionary;
	};

	template< typename T >
	void RegisterGenericTypeSerializer( Uint8 * inplaceBuffer, ERTTITypeType key )
	{
		static_assert( std::is_base_of< PackageTypeSerializer, T >::value, "Default Serializer Type must be base of PackageTypeSerializer." );
		static_assert( sizeof( T ) == sizeof( PackageTypeSerializer ), "Default Type serializer need to be stateless." );
		::new( inplaceBuffer + ( key * sizeof( PackageTypeSerializer ) ) ) T();
	}

	PackageTypeSerializerDictionary::PackageTypeSerializerDictionary()
		: m_defaultSerializers()
		, m_customSerializers( nullptr )
	{
	}

	PackageTypeSerializerDictionary::~PackageTypeSerializerDictionary()
	{
	}

	void PackageTypeSerializerDictionary::Initialize()
	{
		RegisterAllNativeSerializer();
		RegisterAllCustomSerializer();
	}

	void PackageTypeSerializerDictionary::RegisterAllNativeSerializer()
	{
		RegisterGenericTypeSerializer< PackageNameSerializer >( m_defaultSerializers.buffer, RT_Name );
		RegisterGenericTypeSerializer< PackageFundamentalSerializer >( m_defaultSerializers.buffer, RT_Fundamental );
		RegisterGenericTypeSerializer< PackageObjectSerializer >( m_defaultSerializers.buffer, RT_Class );
		RegisterGenericTypeSerializer< PackageArraySerializer >( m_defaultSerializers.buffer, RT_Array );
		RegisterGenericTypeSerializer< PackageArraySerializer >( m_defaultSerializers.buffer, RT_StaticArray );
		RegisterGenericTypeSerializer< PackageArraySerializer >( m_defaultSerializers.buffer, RT_NativeArray );
		RegisterGenericTypeSerializer< PackageEnumSerializer >( m_defaultSerializers.buffer, RT_Enum );
		RegisterGenericTypeSerializer< PackageResourceSerializer >( m_defaultSerializers.buffer, RT_ResourceReference );
		RegisterGenericTypeSerializer< PackageAsyncResourceSerializer >( m_defaultSerializers.buffer, RT_ResourceAsyncReference );
		RegisterGenericTypeSerializer< PackageHandleSerializer >( m_defaultSerializers.buffer, RT_Handle );
		RegisterGenericTypeSerializer< PackageWeakHandleSerializer >( m_defaultSerializers.buffer, RT_WeakHandle );
		RegisterGenericTypeSerializer< PackageBitFieldSerializer >( m_defaultSerializers.buffer, RT_BitField );
		RegisterGenericTypeSerializer< PackageSingleChannelCurveSerializer >( m_defaultSerializers.buffer, RT_LegacySingleChannelCurve );
	}

	void PackageTypeSerializerDictionary::RegisterAllCustomSerializer()
	{
		static PackageCustomSerializerDictionaryProxy s_packageCustomSerializerDictionaryProxy;
		m_customSerializers = &s_packageCustomSerializerDictionaryProxy.dictionary;
	}

	red::UniquePtr< PackageTypeSerializerDictionary > CreatePackageTypeSerializerDictionary()
	{
		red::UniquePtr< PackageTypeSerializerDictionary > dictionary = red::CreateUniquePtr< PackageTypeSerializerDictionary >();
		dictionary->Initialize();
		return dictionary;
	}

}
