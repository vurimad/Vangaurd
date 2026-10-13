/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../backendEngine/include/resourceCommand.h"

class CMesh;

namespace ent
{
	class IComponent;
	class TemplateUnpackedView;
}

namespace tools
{
	// Resource command that replaces all SkinnedMeshComponent instances with GarmentSkinnedMeshComponent.
	// All the properties are just copied.
	// We are also checking and correcting component ID's here.
	class ConvertSkinnedMeshComponentsToGarmentsCommand : public GeneralResourceCommand
	{
		RTTI_DECLARE_TYPE( ConvertSkinnedMeshComponentsToGarmentsCommand );

	public:
		virtual Bool SupportsResource( const rtti::ClassType* resourceClass ) override;
		virtual CName GetName() const override;

		virtual THandle<ResourceCommandOptions> CreateOptions( const THandle<ResourceCommandOptions> suggestedOptions = nullptr ) const override;
		virtual ResourceCommandResult Execute( const THandle<ResourceCommandOptions>& options ) override;

	private:
		// Returns true if any of the components were corrected.
		static Bool CorrectComponendIDs( const red::DynArray< THandle< ent::IComponent > >& components );

		// Do the actual conversion of components from SkinnedMeshComponent to GarmentSkinnedMeshComponent.
		static Bool ConvertSkinnedMeshComponents( const red::SharedPtr< ent::TemplateUnpackedView >& view );
	};

} // tools
