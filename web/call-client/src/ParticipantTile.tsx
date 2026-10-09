import { useEffect, useRef } from 'react';
import { Participant, Track } from 'livekit-client';
import { MicIcon } from './Icons';
import { text } from './copy';

export function initials(name:string) {
  const parts = name.trim().split(/\s+/).filter(Boolean);
  return (parts.length > 1 ? parts[0][0] + parts[1][0] : (parts[0] ?? '?').slice(0, 2)).toUpperCase();
}

export function NameTag({participant,suffix}:{participant:Participant;suffix?:string}) {
  const name = participant.name || 'Guest';
  return <span className="name-tag">{!participant.isMicrophoneEnabled && !suffix && <span className="muted" role="img" aria-label={text.muted}><MicIcon off/></span>}
    {name}{participant.isLocal && !suffix && <span className="dim"> ({text.you})</span>}{suffix && <span className="dim"> · {suffix}</span>}</span>;
}

export function ParticipantTile({participant}:{participant:Participant}) {
  const video = useRef<HTMLVideoElement>(null), audio = useRef<HTMLAudioElement>(null);
  const camera = participant.getTrackPublication(Track.Source.Camera)?.track;
  const microphone = participant.getTrackPublication(Track.Source.Microphone)?.track;
  const showVideo = !!camera && participant.isCameraEnabled;
  useEffect(()=>{ const element=video.current; if (element && camera) camera.attach(element); return()=>{if(element && camera) camera.detach(element);}; },[camera]);
  useEffect(()=>{ const element=audio.current; if (element && microphone && !participant.isLocal) microphone.attach(element); return()=>{if(element && microphone) microphone.detach(element);}; },[microphone,participant.isLocal]);
  return <article className={'tile'+(participant.isSpeaking?' speaking':'')+(participant.isLocal?' local':'')}>
    <video ref={video} autoPlay playsInline muted={participant.isLocal} hidden={!showVideo}><track kind="captions"/></video>
    {!showVideo && <div className="avatar" aria-hidden="true">{initials(participant.name || 'Guest')}</div>}
    <audio ref={audio} autoPlay><track kind="captions"/></audio><NameTag participant={participant}/></article>;
}
