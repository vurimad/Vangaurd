/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

enum ECookingPlatform : Uint8;

namespace red
{
	class PackageSerializer;
	struct PackageSerializeTypeParameter;
	class PackageTableOfContent;
	class PackageWriteStream;
	class PackageReadStream;
	class PackageErrorReporter;
	class IType;

	enum PackagePropertyFlags : Uint32;
	
	class RED_REFLECTION_API PackageTypeSerializer
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

	public:
		
		struct WriteContext
		{
			PackageSerializer & serializer;
			PackageWriteStream & stream;
			PackageTableOfContent & table;
			Uint32 propertyFlags;
			ECookingPlatform cookingPlatform;
		};

		struct ReadContext
		{
			PackageSerializer & serializer;
			PackageReadStream & stream;
			const PackageTableOfContent & table;
			Uint32 packageVersion;
			PackageErrorReporter & errorReporter;
			const memory::Pool & pool;
		};

		struct RemapContext
		{
			PackageSerializer & serializer;
			PackageReadStream & inputStream;
			PackageWriteStream & outputStream;
			PackageTableOfContent & table;
			Uint32 packageVersion;
			bool remapMissingProperty = false;
		};

		PackageTypeSerializer();
		virtual ~PackageTypeSerializer();

		RED_MOCKABLE void WriteValue( const WriteContext & context, const PackageSerializeTypeParameter & param ) const;
		RED_MOCKABLE void ReadValue( const ReadContext & context, const PackageSerializeTypeParameter & param ) const;
		RED_MOCKABLE void RemapValue( const RemapContext & context, const PackageSerializeTypeParameter & param ) const;

	private:

		virtual void OnWriteValue( const WriteContext & context, const PackageSerializeTypeParameter & param ) const = 0;
		virtual void OnReadValue( const ReadContext & context, const PackageSerializeTypeParameter & param ) const = 0;
		virtual void OnRemapValue( const RemapContext& context, const PackageSerializeTypeParameter & param ) const = 0;
	};

	RED_INLINE void PackageTypeSerializer::WriteValue( const WriteContext& context, const PackageSerializeTypeParameter& param ) const
	{
		OnWriteValue( context, param );
	}

	RED_INLINE void PackageTypeSerializer::ReadValue( const ReadContext& context, const PackageSerializeTypeParameter& param ) const
	{
		OnReadValue( context, param );
	}

	RED_INLINE void PackageTypeSerializer::RemapValue( const RemapContext& context, const PackageSerializeTypeParameter& param ) const
	{
		OnRemapValue( context, param );
	}
}
