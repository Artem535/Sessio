// @vitest-environment jsdom
import React, { act } from 'react';
import { createRoot, type Root } from 'react-dom/client';
import { afterEach, beforeEach, expect, test, vi } from 'vitest';
const harness = vi.hoisted(() => ({ rooms: [] as any[], fail: false, failMedia:false, delayMedia: false, release: null as null | (()=>void) }));
vi.mock('livekit-client', () => {
  const events = { Disconnected:'disconnected', Reconnecting:'reconnecting', SignalReconnecting:'signalReconnecting', Reconnected:'reconnected' };
  class Room {
    handlers = new Map<string, Set<Function>>(); remoteParticipants = new Map();
    localParticipant = {identity:'local',name:'Гость',isLocal:true,isMicrophoneEnabled:false,isCameraEnabled:false,
      getTrackPublication:()=>undefined,
      setMicrophoneEnabled:vi.fn(async()=>{if(harness.failMedia)throw new Error('private permission detail');if(harness.delayMedia) await new Promise<void>(resolve=>{harness.release=resolve;});}),
      setCameraEnabled:vi.fn(async()=>{})};
    constructor(){harness.rooms.push(this);}
    on(event:string,fn:Function){if(!this.handlers.has(event))this.handlers.set(event,new Set());this.handlers.get(event)!.add(fn);return this;}
    off(event:string,fn:Function){this.handlers.get(event)?.delete(fn);return this;}
    emit(event:string){this.handlers.get(event)?.forEach(fn=>fn());}
    connect=vi.fn(async()=>{if(harness.fail)throw new Error('private SDK detail');});
    disconnect=vi.fn(async()=>{this.emit('disconnected');});
    startAudio=vi.fn(async()=>{});switchActiveDevice=vi.fn(async()=>{});
  }
  return {Room,RoomEvent:events,Track:{Source:{Camera:'camera',Microphone:'microphone'}}};
});
import { App } from '../src/App';
import { text } from '../src/copy';
import { Prejoin } from '../src/Prejoin';
let root:Root, host:HTMLDivElement;
async function click(label:string){await act(async()=>{Array.from(host.querySelectorAll('button')).find(b=>b.textContent===label)!.click();});}
beforeEach(async()=>{
  harness.rooms=[];harness.fail=false;harness.failMedia=false;harness.delayMedia=false;harness.release=null;
  Object.assign(globalThis,{IS_REACT_ACT_ENVIRONMENT:true});
  Object.defineProperty(navigator,'mediaDevices',{configurable:true,value:{enumerateDevices:async()=>[]}});
  vi.stubGlobal('fetch',vi.fn(async()=>({ok:true,json:async()=>({endpointUrl:'wss://test.invalid',roomName:'room',token:'test',expiresAt:9999999999})})));
  host=document.createElement('div');document.body.append(host);root=createRoot(host);
  await act(async()=>root.render(React.createElement(App,{invitation:{code:'test',passcode:'test'}})));
  await act(async()=>{const input=host.querySelector('input')!;Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value')!.set!.call(input,'Гость');input.dispatchEvent(new Event('input',{bubbles:true}));});
});
afterEach(async()=>{await act(async()=>root.unmount());host.remove();vi.unstubAllGlobals();});
test('failed connection disconnects and permits retry without leaking SDK error',async()=>{
  harness.fail=true;await click(text.join);expect(harness.rooms[0].disconnect).toHaveBeenCalled();expect(host.textContent).not.toContain('private SDK detail');
  harness.fail=false;await click(text.join);expect(harness.rooms).toHaveLength(2);expect(host.textContent).toContain(text.leave);
});
test('reconnect retains room and token; leave disconnects and unmount removes listeners',async()=>{
  await click(text.join);const room=harness.rooms[0];
  await act(async()=>room.emit('reconnecting'));expect(host.textContent).toContain(text.reconnecting);
  await act(async()=>room.emit('reconnected'));expect(fetch).toHaveBeenCalledTimes(1);expect(harness.rooms).toHaveLength(1);
  await act(async()=>room.emit('signalReconnecting'));expect(host.textContent).toContain(text.reconnecting);
  await act(async()=>room.emit('reconnected'));
  await click(text.leave);expect(room.disconnect).toHaveBeenCalled();expect(host.textContent).toContain(text.ended);
  expect([...room.handlers.values()].reduce((n,s)=>n+s.size,0)).toBe(0);
});
test('disconnect during device activation ends join and releases connection',async()=>{
  harness.delayMedia=true;await click(text.join);const room=harness.rooms[0];
  await act(async()=>room.emit('disconnected'));await act(async()=>harness.release?.());
  expect(host.textContent).toContain(text.ended);expect(host.textContent).not.toContain(text.leave);
});
test('deadline disconnects a join stalled during media activation and ignores late completion',async()=>{
  vi.useFakeTimers();
  try {
    harness.delayMedia=true;await click(text.join);const room=harness.rooms[0];
    await act(async()=>vi.advanceTimersByTimeAsync(15000));expect(room.disconnect).toHaveBeenCalled();
    await act(async()=>harness.release?.());expect(host.textContent).not.toContain(text.leave);expect(host.querySelector('[role=alert]')).not.toBeNull();
  } finally {vi.useRealTimers();}
});
test('late device enumeration after preview unmount stops tracks and never starts audio context',async()=>{
  let resolveDevices:(value:any[])=>void=()=>{};const stop=vi.fn();const Audio=vi.fn();
  vi.stubGlobal('AudioContext',Audio);
  Object.defineProperty(navigator,'mediaDevices',{configurable:true,value:{getUserMedia:async()=>({getTracks:()=>[{stop}],getAudioTracks:()=>[],getVideoTracks:()=>[]}),enumerateDevices:()=>new Promise(resolve=>{resolveDevices=resolve;})}});
  await act(async()=>root.render(React.createElement(Prejoin,{busy:false,onJoin:()=>{}})));
  await click(text.check);await act(async()=>root.render(null));await act(async()=>resolveDevices([]));
  expect(stop).toHaveBeenCalled();expect(Audio).not.toHaveBeenCalled();
});
test('media permission rejection preserves joined room and exposes safe notice',async()=>{
  harness.failMedia=true;await click(text.join);expect(host.textContent).toContain(text.leave);expect(host.textContent).toContain(text.media);
  expect(host.textContent).not.toContain('private permission detail');expect(harness.rooms[0].disconnect).not.toHaveBeenCalled();
});
