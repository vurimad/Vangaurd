/**
* Copyright (c) 2014-2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "debugServerHelpers.h"
#include "../../../common/redContainers/include/fundamentalStringConversion.h"
#include "../../../common/redSystem/include/redThreadsThread.h"
#include "../../../common/redNetwork/include/channel.h"

namespace red
{

template < typename T >
RED_INLINE T DebugServerPluginImpl::Functor::Parse( const red::String& str )
{
	T t = T();
	::FromString( str, t );
	return t;
}

//////////////////////////////////////////////////////////////////////////

template < typename T1, typename T2, typename T3, typename T4, typename T5 >
class DebugServerPluginFunctor5 : public DebugServerPluginImpl::Functor
{
	typedef std::function< void( T1, T2, T3, T4, T5 ) > Function;

	Function m_function;

public:

	RED_INLINE DebugServerPluginFunctor5( const Function& func )
		: m_function( func )
	{}

	virtual void Call( const red::DynArray< red::String >& args ) override final
	{
		m_function( Parse< T1 >( args[ 1 ] ), Parse< T2 >( args[ 2 ] ), Parse< T3 >( args[ 3 ] ), Parse< T4 >( args[ 4 ] ), Parse< T5 >( args[ 5 ] ) );
	}

	virtual Uint32 GetArgsCount() const override final { return 5; }
};

template < typename T1, typename T2, typename T3, typename T4 >
class DebugServerPluginFunctor4 : public DebugServerPluginImpl::Functor
{
	typedef std::function< void( T1, T2, T3, T4 ) > Function;

	Function m_function;

public:

	RED_INLINE DebugServerPluginFunctor4( const Function& func )
		: m_function( func )
	{}

	virtual void Call( const red::DynArray< red::String >& args ) override final
	{
		m_function( Parse< T1 >( args[ 1 ] ), Parse< T2 >( args[ 2 ] ), Parse< T3 >( args[ 3 ] ), Parse< T4 >( args[ 4 ] ) );
	}

	virtual Uint32 GetArgsCount() const override final { return 4; }
};

template < typename T1, typename T2, typename T3 >
class DebugServerPluginFunctor3 : public DebugServerPluginImpl::Functor
{
	typedef std::function< void( T1, T2, T3 ) > Function;

	Function m_function;

public:

	RED_INLINE DebugServerPluginFunctor3( const Function& func )
		: m_function( func )
	{}

	virtual void Call( const red::DynArray< red::String >& args ) override final
	{
		m_function( Parse< T1 >( args[ 1 ] ), Parse< T2 >( args[ 2 ] ), Parse< T3 >( args[ 3 ] ) );
	}

	virtual Uint32 GetArgsCount() const override final { return 3; }
};

template < typename T1, typename T2 >
class DebugServerPluginFunctor2 : public DebugServerPluginImpl::Functor
{
	typedef std::function< void( T1, T2 ) > Function;

	Function m_function;

public:

	RED_INLINE DebugServerPluginFunctor2( const Function& func )
		: m_function( func )
	{}

	virtual void Call( const red::DynArray< red::String >& args ) override final
	{
		m_function( Parse< T1 >( args[ 1 ] ), Parse< T2 >( args[ 2 ] ) );
	}

	virtual Uint32 GetArgsCount() const override final { return 2; }
};

template < typename T1 >
class DebugServerPluginFunctor1 : public DebugServerPluginImpl::Functor
{
	typedef std::function< void( T1 ) > Function;

	Function m_function;

public:

	RED_INLINE DebugServerPluginFunctor1( const Function& func )
		: m_function( func )
	{}

	virtual void Call( const red::DynArray< red::String >& args ) override final
	{
		m_function( Parse< T1 >( args[ 1 ] ) );
	}

	virtual Uint32 GetArgsCount() const override final { return 1; }
};

class DebugServerPluginFunctor0 : public DebugServerPluginImpl::Functor
{
	typedef std::function< void( void ) > Function;

	Function m_function;

public:

	RED_INLINE DebugServerPluginFunctor0( const Function& func )
		: m_function( func )
	{}

	virtual void Call( const red::DynArray< red::String >& args ) override final
	{
		m_function();
	}

	virtual Uint32 GetArgsCount() const override final { return 0; }
};

//////////////////////////////////////////////////////////////////////////

RED_INLINE void WritePacketArgs( red::Network::ChannelPacket& packet )
{
	// do nothing
}

template < typename... Args >
RED_INLINE void WritePacketArgs( red::Network::ChannelPacket& packet, const char* str, const Args&... args )
{
	packet.WriteString( str );
	WritePacketArgs( packet, args... );
}

template < typename... Args >
RED_INLINE void WritePacketArgs( red::Network::ChannelPacket& packet, const red::String& str, const Args&... args )
{
	WritePacketArgs( packet, str.AsChar(), args... );
}

template < typename T, typename... Args >
RED_INLINE void WritePacketArgs( red::Network::ChannelPacket& packet, const T& t, const Args&... args )
{
	red::String str;
	::ToString( str, t );
	WritePacketArgs( packet, str, args... );
}

//////////////////////////////////////////////////////////////////////////

template < typename... Args >
void DebugServerPluginImpl::Call( const red::String& targetPluginName, const red::String& functorName, const Args&... args )
{
	red::Network::ChannelPacket packet( DebugServerHelpers::GetChannelName() );
	packet.WriteString( targetPluginName.AsChar() );
	packet.WriteString( functorName.AsChar() );
	WritePacketArgs( packet, args... );
	Send( packet );
}

//////////////////////////////////////////////////////////////////////////

template < typename T1, typename T2, typename T3, typename T4, typename T5 >
RED_INLINE Bool DebugServerPluginImpl::RegisterFunctor( const red::String& name, const std::function< void( T1, T2, T3, T4, T5 ) >& func )
{
	FunctorPtr funcPtr = red::CreateUniquePtr< DebugServerPluginFunctor5< T1, T2, T3, T4, T5 > >( func );
	return m_functors.Insert( name, std::move( funcPtr ) ).IsSuccessful();
}

template < typename T1, typename T2, typename T3, typename T4 >
RED_INLINE Bool DebugServerPluginImpl::RegisterFunctor( const red::String& name, const std::function< void( T1, T2, T3, T4 ) >& func )
{
	FunctorPtr funcPtr = red::CreateUniquePtr< DebugServerPluginFunctor4< T1, T2, T3, T4 > >( func );
	return m_functors.Insert( name, std::move( funcPtr ) ).IsSuccessful();
}

template < typename T1, typename T2, typename T3 >
RED_INLINE Bool DebugServerPluginImpl::RegisterFunctor( const red::String& name, const std::function< void( T1, T2, T3 ) >& func )
{
	FunctorPtr funcPtr = red::CreateUniquePtr< DebugServerPluginFunctor3< T1, T2, T3 > >( func );
	return m_functors.Insert( name, std::move( funcPtr ) ).IsSuccessful();
}

template < typename T1, typename T2 >
RED_INLINE Bool DebugServerPluginImpl::RegisterFunctor( const red::String& name, const std::function< void( T1, T2 ) >& func )
{
	FunctorPtr funcPtr = red::CreateUniquePtr< DebugServerPluginFunctor2< T1, T2 > >( func );
	return m_functors.Insert( name, std::move( funcPtr ) ).IsSuccessful();
}

template < typename T1 >
RED_INLINE Bool DebugServerPluginImpl::RegisterFunctor( const red::String& name, const std::function< void( T1 ) >& func )
{
	FunctorPtr funcPtr = red::CreateUniquePtr< DebugServerPluginFunctor1< T1 > >( func );
	return m_functors.Insert( name, std::move( funcPtr ) ).IsSuccessful();
}

RED_INLINE Bool DebugServerPluginImpl::RegisterFunctor( const red::String& name, const std::function< void( void ) >& func )
{
	FunctorPtr funcPtr = red::CreateUniquePtr< DebugServerPluginFunctor0 >( func );
	return m_functors.Insert( name, std::move( funcPtr ) ).IsSuccessful();
}

} // red
