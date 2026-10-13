/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

class ISerializable;

namespace rtti
{

	class ClassType;

	/// Generic typed pointer to object
	class RED_REFLECTION_API Pointer
	{
	public:
		//! Get actual pointed object
		RED_INLINE void* GetPointer() const { return m_data; }

		//! Get the pointer to stored object pointer
		RED_INLINE void** GetPointerRef() { return &m_data; }

		//! Get class of the pointed object (can be fake)
		RED_INLINE const rtti::ClassType* GetClass() const { return m_class; }

		//! Is this a null pointer
		RED_INLINE Bool IsNull() const { return m_data == NULL; }

	public:
		RED_INLINE Pointer();
		RED_INLINE Pointer( void* pointer, const rtti::ClassType* theClass );
		Pointer( ISerializable* object );
		RED_INLINE Pointer( const Pointer& pointer );
		RED_INLINE ~Pointer();

		//! General assignment operator
		RED_INLINE Pointer& operator=( const Pointer& other );
	 
		//! Compare for being equal
		RED_INLINE Bool operator==( const Pointer& other ) const;

		//! Compare for not being equal
		RED_INLINE Bool operator!=( const Pointer& other ) const;

		//! Is this a pointer to an ISerializable object ?
		Bool IsSerializable() const;

		//! Get class of object pointed by the pointer (not the pointer class, works only on ISerializable and above)
		const rtti::ClassType* GetRuntimeClass() const;

		//! Get a pointer to held ISerializable, returns NULL if it's not a ISerializable pointer
		ISerializable* GetSerializablePtr() const;

	public:
		//! The null pointer
		static Pointer& Null();

	private:
		void initialize( void* ptr, const rtti::ClassType* ptrClass );
		void release();

		void*			m_data;
		const rtti::ClassType*			m_class;
	};

} // rtti

#include "rttiPointer.inl"