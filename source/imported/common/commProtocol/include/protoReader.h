#pragma once

namespace comm
{
	// forward declared message ID
	enum class EMessageID : unsigned int;

	/// Protocol data reader
	class IProtoReader
	{
	public:
		virtual ~IProtoReader() {};

		///////////////////////
		// structure
		///////////////////////

		// if the next object is an object look up it's hash
		// return 0 (NULL object) if next thing is not an object, does not fail
		virtual EMessageID PeekTypeHash() const = 0;

		// report parsing error (can be propagated), does not change the state of the reader
		virtual void Error( STATIC_CHECK_PRINTF_MSC const red::AnsiChar* txt, ... ) const = 0;

		// begin object, returns false if the stream does not match the object
		virtual Bool BeginObject( const red::AnsiChar* typeName, const Uint32 typeHash ) = 0;

		// end object, synchronizes the stream
		virtual void EndObject() = 0;

		/// start block of properties, returns number of properties in the block
		virtual Uint32 StartPropsBlock() = 0;

		// begin reading a property, returns false is there's no property in the stream
		// extracts name hash of the property
		virtual Bool BeginProperty( Uint32& outNameHash ) = 0;

		// end property, synchronizes the stream
		virtual void EndProperty() = 0;
		
		///////////////////////
		// data
		///////////////////////

		// read floating point value, returns false if value was unexpected
		virtual Bool ReadFloat( Float& outResult ) = 0;

		// read double precission floating point value, returns false if value was unexpected
		virtual Bool ReadDouble( Double& outResult ) = 0;

		// read uint8 value, returns false if value was unexpected
		virtual Bool ReadUint8( Uint8& outResult ) = 0;

		// read uint16 value, returns false if value was unexpected
		virtual Bool ReadUint16( Uint16& outResult ) = 0;

		// read uint32 value, returns false if value was unexpected
		virtual Bool ReadUint32( Uint32& outResult ) = 0;

		// read uint64 value, returns false if value was unexpected
		virtual Bool ReadUint64( Uint64& outResult ) = 0;

		// read int8 value, returns false if value was unexpected
		virtual Bool ReadInt8( Int8& outResult ) = 0;

		// read int16 value, returns false if value was unexpected
		virtual Bool ReadInt16( Int16& outResult ) = 0;

		// read int32 value, returns false if value was unexpected
		virtual Bool ReadInt32( Int32& outResult ) = 0;

		// read int64 value, returns false if value was unexpected
		virtual Bool ReadInt64( Int64& outResult ) = 0;

		// read boolean value, returns false if value was unexpected
		virtual Bool ReadBool( Bool& outResult ) = 0;

		// read string value, returns false if value was unexpected
		virtual Bool ReadString( red::String& outResult ) = 0;

		///////////////////////
		// array
		///////////////////////

		// begin dynamic array, returns number of elements in the array (0 if we didn't expect an array)
		virtual Uint32 BeginArray() = 0;

		// end dynamic array block, synchronizes the stream
		virtual void EndArray() = 0;
	};

} // comm