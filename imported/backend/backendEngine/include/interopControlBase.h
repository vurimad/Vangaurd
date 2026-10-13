/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../common/interop/include/interopUtilsManual.h"

namespace tools
{
	class ObjectCollection;

	class BACKEND_ENGINE_API InteropControlBase : public interop::utils::Object
	{
	public:
		InteropControlBase( const AnsiChar* typeName, ObjectCollection* sharedSelection );
		virtual ~InteropControlBase() override;

	private:
		ObjectCollection* m_selection;
	};
} // namespace tools
