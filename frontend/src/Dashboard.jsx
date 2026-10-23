import { useState } from 'react'

const AMPS_PER_KW = 1000 / (Math.sqrt(3) * 400) // 3-phase 400 V
const amps = (kw) => Math.round(kw * AMPS_PER_KW)
const PRIO = { 1: 'P1', 2: 'P2', 3: 'P3' }

const Icon = ({ children }) => (
  <svg viewBox="0 0 32 32" width="30" height="30" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round">
    {children}
  </svg>
)
const CarIcon = () => (
  <svg viewBox="0 0 28 14" width="26" height="13" fill="none" stroke="currentColor" strokeWidth="1.3" strokeLinejoin="round">
    <path d="M2 11V9l2-2.5 5-1.5 3-3h6l3 3 4 1.2V11z" />
    <circle cx="8" cy="11" r="2" fill="#050505" />
    <circle cx="21" cy="11" r="2" fill="#050505" />
  </svg>
)

// ---------------------------------------------------------------- chart
function LoadChart({ history, capacity }) {
  const W = 340, H = 176, L = 34, R = 8, T = 8, B = 22
  const n = history.length
  if (n < 2) return <div className="chartempty">Collecting data…</div>
  const peak = Math.max(capacity, ...history.map((p) => Math.max(p.bal, p.unm)))
  const max = Math.ceil((peak * 1.05) / 50) * 50
  const x = (i) => L + (i / (n - 1)) * (W - L - R)
  const y = (val) => T + (1 - val / max) * (H - T - B)
  const path = (key) => history.map((p, i) => `${i ? 'L' : 'M'}${x(i).toFixed(1)},${y(p[key]).toFixed(1)}`).join(' ')
  const ticks = [0, 0.25, 0.5, 0.75, 1].map((f) => f * max)
  const xt = [0, Math.floor((n - 1) / 3), Math.floor(((n - 1) * 2) / 3), n - 1]
  return (
    <svg viewBox={`0 0 ${W} ${H}`} className="chart" role="img" aria-label="Load balancing over time: managed versus unmanaged">
      {ticks.map((t) => (
        <g key={t}>
          <line x1={L} x2={W - R} y1={y(t)} y2={y(t)} className="grid" />
          <text x={L - 6} y={y(t) + 3} textAnchor="end" className="axis">{Math.round(t)}</text>
        </g>
      ))}
      {xt.map((i) => (
        <text key={i} x={x(i)} y={H - 6} textAnchor={i === 0 ? 'start' : i === n - 1 ? 'end' : 'middle'} className="axis">
          {history[i].clock}
        </text>
      ))}
      <line x1={L} x2={W - R} y1={y(capacity)} y2={y(capacity)} className="budget-line" />
      <path d={path('base')} className="l-base" />
      <path d={path('unm')} className="l-unm" />
      <path d={path('bal')} className="l-bal" />
    </svg>
  )
}

// --------------------------------------------------------------- forecast
function ForecastChart({ s }) {
  const W = 340, H = 110, L = 34, R = 8, T = 8, B = 20
  const pts = s.forecast.map((p) => p.kw)
  const max = Math.ceil(Math.max(s.capacity, ...pts) * 1.05 / 25) * 25
  const x = (i) => L + (i / (pts.length - 1)) * (W - L - R)
  const y = (v) => T + (1 - v / max) * (H - T - B)
  const line = pts.map((v, i) => `${i ? 'L' : 'M'}${x(i).toFixed(1)},${y(v).toFixed(1)}`).join(' ')
  const hrs = [0, 2, 4, 6]
  return (
    <svg viewBox={`0 0 ${W} ${H}`} className="chart" role="img" aria-label="Forecast of site base load for the next six hours">
      {[0, 0.5, 1].map((f) => (
        <g key={f}>
          <line x1={L} x2={W - R} y1={y(f * max)} y2={y(f * max)} className="grid" />
          <text x={L - 6} y={y(f * max) + 3} textAnchor="end" className="axis">{Math.round(f * max)}</text>
        </g>
      ))}
      <line x1={L} x2={W - R} y1={y(s.capacity)} y2={y(s.capacity)} className="budget-line" />
      <path d={`${line} L${x(pts.length - 1)},${y(0)} L${x(0)},${y(0)} Z`} className="fc-area" />
      <path d={line} className="l-bal" />
      <circle cx={x(0)} cy={y(s.base_kw)} r="3.5" fill="#fff" />
      {hrs.map((h) => (
        <text key={h} x={x((h / 6) * (pts.length - 1))} y={H - 5} textAnchor={h === 0 ? 'start' : h === 6 ? 'end' : 'middle'} className="axis">
          {h === 0 ? 'now' : `+${h}h`}
        </text>
      ))}
    </svg>
  )
}

// ----------------------------------------------------------- distribution
function Row({ i, ev, send, open, toggle }) {
  const kw = ev && ev.phase === 'charging' ? ev.kw : 0
  const filled = ev ? Math.round((kw / ev.max_kw) * 20) : 0
  return (
    <div className={`dwrap ${ev ? '' : 'free'}`}>
      <div className="drow">
        <button className="num" onClick={() => ev && toggle(i)} aria-expanded={!!(ev && open)} title={ev ? 'Show details' : ''}>
          {String(i + 1).padStart(2, '0')}
        </button>
        <span className="ico">{ev ? <CarIcon /> : null}</span>
        <div className="segs" aria-label={ev ? `${amps(kw)} amps` : 'free'}>
          {Array.from({ length: 20 }, (_, k) => (
            <i key={k} className={k < filled ? 'on' : ''} />
          ))}
        </div>
        <span className="amp">{ev ? `${amps(kw)} A` : '—'}</span>
        {ev ? (
          <>
            <button className="chip" title="Change priority" onClick={() => send({ cmd: 'set_priority', bay: i, value: (ev.priority % 3) + 1 })}>
              {PRIO[ev.priority]}
            </button>
            <button className="chip x" title="Unplug" onClick={() => send({ cmd: 'remove_ev', bay: i })}>×</button>
          </>
        ) : (
          <>
            <span />
            <button className="chip x" title="Send an EV" onClick={() => send({ cmd: 'add_ev', bay: i })}>+</button>
          </>
        )}
      </div>
      {ev && (
        <div className="dline">
          {ev.name} · {Math.round(ev.soc * 100)}%→{Math.round(ev.target * 100)}% · {kw.toFixed(1)} kW · ₹{Math.round(ev.cost)} · leaves {ev.depart}
        </div>
      )}
      {ev && open && (
        <div className="detail">
          <div className="dgrid">
            <span>Battery</span><b>{ev.cap} kWh · max {ev.max_kw} kW</b>
            <span>Needs</span><b>{ev.need_kw} kW to leave on time</b>
            <span>Session</span><b>{ev.energy} kWh · ₹{ev.cost}</b>
            <span>State</span><b>{ev.phase}</b>
          </div>
          <div className="seg" role="group" aria-label={`Priority for bay ${i + 1}`}>
            {[[1, 'Low'], [2, 'Normal'], [3, 'High']].map(([val, label]) => (
              <button key={val} className={ev.priority === val ? 'on' : ''} onClick={() => send({ cmd: 'set_priority', bay: i, value: val })}>
                {label}
              </button>
            ))}
          </div>
        </div>
      )}
    </div>
  )
}

// ---------------------------------------------------------------- overview
function Overview({ s }) {
  const active = s.bays.filter((b) => b && b.phase === 'charging' && b.kw > 0.1).length
  const nodes = [
    { name: 'Demand forecast', val: `${Math.round(s.pred_hour)} kW next hr`, icon: <Icon><path d="M4 24l7-8 6 4 11-14" /><path d="M4 28h24" /></Icon> },
    { name: 'Scheduler', val: s.tight ? 'throttling' : 'full speed', icon: <Icon><path d="M5 9h22M5 16h22M5 23h22" /><circle cx="11" cy="9" r="2.5" fill="#050505" /><circle cx="21" cy="16" r="2.5" fill="#050505" /><circle cx="14" cy="23" r="2.5" fill="#050505" /></Icon> },
    { name: 'Safety & limits', val: s.overload ? 'OVERLOAD' : 'within budget', bad: s.overload, icon: <Icon><path d="M16 4l10 4v8c0 6-5 10-10 12C11 26 6 22 6 16V8z" /><path d="M11 16l4 4 7-8" /></Icon> },
    { name: 'Chargers', val: `${active} delivering`, icon: <Icon><rect x="9" y="4" width="12" height="22" rx="2" /><rect x="12" y="8" width="6" height="5" /><path d="M21 14h3a2 2 0 012 2v6" /></Icon> },
    { name: 'EVs', val: `${s.bays.filter(Boolean).length} on site`, icon: <Icon><path d="M3 21v-4l3-4 7-2 4-4h6l4 5 2 2v7z" /><circle cx="10" cy="21" r="2.5" fill="#050505" /><circle cx="23" cy="21" r="2.5" fill="#050505" /></Icon> },
  ]
  return (
    <div className="panel overview">
      <h3>SYSTEM OVERVIEW</h3>
      <div className="flow">
        {nodes.map((n, i) => (
          <div className="node" key={n.name}>
            <div className="ni">{n.icon}</div>
            <b>{n.name}</b>
            <span className={n.bad ? 'badtxt' : ''}>{n.val}</span>
            {i < nodes.length - 1 && <u className="link" />}
          </div>
        ))}
      </div>
    </div>
  )
}

// -------------------------------------------------------------------- main
export default function Dashboard({ state: s, connected, history, send }) {
  const [open, setOpen] = useState(null)
  if (!s) {
    return (
      <div className="hud">
        <div className="title">
          <h1>SMART <em>CHARGING.</em></h1>
          <p>{connected ? 'Waiting for the first update…' : 'Backend not reachable. Start it with:\nuvicorn main:app --host 0.0.0.0 --port 8000'}</p>
        </div>
      </div>
    )
  }
  const totalKw = s.bays.reduce((a, b) => a + (b && b.phase === 'charging' ? b.kw : 0), 0)
  return (
    <div className="hud">
      <div className="title">
        <h1>SMART <em>CHARGING.</em></h1>
        <p>Intelligent load management software{'\n'}for shared EV charging infrastructure.</p>
      </div>

      {s.overload && <div className="alarm">GRID OVERLOAD: demand is above the site budget</div>}

      <div className="toolbar">
        <div className="tstat">
          <span>{s.clock} · {s.temp}°C</span>
          <span className={s.tight ? 'amber' : ''}>₹{s.price.toFixed(1)}/kWh</span>
          <span>{s.stats.energy} kWh sold</span>
          <span className={connected ? 'ok' : 'badtxt'}>{connected ? '● LIVE' : '● RECONNECTING'}</span>
        </div>
        <div className="tctl">
          <label className="slider">
            <span>BUDGET {Math.round(s.capacity)} kW</span>
            <input type="range" min="30" max="300" step="5" value={s.capacity} onChange={(e) => send({ cmd: 'set_capacity', value: Number(e.target.value) })} />
          </label>
          <button className="btn" onClick={() => send({ cmd: 'add_ev' })}>Add EV</button>
          <button className="btn amberb" disabled={s.spike} onClick={() => send({ cmd: 'spike' })}>{s.spike ? 'Spike…' : 'Spike'}</button>
          <button className={`btn ghost ${s.auto ? 'on' : ''}`} onClick={() => send({ cmd: 'toggle_auto' })}>Auto {s.auto ? 'on' : 'off'}</button>
          <button className="btn ghost" onClick={() => send({ cmd: 'reset' })}>Reset</button>
        </div>
        <p className="note">
          {s.tight
            ? 'Grid is tight: each car gets only the rate it needs to leave on time.'
            : 'Grid has room: cars charge at full speed while power is cheap.'}
        </p>
      </div>

      <Overview s={s} />

      <div className="rightcol">
        <div className="panel">
          <h3>LOAD BALANCING OVER TIME <small>WINDOW: 1 MIN LIVE</small></h3>
          <LoadChart history={history} capacity={s.capacity} />
          <div className="legend">
            <span><i className="k bal" /> BALANCED LOAD</span>
            <span><i className="k unm" /> UNMANAGED</span>
            <span><i className="k bud" /> BUDGET</span>
          </div>
        </div>
        <div className="panel">
          <h3>DEMAND FORECAST <small>NEXT 6 H · ±{s.forecast_mae} kW</small></h3>
          <ForecastChart s={s} />
        </div>
        <div className="panel dist">
          <h3>CURRENT DISTRIBUTION <small className="ok">LIVE</small></h3>
          {s.bays.map((ev, i) => <Row key={i} i={i} ev={ev} send={send} open={open === i} toggle={(k) => setOpen(open === k ? null : k)} />)}
          <div className="total">
            <span>TOTAL</span>
            <b>{amps(totalKw)} A <small>· {totalKw.toFixed(0)} kW</small></b>
          </div>
        </div>
        <div className="panel">
          <h3>SESSION</h3>
          <div className="stats">
            <span>Energy sold<b>{s.stats.energy} kWh</b></span>
            <span>Revenue<b>₹{Math.round(s.stats.revenue)}</b></span>
            <span>Peak load<b>{s.stats.peak} kW</b></span>
            <span>Served / missed<b>{s.stats.served} / {s.stats.unmet}</b></span>
          </div>
        </div>
      </div>
    </div>
  )
}
