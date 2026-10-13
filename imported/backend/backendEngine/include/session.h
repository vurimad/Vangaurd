/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace ses
{
	// Provides data to session. When command line contains option "-restore", then last session is loaded. Otherwise it's initial state is empty.
	class ISessionDataProvider
	{
	public:
		typedef red::SharedPtr<ISessionDataProvider> SharedPtr;

		virtual ~ISessionDataProvider() {}

		// Get entry in given session group for given key
		virtual Bool GetEntry( const String& group, const String& key, String& output ) const = 0;

		// Set entry in given session group for given key
		virtual void SetEntry( const String& group, const String& key, const String& input ) = 0;

		// Clear specific session group
		virtual void ClearGroup( const String& group ) = 0;
	};

	// Session - simple list of named parameters. Used to save i.e. opened resources and their editors description required to restore engine after crash.
	class Session
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

	public:
		typedef red::SharedPtr<Session> SharedPtr;

		// Constructs
		Session( ISessionDataProvider::SharedPtr sessionDataProvider, const String& name )
			: m_sessionDataProvider( sessionDataProvider )
			, m_name( name )
		{

		}

		~Session()
		{
			Clear();
		}

		// Creates subsession for this session
		SharedPtr GetSubsession( const String& name ) const
		{
			String subsessionName = String::Printf("%hs/%hs", m_name.AsChar(), name.AsChar());
			return SharedPtr( RED_NEW( Session )( m_sessionDataProvider, subsessionName ) );
		}

		// Clears this session
		void Clear()
		{
			m_sessionDataProvider->ClearGroup( m_name );
		}

		// Get session parameter
		template<class T>
		Bool GetParameter( const String& key, T& output ) const
		{
			String value;
			Bool entryFound = m_sessionDataProvider->GetEntry( m_name, key, value );
			if( !entryFound )
				return false;

			return ::FromString( value, output );
		}

		// Set session parameter
		template<class T>
		void SetParameter( const String& key, const T& input )
		{
			String inputStrAnsi = ::ToStringDirect( input );
			m_sessionDataProvider->SetEntry( m_name, key, inputStrAnsi );
		}

		// Get array parameter
		template<class T>
		Bool GetParameterAsArray( const String& key, const AnsiChar separator, red::DynArray<T>& output ) const
		{
			String value;
			Bool entryFound = m_sessionDataProvider->GetEntry( m_name, key, value );
			if( !entryFound )
				return false;

			return SplitTokens<T>( value, separator, output );
		}

		// Set array parameter
		template<class T>
		void SetArrayAsParameter( const String& key, const AnsiChar separator, const red::DynArray<T>& inputArray )
		{
			String parameterValue;
			for( auto& input : inputArray )
			{
				parameterValue += ::ToStringDirect( input );
				parameterValue += separator;
			}
			m_sessionDataProvider->SetEntry( m_name, key, parameterValue );
		}

	private:
		// Helper method for tokens splitting (used when parsing arrays)
		template<class T>
		Bool SplitTokens( const String& text, AnsiChar chr, red::DynArray<T>& output ) const
		{
			String token;
			for( Uint32 i=0; i<text.Length(); ++i )
			{
				if( text[i] == chr && !text.Empty() )
				{
					T parameterValue;
					
					if( FromString( token, parameterValue ) == false )
						return false;

					output.PushBack(parameterValue);
					token = "";
					continue;
				}

				token += text[i];
			}

			return true;
		}

		ISessionDataProvider::SharedPtr m_sessionDataProvider;		// Provides data to session
		String m_name;		// Name of this session, unique identifier
	};
}
