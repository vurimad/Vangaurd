/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

namespace rtti
{

	template< class _Type >
	RED_INLINE Bool ClassType::IsA() const
	{
		const ClassType *testedClass = _Type::GetStaticClass();
		return IsA( testedClass );
	}

	template< class _Type >
	RED_INLINE _Type* ClassType::CreateObject() const
	{
		void *mem = CreateObject( sizeof(_Type) );
		const ClassType *destTypeClass = _Type::GetStaticClass();
		return static_cast< _Type* >( CastTo( destTypeClass, mem ) );
	}

	template< class _Type >
	RED_INLINE THandle< _Type > ClassType::CreateHandle() const
	{
		void *mem = CreateObject( sizeof(_Type) );
		const ClassType *destTypeClass = _Type::GetStaticClass();
		return THandle< _Type >( static_cast< _Type* >( CastTo( destTypeClass, mem ) ) );
	}

	template< class _Type >
	RED_INLINE void ClassType::DestroyObject( _Type *obj ) const
	{
		const ClassType *srcTypeClass = _Type::GetStaticClass();
		void* thisObject = CastFrom( srcTypeClass, obj );
		DestroyObject( thisObject ); 
	}

	template< class _CurrentType, class _ParentType >
	RED_INLINE void ClassType::AddParentClass()
	{
		static_assert( std::is_base_of< _ParentType, _CurrentType >::value, "Unrelated Class Type." );
			 
		const ClassType *parentClass = _ParentType::GetStaticClass();
		if ( !parentClass )
		{
			RED_LOG_WARNING( "Core: Trying to set class parent class for %hs - parent class is not registered in RTTI system!", GetName().AsChar() );
			return;
		}

		// Make sure number of base classer per class is hold
		RED_FATAL_ASSERT( m_baseClass == nullptr, "Base class are set." );

		// Add new class definition
		m_baseClass = parentClass;
	}

	template < class _F >
	RED_INLINE void ClassType::IterateProperties( _F& f ) const
	{
		if ( HasBaseClass() )
		{
			GetBaseClass()->IterateProperties( f );
		}

		for ( auto it = m_localProperties.Begin(), end = m_localProperties.End(); it != end; ++it )
		{
			f( *it );
		}
	}

	// If the source class we ask to cast from is deriving from IScriptable, we bypass the virtual function call to call directly GetLocalClass.
	template< Bool IsIScriptable >
	struct ClassSelector
	{
		template< typename T >
		static const ClassType* Get( T * object ) { return object->GetLocalClass(); } 

		template< typename T >
		static const ClassType * Get( const T * object ) { return object->GetLocalClass(); } 
	};

	template<>
	struct ClassSelector< false >
	{
		template< typename T >
		static const ClassType * Get( T * object ) { return object->GetClass(); } 

		template< typename T >
		static const ClassType * Get( const T * object ) { return object->GetClass(); } 
	};

	template< class _DestType > 
	RED_INLINE _DestType* ClassType::CastTo( void *obj ) const
	{
		const ClassType *destTypeClass = _DestType::GetStaticClass();
		return static_cast<_DestType*>( CastTo( destTypeClass, obj ) );
	}

	template< class _Type > 
	RED_INLINE _Type* ClassType::GetDefaultObject() const
	{
		return static_cast<_Type*>( CastTo( _Type::GetStaticClass(), GetDefaultObject() ) );
	}

	RED_INLINE const ClassType::TPropertyOverrideList& ClassType::GetPropertyOverrides() const
	{
		return m_propertyOverrides;
	}

	RED_INLINE Bool ClassType::HasPropertyOverrides() const
	{
		return !m_propertyOverrides.Empty();
	}

} // rtti

// Helper to get class id from type
template <class T>
RED_INLINE const rtti::ClassType* ClassID()
{
	return T::GetStaticClass();
}

// Non-const version
template< class _DestType, class _SrcType >
RED_INLINE _DestType* Cast( _SrcType *srcObj )
{
	if( srcObj )
	{
		typedef rtti::ClassSelector< std::is_base_of< IScriptable, _SrcType >::value > ClassSelector;
		return ClassSelector::Get( srcObj )->IsA( ClassID< _DestType >() ) ? static_cast< _DestType* >( srcObj ) : nullptr;
	}

	return nullptr;	
}

// Const version
template< class _DestType, class _SrcType >
RED_INLINE const _DestType* Cast( const _SrcType *srcObj )
{
	if( srcObj )
	{
		typedef rtti::ClassSelector< std::is_base_of< IScriptable, _SrcType >::value > ClassSelector; 
		return ClassSelector::Get( srcObj )->IsA( ClassID< _DestType >() ) ? static_cast< const _DestType* >( srcObj ) : nullptr;
	}

	return nullptr;	
}

template< class _DestType >
RED_INLINE _DestType* Cast( const rtti::ClassType* srcClass, void *srcObject )
{
	const rtti::ClassType *destTypeClass = _DestType::GetStaticClass();	
	return static_cast<_DestType*>( srcClass->CastTo( destTypeClass, srcObject ) );
}

template< class _DestType, class _SrcType >
RED_INLINE _DestType* SafeCast( _SrcType *srcObj )
{
	RED_ASSERT( srcObj && srcObj->template IsA< _DestType >() );
	return Cast<_DestType>( srcObj );
}

// Function to support 'IsA' feature for non-ISerializable classes
template< class _DestType, class _RTTIType >
RED_INLINE Bool IsType( const _RTTIType* check )
{
	if ( check == NULL )
	{
		return false;
	}
	else
	{
		return check->GetClass()->template IsA< _DestType >();
	}
}

// Function to support 'IsExactlyA' feature for non-ISerializable classes
template< class _DestType, class _RTTIType >
RED_INLINE Bool IsExactlyType( const _RTTIType* check )
{
	if ( check == NULL )
	{
		return false;
	}
	else
	{
		return check->GetClass() == _DestType::GetStaticClass();
	}
}
