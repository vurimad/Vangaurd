/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../common/redMemory/include/weakPtr.h"
#include "../../../common/world/include/worldNodePinFilters.h"
#include "objectCollection.h"

namespace rtti
{
	class ClassType;
}

namespace tools
{
	class PreviewFilters;

	/// an interop wrapper for the pin scene node
	class BACKEND_ENGINE_API PreviewPinFilters
	{
		DECLARE_INTEROP( PreviewPinFilters );

	public:
		PreviewPinFilters( IEditActionHistory& actionHistory );
		~PreviewPinFilters();

		// bind filters
		void Bind( const red::WeakPtr< world::NodePinFilters >& filters );

		// get state of filter
		Bool IsEnabled( const rtti::ClassType& nodeType ) const;

		// get state of filter
		void SetEnabled( const rtti::ClassType& nodeType, Bool state );

		void ApplyChanges();
		void CancelChanges();
		void EnableAll();
		void Invert();

	private:
		red::WeakPtr< world::NodePinFilters > m_filters;
		world::NodePinFilters m_savedFilters;
		interop::InteropNotifier m_onChangedNotifier;

		ObjectCollection m_filtersWrapper;
		THandle< class FiltersProperties > m_filtersProperties;
	};

} // tools