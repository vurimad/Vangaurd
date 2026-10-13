/**
* Copyright � 2013 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "resourceEditor.h"
#include "resourceTool.h"

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE(IResourceTool, tools);
RTTI_PARENT_TYPE(ISerializable);
RTTI_END_TYPE();


namespace tools
{

	IResourceTool::IResourceTool()
	{}

	IResourceTool::~IResourceTool()
	{}

	void IResourceTool::SetupInterop(IResourceTool* ptr, ::interop::utils::Object& op)
	{	
	}

	void IResourceTool::Action::SetupInterop(Action* ptr, ::interop::utils::Object& op)
	{
		op.RegisterValueRO<CName>("name", ptr->m_name);
		op.RegisterValueRO<String>("caption", ptr->m_caption);
		op.RegisterValueRO<String>("question", ptr->m_question);
		op.RegisterValueRO<String>("tooltip", ptr->m_tooltip);
	}

	void IResourceTool::GetActions(red::DynArray<Action>& outActions) const
	{
		Action info;
		info.m_caption = "Process";
		info.m_name = RED_NAME_CONSTEXPR("Process");
		outActions.PushBack(info);
	}

} // ed

