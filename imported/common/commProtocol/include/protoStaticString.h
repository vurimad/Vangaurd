#pragma once

namespace comm
{

	/// Static string wrapper (to conform with normal string interface)
	class ProtoStaticString
	{
	public:
		RED_INLINE ProtoStaticString()
		{}

		RED_INLINE ProtoStaticString( const red::AnsiChar* txt )
			: m_text( txt )
		{}

		RED_INLINE const red::AnsiChar* AsChar() const
		{
			return m_text;
		}

	private:
		const red::AnsiChar*		m_text;
	};

} // comm