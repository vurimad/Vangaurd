/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once

namespace tools
{
	class ResourceBackend;

	class BACKEND_ENGINE_API BaseSceneRecorder : public interop::utils::Object
	{
		RTTI_DECLARE_POLYMORPHIC_TYPE( BaseSceneRecorder );

	public:
		BaseSceneRecorder();

		ResourceBackend* GetResourceBackend() const { return m_resourceBackend; }
		void SetResourceBackend( ResourceBackend* resourceBackend ) { m_resourceBackend = resourceBackend; }

		virtual void Update() {}

	protected:
		ResourceBackend* m_resourceBackend;
	};
}