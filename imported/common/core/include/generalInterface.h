/*
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#pragma once

///////////////////////////////////////////////////////////////////////////////
// Macros for IGeneralInterface registration
#define DECLARE_GENERAL_INTERFACE_FLAGS( interfaceId, containerClass, flags )												\
	public:																													\
		static const containerClass::InterfaceId INTERFACE_ID = containerClass::InterfaceId( interfaceId );					\
		static containerClass::InterfaceFlags GetInterfaceFlags() { return flags; }

#define DECLARE_GENERAL_INTERFACE( interfaceId, containerClass ) DECLARE_GENERAL_INTERFACE_FLAGS( interfaceId, containerClass, 0 )


