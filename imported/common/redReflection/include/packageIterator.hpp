/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once
#include "packageReader.h"
#include "packageTypeSerializerDictionary.h"
#include "packageErrorReporter.h"
#include "packageReadStream.h"

namespace red
{
	template< typename T >
	bool PackagePropertyIterator::ReadValue( T & result )
	{
		if( m_type == GetTypeObject< T >() )
		{
			const PropertyDescriptor& descriptor = m_propertyTable[ m_index ];
			red::UniquePtr< PackageReadStream > stream = CreateReadStream( m_propertyData.Range( descriptor.dataOffset ) );
			red::UniquePtr< PackageTypeSerializerDictionary > dictionary = CreatePackageTypeSerializerDictionary();
			PackageErrorReporter errorReporter;

			PackageReaderParameter readerParam = 
			{
				m_version,
				stream.Get(),
				m_table,
				dictionary.Get(),
				&errorReporter
			};

			red::UniquePtr< PackageReader > reader = CreatePackageReader( readerParam );

			*reader >> result;
		
			return true;
		}

		return false;
	}

	template< typename T >
	bool PackageObjectIterator::ReadObject( T& result )
	{
		if( m_type == GetTypeObject< T >() )
		{
			red::UniquePtr< PackageReadStream > stream = CreateReadStream( GetData() );
			red::UniquePtr< PackageTypeSerializerDictionary > dictionary = CreatePackageTypeSerializerDictionary();
			PackageErrorReporter errorReporter;

			PackageReaderParameter readerParam =
			{
				m_package->version,
				stream.Get(),
				&m_toc,
				dictionary.Get(),
				&errorReporter
			};

			red::UniquePtr< PackageReader > reader = CreatePackageReader( readerParam );

			*reader >> result;
		
			return true;
		}

		return false;
	}
}
