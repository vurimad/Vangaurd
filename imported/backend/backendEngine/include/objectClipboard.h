/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../../common/interop/include/interopUtilsManual.h"

namespace tools
{

	class IObjectPersistentCollection;

	/// clipboard handler
	class IObjectClipboard
	{
		DECLARE_INTEROP( IObjectClipboard );
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		IObjectClipboard();
		virtual ~IObjectClipboard();

		/// Store new data in the clipboard, we assume that everything is wrapped in the IObjectPersistentCollection
		virtual const Bool StoreData( const red::SharedPtr< IObjectPersistentCollection >& collection ) = 0;

		/// Get current clipboard data
		virtual red::SharedPtr< IObjectPersistentCollection > GetData() = 0;

		/// Do we have data in the clipboard ?
		virtual const Bool HasData() const = 0;
	};
		
} /// tools