/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "graphDescriptor.h"
#include "graphEditorTreeNode.h"

namespace tools
{
	class BACKEND_ENGINE_API ITreeDescriptor : public IGraphDescriptor
	{
		RTTI_DECLARE_TYPE( ITreeDescriptor )
	public:
		virtual void GetGraphRootNodes( red::DynArray< ITreeNodeDescriptor* >& outRootNodes ) const;

		virtual Bool IsTopDownTree() const override;

	protected:
		void InspectBranchRec( NodeInspector& nodeInspector, ITreeNodeDescriptor& node );
	};
} // namespace tools