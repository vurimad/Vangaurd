#pragma once

namespace comm
{
	/// Protocol data writer
	class IProtoWriter
	{
	public:
		virtual ~IProtoWriter() {};

		/////////////////////
		// structure
		/////////////////////

		// begin object block, records the object type (it will have to match)
		virtual void BeginObject( const red::AnsiChar* typeName, const Uint32 typeHash ) = 0;

		// end object block
		virtual void EndObject() = 0;

		// begin property block with given MAXIMUM number of properties
		virtual void BeginParams( const Uint32 maxParams ) = 0;

		// end property block, specifies the number of parameters actually saved
		virtual void EndParams( Uint32 numSavedParams ) = 0;

		// begin single property value, name and name hash is specified
		virtual void BeginParam( const red::AnsiChar* name, const Uint32 nameHash ) = 0;

		// end property value block
		virtual void EndParam() = 0;

		/////////////////////
		// data
		/////////////////////

		// write NULL object marker
		virtual void WriteNull() = 0;

		// write floating point value
		virtual void WriteFloat( const Float value ) = 0;

		// write double precission floating point value
		virtual void WriteDouble( const Double value ) = 0;

		// write uint8 value
		virtual void WriteUint8( const Uint8 value ) = 0;

		// write uint16 value
		virtual void WriteUint16( const Uint16 value ) = 0;

		// write uint32 value
		virtual void WriteUint32( const Uint32 value ) = 0;

		// write uint64 value
		virtual void WriteUint64( const Uint64 value ) = 0;

		// write int8 value
		virtual void WriteInt8( const Int8 value ) = 0;

		// write int16 value
		virtual void WriteInt16( const Int16 value ) = 0;

		// write int32 value
		virtual void WriteInt32( const Int32 value ) = 0;

		// write int64 value
		virtual void WriteInt64( const Int64 value ) = 0;

		// write boolean value
		virtual void WriteBool( const Bool value ) = 0;

		// write string value
		virtual void WriteString( const red::String& value ) = 0;

		/////////////////////
		// data
		/////////////////////

		// begin array with given type and number of elements
		virtual void BeginArray( const red::AnsiChar* typeName, const Uint32 typeHash, const Uint32 numElements ) = 0;

		// end array block
		virtual void EndArray() = 0;
	};

} // comm