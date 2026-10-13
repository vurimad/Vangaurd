#pragma once

#include "protoStaticString.h"

#include "../../redMemory/include/sharedPtr.h"

namespace comm
{
	// forward declared message ID
	enum class EMessageID : Uint32;

	// base class for the message
	class COMMPROTOCOL_API Message
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		Message();
		virtual ~Message(); // for static creation

		// internal reference counting
		void AddRef();
		void Release();

		// get message type name
		virtual const ProtoStaticString GetName() const = 0;

		// get message ID (unique, static)
		virtual const EMessageID GetID() const = 0;

		// write message using the provided writer interface
		virtual void Serialize( class IProtoWriter& writer ) const = 0;

		// poors man RTTI
		virtual bool IsA( const EMessageID message ) const = 0;

	private:
		Uint32		m_refs;
	};

	using MessageSharedPtr = red::SharedPtr< comm::Message >;

} // comm