/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once
#include "handle.h"

class ISerializable;
typedef THandle< ISerializable > SerializableHandle;

namespace red
{
	struct Package;

	class PackageReadStream;
	class PackageReader;
	class PackageTableOfContent;
	struct PackageTableReader;
	class PackageTypeSerializerDictionary;
	class PackageErrorReporter;
	
	class RED_REFLECTION_API PackageInspector
	{
	public:

		PackageInspector();
		PackageInspector( PackageInspector&& other );
		~PackageInspector();

		PackageInspector& operator=( PackageInspector && other );

		void Initialize( const Package & package );

		void ReadObject( ISerializable & object, Uint32 index ) const;

	private:

		void Swap( PackageInspector & swapWith );

		// ctremblay: NOTE - way too much dynamic allocation here. Mostly for Unit Testing purpose. Can be easily all removed.
		const Package * m_package;
		red::UniquePtr< PackageReader > m_packageReader;
		red::UniquePtr< PackageTableOfContent > m_tableOfContent;
		red::UniquePtr< PackageTableReader > m_table;
		red::UniquePtr< PackageReadStream > m_stream;
		red::UniquePtr< PackageTypeSerializerDictionary > m_dictionary;
		red::UniquePtr< PackageErrorReporter > m_errorReporter;
	};
}
