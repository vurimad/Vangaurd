/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "package.h"
#include "packageTableOfContentView.h"
#include "packageVersion.h"
#include "../../redContainers/include/blob.h"

namespace rtti
{
	class IType;
	class ClassType;
}

namespace red
{
	struct Package;
	struct PropertyDescriptor;
	class PackageTableOfContent;
	class PackagePropertyView;

	typedef red::ArraySpan< const PropertyDescriptor > PackagePropertyTable;

	class RED_REFLECTION_API PackagePropertyIterator
	{
	public:
		PackagePropertyIterator( red::BlobView propertyData, PackageTableOfContent * table, PackagePropertyTable propertyTable, Uint32 version = c_packageCurrentVersion );
		~PackagePropertyIterator();

		CName GetName();
		const rtti::IType * GetType() const;
		const void * GetData() const;

		void Next();
		bool IsValid() const;
		bool IsObjectProperty() const;

		PackagePropertyView GetObjectPropertyView() const;

		template< typename T > 
		bool ReadValue( T& result );

	private:

		void ResolveContent();
		CName GetName( Uint32 tableIndex ) const;

		red::BlobView m_propertyData;
		PackageTableOfContent * m_table;
		PackagePropertyTable m_propertyTable;

		Uint32 m_index;
		CName m_name;
		const rtti::IType * m_type;
		const void * m_data;
		Uint32 m_version;
	};

	class RED_REFLECTION_API PackagePropertyView
	{
	public:
		PackagePropertyView( red::BlobView propertyData, PackageTableOfContent * table, Uint32 version = c_packageCurrentVersion );
		~PackagePropertyView();

		Uint32 GetCount() const;

		PackagePropertyIterator GetPropertyIterator() const;

	private:
		
		red::BlobView m_propertyData;
		PackageTableOfContent * m_table;
		PackagePropertyTable m_propertyTable;
		Uint32 m_version;
	};

	class RED_REFLECTION_API PackageObjectIterator
	{
	public:

		PackageObjectIterator( const Package & package, Bool root = false ); // pass true to iterate root objects
		~PackageObjectIterator();

		const rtti::ClassType * GetType() const;
		CName GetTypeName() const;
		Uint32 GetDataOffset() const;
		Uint32 GetDataSize() const;
		BlobView GetData() const;
		PackagePropertyView GetPropertyView() const;
		
		void Next();
		bool IsValid() const;
		bool IsTypeValid() const;

		PackagePropertyIterator FindProperty( const CName & propertyName ) const;

		void SetCurrentIndex( Uint32 index );
		Uint32 GetCurrentIndex() const;

		template< typename T >
		bool ReadObject( T& result );

	private:

		void ResolveContent();
		CName GetName( Uint32 tableIndex ) const;
		
		const Package* m_package;
		const Package::ObjectTable* m_iteratedTable;
		mutable PackageTableOfContentView m_toc;
		Uint32 m_index;
		const rtti::ClassType * m_type;
		CName m_typeName; 
	};

	class RED_REFLECTION_API PackageResourceIterator
	{
	public:

		PackageResourceIterator();
		PackageResourceIterator( const Package & package );
		~PackageResourceIterator();

		void SetPackage( const Package & package );

		Uint32 GetIndex() const;
		void SetIndex( Uint32 index );
		const res::ResourcePath & GetPath() const;
		PackageResourceImportType GetImportType() const;

		void Next();
		bool IsValid();

	private:

		void ResolveResource();

		const Package * m_package;
		Uint32 m_index;
		res::ResourcePath m_path;
		bool m_import;
		bool m_resourcePathStringDiscarded;
	};

	class RED_REFLECTION_API PackageNameIterator
	{
	public:
		PackageNameIterator( const Package& package );
		~PackageNameIterator();

		Uint32 GetIndex() const;
		CName GetName() const;

		void Next();
		bool IsValid();

	private:

		void ResolveName();

		const Package* m_package;
		Uint32 m_index;
		CName m_name;
	};
}

#include "packageIterator.hpp"
