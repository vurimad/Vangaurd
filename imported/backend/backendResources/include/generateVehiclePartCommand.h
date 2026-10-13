/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "../../backendEngine/include/resourceCommand.h"

class CMesh;

namespace ent
{
	class MeshComponent;
	class PhysicalMeshComponent;
	class SkinnedMeshComponent;
}

namespace tools
{
	class AppearanceNamingParser;

	class GenerateVehiclePartCommand : public GeneralResourceCommand
	{
		RTTI_DECLARE_TYPE( GenerateVehiclePartCommand );

	public:
		virtual Bool SupportsResource( const rtti::ClassType* resourceClass ) override;
		virtual CName GetName() const override;

		virtual THandle<ResourceCommandOptions> CreateOptions( const THandle<ResourceCommandOptions> suggestedOptions = nullptr ) const override;
		virtual ResourceCommandResult Execute( const THandle<ResourceCommandOptions>& options ) override;

	private:
		THandle< AppearanceNamingParser > LoadParsingRulesForFile() const;
		/*void FindExternalComponentName( CName& outExternalComponentName ) const;*/

		ResourceCommandResult GenerateTemplate( const res::ResourcePath& destinationPath ) const;

		static THandle< ent::PhysicalMeshComponent > CreateProperMeshComponent( const THandle<CMesh>& mesh );
		static THandle< ent::SkinnedMeshComponent > CreateProperSkinnedMeshComponent( const THandle< CMesh >& mesh );
	};
}