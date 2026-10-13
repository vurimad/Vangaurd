/*
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "dependencyJsonWriter.h"
#include "dependencyHash.h"

#include "../../backendEngine/include/jsonBatchedTextWriter.h"
#include "../../../common/resource/include/jsonAllocator.h"
#include "../../../../external/rapidjson/include/rapidjson/rapidjson.h"
#include "../../../../external/rapidjson/include/rapidjson/prettywriter.h"
#include "../../../../external/rapidjson/include/rapidjson/writer.h"
#include "../../../../external/rapidjson/include/rapidjson/filewritestream.h"

namespace dependency
{

using RapidJsonWriter = rapidjson::PrettyWriter< tools::BatchedTextWriter, rapidjson::UTF8< char >, rapidjson::UTF8< char >, res::json::JSONAllocator >;


//------------------------------------------------------------------------------

class JsonWriter : public Writer
{
public:
	explicit JsonWriter( RapidJsonWriter& writer );
	virtual ~JsonWriter();

	virtual WriterScope StartScope( const red::StringView& key ) override;
	virtual WriterArrayScope StartArray( const red::StringView& key, Uint32 elements ) override;

	virtual void WriteName( const red::StringView& key, const CName name ) override;
	virtual void WriteBool( const red::StringView& key, bool value ) override;
	virtual void WriteNumber( const red::StringView& key, Int32 number ) override;
	virtual void WriteNumber( const red::StringView& key, Uint32 number ) override;
	virtual void WriteNumber( const red::StringView& key, Int64 number ) override;
	virtual void WriteNumber( const red::StringView& key, Uint64 number ) override;
	virtual void WriteFloat( const red::StringView& key, float number ) override;
	virtual void WriteString( const red::StringView& key, const red::StringView& value ) override;

	virtual void WritePath( const red::StringView& key, const res::ResourcePath& value ) override;
	virtual void WriteTimestamp( const red::StringView& key, const Timestamp timestamp ) override;
	virtual void WriteHash( const red::StringView& key, const Hash& hash ) override;

protected:
	RapidJsonWriter& m_writer;
};

//------------------------------------------------------------------------------

class JsonArrayWriter : public WriterArray
{
public:
	explicit JsonArrayWriter( RapidJsonWriter& writer );
	virtual ~JsonArrayWriter();

	virtual WriterScope StartScope( const red::StringView& key ) override;

	virtual void WriteName( const CName name ) override;
	virtual void WriteNumber( Int32 number ) override;
	virtual void WriteNumber( Uint32 number ) override;
	virtual void WriteNumber( Int64 number ) override;
	virtual void WriteNumber( Uint64 number ) override;
	virtual void WriteFloat( float number ) override;
	virtual void WriteString( const red::StringView& value ) override;
	virtual void WritePath( const res::ResourcePath& value ) override;

private:
	RapidJsonWriter& m_writer;
};

class JsonObjectWriter : public JsonWriter
{
public:
	explicit JsonObjectWriter( RapidJsonWriter& writer );
	virtual ~JsonObjectWriter();
};

//------------------------------------------------------------------------------

JsonWriter::JsonWriter( RapidJsonWriter& writer )
	: m_writer( writer )
{
}

JsonWriter::~JsonWriter()
{
}

WriterScope JsonWriter::StartScope( const red::StringView& key )
{
	if ( !key.Empty() )
	{
		m_writer.Key( key.Data(), key.Length() );
	}
	return red::CreateUniquePtr< JsonObjectWriter >( m_writer );
}

WriterArrayScope JsonWriter::StartArray( const red::StringView& key, Uint32 elements )
{
	if ( !key.Empty() )
	{
		m_writer.Key( key.Data(), key.Length() );
	}
	return red::CreateUniquePtr< JsonArrayWriter >( m_writer );
}

void JsonWriter::WriteName( const red::StringView& key, const CName name )
{
	m_writer.Key( key.Data(), key.Length() );
	m_writer.String( name.AsChar() );
}

void JsonWriter::WriteBool( const red::StringView& key, bool value )
{
	m_writer.Key( key.Data(), key.Length() );
	m_writer.Bool( value );
}

void JsonWriter::WriteNumber( const red::StringView& key, Int32 number )
{
	m_writer.Key( key.Data(), key.Length() );
	m_writer.Int( number );
}

void JsonWriter::WriteNumber( const red::StringView& key, Uint32 number )
{
	m_writer.Key( key.Data(), key.Length() );
	m_writer.Uint( number );
}

void JsonWriter::WriteNumber( const red::StringView& key, Int64 number )
{
	m_writer.Key( key.Data(), key.Length() );
	m_writer.Int64( number );
}

void JsonWriter::WriteNumber( const red::StringView& key, Uint64 number )
{
	m_writer.Key( key.Data(), key.Length() );
	m_writer.Uint64( number );
}

void JsonWriter::WriteFloat( const red::StringView& key, float number )
{
	m_writer.Key( key.Data(), key.Length() );
	auto str = String::Printf( "%1.8e", number );
	m_writer.String( str.AsChar(), str.Length() );
}

void JsonWriter::WriteString( const red::StringView& key, const red::StringView& value )
{
	m_writer.Key( key.Data(), key.Length() );
	m_writer.String( value.Data(), value.Length() );
}

void JsonWriter::WritePath( const red::StringView& key, const res::ResourcePath& value )
{
	m_writer.Key( key.Data(), key.Length() );
	red::StringView sv = value.ToStringView();
	m_writer.String( sv.Data(), sv.Length() );

	// For resource paths write the hash as an additional parameter
	// do this only for JSON as it helps debugging and is needed for the server data cache
	red::String hashKey = key.ToString() + " hash";
	m_writer.Key( hashKey.AsChar(), hashKey.Length() );
	red::String hashValue = String::Printf( "%.16llX", value.GetHash() );
	m_writer.String( hashValue.AsChar(), hashValue.Length() );
}

void JsonWriter::WriteTimestamp( const red::StringView& key, const Timestamp timestamp )
{
	m_writer.Key( key.Data(), key.Length() );
	m_writer.Uint64( timestamp );
}

void JsonWriter::WriteHash( const red::StringView& key, const Hash& hash )
{
	m_writer.Key( key.Data(), key.Length() );
	String str = Hash::ToEncodedString( hash );
	m_writer.String( str.AsChar(), str.Length() );
}

//------------------------------------------------------------------------------

JsonArrayWriter::JsonArrayWriter( RapidJsonWriter& writer )
	: m_writer( writer )
{
	m_writer.StartArray();
}

JsonArrayWriter::~JsonArrayWriter()
{
	m_writer.EndArray();
}

WriterScope JsonArrayWriter::StartScope( const red::StringView& key )
{
	return red::CreateUniquePtr< JsonObjectWriter >( m_writer );
}

void JsonArrayWriter::WriteName( const CName name )
{
	m_writer.String( name.AsChar() );
}

void JsonArrayWriter::WriteNumber( Int32 number )
{
	m_writer.Int( number );
}

void JsonArrayWriter::WriteNumber( Uint32 number )
{
	m_writer.Uint( number );
}

void JsonArrayWriter::WriteNumber( Int64 number )
{
	m_writer.Int64( number );
}

void JsonArrayWriter::WriteNumber( Uint64 number )
{
	m_writer.Uint64( number );
}

void JsonArrayWriter::WriteFloat( float number )
{
	auto str = String::Printf( "%1.8e", number );
	m_writer.String( str.AsChar(), str.Length() );
}

void JsonArrayWriter::WriteString( const red::StringView& value )
{
	m_writer.String( value.Data(), value.Length() );
}

void JsonArrayWriter::WritePath( const res::ResourcePath& value )
{
	red::StringView sv = value.ToStringView();
	m_writer.String( sv.Data(), sv.Length() );
}

//------------------------------------------------------------------------------

JsonObjectWriter::JsonObjectWriter( RapidJsonWriter& writer )
	: JsonWriter( writer )
{
	m_writer.StartObject();
}

JsonObjectWriter::~JsonObjectWriter()
{
	m_writer.EndObject();
}

//------------------------------------------------------------------------------

class JsonMainWriter : public JsonWriter
{
public:
	explicit JsonMainWriter( const red::AbsolutePath& path );
	virtual ~JsonMainWriter();

private:
	static red::UniquePtr< IFile > CreateOutputFile( const red::AbsolutePath& path );

	red::AbsolutePath m_path;
	red::UniquePtr< IFile > m_file;
	tools::BatchedTextWriter m_textWriter;
	RapidJsonWriter m_jsonWriter;
};

red::UniquePtr< IFile > JsonMainWriter::CreateOutputFile( const red::AbsolutePath& path )
{
	GFileManager->CreatePath( ::red::paths::ParentAbsolutePath( path ) );

	return GFileManager->CreateFileWriter( path, FOF_AbsolutePath );
}

JsonMainWriter::JsonMainWriter( const red::AbsolutePath& path )
	: JsonWriter( m_jsonWriter )
	, m_path( path )
	, m_file( CreateOutputFile( m_path ) )
	, m_textWriter{ *m_file }
	, m_jsonWriter{ m_textWriter }
{
	m_writer.SetIndent( '\t', 1 );
	m_writer.StartObject();
}

JsonMainWriter::~JsonMainWriter()
{
	m_writer.EndObject();
}

//------------------------------------------------------------------------------


WriterScope CreateJsonWriter( const red::AbsolutePath& path )
{
	return red::CreateUniquePtr< JsonMainWriter >( path );
}

//------------------------------------------------------------------------------

} // dependency
