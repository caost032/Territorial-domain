# Fixed joystick placement — v10 host

The movement joystick base is fixed during gameplay. It never follows a pointer or recenters under a new touch.

## User settings

- `LEFT`: preset near the lower-left thumb zone.
- `RIGHT`: preset near the lower-right thumb zone.
- `ADJUST POSITION`: pauses simulation and allows dragging the joystick base to an exact location.
- `RESET`: restores the left default.

Custom positions are stored as normalized viewport X/Y and clamped inside a safe radius on every resize/orientation change.

## Architecture

Placement is host-application state. It is deliberately absent from deterministic world state and state hashing. The engine continues to expose control heading/local projections and consume normalized input only.

## Interaction

During play the base remains fixed. The knob can rotate inside the base according to the engine's current camera-local projection of the world heading. This retains heading readability without moving the control itself.


## v10 addition
The host may persist joystick physical scale (`0.85`, `1.0`, `1.18`) in addition to normalized position. Scale remains outside C authority.

> Historical note: v12 supersedes the v10 heading-coupled knob behavior. The base remains fixed, but the current normal-play joystick is purely camera-local and no longer rebases itself as camera yaw changes.
