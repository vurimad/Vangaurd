/**
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../backendEngine/include/resourceCommand.h"

namespace tools
{
	class ExportPhysicsCommand : public GeneralResourceCommand
	{
		RTTI_DECLARE_TYPE( ExportPhysicsCommand );

	public:
		virtual Bool SupportsResource( const rtti::ClassType* resourceClass ) override;
		virtual CName GetName() const override;
		virtual ResourceCommandResult Execute( const THandle<ResourceCommandOptions>& options ) override;

		virtual THandle< ResourceCommandOptions > CreateOptions( const THandle< ResourceCommandOptions > suggestedOptions = nullptr ) const override;
	};
}