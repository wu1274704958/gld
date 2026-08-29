# TODO

## Formation movement

- [ ] Preserve the formation's final facing while members settle into their
  destination slots.
  - Symptom: when a squad switches to Idle at its destination, some units also
    visibly turn.
  - Confirmed cause: final slot settling can use lateral or backward correction
    velocity. `movement_tick` derives `AoeFacing` from that velocity, and
    `AoeRouteCommandCompletionModule` preserves the resulting facing when it
    clears movement and switches the unit to Idle.
  - The AoE2 bridge is not recalculating the direction, and the affected
    `walkA`/`idleA` assets both use 16 direction slots.
  - Intended follow-up: position correction at the destination must not change
    the unit's presentation facing away from the formation's final forward
    direction. Keep this behavior in formation movement/completion rather than
    adding a presentation-layer workaround.
  - Status: recorded for later; do not implement as part of the current change.
