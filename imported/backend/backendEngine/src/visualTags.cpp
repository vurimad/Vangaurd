/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "visualTags.h"

#include "../../common/gameTweakDB/include/tweakDB.h"
#include "../../common/gameTweakDB/include/tweakDBLoader.h"
#include "../../common/gameTweakDB/include/tweakDBParserNodes.h"
#include "../../common/resource/include/jsonResource.h"
#include "../../common/redReflection/include/gatheredResource.h"

namespace
{
	static const AnsiChar c_variableName[] = "visualTags";
	static const AnsiChar c_weaponSchema[] = "WeaponItem";
	static const AnsiChar c_weaponPartSchema[] = "Item";
}

namespace tools
{
	res::GatheredResource s_configurationFile( RED_CONST_RESOURCEPATH( "base\\gameplay\\static_data\\database\\visual_tags.json" ), GRF_Backend );
	res::GatheredResource s_weapon_configurationFile( RED_CONST_RESOURCEPATH( "base\\gameplay\\static_data\\database\\weapon_visual_tags.json" ), GRF_Backend );

	struct VisualTagsDefinition : public ISerializable
	{
		RTTI_DECLARE_TYPE( VisualTagsDefinition );
	public:
		VisualTagsDefinition() = default;
		~VisualTagsDefinition() = default;

		CName m_name;
	};

	struct VisualTagsGroup : public ISerializable
	{
		RTTI_DECLARE_TYPE( VisualTagsGroup );
	public:
		VisualTagsGroup() = default;
		~VisualTagsGroup() = default;

		CName m_name;
		red::DynArray< VisualTagsDefinition > m_tags;
	};

	struct VisualTagsSchema : public ISerializable
	{
		RTTI_DECLARE_TYPE( VisualTagsSchema );
	public:
		VisualTagsSchema() = default;
		~VisualTagsSchema() = default;

		CName m_name;
		red::DynArray< VisualTagsGroup > m_categories;
		red::DynArray< VisualTagsGroup > m_presets;
	};

	class VisualTagsRoot : public ISerializable
	{
		RTTI_DECLARE_TYPE( VisualTagsRoot );
	public:
		VisualTagsRoot() = default;
		~VisualTagsRoot() = default;

		red::DynArray< VisualTagsSchema > m_schemas;
	};

	///
	///
	///

	Bool VisualTagHelper::LoadFromConfigFile()
	{
		res::ResourceTokenHandle token = s_configurationFile.IssueLoadingRequest();
		THandle< JsonResource > configurationFile = token->WaitUntilLoaded< JsonResource >();

		THandle< tools::VisualTagsRoot > configRoot = configurationFile->GetRoot< tools::VisualTagsRoot >();
		if ( !configRoot )
		{
			RED_FATAL_ASSERT( "[Visual Tags configuration] Missing json root node" );
			return false;
		}

		m_configRoot = configRoot;

		return true;
	}


	// Load weapon definition
	Bool VisualTagHelper::LoadWeaponConfigFile()
	{
		res::ResourceTokenHandle token = s_weapon_configurationFile.IssueLoadingRequest();
		THandle< JsonResource > configurationFile = token->WaitUntilLoaded< JsonResource >();

		THandle< tools::VisualTagsRoot > root = configurationFile->GetRoot< tools::VisualTagsRoot >();
		if ( !root )
		{
			RED_FATAL_ASSERT( "[Visual Tags configuration] Missing json root node" );
			return false;
		}

		m_configRoot->m_schemas.PushBack( root->m_schemas );
		return false;
	}

	red::DynArray< proxy::VisualTagItem > VisualTagHelper::GetSchemaTags( const String& schema )
	{
		CName name = RED_NAME( schema );
		red::DynArray< proxy::VisualTagItem > tags;

		for ( const VisualTagsSchema& item : m_configRoot->m_schemas )
		{
			if ( item.m_name == name )
			{
				for ( const VisualTagsGroup& category : item.m_categories )
				{
					for ( const VisualTagsDefinition& defintion : category.m_tags )
					{
						tags.PushBack( proxy::VisualTagItem{ defintion.m_name.AsChar(), category.m_name.AsChar() } );
					}
				}
			}
		}

		return tags;
	}

	red::DynArray< proxy::VisualTagItem > VisualTagHelper::GetSchemaPresets( const String& schema )
	{
		CName name = RED_NAME( schema );
		red::DynArray< proxy::VisualTagItem > tags;

		for ( const VisualTagsSchema& item : m_configRoot->m_schemas )
		{
			if ( item.m_name == name )
			{
				for ( const VisualTagsGroup& preset : item.m_presets )
				{
					for ( const VisualTagsDefinition& defintion : preset.m_tags )
					{
						tags.PushBack( proxy::VisualTagItem{ defintion.m_name.AsChar(), preset.m_name.AsChar() } );
					}
				}
			}
		}

		return tags;
	}

	red::DynArray< CName > VisualTagHelper::GetSchemas()
	{
		red::DynArray< CName > shemas;

		for ( const VisualTagsSchema& item : m_configRoot->m_schemas )
		{
			shemas.PushBack( item.m_name );
		}

		return shemas;
	}

	///
	///
	///	

	VisualTagHelper::VisualTagHelper()
	{
		LoadFromConfigFile();
		LoadWeaponConfigFile();
	}

	VisualTagHelper::~VisualTagHelper() = default;

	void VisualTagHelper::Refresh( Bool force )
	{
	}

	void VisualTagHelper::SetupInterop( VisualTagHelper* ptr, ::interop::utils::Object& op )
	{
		op.RegisterFunctor< red::DynArray< proxy::VisualTagItem >, String >( "visualTags", [ ptr ]( ::interop::IContext& ctx, String schema )
			{ 
				return ptr->GetSchemaTags( schema );
			} );

		op.RegisterFunctor< red::DynArray< CName > >( "visualTagsSchemas", [ ptr ]( ::interop::IContext& ctx )
		{
			return ptr->GetSchemas();
		} );

		op.RegisterFunctor< red::DynArray< proxy::VisualTagItem >, String >( "visualTagsPresets", [ ptr ]( ::interop::IContext& ctx, String schema )
		{
			return ptr->GetSchemaPresets( schema );
		} );
	}
}

RTTI_BEGIN_TYPE_IN_NAMESPACE( VisualTagsDefinition, tools );
	RTTI_PROPERTY( m_name );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( VisualTagsGroup, tools );
	RTTI_PARENT_TYPE( ISerializable );
	RTTI_PROPERTY( m_name );
	RTTI_PROPERTY( m_tags );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( VisualTagsSchema, tools );
	RTTI_PARENT_TYPE( ISerializable );
	RTTI_PROPERTY( m_name );
	RTTI_PROPERTY( m_categories );
	RTTI_PROPERTY( m_presets );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( VisualTagsRoot, tools );
	RTTI_PARENT_TYPE( ISerializable );
	RTTI_PROPERTY( m_schemas );
RTTI_END_TYPE();