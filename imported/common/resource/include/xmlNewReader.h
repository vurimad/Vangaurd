/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redReflection/include/types.h"

class IFile;

namespace xml 
{

	/// Structured XML reader
	/// Should be used to read more organized data, contains internal NODE STACK
	class RED_RESOURCE_API Reader
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

	public:
		virtual ~Reader();

		typedef std::function< void(void*) > TFreeFunction;

		// error message
		virtual void Error( STATIC_CHECK_PRINTF_MSC const AnsiChar* txt, ... ) const = 0;

		// nodes
		virtual Bool Open( const AnsiChar* name ) = 0;
		virtual Bool Iterate( const AnsiChar* name ) = 0;
		virtual Bool IterateNodes(const AnsiChar* name) = 0;
		virtual Bool IterateReverse( const AnsiChar* name ) = 0;
		virtual Bool Close() = 0;

		// attributes
		virtual Bool PeekAttribute( const AnsiChar* name, String& outValue ) const = 0;
		virtual Bool PeekAttribute( const AnsiChar* name, Float& outValue ) const = 0;
		virtual Bool PeekAttribute( const AnsiChar* name, Int32& outValue ) const = 0;
		virtual Bool PeekAttribute( const AnsiChar* name, Bool& outValue ) const = 0;

		// attributes with defaults
		virtual String GetAttribute( const AnsiChar* name, const String& defaultValue ) const = 0;
		virtual Float GetAttribute( const AnsiChar* name, const Float defaultValue ) const = 0;
		virtual Int32 GetAttribute( const AnsiChar* name, const Int32 defaultValue ) const = 0;
		virtual Bool GetAttribute( const AnsiChar* name, const Bool defaultValue ) const = 0;

		// get node value
		virtual Bool GetValue( String& outValue ) const = 0;
		virtual Bool GetValue( Float& outValue ) const = 0;
		virtual Bool GetValue( Int32& outValue ) const = 0;
		virtual Bool GetValue( Bool& outValue ) const = 0;

		// get node name
		virtual Bool GetName(String& outValue) const = 0;

		// create a reader from string held in the memory
		/// NOTE: data is still owned by the caller and should not be deleted while this object is used
		/// NOTE: lambda can be provided to free the memory
		/// NOTE: data buffer will be modified in order to parse the XML
		static red::UniquePtr<Reader> Create( AnsiChar* dataBuffer, TFreeFunction freeFunction );

		// create a reader from file content
		static red::UniquePtr<Reader> Create( IFile* file, const Uint32 sizeToRead = std::numeric_limits<Uint32>::max() );

		// Create a reader from an opened file directly from GFileManager
		static red::UniquePtr<Reader> Create( red::UniquePtr<IFile> file, const Uint32 sizeToRead = std::numeric_limits<Uint32>::max() );
	};

} // xml