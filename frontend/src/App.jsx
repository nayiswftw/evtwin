import Scene from './Scene.jsx'
import Dashboard from './Dashboard.jsx'
import { useTwin } from './useTwin.js'

export default function App() {
  const { state, connected, history, send } = useTwin()
  return (
    <>
      <div className="stage">
        <Scene state={state} />
      </div>
      <Dashboard state={state} connected={connected} history={history} send={send} />
    </>
  )
}
