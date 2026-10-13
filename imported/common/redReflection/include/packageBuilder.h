/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "package.h"
#include "packageTable.h"
#include "packageIterator.h"

namespace rtti { class IType; }

namespace red
{
	class PackageWriter;
	class PackageTableOfContent;
	class PackageTableOfContentBuilder;
	class PackageWriteStream;
	class PackageTypeSerializerDictionary;
	class PackageLayoutSaver;
	class PackageErrorReporter;

	enum PackagePropertyFlags : Uint32;
	
	struct PackageBuilderParameter
	{
		PackagePropertyFlags packagePropertyFlags = PackagePropertyType_All;
		ECookingPlatform cookingPlatform = PLATFORM_None;
		Uint32 memoryReserve = RED_KILO_BYTE( 4 );
		bool discardResourcePathString = false;
	};

	class RED_REFLECTION_API PackageBuilder
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

	public:
		PackageBuilder();
		PackageBuilder( PackageBuilder && other );
		RED_MOCKABLE ~PackageBuilder();

		PackageBuilder& operator=( PackageBuilder && other );

		void Initialize( const PackageBuilderParameter & param );

		Uint32 WriteObject( const ISerializable * object );
		Uint32 WriteObject( const ISerializable * object, const void * referenceObject );
		Uint32 WriteObject( Uint32 index, const Package & package );
				
		// OverrideObject will override object and ALL dependency for provided index.
		void OverrideObject( Uint32 index, const ISerializable* object );
		void OverrideObject( Uint32 index, const ISerializable* object, const void * referenceObject );
	
		void OverrideResourcePath( Uint32 index, const res::ResourcePath& path );

		Uint32 MergeObject( Uint32 index, Uint32 packageIndex, const red::Package & package );

		void RemoveObject( Uint32 index, bool allowRecycling = false );

		void AppendPackage( const Package & package );
		void WriteAndRemapObjects( const BitSetDynamic & mask, const Package & package );

		SerializableHandle ReadRootObject( Uint32 index ) const;
		SerializableHandle ReadObject( Uint32 index ) const;

		CompiledPackage BuildPackage() const;
		CompiledPackage BuildPackage( const PackageLayoutSaver & saver, Uint32 memoryReserve = RED_KILO_BYTE( 4 ) ) const;

		Uint32 GetObjectCount() const;
		Uint32 GetRootObjectCount() const;
		
		red::ArraySpan< const Int16 > GetRemovedRootObjectIndices() const;

		// For Unit Test only
		PackagePropertyView Internal_GetObjectPropertyView( Uint32 index ) const;
		bool Internal_IsObjectWrittenAtPosition( const SerializableHandle & handle, Uint32 index ) const;
		void Internal_SetPackageTable( red::UniquePtr< PackageTable > table );
		red::UniqueBuffer Internal_ReleaseWrittenBuffer();

	private:
		void WriteAllPendingObject();
		void Swap( PackageBuilder & swapWith );

		// ctremblay: Lots of dynamic allocation for not much reason but Unit Test. Can be optimized.
		red::UniquePtr< PackageTable > m_table;
		red::UniquePtr< PackageWriter > m_packageWriter;
		red::UniquePtr< PackageTableOfContentBuilder > m_tableBuilder;
		red::UniquePtr< PackageWriteStream > m_stream;
		red::UniquePtr< PackageTypeSerializerDictionary > m_dictionary;
		red::UniquePtr< PackageErrorReporter > m_errorReporter;
		red::DynArray< Int16 > m_removedRootObjects{ red::PoolEngine{} };
		ECookingPlatform m_cookingPlatform;
		bool m_discardResourcePathString;
	};

	RED_REFLECTION_API red::UniquePtr< PackageBuilder > CreatePackageBuilder( const PackageBuilderParameter & param  );
}
