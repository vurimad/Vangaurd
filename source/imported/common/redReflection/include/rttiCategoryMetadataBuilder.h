/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redContainers/include/string/stringBuilder.h"

namespace rtti
{
	enum OrderPriority : Uint8
	{
		OrderPriority_Top = 100,
		OrderPriority_High = 10,
		OrderPriority_Low = 1,
		OrderPriority_Bottom = 0
	};

	class RED_REFLECTION_API CategoryMetadataBuilder
	{
	public:
		void ChangeContext( CName categoryName );

		CName GetCategory() const;

		CategoryMetadataBuilder& collapsed();

		// sets the order in which the property will appear in the editor
		// the higher the priority, the higher it will be placed
		CategoryMetadataBuilder& order( const OrderPriority order );

	private:
		red::StringBuilder< String > m_stringBuilder;
	};
}