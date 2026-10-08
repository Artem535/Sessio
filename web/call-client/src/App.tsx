import { useCallback, useEffect, useRef, useState } from 'react';
import { Room, RoomEvent } from 'livekit-client';
import { type Invitation } from './invitation';
import { joinGuest } from './join-api';
import { Prejoin, type DeviceSettings } from './Prejoin';
import { CallRoom } from './CallRoom';
import { text } from './copy';
export function App({invitation}:{invitation:Invitation|null}) {
  const [room,setRoom] = useState<Room|null>(null), [busy,setBusy] = useState(false), [ended,setEnded] = useState(false), [error,setError] = useState('');
  const pending = useRef<AbortController|null>(null), active = useRef<Room|null>(null), generation = useRef(0), invite = useRef(invitation);
  const removeListener = useRef<(()=>void)|null>(null);
  const release=()=>{removeListener.current?.();removeListener.current=null;const current=active.current;active.current=null;void current?.disconnect();};
  const leave=useCallback(()=>{++generation.current;pending.current?.abort();pending.current=null;release();invite.current=null;setRoom(null);setBusy(false);setEnded(true);},[]);
  useEffect(()=>()=>{++generation.current;pending.current?.abort();release();},[]);
  const join=async(settings:DeviceSettings)=>{
    if(pending.current || !invite.current)return;const controller=new AbortController();pending.current=controller;const epoch=generation.current;
    controller.signal.addEventListener('abort',()=>{if(epoch===generation.current){++generation.current;release();pending.current=null;setBusy(false);setError(text.error);}}, {once:true});
    const timer=setTimeout(()=>controller.abort(),15000);setBusy(true);setError('');let current:Room|null=null;
    try{const token=await joinGuest(invite.current,settings.name,controller.signal);if(epoch!==generation.current || controller.signal.aborted)return;
      current=new Room();active.current=current;
      const joinedRoom=current;
      let connected=false, disconnected=false;
      const onDisconnected=()=>{disconnected=true;if(active.current===joinedRoom && connected)leave();};
      current.on(RoomEvent.Disconnected,onDisconnected);
      removeListener.current=()=>joinedRoom.off(RoomEvent.Disconnected,onDisconnected);
      await current.connect(token.endpointUrl,token.token);
      connected=true;
      if(disconnected)throw new Error('connection_closed');
      if(epoch!==generation.current){await current.disconnect();return;}
      try{await current.localParticipant.setMicrophoneEnabled(settings.microphoneEnabled,{deviceId:settings.microphoneId||undefined});await current.localParticipant.setCameraEnabled(settings.cameraEnabled,{deviceId:settings.cameraId||undefined});}catch{setError(text.media);}
      if(epoch!==generation.current){await current.disconnect();return;}setRoom(current);
    }catch{if(epoch===generation.current){release();setError(text.error);}}
    finally{clearTimeout(timer);if(pending.current===controller){pending.current=null;setBusy(false);}}
  };
  if(ended)return <main><h1>{text.ended}</h1></main>;
  if(!invitation)return <main><h1>{text.invalid}</h1></main>;
  const nativeLink = `sessio://join?${new URLSearchParams({code:invitation.code,passcode:invitation.passcode,backend:location.origin})}`;
  return <main>{room ? <CallRoom room={room} onLeave={leave}/> : <><Prejoin busy={busy} onJoin={settings=>void join(settings)}/><p className="native"><a href={nativeLink} rel="noreferrer">{text.open}</a></p></>} {error && <p role="alert">{error}</p>}</main>;
}
