/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "../../backendEngine/include/factory.h"
#include "../../../common/worldEntities/include/appearanceResource.h"
#include "../../backendEngine/include/editableCollectionCustomPropertyEditor.h"
#include "../../../common/generalEditorBackend/include/appearancesConfig.h"
#include "../../../common/resource/include/jsonResource.h"

static const CName s_customBaseTypeName = RED_NAME_CONSTEXPR( "Custom" );

class AppearanceResourceFactoryOptions : public FactoryOptions
{
	RTTI_DECLARE_TYPE( AppearanceResourceFactoryOptions );

public:
	AppearanceResourceFactoryOptions()
		: m_baseType( s_customBaseTypeName )
	{
	}

	AppearanceResourceFactoryOptions( const CName& baseType )
		: m_baseType( baseType )
	{
	}

	const CName& GetBaseType() const { return m_baseType; }

	virtual Bool NeedOptions() const override
	{
		return true;
	}

private:
	CName m_baseType;
};

/// Factory for the character resource containing appearances list
class AppearanceResourceFactory : public IFactory
{
	RTTI_DECLARE_TYPE( AppearanceResourceFactory );

public:
	AppearanceResourceFactory()
	{
		m_resourceClass = ClassID< appearance::AppearanceResource >();
	}

	virtual Bool DoCreate( THandle< CResource > res, const THandle< FactoryOptions >& options ) override
	{
		Cast< appearance::AppearanceResource >( res )->SetBaseType(
			Cast< AppearanceResourceFactoryOptions >( options )->GetBaseType() );

		return true;
	}

	virtual THandle<FactoryOptions> CreateOptions( const res::ResourcePath& path ) const override
	{
		return CreateHandle< AppearanceResourceFactoryOptions >();
	}
};

namespace tools
{
	static const String s_configurationFilePath( "base//appearances_mappings.json" );

	class AppearanceBaseType_CustomEditor final : public IEditableCustomProperty
	{
		RTTI_DECLARE_TYPE( AppearanceBaseType_CustomEditor );

	public:
		AppearanceBaseType_CustomEditor()
		{
		}

		virtual const Bool HandlesCustomType( const CName customTypeName, const prv::EditableCollectionRootItem* root ) const override
		{
			return customTypeName == RED_NAME_CONSTEXPR_NOREG( "appearanceBaseType" );
		}

		virtual Bool HasOptions() const override
		{
			return true;
		}

		virtual const red::DynArray<String>& GetOptions() const override
		{
			if ( s_options.Empty() )
			{
				TResRef< JsonResource > configurationFile( res::ResourcePath::Build( s_configurationFilePath ) );
				RED_FATAL_ASSERT( configurationFile.Get(), "Missing appearances editor configuration file %hs", s_configurationFilePath.AsChar() );

				THandle< AppearancesConfig > configRoot = configurationFile->GetRoot< AppearancesConfig >();
				if ( !configRoot )
				{
					RED_LOG_ERROR( "[Appearance Factory configuration] Missing json root node" );
					return s_options;
				}

				for ( const AppearanceConfigMapping& mapping : configRoot->GetMappings() )
				{
					if ( !mapping.m_name.Empty() )
					{
						s_options.PushBack( mapping.m_name.AsChar() );
					}
				}
				s_options.PushBack( s_customBaseTypeName.AsChar() );
			}
			return s_options;
		}

		virtual CName GetControlType() const override
		{
			return RED_NAME( red::String::Printf( "%hs:Restricted", ::GetTypeName<CName>().AsChar() ) );
		}

	private:
		static red::DynArray< String > s_options;
	};

	red::DynArray< String > AppearanceBaseType_CustomEditor::s_options;
}


RTTI_BEGIN_TYPE( AppearanceResourceFactory );
RTTI_PARENT_TYPE( IFactory );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE( AppearanceResourceFactoryOptions );
RTTI_PARENT_TYPE( FactoryOptions );
RTTI_PROPERTY( m_baseType ).editable().customEditor( "appearanceBaseType" );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( AppearanceBaseType_CustomEditor, tools );
RTTI_PARENT_TYPE( tools::IEditableCustomProperty );
RTTI_END_TYPE();