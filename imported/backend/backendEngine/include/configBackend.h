/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace interop
{
	class IBaseObject;
	typedef red::SharedPtr<IBaseObject> ObjectPtr;
}

namespace Config
{

class ConfigVarGlobalNotifierProxy;

class ConfigBackend
{
	RED_USE_MEMORY_POOL( red::PoolBackend );

public:
			 ConfigBackend();
	virtual ~ConfigBackend();

	::interop::ObjectPtr CreateInterop();

private:
	friend class CBackendEngine;
	::interop::ObjectPtr	  m_root;
	ConfigVarGlobalNotifierProxy* m_notifier;
	red::SharedPtr< class SharedConfigData > m_sharedData;
};

}