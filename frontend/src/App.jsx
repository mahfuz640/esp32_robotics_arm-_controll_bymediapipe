import { useCallback, useEffect, useRef, useState } from 'react'
import { FilesetResolver, HandLandmarker } from '@mediapipe/tasks-vision'

const MODEL = 'https://storage.googleapis.com/mediapipe-models/hand_landmarker/hand_landmarker/float16/1/hand_landmarker.task'
const WASM = 'https://cdn.jsdelivr.net/npm/@mediapipe/tasks-vision@0.10.22-rc.20250304/wasm'
const API_BASE = (import.meta.env.VITE_API_URL || '').replace(/\/$/, '')
const apiUrl = path => `${API_BASE}${path}`
const CLOUD_MQTT_MODE = Boolean(API_BASE)
const LINKS = [[0,1],[1,2],[2,3],[3,4],[0,5],[5,6],[6,7],[7,8],[5,9],[9,10],[10,11],[11,12],[9,13],[13,14],[14,15],[15,16],[13,17],[17,18],[18,19],[19,20],[0,17]]

function fingerStates(p, hand) {
  const states = [hand === 'Right' ? Number(p[4].x < p[3].x) : Number(p[4].x > p[3].x)]
  for (const [tip, pip] of [[8,6],[12,10],[16,14],[20,18]]) states.push(Number(p[tip].y < p[pip].y))
  return states
}

function paint(canvas, source, points) {
  const width = source.videoWidth || source.naturalWidth, height = source.videoHeight || source.naturalHeight
  if (!width || !height) return
  canvas.width = width; canvas.height = height
  const ctx = canvas.getContext('2d'); ctx.save(); ctx.translate(width, 0); ctx.scale(-1, 1); ctx.drawImage(source, 0, 0)
  if (points) {
    ctx.strokeStyle = '#40e6b1'; ctx.lineWidth = 4
    for (const [a,b] of LINKS) { ctx.beginPath(); ctx.moveTo(points[a].x*width, points[a].y*height); ctx.lineTo(points[b].x*width, points[b].y*height); ctx.stroke() }
    ctx.fillStyle = '#ffca5c'
    for (const p of points) { ctx.beginPath(); ctx.arc(p.x*width,p.y*height,5,0,Math.PI*2); ctx.fill() }
  }
  ctx.restore()
}

const Status = ({online, children}) => <span className={`status ${online?'online':''}`}><i />{children}</span>

export default function App() {
  const video = useRef(null), canvas = useRef(null), detector = useRef(null), job = useRef(0), active = useRef(false)
  const lastFinger = useRef(-1), lastSent = useRef(0)
  const [running,setRunning] = useState(false), [loading,setLoading] = useState(false), [error,setError] = useState('')
  const [mode,setMode] = useState('local'), [fingers,setFingers] = useState(0), [hasHand,setHasHand] = useState(false)
  const [controllerOnline,setControllerOnline] = useState(false), [monitorOnline,setMonitorOnline] = useState(false), [monitorTick,setMonitorTick] = useState(0), [monitorFrameError,setMonitorFrameError] = useState(false), [settings,setSettings] = useState(false)
  const [angles,setAngles] = useState({base:90,shoulder:90,elbow:90}), [manualBusy,setManualBusy] = useState(false), [manualMessage,setManualMessage] = useState('')
  const [mobileUrl,setMobileUrl] = useState(()=>localStorage.getItem('mobileUrl')||'http://192.168.0.20:8080/video')
  const [controllerUrl,setControllerUrl] = useState(()=>localStorage.getItem('controllerUrl')||'http://192.168.0.40')

  const send = useCallback(async (pattern, dir) => {
    try {
      const q = new URLSearchParams({controller:controllerUrl,pattern:pattern.join(','),dir})
      const response = await fetch(apiUrl(`/api/control?${q}`),{signal:AbortSignal.timeout(1800)})
      setControllerOnline(response.ok)
    } catch { setControllerOnline(false) }
  },[controllerUrl])

  const processResult = useCallback((source,result) => {
    const points=result.landmarks?.[0]; paint(canvas.current,source,points); setHasHand(Boolean(points))
    if (!points) return
    const pattern=fingerStates(points,result.handednesses?.[0]?.[0]?.categoryName||'Right'); const value=pattern.reduce((a,b)=>a+b,0); setFingers(value)
    const dir=points[0].x<0.4?'left':points[0].x>0.6?'right':'center'; const gestureKey=`${pattern.join(',')}:${dir}`
    const now=performance.now()
    if(gestureKey!==lastFinger.current && now-lastSent.current>180){lastFinger.current=gestureKey;lastSent.current=now;send(pattern,dir)}
  },[send])

  async function loadDetector(runningMode){
    const vision=await FilesetResolver.forVisionTasks(WASM); detector.current?.close()
    detector.current=await HandLandmarker.createFromOptions(vision,{baseOptions:{modelAssetPath:MODEL,delegate:'GPU'},runningMode,numHands:1,minHandDetectionConfidence:.6,minTrackingConfidence:.6})
  }

  async function startIp(){
    setLoading(true);setError('')
    try{
      await loadDetector('IMAGE');active.current=true;setRunning(true)
      const loop=async()=>{
        if(!active.current)return
        try{
          const image=new Image();image.crossOrigin='anonymous';image.src=apiUrl(`/api/mobile-frame?url=${encodeURIComponent(mobileUrl)}&t=${Date.now()}`);await image.decode()
          processResult(image,detector.current.detect(image));job.current=setTimeout(loop,80)
        }catch{active.current=false;setRunning(false);setError('Mobile camera পাওয়া যায়নি। IP Webcam app-এর Start server চাপুন এবং URL পরীক্ষা করুন।')}
      };loop()
    }catch(e){setError(e.message||'MediaPipe load হয়নি');setRunning(false)}finally{setLoading(false)}
  }

  async function startLocal(){
    setLoading(true);setError('')
    try{
      if(!window.isSecureContext)throw new Error('এই device camera-এর জন্য HTTPS অথবা localhost প্রয়োজন।')
      await loadDetector('VIDEO');const stream=await navigator.mediaDevices.getUserMedia({video:{facingMode:'user'},audio:false})
      video.current.srcObject=stream;await video.current.play();active.current=true;setRunning(true)
      const loop=()=>{if(!active.current)return;processResult(video.current,detector.current.detectForVideo(video.current,performance.now()));job.current=requestAnimationFrame(loop)};loop()
    }catch(e){setError(e.message||'Camera চালু হয়নি');setRunning(false)}finally{setLoading(false)}
  }

  function stop(){active.current=false;cancelAnimationFrame(job.current);clearTimeout(job.current);video.current?.srcObject?.getTracks().forEach(t=>t.stop());setRunning(false);setHasHand(false)}
  function save(e){e.preventDefault();localStorage.setItem('mobileUrl',mobileUrl);localStorage.setItem('controllerUrl',controllerUrl);setSettings(false)}
  async function moveServos(){
    setManualBusy(true);setManualMessage('Sending...')
    try{
      const q=new URLSearchParams({controller:controllerUrl,...Object.fromEntries(Object.entries(angles).map(([k,v])=>[k,String(v)]))})
      const response=await fetch(apiUrl(`/api/servo?${q}`),{signal:AbortSignal.timeout(9000)})
      if(!response.ok)throw new Error('Servo command failed')
      setControllerOnline(true);setManualMessage('Movement complete')
    }catch{setControllerOnline(false);setManualMessage('ESP8266 not responding')}
    finally{setManualBusy(false)}
  }
  useEffect(()=>{
    let cancelled=false
    const check=async()=>{
      try{
        const q=new URLSearchParams({controller:controllerUrl})
        const response=await fetch(apiUrl(`/api/device-status?${q}`),{signal:AbortSignal.timeout(2200),cache:'no-store'})
        const result=await response.json()
        if(!cancelled)setControllerOnline(Boolean(result.online))
      }catch{if(!cancelled)setControllerOnline(false)}
    }
    check();const timer=setInterval(check,3000)
    return()=>{cancelled=true;clearInterval(timer)}
  },[controllerUrl])
  useEffect(()=>{
    let cancelled=false
    const check=async()=>{
      try{const response=await fetch(apiUrl('/api/camera-status'),{cache:'no-store'});const result=await response.json();if(!cancelled){setMonitorOnline(Boolean(result.online));if(result.online)setMonitorTick(Date.now())}}
      catch{if(!cancelled)setMonitorOnline(false)}
    }
    check();const timer=setInterval(check,1000)
    return()=>{cancelled=true;clearInterval(timer)}
  },[])
  useEffect(()=>()=>{active.current=false;cancelAnimationFrame(job.current);clearTimeout(job.current);video.current?.srcObject?.getTracks().forEach(t=>t.stop());detector.current?.close()},[])

  return <div className="app">
    <header><div><p className="eyebrow">ESP ROBOTICS LAB</p><h1>Vision Control Center</h1><p className="subtitle">Mobile IP camera, MediaPipe এবং ESP32 monitoring</p></div><div className="header-actions"><div className="gesture"><small>DETECTED</small><strong>{hasHand?`${fingers} FINGER${fingers===1?'':'S'}`:'NO HAND'}</strong></div><button className="icon-button" onClick={()=>setSettings(v=>!v)}>⚙ Settings</button></div></header>
    {settings&&<form className="settings" onSubmit={save}><label>Mobile IP Camera<input value={mobileUrl} onChange={e=>setMobileUrl(e.target.value)} placeholder="http://PHONE_IP:8080/video"/></label>{CLOUD_MQTT_MODE?<label>ESP8266 connection<input value="Automatic · MQTT Cloud" disabled/></label>:<label>ESP8266 URL<input value={controllerUrl} onChange={e=>setControllerUrl(e.target.value)}/></label>}<label>ESP32-CAM connection<input value="Automatic · MQTT Cloud" disabled/></label><button>Save</button></form>}
    {error&&<div className="alert">{error}</div>}
    <main>
      <section className="camera-card"><div className="card-head"><div><span className="number">01</span><h2>Device Camera</h2><p>এই device-এর camera দিয়ে finger detection</p></div><Status online={running}>{running?'LIVE':'OFFLINE'}</Status></div><div className="viewport"><video ref={video} playsInline muted/><canvas ref={canvas}/>{!running&&<div className="empty"><span>✋</span><p>Camera বন্ধ আছে</p></div>}</div><div className="controls source-controls"><select value={mode} disabled={running} onChange={e=>setMode(e.target.value)}><option value="local">This device camera</option><option value="ip">Mobile IP Camera</option></select>{running?<button className="danger" onClick={stop}>Stop camera</button>:<button onClick={mode==='ip'?startIp:startLocal} disabled={loading}>{loading?'MediaPipe loading…':'Start device camera'}</button>}</div></section>
      <section className="camera-card"><div className="card-head"><div><span className="number">02</span><h2>ESP32-CAM</h2><p>MQTT cloud monitoring</p></div><Status online={monitorOnline&&!monitorFrameError}>{monitorOnline&&!monitorFrameError?'LIVE':monitorOnline?'WAITING':'OFFLINE'}</Status></div><div className="viewport">{monitorOnline?<img className="monitor" src={apiUrl(`/api/camera-frame?t=${monitorTick}`)} alt="ESP32-CAM" onLoad={()=>setMonitorFrameError(false)} onError={()=>setMonitorFrameError(true)}/>:<div className="empty"><span>📷</span><p>ESP32-CAM offline</p></div>}</div><div className="controls meta"><span>ESP8266</span><Status online={controllerOnline}>{controllerOnline?'CONNECTED':'NOT CONNECTED'}</Status></div></section>
    </main>
    <section className="manual-panel"><div className="manual-heading"><div><p className="eyebrow">MANUAL MODE</p><h2>Servo position control</h2></div><Status online={controllerOnline}>{controllerOnline?'ESP8266 CONNECTED':'ESP8266 OFFLINE'}</Status></div><div className="sliders">{[['base','Base'],['shoulder','Shoulder'],['elbow','Elbow']].map(([key,label])=><label className="slider" key={key}><span><b>{label}</b><output>{angles[key]}°</output></span><input type="range" min="0" max="180" value={angles[key]} onChange={e=>setAngles(current=>({...current,[key]:Number(e.target.value)}))}/><small>0° <i/> 90° <i/> 180°</small></label>)}</div><div className="manual-actions"><span className={manualMessage.includes('complete')?'success':''}>{manualMessage}</span><button onClick={moveServos} disabled={manualBusy||!controllerOnline}>{manualBusy?'Moving…':'Move servos'}</button></div></section>
    <footer><span>0: close</span><span>1: left</span><span>2: center</span><span>3: right</span><span>4: down</span><span>5: open/up</span></footer>
  </div>
}
