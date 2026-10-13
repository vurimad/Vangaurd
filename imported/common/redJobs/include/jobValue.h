/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

#pragma once

#include "../../redSystem/include/utility.h"
#include "../../redMemory/include/atomicSharedPtr.h"
#include "../../redMemory/include/intrusivePtr.h"

namespace job
{
	template< typename T >
	class Value;
	
	namespace prv
	{
		template< typename T >
		class RefcountedValue;

		template< typename T, typename U = void >
		struct JobValueTrait
		{
			static const constexpr Bool isJobValue = false;
		};

		template< typename U >
		struct JobValueTrait< Value< U > >
		{
			static const constexpr Bool isJobValue = true;
		};
	}

	/// holder for a value that will be available when the job runs and that may be set by other jobs
	/// this provides basic data routing to and from the job
	/// the interface is made as such that it can be used just as a normal variable would (so it can be copied around, etc)
	/// NOTE: the changes to the value are always atomic (ie. even if the T is a structure you cannot change the parts inside, you need to change whole structure) - it's TBD if this will stay or go
	template< typename T >
	class Value
	{
		static_assert( !std::is_void< T >::value, "Void job value makes no sense" );
		static_assert( !prv::JobValueTrait< T >::isJobValue, "Job values of job values are forbidden" );
		static_assert( !std::is_reference< T >::value, "No reference types allowed; not copyable" );
		static_assert( !std::is_volatile< T >::value, "No volatile types allowed; too dodgy like trying to use this as some atomic var" );
		
	public:
		/// initialize as EMPTY value, requires default constructor on T
		RED_FORCE_INLINE Value();

		/// Explicit to help avoid things like creating new job values for comparisons (template apparently not high priority enough)
		/// initialize to default value
		explicit RED_FORCE_INLINE Value( T&& initial );
		
		explicit RED_FORCE_INLINE Value( const T& initial );

		Value( const job::Value<T>& ) = default;

		// Avoid nuking the job::Values. Could happen easily enough thinking you're update the value, not changing the sharedptr itself
 		Value& operator=( const job::Value<T>& ) = delete;
		Value& operator=( job::Value<T>&& ) = delete;

		// If you want to replace the job::Value, be explicit
		void Reset( const job::Value< T >& value )
		{
			m_sharedValuePtr = value.m_sharedValuePtr;
		}
		
		void Reset( job::Value< T >&& value )
		{
			m_sharedValuePtr = std::move( value.m_sharedValuePtr );
		}

		/// #tbd: put back the setter/change this iface a bit.
		/// initialize as undefined value - accessing it will result in FATAL ASSERT
		/// EXPLICIT ctor to avoid errors where you go value = X vs *value = X and nuke the shared value.
		explicit RED_FORCE_INLINE Value( red::EUndefined );

		// If you want the address of the underlying value, use &AsRef(). Don't be cute and overload operator&()
		Value< T >* operator&() = delete;


		//////////////////////////////////////////////////////////////////////////
		// TO BE DEPRECATED

		/// get the stored value, fatal asserts if a NULL value is accessed (means that the job chain is fucked)
		/// requires value to already be defined
		RED_FORCE_INLINE const T& Get() const;

		/// access the value directly, NOTE: fatal asserts if value is undefined, you need to know what you are doing
		/// requires value to already be defined
		RED_FORCE_INLINE T* operator->() const;

		// #FIXME: xmlResourceLoader relies on this returning non-const in a move
		/// access the value directly, NOTE: fatal asserts if value is undefined, you need to know what you are doing
		/// requires value to already be defined
		//RED_FORCE_INLINE const T& operator*() const;

		// #FIXME: xmlResourceLoader relies on this returning non-const on a const in a move
		RED_FORCE_INLINE T& operator*() const;
		
	private:
		// Store as mutable so can shared between const and mutable job::SharedValue types
		using TInternalStorageType = typename std::remove_const< T >::type;
		using TRefcountedValue = prv::RefcountedValue< TInternalStorageType >;

		red::IntrusivePtr< TRefcountedValue > m_sharedValuePtr;
	};

} // job

#include "jobValue.inl"