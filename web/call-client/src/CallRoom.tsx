import { useEffect, useState, type CSSProperties } from 'react';
import { Room, RoomEvent, type Participant } from 'livekit-client';
import { ParticipantTile } from './ParticipantTile';
import { ScreenTile, screenTrackOf } from './ScreenTile';
import { NativeLink } from './NativeLink';
import { CameraIcon, DevicesIcon, MicIcon, ScreenIcon, SpeakerIcon } from './Icons';
import { text } from './copy';

// Same column count the desktop stage uses for a camera grid: as square as
// the room allows, one column on a phone held upright.
export function gridColumns(count:number, width:number) {
  if (width < 640) return count > 4 ? 2 : 1;
  return Math.max(1, Math.ceil(Math.sqrt(count)));
}

function useWidth() {
  const [width,setWidth] = useState(()=>window.innerWidth);
  useEffect(()=>{const update=()=>setWidth(window.innerWidth);window.addEventListener('resize',update);return()=>window.removeEventListener('resize',update);},[]);
  return width;
}

type DeviceKind = 'audioinput'|'videoinput'|'audiooutput';
const canPickSpeaker = typeof HTMLMediaElement !== 'undefined' && 'setSinkId' in HTMLMediaElement.prototype;

export function CallRoom({room,onLeave,nativeLink}:{room:Room;onLeave:()=>void;nativeLink?:string}) {
  const [,render] = useState(0), [notice,setNotice] = useState('');
  const [reconnecting,setReconnecting] = useState(false), [devices,setDevices] = useState<MediaDeviceInfo[]>([]);
  const [devicesOpen,setDevicesOpen] = useState(false), [featured,setFeatured] = useState<string|null>(null);
  const width = useWidth();
  useEffect(()=>{ const refresh=()=>render(n=>n+1); const reconnect=()=>setReconnecting(true), connected=()=>setReconnecting(false);
    const events=[RoomEvent.ParticipantConnected,RoomEvent.ParticipantDisconnected,RoomEvent.TrackSubscribed,RoomEvent.TrackUnsubscribed,RoomEvent.TrackMuted,RoomEvent.TrackPublished,RoomEvent.TrackUnpublished,RoomEvent.TrackUnmuted,RoomEvent.LocalTrackPublished,RoomEvent.LocalTrackUnpublished,RoomEvent.ParticipantNameChanged,RoomEvent.ActiveSpeakersChanged,RoomEvent.AudioPlaybackStatusChanged,RoomEvent.ActiveDeviceChanged];
    events.forEach(event=>room.on(event,refresh)); room.on(RoomEvent.Reconnecting,reconnect); room.on(RoomEvent.SignalReconnecting,reconnect); room.on(RoomEvent.Reconnected,connected); room.on(RoomEvent.Disconnected,onLeave);
    void navigator.mediaDevices.enumerateDevices().then(setDevices).catch(()=>{});
    return()=>{events.forEach(event=>room.off(event,refresh));room.off(RoomEvent.Reconnecting,reconnect);room.off(RoomEvent.SignalReconnecting,reconnect);room.off(RoomEvent.Reconnected,connected);room.off(RoomEvent.Disconnected,onLeave);};
  },[room,onLeave]);
  const update=async(action:()=>Promise<unknown>)=>{try{await action();render(n=>n+1);}catch{setNotice(text.media);}};
  const local = room.localParticipant;
  const canShare=typeof navigator.mediaDevices?.getDisplayMedia==='function', sharing=local.isScreenShareEnabled;
  // Cancelling the browser's chooser is a user choice, not a device failure.
  const toggleShare=async()=>{try{await local.setScreenShareEnabled(!sharing);render(n=>n+1);}catch(error){if((error as Error)?.name!=='NotAllowedError')setNotice(text.media);}};

  const everyone:Participant[]=[local,...room.remoteParticipants.values()];
  const sharers=everyone.filter(p=>screenTrackOf(p));
  const stageScreen=sharers.find(p=>p.identity===featured) ?? sharers[0];
  const columns=gridColumns(everyone.length,width);
  const grid={'--cols':columns,'--rows':Math.ceil(everyone.length/columns)} as CSSProperties;
  const alone=room.remoteParticipants.size===0;
  const kinds:DeviceKind[]=canPickSpeaker?['audioinput','videoinput','audiooutput']:['audioinput','videoinput'];
  const kindLabel=(kind:DeviceKind)=>kind==='audioinput'?text.mic:kind==='videoinput'?text.camera:text.speaker;

  return <section className="call">
    <header className="call-top"><span className="brand">Sessio</span><span className="count">{everyone.length}</span>
      {nativeLink && <NativeLink href={nativeLink} compact/>}</header>
    <div className={'stage'+(stageScreen?' with-screen':'')}>
      {stageScreen ? <>
        <div className="featured"><ScreenTile key={stageScreen.identity+'#screen'} participant={stageScreen}/></div>
        <div className="strip">{sharers.filter(p=>p!==stageScreen).map(p=><ScreenTile key={p.identity+'#screen'} participant={p} onSelect={()=>setFeatured(p.identity)}/>)}
          {everyone.map(p=><ParticipantTile key={p.identity} participant={p}/>)}</div>
      </> : <div className="grid" style={grid}>{everyone.map(p=><ParticipantTile key={p.identity} participant={p}/>)}</div>}
      {alone && !stageScreen && <p className="pill waiting" role="status"><span className="dot"/>{text.waiting}</p>}
      {reconnecting && <p className="pill" role="status">{text.reconnecting}</p>}
      {notice && <p className="pill notice" role="status">{notice}</p>}
    </div>
    <nav className="controls">
      <button className={'round'+(local.isMicrophoneEnabled?'':' off')} aria-label={text.mic} title={text.mic} aria-pressed={local.isMicrophoneEnabled} onClick={()=>void update(()=>local.setMicrophoneEnabled(!local.isMicrophoneEnabled))}><MicIcon off={!local.isMicrophoneEnabled}/></button>
      <button className={'round'+(local.isCameraEnabled?'':' off')} aria-label={text.camera} title={text.camera} aria-pressed={local.isCameraEnabled} onClick={()=>void update(()=>local.setCameraEnabled(!local.isCameraEnabled))}><CameraIcon off={!local.isCameraEnabled}/></button>
      {canShare && <button className={'round'+(sharing?' active':'')} aria-label={sharing?text.stopShare:text.share} title={sharing?text.stopShare:text.share} aria-pressed={sharing} onClick={()=>void toggleShare()}><ScreenIcon/></button>}
      <button className={'round'+(devicesOpen?' active':'')} aria-label={text.devices} title={text.devices} aria-expanded={devicesOpen} onClick={()=>setDevicesOpen(open=>!open)}><DevicesIcon/></button>
      {room.canPlaybackAudio===false && <button className="round active" aria-label={text.sound} title={text.sound} onClick={()=>void update(()=>room.startAudio())}><SpeakerIcon/></button>}
      <button className="leave" onClick={onLeave}>{text.leave}</button>
    </nav>
    {devicesOpen && <div className="devices" role="dialog" aria-label={text.devices}>
      {kinds.map(kind=><label key={kind}>{kindLabel(kind)}<select value={room.getActiveDevice?.(kind) ?? ''} onChange={e=>void update(()=>room.switchActiveDevice(kind,e.target.value))}>
        <option value="">{text.defaultDevice}</option>{devices.filter(d=>d.kind===kind).map(d=><option key={d.deviceId} value={d.deviceId}>{d.label || kindLabel(kind)}</option>)}</select></label>)}
    </div>}
  </section>;
}
