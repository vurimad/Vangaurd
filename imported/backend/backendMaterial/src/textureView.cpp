/**
* Copyright (c) 2016-2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "textureView.h"
#include "textureViewer.h"

#include "../../backendEngine/include/editAction.h"
#include "../../backendEngine/include/objectCollection.h"

#include "../../../common/resourceMaterial/include/sourceTextureData.h"
#include "../../backendEngine/include/editActionHistory.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/resourceMaterial/include/bitmapTexture.h"
#include "../../../common/redReflection/include/rttiPathParser.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( TextureProperties, tools )
RTTI_END_TYPE();

namespace tools
{
	class AlphaPremultiply_EditAction : public IEditAction
	{
	public:
		AlphaPremultiply_EditAction( const THandle< ITexture >& texture )
			: m_texture( texture )
		{}

		virtual String GetDescription() const override { return ""; }

		virtual Bool Do() override
		{
			auto func = []( img::ImageIterator& it, img::ImagePixel& pixel )
			{
				Float alpha = pixel[3];
				pixel.MulInplace( alpha );
				pixel[3] = alpha;
				return true;
			};

			return ProcessImage( func );
		}

		virtual Bool Undo() override
		{
			auto func = []( img::ImageIterator& it, img::ImagePixel& pixel )
			{
				Float alpha = pixel[3];
				pixel.MulInplace( 1.f / alpha );
				pixel[3] = alpha;
				return true;
			};

			return ProcessImage( func );
		}

		virtual void GetAffectedResources( red::Set< res::ResourcePath >& outResourcePaths ) const override
		{
			outResourcePaths.Insert( m_texture->GetPath() );
		}

	private:
		typedef std::function< Bool( img::ImageIterator& it, img::ImagePixel& pixel ) > TPixelModificationFunc;

		Bool ProcessImage( TPixelModificationFunc func ) const
		{
			if( auto bitmapTexture = ::Cast<CBitmapTexture>( m_texture ) )
			{
				THandle< CSourceTexture > sourceTexture = bitmapTexture->GetSourceData();
				img::ImagePtr sourceImage = sourceTexture->GetImage();
				img::ImageAccess src( *sourceImage );
				img::ImageIterator it = sourceImage->GetIterator();
				img::utils::ProcessImageInplace( src, it, func );

				sourceTexture->SetImage( sourceImage );
				if( bitmapTexture->InitFromSourceData( sourceTexture, m_texture->GetTextureSetup() ) )
				{
					m_texture->NotifyPropertyChanged( rtti::AccessPath( "sourceData" ) );
					return true;
				}
			}

			return false;
		}

		THandle< ITexture > m_texture;
	};


	TextureController::TextureController( TextureViewerResourceEditor* editor )
		: m_mipmapLevel( -1 )
		, m_mipmapCount( 1 )
		, m_faceIndex( 0 )
		, m_slice( 0 )
		, m_showRaw( false )
		, m_pointSample( false )
		, m_editorFallbackPtr( editor )
		, m_min( 0.0f )
		, m_max( 1.0f )
	{
		m_propertiesWrapper = red::CreateUniquePtr< ObjectCollection >( editor->GetAccessHistory() );
		m_properties = CreateHandle< TextureProperties >( this );

		m_propertiesWrapper->SetObject( m_properties.Get() );
	}

	TextureController::~TextureController()
	{
	}

	void TextureController::SetupInterop( TextureController* controller, ::interop::utils::Object& op )
	{
		op.RegisterObject( "properties", controller->m_propertiesWrapper->GetRootInterop() );

		op.RegisterFunctor<void>( "premultiplyAlpha",
			[controller]( interop::IContext& ctx ) 
		{ 
			controller->PremultiplyAlpha();
		} );

		op.RegisterFunctor<void, red::AbsolutePath>("onSourceExport", [controller](interop::IContext& ctx, const red::AbsolutePath& filePath )
		{ 
			controller->OnSourceExport( filePath );
		} );

// 		op.RegisterRestrictedValue<String>( "PCresolution", controller->m_platformHelper.GetPossibleValues(),
// 			[controller]( interop::IContext& ctx, String& outValue ) { outValue = controller->m_platformHelper.GetResolutionPC(); },
// 			[controller]( interop::IContext& ctx, const String& newValue ) { controller->m_platformHelper.SetResolutionPC( newValue ); },
// 			&controller->m_resolutionPCNotifier );
// 
// 		op.RegisterRestrictedValue<String>( "XBoneresolution", controller->m_platformHelper.GetPossibleValues(),
// 			[controller]( interop::IContext& ctx, String& outValue ) { outValue = controller->m_platformHelper.GetResolutionXBone(); },
// 			[controller]( interop::IContext& ctx, const String& newValue ) { controller->m_platformHelper.SetResolutionXBone( newValue ); } );
// 
// 		op.RegisterRestrictedValue<String>( "PS4resolution", controller->m_platformHelper.GetPossibleValues(),
// 			[controller]( interop::IContext& ctx, String& outValue ) { outValue = controller->m_platformHelper.GetResolutionPS4(); },
// 			[controller]( interop::IContext& ctx, const String& newValue ) { controller->m_platformHelper.SetResolutionPS4( newValue ); } );
	}

	void TextureController::SetTexture( THandle< ITexture > texture )
	{
		if( m_texture = texture )
		{
			// Fetch render resource so it'll initialize data
			m_mipmapCount	= texture->GetSizeDesc().numMips;
			if ( texture->GetTextureSetup().m_rawFormat > TRF_Invalid && texture->GetTextureSetup().m_rawFormat < TRF_Max )
			{
				m_channelMask.Resize( Texture::GetNumChannels( texture->GetTextureSetup().m_rawFormat ) );
			}
		}

		m_propertiesWrapper->Refresh();
	}

	void TextureController::PremultiplyAlpha()
	{
		m_editorFallbackPtr->GetAccessHistory().Execute( red::CreateUniquePtr< AlphaPremultiply_EditAction >( m_texture ) );
	}

	void TextureController::OnSourceExport( const red::AbsolutePath& absPath )
	{
		const THandle< CBitmapTexture > bitmapTexture = ::Cast<CBitmapTexture>(m_texture);
		if (bitmapTexture)
		{
			const THandle< CSourceTexture > &sourceData = bitmapTexture->GetSourceData();
			if (sourceData)
			{
				img::GenericWriter writer;
				writer.Reserve(sourceData->GetBufferSize());
				img::ImagePtr sourceImage = img::utils::Flip(sourceData->GetImage(), false, true);

				if (img::BaseImageSaver::Save(writer, sourceImage, "png"))
				{
					auto fileWriter = GFileManager->CreateFileWriter(absPath, FOF_AbsolutePath);
					if (fileWriter)
					{
						fileWriter->Serialize(writer.GetData(), writer.GetSize());

						RED_LOG( "File %hs saved sucessfully.", absPath.AsChar() );
						return;
					}
				}
				RED_LOG_ERROR( "File %hs was NOT saved!", absPath.AsChar() );
			}
			else
			{
				RED_LOG_ERROR( "File %hs doesn't contain source data!", absPath.AsChar() );
			}
		}
	}

	//////////////////////////////////////////////////////////////////////////

	TextureProperties::TextureProperties() = default;
	TextureProperties::TextureProperties( TextureController* controller )
		: m_controller( controller )
	{
	}

	Bool TextureProperties::OnPropertyPreChange( const rtti::AccessPath& path, red::SharedPtr<const rtti::ValueHolder>& inOutValue )
	{
		// don't call ISerializable's OnPropertyPreChange to prevent resource modification notification
		return true;
	}

	void TextureProperties::GetCustomEditableProperties( rtti::EditableProperties& outRootProperties ) const
	{
		if ( !m_controller->GetTexture() )
			return;

		{
			rtti::ClassEditablePropertyInfo propertyInfo;
			propertyInfo.m_name = RED_NAME_CONSTEXPR( "mipmapLevel" );
			propertyInfo.m_type = GetTypeObject<Int32>();
			propertyInfo.m_minValue = -1;
			propertyInfo.m_maxValue = Max<Float>( (Float)m_controller->GetMipmapCount() - 1, 0 );
			outRootProperties.Add( propertyInfo );
		}

		{
			rtti::ClassEditablePropertyInfo propertyInfo;
			propertyInfo.m_name = RED_NAME_CONSTEXPR( "slice" );
			propertyInfo.m_type = GetTypeObject<Uint32>();
			propertyInfo.m_minValue = 0;
			propertyInfo.m_maxValue = Max<Float>( (Float)m_controller->GetSlicesCount() - 1, 0 );
			outRootProperties.Add( propertyInfo );
		}

		{
			rtti::ClassEditablePropertyInfo propertyInfo;
			propertyInfo.m_name = RED_NAME_CONSTEXPR( "channels" );
			propertyInfo.m_type = GetTypeObject< red::DynArray< Bool > >();
			outRootProperties.Add( propertyInfo );
		}

		{
			rtti::ClassEditablePropertyInfo propertyInfo;
			propertyInfo.m_name = RED_NAME_CONSTEXPR( "showRaw" );
			propertyInfo.m_type = GetTypeObject<Bool>();
			outRootProperties.Add( propertyInfo );
		}

		{
			rtti::ClassEditablePropertyInfo propertyInfo;
			propertyInfo.m_name = RED_NAME_CONSTEXPR( "pointSample" );
			propertyInfo.m_type = GetTypeObject<Bool>();
			outRootProperties.Add( propertyInfo );
		}

		{
			rtti::ClassEditablePropertyInfo propertyInfo;
			propertyInfo.m_name = RED_NAME_CONSTEXPR( "min" );
			propertyInfo.m_type = GetTypeObject<Float>();
			outRootProperties.Add( propertyInfo );
		}

		{
			rtti::ClassEditablePropertyInfo propertyInfo;
			propertyInfo.m_name = RED_NAME_CONSTEXPR( "max" );
			propertyInfo.m_type = GetTypeObject<Float>();
			outRootProperties.Add( propertyInfo );
		}
	}

	Bool TextureProperties::ReadCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, red::SharedPtr<rtti::ValueHolder>& outValue ) const
	{
		if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "mipmapLevel" ) )
		{
			const Int32 mipmapLevel = m_controller->GetMipmapLevel();
			return GetTypeObject<Int32>()->ReadValue( ctx, &mipmapLevel, rtti::AccessPath(), outValue );
		}
		else if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "slice" ) )
		{
			const Uint32 slice = m_controller->GetSlice();
			return GetTypeObject<Uint32>()->ReadValue( ctx, &slice, rtti::AccessPath(), outValue );
		}
		else if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "channels" ) )
		{
			const red::DynArray< Bool >* channels = m_controller->GetChannelMaskArray();
			return GetTypeObject< red::DynArray< Bool > >()->ReadValue( ctx, channels, restOfThePath, outValue );
		}
		else if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "showRaw" ) )
		{
			const Bool showRaw = m_controller->GetShowRaw();
			return GetTypeObject<Bool>()->ReadValue( ctx, &showRaw, rtti::AccessPath(), outValue );
		}
		else if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "pointSample" ) )
		{
			const Bool pointSample = m_controller->GetPointSample();
			return GetTypeObject<Bool>()->ReadValue( ctx, &pointSample, rtti::AccessPath(), outValue );
		}
		else if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "min" ) )
		{
			const Float minValue = m_controller->GetMinValue();
			return GetTypeObject<Float>()->ReadValue( ctx, &minValue, rtti::AccessPath(), outValue );
		}
		else if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "max" ) )
		{
			const Float maxValue = m_controller->GetMaxValue();
			return GetTypeObject<Float>()->ReadValue( ctx, &maxValue, rtti::AccessPath(), outValue );
		}
		return false;
	}

	Bool TextureProperties::WriteCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, const red::SharedPtr<rtti::ValueHolder>& newValue )
	{
		if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "mipmapLevel" ) )
		{
			Int32 mipmapLevel;
			if ( GetTypeObject<Int32>()->WriteValue( ctx, &mipmapLevel, rtti::AccessPath(), *newValue ) )
			{
				m_controller->SetMipmapLevel( mipmapLevel );
				return true;
			}
		}
		else if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "slice" ) )
		{
			Uint32 slice;
			if ( GetTypeObject<Uint32>()->WriteValue( ctx, &slice, rtti::AccessPath(), *newValue ) )
			{
				m_controller->SetSlice( slice );
				return true;
			}
		}
		else if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "channels" ) )
		{
			rtti::PathParser parser( restOfThePath );
			Int32 index;
			if ( parser.EatIndex( index ) )
			{
				red::DynArray< Bool >* channels = m_controller->GetChannelMaskArray();
				Bool value;
				if ( GetTypeObject< Bool >()->WriteValue( ctx, &value, rtti::AccessPath(), *newValue ) )
				{
					( *channels )[ index ] = value;
					return true;
				}
			}
		}
		else if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "showRaw" ) )
		{
			Bool showRaw;
			if ( GetTypeObject<Bool>()->WriteValue( ctx, &showRaw, rtti::AccessPath(), *newValue ) )
			{
				m_controller->SetShowRaw( showRaw );
				return true;
			}
		}
		else if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "pointSample" ) )
		{
			Bool pointSample;
			if ( GetTypeObject<Bool>()->WriteValue( ctx, &pointSample, rtti::AccessPath(), *newValue ) )
			{
				m_controller->SetPointSample( pointSample );
				return true;
			}
		}
		else if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "min" ) )
		{
			Float minValue;
			if ( GetTypeObject<Float>()->WriteValue( ctx, &minValue, rtti::AccessPath(), *newValue ) )
			{
				if ( minValue < m_controller->GetMaxValue() )
				{
					m_controller->SetMinValue( minValue );
					return true;
				}

				return false;
			}
		}
		else if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "max" ) )
		{
			Float maxValue;
			if ( GetTypeObject<Float>()->WriteValue( ctx, &maxValue, rtti::AccessPath(), *newValue ) )
			{
				if ( maxValue > m_controller->GetMinValue() )
				{
					m_controller->SetMaxValue( maxValue );
					return true;
				}

				return false;
			}
		}
		return false;
	}

} // namespace tools