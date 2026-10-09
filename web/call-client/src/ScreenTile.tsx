import { useEffect, useRef } from 'react';
import { Participant, Track } from 'livekit-client';
import { NameTag } from './ParticipantTile';
import { text } from './copy';

export function screenTrackOf(participant: Participant) {
  return participant.getTrackPublication(Track.Source.ScreenShare)?.track;
}

// Any participant may publish a screen; each one gets its own tile.
export function ScreenTile({participant,onSelect}:{participant:Participant;onSelect?:()=>void}) {
  const video = useRef<HTMLVideoElement>(null);
  const screen = screenTrackOf(participant);
  useEffect(()=>{ const element=video.current; if (element && screen) screen.attach(element); return()=>{if(element && screen) screen.detach(element);}; },[screen]);
  if (!screen) return null;
  return <article className={'tile screen'+(onSelect?' selectable':'')} onClick={onSelect}><video ref={video} autoPlay playsInline muted/><NameTag participant={participant} suffix={text.sharing}/></article>;
}
