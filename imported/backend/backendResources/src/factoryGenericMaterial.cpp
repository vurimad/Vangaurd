/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "../../../common/resourceMaterial/include/materialTemplate.h"
#include "../../../common/renderBackend/include/materialLibrary.h"
#include "../../../common/renderBackend/include/materialMetaDefinitionLoader.h"

#include "../../backendEngine/include/factory.h"

class MaterialTemplateFactoryOptions : public FactoryOptions
{
	RTTI_DECLARE_TYPE(MaterialTemplateFactoryOptions);
	RED_BASE_CLASS(FactoryOptions);

public:
	MaterialTemplateFactoryOptions()
	{}

	MaterialTemplateFactoryOptions( const String& path )
		: FactoryOptions(path)
	{}

	virtual ~MaterialTemplateFactoryOptions() {}

	virtual Bool NeedOptions() const override { return true; }

	ERenderStageMaterialTemplate m_materialTemplate;
};


RTTI_BEGIN_TYPE(MaterialTemplateFactoryOptions);
	RTTI_PARENT_TYPE(FactoryOptions);
		RTTI_PROPERTY(m_materialTemplate).editable().inlined();
RTTI_END_TYPE();

class CMaterialTemplateFactory : public IFactory
{
	RTTI_DECLARE_TYPE( CMaterialTemplateFactory );

public:
	CMaterialTemplateFactory()
	{
		m_resourceClass = ClassID< CMaterialTemplate >();
	}

	virtual Bool DoCreate( THandle< CResource > res, const THandle< FactoryOptions >& options ) override
	{
		if ( !GetMaterialLibrary() )
		{
			return false;
		}

		if ( options->m_path.Empty() )
		{
			RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Rendering, "MaterialTemplate creation: Resource has an empty path!" );
			return false;
		}

		if ( auto mat = Cast< CMaterialTemplate >( res ) )
		{
			const CName name = RED_NAME( red::utils::ExtractFileStem( options->m_path ) );
			RED_ASSERT( !name.Empty() );
			mat->SetName( name );

			ERenderStageMaterialTemplate stage = EMST_Transparent;
			if( THandle< MaterialTemplateFactoryOptions > matOptions = Cast< MaterialTemplateFactoryOptions>(options) )
			{
				stage = matOptions->m_materialTemplate;
			}

			red::AbsolutePath createdFiles[ EMFPT_MAX ];
			EMaterialLibraryPathType pathType = options->m_path.BeginsWith( "engine\\" ) ? EMLPT_Engine : EMLPT_Game;
			if ( !GetMaterialLibrary()->CreateShaderMetadataAndCodeFiles( pathType, name, stage, createdFiles ) )
			{
				return false;
			}

			// #TODO: automatic checkout / add to Version Control of .fx and .fxmeta files?

			MaterialMetaDefinition metaDefinition( name );
			CMaterialMetaDefinitionLoader::Logger log( name );
			CMaterialMetaDefinitionLoader loader( false, GetMaterialLibrary(), log );
			if ( loader.Load( name, metaDefinition ) != CMaterialMetaDefinitionLoader::LoaderStatus::Ok )
			{
				return false;
			}

			mat->UpdateParametersList( metaDefinition );
			mat->UpdateTechniques( metaDefinition );
			mat->SetPriority( metaDefinition.m_materialPriority );

			if ( stage == EMST_Fullscreen )
			{
				red::alg::PushBackUnique( mat->m_vertexFactories, MVF_Fullscreen );
			}

			// Can be added for more canUseInXXXX in the following way if needed in the future
			// mat->m_canUseOnParticles	= (stage == EMST_Forward) || (stage == EMST_Transparent) || (stage == EMST_Refractive);

#ifndef NO_RUNTIME_MATERIAL_COMPILATION
			mat->ForceRecompilation();
#endif // NO_RUNTIME_MATERIAL_COMPILATION

			return true;
		}
		else
		{
			RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Rendering, "Resource being created is not a MaterialTemplate!" );
			return false;
		}
	}

	virtual THandle<FactoryOptions> CreateOptions( const res::ResourcePath& path ) const override
	{
		THandle< MaterialTemplateFactoryOptions > options = CreateHandle<MaterialTemplateFactoryOptions>( path.ToString() );
		options->m_materialTemplate = EMST_Transparent;
		return options;
	}
};

RTTI_BEGIN_TYPE( CMaterialTemplateFactory );
	RTTI_PARENT_TYPE( IFactory );
RTTI_END_TYPE();