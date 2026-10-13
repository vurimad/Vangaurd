/**
* Copyright (c) 2013 CDProjekt Red, Inc. All Rights Reserved.
*/

#pragma once

#if defined(RED_DLL) || defined(RED_WITH_DLL)

#define RED_INITIALIZE_MODULE(x)				\
		__declspec(dllimport) void DllInitializeModule_##x();			\
		DllInitializeModule_##x();

#else

	#define RED_INITIALIZE_MODULE(x)				\
		extern void InitializeModule_##x();			\
		InitializeModule_##x();	

#endif

#if defined(RED_DLL)
																			
	#define RED_MODULE(x)																		\
		BOOL DllMain( void*, unsigned reason, void* plvReserved )								\
		{ 																						\
			/* fixme :( due to static order destruction hell we're going to skip it	for now */	\
			switch ( reason )																	\
			{																					\
				case DLL_PROCESS_DETACH:														\
					if ( plvReserved != nullptr )												\
					{																			\
						std::quick_exit( 0 );													\
					}																			\
					break;																		\
			}																					\
			return TRUE;																		\
		}																						\
		__declspec(dllexport) void DllInitializeModule_##x()
		
	#define RED_MODULE_DLL_EXPORT(x)														\
		__declspec(dllexport) void DllInitializeModule_##x()								\
		{																					\
			extern void InitializeModule_##x();												\
			InitializeModule_##x();															\
		}
#else

	#define RED_MODULE(x) void InitializeModule_##x()
	#define RED_MODULE_DLL_EXPORT(x)

#endif



