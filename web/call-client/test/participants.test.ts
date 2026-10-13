// @vitest-environment jsdom
import React, { act } from 'react';
import { createRoot } from 'react-dom/client';
import { expect, test, vi } from 'vitest';
import { CallRoom } from '../src/CallRoom';

test('large participants mode separates self and updates when a remote leaves', async()=>{
  Object.assign(globalThis,{IS_REACT_ACT_ENVIRONMENT:true});
  Object.defineProperty(navigator,'mediaDevices',{configurable:true,value:{enumerateDevices:async()=>[]}});
  const participant=(identity:string,isLocal=false)=>({identity,name:identity,isLocal,getTrackPublication:()=>undefined});
  const listeners=new Map<string,Function[]>();
  const room:any={localParticipant:participant('Self',true),remoteParticipants:new Map([['other',participant('Other')]]),on:(event:string,fn:Function)=>{listeners.set(event,[...(listeners.get(event)??[]),fn]);},off:()=>{}};
  const host=document.createElement('div');document.body.append(host);const root=createRoot(host);
  try {
    await act(async()=>root.render(React.createElement(CallRoom,{room,onLeave:()=>{}})));
    const toggle=host.querySelector<HTMLButtonElement>('[data-testid=participants-layout]');
    expect(toggle).not.toBeNull();
    await act(async()=>toggle!.click());
    expect(host.querySelector('.participants-main')!.textContent).toContain('Other');
    expect(host.querySelector('.participants-main')!.textContent).not.toContain('Self');
    expect(host.querySelector('.self-preview')!.textContent).toContain('Self');
    await act(async()=>{room.remoteParticipants.clear();listeners.forEach(fns=>fns.forEach(fn=>fn()));});
    expect(host.querySelectorAll('.self-preview .tile')).toHaveLength(1);
    await act(async()=>toggle!.click());
    expect(host.querySelector('.self-preview')).toBeNull();
    expect(host.querySelectorAll('.grid .tile')).toHaveLength(1);
  } finally {await act(async()=>root.unmount());host.remove();}
});
