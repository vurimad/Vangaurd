/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "editActionHistory.h"
#include "editableCollectionInterop.h"
#include "editableCollection.h"
#include "objectCollection.h"
#include "resourceOptions.h"
#include "resourceCommand.h"

namespace tools
{
	class ResourceCommandWrapper
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		DECLARE_INTEROP( ResourceCommandWrapper );
	public:
		ResourceCommandWrapper( ResourceCommandUniquePtr command, THandle< ResourceCommandOptions >& options );
		virtual ~ResourceCommandWrapper();

		const THandle< ResourceCommandOptions >& GetOptions() const;
		ResourceCommandResult Execute();

	private:
		red::UniquePtr<IEditActionHistory> m_actionHistory;
		red::UniquePtr<ObjectCollection> m_objectCollection;

		THandle< ResourceCommandOptions > m_options;
		ResourceCommandUniquePtr m_command;
	};

	typedef red::SharedPtr< ResourceCommandWrapper > ResourceCommandWrapperSharedPtr;
	typedef red::UniquePtr< ResourceCommandWrapper > ResourceCommandWrapperUniquePtr;

} // namespace tools