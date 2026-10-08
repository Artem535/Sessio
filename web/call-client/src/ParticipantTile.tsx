import { useEffect, useRef } from 'react';
import { Participant, Track } from 'livekit-client';
export function ParticipantTile({participant}:{participant:Participant}) {
  const video = useRef<HTMLVideoElement>(null), audio = useRef<HTMLAudioElement>(null);
  const camera = participant.getTrackPublication(Track.Source.Camera)?.track;
  const microphone = participant.getTrackPublication(Track.Source.Microphone)?.track;
  useEffect(()=>{ const element=video.current; if (element && camera) camera.attach(element); return()=>{if(element && camera) camera.detach(element);}; },[camera]);
  useEffect(()=>{ const element=audio.current; if (element && microphone && !participant.isLocal) microphone.attach(element); return()=>{if(element && microphone) microphone.detach(element);}; },[microphone,participant.isLocal]);
  return <article className="tile"><video ref={video} autoPlay playsInline muted={participant.isLocal}/><audio ref={audio} autoPlay/><span>{participant.name || 'Guest'}{!participant.isMicrophoneEnabled && ' · 🔇'}</span></article>;
}
