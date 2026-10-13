/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#pragma once

#include "../../redMemory/include/sharedPtr.h"
#include "reflectionPool.h"

namespace rtti
{
	class SingleValueHolder;

	class ValueParser;
	class ValueBuilder;

	class ValueHolder;
	typedef red::SharedPtr< ValueHolder > ValuePtr;

	/// Safe value holder for data
	/// Supports arrays, structures and all simple types
	/// Pointers to ISerializable are encoded as IDs (SerializableID)
	/// Resource paths are stored as string
	/// Excellent data representation for edition, property copy/paste, ad-hoc text view, etc
	////
	/// DO NOT USE AT RUNTIME IN GAME unless it's strictly debug code - every other use will be tracked and prosecuted
	////
	/// Text format of ToString/FromString
	/// Simple types parsable

	/// Arrays:
	///   [1,2,3,4]
	/// Structs:
	///   {X=5,Y=10,Z=15}
	/// More complex objects are possible via self-recursion:
	///   {X=5,Y=10,Z=15}
	class RED_REFLECTION_API ValueHolder : public red::NonCopyable
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		~ValueHolder();

		/// Is this holder empty ?
		RED_FORCE_INLINE const Bool IsEmpty() const { return (m_type == EType::Empty); }

		/// Is this a single value holder ?
		RED_FORCE_INLINE const Bool IsSingle() const { return (m_type == EType::Single); }

		/// Is this an array ?
		RED_FORCE_INLINE const Bool IsArray() const { return (m_type == EType::Array); }

		/// Is this a structure ?
		RED_FORCE_INLINE const Bool IsStructure() const { return (m_type == EType::Struct); }

		/// Is this a structure ?
		RED_FORCE_INLINE const Bool IsHandle() const { return (m_type == EType::Handle); }

		/// Compare to another value type
		RED_FORCE_INLINE const Bool AreTypesSame(const ValueHolder& v) const { return m_type == v.m_type; }

		// Convert to a string representation that will be REPARSABLE (x == FromString(x.ToString()))
		// TODO: we should use something better than string here
		String ToString() const;

		// Try to parse from string representation, top level type must be known to validate the structure
		static ValuePtr Parse( const AnsiChar* valueText );
		
	public:
		// Build an empty value
		static ValuePtr CreateEmpty();

		// Build a single value holder - skips the parsing step and directly builds the "simple value"
		// NOTE: empty strings create the "Empty" value which should be handled as the zero/default/empty case any way
		static ValuePtr CreateSingle( const AnsiChar* valueText );

		// Create an ISerializable reference holder
		static ValuePtr CreateHandle( const AnsiChar* valueText );

		// If this is a single element get the value, returns empty value holder otherwise
		red::SharedPtr< SingleValueHolder > GetSingle() const;

		// If this is a handle element get the value, returns empty value holder otherwise
		red::SharedPtr< SingleValueHolder > GetHandle() const;

		// If this element is a single value holder return its value, otherwise return an empty string
		const String& GetSingleValue() const;

		// If this element is a handle value holder return its value, otherwise return an empty string
		const String& GetHandleValue() const;

	public:
		// Create an empty array holder
		static ValuePtr CreateArray();

		// Get number of array/struct elements
		// Returns nothing if the holder is not an array/struct
		const Uint32 GetNumElements() const;

		// Set/get array element value. Operation only valid if IsArray() returns true.
		// Returns nothing if the holder is not an array
		ValuePtr GetArrayElement( Uint32 index ) const;

		// Set new array element value in the value holder, ASSERTS if the holder is not an array
		void SetArrayElement( const Uint32 index, const ValuePtr& value );

		// Add element at the end of the array (push back), ASSERTS if the holder is not an array
		void AddArrayElement( const ValuePtr& value );

		// Insert element at the specified index of the array, ASSERTS if the holder is not an array
		void InsertArrayElement( const Uint32 index, const ValuePtr& value );

		// Remove array element by index
		void RemoveArrayElement( const Uint32 index );

	public:
		// Create an empty structure holder
		static ValuePtr CreateStructure();

		// Set value of a structure element directly by name, if element exists it's value is overridden, ASSERTS if we are not a struct
		void SetStructElement( const CName name, const ValuePtr& value );

		// Remove structure element by name, asserts if not a struct
		void RemoveStructElement( const CName name );

		// Get value of a struct element, returns nothing if not a struct (+asserts)
		ValuePtr GetStructElement( const CName name ) const;

		// Get value of a n-th struct element, returns nothing if not a struct (+asserts)
		ValuePtr GetStructElement( const Uint32 index ) const;

		// Get name of n-th struct element stored here, returns empty name if not a struct (+asserts)
		const CName GetStructElementName( const Uint32 index ) const;

		// Check if struct element exists
		Bool HasStructElement( const CName name ) const;

	private:
		ValueHolder();

		enum class EType
		{
			Empty,		// empty - stores no value
			Single,		// only single value is stored
			Array,		// array of values
			Struct,		// structure
			Handle,		// special case, this is not some string but a SerializableID
		};

		struct Element
		{
			CName						m_name;		// property name (only structures)
			red::SharedPtr<ValueHolder>	m_value;	// value for given sub-property
		};

		EType									m_type;
		
		red::SharedPtr< SingleValueHolder >		m_simple;
		red::DynArray< Element >				m_elements;

		// parse from specialized parser
		static ValuePtr Parse( ValueParser& parser );

		// store as text using specialized builder
		void ToString( ValueBuilder& builder ) const;
	};

	typedef ValuePtr ValueHolderPtr;

} // rtti