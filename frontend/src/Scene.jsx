import { useEffect, useMemo, useRef } from 'react'
import { Canvas, useFrame, useThree } from '@react-three/fiber'
import { Html, Line, OrbitControls, RoundedBox } from '@react-three/drei'
import * as THREE from 'three'

const BAY_X = [-6.3, -2.1, 2.1, 6.3]
const ROAD_Z = 7
const PARK_Z = 1.2
const CHG_Z = -2.0
const BUDGET_Y = 6.4
const GREEN = '#3ddc97'
const RED = '#ff5a6a'
const v = (x, y, z) => new THREE.Vector3(x, y, z)
const ease = (t) => (t < 0.5 ? 2 * t * t : 1 - Math.pow(-2 * t + 2, 2) / 2)

function samplePoly(pts, t) {
  const segs = []
  let total = 0
  for (let i = 0; i < pts.length - 1; i++) {
    const l = pts[i].distanceTo(pts[i + 1])
    segs.push(l)
    total += l
  }
  let d = Math.min(Math.max(t, 0), 1) * total
  for (let i = 0; i < segs.length; i++) {
    if (d <= segs[i] || i === segs.length - 1) {
      const a = pts[i]
      const b = pts[i + 1]
      const k = segs[i] > 0 ? Math.min(d / segs[i], 1) : 0
      return { pos: a.clone().lerp(b, k), dir: b.clone().sub(a).normalize(), seg: i }
    }
    d -= segs[i]
  }
}

// ---- car geometry: side silhouette extruded across the width ---------------
let carGeo = null
function getCarGeo() {
  if (carGeo) return carGeo
  const mk = (pts, depth, bevel) => {
    const sh = new THREE.Shape()
    pts.forEach(([z, y], i) => (i ? sh.lineTo(z, y) : sh.moveTo(z, y)))
    const g = new THREE.ExtrudeGeometry(sh, {
      depth, bevelEnabled: bevel > 0, bevelSize: bevel, bevelThickness: bevel, bevelSegments: 2,
    })
    g.translate(0, 0, -depth / 2)
    g.rotateY(-Math.PI / 2) // shape x -> car length (+z = front), extrusion -> width
    return g
  }
  const body = new THREE.Shape()
  body.moveTo(-2, 0.3)
  body.lineTo(-2, 0.74)
  body.quadraticCurveTo(-1.97, 0.93, -1.6, 0.97)
  body.bezierCurveTo(-1.2, 1.05, -1.0, 1.38, -0.5, 1.42)
  body.bezierCurveTo(0.0, 1.47, 0.45, 1.45, 0.8, 1.12)
  body.quadraticCurveTo(1.0, 0.97, 1.5, 0.9)
  body.bezierCurveTo(1.85, 0.85, 2.0, 0.75, 2.0, 0.55)
  body.lineTo(2, 0.3)
  body.lineTo(-2, 0.3)
  const glass = new THREE.Shape()
  glass.moveTo(-0.9, 1.0)
  glass.bezierCurveTo(-0.8, 1.2, -0.55, 1.35, -0.3, 1.37)
  glass.bezierCurveTo(0.1, 1.41, 0.4, 1.38, 0.72, 1.06)
  glass.lineTo(0.7, 1.0)
  glass.lineTo(-0.9, 1.0)
  const ext = (shape, depth, bevel) => {
    const g = new THREE.ExtrudeGeometry(shape, {
      depth, curveSegments: 28, bevelEnabled: bevel > 0, bevelSize: bevel, bevelThickness: bevel, bevelSegments: 5,
    })
    g.translate(0, 0, -depth / 2)
    g.rotateY(-Math.PI / 2)
    return g
  }
  carGeo = { body: ext(body, 1.5, 0.1), glass: ext(glass, 1.8, 0) }
  return carGeo
}

function Flow({ getPoint, count = 6, speed = 0.4, color = GREEN, size = 0.1, active = true }) {
  const refs = useRef([])
  const phase = useRef(0)
  useFrame((_, dt) => {
    phase.current = (phase.current + dt * speed) % 1
    refs.current.forEach((m, i) => {
      if (!m) return
      m.visible = active
      if (active) m.position.copy(getPoint((phase.current + i / count) % 1))
    })
  })
  return (
    <group>
      {Array.from({ length: count }, (_, i) => (
        <mesh key={i} ref={(el) => (refs.current[i] = el)} visible={false}>
          <sphereGeometry args={[size, 8, 8]} />
          <meshBasicMaterial color={color} />
        </mesh>
      ))}
    </group>
  )
}

function Vehicle({ ev, bx }) {
  const outer = useRef()
  const inner = useRef()
  const heading = useRef(Math.PI)
  const ready = useRef(false)
  const geo = getCarGeo()
  const path = useMemo(
    () => ({
      arrive: [v(-19, 0, ROAD_Z), v(bx, 0, ROAD_Z), v(bx, 0, PARK_Z)],
      leave: [v(bx, 0, PARK_Z), v(bx, 0, ROAD_Z), v(19, 0, ROAD_Z)],
    }),
    [bx],
  )

  useFrame((_, dt) => {
    let pos
    let h = Math.PI
    if (ev.phase === 'arriving') {
      const s = samplePoly(path.arrive, ease(ev.pt))
      pos = s.pos
      h = Math.atan2(s.dir.x, s.dir.z)
    } else if (ev.phase === 'leaving') {
      const s = samplePoly(path.leave, ease(ev.pt))
      pos = s.pos
      h = s.seg === 0 ? Math.PI : Math.atan2(s.dir.x, s.dir.z)
    } else pos = path.arrive[2]
    if (!ready.current) {
      outer.current.position.copy(pos)
      heading.current = h
      ready.current = true
    }
    const k = 1 - Math.exp(-dt * 10)
    outer.current.position.lerp(pos, k)
    heading.current += Math.atan2(Math.sin(h - heading.current), Math.cos(h - heading.current)) * k
    inner.current.rotation.y = heading.current
  })

  const active = ev.phase === 'charging' && ev.kw > 0.1
  return (
    <group ref={outer}>
      <group ref={inner}>
        <mesh geometry={geo.body} castShadow>
          <meshPhysicalMaterial color="#ffffff" roughness={0.35} metalness={0} clearcoat={0.6} clearcoatRoughness={0.25} />
        </mesh>
        <mesh geometry={geo.glass}>
          <meshStandardMaterial color="#14181b" roughness={0.1} metalness={0.6} />
        </mesh>
        {[[-0.85, 1.25], [0.85, 1.25], [-0.85, -1.3], [0.85, -1.3]].map(([x, z], i) => (
          <mesh key={i} position={[x, 0.36, z]} rotation={[0, 0, Math.PI / 2]} castShadow>
            <cylinderGeometry args={[0.36, 0.36, 0.22, 16]} />
            <meshStandardMaterial color="#151515" roughness={0.8} />
          </mesh>
        ))}
        <mesh position={[0, 0.78, -2.04]}>
          <boxGeometry args={[1.2, 0.06, 0.04]} />
          <meshBasicMaterial color="#ff5a4a" />
        </mesh>
        {[-0.55, 0.55].map((x) => (
          <mesh key={x} position={[x, 0.7, 2.01]}>
            <boxGeometry args={[0.35, 0.07, 0.04]} />
            <meshBasicMaterial color="#ffffff" />
          </mesh>
        ))}
        {active && (
          <mesh position={[0, 0.03, 0]} rotation={[-Math.PI / 2, 0, 0]}>
            <planeGeometry args={[2.3, 4.6]} />
            <meshBasicMaterial color={GREEN} transparent opacity={0.06 + Math.min(ev.kw / 150, 0.14)} />
          </mesh>
        )}
      </group>
      {(ev.phase === 'charging' || ev.phase === 'done') && (
        <Html position={[0, 2.5, 0]} center style={{ pointerEvents: 'none' }}>
          <div className="tag">
            <span>{ev.name} · {Math.round(ev.soc * 100)}%</span>
            <i style={{ width: `${ev.soc * 100}%` }} />
          </div>
        </Html>
      )}
    </group>
  )
}

function numberTexture(n) {
  const c = document.createElement('canvas')
  c.width = c.height = 128
  const g = c.getContext('2d')
  g.fillStyle = '#060606'
  g.fillRect(0, 0, 128, 128)
  g.fillStyle = GREEN
  g.font = '600 64px "IBM Plex Mono", ui-monospace, monospace'
  g.textAlign = 'center'
  g.textBaseline = 'middle'
  g.fillText(String(n).padStart(2, '0'), 64, 68)
  const t = new THREE.CanvasTexture(c)
  t.colorSpace = THREE.SRGBColorSpace
  return t
}

function Charger({ i, bx, ev, overload }) {
  const tex = useMemo(() => numberTexture(i + 1), [i])
  const active = ev && ev.phase === 'charging' && ev.kw > 0.1
  const plugged = ev && (ev.phase === 'charging' || ev.phase === 'done')
  const led = overload ? RED : active ? GREEN : '#555'
  const curve = useMemo(
    () => new THREE.QuadraticBezierCurve3(v(bx + 0.5, 0.9, CHG_Z), v(bx + 1.0, 0.1, -1.3), v(bx, 0.75, -0.85)),
    [bx],
  )
  return (
    <group>
      <RoundedBox args={[0.85, 2.3, 0.5]} radius={0.14} smoothness={4} position={[bx, 1.15, CHG_Z]} castShadow>
        <meshPhysicalMaterial color="#f4f4f4" roughness={0.35} clearcoat={0.6} />
      </RoundedBox>
      <mesh position={[bx, 1.75, CHG_Z + 0.256]}>
        <planeGeometry args={[0.5, 0.5]} />
        <meshBasicMaterial map={tex} />
      </mesh>
      <mesh position={[bx, 1.35, CHG_Z + 0.256]}>
        <boxGeometry args={[0.5, 0.05, 0.02]} />
        <meshBasicMaterial color={led} />
      </mesh>
      <mesh position={[bx + 0.46, 1.15, CHG_Z]} rotation={[0, Math.PI / 2, 0]}>
        <torusGeometry args={[0.2, 0.04, 8, 24, Math.PI * 1.6]} />
        <meshStandardMaterial color="#111" roughness={0.7} />
      </mesh>
      {plugged && (
        <mesh>
          <tubeGeometry args={[curve, 24, 0.045, 8, false]} />
          <meshStandardMaterial color="#0c0c0c" roughness={0.8} />
        </mesh>
      )}
      <Flow getPoint={(t) => curve.getPoint(t)} active={!!active} count={5} size={0.08} speed={0.5 + (ev ? ev.kw / 80 : 0)} color="#c9ffe6" />
    </group>
  )
}

// dashed green feed lines from the budget sign to each charger
function Feeds({ s }) {
  const lines = useMemo(
    () => BAY_X.map((bx) => [v(0, BUDGET_Y - 0.9, CHG_Z), v(bx, BUDGET_Y - 0.9, CHG_Z), v(bx, 2.3, CHG_Z)]),
    [],
  )
  const refs = useRef([])
  useFrame((_, dt) => {
    refs.current.forEach((l) => {
      if (l && l.material) l.material.dashOffset -= dt * 0.6
    })
  })
  const col = s.overload ? RED : GREEN
  return (
    <group>
      {lines.map((pts, i) => {
        const ev = s.bays[i]
        const kw = ev && ev.phase === 'charging' ? ev.kw : 0
        return (
          <group key={i}>
            <Line ref={(el) => (refs.current[i] = el)} points={pts} color={col} lineWidth={1.4} dashed dashSize={0.25} gapSize={0.18} transparent opacity={0.8} />
            <Flow getPoint={(t) => samplePoly(pts, t).pos} active={kw > 0.1} count={4} size={0.09} speed={0.25 + kw / 120} color={col} />
          </group>
        )
      })}
      <Html position={[0, BUDGET_Y, CHG_Z]} center style={{ pointerEvents: 'none' }}>
        <div className={`budget ${s.overload ? 'over' : ''}`}>
          <small>SITE POWER BUDGET</small>
          <b>{Math.round(s.capacity)} kW</b>
          <small>{s.overload ? 'OVERLOAD' : `${Math.round(s.total_kw)} kW IN USE`}</small>
        </div>
      </Html>
    </group>
  )
}

const slotPts = (bx) => {
  const w = 1.7, z0 = PARK_Z - 2.8, z1 = PARK_Z + 2.8
  return [[bx - w, 0.03, z0], [bx + w, 0.03, z0], [bx + w, 0.03, z1], [bx - w, 0.03, z1], [bx - w, 0.03, z0]]
}

function Ground() {
  return (
    <group>
      <mesh rotation={[-Math.PI / 2, 0, 0]} receiveShadow>
        <planeGeometry args={[120, 120]} />
        <meshStandardMaterial color="#080808" roughness={1} />
      </mesh>
      {BAY_X.map((bx) => (
        <group key={bx}>
          <mesh position={[bx, 0.01, PARK_Z]} rotation={[-Math.PI / 2, 0, 0]}>
            <planeGeometry args={[3.4, 5.6]} />
            <meshBasicMaterial color="#1b1b1b" />
          </mesh>
          <Line points={slotPts(bx)} color="#b0b0b0" lineWidth={1.2} transparent opacity={0.6} />
        </group>
      ))}
      <Line points={[[-20, 0.02, ROAD_Z + 1.6], [20, 0.02, ROAD_Z + 1.6]]} color="#2a2a2a" lineWidth={1} dashed dashSize={0.6} gapSize={0.5} />
    </group>
  )
}

function AutoZoom() {
  const { camera, size } = useThree()
  useEffect(() => {
    camera.zoom = Math.min(size.width / 26, size.height / 17)
    camera.updateProjectionMatrix()
  }, [size, camera])
  return null
}

export default function Scene({ state }) {
  return (
    <Canvas shadows dpr={[1, 2]} orthographic camera={{ position: [-16, 13, 16], zoom: 40, near: -60, far: 200 }}>
      <color attach="background" args={['#050505']} />
      <AutoZoom />
      <hemisphereLight args={['#ffffff', '#3a3a3a', 1.9]} />
      <directionalLight position={[-10, 8, 14]} intensity={1.0} />
      <directionalLight
        position={[-8, 16, 10]} intensity={2.0} castShadow shadow-mapSize={[1024, 1024]}
        shadow-camera-left={-22} shadow-camera-right={22} shadow-camera-top={16} shadow-camera-bottom={-16}
      />
      <Ground />
      {state && (
        <>
          <Feeds s={state} />
          {BAY_X.map((bx, i) => (
            <Charger key={bx} i={i} bx={bx} ev={state.bays[i]} overload={state.overload} />
          ))}
          {state.bays.map((ev) => ev && <Vehicle key={ev.id} ev={ev} bx={BAY_X[ev.bay]} />)}
        </>
      )}
      <OrbitControls target={[0, 1.5, -3.5]} enablePan={false} maxPolarAngle={Math.PI / 2.1} minZoom={15} maxZoom={120} />
    </Canvas>
  )
}
