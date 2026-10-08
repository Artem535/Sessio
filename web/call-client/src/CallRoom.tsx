import { useEffect, useState } from 'react';
import { Room, RoomEvent } from 'livekit-client';
import { ParticipantTile } from './ParticipantTile';
import { ScreenTile } from './ScreenTile';
import { text } from './copy';
export function CallRoom({room,onLeave}:{room:Room;onLeave:()=>void}) {
  const [,render] = useState(0), [notice,setNotice] = useState('');
  const [reconnecting,setReconnecting] = useState(false), [devices,setDevices] = useState<MediaDeviceInfo[]>([]);
  useEffect(()=>{ const refresh=()=>render(n=>n+1); const reconnect=()=>setReconnecting(true), connected=()=>setReconnecting(false);
    const events=[RoomEvent.ParticipantConnected,RoomEvent.ParticipantDisconnected,RoomEvent.TrackSubscribed,RoomEvent.TrackUnsubscribed,RoomEvent.TrackMuted,RoomEvent.TrackPublished,RoomEvent.TrackUnpublished,RoomEvent.TrackUnmuted,RoomEvent.LocalTrackPublished,RoomEvent.LocalTrackUnpublished,RoomEvent.ParticipantNameChanged];
    events.forEach(event=>room.on(event,refresh)); room.on(RoomEvent.Reconnecting,reconnect); room.on(RoomEvent.SignalReconnecting,reconnect); room.on(RoomEvent.Reconnected,connected); room.on(RoomEvent.Disconnected,onLeave);
    void navigator.mediaDevices.enumerateDevices().then(setDevices).catch(()=>{});
    return()=>{events.forEach(event=>room.off(event,refresh));room.off(RoomEvent.Reconnecting,reconnect);room.off(RoomEvent.SignalReconnecting,reconnect);room.off(RoomEvent.Reconnected,connected);room.off(RoomEvent.Disconnected,onLeave);};
  },[room,onLeave]);
  const update=async(action:()=>Promise<unknown>)=>{try{await action();render(n=>n+1);}catch{setNotice(text.media);}};
  const canShare=typeof navigator.mediaDevices?.getDisplayMedia==='function', sharing=room.localParticipant.isScreenShareEnabled;
  // Cancelling the browser's chooser is a user choice, not a device failure.
  const toggleShare=async()=>{try{await room.localParticipant.setScreenShareEnabled(!sharing);render(n=>n+1);}catch(error){if((error as Error)?.name!=='NotAllowedError')setNotice(text.media);}};
  const everyone=[room.localParticipant,...room.remoteParticipants.values()];
  return <section><h1>Sessio</h1>{reconnecting && <p role="status">{text.reconnecting}</p>}<div className="participants">{everyone.map(p=><ScreenTile key={p.identity+'#screen'} participant={p}/>)}{everyone.map(p=><ParticipantTile key={p.identity} participant={p}/>)}</div>
    <nav><button aria-pressed={room.localParticipant.isMicrophoneEnabled} onClick={()=>void update(()=>room.localParticipant.setMicrophoneEnabled(!room.localParticipant.isMicrophoneEnabled))}>{text.mic}</button>
    <button aria-pressed={room.localParticipant.isCameraEnabled} onClick={()=>void update(()=>room.localParticipant.setCameraEnabled(!room.localParticipant.isCameraEnabled))}>{text.camera}</button>
    {canShare && <button aria-pressed={sharing} onClick={()=>void toggleShare()}>{sharing?text.stopShare:text.share}</button>}<button onClick={()=>void update(()=>room.startAudio())}>{text.sound}</button><button onClick={onLeave}>{text.leave}</button></nav>
    {(['audioinput','videoinput'] as const).map(kind=><label key={kind}>{kind==='audioinput'?text.mic:text.camera}<select defaultValue="" onChange={e=>void update(()=>room.switchActiveDevice(kind,e.target.value))}><option value="">Default</option>{devices.filter(d=>d.kind===kind).map(d=><option key={d.deviceId} value={d.deviceId}>{d.label}</option>)}</select></label>)}
    {notice && <p role="status">{notice}</p>}</section>;
}
