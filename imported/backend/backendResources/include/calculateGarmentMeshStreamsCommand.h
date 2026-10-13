/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../backendEngine/include/resourceCommand.h"

class CMesh;

namespace tools
{
	class CalculateGarmentMeshStreamsCommand : public GeneralResourceCommand
	{
		RTTI_DECLARE_TYPE( CalculateGarmentMeshStreamsCommand );

	public:
		virtual Bool SupportsResource( const rtti::ClassType* resourceClass ) override;
		virtual CName GetName() const override;

		virtual THandle<ResourceCommandOptions> CreateOptions( const THandle<ResourceCommandOptions> suggestedOptions = nullptr ) const override;
		virtual ResourceCommandResult Execute( const THandle<ResourceCommandOptions>& options ) override;
	};

} // tools
