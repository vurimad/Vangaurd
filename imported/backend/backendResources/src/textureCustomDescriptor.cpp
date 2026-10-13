#include "build.h"

#include "../../backendEngine/include/batchTool.h"
#include "../../backendMaterial/include/bitmapTextureCooker.h"

#include "../../../common/resourceMaterial/include/bitmapTexture.h"
#include "../../../common/redJobs2/include/jobBuilder.h"

namespace tools
{
	class TextureCustomPropertiesDescriptor : public IResourceCustomPropertiesDescriptor
	{
		RTTI_DECLARE_TYPE( TextureCustomPropertiesDescriptor );

	public:
		TextureCustomPropertiesDescriptor();

		virtual void ApplyChanges( const job::RunContext& runContext ) const override;
		virtual red::Map< CName, Bool >& GetSwitches() override;

		virtual Bool SupportsResource( const rtti::ClassType* resourceClass ) const;
		virtual const String& GetTitle() const override;

		virtual void GetCustomEditableProperties( rtti::EditableProperties& outRootProperties ) const override;
		virtual Bool ReadCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, red::SharedPtr<rtti::ValueHolder>& outValue ) const override;
		virtual Bool WriteCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, const red::SharedPtr<rtti::ValueHolder>& newValue ) override;

	private:
		ResourceBaseProperties m_properties;

		Int32 m_mipBiasPCIncrease = 0;
		Int32 m_mipBiasConsoleIncrease = 0;
	};

	TextureCustomPropertiesDescriptor::TextureCustomPropertiesDescriptor()
		: m_properties( ClassID< CBitmapTexture >(), nullptr )
	{}

	void TextureCustomPropertiesDescriptor::ApplyChanges( const job::RunContext& runContext ) const
	{
		job::Builder builder( runContext );
		builder.DispatchParallelForJob( "BatchTool/ApplyChanges/Custom/Texture", [ this ]() { return m_resources.Size(); },
			[this]( Uint32 index, const job::RunContext& )
		{
			const THandle<CResource>& resource = m_resources[ index ];
			if ( THandle< CBitmapTexture > texture = Cast< CBitmapTexture >( resource ) )
			{
				m_properties.ApplyChanges( resource );

				if ( IsSwitchChecked( RED_NAME_CONSTEXPR( "platformMipBiasPCIncrease" ) ) )
				{
					texture->EnableMipChain( true );

					Int32 bias = texture->GetTextureSetup().m_platformMipBiasPC;
					bias = Max( 0, Min( bias + m_mipBiasPCIncrease, texture->GetMaxMipCount() ) );
					texture->SetDownscaleBiasForCookingPlatform( ECookingPlatform::PLATFORM_PC, bias );
				}

				if ( IsSwitchChecked( RED_NAME_CONSTEXPR( "platformMipBiasConsoleIncrease" ) ) )
				{
					texture->EnableMipChain( true );

					Int32 bias = texture->GetTextureSetup().m_platformMipBiasConsole;
					bias = Max( 0, Min( bias + m_mipBiasConsoleIncrease, texture->GetMaxMipCount() ) );
					texture->SetDownscaleBiasForCookingPlatform( ECookingPlatform::PLATFORM_PS4, bias );
				}

				tools::BitmapTextureCookerSetup cookerSetup( *texture );
				cookerSetup.cookPC = true;
				cookerSetup.cookDURANGO = true;
				cookerSetup.cookORBIS = true;
				cookerSetup.cookWINDOWS_SERVER = true;

				tools::BitmapTextureCooker cooker( cookerSetup );
				cooker.DoCook();

				texture->ReleaseRenderResource();
			}
		} );
	}

	red::Map< CName, Bool >& TextureCustomPropertiesDescriptor::GetSwitches()
	{
		return m_properties.GetSwitches();
	}

	Bool TextureCustomPropertiesDescriptor::SupportsResource( const rtti::ClassType* resourceClass ) const
	{
		return resourceClass->IsA< CBitmapTexture >();
	}

	const String& TextureCustomPropertiesDescriptor::GetTitle() const
	{
		static const String s_title( "Properties" );
		return s_title;
	}

	void TextureCustomPropertiesDescriptor::GetCustomEditableProperties( rtti::EditableProperties& outRootProperties ) const
	{
		ClassID< CBitmapTexture >()->GetEditableProperties( outRootProperties );

		rtti::ClassEditablePropertyInfo increaseBiasPCInfo;
		increaseBiasPCInfo.m_name = RED_NAME_CONSTEXPR( "platformMipBiasPCIncrease" );
		increaseBiasPCInfo.m_category = RED_NAME_CONSTEXPR( "MipBiasIncrease" );
		increaseBiasPCInfo.m_type = GetTypeObject< Int32 >();
		outRootProperties.Add( increaseBiasPCInfo );

		rtti::ClassEditablePropertyInfo increaseBiasConsoleInfo;
		increaseBiasConsoleInfo.m_name = RED_NAME_CONSTEXPR( "platformMipBiasConsoleIncrease" );
		increaseBiasConsoleInfo.m_category = RED_NAME_CONSTEXPR( "MipBiasIncrease" );
		increaseBiasConsoleInfo.m_type = GetTypeObject< Int32 >();
		outRootProperties.Add( increaseBiasConsoleInfo );
	}

	Bool TextureCustomPropertiesDescriptor::ReadCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, red::SharedPtr<rtti::ValueHolder>& outValue ) const
	{
		if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "platformMipBiasPCIncrease" ) )
		{
			return GetTypeObject< Int32 >()->ReadValue( ctx, &m_mipBiasPCIncrease, rtti::AccessPath(), outValue );
		}
		
		if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "platformMipBiasConsoleIncrease" ) )
		{
			return GetTypeObject< Int32 >()->ReadValue( ctx, &m_mipBiasConsoleIncrease, rtti::AccessPath(), outValue );
		}

		String path = propertyName.AsChar() + restOfThePath.ToString();
		return GetTypeObject< CBitmapTexture >()->ReadValue( ctx, m_properties.GetResourceTemplate().Get(), rtti::AccessPath( path.AsChar() ), outValue );
	}

	Bool TextureCustomPropertiesDescriptor::WriteCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, const red::SharedPtr<rtti::ValueHolder>& newValue )
	{
		if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "platformMipBiasPCIncrease" ) )
		{
			return GetTypeObject< Int32 >()->WriteValue( ctx, &m_mipBiasPCIncrease, rtti::AccessPath(), *newValue.Get(), false );
		}

		if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "platformMipBiasConsoleIncrease" ) )
		{
			return GetTypeObject< Int32 >()->WriteValue( ctx, &m_mipBiasConsoleIncrease, rtti::AccessPath(), *newValue.Get(), false );
		}

		String path = propertyName.AsChar() + restOfThePath.ToString();
		return GetTypeObject< CBitmapTexture >()->WriteValue( ctx, m_properties.GetResourceTemplate().Get(), rtti::AccessPath( path.AsChar() ), *newValue.Get(), false );
	}

}

RTTI_BEGIN_TYPE_IN_NAMESPACE( TextureCustomPropertiesDescriptor, tools );
	RTTI_PARENT_TYPE( tools::IResourceCustomPropertiesDescriptor );
RTTI_END_TYPE();