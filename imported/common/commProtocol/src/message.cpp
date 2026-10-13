#include "build.h"
#include "../include/message.h"

namespace comm
{

	Message::Message()
		: m_refs( 1 )
	{
	}

	Message::~Message()
	{
	}

	void Message::AddRef()
	{
		++m_refs;
	}

	void Message::Release()
	{
		if ( 0 == --m_refs )
		{
			delete this;
		}
	}

} // comm