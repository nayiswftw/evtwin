"""FastAPI server: runs the twin and streams it to the browser and the ESP32.

    uvicorn main:app --host 0.0.0.0 --port 8000
"""
import asyncio
import json
import time
from contextlib import asynccontextmanager
from pathlib import Path

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.staticfiles import StaticFiles

from forecast import Forecaster
from sim import Twin

TICK_HZ = 10
DEVICE_EVERY = 5  # ESP32 gets 2 updates per second

twin: Twin | None = None
clients: dict[WebSocket, str] = {}


async def broadcast(tick: int):
    snap = twin.snapshot()
    compact = twin.compact() if tick % DEVICE_EVERY == 0 else None
    for ws, role in list(clients.items()):
        try:
            if role == "device":
                if compact:
                    await ws.send_text(json.dumps(compact, separators=(",", ":")))
            else:
                await ws.send_text(json.dumps(snap, separators=(",", ":")))
        except Exception:
            clients.pop(ws, None)


async def sim_loop():
    last, tick = time.monotonic(), 0
    while True:
        await asyncio.sleep(1 / TICK_HZ)
        now = time.monotonic()
        twin.step(min(now - last, 0.25))
        last = now
        tick += 1
        await broadcast(tick)


@asynccontextmanager
async def lifespan(app: FastAPI):
    global twin
    print("Training demand forecaster ...")
    fc = Forecaster().train()
    print(f"Forecaster ready (held-out MAE {fc.mae_kw:.2f} kW)")
    twin = Twin(fc)
    task = asyncio.create_task(sim_loop())
    yield
    task.cancel()


app = FastAPI(title="EV Charging Digital Twin", lifespan=lifespan)


@app.websocket("/ws")
async def ws_endpoint(websocket: WebSocket, role: str = "web"):
    await websocket.accept()
    clients[websocket] = "device" if role == "device" else "web"
    print(f"+ {clients[websocket]} connected ({len(clients)} clients)")
    try:
        while True:
            try:
                twin.handle(json.loads(await websocket.receive_text()))
            except json.JSONDecodeError:
                pass
    except WebSocketDisconnect:
        pass
    finally:
        clients.pop(websocket, None)
        print(f"- {role} disconnected ({len(clients)} clients)")


@app.get("/api/state")
def api_state():
    return twin.snapshot()


@app.post("/api/cmd")
def api_cmd(cmd: dict):
    twin.handle(cmd)
    return {"ok": True}


dist = Path(__file__).parent.parent / "frontend" / "dist"
if dist.exists():  # after `npm run build`, the backend serves the UI too
    app.mount("/", StaticFiles(directory=dist, html=True), name="ui")
