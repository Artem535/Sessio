import { useEffect, useRef, useState } from 'react';
import { validateDisplayName } from './display-name';
import { text } from './copy';
export interface DeviceSettings { name:string; microphoneId:string; cameraId:string; microphoneEnabled:boolean; cameraEnabled:boolean }
export function Prejoin({onJoin,busy}:{onJoin:(settings:DeviceSettings)=>void;busy:boolean}) {
  const [name,setName] = useState(''), [mic,setMic] = useState(false), [camera,setCamera] = useState(false);
  const [devices,setDevices] = useState<MediaDeviceInfo[]>([]), [microphoneId,setMicrophoneId] = useState(''), [cameraId,setCameraId] = useState('');
  const [notice,setNotice] = useState(''), [level,setLevel] = useState(0);
  const video = useRef<HTMLVideoElement>(null), stream = useRef<MediaStream|null>(null), generation = useRef(0);
  const audio = useRef<AudioContext|null>(null), frame = useRef(0);
  // Null allows the first successful device check to enable preview. Explicit
  // checkbox intent survives replacement and asynchronous permission results.
  const microphoneIntent=useRef<boolean|null>(null), cameraIntent=useRef<boolean|null>(null);
  const applyIntent=(media:MediaStream)=>{
    media.getAudioTracks().forEach(track=>track.enabled=microphoneIntent.current??true);
    media.getVideoTracks().forEach(track=>track.enabled=cameraIntent.current??true);
  };
  const stop = () => { ++generation.current; stream.current?.getTracks().forEach(track => track.stop()); stream.current = null;
    cancelAnimationFrame(frame.current); void audio.current?.close(); audio.current = null; setLevel(0); };
  useEffect(() => () => { ++generation.current; stream.current?.getTracks().forEach(track => track.stop()); cancelAnimationFrame(frame.current); void audio.current?.close(); }, []);
  const preview = async (micId= microphoneId, camId=cameraId) => {
    stop(); const epoch = generation.current;
    try {
      const media = await navigator.mediaDevices.getUserMedia({audio:micId ? {deviceId:{exact:micId}} : true,video:camId ? {deviceId:{exact:camId}} : true});
      if (generation.current !== epoch) { media.getTracks().forEach(track=>track.stop()); return; }
      applyIntent(media);
      stream.current = media; if (video.current) video.current.srcObject = media;
      const available=await navigator.mediaDevices.enumerateDevices();
      if(generation.current!==epoch){media.getTracks().forEach(track=>track.stop());return;}
      applyIntent(media);
      setDevices(available); setMic(microphoneIntent.current??true); setCamera(cameraIntent.current??true); setNotice('');
      const ctx = new AudioContext(), meter = ctx.createAnalyser(); audio.current = ctx;
      ctx.createMediaStreamSource(media).connect(meter); const samples = new Float32Array(meter.fftSize);
      const tick = () => { if (generation.current !== epoch) return; meter.getFloatTimeDomainData(samples);
        setLevel(Math.min(1,Math.sqrt(samples.reduce((sum,x)=>sum+x*x,0)/samples.length)*4)); frame.current=requestAnimationFrame(tick); }; tick();
    } catch { if (generation.current === epoch) { stop(); setMic(false); setCamera(false); setNotice(text.denied); } }
  };
  let valid = false; try { validateDisplayName(name); valid = true; } catch { /* form validity */ }
  return <section className="prejoin"><h1>{text.title}</h1><video ref={video} autoPlay muted playsInline aria-label={text.preview}/>
    <label>{text.name}<input maxLength={160} value={name} onChange={event=>setName(event.target.value)} autoComplete="off" disabled={busy}/></label>
    <button disabled={busy} onClick={()=>void preview()}>{text.check}</button><meter min={0} max={1} value={mic ? level : 0} aria-label={text.mic}/>
    <label>{text.mic}<select aria-label={`${text.mic} device`} disabled={busy} value={microphoneId} onChange={event=>{setMicrophoneId(event.target.value); void preview(event.target.value,cameraId);}}><option value="">Default</option>{devices.filter(d=>d.kind==='audioinput').map(d=><option key={d.deviceId} value={d.deviceId}>{d.label}</option>)}</select></label>
    <label>{text.camera}<select aria-label={`${text.camera} device`} disabled={busy} value={cameraId} onChange={event=>{setCameraId(event.target.value); void preview(microphoneId,event.target.value);}}><option value="">Default</option>{devices.filter(d=>d.kind==='videoinput').map(d=><option key={d.deviceId} value={d.deviceId}>{d.label}</option>)}</select></label>
    <label className="check"><input type="checkbox" checked={mic} onChange={event=>{microphoneIntent.current=event.target.checked;setMic(event.target.checked); stream.current?.getAudioTracks().forEach(t=>t.enabled=event.target.checked);}}/>{text.mic}</label>
    <label className="check"><input type="checkbox" checked={camera} onChange={event=>{cameraIntent.current=event.target.checked;setCamera(event.target.checked); stream.current?.getVideoTracks().forEach(t=>t.enabled=event.target.checked);}}/>{text.camera}</label>
    {notice && <p role="status">{notice}</p>}<button className="primary" disabled={!valid || busy} onClick={()=>{stop(); onJoin({name:validateDisplayName(name),microphoneId,cameraId,microphoneEnabled:mic,cameraEnabled:camera});}}>{busy ? text.connecting : text.join}</button>
  </section>;
}
