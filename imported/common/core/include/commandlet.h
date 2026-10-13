/**
* Copyright (c) 2010-16 CD Projekt Red. All Rights Reserved.
*/
#pragma once


namespace red
{
	class CommandLine;

	/// Base commandlet interface
	class RED_CORE_API ICommandlet
	{
		RTTI_DECLARE_POLYMORPHIC_TYPE( ICommandlet );
		RED_USE_POLYMORPHIC_MEMORY_POOL( red::PoolBackend );

	public:
		virtual ~ICommandlet();

		RED_INLINE const CName GetName() const { return m_commandletName; }

		// Executes commandlet command
		virtual Bool Execute( const CommandLine& options ) = 0;

		// Returns commandlet one-liner
		virtual const char* GetOneLiner() const = 0;
	
		// Prints commandlet help
		virtual void PrintHelp() const = 0;

		// Initialize commandlet environment
		virtual Bool InitializeEnvironment();

		// Tear down commandlet environment
		virtual void TeardownEnvironment();

	protected:
		ICommandlet() = default;
		ICommandlet( const CName name );

	private:
		CName		m_commandletName;
	};

} // red

