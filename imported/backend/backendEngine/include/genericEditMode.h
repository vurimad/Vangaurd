/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../backendEngine/include/previewEditMode.h"

namespace tools
{
	class GenericPreviewPanel;

	/// The generic edit mode coorperating with generic panel
	template< typename SharedDataClass >
	class GenericEditMode : public IPreviewEditMode
	{
	public:
		GenericEditMode( const String& name )
			: m_name( name )
		{}

		/// get shared data
		RED_FORCE_INLINE SharedDataClass* GetSharedData() const 
		{ 
			if( PreviewPanelSharedDataPtr sharedData = m_sharedData.Lock() )
				return static_cast< SharedDataClass* >(sharedData.Get() );
			return nullptr;
		}

		/// IPreviewEditMode
		virtual Bool OnInitialize(const PreviewPanelSharedDataPtr& sharedData) override
		{
			if ( !IPreviewEditMode::OnInitialize(sharedData) )
				return false;

			m_sharedData = sharedData;
			return true;
		}

		virtual const AnsiChar* GetDisplayName() const override final
		{
			return m_name.AsChar();
		}

	private:
		red::WeakPtr<PreviewPanelSharedData>	m_sharedData;
		String						m_name;
	};

} // tools

