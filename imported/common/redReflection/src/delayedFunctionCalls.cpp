/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "delayedFunctionCalls.h"
#include "rttiFunction.h"
#include "scriptable.h"

namespace red
{
	DelayedFunctionCallDispatcher::DelayedFunctionCallDispatcher()
		: m_calls( PoolEngine() )
	{
	}

	DelayedFunctionCallDispatcher::~DelayedFunctionCallDispatcher()
	{
	}

	void DelayedFunctionCallDispatcher::Initialize( WeakHandle< IScriptable > owner )
	{
		m_owner = std::move( owner );
	}
	
	void DelayedFunctionCallDispatcher::ServiceCalls()
	{
		if ( m_calls.Empty() )
		{
			return;
		}

		red::DynArray< Call > callsToService{ red::PoolEngine() };
		{
			RED_SCOPE_LOCK( m_callsLock );
			callsToService = std::move( m_calls );
		}

		for ( Call& call : callsToService )
		{
			ServiceCall( call );
		}
	}

	void DelayedFunctionCallDispatcher::ServiceCall( Call& call )
	{
		if ( const THandle< IScriptable > owner = m_owner )
		{
			const rtti::IType* parameterType = call.m_parameter.GetRTTIType();
			void* parameterData =
				( call.m_function->GetParameter( 0 )->GetType() == GetTypeObject< rtti::Variant >() ) ?
					&call.m_parameter :
					const_cast< void* >( call.m_parameter.GetData() ); // Note: const_cast<> saves us redundant value copy (and potentially extra memory allocation(s) - depending on type)

			rtti::FunctionContext_ExternalParams callCtx( owner.Get(), parameterData, nullptr, nullptr, nullptr );
			call.m_function->Call( callCtx );
		}
	}

	void DelayedFunctionCallDispatcher::QueueCall( const rtti::Function* function, const rtti::Variant& parameter )
	{
		// Validation

#ifdef RED_ASSERTS_ENABLED
		if ( const THandle< IScriptable > owner = m_owner )
		{
			const rtti::Function* foundFunction = owner->GetClass()->FindFunction( function->GetName() );
			RED_ASSERT( foundFunction == function, "Attempted to queue function call where owner object's class doesn't have that function." );
		}
		RED_ASSERT( function->GetNumParameters() == 1 );
		const rtti::IType* parameterType = function->GetParameter( 0 )->GetType();
		RED_ASSERT( parameterType == GetTypeObject< rtti::Variant >() ||
					parameterType == parameter.GetRTTIType(),
					"DelayedFunctionCallDispatcher::QueueCall(): Parameter type mismatch. Function '%s' in a class '%s' has parameter of type '%s' but got '%s'.",
						function->GetName().AsChar(),
						function->GetClass()->GetName().AsChar(),
						parameterType->GetName().AsChar(),
						parameter.GetRTTIType()->GetName().AsChar() );
#endif

		// Actual enqueuing of the function call

		RED_SCOPE_LOCK( m_callsLock );
		m_calls.PushBack( { function, parameter } );
	}

	volatile Uint32 DelayedFunctionCallbacks::s_idGenerator = 0;

	DelayedFunctionCallbacks::DelayedFunctionCallbacks()
		: m_callbacks( PoolEngine() )
	{
	}

	DelayedFunctionCallbacks::DelayedFunctionCallbacks( DelayedFunctionCallbacks&& other )
		: m_callbacks( std::move( other.m_callbacks ) )
	{}
	
	DelayedFunctionCallbacks::~DelayedFunctionCallbacks()
	{
	}

	void DelayedFunctionCallbacks::operator = ( DelayedFunctionCallbacks&& other )
	{
		m_callbacks = std::move( other.m_callbacks );
	}
	
	Uint32 DelayedFunctionCallbacks::RegisterCallback( WeakHandle< IScriptable > object, const rtti::Function* function )
	{
		RED_SCOPE_LOCK( m_callbacksLock );

#ifdef RED_ASSERTS_ENABLED
		for ( const Callback& callback : m_callbacks )
		{
			if ( callback.m_object == object && callback.m_function == function )
			{
				RED_LOG_WARNING( "DelayedFunctionCallbacks::UnregisterCallback(): Attempted multiple registrations for the same object (friendly_name='%s' class='%s') and function (name='%s')",
					object.ToHandle()->GetFriendlyName().AsChar(),
					object.ToHandle()->GetClass()->GetName().AsChar(),
					function->GetName().AsChar() );
				return -1;
			}
		}
#endif
		const Uint32 callbackID = s_idGenerator++;
		m_callbacks.PushBack( { callbackID, std::move( object ), function } );
		return callbackID;
	}
	
	Bool DelayedFunctionCallbacks::UnregisterCallback( const Uint32 id )
	{
		RED_SCOPE_LOCK( m_callbacksLock );
		for ( Uint32 i = 0; i < m_callbacks.Size(); ++i )
		{
			if ( m_callbacks[ i ].m_id == id )
			{
				m_callbacks.RemoveAtReorder( i );
				return true;
			}
		}
		return false;
	}
	
	void DelayedFunctionCallbacks::EnqueueCallbacks( const rtti::Variant& parameter )
	{
		if ( m_callbacks.Empty() )
		{
			return;
		}

		RED_SCOPE_LOCK( m_callbacksLock );
		for ( const Callback& callback : m_callbacks )
		{
			if ( THandle< IScriptable > object = callback.m_object.ToHandle() )
			{
				object->QueueFunctionCall( callback.m_function, parameter );
			}
		}
	}
}