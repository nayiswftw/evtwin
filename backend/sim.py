"""Digital-twin core: virtual grid + EV batteries + closed control loop.

Every tick:  grid base load -> forecast -> price -> scheduler -> EV charging
-> new EV load -> (next tick sees updated state).
"""
import math
import random
from dataclasses import dataclass
from typing import Optional

import pricing
import scheduler
from forecast import Forecaster, true_profile, temperature

N_BAYS = 4
SIM_MIN_PER_SEC = 3.0   # 1 real second = 3 simulated minutes
ARRIVE_S = 4.0          # real seconds of drive-in / drive-out animation
LEAVE_S = 4.0
DONE_HOLD_S = 6.0       # stay plugged in after reaching target
SPIKE_KW = 25.0
SPIKE_S = 12.0
TIGHT_ON, TIGHT_OFF = 0.85, 0.70  # hysteresis on grid utilisation

MODELS = [  # name, battery kWh, max charge kW, colour
    ("Nexon EV", 40, 30, "#35c2ff"),
    ("Punch EV", 35, 25, "#ff7a59"),
    ("Windsor EV", 50, 40, "#9be15d"),
    ("Atto 3", 60, 50, "#c792ff"),
    ("XUV400", 40, 30, "#ffd23f"),
    ("Tigor EV", 26, 22, "#ff4f8b"),
]


def clock_str(minute: float) -> str:
    m = int(minute % 1440)
    return f"{m // 60:02d}:{m % 60:02d}"


@dataclass
class EV:
    id: int
    bay: int
    name: str
    color: str
    cap_kwh: float
    max_kw: float
    soc: float
    target_soc: float
    depart_min: float
    priority: int = 2
    phase: str = "arriving"  # arriving | charging | done | leaving
    clock: float = 0.0
    kw: float = 0.0
    cost: float = 0.0
    energy: float = 0.0

    def pt(self) -> float:
        if self.phase == "arriving":
            return min(1.0, self.clock / ARRIVE_S)
        if self.phase == "leaving":
            return min(1.0, self.clock / LEAVE_S)
        return 1.0


PHASE_CODE = {"arriving": 1, "charging": 2, "done": 3, "leaving": 4}


class Twin:
    def __init__(self, forecaster: Forecaster):
        self.fc = forecaster
        self._next_id = 1
        self.reset()

    # ------------------------------------------------------------------ setup
    def reset(self):
        self.minute = 8 * 60.0
        self.capacity = 100.0
        self.bays: list[Optional[EV]] = [None] * N_BAYS
        self.noise = 0.0
        self.spike_ttl = 0.0
        self.spike_kw = 0.0
        self.base_kw = 0.0
        self.ev_kw = 0.0
        self.unmanaged_kw = 0.0
        self.price = pricing.BASE_PRICE
        self.tight = False
        self.overload = False
        self.auto = True
        self.arrival_in = 6.0
        self.horizon = self.fc.horizon(self.minute)
        self.pred_hour = self.horizon[0][1]
        self._fc_age = 0.0
        self.stats = dict(energy=0.0, revenue=0.0, peak=0.0, served=0, unmet=0)
        self.add_ev()
        self.add_ev()
        self.bays[0].soc, self.bays[0].phase, self.bays[0].clock = 0.25, "charging", ARRIVE_S
        self.bays[1].soc, self.bays[1].phase, self.bays[1].clock = 0.40, "charging", ARRIVE_S

    def _new_ev(self, bay: int) -> EV:
        name, cap, mx, color = random.choice(MODELS)
        ev = EV(
            id=self._next_id, bay=bay, name=name, color=color, cap_kwh=cap, max_kw=mx,
            soc=round(random.uniform(0.08, 0.45), 2),
            target_soc=round(random.choice([0.8, 0.9, 1.0]), 2),
            depart_min=self.minute + random.uniform(90, 240),
            priority=random.choice([1, 2, 2, 3]),
        )
        self._next_id += 1
        return ev

    def add_ev(self, bay: Optional[int] = None) -> bool:
        if bay is None or not (0 <= bay < N_BAYS) or self.bays[bay] is not None:
            free = [i for i, b in enumerate(self.bays) if b is None]
            if not free:
                return False
            bay = free[0]
        self.bays[bay] = self._new_ev(bay)
        return True

    def remove_ev(self, bay: int):
        ev = self.bays[bay] if 0 <= bay < N_BAYS else None
        if ev and ev.phase != "leaving":
            ev.phase, ev.clock, ev.kw = "leaving", 0.0, 0.0

    # --------------------------------------------------------------- commands
    def handle(self, cmd: dict):
        c = cmd.get("cmd")
        bay = cmd.get("bay")
        if c == "add_ev":
            self.add_ev(bay)
        elif c == "remove_ev" and isinstance(bay, int):
            self.remove_ev(bay)
        elif c == "toggle_ev" and isinstance(bay, int) and 0 <= bay < N_BAYS:
            self.remove_ev(bay) if self.bays[bay] else self.add_ev(bay)
        elif c == "set_capacity":
            self.capacity = float(max(30, min(300, cmd.get("value", self.capacity))))
        elif c == "adjust_capacity":
            self.capacity = float(max(30, min(300, self.capacity + cmd.get("delta", 0))))
        elif c == "spike":
            self.spike_ttl = SPIKE_S
        elif c == "reset":
            self.reset()
        elif c == "toggle_auto":
            self.auto = not self.auto
        elif c in ("set_priority", "adjust_priority") and isinstance(bay, int):
            ev = self.bays[bay] if 0 <= bay < N_BAYS else None
            if ev:
                p = cmd.get("value") if c == "set_priority" else ev.priority + cmd.get("delta", 0)
                ev.priority = int(max(1, min(3, p)))

    # ------------------------------------------------------------------- tick
    def step(self, dt: float):
        dm = dt * SIM_MIN_PER_SEC
        self.minute += dm
        mod = self.minute % 1440

        # grid base load (+ noise, + demand spike)
        self.noise = 0.92 * self.noise + random.gauss(0, 0.35)
        if self.spike_ttl > 0:
            self.spike_ttl = max(0.0, self.spike_ttl - dt)
        self.spike_kw = SPIKE_KW * min(1.0, self.spike_ttl / 3.0) if self.spike_ttl > 0 else 0.0
        self.base_kw = max(2.0, true_profile(mod) + self.noise) + self.spike_kw

        # forecast (refresh every 2 real seconds)
        self._fc_age += dt
        if self._fc_age >= 2.0:
            self._fc_age = 0.0
            self.horizon = self.fc.horizon(mod)
            self.pred_hour = max(kw for off, kw in self.horizon if off <= 60)

        # auto arrivals
        self.arrival_in -= dt
        if self.arrival_in <= 0:
            self.arrival_in = random.uniform(8, 18)
            if self.auto:
                self.add_ev()

        # phase timers
        for ev in [b for b in self.bays if b]:
            if ev.phase in ("arriving", "leaving", "done"):
                ev.clock += dt
            if ev.phase == "arriving" and ev.clock >= ARRIVE_S:
                ev.phase = "charging"
            if ev.phase == "done" and ev.clock >= DONE_HOLD_S:
                self.stats["served"] += 1
                ev.phase, ev.clock = "leaving", 0.0
            if ev.phase in ("charging", "done") and self.minute >= ev.depart_min:
                if ev.soc < ev.target_soc - 0.01:
                    self.stats["unmet"] += 1
                else:
                    self.stats["served"] += 1
                ev.phase, ev.clock, ev.kw = "leaving", 0.0, 0.0
            if ev.phase == "leaving" and ev.clock >= LEAVE_S:
                self.bays[ev.bay] = None

        charging = [b for b in self.bays if b and b.phase == "charging"]

        # price + tight-grid flag from *requested* demand (stable, no feedback loop)
        req = sum(scheduler.required_kw(e, self.minute) for e in charging)
        util = (0.5 * self.base_kw + 0.5 * self.pred_hour + req) / self.capacity
        if util > TIGHT_ON:
            self.tight = True
        elif util < TIGHT_OFF:
            self.tight = False
        self.price = pricing.price_per_kwh(util)

        # schedule + charge
        # what the site would draw if every car simply charged flat-out (no scheduler)
        self.unmanaged_kw = self.base_kw + sum(scheduler.eff_max_kw(e) for e in charging)
        headroom = max(0.0, self.capacity - self.base_kw)
        alloc = scheduler.allocate(charging, headroom, self.tight, self.minute)
        dh = dm / 60.0
        self.ev_kw = 0.0
        for ev in charging:
            kw = alloc.get(ev.bay, 0.0)
            grid_kwh = kw * dh
            ev.soc = min(ev.target_soc, ev.soc + grid_kwh * scheduler.EFFICIENCY / ev.cap_kwh)
            ev.kw = kw
            ev.energy += grid_kwh
            ev.cost += grid_kwh * self.price
            self.stats["energy"] += grid_kwh
            self.stats["revenue"] += grid_kwh * self.price
            self.ev_kw += kw
            if ev.soc >= ev.target_soc - 1e-3:
                ev.phase, ev.clock, ev.kw = "done", 0.0, 0.0
        for b in self.bays:
            if b and b.phase != "charging":
                b.kw = 0.0

        self.overload = self.base_kw > self.capacity
        self.stats["peak"] = max(self.stats["peak"], self.base_kw + self.ev_kw)

    # --------------------------------------------------------------- snapshots
    def _ev_dict(self, ev: EV) -> dict:
        return {
            "id": ev.id, "bay": ev.bay, "name": ev.name, "color": ev.color,
            "cap": ev.cap_kwh, "max_kw": ev.max_kw,
            "soc": round(ev.soc, 3), "target": ev.target_soc, "priority": ev.priority,
            "phase": ev.phase, "pt": round(ev.pt(), 3), "kw": round(ev.kw, 2),
            "cost": round(ev.cost, 1), "energy": round(ev.energy, 2),
            "depart": clock_str(ev.depart_min),
            "need_kw": round(scheduler.required_kw(ev, self.minute), 1),
        }

    def snapshot(self) -> dict:
        return {
            "type": "state",
            "minute": round(self.minute, 2),
            "clock": clock_str(self.minute),
            "temp": round(temperature(self.minute % 1440), 1),
            "capacity": self.capacity,
            "base_kw": round(self.base_kw, 2),
            "ev_kw": round(self.ev_kw, 2),
            "total_kw": round(self.base_kw + self.ev_kw, 2),
            "unmanaged_kw": round(self.unmanaged_kw, 2),
            "spike": self.spike_ttl > 0,
            "overload": self.overload,
            "tight": self.tight,
            "price": self.price,
            "auto": self.auto,
            "pred_hour": round(self.pred_hour, 2),
            "forecast": [{"m": o, "kw": round(k, 2)} for o, k in self.horizon],
            "forecast_mae": round(self.fc.mae_kw or 0, 2),
            "bays": [self._ev_dict(b) if b else None for b in self.bays],
            "stats": {k: round(v, 1) if isinstance(v, float) else v for k, v in self.stats.items()},
        }

    def compact(self) -> dict:
        """Tiny payload for the ESP32."""
        return {
            "t": "s",
            "cap": round(self.capacity),
            "base": round(self.base_kw, 1),
            "ev": round(self.ev_kw, 1),
            "price": round(self.price, 1),
            "n": sum(1 for b in self.bays if b),
            "ovl": int(self.overload),
            "tight": int(self.tight),
            "clk": clock_str(self.minute),
            "bays": [
                [PHASE_CODE[b.phase], int(round(b.soc * 100)), round(b.kw, 1), b.priority]
                if b else [0, 0, 0, 0]
                for b in self.bays
            ],
        }
