# Territory Turrets + Logistics v8

Turrets are infrastructure, not weapons against HP.

## Target cycle

1. Search for nearest exposed enemy trail in range.
2. If none exists, search enemy territory.
3. Lock/telegraph for 240–360 ticks (~2.00–3.00 s).
4. If the target ceases to exist or leaves range, cancel without spending ammo.
5. Fire one topology-changing shot.
6. Cool down for roughly 360–504 ticks (~3.00–4.20 s).

This intentionally lets a careful player expand in short, thick loops near an enemy turret while
long careless trails remain vulnerable.

## Ownership

A turret transfers when one actor controls at least the configured majority of its local 5×5
playable neighborhood. Merely touching the turret never captures it.

## Transport

Only an owned turret can be picked up. Carrying folds it into a compact inactive module. The
placement target is exactly 1.9 m in front of player facing; if that exact point is blocked or
outside playable land, placement fails safely and the turret remains carried.

## Supply

- Baseline turret capacity: 48 rounds.
- Capturing territory accumulates 1 reserve round per ~10 newly captured cells.
- Reserve stays on the actor until within delivery distance of an owned depleted turret.
- 14 physical supply crates spawn around unowned playable land with bounded ammo quantities.
- A carried crate automatically unloads into nearby owned turrets.
- Bots use the same delivery rules.
