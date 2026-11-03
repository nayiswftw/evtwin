"""Power scheduler: splits the grid headroom between charging EVs.

Pass 1  every EV gets the rate it needs to hit its target SoC by its
        departure time ("required rate"), most urgent / highest priority first.
Pass 2  if the grid is NOT tight, leftover power is shared out by priority
        weight (water-filling) so cars fill up fast while energy is cheap.
        If the grid IS tight, pass 2 is skipped: cars only get what they need.
"""

PRIORITY_WEIGHT = {1: 1.0, 2: 2.0, 3: 4.0}  # low / normal / high
EFFICIENCY = 0.92  # grid kWh -> battery kWh


def eff_max_kw(ev) -> float:
    """Charger limit with the usual taper above 80% state of charge."""
    if ev.soc < 0.8:
        return ev.max_kw
    return ev.max_kw * max(0.2, (1.0 - ev.soc) / 0.2)


def required_kw(ev, now_min: float) -> float:
    """Average grid power needed to reach target SoC by departure."""
    need_grid = max(0.0, ev.target_soc - ev.soc) * ev.cap_kwh / EFFICIENCY
    hours_left = max((ev.depart_min - now_min) / 60.0, 0.1)
    return min(need_grid / hours_left, eff_max_kw(ev))


def allocate(evs, headroom_kw: float, defer: bool, now_min: float) -> dict:
    alloc = {ev.bay: 0.0 for ev in evs}
    remaining = max(0.0, headroom_kw)

    order = sorted(
        evs,
        key=lambda e: -(PRIORITY_WEIGHT[e.priority] * required_kw(e, now_min)),
    )
    for ev in order:
        give = min(required_kw(ev, now_min), eff_max_kw(ev), remaining)
        alloc[ev.bay] += give
        remaining -= give

    if not defer:
        active = [e for e in evs if eff_max_kw(e) - alloc[e.bay] > 1e-6]
        while remaining > 1e-6 and active:
            total_w = sum(PRIORITY_WEIGHT[e.priority] for e in active)
            spent, nxt = 0.0, []
            for e in active:
                share = remaining * PRIORITY_WEIGHT[e.priority] / total_w
                room = eff_max_kw(e) - alloc[e.bay]
                g = min(share, room)
                alloc[e.bay] += g
                spent += g
                if room - g > 1e-6:
                    nxt.append(e)
            remaining -= spent
            if spent < 1e-6:
                break
            active = nxt
    return alloc
