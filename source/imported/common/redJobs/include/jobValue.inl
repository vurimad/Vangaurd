/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

#pragma once

namespace job
{
	namespace prv
	{

		template< typename T >
		class RefcountedValue : red::NonCopyable
		{
			RED_USE_MEMORY_POOL( red::PoolEngine );

		public:
			RefcountedValue()
				: m_value()
				, m_refCount(1)
			{}

			explicit RefcountedValue( red::EUndefined )
				: m_refCount(1)
			{
				// no init for pods
			}

			explicit RefcountedValue( T&& value )
				: m_value( std::move( value ) )
				, m_refCount( 1 )
			{}

			explicit RefcountedValue( const T& value )
				: m_value( value )
				, m_refCount( 1 )
			{}

			operator T() const = delete;

			Int32 Release()
			{
				return Release_CompatibleWithIntrusivePtr();
			}

			void AddRef()
			{
				m_refCount.AddRef();
			}

			void SetValue( T&& value )
			{
				m_value = std::move( value );
			}

			void SetValue( const T& value )
			{
				m_value = value;
			}

			const T& GetValue() const { return m_value; }

			T& GetValue() { return m_value; }

		private:
			Int32 Release_CompatibleWithIntrusivePtr()
			{
				if ( m_refCount.Release() )
					return 0;

				// Just non-zero so we're not deleted
				// Intrusive pointer doesn't and shouldn't ever care about the *exact* refcount (except maybe if debugging)
				return 1;
			}

			T m_value;
			mutable red::RefCount16< red::RefCountPolicyAtomic > m_refCount;
		};
	}

	/// initialize to default value
	template< typename T >
	RED_FORCE_INLINE Value<T>::Value( T&& initial )
	{
		// Sanity check assert
		static_assert( !prv::JobValueTrait<T>::isJobValue, "Invalid initialized type" );

		m_sharedValuePtr.Reset( RED_NEW( TRefcountedValue )( std::move( initial ) ) );
	}

	template< typename T >
	RED_FORCE_INLINE Value<T>::Value( const T& initial )
	{
		// Sanity check assert
		static_assert( !prv::JobValueTrait<T>::isJobValue, "Invalid initialized type" );
		
		m_sharedValuePtr.Reset( RED_NEW( TRefcountedValue )( initial ) );
	}

	template< typename T >
	RED_FORCE_INLINE Value<T>::Value( red::EUndefined )
	{
		m_sharedValuePtr.Reset( RED_NEW( TRefcountedValue )( red::Undefined ) );
	}

	template< typename T >
	RED_FORCE_INLINE Value<T>::Value()
	{
		m_sharedValuePtr.Reset( RED_NEW( TRefcountedValue )() );
	}

	template< typename T >
	RED_FORCE_INLINE const T& Value<T>::Get() const
	{
		return m_sharedValuePtr->GetValue();
	}

	/// access the value directly, NOTE: fatal asserts if value is undefined, you need to know what you are doing
	template< typename T >
	RED_FORCE_INLINE T* Value<T>::operator->() const
	{
		return &m_sharedValuePtr->GetValue();
	}

	/// access the value directly, NOTE: fatal asserts if value is undefined, you need to know what you are doing
	template< typename T >
	RED_FORCE_INLINE T& Value<T>::operator*() const
	{
		return m_sharedValuePtr->GetValue();
	}

// 	template< typename T >
// 	RED_FORCE_INLINE const T& Value<T>::operator*() const
// 	{
// 		return m_sharedValuePtr->GetValue();
// 	}

} // job