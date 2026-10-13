#include "build.h"

#include "../../backendEngine/include/batchTool.h"

#include "../../../common/redJobs2/include/jobBuilder.h"
#include "../../../common/resourceMaterial/include/multilayerMask.h"
#include "../../backendMaterial/include/mlmaskView.h"
#include "../../backendMaterial/include/multilayerMaskBuilder.h"
#include "../../../common/resourceMaterial/include/sourceTextureData.h"
#include "../../backendEngine/include/editableCollectionCustomPropertyEditor.h"

namespace tools
{
	class BiasCopyProperty : public IEditableCustomProperty
	{
		RTTI_DECLARE_TYPE( BiasCopyProperty );

	public:
		virtual CName GetControlType() const override
		{
			static const CName sControlType = RED_NAME_CONSTEXPR( "String:Restricted" );
			return sControlType;
		}

		virtual const red::DynArray<String>& GetOptions() const override
		{
			static const red::DynArray< String > sOptions = red::DynArray< String >{ "PC", "Console" };
			return sOptions;
		}

		virtual const Bool HandlesCustomType( const CName customTypeName, const prv::EditableCollectionRootItem* root ) const override
		{
			static const CName sHandledType = RED_NAME_CONSTEXPR( "CopyBiasProperty" );
			return customTypeName == sHandledType;
		}

		virtual Bool HasOptions() const override
		{
			return true;
		}
	};

	class MultilayerMaskCustomDescriptor : public IResourceCustomPropertiesDescriptor
	{
		RTTI_DECLARE_TYPE( MultilayerMaskCustomDescriptor );

	public:
		MultilayerMaskCustomDescriptor();

		virtual void Initialize( const red::DynArray< THandle< CResource > >& resources ) override;
		virtual void ApplyChanges( const job::RunContext& runContext ) const override;
		virtual Bool SupportsResource( const rtti::ClassType* resourceClass ) const override;
		virtual const String& GetTitle() const override;

		virtual void GetCustomEditableProperties( rtti::EditableProperties& outRootProperties ) const override;
		virtual Bool ReadCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, red::SharedPtr<rtti::ValueHolder>& outValue ) const override;
		virtual Bool WriteCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, const red::SharedPtr<rtti::ValueHolder>& newValue ) override;

	private:
		THandle< class AggregatedOperations > m_aggregatedOperations;
		THandle< class ExplicitBiases > m_explicitBiases;
	};

	class AggregatedOperations : public ISerializable
	{
	public:
		virtual void GetCustomEditableProperties( rtti::EditableProperties& outRootProperties ) const override
		{
			rtti::ClassEditablePropertyInfo copyBiasInfo;
			copyBiasInfo.m_name = RED_NAME_CONSTEXPR( "CopyBiasFrom" );
			copyBiasInfo.m_category = RED_NAME_CONSTEXPR( "AggregatedOperations" );
			copyBiasInfo.m_type = GetTypeObject< String >();
			copyBiasInfo.m_customTypeName = RED_NAME_CONSTEXPR( "CopyBiasProperty" );
			outRootProperties.Add( copyBiasInfo );

			rtti::ClassEditablePropertyInfo increaseBiasPCInfo;
			increaseBiasPCInfo.m_name = RED_NAME_CONSTEXPR( "PCBiasIncrease" );
			increaseBiasPCInfo.m_category = RED_NAME_CONSTEXPR( "AggregatedOperations" );
			increaseBiasPCInfo.m_type = GetTypeObject< Int32 >();
			outRootProperties.Add( increaseBiasPCInfo );

			rtti::ClassEditablePropertyInfo increaseBiasConsoleInfo;
			increaseBiasConsoleInfo.m_name = RED_NAME_CONSTEXPR( "ConsoleBiasIncrease" );
			increaseBiasConsoleInfo.m_category = RED_NAME_CONSTEXPR( "AggregatedOperations" );
			increaseBiasConsoleInfo.m_type = GetTypeObject< Int32 >();
			outRootProperties.Add( increaseBiasConsoleInfo );
		}

		virtual Bool ReadCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, red::SharedPtr<rtti::ValueHolder>& outValue ) const override
		{
			if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "PCBiasIncrease" ) )
			{
				return GetTypeObject< Int32 >()->ReadValue( ctx, &m_mipBiasPCIncrease, rtti::AccessPath(), outValue );
			}

			if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "ConsoleBiasIncrease" ) )
			{
				return GetTypeObject< Int32 >()->ReadValue( ctx, &m_mipBiasConsoleIncrease, rtti::AccessPath(), outValue );
			}

			if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "CopyBiasFrom" ) )
			{
				return GetTypeObject< String >()->ReadValue( ctx, &m_copyFrom, rtti::AccessPath(), outValue );
			}
			return ISerializable::ReadCustomEditableProperty( ctx, propertyName, restOfThePath, outValue );
		}

		virtual Bool WriteCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, const red::SharedPtr<rtti::ValueHolder>& newValue ) override
		{
			if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "PCBiasIncrease" ) )
			{
				return GetTypeObject< Int32 >()->WriteValue( ctx, &m_mipBiasPCIncrease, rtti::AccessPath(), *newValue.Get(), false );
			}

			if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "ConsoleBiasIncrease" ) )
			{
				return GetTypeObject< Int32 >()->WriteValue( ctx, &m_mipBiasConsoleIncrease, rtti::AccessPath(), *newValue.Get(), false );
			}

			if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "CopyBiasFrom" ) )
			{
				return GetTypeObject< String >()->WriteValue( ctx, &m_copyFrom, rtti::AccessPath(), *newValue.Get() );
			}
			return ISerializable::WriteCustomEditableProperty( ctx, propertyName, restOfThePath, newValue );
		}

		void ApplyChanges( const THandle< Multilayer_Mask >& mask, const MultilayerMaskCustomDescriptor* descriptor )
		{
			if ( descriptor->IsSwitchChecked( RED_NAME_CONSTEXPR( "CopyBiasFrom" ) ) && !m_copyFrom.Empty() )
			{
				ECookingPlatform source, target;
				if ( m_copyFrom.EqualsNC( "PC" ) )
				{
					source = ECookingPlatform::PLATFORM_PC;
					target = ECookingPlatform::PLATFORM_PS4;
				}
				else
				{
					source = ECookingPlatform::PLATFORM_PS4;
					target = ECookingPlatform::PLATFORM_PC;
				}

				for ( Uint32 i = 1; i < mask->GetLayerCount(); ++i )
				{
					Int32 bias = mask->GetLayerBias( i, source );
					mask->SetLayerBias( i, target, bias );
				}
			}

			if ( descriptor->IsSwitchChecked( RED_NAME_CONSTEXPR( "PCBiasIncrease" ) ) )
			{
				for ( Uint32 i = 1; i < mask->GetLayerCount(); ++i )
				{
					Int32 bias = mask->GetLayerBias( i, ECookingPlatform::PLATFORM_PC );
					bias = Max( 0, Min( bias + m_mipBiasPCIncrease, (Int32)mask->GetMaxLayerBias( i ) ) );
					mask->SetLayerBias( i, ECookingPlatform::PLATFORM_PC, bias );
				}
			}

			if ( descriptor->IsSwitchChecked( RED_NAME_CONSTEXPR( "ConsoleBiasIncrease" ) ) )
			{
				for ( Uint32 i = 1; i < mask->GetLayerCount(); ++i )
				{
					Int32 bias = mask->GetLayerBias( i, ECookingPlatform::PLATFORM_PS4 );
					bias = Max( 0, Min( bias + m_mipBiasConsoleIncrease, (Int32)mask->GetMaxLayerBias( i ) ) );
					mask->SetLayerBias( i, ECookingPlatform::PLATFORM_PS4, bias );
				}
			}
		}

	private:
		Int32 m_mipBiasPCIncrease = 0;
		Int32 m_mipBiasConsoleIncrease = 0;

		String m_copyFrom;
	};

	class ExplicitBiases : public ISerializable
	{
	public:
		void Initialize( const red::DynArray< THandle< CResource > >& resources )
		{
			Uint32 maxLayersCount = 0;
			for ( const THandle< CResource >& res : resources )
			{
				THandle< Multilayer_Mask > mask = Cast< Multilayer_Mask >( res );
				if ( !mask )
				{
					continue;
				}

				if ( mask->GetLayerCount() > maxLayersCount )
				{
					maxLayersCount = mask->GetLayerCount();
				}

				for ( Uint32 i = 0; i < mask->GetLayerCount(); ++i )
				{
					Uint32 layerBias = mask->GetMaxLayerBias( i );
					if ( layerBias > m_maxBias )
					{
						m_maxBias = layerBias;
					}
				}
			}

			if ( maxLayersCount > 1 )
			{
				m_biasPC.Resize( maxLayersCount - 1 );
				m_biasConsole.Resize( maxLayersCount - 1 );
			}
		}

		virtual void GetCustomEditableProperties( rtti::EditableProperties& outRootProperties ) const override
		{
			rtti::ClassEditablePropertyInfo biasPCInfo;
			biasPCInfo.m_category = RED_NAME_CONSTEXPR( "PCBias" );
			biasPCInfo.m_type = GetTypeObject< Uint32 >();
			biasPCInfo.m_minValue = 0;
			biasPCInfo.m_maxValue = (Float)m_maxBias;

			for ( Uint32 i = 0; i < m_biasPC.Size(); ++i )
			{
				biasPCInfo.m_name = RED_NAME( String::Printf( "PCBias%d", i + 1 ) );
				outRootProperties.Add( biasPCInfo );
			}

			rtti::ClassEditablePropertyInfo biasConsoleInfo;
			biasConsoleInfo.m_category = RED_NAME_CONSTEXPR( "ConsoleBias" );
			biasConsoleInfo.m_type = GetTypeObject< Uint32 >();
			biasConsoleInfo.m_minValue = 0;
			biasConsoleInfo.m_maxValue = (Float)m_maxBias;

			for ( Uint32 i = 0; i < m_biasConsole.Size(); ++i )
			{
				biasConsoleInfo.m_name = RED_NAME( String::Printf( "ConsoleBias%d", i + 1 ) );
				outRootProperties.Add( biasConsoleInfo );
			}
		}

		virtual Bool ReadCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, red::SharedPtr<rtti::ValueHolder>& outValue ) const override
		{
			String propNameString( propertyName.AsChar() );

			if ( propNameString.BeginsWith( "PCBias" ) && !propNameString.EndsWith( "Increase" ) )
			{
				String indexString = propNameString.StringAfter( "PCBias" );
				Int32 index = FromStringDirect< Int32 >( indexString );

				if ( index <= (Int32)m_biasPC.Size() )
				{
					return GetTypeObject< Uint32 >()->ReadValue( ctx, &m_biasPC[index - 1], restOfThePath, outValue );
				}
			}

			if ( propNameString.BeginsWith( "ConsoleBias" ) && !propNameString.EndsWith( "Increase" ) )
			{
				String indexString = propNameString.StringAfter( "ConsoleBias" );
				Int32 index = FromStringDirect< Int32 >( indexString );
				
				if ( index <= (Int32)m_biasConsole.Size() )
				{
					return GetTypeObject< Uint32 >()->ReadValue( ctx, &m_biasConsole[index - 1], restOfThePath, outValue );
				}
			}
			return ISerializable::ReadCustomEditableProperty( ctx, propertyName, restOfThePath, outValue );
		}

		virtual Bool WriteCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, const red::SharedPtr<rtti::ValueHolder>& newValue ) override
		{
			String propNameString( propertyName.AsChar() );

			if ( propNameString.BeginsWith( "PCBias" ) )
			{
				String indexString = propNameString.StringAfter( "PCBias" );
				Int32 index = FromStringDirect< Int32 >( indexString );

				if ( index <= (Int32)m_biasPC.Size() )
				{
					return GetTypeObject< Uint32 >()->WriteValue( ctx, &m_biasPC[index - 1], restOfThePath, *newValue.Get() );
				}
			}

			if ( propNameString.BeginsWith( "ConsoleBias" ) )
			{
				String indexString = propNameString.StringAfter( "ConsoleBias" );
				Int32 index = FromStringDirect< Int32 >( indexString );

				if ( index <= (Int32)m_biasConsole.Size() )
				{
					return GetTypeObject< Uint32 >()->WriteValue( ctx, &m_biasConsole[index - 1], restOfThePath, *newValue.Get() );
				}
			}
			return ISerializable::WriteCustomEditableProperty( ctx, propertyName, restOfThePath, newValue );
		}

		void ApplyChanges( const THandle< Multilayer_Mask >& mask, const MultilayerMaskCustomDescriptor* descriptor )
		{
			for ( Uint32 i = 1; i < mask->GetLayerCount(); ++i )
			{
				if ( descriptor->IsSwitchChecked( RED_NAME( String::Printf( "PCBias%d", i ) ) ) )
				{
					mask->SetLayerBias( i, ECookingPlatform::PLATFORM_PC, m_biasPC[i-1] );
				}

				if ( descriptor->IsSwitchChecked( RED_NAME( String::Printf( "ConsoleBias%d", i ) ) ) )
				{
					mask->SetLayerBias( i, ECookingPlatform::PLATFORM_PS4, m_biasConsole[i-1] );
				}
			}
		}

	private:
		Uint32 m_maxBias;
		red::DynArray< Uint32 > m_biasPC;
		red::DynArray< Uint32 > m_biasConsole;
	};

	MultilayerMaskCustomDescriptor::MultilayerMaskCustomDescriptor()
		: m_explicitBiases( CreateHandle< ExplicitBiases >() )
		, m_aggregatedOperations( CreateHandle< AggregatedOperations >() )
	{
	}

	void MultilayerMaskCustomDescriptor::Initialize( const red::DynArray< THandle< CResource > >& resources )
	{
		IResourceCustomPropertiesDescriptor::Initialize( resources );

		m_explicitBiases->Initialize( resources );
	}

	void MultilayerMaskCustomDescriptor::ApplyChanges( const job::RunContext& runContext ) const
	{
		job::Builder builder( runContext );
		builder.DispatchParallelForJob( "BatchTool/ApplyChanges/Custom/MultilayerMask", [this] ()
		{
			return m_resources.Size();
		},
			[this] ( Uint32 index, const job::RunContext& )
		{
			const THandle<CResource>& resource = m_resources[index];
			if ( THandle< Multilayer_Mask > mask = Cast< Multilayer_Mask >( resource ) )
			{
				m_explicitBiases->ApplyChanges( mask, this );
				m_aggregatedOperations->ApplyChanges( mask, this );

				red::DynArray<THandle<CSourceTexture>> sourceDatas{ red::PoolBackend() };
				mask->GetSourceData( sourceDatas );

				if ( sourceDatas.Empty() )
				{
					return;
				}

				red::DynArray<img::ImagePtr> sourceImages( sourceDatas.Size(), red::PoolBackend() );
				red::DynArray<MultilayerMaskBuilder::LayerSource> layerSources( sourceDatas.Size(), red::PoolBackend() );

				for ( Uint32 i = 0; i < sourceDatas.Size(); i++ )
				{
					auto& source = sourceDatas[i];
					sourceImages[i] = source->GetImage();
					layerSources[i].imageData = (Uint8*)sourceImages[i]->GetPixelBuffer();
					layerSources[i].format = GpuWrapApi::TEXFMT_R8_Unorm;
					layerSources[i].width = source->GetWidth();
					layerSources[i].height = source->GetHeight();
					layerSources[i].rowStride = source->GetWidth() * Texture::GetPixelSizeInBytes( source->GetFormat() );

					// i+1 -- sourceImages is built up so that sourceImages[0] is actually layer 1 (how Builder
					// wants it). But mask gives bias for the actual layer index we give. So just need to account
					// for that.
					layerSources[i].bias = mask->GetLayerBias( i + 1, ECookingPlatform::PLATFORM_PC );
					layerSources[i].biasConsole = mask->GetLayerBias( i + 1, ECookingPlatform::PLATFORM_PS4 );
				}

				MultilayerMaskBuilder builder;
				builder.SetLayerSources( layerSources, false );
				builder.Generate( mask, false );
			}
		} );
	}

	Bool MultilayerMaskCustomDescriptor::SupportsResource( const rtti::ClassType* resourceClass ) const
	{
		return resourceClass->IsA< Multilayer_Mask >();
	}

	const String& MultilayerMaskCustomDescriptor::GetTitle() const
	{
		static const String s_title( "Layers setup" );
		return s_title;
	}

	void MultilayerMaskCustomDescriptor::GetCustomEditableProperties( rtti::EditableProperties& outRootProperties ) const
	{
		m_aggregatedOperations->GetCustomEditableProperties( outRootProperties );
		m_explicitBiases->GetCustomEditableProperties( outRootProperties );
	}

	Bool MultilayerMaskCustomDescriptor::ReadCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, red::SharedPtr<rtti::ValueHolder>& outValue ) const
	{
		if ( m_aggregatedOperations->ReadCustomEditableProperty( ctx, propertyName, restOfThePath, outValue ) )
		{
			return true;
		}

		if ( m_explicitBiases->ReadCustomEditableProperty( ctx, propertyName, restOfThePath, outValue ) )
		{
			return true;
		}
		return IResourceCustomPropertiesDescriptor::ReadCustomEditableProperty( ctx, propertyName, restOfThePath, outValue );
	}

	Bool MultilayerMaskCustomDescriptor::WriteCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, const red::SharedPtr<rtti::ValueHolder>& newValue )
	{
		if ( m_aggregatedOperations->WriteCustomEditableProperty( ctx, propertyName, restOfThePath, newValue ) )
		{
			return true;
		}

		if ( m_explicitBiases->WriteCustomEditableProperty( ctx, propertyName, restOfThePath, newValue ) )
		{
			return true;
		}
		return IResourceCustomPropertiesDescriptor::WriteCustomEditableProperty( ctx, propertyName, restOfThePath, newValue );
	}
}

RTTI_BEGIN_TYPE_IN_NAMESPACE( MultilayerMaskCustomDescriptor, tools );
	RTTI_PARENT_TYPE( tools::IResourceCustomPropertiesDescriptor );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( BiasCopyProperty, tools );
	RTTI_PARENT_TYPE( tools::IEditableCustomProperty );
RTTI_END_TYPE();