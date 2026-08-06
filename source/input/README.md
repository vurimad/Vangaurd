# Input

The input module defines Vanguard's platform-neutral physical input vocabulary and immutable frame-state contracts. Platform adapters translate native events into `RawEvent`; engine services consume those events during the Input frame phase. SDL is an implementation detail of the current Windows backend and never appears in this API.

Gameplay actions, contexts, rebinding, chords, dead zones, sensitivity curves, and listener dispatch belong to a higher mapping layer. Keeping physical collection separate makes editor viewports, automated input, replay, remote input, and future platforms use the same engine contract.

## Frame contract

`InputService::Snapshot()` is stable after the Input phase until the next frame. Persistent state (`IsDown`) is distinct from one-frame transitions (`WasPressed` and `WasReleased`). Mouse motion and wheel values accumulate all events received before the frame. `Events()` retains ordered physical changes, including synthetic releases caused by focus loss, explicit reset, device removal, or backend overflow recovery.

Keyboard identity is based on physical key position, not the active text layout. Text entry is a separate UTF-8 event stream so gameplay bindings and editor text fields do not conflict. Gamepads use generation-bearing device IDs, normalized axes, connection events, and bounded rumble commands. The aggregate snapshot records the last meaningfully active device for automatic UI-hint switching.

```cpp
const input::FrameSnapshot& frame = inputService.Snapshot();
if (frame.keyboard.WasPressed(input::Key::Escape)) OpenPauseMenu();
if (frame.mouse.IsDown(input::MouseButton::Right)) RotateCamera(frame.mouse.deltaX, frame.mouse.deltaY);
if (const input::GamepadState* pad = inputService.FindGamepad(activePad))
    MovePlayer(pad->Axis(input::GamepadAxis::LeftX), pad->Axis(input::GamepadAxis::LeftY));
```

The fixed backend queue and frame buffers allocate no memory while polling or publishing input. Overflow is observable in `InputStats`; the service releases all held state before applying the surviving batch so lost events cannot leave a control stuck.

Window-targeted events carry a generational `window::WindowHandle`, never an SDL or operating-system window identifier. Global device events use an invalid window handle. The platform pump updates window state first and then queues input against the resolved handle, preserving one native event order across windows, input, and editor integrations.
