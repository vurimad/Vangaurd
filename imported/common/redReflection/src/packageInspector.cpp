/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "packageInspector.h"
#include "packageTableOfContent.h"
#include "package.h"
#include "packageReader.h"
#include "packageReadStream.h"
#include "packageTypeSerializerDictionary.h"
#include "packageUtils.h"
#include "packageSerializer.h"
#include "packageTableOfContentView.h"

#include "serializable.h"
#include "packageTableOfContentReader.h"
#include "packageErrorReporter.h"

namespace red
{
	PackageInspector::PackageInspector()
		: m_package( nullptr )
	{}

	PackageInspector::PackageInspector( PackageInspector&& other )
		: m_package( std::move( other.m_package ) )
		, m_stream( std::move( other.m_stream ) )
		, m_dictionary( std::move( other.m_dictionary ) )
	{}

	PackageInspector::~PackageInspector()
	{}

	PackageInspector & PackageInspector::operator=( PackageInspector && other )
	{
		PackageInspector( std::move( other ) ).Swap( *this );
		return *this;
	}

	void PackageInspector::Swap( PackageInspector & swapWith )
	{
		std::swap( m_package, swapWith.m_package );
		std::swap( m_stream, swapWith.m_stream );
		std::swap( m_dictionary, swapWith.m_dictionary );
	}

	void PackageInspector::Initialize( const Package & package )
	{
		m_package = &package;
		m_dictionary = CreatePackageTypeSerializerDictionary();
		m_stream = CreateReadStream( package.buffer );
		m_table = red::CreateUniquePtr< PackageTableReader >();
		m_table->objectTable.Resize( m_package->objectTable.Size() );
		m_tableOfContent = CreatePackageTableOfContentReader( *m_package, *m_table );
		m_errorReporter = red::CreateUniquePtr< PackageErrorReporter >();

		PackageReaderParameter param = 
		{
			m_package->version, 
			m_stream.Get(), 
			m_tableOfContent.Get(),
			m_dictionary.Get(),
			m_errorReporter.Get()
		};

		m_packageReader = CreatePackageReader( param );
	}

	void PackageInspector::ReadObject( ISerializable & object, Uint32 index ) const
	{
		RED_FATAL_ASSERT( index < m_table->objectTable.Size(), "Out of Bound Index." );
	
		SerializableHandle objectHandle = object.HandleFromThis();
		m_tableOfContent->UnmapObject( index, objectHandle );
		
		for( Uint32 pendingIndex = 0; pendingIndex != m_table->pendingObjectContainer.Size(); ++pendingIndex )
		{
			const Int32 pendingObject = m_table->pendingObjectContainer[ pendingIndex ];
			SerializableHandle & handle = m_table->objectTable[ pendingObject ];
			if( handle )
			{
				m_errorReporter->SetCurrentObject( handle );
				const ObjectDescriptor & descriptor = m_package->objectTable[ pendingObject ];
				m_stream->Seek( descriptor.dataOffset );
				m_packageReader->SerializeType( { handle.Get(), handle->GetClass(), nullptr } );
			}
		}

		m_errorReporter->BroadcastAllReportedErrors();
	}
}	
