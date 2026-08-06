/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

#include "rttiArrayTypes.h"

#ifdef RED_PLATFORM_LINUX
#	include <limits.h>
#endif

namespace rtti
{

	// dynamic array type wrapper for red::DynArray
	// but using its inner type rather then template parameter
	// we cannot create ArrayType as template as it would require registering all possible types of arrays
	// in rtti which is rather impossible task (think of int************************** ;-)
	class RED_REFLECTION_API ArrayType : public IBaseArrayType
	{
		const rtti::IType*			m_innerType;	// element type
		CName				m_name;			// cached full type name (in form of array<[type],[memoryclass],[memorypool]>)

	public:
		ArrayType( const rtti::IType *innerType );

		virtual const CName GetName() const override final { return m_name; }
		virtual Uint32 GetSize() const override final { return sizeof( red::DynArray<Int32> ); } // we assume that every array variation uses the same amount of memory
		virtual Uint32 GetAlignment() const  override final { return __alignof( red::DynArray<Int32> ); } // no alignment restrictions
		virtual ERTTITypeType GetType() const override final { return RT_Array; }

		virtual void Construct( void *object ) const override final;
		virtual void Destruct( void *object ) const override final;
		virtual Bool Compare( const void* data1, const void* data2, Uint32 flags ) const override final;
		virtual void Copy( void* dest, const void* src ) const override final;
		virtual void Move( void* dest, void* src ) const override final;
		virtual Bool Serialize( IFile& file, void* data, ISerializable* owner = nullptr ) const override final;
		virtual Bool FromString( void* data, const String& valueString ) const override final { return false; }
		virtual Bool NeedsCleaning() const override final;

	public:
		/// get size of the array
		Uint32 GetArraySize( const void* arrayData ) const;

		/// Get the current capacity of an array
		Uint32 GetArrayCapacity( const void* arrayData ) const;

		// get pointer to array's element
		void* GetArrayElement( void* arrayData, Uint32 index ) const;

		/// get element's data
		const void* GetArrayElement( const void* arrayData, Uint32 index ) const;

		/// add element(s) to array
		Int32 AddArrayElement( void* arrayData, Uint32 count, const red::memory::Pool & pool ) const;

		/// delete is copying the whole array, preserving order of elements
		Bool DeleteArrayElement( void* arrayData, Int32 itemIndex ) const;

		/// delete fast is replacing the last element with a deleted element, not preserving order of elements
		Bool DeleteArrayElementFast( void* arrayData, Int32 itemIndex ) const;

		/// insert element in array
		Bool InsertArrayElementAt( void* arrayData, Int32 itemIndex ) const;

		// Get the type of array element, can be anything (array of arrays is allowed, etc)
		RED_FORCE_INLINE const rtti::IType* GetInnerType() const { return m_innerType; }

	public:
		// IBaseArrayType interface implementation
		virtual Bool ArrayIsResizable() const override final { return true; }
		virtual const rtti::IType* ArrayGetInnerType() const override final { return GetInnerType(); }
		virtual Uint32 ArrayGetArrayCapacity( const void* arrayData ) const override final { return UINT_MAX; }
		virtual Uint32 ArrayGetArraySize( const void* arrayData ) const override final { return GetArraySize( arrayData ); }
		virtual void* ArrayGetArrayElement( void* arrayData, Uint32 index ) const override final { return GetArrayElement( arrayData, index ); }
		virtual const void* ArrayGetArrayElement( const void* arrayData, Uint32 index ) const override final { return GetArrayElement( arrayData, index ); }
		virtual Int32 ArrayAddElement( void* arrayData, Uint32 count=1 ) const override final { return AddArrayElement( arrayData, count, red::PoolDefault() ); }
		virtual Bool ArrayDeleteElement( void* arrayData, Int32 itemIndex ) const override final { return DeleteArrayElement( arrayData, itemIndex ); }
		virtual Bool ArrayInsertElement( void* arrayData, Int32 itemIndex ) const override final { return InsertArrayElementAt( arrayData, itemIndex ); }
	
		virtual Uint32 TryResizingArray( const void* arrayData, Uint32 size, const red::memory::Pool & pool ) const override final;
	};

	// static array type for <type>[N] kind of stuff
	class RED_REFLECTION_API NativeArrayType : public IBaseArrayType
	{
		const rtti::IType*			m_innerType;	// element type
		Uint32				m_elementCount;	// size of the array (number of elements) (it's part of the type definition)
		CName				m_name;			// cached full type name (in form of array<[type],[memoryclass],[memorypool]>)
		CName				m_refName;

	public:
		NativeArrayType( const rtti::IType *innerType, const Uint32 elementCount );

		virtual const CName GetName() const  override final { return m_name; }
		virtual Uint32 GetSize() const override final; // this returns byte size of the whole type, do not confuse this with GetElementCount() !!!!
		virtual Uint32 GetAlignment() const override final;
		virtual ERTTITypeType GetType() const  override final { return RT_NativeArray; }
		virtual CName GetRefName() const override final { return m_refName; }

		virtual void Construct( void *object ) const override final;
		virtual void Destruct( void *object ) const override final;
		virtual Bool Compare( const void* data1, const void* data2, Uint32 flags ) const override final;
		virtual void Copy( void* dest, const void* src ) const override final;
		virtual Bool Serialize( IFile& file, void* data, ISerializable* owner = nullptr ) const override final;
		virtual Bool FromString( void* data, const String& valueString ) const override final { return false; };
		virtual Bool NeedsCleaning() const;

	public:
		/// get size of the array
		Uint32 GetArraySize( const void* arrayData ) const;

		/// get element's data	 
		void* GetArrayElement( void* arrayData, Uint32 index ) const;

		/// get element's data	 
		const void* GetArrayElement( const void* arrayData, Uint32 index ) const;

		// Get the type of array element, can be anything (array of arrays is allowed, etc)
		RED_FORCE_INLINE const rtti::IType* GetInnerType() const { return m_innerType; }

		// Get number of array elements defined in the array type
		RED_FORCE_INLINE Uint32 GetElementCount() const { return m_elementCount; } 

	public:
		// IBaseArrayType interface
		virtual Bool ArrayIsResizable() const override final { return false; }
		virtual const rtti::IType* ArrayGetInnerType() const override final { return GetInnerType(); }
		virtual Uint32 ArrayGetArraySize( const void* arrayData ) const override final { return m_elementCount; }
		virtual Uint32 ArrayGetArrayCapacity( const void* arrayData ) const override final { return m_elementCount; }
		virtual void* ArrayGetArrayElement( void* arrayData, Uint32 index ) const override final { return GetArrayElement( arrayData, index ); }
		virtual const void* ArrayGetArrayElement( const void* arrayData, Uint32 index ) const override final { return GetArrayElement( arrayData, index ); }
		virtual Int32 ArrayAddElement( void* arrayData, Uint32 count=1 ) const override final { RED_UNUSED( arrayData ); RED_UNUSED( count ); return -1; }
		virtual Bool ArrayDeleteElement( void* arrayData, Int32 itemIndex ) const override final { RED_UNUSED( arrayData ); RED_UNUSED( itemIndex ); return false; }
		virtual Bool ArrayInsertElement( void* arrayData, Int32 itemIndex ) const override final { RED_UNUSED( arrayData ); RED_UNUSED( itemIndex ); return false; }
	
		virtual Uint32 TryResizingArray( const void* arrayData, Uint32 size, const red::memory::Pool & pool ) const override final;
	};

	// RTTI wrapper for red::StaticArray template
	class RED_REFLECTION_API StaticArrayType : public IBaseArrayType
	{
		const rtti::IType*			m_innerType;	// element type
		Uint32				m_maxSize;		// static array capacity

		CName				m_name;			// cached full type name (in form of array<[type],[memoryclass],[memorypool]>)
		CName				m_refName;

	public:
		StaticArrayType( const rtti::IType *innerType, const Uint32 maxSize );

		RED_INLINE Uint32 GetMaxSize() const { return m_maxSize; }

		virtual const CName GetName() const override final { return m_name; }
		virtual Uint32 GetSize() const override final;
		virtual Uint32  GetAlignment() const override final;
		virtual ERTTITypeType GetType() const override final { return RT_StaticArray; }
		virtual CName GetRefName() const override final { return m_refName; }

		virtual void Construct( void *object ) const override final;
		virtual void Destruct( void *object ) const override final;
		virtual Bool Compare( const void* data1, const void* data2, Uint32 flags ) const override final;
		virtual void Copy( void* dest, const void* src ) const override final;
		virtual Bool Serialize( IFile& file, void* data, ISerializable* owner = nullptr ) const override final;
		virtual Bool FromString( void* data, const String& valueString ) const override final { return false; };
		virtual Bool NeedsCleaning() const override final;

	public:
		// IBaseArrayType interface implementation
		virtual Bool ArrayIsResizable() const override final { return true; }
		virtual const rtti::IType* ArrayGetInnerType() const override final { return m_innerType; }
		virtual Uint32 ArrayGetArraySize( const void* arrayData ) const override final;
		virtual Uint32 ArrayGetArrayCapacity( const void* arrayData ) const override final;
		virtual void* ArrayGetArrayElement( void* arrayData, Uint32 index ) const override final;
		virtual const void* ArrayGetArrayElement( const void* arrayData, Uint32 index ) const override final;
		virtual Int32 ArrayAddElement( void* arrayData, Uint32 count=1 ) const override final;
		virtual Bool ArrayDeleteElement( void* arrayData, Int32 itemIndex ) const override final;
		virtual Bool ArrayInsertElement( void* arrayData, Int32 itemIndex ) const override final;

		virtual Uint32 TryResizingArray( const void* arrayData, Uint32 size, const red::memory::Pool & pool ) const override final;
	};

} // rtti
