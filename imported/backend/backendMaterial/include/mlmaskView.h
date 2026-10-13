
/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../common/interop/include/interopUtilsManual.h"
#include "../../../common/resourceMaterial/include/multilayerMask.h"

namespace tools
{
	class ObjectCollection;
	class MLMaskViewerResourceEditor;

	enum class MLMaskVisualizationMode : Uint32
	{
		Default,
		ContributingLayers,
		StoredLayers
	};

	class MLMaskProperties : public ISerializable
	{
		RTTI_DECLARE_TYPE( MLMaskProperties )

	public:
		MLMaskProperties();
		MLMaskProperties( const THandle< Multilayer_Mask >& mask );

		Uint32 GetLayerCount() { return m_layerCount; }
		void SetLayerCount( const Uint32 layerCount );

		Uint32 GetLayer() { return m_layer; }
		MLMaskVisualizationMode GetVisualizationMode() { return m_visualizationMode; }
		Uint32 GetBiasPC() { return m_biasPC; }
		Uint32 GetBiasConsole() { return m_biasConsole; }

		void ChangeLayerBiasOnAllLayers(Int32 amount);

		Event<> PreviewParamsChanged;

	protected:
		virtual void GetCustomEditableProperties( rtti::EditableProperties& outRootProperties ) const override;
		virtual Bool ReadCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, red::SharedPtr<rtti::ValueHolder>& outValue ) const override;
		void RecreateMLMask(THandle<Multilayer_Mask> &mask);
		virtual Bool WriteCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, const red::SharedPtr<rtti::ValueHolder>& newValue ) override;
		virtual void OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& newValue ) override;

	private:
		// resource parameters
		Uint32 m_layerCount;

		// user parameters
		MLMaskVisualizationMode m_visualizationMode;
		Uint32 m_layer;
		Uint32 m_biasPC;
		Uint32 m_biasConsole;

		THandle< Multilayer_Mask > m_mask;
	};

	class MLMaskController
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		MLMaskController(MLMaskViewerResourceEditor* editor );
		virtual ~MLMaskController();

		void SetMLMask( THandle< Multilayer_Mask > mask );
		THandle< Multilayer_Mask >& GetMLMask() { return m_mask; }

		MLMaskVisualizationMode GetVisualizationMode() { return m_properties->GetVisualizationMode(); }
		Uint32 GetLayer() { return m_properties->GetLayer(); }

		interop::ObjectPtr CreateInterop() const;

		virtual void ChangeLayerBiasOnAllLayers(Int32 amount);

	private:
		THandle< Multilayer_Mask > m_mask;

		THandle< MLMaskProperties > m_properties;
		red::UniquePtr< ObjectCollection > m_propertiesWrapper;
	};

} // namespace tools

RTTI_DECLARE_ENUM_IN_NAMESPACE(MLMaskVisualizationMode, tools);