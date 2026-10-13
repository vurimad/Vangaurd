/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

namespace rtti
{

	// scripted class
	class ScriptedClassType : public ClassType
	{
	public:
		ScriptedClassType( const CName name, Uint32 flags );

	private:
		virtual void OnConstruct( void * buffer ) const override final;
		virtual void OnDestruct( void * buffer ) const override final;

		virtual Bool Compare( const void* data1, const void* data2, Uint32 ) const override final;
		virtual void Copy( void* dest, const void* src ) const override final;
	
		virtual void RecalculateClassDataSize() override final;
		virtual const red::memory::Pool & GetInnerTypeMemoryPool() const override final;
	
		virtual void * AllocateClassBuffer() const override final;
	};

} // rtti