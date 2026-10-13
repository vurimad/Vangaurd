/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace tools
{
	class IMessageLocation;
	using MessageLocationPtr = THandle< IMessageLocation >;

	class IMessageToken;
	using MessageTokenPtr = THandle< IMessageToken >;

	namespace message
	{
		template< typename T, typename... Args >
		RED_INLINE MessageLocationPtr CreateLocationPtr( Args && ... args ) { return CreateHandle< T >( std::forward< Args >( args )... ); }

		template< typename T, typename... Args >
		RED_INLINE MessageTokenPtr CreateTokenPtr( Args && ... args ) { return CreateHandle< T >( std::forward< Args >( args )... ); }
	}
}
