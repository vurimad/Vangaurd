#include "build.h"

#include "xmlNewWriter.h"
#include "basicTextBuilder.h"

using red::DynArray;

namespace xml 
{

	/// XML writer that writes to the string buffer
	class WriterStringBuf : public Writer
	{
	public:
		WriterStringBuf( CBasicTextBuilder* buf, TFreeFunction freeFunction );
		virtual ~WriterStringBuf();

		virtual void Open( const AnsiChar* name ) override;
		virtual void Openf( STATIC_CHECK_PRINTF_MSC const AnsiChar* txt, ... ) override;
		virtual void Close() override;

		virtual void Attr( const AnsiChar* name, const AnsiChar* value ) override;
		virtual void Attrf( const AnsiChar* name, STATIC_CHECK_PRINTF_MSC const AnsiChar* value, ... ) override;

		virtual void Value( const AnsiChar* txt ) override;
		virtual void Valuef( STATIC_CHECK_PRINTF_MSC const AnsiChar* txt, ... ) override;

	private:
		CBasicTextBuilder*			m_buf;

		TFreeFunction				m_freeFunction;

		DynArray< String >		m_nodeStack{ red::PoolBackend() };
		Bool						m_inNodeHeader;

		void CloseNodeHeader();
	};

	//-----------------------------------------------------------------------------

	Writer::~Writer()
	{
	}

	//-----------------------------------------------------------------------------

	WriterStringBuf::WriterStringBuf( CBasicTextBuilder* buf, TFreeFunction freeFunction )
		: m_buf( buf )
		, m_inNodeHeader( false )
		, m_freeFunction( freeFunction )
	{
	}

	WriterStringBuf::~WriterStringBuf()
	{
		// close opened tags
		while ( !m_nodeStack.Empty() )
			Close();

		// free buffer
		if ( m_freeFunction )
			m_freeFunction( m_buf );

		// cleanup
		m_buf = nullptr;
	}

	void WriterStringBuf::Open( const AnsiChar* name )
	{
		CloseNodeHeader();

		m_buf->Write( "<" );
		m_buf->Write( name );
		m_inNodeHeader = true;

		m_nodeStack.PushBack( name );
	}

	STATIC_CHECK_USE_DECL
	void WriterStringBuf::Openf( const AnsiChar* txt, ... )
	{
		AnsiChar buf[ 2048 ];
		va_list args;

		va_start( args, txt );
		red::VSPrintF( buf, RED_ARRAY_COUNT(buf), txt, args );
		va_end( args );

		Open( buf );
	}

	void WriterStringBuf::Close()
	{
		RED_ASSERT( !m_nodeStack.Empty(), "Trying to close node on empty tag stack" );

		if ( m_nodeStack.Empty() )
			return;

		if ( m_inNodeHeader )
		{
			// just close current tag
			m_buf->Write( "/>");
			m_inNodeHeader = false;
		}
		else
		{
			// full close
			const auto& nodeName = m_nodeStack.Back();
			m_buf->Write( "</" );
			m_buf->Write( nodeName.AsChar() );
			m_buf->Write( ">" );
		}

		m_nodeStack.PopBack();
	}

	void WriterStringBuf::Attr( const AnsiChar* name, const AnsiChar* value )
	{
		if ( m_nodeStack.Empty() || !m_inNodeHeader )
			return;

		m_buf->Write( " " );
		m_buf->Write( name );
		m_buf->Write( "=\"" );
		m_buf->Write( value );
		m_buf->Write( "\"" );
	}

	STATIC_CHECK_USE_DECL
	void WriterStringBuf::Attrf( const AnsiChar* name, const AnsiChar* value, ... )
	{
		AnsiChar buf[ 2048 ];
		va_list args;

		va_start( args, value );
		red::VSPrintF( buf, RED_ARRAY_COUNT(buf), value, args );
		va_end( args );

		Attr( name, buf );
	}

	void WriterStringBuf::Value( const AnsiChar* txt )
	{
		CloseNodeHeader();
		m_buf->Write( txt );
	}

	STATIC_CHECK_USE_DECL
	void WriterStringBuf::Valuef( const AnsiChar* txt, ... )
	{
		AnsiChar buf[ 2048 ];
		va_list args;

		va_start( args, txt );
		red::VSPrintF( buf, RED_ARRAY_COUNT(buf), txt, args );
		va_end( args );

		Value( buf );
	}

	void WriterStringBuf::CloseNodeHeader()
	{
		if ( m_inNodeHeader && !m_nodeStack.Empty() )
		{
			m_buf->Write( ">" );
			m_inNodeHeader = false;
		}
	}

	//-----------------------------------------------------------------------------

	red::UniquePtr<Writer> Writer::Create( class CBasicTextBuilder* printBuffer, TFreeFunction freeFunction )
	{
		// no output
		if ( !printBuffer )
			return nullptr;

		// create wrapper
		return red::UniquePtr<Writer>( RED_NEW( WriterStringBuf )( printBuffer, freeFunction ) );
	}

	red::UniquePtr<Writer> Writer::Create( class CBasicTextBuilder& printBuffer )
	{
		return red::UniquePtr<Writer>( RED_NEW( WriterStringBuf )( &printBuffer, []( class CBasicTextBuilder* ) {} ) );
	}

	//-----------------------------------------------------------------------------

	WriteBlock::WriteBlock( Writer& xml, const AnsiChar* name )
		: m_writer( &xml )
		, m_name( name )
	{
		m_writer->Open( m_name );
	}

	WriteBlock::WriteBlock( WriteBlock& block, const AnsiChar* name )
		: m_writer( block.m_writer )
		, m_name( name )
	{
		m_writer->Open( m_name );
	}

	WriteBlock::~WriteBlock()
	{
		m_writer->Close();
	}

	//-----------------------------------------------------------------------------

} // xml