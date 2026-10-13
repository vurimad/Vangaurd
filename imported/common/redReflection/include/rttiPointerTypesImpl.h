/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

#include "rttiPointerTypes.h"
#include "resourceCommon.h"

namespace res
{
	class ResourceReference;
	class ResourceAsyncReference;
}

namespace rtti
{
	// class for handling pointer types
	class RED_REFLECTION_API PointerType : public IBasePointerType
	{
	public:
		PointerType( const rtti::IType *pointedType = NULL );

		/// const rtti::IType interface
		virtual const CName GetName() const override final { return m_name; }
		virtual Uint32 GetSize() const override final { return sizeof( void* ); }
		virtual Uint32 GetAlignment() const override final { return __alignof( void* ); }
		virtual ERTTITypeType GetType() const override final { return RT_Pointer; }
		virtual CName GetRefName() const override final { return m_refName; }

		virtual void Construct( void *object ) const override final;
		virtual void Destruct( void *object ) const override final;
		virtual Bool Compare( const void* data1, const void* data2, Uint32 flags ) const override final;
		virtual void Copy( void* dest, const void* src ) const override final;
		virtual Bool Serialize( IFile& file, void* data, ISerializable* owner = nullptr ) const override final;
		virtual Bool ToString( const void* data, String& valueString ) const override final;
		virtual Bool FromString( void* data, const String& valueString ) const override final;

		RED_FORCE_INLINE void* GetPointed( void *pointerData ) const { return *((void**)pointerData); }
		RED_FORCE_INLINE const void* GetPointed( const void *pointerData ) const { return *((void**)pointerData); }

		// IRTTIPointerType interface
		virtual const rtti::ClassType*	GetPointedType() const override final;
		virtual rtti::Pointer GetPointer( const void* data ) const override final;
		virtual void SetPointer( void* data, const rtti::Pointer & ptr ) const override final;
		virtual void ClonePointer( void* data, const rtti::Pointer & ptr ) const override final;

		virtual const red::memory::Pool & GetInnerTypeMemoryPool() const override final;

	private:
		const rtti::ClassType*		m_pointedClass;
		CName						m_name;			// cached in constructor
		CName						m_refName;
	};

	// class for handle types ( BaseHandle and such )
	class RED_REFLECTION_API HandleType : public IBasePointerType
	{
	public:
		HandleType( const rtti::IType *pointedType = NULL );

		/// const rtti::IType interface
		virtual const CName GetName() const override final;
		virtual Uint32 GetSize() const override final;
		virtual Uint32 GetAlignment() const override final; // no alignment restrictions
		virtual ERTTITypeType GetType() const override final;
		virtual CName GetRefName() const override final;

		virtual void Construct( void *object ) const override final;
		virtual void Destruct( void *object ) const override final;
		virtual Bool Compare( const void* data1, const void* data2, Uint32 flags ) const override final;
		virtual void Copy( void* dest, const void* src ) const override final;
		virtual Bool Serialize( IFile& file, void* data, ISerializable* owner = nullptr ) const override final;
		virtual Bool ToString( const void* data, String& valueString ) const override final;
		virtual Bool FromString( void* data, const String& valueString ) const override final;
		virtual Bool NeedsCleaning() const override final { return true; }
		virtual const red::memory::Pool & GetInnerTypeMemoryPool() const override final;

		void* GetPointed( void *pointerData ) const;
		const void* GetPointed( const void *pointerData ) const;

		// IRTTIPointerType interface
		virtual const rtti::ClassType*	GetPointedType() const override final;
		virtual rtti::Pointer GetPointer( const void* data ) const override final;
		virtual void SetPointer( void* data, const rtti::Pointer & ptr ) const override final;
		virtual void ClonePointer( void* data, const rtti::Pointer & ptr ) const override final;

		void RebuildParentHierarchy( void * object, ISerializable * parent ) const override final;

	protected:
		const rtti::ClassType*		m_pointedClass;
		CName						m_name;
		CName						m_refName;
	};

	class RED_REFLECTION_API WeakHandleType : public IBasePointerType
	{
	public:
		WeakHandleType( const rtti::IType *pointedType = NULL );

		/// const rtti::IType interface
		virtual const CName GetName() const override final;
		virtual Uint32 GetSize() const override final;
		virtual Uint32 GetAlignment() const override final; // no alignment restrictions
		virtual ERTTITypeType GetType() const override final;
		virtual CName GetRefName() const override final;

		virtual void Construct( void *object ) const override final;
		virtual void Destruct( void *object ) const override final;
		virtual Bool Compare( const void* data1, const void* data2, Uint32 flags ) const override final;
		virtual void Copy( void* dest, const void* src ) const override final;
		virtual Bool Serialize( IFile& file, void* data, ISerializable* owner = nullptr ) const override final;
		virtual Bool ToString( const void* data, String& valueString ) const override final;
		virtual Bool FromString( void* data, const String& valueString ) const override final;
		virtual Bool NeedsCleaning() const override final { return true; }
		virtual const red::memory::Pool & GetInnerTypeMemoryPool() const override final;

		void* GetPointed( void *pointerData ) const;
		const void* GetPointed( const void *pointerData ) const;

		// IRTTIPointerType interface
		virtual const rtti::ClassType*	GetPointedType() const override final;
		virtual rtti::Pointer GetPointer( const void* data ) const override final;
		virtual void SetPointer( void* data, const rtti::Pointer & ptr ) const override final;
		virtual void ClonePointer( void* data, const rtti::Pointer & ptr ) const override final;

	protected:
		const rtti::ClassType*		m_pointedClass;
		CName						m_name;
		CName						m_refName;
	};

	// class for resource reference type
	class RED_REFLECTION_API ResourceReferenceType : public rtti::IType
	{
	public:
		ResourceReferenceType( const rtti::IType* pointedType = nullptr );

		// const rtti::IType interface
		virtual const CName GetName() const override;
		virtual Uint32 GetSize() const override;
		virtual Uint32 GetAlignment() const override;
		virtual ERTTITypeType GetType() const override;
		virtual CName GetRefName() const override;
		virtual void Construct( void *object ) const override;
		virtual void Destruct( void *object ) const override;
		virtual Bool Compare( const void* data1, const void* data2, Uint32 DEPRECATED_flags ) const override;
		virtual void Copy( void* dest, const void* src ) const override;
		virtual Bool Serialize( IFile& file, void* data, ISerializable* owner = nullptr ) const override;
		virtual Bool ToString(const void* object, String& valueString) const override;
		virtual Bool FromString(void* object, const String& valueString) const override;
		virtual const red::memory::Pool & GetInnerTypeMemoryPool() const override final;
		// const rtti::IType interface end

		const rtti::ClassType* GetPointedType() const;
		res::ResourceReference * GetResourceReference( void * data ) const;

	protected:
		CName					m_name;
		CName					m_refName;
		const rtti::ClassType*	m_pointedClass;
	};

	// class for resource async reference type
	class RED_REFLECTION_API ResourceAsyncReferenceType : public rtti::IType
	{
	public:
		ResourceAsyncReferenceType( const rtti::IType* pointedType = nullptr );

		// const rtti::IType interface
		virtual const CName GetName() const override;
		virtual Uint32 GetSize() const override;
		virtual Uint32 GetAlignment() const override;
		virtual ERTTITypeType GetType() const override;
		virtual CName GetRefName() const override;
		virtual void Construct( void *object ) const override;
		virtual void Destruct( void *object ) const override;
		virtual Bool Compare( const void* data1, const void* data2, Uint32 DEPRECATED_flags ) const override;
		virtual void Copy( void* dest, const void* src ) const override;
		virtual Bool Serialize( IFile& file, void* data, ISerializable* owner = nullptr ) const override;
		virtual const Bool WriteValue( IRTTIContext& ctx, void* data, const rtti::AccessPath& path, const rtti::ValueHolder& newValue, bool clone ) const override final;
		// const rtti::IType interface end

		virtual Bool ToString(const void* object, String& valueString) const override;
		virtual Bool FromString(void* object, const String& valueString) const override;

		virtual const red::memory::Pool & GetInnerTypeMemoryPool() const override final;

		res::ResourceAsyncReference * GetResourceReference( void * data ) const;
		const rtti::ClassType* GetPointedType() const;

	protected:
		CName					m_name;
		CName					m_refName;
		const rtti::ClassType*	m_pointedClass;
	};

	// class for handling script references
	class RED_REFLECTION_API ScriptedReferenceType : public rtti::IType
	{
	public:
		ScriptedReferenceType( const rtti::IType *pointedType = NULL );

		/// const rtti::IType interface
		virtual const CName GetName() const override final { return m_name; }
		virtual Uint32 GetSize() const override final { return sizeof( ScriptedReferenceType ); }
		virtual Uint32 GetAlignment() const override final { return __alignof( ScriptedReferenceType ); }
		virtual ERTTITypeType GetType() const override final { return RT_ScriptReference; }
		virtual CName GetRefName() const override final;

		virtual void Construct( void *object ) const override final;
		virtual void Destruct( void *object ) const override final;
		virtual Bool Compare( const void* data1, const void* data2, Uint32 flags ) const override final;
		virtual void Copy( void* dest, const void* src ) const override final;
		virtual Bool Serialize( IFile& file, void* data, ISerializable* owner = nullptr ) const override final;
		virtual Bool ToString( const void* data, String& valueString ) const override final;
		virtual Bool FromString( void* data, const String& valueString ) const override final;

		virtual const red::memory::Pool & GetInnerTypeMemoryPool() const override final;

		void SetReference( const rtti::IType *pointedType, void* data );
		void* GetReferencedObject() const { return m_pointedObject; }
		const rtti::IType* GetPointedType() const { return m_pointedType; }

	private:
		const rtti::IType* m_pointedType;
		void* m_pointedObject;
		CName m_name;
	};

} // rtti
