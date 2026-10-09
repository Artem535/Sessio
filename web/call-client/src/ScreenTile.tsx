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
  const content = <><video ref={video} autoPlay playsInline muted><track kind="captions"/></video><NameTag participant={participant} suffix={text.sharing}/></>;
  // A screen in the strip is a button that brings it to the stage.
  if (onSelect) return <button type="button" className="tile screen selectable" aria-label={`${participant.name || 'Guest'} · ${text.sharing}`} onClick={onSelect}>{content}</button>;
  return <article className="tile screen">{content}</article>;
}
