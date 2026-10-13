/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once
#include "../../../common/interop/include/interopUtilsManual.h"

namespace tools
{
	class MaterialController;

	class MaterialInterop
	{
		DECLARE_INTEROP( MaterialInterop );
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		MaterialInterop( MaterialController* materialController );
		virtual ~MaterialInterop();

		void SetGeometryTypes( const red::DynArray<String>& values );
		void SetCurrentType( const String& type );
		const String& GetCurrentType() const { return m_currentType; }
		const red::DynArray<String>& GetSupportedGeometryTypes() const;

	private:
		MaterialController* m_materialController;
		red::DynArray< String > m_geometryTypes;
		String m_currentType;
		String m_fallbackType;
		interop::InteropNotifier m_notifier;
	};

	typedef red::SharedPtr< MaterialInterop > MaterialInteropPtr;
}