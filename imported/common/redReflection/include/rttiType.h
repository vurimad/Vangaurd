/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

#include "rttiCommon.h"
#include "../../redMemory/include/sharedPtr.h"
#include "reflectionPool.h"

class IFile;
class ISerializable;

namespace text
{
	class ITextWriter;
	class ITextReader;
}

namespace rep
{
	class Type;
	class RTTIService;
}

namespace rtti
{
	class AccessPath;
	class ValueHolder;

	typedef red::SharedPtr< ValueHolder > ValuePtr;

	/// Base type in the RTTI system
	class RED_REFLECTION_API IType
	{
		RED_USE_MEMORY_POOL( red::PoolRTTI );

	public:
		IType();
		virtual ~IType();
		IType( const IType & ) = delete;
		IType & operator=( const IType & ) = delete;

		/// Get name of the RTTI type, internal, can be stored in files
		virtual const CName GetName() const = 0;

		/// Get size in the memory, always well known
		virtual Uint32 GetSize() const = 0;

		/// Get required memory alignment in case the storage for typed data is allocated dynamically
		/// NOTE: this is basically _alignof + some hacks
		virtual Uint32 GetAlignment() const = 0;

		/// Get meta type (type of the type), this can be used to static_cast to derived classes
		virtual ERTTITypeType GetType() const = 0;
		
		/// Get meta type (type of the type) as string
		virtual String GetERTTITypeString() const;

		/// Get name of reference of the RTTI type
		virtual CName GetRefName() const;

	public:
		/// DataInternface: Construct valid data in given memory
		/// Equivalent of constructor call
		/// NOTE: zero-filled memory is ALSO a good initial state, in this case the Construct() can be omitted
		virtual void Construct( void *object ) const = 0;

		/// DataInternface: Destroy data from given memory
		/// Equivalent of destructor call, usually not called if NeedsCleaning() returns false
		virtual void Destruct( void *object ) const = 0;

		/// DataInternface: Compare two values, returns true if values are equal
		virtual Bool Compare( const void* data1, const void* data2, Uint32 DEPRECATED_flags ) const = 0;

		/// DataInternface: Copy value from one place in memory to other place
		/// Equivalent of copy assignment operator
		/// NOTE: the target memory MUST be properly initialized before we copy value into it
		virtual void Copy( void* dest, const void* src ) const=0;

		/// DataInternface: Move value from one place in memory to another place
		/// Equivalent of move assignment operator
		/// NOTE: the target memory MUST be properly initialized before we move value into it
		/// NOTE: default implementation is to copy memory. Move needs to be implemented explicitly per type
		/// NOTE: you should not read/write src after move, only destruction is allowed (like in C++)
		virtual void Move( void* dest, void* src ) const;

		/// DataInternface: Save/Load data from IFile
		virtual Bool Serialize( IFile& file, void* data, ISerializable* owner = nullptr ) const = 0;

		/// DataInternface: Get a simple string value
		/// NOTE: DEPRECATED, use ReadValue/WriteValue instead
		virtual Bool ToString( const void* object, String& valueString ) const { return false; }
		
		/// DataInternface: Parse a simple string value
		/// NOTE: DEPRECATED, use ReadValue/WriteValue instead
		virtual Bool FromString( void* object, const String& valueString ) const { return false; }

		/// DataInternface: Do we need a call to Destruct() for this type ?
		virtual Bool NeedsCleaning() const { return true; }

		//--------------------

		/// Serialize value of this type using the generic text writer
		/// By default the value is retrieved via ReadValue and saved as text
		virtual const Bool SerializeToText( text::ITextWriter& writer, const void* data ) const;

		/// Deserialize value of this type using the generic text reader
		/// By default the text value is read and parsed via the WriteValue
		virtual const Bool SerializeFromText( text::ITextReader& reader, void* data ) const;

		//--------------------

		// Retrieve the value in the universal value holder for recursive property data
		// Can easily retrieve sub-elements, depends on the path
		virtual const Bool ReadValue( IRTTIContext& ctx, const void* data, const rtti::AccessPath& path, rtti::ValuePtr& outValue ) const;

		// Write the value stored in the universal value holder into the typed data pointed by "data"
		// Can easily retrieve sub-elements, depends on the path
		virtual const Bool WriteValue( IRTTIContext& ctx, void* data, const rtti::AccessPath& path, const rtti::ValueHolder& newValue, bool clone = false ) const;

		virtual Bool IsPropertyReadOnly( IRTTIContext& ctx, const rtti::AccessPath& path, Bool& outReadOnly ) const;

		//--------------------

		// Check if type supports replication
		RED_INLINE Bool IsReplicable() const { return m_defaultReplicatedType != nullptr; }

		// Get corresponding default replication type
		RED_INLINE const rep::Type* GetDefaultReplicatedType() const { return m_defaultReplicatedType; }

		// Is type trivially movable
		virtual Bool IsTriviallyMovable() const { return false; }

		virtual void RebuildParentHierarchy( void * object, ISerializable * parent ) const;

		virtual const red::memory::Pool & GetInnerTypeMemoryPool() const;

	private:


		// Corresponding default replication type (only valid if type supports replication)
		const rep::Type* m_defaultReplicatedType;

		friend class rep::RTTIService;
	};

}
