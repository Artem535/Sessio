// @vitest-environment jsdom
import React, { act } from 'react';
import { createRoot } from 'react-dom/client';
import { expect, test } from 'vitest';
import { CallRoom } from '../src/CallRoom';
import { Track } from 'livekit-client';

test('large participants mode separates self and updates when a remote leaves', async()=>{
  Object.assign(globalThis,{IS_REACT_ACT_ENVIRONMENT:true});
  Object.defineProperty(navigator,'mediaDevices',{configurable:true,value:{enumerateDevices:async()=>[]}});
  let sharing=false;
  const screen={attach:()=>{},detach:()=>{}};
  const participant=(identity:string,isLocal=false)=>({identity,name:identity,isLocal,getTrackPublication:(source:string)=>sharing && source===Track.Source.ScreenShare && identity==='Other'?{track:screen}:undefined});
  const listeners=new Map<string,Function[]>();
  const room:any={localParticipant:participant('Self',true),remoteParticipants:new Map([['other',participant('Other')]]),on:(event:string,fn:Function)=>{listeners.set(event,[...(listeners.get(event)??[]),fn]);},off:()=>{}};
  const host=document.createElement('div');document.body.append(host);const root=createRoot(host);
  try {
    await act(async()=>root.render(React.createElement(CallRoom,{room,onLeave:()=>{}})));
    const toggle=host.querySelector<HTMLButtonElement>('[data-layout=side]');
    expect(toggle).not.toBeNull();
    expect(host.querySelector('[data-layout=auto]')!.getAttribute('aria-pressed')).toBe('true');
    expect(host.querySelector('.pip-preview')).not.toBeNull();
    await act(async()=>toggle!.click());
    expect(host.querySelector('.participants-main')!.textContent).toContain('Other');
    expect(host.querySelector('.participants-main')!.textContent).not.toContain('Self');
    expect(host.querySelector('.self-preview')!.textContent).toContain('Self');
    await act(async()=>{
      room.remoteParticipants.set('second',participant('Second'));
      listeners.forEach(fns=>fns.forEach(fn=>fn()));
      host.querySelector<HTMLButtonElement>('[data-layout=auto]')!.click();
    });
    expect(host.querySelector('.pip-preview')).toBeNull();
    expect(host.querySelectorAll('.grid .tile')).toHaveLength(3);
    await act(async()=>{sharing=true;listeners.forEach(fns=>fns.forEach(fn=>fn()));});
    expect(host.querySelectorAll('.featured .screen')).toHaveLength(1);
    await act(async()=>host.querySelector<HTMLButtonElement>('[data-layout=overlay]')!.click());
    expect(host.querySelector('.screen')).toBeNull();
    expect(host.querySelectorAll('.participants-main .tile')).toHaveLength(2);
    expect(host.querySelectorAll('.pip-preview .tile')).toHaveLength(1);
    await act(async()=>toggle!.click());
    await act(async()=>{room.remoteParticipants.clear();listeners.forEach(fns=>fns.forEach(fn=>fn()));});
    expect(host.querySelectorAll('.self-preview .tile')).toHaveLength(1);
    await act(async()=>host.querySelector<HTMLButtonElement>('[data-layout=grid]')!.click());
    expect(host.querySelector('.self-preview')).toBeNull();
    expect(host.querySelectorAll('.grid .tile')).toHaveLength(1);
  } finally {await act(async()=>root.unmount());host.remove();}
});
