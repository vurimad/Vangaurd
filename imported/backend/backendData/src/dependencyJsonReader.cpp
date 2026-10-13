/*
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "dependencyJsonReader.h"
#include "dependencyHash.h"

#include "../../../common/redFileSystem/include/fileSys.h"

#include "../../../common/resource/include/jsonSerializer.h"
#include "../../../common/resource/include/jsonAllocator.h"

using RapidJsonDocument = rapidjson::JSONDocument;
using RapidJsonValue = rapidjson::JSONValue;

namespace dependency
{

//------------------------------------------------------------------------------

class JsonReader : public Reader
{
public:
	explicit JsonReader( const RapidJsonValue& reader );
	virtual ~JsonReader();

	virtual bool HasErrors() const override;

	virtual bool HasValue( const red::StringView& key ) const override;

	virtual ReaderScope StartScope( const red::StringView& key ) override;
	virtual ReaderArrayScope StartArray( const red::StringView& key ) override;

	virtual void ReadName( const red::StringView& key, CName& name ) override;
	virtual void ReadBool( const red::StringView& key, bool& value ) override;
	virtual void ReadNumber( const red::StringView& key, Int32& number ) override;
	virtual void ReadNumber( const red::StringView& key, Uint32& number ) override;
	virtual void ReadNumber( const red::StringView& key, Int64& number ) override;
	virtual void ReadNumber( const red::StringView& key, Uint64& number ) override;
	virtual void ReadFloat( const red::StringView& key, float& number ) override;
	virtual void ReadString( const red::StringView& key, red::String& value ) override;

	virtual void ReadPath( const red::StringView& key, res::ResourcePath& value ) override;
	virtual void ReadTimestamp( const red::StringView& key, Timestamp& timestamp ) override;
	virtual void ReadHash( const red::StringView& key, Hash& hash ) override;

private:
	const RapidJsonValue& m_reader;
};

//------------------------------------------------------------------------------

class JsonArrayReader : public ReaderArray
{
public:
	explicit JsonArrayReader( const RapidJsonValue& reader );
	virtual ~JsonArrayReader();

	virtual Uint32 Size() const override;
	virtual ReaderScope GetScope( Uint32 index ) const override;

	virtual void ReadName( Uint32 index, CName& name ) override;
	virtual void ReadNumber( Uint32 index, Int32& number ) override;
	virtual void ReadNumber( Uint32 index, Uint32& number ) override;
	virtual void ReadNumber( Uint32 index, Int64& number ) override;
	virtual void ReadNumber( Uint32 index, Uint64& number ) override;
	virtual void ReadFloat( Uint32 index, float& number ) override;
	virtual void ReadString( Uint32 index, red::String& value ) override;
	virtual void ReadPath( Uint32 index, res::ResourcePath& value ) override;

private:
	const RapidJsonValue& m_reader;
};

JsonArrayReader::JsonArrayReader( const RapidJsonValue& reader )
	: m_reader( reader )
{
}

JsonArrayReader::~JsonArrayReader()
{
}

Uint32 JsonArrayReader::Size() const
{
	return m_reader.Size();
}

ReaderScope JsonArrayReader::GetScope( Uint32 index ) const
{
	const RapidJsonValue& scopeValue = m_reader[ index ];
	return red::CreateUniquePtr< JsonReader >( scopeValue );
}

void JsonArrayReader::ReadName( Uint32 index, CName& name )
{
	const RapidJsonValue& jsonValue = m_reader[ index ];
	name = RED_NAME( red::StringView( jsonValue.GetString(), jsonValue.GetStringLength() ) );
}

void JsonArrayReader::ReadNumber( Uint32 index, Int32& number )
{
	number = m_reader[ index ].GetInt();
}

void JsonArrayReader::ReadNumber( Uint32 index, Uint32& number )
{
	number = m_reader[ index ].GetUint();
}

void JsonArrayReader::ReadNumber( Uint32 index, Int64& number )
{
	number = m_reader[ index ].GetInt64();
}

void JsonArrayReader::ReadNumber( Uint32 index, Uint64& number )
{
	number = m_reader[ index ].GetUint64();
}

void JsonArrayReader::ReadFloat( Uint32 index, float& number )
{
	const auto& value = m_reader[ index ];
	if ( value.IsString() )
	{
		char* endPos = nullptr;
		number = std::strtof( value.GetString(), &endPos );
	}
	else
	{
		number = value.GetFloat();
	}
}

void JsonArrayReader::ReadString( Uint32 index, red::String& value )
{
	const auto& jsonValue = m_reader[ index ];
	value = red::String( jsonValue.GetString(), jsonValue.GetStringLength() );
}

void JsonArrayReader::ReadPath( Uint32 index, res::ResourcePath& value )
{
	const auto& jsonValue = m_reader[ index ];
	red::StringView sv{ jsonValue.GetString(), jsonValue.GetStringLength() };
	value = res::ResourcePath::Build( sv );
}

//------------------------------------------------------------------------------

JsonReader::JsonReader( const RapidJsonValue& reader )
	: m_reader( reader )
{
}

JsonReader::~JsonReader()
{
}

bool JsonReader::HasErrors() const
{
	return false;
}

bool JsonReader::HasValue( const red::StringView& key ) const
{
	return m_reader.HasMember( key.Data() );
}

ReaderScope JsonReader::StartScope( const red::StringView& key )
{
	const RapidJsonValue& scopeValue = m_reader[ key.Data() ];
	return red::CreateUniquePtr< JsonReader >( scopeValue );
}

ReaderArrayScope JsonReader::StartArray( const red::StringView& key )
{
	const RapidJsonValue& arrayValue = m_reader[ key.Data() ];
	return red::CreateUniquePtr< JsonArrayReader >( arrayValue );
}

void JsonReader::ReadName( const red::StringView& key, CName& name )
{
	const auto& jsonValue = m_reader[ key.Data() ];
	name = RED_NAME( red::StringView( jsonValue.GetString(), jsonValue.GetStringLength() ) );
}

void JsonReader::ReadBool( const red::StringView& key, bool& value )
{
	value = m_reader[ key.Data() ].GetBool();
}

void JsonReader::ReadNumber( const red::StringView& key, Int32& number )
{
	number = m_reader[ key.Data() ].GetInt();
}

void JsonReader::ReadNumber( const red::StringView& key, Uint32& number )
{
	number = m_reader[ key.Data() ].GetUint();
}

void JsonReader::ReadNumber( const red::StringView& key, Int64& number )
{
	number = m_reader[ key.Data() ].GetInt64();
}

void JsonReader::ReadNumber( const red::StringView& key, Uint64& number )
{
	number = m_reader[ key.Data() ].GetUint64();
}

void JsonReader::ReadFloat( const red::StringView& key, float& number )
{
	const auto& value = m_reader[ key.Data() ];
	if ( value.IsString() )
	{
		char* endPos = nullptr;
		number = std::strtof( value.GetString(), &endPos );
	}
	else
	{
		number = value.GetFloat();
	}
}

void JsonReader::ReadString( const red::StringView& key, red::String& value )
{
	const auto& jsonValue = m_reader[ key.Data() ];
	value = red::String( jsonValue.GetString(), jsonValue.GetStringLength() );
}

void JsonReader::ReadPath( const red::StringView& key, res::ResourcePath& value )
{
	const auto& jsonValue = m_reader[ key.Data() ];
	red::StringView sv{ jsonValue.GetString(), jsonValue.GetStringLength() };
	value = res::ResourcePath::Build( sv );
}

void JsonReader::ReadTimestamp( const red::StringView& key, Timestamp& timestamp )
{
	timestamp = m_reader[ key.Data() ].GetUint64();
}

void JsonReader::ReadHash( const red::StringView& key, Hash& hash )
{
	const auto& jsonValue = m_reader[ key.Data() ];
	red::StringView sv{ jsonValue.GetString(), jsonValue.GetStringLength() };
	hash = dependency::Hash::FromEncodedString( sv );
}

//------------------------------------------------------------------------------

class JsonMainReader : public JsonReader
{
public:
	explicit JsonMainReader( const red::AbsolutePath& path );
	virtual ~JsonMainReader();

	virtual bool HasErrors() const override;

private:
	bool Load( const red::AbsolutePath& path );

	red::String m_string;
	RapidJsonDocument m_doc;
	bool m_hasErrors;
};

JsonMainReader::JsonMainReader( const red::AbsolutePath& path )
	: JsonReader( m_doc )
	, m_hasErrors( false )
{
	if ( !Load( path ) )
	{
		m_hasErrors = true;
	}
}

JsonMainReader::~JsonMainReader()
{
}

bool JsonMainReader::Load( const red::AbsolutePath& path )
{
	if ( !red::LoadFileToString( path, m_string ) )
	{
		return false;
	}

	m_doc.Parse( m_string.AsChar(), m_string.Length() );
	if ( m_doc.HasParseError() )
	{
		return false;
	}

	// Check for old version which does not have the "dependencies" field
	if ( !m_doc.HasMember( "dependencies" ) )
	{
		return false;
	}

	return true;
}

bool JsonMainReader::HasErrors() const
{
	return m_hasErrors;
}

//------------------------------------------------------------------------------

ReaderScope CreateJsonReader( const red::AbsolutePath& path )
{
	return red::CreateUniquePtr< JsonMainReader >( path );
}

} // dependency
