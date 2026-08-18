# Precision + navigation architecture — v10

## 1. Requested heading vs contact displacement

The player command remains an authoritative world heading. Collision is not allowed to rewrite it.

When the full requested step is clear, movement is exact. When blocked, the contact solver tests small signed rotations around the requested direction (11.25°, 22.5°, 33.75°, 45°). The closest legal candidate is used as the **actual contact displacement** while the original control heading remains unchanged. A short latch preserves the chosen contact side.

A truly head-on collision with no nearby legal tangent stops; the engine does not invent a route for the human.

## 2. Bot planner / locomotion separation

The territorial planner and physical world now share a derived `bot_nav_edges[128×128]` layer built once per round.

Each cardinal edge exists only when:

- both cells are playable;
- the bot-radius disk fits at both cell centers;
- the disk also fits at the edge midpoint;
- no static obstacle overlaps those samples.

RETURN BFS traverses those precomputed edges instead of assuming every playable territory cell is physically navigable.

The chosen adjacent BFS cell is steered toward its center. This keeps the path within the current+adjacent cardinal cells while removing the old grid-axis snap caused by commanding pure ±X/±Z from an off-center physical position.

A 72-tick progress watchdog invalidates a bot plan if commanded movement produced negligible displacement. It is a fail-safe, not the primary steering mechanism.

## 3. Camera occlusion

The desired chase distance is sampled from the full v10 distance down to a minimum distance. For every candidate distance, the segment behind the player is sampled against obstacle volumes. The camera retracts quickly when blocked and expands slowly when clear.

Camera compression is presentation state only: it never pushes the player or changes territorial topology.

## 4. World-line rendering

World lines (territory edges, coast, trail bridges, turret telegraphs, placement markers) now:

- clip against the same near plane used by triangles;
- interpolate perspective-correct depth;
- participate in the z-buffer.

They therefore cannot remain visible through buildings simply because they were drawn as UI-like overlays.

## 5. Touch-host boundary

Joystick side, normalized position and physical scale are host preferences. They do not enter `odg_world`, deterministic hashes or future server authority.
