# AI EV Charging Station: Digital Twin

A 3D simulation of a 4-bay EV charging station. A Python backend runs the grid,
the batteries, an ML demand forecast, dynamic pricing and a power scheduler.
A browser shows it live in 3D, and an ESP32-S3 with an LCD acts as a physical
display and four-button controller.

```
backend/   Python + FastAPI simulation, scikit-learn forecast, scheduler, WebSocket
frontend/  React + Three.js (React Three Fiber) 3D scene and dashboard
firmware/  ESP32-S3 (PlatformIO / Arduino): LCD, four touch buttons, RGB LED, buzzer
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
4. `src/config.h` is preconfigured for the Waveshare ESP32-S3-LCD-2 and the listed modules.
5. Upload, then open the serial monitor at 115200.

Laptop and ESP32 must be on the same Wi-Fi. If it shows `SERVER...`, allow
port 8000 through the laptop firewall, and avoid guest networks / hotspots with
"client isolation".

### Controls on the device
| Input | Action |
|---|---|
| Simulation off | Touch buttons 1–4 add/remove the EV in their matching bay; clock and charging continue |
| Simulation on | Automatic arrivals run; two-key chords: 1+2 spike, 1+3 lower priority, 1+4 previous bay, 2+3 next bay, 2+4 raise priority, hold 3+4 reset |
| Single-key fallback | 1/2 select bays or adjust grid budget; hold 1/2 changes priority or repeats budget. 3 toggles selected EV or holds for spike. 4 switches Bays/Grid or holds for reset |
| RGB LED | Green: normal; blue: tight/mild load; red: overload; off: device/backend disconnected |
| Active buzzer | Short confirmation beep on touch; repeating alarm during overload |

Wire touch outputs 1-4 to GPIO 4, 13, 6, and 7 respectively, RGB R/G/B to GPIO 8-10, and the
buzzer S pin to GPIO 11. Connect module grounds to board GND. Power the touch
module from 3V3 (not 5V) so its outputs stay within ESP32-S3 input limits; the
RGB module is common-cathode and has onboard current-limiting resistors. The
LCD-2-C camera is held in power-down by the firmware because these header GPIOs
are shared with its camera bus. If your board revision differs, verify its
pinout before wiring the external modules.

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
