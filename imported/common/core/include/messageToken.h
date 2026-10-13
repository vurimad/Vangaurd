/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "messageFwd.h"

namespace tools
{
	enum class MessageTokenType
	{
		Text,
		Location,
		Tag
	};

	// represents a data that will be displayed for a user in detailed message description
	class RED_CORE_API IMessageToken : public ISerializable
	{
		RED_USE_MEMORY_POOL( red::PoolDebug );
		RTTI_DECLARE_TYPE( IMessageToken );

	public:
		IMessageToken();
		virtual ~IMessageToken();

		virtual String ToString() const = 0;
		virtual MessageTokenType GetType() const = 0;
	};

	// plain text token
	class RED_CORE_API MessageToken_Text final : public IMessageToken
	{
		RTTI_DECLARE_TYPE( MessageToken_Text );

	public:
		MessageToken_Text();
		explicit MessageToken_Text( const String& text );
		explicit MessageToken_Text( const red::StringView text );

		virtual String ToString() const override;
		virtual MessageTokenType GetType() const override;

	private:
		String m_text;
	};

	// name token (can be filtered in the frontend)
	class RED_CORE_API MessageToken_Name final : public IMessageToken
	{
		RTTI_DECLARE_TYPE( MessageToken_Name );

	public:
		MessageToken_Name();
		explicit MessageToken_Name( const CName name );

		const CName& GetTag() const;

		virtual String ToString() const override;
		virtual MessageTokenType GetType() const override;

	private:
		CName m_name;
	};

	// location token
	class RED_CORE_API MessageToken_Location final : public IMessageToken
	{
		RTTI_DECLARE_TYPE( MessageToken_Location );

	public:
		MessageToken_Location();
		explicit MessageToken_Location( MessageLocationPtr location );

		res::ResourcePath GetResourcePath() const;
		void Goto( class IMessageLocationGotoFacilitator* facilitator ) const;

		virtual String ToString() const override;
		virtual MessageTokenType GetType() const override;

	private:
		MessageLocationPtr m_location;
	};

} // tools
