/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once
#include "rttiCommon.h"
#include "rttiType.h"
#include "packageTypeSerializer.h"

namespace rtti { class IType; }

namespace red
{
	class PackageTypeSerializer;

	typedef red::HashMap< const rtti::IType *, red::UniquePtr< PackageTypeSerializer > > PackageCustomSerializerDictionary;

	class RED_REFLECTION_API PackageTypeSerializerDictionary : red::NonCopyable
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

	public:

		PackageTypeSerializerDictionary();
		RED_MOCKABLE ~PackageTypeSerializerDictionary();

		void Initialize();

		RED_MOCKABLE const PackageTypeSerializer * FindTypeSerializer( const rtti::IType * type ) const;

	private:
		
		void RegisterAllNativeSerializer();
		void RegisterAllCustomSerializer();

		struct { Uint8 buffer[ RT_Count * 8 ]; } m_defaultSerializers; // ctremblay: this class will be hammered. I need to reduce memory jump. See code.
		
		PackageCustomSerializerDictionary * m_customSerializers;
	};

	RED_REFLECTION_API red::UniquePtr< PackageTypeSerializerDictionary > CreatePackageTypeSerializerDictionary(); // ctremblay: Do not need to be allocated. Just for UnitTest simplicity. Should be change if performance impact.
	RED_REFLECTION_API void PackageRegisterCustomTypeSerializer();

	RED_INLINE const PackageTypeSerializer* PackageTypeSerializerDictionary::PackageTypeSerializerDictionary::FindTypeSerializer( const rtti::IType* type ) const
	{
		RED_FATAL_ASSERT( type, "Cannot return a proper serializer for a null IType." );

		const ERTTITypeType rttiType = type->GetType();
		if( rttiType != RT_Simple )
		{
			const void* serializerOffset = m_defaultSerializers.buffer + ( rttiType * sizeof( PackageTypeSerializer ) );
			if( *static_cast< const Uint32* >( serializerOffset ) != 0 )
			{
				return static_cast< const PackageTypeSerializer* >( serializerOffset );
			}
		}

		if( m_customSerializers )
		{
			auto iter = m_customSerializers->Find( type );
			return iter != m_customSerializers->End() ? iter.Value().Get() : nullptr;
		}

		return nullptr;
	}
}
