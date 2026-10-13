/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "../include/interopControlBase.h"
#include "../include/objectCollection.h"

namespace tools
{
	InteropControlBase::InteropControlBase( const AnsiChar* typeName, ObjectCollection* sharedSelection )
		: ::interop::utils::Object( typeName )
		, m_selection( sharedSelection )
	{
		::interop::utils::Object::RegisterFunctor<void>( "updateGlobalSelection",
			[this]( interop::IContext& ctx )
		{
			if ( m_selection )
			{
				m_selection->UpdateGlobalSelection();
			}
		} );
	}

	InteropControlBase::~InteropControlBase() = default;
} // namespace tools