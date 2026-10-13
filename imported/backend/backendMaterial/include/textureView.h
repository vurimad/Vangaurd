
/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "textureInterface.h"
#include "../../../common/interop/include/interopUtilsManual.h"
#include "../../../common/resourceMaterial/include/texture.h"

namespace tools
{
	class TextureViewerResourceEditor;
	class TextureController;
	class ObjectCollection;

	class TextureProperties : public ISerializable
	{
		RTTI_DECLARE_TYPE( TextureProperties )

	public:
		TextureProperties();
		TextureProperties( TextureController* controller );

	protected:
		virtual Bool OnPropertyPreChange( const rtti::AccessPath& path, red::SharedPtr<const rtti::ValueHolder>& inOutValue ) override;
		virtual void GetCustomEditableProperties( rtti::EditableProperties& outRootProperties ) const override;
		virtual Bool ReadCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, red::SharedPtr<rtti::ValueHolder>& outValue ) const override;
		virtual Bool WriteCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, const red::SharedPtr<rtti::ValueHolder>& newValue ) override;

	private:
		TextureController* m_controller;
	};

	class TextureController
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		DECLARE_INTEROP( TextureController );

	public:
		TextureController( TextureViewerResourceEditor* editor );
		virtual ~TextureController();

		void SetTexture( THandle< ITexture > texture );
		THandle< ITexture >& GetTexture() { return m_texture; }

		Int32 GetMipmapLevel() { return m_mipmapLevel; }
		void SetMipmapLevel( Int32 mipmapLevel ) { m_mipmapLevel = mipmapLevel; }

		Uint32 GetMipmapCount() { return m_texture->GetSizeDesc().numMips; }
		Uint32 GetSlicesCount() { return m_texture->GetSizeDesc().depth; }
		//void SetMipmapCount( Uint32 mipmapCount ) { m_mipmapCount = mipmapCount; }

		Uint32 GetSlice() { return m_slice; }
		void SetSlice(Uint32 slice) { m_slice = slice; }

		Uint32 GetFaceIndex() { return m_faceIndex; }

		red::DynArray<Bool>* GetChannelMaskArray() { return m_channelMask.GetMaskArray(); }
		Uint8 GetChannelMask() { return m_channelMask.GetMask(); }
		Uint32 GetChannelNumber() { return m_channelMask.GetSize(); }
		void SetChannelNumber( Uint32 channelNumber ) { m_channelMask.Resize( channelNumber ); };

		Bool GetShowRaw() const { return m_showRaw; }
		void SetShowRaw( Bool showRaw ) { m_showRaw = showRaw; }

		Bool GetPointSample() const { return m_pointSample; }
		void SetPointSample( Bool pointSample ) { m_pointSample = pointSample; }

		Float GetMinValue() const { return m_min; }
		void SetMinValue( Float min ) { m_min = min; }

		Float GetMaxValue() const { return m_max; }
		void SetMaxValue( Float max ) { m_max = max; }

	private:
		void PremultiplyAlpha();
		void OnSourceExport( const red::AbsolutePath& absPath );

		THandle< ITexture > m_texture;

		Int32 m_mipmapLevel;
		Uint32 m_mipmapCount;
		Uint32 m_faceIndex;
		Uint32 m_slice;
		ChannelMask m_channelMask;
		GroupHelper m_groupHelper;
		Bool m_showRaw;
		Bool m_pointSample;
		Float m_min;
		Float m_max;

		THandle< TextureProperties > m_properties;
		red::UniquePtr< ObjectCollection > m_propertiesWrapper;

		interop::InteropNotifier m_resolutionPCNotifier;
		TextureViewerResourceEditor* m_editorFallbackPtr;
	};

} // namespace tools