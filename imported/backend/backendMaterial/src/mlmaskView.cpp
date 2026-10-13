/**
* Copyright (c) 2016-2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "mlmaskView.h"

#include "mlmaskViewer.h"
#include "multilayerMaskBuilder.h"

#include "../../backendEngine/include/objectCollection.h"

#include "../../../common/resourceMaterial/include/sourceTextureData.h"

#include "../../../common/redReflection/include/rttiPathParser.h"

RTTI_BEGIN_ENUM_IN_NAMESPACE(MLMaskVisualizationMode, tools)
	RTTI_ENUM_OPTION(Default);
	RTTI_ENUM_OPTION(ContributingLayers);
	RTTI_ENUM_OPTION(StoredLayers);
RTTI_END_ENUM();

RTTI_BEGIN_TYPE_IN_NAMESPACE( MLMaskProperties, tools )
	RTTI_PROPERTY( m_visualizationMode ).editable();
RTTI_END_TYPE();

namespace tools
{
	MLMaskProperties::MLMaskProperties()
		: m_layerCount(2)
		, m_visualizationMode(MLMaskVisualizationMode::Default)
		, m_layer(1) // layer 0 is not interesting because it's always considered fully opaque
		, m_biasPC(0)
		, m_biasConsole(0)
	{
	}

	MLMaskProperties::MLMaskProperties( const THandle< Multilayer_Mask >& mask )
		: m_layerCount(2)
		, m_visualizationMode(MLMaskVisualizationMode::Default)
		, m_layer(1) // layer 0 is not interesting because it's always considered fully opaque
		, m_biasPC(0)
		,m_biasConsole(0)
		, m_mask( mask )
	{
	}

	void MLMaskProperties::SetLayerCount( const Uint32 layerCount )
	{
		m_layerCount = layerCount;
		m_layer = math::Min( m_layer, layerCount-1 );
	}

	void MLMaskProperties::ChangeLayerBiasOnAllLayers(Int32 amount)
	{
		for (Uint32 layer = 1; layer < m_layerCount; layer++)
		{
			Uint32 maxBias = m_mask->GetMaxLayerBias(layer);
			Uint32 currentBiasPC = m_mask->GetLayerBias(layer, ECookingPlatform::PLATFORM_PC);
			Uint32 targetBiasPC = Uint32(Clamp(Int32(currentBiasPC)+amount, 0, Int32(maxBias)));
			m_mask->SetLayerBias(layer, ECookingPlatform::PLATFORM_PC, targetBiasPC);

			Uint32 currentBiasConsole = m_mask->GetLayerBias( layer, ECookingPlatform::PLATFORM_PS4 );
			Uint32 targetBiasConsole = Uint32( Clamp( Int32( currentBiasConsole ) + amount, 0, Int32( maxBias ) ) );
			m_mask->SetLayerBias( layer, ECookingPlatform::PLATFORM_PS4, targetBiasConsole );

			if (layer == m_layer)
			{
				m_biasPC = targetBiasPC;
				m_biasConsole = targetBiasConsole;
			}
		}

		RecreateMLMask(m_mask);

		PreviewParamsChanged.Invoke();
	}

	void MLMaskProperties::GetCustomEditableProperties( rtti::EditableProperties& outRootProperties ) const
	{
		// If the mask only has one layer, then it's layer 0, which is implicit, and can't really be edited.
		if ( m_layerCount > 1 )
		{
			rtti::ClassEditablePropertyInfo info;

			info.m_name = RED_NAME_CONSTEXPR( "layer" );
			info.m_type = GetTypeObject<Uint32>();
			info.m_minValue = 1;
			info.m_maxValue = (Float)( m_layerCount-1 );
			info.m_hideSlider = false;

			outRootProperties.Add( info );

			info.m_name = RED_NAME_CONSTEXPR("PCBiasOfCurrentLayer");
			info.m_type = GetTypeObject<Uint32>();
			info.m_minValue = 0;
			info.m_maxValue = (Float)m_mask->GetMaxBias();
			info.m_hideSlider = false;

			outRootProperties.Add(info);

			info.m_name = RED_NAME_CONSTEXPR( "consoleBiasOfCurrentLayer" );

			outRootProperties.Add( info );

			info = {};
			info.m_name = RED_NAME_CONSTEXPR("maxBiasForCurrentLayer");
			info.m_type = GetTypeObject<Uint32>();
			info.m_isReadOnly = true;

			outRootProperties.Add(info);
		}
	}

	Bool MLMaskProperties::ReadCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, red::SharedPtr<rtti::ValueHolder>& outValue ) const
	{
		if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "layer" ) )
		{
			outValue = rtti::ValueHolder::CreateSingle( ToStringDirect<Uint32>( m_layer ).AsChar() );
			return true;
		}

		if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "PCBiasOfCurrentLayer" ) )
		{
			outValue = rtti::ValueHolder::CreateSingle( ToStringDirect<Uint32>( m_mask->GetLayerBias( m_layer, ECookingPlatform::PLATFORM_PC ) ).AsChar() );
			return true;
		}

		if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "consoleBiasOfCurrentLayer" ) )
		{
			outValue = rtti::ValueHolder::CreateSingle( ToStringDirect<Uint32>( m_mask->GetLayerBias( m_layer, ECookingPlatform::PLATFORM_PS4 ) ).AsChar() );
			return true;
		}

		if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "maxBiasForCurrentLayer" ) )
		{
			outValue = rtti::ValueHolder::CreateSingle( ToStringDirect<Uint32>( m_mask->GetMaxLayerBias( m_layer ) ).AsChar() );
			return true;
		}

		return false;
	}

	void MLMaskProperties::RecreateMLMask(THandle<Multilayer_Mask> &mask)
	{
		red::DynArray<THandle<CSourceTexture>> sourceDatas{ red::PoolBackend() };
		mask->GetSourceData(sourceDatas);

		if (!sourceDatas.Empty())
		{
			// need to keep sourceimages around because it's a shared pointer that needs to exist until the generation is done
			red::DynArray<img::ImagePtr> sourceImages(sourceDatas.Size(), red::PoolBackend());
			red::DynArray<MultilayerMaskBuilder::LayerSource> layerSources(sourceDatas.Size(), red::PoolBackend());

			for (Uint32 i = 0; i < sourceDatas.Size(); i++)
			{
				auto& source = sourceDatas[i];
				sourceImages[i] = source->GetImage();
				layerSources[i].imageData = (Uint8*)sourceImages[i]->GetPixelBuffer();
				layerSources[i].format = GpuWrapApi::TEXFMT_R8_Unorm;
				layerSources[i].width = source->GetWidth();
				layerSources[i].height = source->GetHeight();

				//REVIEW pitch is not exposed from CSourceTexture, should it be?
				layerSources[i].rowStride = source->GetWidth() * Texture::GetPixelSizeInBytes(source->GetFormat());

				// i+1 -- sourceImages is built up so that sourceImages[0] is actually layer 1 (how Builder
				// wants it). But mask gives bias for the actual layer index we give. So just need to account
				// for that.
				layerSources[i].bias = mask->GetLayerBias( i + 1, ECookingPlatform::PLATFORM_PC );
				layerSources[i].biasConsole = mask->GetLayerBias( i + 1, ECookingPlatform::PLATFORM_PS4 );
			}

			MultilayerMaskBuilder builder;
			builder.SetLayerSources(layerSources, false);
			builder.Generate(mask, false);
		}
	}

	Bool MLMaskProperties::WriteCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, const red::SharedPtr<rtti::ValueHolder>& newValue )
	{
		if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "layer" ) )
		{
			if ( newValue->IsSingle() && FromString( newValue->GetSingleValue(), m_layer ) )
			{
				// when we change the layer we should also change the bias to reflect the currently selected layer
				{
					m_biasPC = m_mask->GetLayerBias( m_layer, ECookingPlatform::PLATFORM_PC );
					m_biasConsole = m_mask->GetLayerBias( m_layer, ECookingPlatform::PLATFORM_PS4 );

					NotifyPropertyChanged( rtti::AccessPath( "PCBiasOfCurrentLayer" ) );
					NotifyPropertyChanged( rtti::AccessPath( "consoleBiasOfCurrentLayer" ) );
					NotifyPropertyChanged( rtti::AccessPath( "maxBiasForCurrentLayer" ) );
				}

				PreviewParamsChanged.Invoke();
				return true;
			}
		}

		if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "PCBiasOfCurrentLayer" ) || propertyName == RED_NAME_CONSTEXPR_NOREG( "consoleBiasOfCurrentLayer" ) )
		{
			Uint32 newBias = 0;
			if (newValue->IsSingle() && FromString(newValue->GetSingleValue(), newBias))
			{
				newBias = Min(newBias, m_mask->GetMaxLayerBias(m_layer));


				if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "PCBiasOfCurrentLayer" ) )
				{
					if ( newBias == m_biasPC )
					{
						return true;
					}

					m_biasPC = newBias;
					m_mask->SetLayerBias( m_layer, ECookingPlatform::PLATFORM_PC, m_biasPC );
				}
				else
				{
					if ( newBias == m_biasConsole )
					{
						return true;
					}

					m_biasConsole = newBias;
					m_mask->SetLayerBias( m_layer, ECookingPlatform::PLATFORM_PS4, m_biasConsole );
				}

				RecreateMLMask(m_mask);

				PreviewParamsChanged.Invoke();
				return true;
			}
		}
		return false;
	}

	void MLMaskProperties::OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& newValue )
	{
		ISerializable::OnPropertyPostChange( path, oldValue, newValue );

		rtti::PathParser parser(path);
		CName propertyName;
		parser.EatName(propertyName);

		if (propertyName == RED_NAME_CONSTEXPR_NOREG( "visualizationMode" ))
		{
			PreviewParamsChanged.Invoke();
		}
	}

	//////////////////////////////////////////////////////////////////////////

	MLMaskController::MLMaskController( MLMaskViewerResourceEditor* editor )
		: m_properties( CreateHandle< MLMaskProperties >( editor->GetMLMask() ) )
		, m_propertiesWrapper( red::CreateUniquePtr< ObjectCollection >( editor->GetAccessHistory() ) )
	{
		m_properties->PreviewParamsChanged += { [editor]()
		{
			editor->OnPreviewParameterChanged();
		} };
		m_propertiesWrapper->SetObject( m_properties.Get() );
	}

	MLMaskController::~MLMaskController()
	{}

	void MLMaskController::SetMLMask( THandle< Multilayer_Mask > mask )
	{
		m_mask = mask;

		// extract some parameters from the MLMask data to be able to implement the UI more efficiently
		const rend::RenderMultilayerMaskResource multiLayerResource = m_mask->GetRenderMultilayerMaskResource();
		THandle< rend::RenderMultilayerMaskBlob > multilayerResourceBlob = Cast< rend::RenderMultilayerMaskBlob >( multiLayerResource.GetRenderResourceBlob() );

		m_properties->SetLayerCount( multilayerResourceBlob->m_header.m_numLayers );

		m_propertiesWrapper->Refresh();
	}

	interop::ObjectPtr MLMaskController::CreateInterop() const
	{
		return m_propertiesWrapper->GetRootInterop();
	}

	inline void MLMaskController::ChangeLayerBiasOnAllLayers(Int32 amount)
	{
		m_properties->ChangeLayerBiasOnAllLayers(amount);
		m_propertiesWrapper->Refresh();
	}
} // namespace tools