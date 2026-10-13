/*
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#pragma once
#include "../../../common/redContainers/include/redContainersPublic.h"

#ifndef NO_DATA_VALIDATION

struct SDataError;

class IDataErrorListener
{
public:
	virtual void OnDataErrorReported( const SDataError& error ) = 0;
	virtual void StartProcessing() = 0;
	virtual void ProcessDataErrors( const red::DynArray< SDataError >& errors ) = 0;
	virtual void StoreNonProcessedErrors( const red::DynArray< SDataError >& errors ) = 0;
	virtual void StopProcessing() = 0;
};

// Data error priorities
enum EDataErrorSeverity
{
	DES_Uber,		//!< Uber error - game will probably not work
	DES_Major,		//!< Major error ( P1 )
	DES_Minor,		//!< Minor
	DES_Tiny,		//!< Really cosmetic thing

	DES_Count,
};

// Interface for data error system
class RED_CORE_API IDataErrorSystem
{
public:
	struct SAssertInfo
	{
		const CResource*	m_assertParent;

		EDataErrorSeverity	m_severity;
		const char*			m_expression;
		const char*			m_category;
		const char*			m_file;
		Uint32				m_line;
	};

public:
	virtual ~IDataErrorSystem() = 0;

	virtual void Flush() = 0;
	virtual void GetCurrentCatchedFilteredForResource( red::SortedArray< SDataError >& arrayForErrors, const red::DynArray< red::String >& dependenciesPaths ) = 0;
	virtual void ClearCatchedForResource( const red::DynArray< red::String >& dependenciesPaths ) = 0;
	virtual void ReportError( SAssertInfo& info, STATIC_CHECK_PRINTF_MSC const char* message, ... ) = 0;

	virtual void FillErrorInfoFromPerforce( SDataError& error ) = 0;

	virtual void RegisterListener( IDataErrorListener* listener ) = 0;
	virtual void UnregisterListener( IDataErrorListener* listener ) = 0;

	void ReportXMLError( const char* filepath, STATIC_CHECK_PRINTF_MSC const char* error, ... );	
	void ClearXMLErrorsForFile( const char* filepath )						{ m_xmlErrors.Remove( String ( filepath ) );		}
	const red::HashMap< red::String, red::DynArray< red::String > >& GetXMLErrors()			{ return m_xmlErrors;							}

private:
	red::HashMap< red::String, red::DynArray< red::String > >	m_xmlErrors;
	
};

// global variable of data error system
RED_CORE_API extern IDataErrorSystem* GDataError;

#else

#define NO_DATA_ASSERTS

#endif // NO_DATA_VALIDATION



//////////////////////////////////////////////////////////////////////////
// Predefine macros
//////////////////////////////////////////////////////////////////////////
#ifndef NO_DATA_ASSERTS

#define DATA_HALT( Severity, CResource, Category, Message, ... )				\
{																				\
	if( GDataError != nullptr )													\
	{																			\
		IDataErrorSystem::SAssertInfo info;										\
		info.m_assertParent = CResource;										\
		info.m_severity = Severity;												\
		info.m_file = __FILE__;									\
		info.m_line = __LINE__;													\
		info.m_expression = "Halt";										\
		info.m_category = Category;												\
																				\
		GDataError->ReportError( info, Message, ##__VA_ARGS__ );				\
	}																			\
}

#define XML_ERROR( File, Message, ... )											\
{																				\
	if( GDataError != nullptr )													\
	{																			\
		GDataError->ReportXMLError( File, Message, ##__VA_ARGS__ );				\
	}																			\
}																				\

#define CLEAR_XML_ERRORS( File )												\
{																				\
	if ( GDataError != nullptr )												\
	{																			\
		GDataError->ClearXMLErrorsForFile( File );								\
	}																			\
}																				\

#else

#define DATA_HALT( Severity, CResource, Category, Message, ... )

#define XML_ERROR( File, Message, ... )

#define CLEAR_XML_ERRORS( File )

#endif	// NO_DATA_ASSERTS
