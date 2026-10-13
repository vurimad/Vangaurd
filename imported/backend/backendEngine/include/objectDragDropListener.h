/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "objectFwd.h"
#include "previewInputListener.h"
#include "../../../common/core/include/message.h"

namespace tools
{

	/// Standard input listener for the object creation
	/// It allows for nice preview of the created object
	class BACKEND_ENGINE_API ObjectDragDropListener : public IPreviewInputListener
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		ObjectDragDropListener( ObjectHandler* handler );
		virtual ~ObjectDragDropListener();

	private:
		ObjectDragDropListener( const ObjectDragDropListener& ) = delete;
		ObjectDragDropListener& operator = ( const ObjectDragDropListener& ) = delete;

		ObjectHandler* m_handler;

		// IPreviewInputListener interface

		/// Handles the start of the drag&drop operation
		virtual IPreviewDragDropHandler* OnDragEnter( PreviewPanel& panel, const String& data, const InputMouseMove& evt ) override final;
		virtual void OnDragDrop() override final;
		virtual void OnDragLeave() override final;
	};

} /// tools