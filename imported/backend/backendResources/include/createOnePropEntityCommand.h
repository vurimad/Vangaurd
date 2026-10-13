/**
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "../../backendEngine/include/resourceCommand.h"
#include "../../../common/worldEntities/include/entityTemplate.h"

class CMesh;

namespace ent
{
	class SkinnedMeshComponent;
}

namespace tools
{
	class CreateOnePropEntityCommand : public GeneralResourceCommand
	{
		RTTI_DECLARE_TYPE( CreateOnePropEntityCommand );

	public:
		virtual Bool SupportsResource( const rtti::ClassType* resourceClass ) override;
		virtual CName GetName() const override;

		virtual THandle<ResourceCommandOptions> CreateOptions( const THandle<ResourceCommandOptions> suggestedOptions = nullptr ) const override;
		virtual ResourceCommandResult Execute( const THandle<ResourceCommandOptions>& options ) override;

		virtual void SetOptionalParameter( const String& param ) override;

	private:
		res::ResourcePath CreateResourcePath( const String& destinationDir, const String& fileName, const String& postFix, const String& fileExtension ) const;

		void CreateEntityTemplate( const res::ResourcePath& filePath, THandle< ent::EntityTemplate >& entityTemplate, ent::TemplateUnpackedViewPtr& view, const red::Set< const rtti::ClassType* >* componentsClassesToRemove = nullptr ) const;
		void CreateAppearanceResource( const res::ResourcePath& filePath, THandle< appearance::AppearanceResource >& appearance ) const;

		ResourceCommandResult CreatePartEntity( const String& destinationDir, const String& fileName, THandle< ent::EntityTemplate >& entityTemplate );
		Bool CreateMeshComponents( ent::TemplateUnpackedViewPtr& view, const red::DynArray< res::ResourcePath >& paths ) const;

		ResourceCommandResult CreateAppearance( const String& destinationDir, const String& fileName, THandle< ent::EntityTemplate >& partTemplate, THandle< appearance::AppearanceResource >& appearance );
		void CreateAppearanceDefinitions( const THandle< appearance::AppearanceResource >& appearance, const THandle< ent::EntityTemplate >& partEntity );

		ResourceCommandResult CreateOnePropEntity( const String& destinationDir, const String& fileName, const THandle< appearance::AppearanceResource >& appearance, THandle< ent::EntityTemplate >& entityTemplate );	
		void CreateFileName( String& fileName, String& category ) const;

		String m_mainDir;
	};
}