# AI EV Charging Station: Digital Twin

A 3D simulation of a 4-bay EV charging station. A Python backend runs the grid,
the batteries, an ML demand forecast, dynamic pricing and a power scheduler.
A browser shows it live in 3D, and an ESP32-S3 with an LCD acts as a physical
display and joystick controller.

```
backend/   Python + FastAPI simulation, scikit-learn forecast, scheduler, WebSocket
frontend/  React + Three.js (React Three Fiber) 3D scene and dashboard
firmware/  ESP32-S3 (PlatformIO / Arduino): LCD, joystick, buttons, buzzer
```

## 1. Run the backend

```bash
cd backend
python -m venv .venv && source .venv/bin/activate   # Windows: .venv\Scripts\activate
pip install -r requirements.txt
uvicorn main:app --host 0.0.0.0 --port 8000
```
`--host 0.0.0.0` lets the ESP32 reach it. First start trains the forecaster (~2 s).

## 2. Run the 3D frontend

```bash
cd frontend
npm install
npm run dev          # open http://localhost:5173
```
(Or `npm run build` once, and the backend serves the UI itself at http://localhost:8000.)

## 3. Flash the ESP32-S3

1. Install VS Code + the PlatformIO extension.
2. Open the `firmware/` folder.
3. Edit `src/secrets.h`: Wi-Fi name/password and your laptop's IP.
4. Edit `src/config.h`: set every GPIO number for your board (the build stops until you do).
5. Upload, then open the serial monitor at 115200.

Laptop and ESP32 must be on the same Wi-Fi. If it shows `SERVER...`, allow
port 8000 through the laptop firewall, and avoid guest networks / hotspots with
"client isolation".

### Controls on the device
| Input | Bays mode | Grid mode |
|---|---|---|
| Stick left / right | select bay | site budget -/+10 kW (hold = faster) |
| Stick up / down | raise / lower that bay's priority | |
| Stick click | plug in / unplug EV in selected bay | same |
| SPIKE button | demand spike | same |
| RESET button (hold 1 s) | reset simulation | same |
| BACK button | switch to Grid mode | back to Bays mode |

The buzzer sounds an alarm on grid overload.

## How the AI loop works
1. **Forecast**: a random forest (time of day + temperature) predicts base load for the next 6 h.
2. **Pricing**: price = f((expected base load + requested EV power) / capacity), from Rs 6 to Rs 30 per kWh.
3. **Scheduler**: every car first gets the rate it needs to reach its target by departure
   (urgent and high-priority first). If the grid is not tight, spare power is shared by priority weight.
   If it is tight, cars get only what they need.
4. **Closed loop**: EV charging changes grid load, which changes price and headroom on the next tick.

The right-hand chart compares the **balanced** load (green, what the scheduler actually draws) with an **unmanaged** line (what the site would draw if every car charged flat-out), against the site budget.

Try it: drop the budget to ~70 kW, add EVs, hit **Demand spike**. Prices rise, the scheduler
throttles to needed rates, and if base load alone exceeds capacity the overload alarm fires.

## WebSocket protocol
Connect to `ws://<laptop>:8000/ws?role=web` (full JSON state, 10 Hz) or `?role=device`
(compact state, 2 Hz). Send commands as JSON:
`{"cmd":"add_ev","bay":0}`, `remove_ev`, `toggle_ev`, `set_capacity` (`value`), `adjust_capacity` (`delta`),
`set_priority` (`bay`,`value` 1-3), `adjust_priority` (`bay`,`delta`), `spike`, `toggle_auto`, `reset`.
REST helpers: `GET /api/state`, `POST /api/cmd`.

## Tuning
Constants are at the top of `backend/sim.py` (simulation speed, spike size, tight-grid thresholds),
`pricing.py` (price curve) and `scheduler.py` (priority weights, charge taper).
