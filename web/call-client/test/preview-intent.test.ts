// @vitest-environment jsdom
import React, { act } from 'react';
import { createRoot, type Root } from 'react-dom/client';
import { afterEach, beforeEach, expect, test, vi } from 'vitest';
import { Prejoin } from '../src/Prejoin';
import { text } from '../src/copy';
let host:HTMLDivElement,root:Root;
const join=vi.fn();
const makeStream=()=>{const audio={enabled:true,stop:vi.fn()},video={enabled:true,stop:vi.fn()};return {getTracks:()=>[audio,video],getAudioTracks:()=>[audio],getVideoTracks:()=>[video]};};
const streams:ReturnType<typeof makeStream>[]=[];
async function click(label:string){await act(async()=>Array.from(host.querySelectorAll('button')).find(b=>b.textContent===label)!.click());}
const boxes=()=>host.querySelectorAll<HTMLInputElement>('input[type=checkbox]');
beforeEach(async()=>{
  streams.length=0;join.mockClear();Object.assign(globalThis,{IS_REACT_ACT_ENVIRONMENT:true});
  vi.stubGlobal('requestAnimationFrame',()=>1);vi.stubGlobal('cancelAnimationFrame',()=>{});
  vi.stubGlobal('AudioContext',class {createAnalyser(){return {fftSize:4,getFloatTimeDomainData:()=>{}};}createMediaStreamSource(){return {connect:()=>{}};}close(){return Promise.resolve();}});
  Object.defineProperty(navigator,'mediaDevices',{configurable:true,value:{getUserMedia:async()=>{const stream=makeStream();streams.push(stream);return stream;},enumerateDevices:async()=>[{deviceId:'mic-2',kind:'audioinput',label:'Mic 2'},{deviceId:'cam-2',kind:'videoinput',label:'Cam 2'}]}});
  host=document.createElement('div');document.body.append(host);root=createRoot(host);
  await act(async()=>root.render(React.createElement(Prejoin,{busy:false,onJoin:join})));
  await act(async()=>{const input=host.querySelector('input')!;Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value')!.set!.call(input,'Guest');input.dispatchEvent(new Event('input',{bubbles:true}));});
});
afterEach(async()=>{await act(async()=>root.unmount());host.remove();vi.unstubAllGlobals();});
test.each([[0,'mic-2'],[1,'cam-2']] as const)('device selector %i preserves disabled checkboxes, new track state and join settings',async(index,deviceId)=>{
  await click(text.check);expect(boxes()[0].checked).toBe(true);expect(boxes()[1].checked).toBe(true);
  await act(async()=>{boxes()[0].click();boxes()[1].click();});
  await act(async()=>{const selector=host.querySelectorAll('select')[index];selector.value=deviceId;selector.dispatchEvent(new Event('change',{bubbles:true}));});
  expect([boxes()[0].checked,boxes()[1].checked,...streams.at(-1)!.getTracks().map(track=>track.enabled)]).toEqual([false,false,false,false]);
  expect(streams[0].getTracks().every(track=>track.stop.mock.calls.length>0)).toBe(true);
  await click(text.join);expect(join).toHaveBeenCalledWith(expect.objectContaining({microphoneEnabled:false,cameraEnabled:false,[index===0?'microphoneId':'cameraId']:deviceId}));
});
test('first preview completion respects explicit off intent chosen while permission is pending',async()=>{
  let resolveMedia:(stream:any)=>void=()=>{};navigator.mediaDevices.getUserMedia=vi.fn(()=>new Promise<MediaStream>(resolve=>{resolveMedia=resolve;}));
  await click(text.check);await act(async()=>{boxes()[0].click();boxes()[0].click();boxes()[1].click();boxes()[1].click();});
  const stream=makeStream();await act(async()=>resolveMedia(stream));
  expect([boxes()[0].checked,boxes()[1].checked,...stream.getTracks().map(track=>track.enabled)]).toEqual([false,false,false,false]);
  await click(text.join);expect(join).toHaveBeenCalledWith(expect.objectContaining({microphoneEnabled:false,cameraEnabled:false}));
});
test('stale preview stops its tracks without overriding the latest off intent',async()=>{
  const completions:((stream:any)=>void)[]=[];navigator.mediaDevices.getUserMedia=vi.fn(()=>new Promise<MediaStream>(resolve=>completions.push(resolve)));
  await click(text.check);await click(text.check);
  const latest=makeStream();await act(async()=>completions[1](latest));await act(async()=>{boxes()[0].click();boxes()[1].click();});
  const stale=makeStream();await act(async()=>completions[0](stale));
  expect(stale.getTracks().every(track=>track.stop.mock.calls.length>0)).toBe(true);
  expect([boxes()[0].checked,boxes()[1].checked,...latest.getTracks().map(track=>track.enabled)]).toEqual([false,false,false,false]);
});
