/**
* Copyright (c) 2019 CDProjekt Red, Inc. All Rights Reserved.
*/

#pragma once

namespace red
{
	namespace err
	{
		struct ApplicationDataDumpContext
		{
			Bool isAssertHandler = false;
		};

		using ApplicationDumpDataOnErrorFunc = void( const ApplicationDataDumpContext& context );

		class REDSYSTEM_API ApplicationErrorDumper
		{
		public:

			static void RegisterDumperOnce( ApplicationDumpDataOnErrorFunc* func );
			static void RunDumpers( const red::err::ApplicationDataDumpContext& context );

		private:

			static constexpr Uint32 c_MaxDumpers = 100;
			static Uint32 m_dumpersCount;
			static ApplicationDumpDataOnErrorFunc* m_dumpers[ c_MaxDumpers ];
		};

	} // namespace err
} // namespace red