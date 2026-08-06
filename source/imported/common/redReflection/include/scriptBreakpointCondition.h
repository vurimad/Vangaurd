/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "scriptExpressionParserPath.h"

namespace script
{
	namespace breakpoint
	{
		enum class PassStyle
		{
			None,
			Equal,
			EqualOrGreater,
			Mod,
		};

		// Matches enum_BP_COND_STYLE
		enum class ConditionStyle
		{
			None,
			IsTrue,
			WhenChanged,
		};

		class PassCountImpl
		{
			RED_USE_MEMORY_POOL( red::PoolScriptDebugger );

		public:
			PassCountImpl();
			~PassCountImpl();

			void SetPassCount( Uint32 count, PassStyle style );
			void SetHitCount( Uint32 hitcount );

			Bool Evaluate();

		private:
			Uint32 m_passcount;
			Uint32 m_hitcount;
			PassStyle m_passStyle;
		};

		class IsTrueImpl
		{
			RED_USE_MEMORY_POOL( red::PoolScriptDebugger );

		public:
			IsTrueImpl();
			~IsTrueImpl();

			void SetExpression( const String& expression );

			Bool Evaluate( const class CScriptStackFrame& callstack );

		private:
			String m_expression;
			expression::Path m_path;
		};

		class WhenChangedImpl
		{
			RED_USE_MEMORY_POOL( red::PoolScriptDebugger );
		};

		class RED_REFLECTION_API Condition
		{
			RED_USE_MEMORY_POOL( red::PoolScriptDebugger );

		public:
			Condition();
			~Condition();

			void SetPassCount( Uint32 count, PassStyle style );
			void SetHitCount( Uint32 hitcount );

			void SetExpression( const String& condition, ConditionStyle style );

			Bool Evaluate( const class CScriptStackFrame& callstack );

		private:
			red::UniquePtr< PassCountImpl > m_passcount;
			red::UniquePtr< IsTrueImpl > m_isTrue;
		};
	}
}
