/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "objectClipboard.h"

class GrowableMemoryBuffer;

namespace tools
{

	class IObjectPersistentCollection;

	/// clipboard handler
	class ObjectClipboardWindows : public IObjectClipboard
	{
		DECLARE_INTEROP_DERIVED( ObjectClipboardWindows, IObjectClipboard );

	public:
		ObjectClipboardWindows();
		virtual ~ObjectClipboardWindows();

		/// IObjectClipboard interface
		virtual const Bool StoreData( const red::SharedPtr< IObjectPersistentCollection >& collection ) override final;
		virtual red::SharedPtr< IObjectPersistentCollection > GetData() override final;
		virtual const Bool HasData() const override final;

	private:
		Uint32		m_dataFormat;

		// just copy
		HGLOBAL CreateBinaryData( const GrowableMemoryBuffer& buffer );

		// convert line endings to windows format
		HGLOBAL CreateTextData( const GrowableMemoryBuffer& buffer );
	};

} /// tools