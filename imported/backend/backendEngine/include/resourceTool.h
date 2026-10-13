/**
* Copyright (c) 2015-2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once
#include "../../../common/interop/include/interopUtilsManual.h"

namespace tools
{
	class IResourceEditor;
	class IEditAction;

	/// an abstract pipeline tool runnable on the resource
	/// actions are exposed through interop, data via rtti
	class BACKEND_ENGINE_API IResourceTool : public ISerializable
	{
		RTTI_DECLARE_TYPE(IResourceTool);
		DECLARE_INTEROP(IResourceTool);

	public:
		IResourceTool();
		virtual ~IResourceTool();

		/// connect the action with object it will be acting on
		RED_INLINE void Bind(THandle< ISerializable > base) { SetParent( base.Get() ); }

		struct BACKEND_ENGINE_API Action
		{
			DECLARE_INTEROP(Action);

			CName m_name;
			String m_caption;
			String m_tooltip;
			String m_question; // if set an user will be asked to confirm - Yes/No
		};

		// display title
		virtual const AnsiChar* GetTitle() const = 0;

		// get binding name
		virtual const CName GetName() const = 0;

		// does this tool support given resource ?
		virtual const Bool SupportsResource(const CResource& res) const = 0;

		// initialize tool on a given resource, a resource editor is passed since it has usually more access
		virtual const Bool Initialize(IResourceEditor* editor) = 0;

		// external refresh (resource changed)
		virtual void Refresh() {};

		// perform action, creates UNDO data, is allowed to fail, error can be returned via the interop ctx (for now)
		virtual red::UniquePtr< IEditAction > PerformAction(interop::IContext& ctx, CName name) = 0;
		
		// get supported actions
		virtual void GetActions(red::DynArray<Action>& outActions) const;
	};

} // tools
