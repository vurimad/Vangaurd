/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace red
{ 

struct ErrorMessage;

namespace err
{


struct ErrorHandlerHooks
{
	using AssertHandlerFunc = void( const ErrorMessage& pCustomErrorMsg );
	using RegisterAttachmentFunc = void( const char* pathToRegister );
	using FailFastAbortProcess = void();
	using ScriptCallstackVisitorFunc = void( red::FixedSizeFunction< Bool( Uint32, const red::AnsiChar*, Uint32) > );

	AssertHandlerFunc* fnAssertHandler = nullptr;
	RegisterAttachmentFunc* fnRegisterAttachment = nullptr;
	FailFastAbortProcess* fnFailFastAbortProcess = nullptr;
	ScriptCallstackVisitorFunc* fnScriptCallstackVisitor = nullptr;
};

} } // red/err