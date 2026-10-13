/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../backendEngine/include/resourceCommand.h"

namespace tools
{
	class RebuildMorphTargetsCommand : public GeneralResourceCommand
	{
		RTTI_DECLARE_TYPE( RebuildMorphTargetsCommand );

	public:
		virtual THandle< ResourceCommandOptions > CreateOptions( const THandle< ResourceCommandOptions > suggestedOptions = nullptr ) const override;
		virtual Bool SupportsResource( const rtti::ClassType* resourceClass ) override;
		virtual CName GetName() const override;

		virtual ResourceCommandResult Execute( const THandle< ResourceCommandOptions >& options ) override;
	};

} // tools
