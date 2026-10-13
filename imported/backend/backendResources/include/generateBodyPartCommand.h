/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "../../backendEngine/include/resourceCommand.h"

class CMesh;

namespace ent
{
	class SkinnedMeshComponent;
}

namespace tools
{
	class AppearanceNamingParser;

	class GenerateBodyPartCommand : public GeneralResourceCommand
	{
		RTTI_DECLARE_TYPE( GenerateBodyPartCommand );

	public:
		virtual Bool SupportsResource( const rtti::ClassType* resourceClass ) override;
		virtual CName GetName() const override;

		virtual THandle<ResourceCommandOptions> CreateOptions( const THandle<ResourceCommandOptions> suggestedOptions = nullptr ) const override;
		virtual ResourceCommandResult Execute( const THandle<ResourceCommandOptions>& options ) override;

	private:
		THandle< AppearanceNamingParser > LoadParsingRulesForFile() const;
		void FindExternalComponentName( CName& outExternalComponentName ) const;

		ResourceCommandResult GenerateTemplate( const res::ResourcePath& destinationPath ) const;

		static THandle< ent::SkinnedMeshComponent > CreateProperMeshComponent( const THandle< CMesh >& mesh );
	};
}