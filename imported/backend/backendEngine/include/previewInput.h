/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

/// Input events used in the preview panel system
namespace tools
{
	struct EditModeViewport;

	/// Small structure representing keyboard modifier buttons (Shift, Ctrl, Alt) state.
	///
	/// Generally left/right modifiers are distinguished, but for convenience there are also methods for testing any shift, any alt etc.
	/// Each of these buttons has a corresponding bit flag: when 1 means the button is down, when 0 means the button is up.
	///
	/// Rationale for this class:
	/// It can be very useful to have this neatly wrapped up and used inside input event classes,
	/// since in many cases while handling i.e. mouse input we just need to check if shift was pressed during the mouse click etc.
	/// Manually checking the raw input state can lead to time desynchronization (mouse input event was generated
	/// when the modifier was not yet pressed etc.), and handling modifiers always as events is a pain-in-the-bottom,
	/// and also can lead to another type of desynchronization (of key state).
	struct BACKEND_ENGINE_API InputKeyModifiers
	{
		/// Enum for modifier keys.
		enum ModifierKey               // Not using enum class, it makes more sense to do so here.
		{
			LAlt = RED_FLAG(0),
			RAlt = RED_FLAG(1),
			LShift = RED_FLAG(2),
			RShift = RED_FLAG(3),
			LControl = RED_FLAG(4),
			RControl = RED_FLAG(5)
		};

		Uint8 m_flags;       // Modifier button flags, OR'ed values of ModifierKey enum. When bit is 1 means modifier button is down.


		InputKeyModifiers() { Clear(); }
		InputKeyModifiers(const InputKeyModifiers& other) { m_flags = other.m_flags; }     // Defined for speed.

		explicit InputKeyModifiers(Uint8 flags) { m_flags = flags; }

		InputKeyModifiers& operator=(const InputKeyModifiers& other) { m_flags = other.m_flags; return *this; }     // Defined for speed.

		bool IsAnyAltDown() const { return (m_flags & (LAlt | RAlt)) != 0; }
		bool IsAnyShiftDown() const { return (m_flags & (LShift | RShift)) != 0; }
		bool IsAnyControlDown() const { return (m_flags & (LControl | RControl)) != 0; }

		bool IsAnyModifierDown() const { return m_flags != 0; }

		/// Process pressing or releasing of a modifier key (alt, shift, control).
		/// Any non-modifier key (like IK_Enter) passed as argument will be silently ignored.
		void UpdateKey(EInputKey key, bool keyIsDown);

		/// Test if given key is a modifier key (any CTRL/ALT/SHIFT).
		static Bool IsModifierKey( EInputKey key );

		void Clear() { m_flags = 0; }
	};

	/// Key event (both down and up)
	struct InputKeyEvent : public red::NonCopyable
	{
		//   m_capture;			// mouse capture function
		EInputKey		m_key;

		InputKeyEvent( EInputKey key )
			: m_key ( key )
		{}
	};

	enum EMouseButton : Uint8
	{
		None = 0,
		LMB = RED_FLAG( 0 ),
		RMB = RED_FLAG( 1 ),
		MMB = RED_FLAG( 2 )
	};

	struct InputMouseBase : public red::NonCopyable
	{
		InputMouseBase( Uint8 mask, Int32 x, Int32 y, const EditModeViewport& vpt )
			: m_mask( mask ) 
			, m_x( x )
			, m_y( y )
			, m_viewport( vpt )
		{}

		Uint8						m_mask;			// mask of pressed buttons (1-LMB 2-RMB 4-MMB)
		Int32						m_x;			// position in viewport
		Int32						m_y;			// position in viewport
		const EditModeViewport&		m_viewport;		// viewport settings
	};

	/// Mouse button event (both press and release)
	struct InputMouseButton final : public InputMouseBase
	{
		InputMouseButton( EMouseButton button, Uint8 mask, Int32 x, Int32 y, const EditModeViewport& vpt, Bool down )
			: InputMouseBase( mask, x, y, vpt )
			, m_button(button)
			, m_down(down)
		{}

		EMouseButton m_button;
		Bool m_down;
	};

	/// Mouse button event (both press and release)
	struct InputMouseClick final : public InputMouseBase
	{
		EMouseButton m_button;
		Bool m_double;

		InputMouseClick( EMouseButton button, Uint8 mask, Int32 x, Int32 y, const EditModeViewport& vpt, Bool dbl )
			: InputMouseBase( mask, x, y, vpt )
			, m_button( button )
			, m_double( dbl )
		{}
	};

	/// Mouse movement event (captured/uncaptured)
	struct InputMouseMove : public InputMouseBase
	{
		InputMouseMove( Uint8 mask, Int32 x, Int32 y, Int32 dx, Int32 dy, Float rawDx, Float rawDy, Uint64 viewportID, const EditModeViewport& vpt )
			: InputMouseBase( mask, x, y, vpt )
			, m_rawDx( rawDx )
			, m_rawDy( rawDy )
			, m_dx( dx )
			, m_dy( dy )
			, m_viewportId( viewportID )
		{}

		Float						m_rawDx;		// raw mouse delta
		Float						m_rawDy;		// raw mouse delta
		Int32						m_dx;			// delta position in viewport (only captured mouse)
		Int32						m_dy;			// delta position in viewport (only captured mouse)
		Uint64						m_viewportId;
	};

	/// Mouse wheel event
	struct InputMouseWheel : public InputMouseBase
	{
		InputMouseWheel( Float delta, Uint8 mask, Int32 x, Int32 y, const EditModeViewport& vpt )
			: InputMouseBase( mask, x, y, vpt )
			, m_delta( delta )
		{}

		Float m_delta;
	};

} // tools

