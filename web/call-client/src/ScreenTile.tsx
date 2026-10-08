import { useEffect, useRef } from 'react';
import { Participant, Track } from 'livekit-client';
import { text } from './copy';

export function screenTrackOf(participant: Participant) {
  return participant.getTrackPublication(Track.Source.ScreenShare)?.track;
}

// Any participant may publish a screen; each one gets its own tile.
export function ScreenTile({participant}:{participant:Participant}) {
  const video = useRef<HTMLVideoElement>(null);
  const screen = screenTrackOf(participant);
  useEffect(()=>{ const element=video.current; if (element && screen) screen.attach(element); return()=>{if(element && screen) screen.detach(element);}; },[screen]);
  if (!screen) return null;
  return <article className="tile screen"><video ref={video} autoPlay playsInline muted/><span>{participant.name || 'Guest'} · {text.sharing}</span></article>;
}
