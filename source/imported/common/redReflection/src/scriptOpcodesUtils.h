/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#if defined( RED_LOGGING_ENABLED )

namespace script
{
	class WarnOnceGuard
	{
	public:

		RED_INLINE Bool CanWarn( CScriptStackFrame& stack )
		{

			const Id id { stack.m_function, stack.m_debugData.GetLine() };
			{
				RED_SCOPE_SHARED_LOCK( m_lock );
				if ( m_warned.Exist( id ) )
				{
					return false;
				}
			}
			{
				RED_SCOPE_LOCK( m_lock );
				return m_warned.Insert( id ).IsSuccessful();
			}
		}

	private:

		typedef std::pair< const void*, Uint32 > Id;
		red::RWLock m_lock;
		red::Set< Id > m_warned{red::PoolDebug() };
	};
} // script



#define SCRIPT_RUNTIME_WARN_ONCE( stack, txt, ... )				\
{																\
	static script::WarnOnceGuard warnGuard;						\
	if ( warnGuard.CanWarn( stack ) )							\
	{															\
		SCRIPT_RUNTIME_WARN( stack, txt, ## __VA_ARGS__ );		\
	}															\
}

#else

#define SCRIPT_RUNTIME_WARN_ONCE( stack, txt, ... )

#endif

