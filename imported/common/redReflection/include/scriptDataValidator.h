/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

/// Validate script environment vs engine runtime RTTI
/// Report any conflicts
class RED_REFLECTION_API CScriptDataValidator
{
public:
	CScriptDataValidator();

	// validate environment
	Bool Validate( const class CScriptedDataEnvironment& env, class IScriptDataErrorReporter& err ) const;

private:
	Bool ValidateTypeRef( const class CScriptedDataTypeRef& obj, class IScriptDataErrorReporter& err ) const;
	Bool ValidateEnum( const class CScriptedDataEnum& obj, class IScriptDataErrorReporter& err ) const;
	Bool ValidateBitfield( const class CScriptedDataBitfield& obj, class IScriptDataErrorReporter& err ) const;
	Bool ValidateClass( const class CScriptedDataClass& obj, class IScriptDataErrorReporter& err ) const;
	Bool ValidateFunction( const class CScriptedDataFunction& obj, class IScriptDataErrorReporter& err ) const;
	Bool ValidatePropertyType( const CScriptedDataProperty* prop, const rtti::Property* baseProp ) const;
};