#pragma once
#include "handle.h"
#include "variant.h"

namespace rtti
{
	class Function;
}

namespace red
{
	// Purpose: ability to queue up set of function calls on a single object for later dispatch.
	//		Helpful where thread-safety matters.
	// 
	// Recommended usage:
	// - Derive your class from IScriptable
	// - Aggregate DelayedFunctionCallDispatcher in it
	// - Override QueueFunctionCall() from IScriptable and redirect logic to aggreated dispatcher
	// - Enqueue function calls by either (2 use cases):
	//		(a) Calling QueueCall() on your scriptables
	//		(b) Using DelayedFunctionCallbacks utility to first register set of callbacks on multiple scriptables, then enqueue them all at once
	// - Call ServiceCalls() to dispatch enqueued calls
	class RED_REFLECTION_API DelayedFunctionCallDispatcher
	{
	public:
		DelayedFunctionCallDispatcher();
		~DelayedFunctionCallDispatcher();

		void Initialize( WeakHandle< IScriptable > owner );

		// Dispatches all enqueued function calls
		// NOTE: Thread safe
		void ServiceCalls();

		// Enqueues function call
		// NOTE: Thread safe
		void QueueCall( const rtti::Function* function, const rtti::Variant& parameter );

	private:
		struct Call
		{
			const rtti::Function* m_function;
			rtti::Variant m_parameter;
		};
		void ServiceCall( Call& call );

		WeakHandle< IScriptable > m_owner;
		red::SpinLock m_callsLock;
		red::DynArray< Call > m_calls;
	};

	// Utility that makes registration (for delayed function calls) of multiple listeners easier.
	//
	// Usage:
	// - Register/unregister your callbacks
	// - Enqueue your callbacks (with appropriate scriptables)
	class RED_REFLECTION_API DelayedFunctionCallbacks
	{
	public:
		DelayedFunctionCallbacks();
		DelayedFunctionCallbacks( DelayedFunctionCallbacks&& other );
		~DelayedFunctionCallbacks();
		void operator = ( DelayedFunctionCallbacks&& other );

		// Adds function callback for a specific object
		// NOTE: Thread safe
		Uint32 RegisterCallback( WeakHandle< IScriptable > object, const rtti::Function* function );

		// Removed function callback for a specific object
		// NOTE: Thread safe
		Bool UnregisterCallback( const Uint32 id );

		// Enqueues all registered callbacks with appropriate scriptables
		// NOTE: Thread safe
		void EnqueueCallbacks( const rtti::Variant& parameter );

	private:
		struct Callback
		{
			Uint32 m_id;
			WeakHandle< IScriptable > m_object;
			const rtti::Function* m_function;
		};

		red::SpinLock m_callbacksLock;
		red::DynArray< Callback > m_callbacks;
		
		static volatile Uint32 s_idGenerator;
	};
}
