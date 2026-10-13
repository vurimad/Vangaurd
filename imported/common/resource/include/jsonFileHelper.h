/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/

#pragma once
#ifndef __JSON_FILE_H__
#define __JSON_FILE_H__

// TODO: remove this nasty dependency on JSON crap once engine implements proper json support
#include "rapidjson.h"

// JSON based file crap class
namespace JSONFileHelper
{
	// stream out writer for JSON
	template< Uint32 BUF_SIZE >
	class JSONStreamWriter
	{
	public:
		typedef AnsiChar Ch;

		JSONStreamWriter(IFile* file)
			: m_file(file)
			, m_pos(0)
		{}

		~JSONStreamWriter()
		{
			Flush();
		}

		void Flush()
		{
			if ( m_pos > 0 )
			{
				m_file->Serialize( m_buf, m_pos );
				m_pos = 0;
			}
		}

		void Put(const AnsiChar ch)
		{
			if (m_pos == BUF_SIZE)
				Flush();

			m_buf[ m_pos++ ] = ch;
		}

	private:
		IFile*		m_file;

		AnsiChar	m_buf[ BUF_SIZE ];
		Uint32		m_pos;
	};

	// #note: if used with the JSON pool allocator, then MUST set the chunk size to match the buffer size
	// or else it'll assume 64K and crash.
	class FixedAllocator
	{
	public:
		FixedAllocator()
			: m_buffer( nullptr )
			, m_size( 0 )
		{}

		FixedAllocator( void* buffer, Uint32 size )
			: m_buffer( static_cast< Uint8* >( buffer ) )
			, m_size( size )
		{}

	public:
		static const bool kNeedFree = false;
		void* Malloc( size_t size )
		{ 
			RED_FATAL_ASSERT( m_size >= size );
			void* ret = nullptr;
			if ( size <= m_size )
			{
				ret = m_buffer;
				m_buffer += size;
				m_size -= static_cast< Uint32 >( size );
			}
			return ret;
		}
		void* Realloc( void* originalPtr, size_t originalSize, size_t newSize ) 
		{ 
			if ( originalSize >= newSize )
			{
				return originalPtr;
			}
			return Malloc( newSize );
		}

		static void Free( void *ptr )
		{ 
			RED_UNUSED(ptr);
		}

	private:
		Uint8* m_buffer;
		Uint32 m_size;
	};

	using JSONDocument = rapidjson::JSONDocument;
	using JSONValue = rapidjson::JSONValue;

	// TODO: move the writer interface to Core
	typedef JSONStreamWriter< 4096 > JSONFileStream;
	typedef rapidjson::PrettyWriter< JSONFileStream, rapidjson::UTF8< AnsiChar >, rapidjson::UTF8< AnsiChar >, res::json::JSONAllocator > JSONWriter;

	using JSONCompactWriter = rapidjson::Writer< JSONFileStream, rapidjson::UTF8<>, rapidjson::UTF8<>, rapidjson::MemoryPoolAllocator< FixedAllocator > >;

	static red::String GetAttrStr( const JSONValue& val, const red::AnsiChar* txt, const red::AnsiChar* defaultText = "" )
	{
		if ( !val.HasMember(txt) )
			return defaultText;

		const JSONValue& member = val[txt];
		if ( !member.IsString() )
			return defaultText;

		return member.GetString();
	}

	static red::StringView GetAttrStrView( const JSONValue& val, const red::AnsiChar* txt, const red::StringView defaultText = red::StringView() )
	{
		if ( !val.HasMember(txt) )
			return defaultText;

		const JSONValue& member = val[txt];
		if ( !member.IsString() )
			return defaultText;

		return red::StringView( member.GetString(), member.GetStringLength() );
	}

	static CName GetAttrName( const JSONValue& val, const red::AnsiChar* txt, const CName defaultText = CName() )
	{
		if ( !val.HasMember(txt) )
			return defaultText;

		const JSONValue& member = val[txt];
		if ( !member.IsString() )
			return defaultText;

		return RED_NAME( member.GetString() );
	}

	static red::DynArray< CName > GetAttrNameArray( const JSONValue& val, const red::AnsiChar* txt )
	{
		red::DynArray< CName > ret{ red::PoolEngine() };

		if ( !val.HasMember(txt) )
			return ret;

		const JSONValue& member = val[txt];
		if ( !member.IsArray() )
			return ret;

		const Uint32 count = member.Size();
		for ( auto it = member.Begin(); it != member.End(); ++it )
		{
			const JSONValue& arrayMember = (*it);
			if ( arrayMember.IsString() )
			{
				CName name = RED_NAME( arrayMember.GetString() );
				if ( name )
				{
					ret.PushBack( name );
				}
			}
		}

		return ret;
	}

	static Bool GetAttrBool( const JSONValue& val, const AnsiChar* txt, const Bool defaultValue = false )
	{
		if ( !val.HasMember(txt) )
			return defaultValue;

		const JSONValue& member = val[txt];
		if ( !member.IsBool() )
			return defaultValue;

		return member.GetBool();
	}

	static Int32 GetAttrInt( const JSONValue& val, const AnsiChar* txt, const Int32 defaultValue = 0 )
	{
		if ( !val.HasMember(txt) )
			return defaultValue;

		const JSONValue& member = val[txt];
		if ( !member.IsNumber() )
			return defaultValue;

		if( member.IsInt() )
			return member.GetInt();

		// This allows to support reading float values as integers without explicit rules.
		return static_cast<Int32>( member.GetDouble() );
	}

	static Uint64 GetAttrUint64( const JSONValue& val, const AnsiChar* txt, const Uint64 defaultValue = 0 )
	{
		if ( !val.HasMember(txt) )
			return defaultValue;

		const JSONValue& member = val[txt];
		if ( !member.IsNumber() )
			return defaultValue;

		if( member.IsUint64() )
			return member.GetUint64();

		// This allows to support reading float values as integers without explicit rules.
		return static_cast<Uint64>( member.GetDouble() );
	}

	static Uint32 GetAttrUint32( const JSONValue& val, const AnsiChar* txt, const Uint32 defaultValue = 0 )
	{
		if ( !val.HasMember(txt) )
			return defaultValue;

		const JSONValue& member = val[txt];
		if ( !member.IsNumber() )
			return defaultValue;

		if( member.IsUint() )
			return member.GetUint();

		// This allows to support reading float values as integers without explicit rules.
		return static_cast<Uint32>( member.GetDouble() );
	}

	static Float GetAttrFloat( const JSONValue& val, const AnsiChar* txt, const Float defaultValue = 0.0 )
	{
		if ( !val.HasMember(txt) )
			return defaultValue;

		const JSONValue& member = val[txt];
		if ( !member.IsNumber() )
			return defaultValue;

		return static_cast<Float>( member.GetDouble() );
	}

	static Double GetAttrDouble( const JSONValue& val, const AnsiChar* txt, const Double defaultValue = 0.0 )
	{
		if ( !val.HasMember(txt) )
			return defaultValue;

		const JSONValue& member = val[txt];
		if ( !member.IsNumber() )
			return defaultValue;

		return member.GetDouble();
	}

}

#endif // __JSON_FILE_H__