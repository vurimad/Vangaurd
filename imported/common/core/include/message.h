/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include <ctime>

#include "messageLocation.h"
#include "messageToken.h"

namespace res
{
	class ResourcePath;
}

namespace tools
{
	enum class MessageSeverity : Uint32
	{
		Success = 0,
		Info,
		Warning,
		Error,
	};

	constexpr Uint32 MessageSeverityToBitflag( const MessageSeverity severity )
	{
		return RED_FLAG( static_cast<Uint32>( severity ) );
	}

	// generic message
	class RED_CORE_API Message final
	{
		RED_USE_MEMORY_POOL( red::PoolDebug );
		RTTI_DECLARE_TYPE( Message );

	public:
		static const AnsiChar* SeverityToString( const MessageSeverity severity );

		Message();
		Message( const MessageSeverity severity, MessageLocationPtr&& location );

		~Message();

		Message( Message&& );
		Message& operator = ( Message&& );

		Message( const Message& );
		Message& operator = ( const Message& );

		void Build( const red::StringView format );

		template< typename T, typename ... Args >
		RED_FORCE_INLINE void Build( const red::StringView format, const T& arg, const Args& ... args )
		{
			for ( Uint32 i = 0; i < format.Length(); ++i )
			{
				if ( format[ i ] == '%' ) // TODO %% should turn into %
				{
					// flush characters read so far as plain text
					if ( i > 0 )
					{
						*this << format.Slice( 0, i );
					}

					// flush the argument
					*this << arg;

					// continue parsing
					return Build( format.SubView( i + 1 ), args ... );
				}
			}
		}

		void SetResourcePath( const res::ResourcePath &path );
		const res::ResourcePath& GetResourcePath() const;

		void SetMessageSource( CName source );
		CName GetMessageSource() const;

		std::time_t GetCreated() const { return m_created; }
		MessageSeverity GetSeverity() const { return m_severity; }
		const IMessageLocation* GetLocation() const { return m_location.Get(); }
		MessageLocationPtr GetLocationPtr() const { return m_location; }
		String ToString() const;
		String ToStringFull() const;

		// push tokens (integers and floating points)
		template< typename T >
		RED_FORCE_INLINE typename std::enable_if<std::is_integral<T>::value || std::is_floating_point<T>::value, Message&>::type operator << ( const T& value )
		{
			String str = ToStringDirect( value );
			MessageTokenPtr token = message::CreateTokenPtr< MessageToken_Text >( str );
			m_tokens.PushBack( token );

			return *this;
		}

		// push tokens (rest of types)
		Message& operator << ( const MessageLocationPtr& location );
		Message& operator << ( const red::StringView string );
		Message& operator << ( const String& string );
		Message& operator << ( const AnsiChar* string );
		Message& operator << ( const CName name );
		Message& operator << ( const res::ResourcePath& resourcePath );
		Message& operator << ( const tools::EditorObjectID& objectID );
		Message& operator << ( const tools::EditorObjectIDPath& objectPath );
		Message& operator << ( const math::Vector3& point );
		Message& operator << ( const math::Box& boundingBox );
		Message& operator << ( const rtti::AccessPath& path );

		Uint32 TokenCount() const { return m_tokens.Size(); }
		const IMessageToken& operator[]( const Uint32 index ) const { return *m_tokens[ index ]; }

	private:
		friend RED_CORE_API Bool operator==( const tools::Message &lhs, const tools::Message &rhs );

		red::DynArray< MessageTokenPtr > m_tokens{ red::PoolBackend() }; // description in tokenized form
		MessageLocationPtr m_location;
		res::ResourcePath m_path;
		CName m_messageSource;
		std::time_t m_created;
		MessageSeverity m_severity = MessageSeverity::Success;
	};

	//////////////////////////////////////////////////////////////////////////

	// generic interface for reporting resource messages
	class RED_CORE_API IMessageReporter
	{
		RED_USE_MEMORY_POOL( red::PoolDebug );

	public:
		virtual ~IMessageReporter();

		// build & report a message
		// Note: format is not printf compatible
		RED_FORCE_INLINE void Report( const MessageSeverity severity, res::ResourcePath file, MessageLocationPtr location, const red::StringView format )
		{
			Message message( severity, std::move( location ) );
			message.SetResourcePath( file );
			message.Build( format );
			Report( message );
		}

		// build & report a message
		// Note: format is not printf compatible
		template< typename ... Args >
		RED_FORCE_INLINE void Report( const MessageSeverity severity, res::ResourcePath file, MessageLocationPtr location, const red::StringView format, const Args& ... args )
		{
			Message message( severity, std::move( location ) );
			message.SetResourcePath( file );
			message.Build( format, args ... );
			Report( message );
		}

		// build & report a message
		// Note: format is not printf compatible
		RED_FORCE_INLINE void Report( const MessageSeverity severity, res::ResourcePath file, const red::StringView format )
		{
			Message message( severity, nullptr );
			message.SetResourcePath( file );
			message.Build( format );
			Report( message );
		}

		// build & report a message
		// Note: format is not printf compatible
		template< typename ... Args >
		RED_FORCE_INLINE void Report( const MessageSeverity severity, res::ResourcePath file, const red::StringView format, const Args& ... args )
		{
			Message message( severity, nullptr );
			message.SetResourcePath( file );
			message.Build( format, args ... );
			Report( message );
		}

		RED_FORCE_INLINE void Report( const MessageSeverity severity, MessageLocationPtr location, const red::StringView format )
		{
			Message message( severity, std::move( location ) );
			message.Build( format );
			Report( message );
		}

		// build & report a message
		// Note: format is not printf compatible
		template< typename ... Args >
		RED_FORCE_INLINE void Report( const MessageSeverity severity, MessageLocationPtr location, const red::StringView format, const Args& ... args )
		{
			Message message( severity, std::move( location ) );
			message.Build( format, args ... );
			Report( message );
		}

		// build & report a message
		// Note: format is not printf compatible
		RED_FORCE_INLINE void Report( const MessageSeverity severity, const red::StringView format )
		{
			Message message( severity, nullptr );
			message.Build( format );
			Report( message );
		}

		// build & report a message
		// Note: format is not printf compatible
		template< typename ... Args >
		RED_FORCE_INLINE void Report( const MessageSeverity severity, const red::StringView format, const Args& ... args )
		{
			Message message( severity, nullptr );
			message.Build( format, args ... );
			Report( message );
		}

		static void SetReporter( IMessageReporter* reporter );
		RED_INLINE static IMessageReporter* GetReporter() { return s_reporter; }

		virtual void Report( Message& message ) = 0;
		virtual void Clear( MessageLocationPtr&& location ) {};

		RED_INLINE static void ReportSafe( Message& message )
		{
			IMessageReporter* reporter = IMessageReporter::GetReporter();

			if ( reporter )
			{
				reporter->Report( message );
			}
		}

		RED_INLINE static void ReportSafe( MessageLocationPtr&& location )
		{
			IMessageReporter* reporter = IMessageReporter::GetReporter();

			if ( reporter )
			{
				reporter->Clear( std::move( location ) );
			}
		}

		template< typename ... Args >
		static void ReportInternal( tools::MessageSeverity severity, MessageLocationPtr&& location, const red::StringView format, const Args& ... args )
		{
			tools::Message message( severity, std::move( location ) );
			message.Build( format, args ... );
			message.SetResourcePath( message.GetLocation()->GetResourcePath() );

			ReportSafe( message );
		}

	private:
		static IMessageReporter* s_reporter;
	};
} // tools

RTTI_DECLARE_ENUM_IN_NAMESPACE( MessageSeverity, tools );

#define RED_REPORT_SUCCESS( location, format, ... )	tools::IMessageReporter::ReportInternal( tools::MessageSeverity::Success,	std::move( location ), format, ##__VA_ARGS__ )
#define RED_REPORT_INFO( location, format, ... )	tools::IMessageReporter::ReportInternal( tools::MessageSeverity::Info,		std::move( location ), format, ##__VA_ARGS__ )
#define RED_REPORT_WARNING( location, format, ... )	tools::IMessageReporter::ReportInternal( tools::MessageSeverity::Warning,	std::move( location ), format, ##__VA_ARGS__ )
#define RED_REPORT_ERROR( location, format, ... )	tools::IMessageReporter::ReportInternal( tools::MessageSeverity::Error,		std::move( location ), format, ##__VA_ARGS__ )
#define RED_REPORT_CLEAR( location )				tools::IMessageReporter::Clear( std::move( location ) )
