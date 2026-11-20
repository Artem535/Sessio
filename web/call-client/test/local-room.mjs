// Isolated synthetic-media integration. No production keys, database or hosts.
import { spawn, spawnSync } from 'node:child_process';
import { mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { resolve } from 'node:path';
import https from 'node:https';
import http from 'node:http';
import { WebSocket, WebSocketServer } from 'ws';
import { chromium } from '@playwright/test';
const dir=mkdtempSync(`${tmpdir()}/sessio-guest-`), processes=[], name=`sessio-guest-${process.pid}`;
const backend=resolve(process.env.SESSIO_TEST_BACKEND || '../../build-token-backend/pcm_token_backend');
const env={...process.env,DB_PATH:`${dir}/test.sqlite`,PORT:'19081',LIVEKIT_API_KEY:'devkey',LIVEKIT_API_SECRET:'secret',LIVEKIT_WS_ENDPOINT:'wss://localhost:19443/livekit',INVITATION_BASE_URL:'https://localhost:19443/join#code={0}&passcode={1}'};
let browser, server, nativeProxy, exchanges=0;const mediaSockets=new Set();
const run=(cmd,args,options={})=>{const child=spawn(cmd,args,{stdio:'ignore',...options});processes.push(child);return child;};
const check=(condition,message)=>{if(!condition)throw new Error(message);};
async function wait(url){for(let i=0;i<100;i++){try{if((await fetch(url)).ok)return;}catch{}await new Promise(r=>setTimeout(r,100));}throw new Error('local service startup failed');}
try {
  check(spawnSync('openssl',['req','-x509','-newkey','rsa:2048','-nodes','-keyout',`${dir}/key.pem`,'-out',`${dir}/cert.pem`,'-days','1','-subj','/CN=localhost'],{stdio:'ignore'}).status===0,'certificate generation failed');
  const seed=spawnSync(backend,['--seed-account'],{env,encoding:'utf8'});check(seed.status===0,'test backend seed failed');
  const credential=seed.stdout.trim().split('\n').at(-1);
  run('podman',['run','--rm','--name',name,'--network=host','docker.io/livekit/livekit-server@sha256:5d3dcc475d064536d9948ebe4eeab8e3b24d6f07a46f6d71a3415a2901bbdc52','--dev','--bind','127.0.0.1','--port','19080']);
  run(backend,[],{env});await wait('http://127.0.0.1:19081/healthz');
  server=https.createServer({key:readFileSync(`${dir}/key.pem`),cert:readFileSync(`${dir}/cert.pem`)},(req,res)=>{
    if(req.url.startsWith('/v1/') || req.url.startsWith('/livekit/')){
      if(req.method==='POST' && req.url.endsWith('/client-token'))exchanges++;
      const media=req.url.startsWith('/livekit/');const proxy=http.request({hostname:'127.0.0.1',port:media?19080:19081,path:media?req.url.slice(8):req.url,method:req.method,headers:req.headers},reply=>{res.writeHead(reply.statusCode,reply.headers);reply.pipe(res);});
      proxy.on('error',()=>{res.writeHead(502);res.end();});req.pipe(proxy);return;
    }
    const path=req.url.startsWith('/assets/')?resolve('dist',req.url.slice(1)):resolve('dist/index.html');
    try{res.setHeader('Content-Type',path.endsWith('.js')?'text/javascript':path.endsWith('.css')?'text/css':'text/html');res.end(readFileSync(path));}catch{res.writeHead(404);res.end();}
  });
  const wss=new WebSocketServer({noServer:true});server.on('upgrade',(req,socket,head)=>{
    const upstream=new WebSocket(`ws://127.0.0.1:19080${req.url.slice(8)}`);
    upstream.on('open',()=>wss.handleUpgrade(req,socket,head,client=>{
      mediaSockets.add(client);client.on('close',()=>mediaSockets.delete(client));
      client.on('message',(data,binary)=>upstream.send(data,{binary}));upstream.on('message',(data,binary)=>client.send(data,{binary}));
      client.on('close',()=>upstream.close());upstream.on('close',()=>client.close());client.on('error',()=>upstream.close());
    }));upstream.on('error',()=>socket.destroy());
  });await new Promise(r=>server.listen(19443,'127.0.0.1',r));
  const now=Date.now();const response=await fetch('http://127.0.0.1:19081/v1/meetings',{method:'POST',headers:{Authorization:`Bearer ${credential}`,'Content-Type':'application/json'},body:JSON.stringify({scheduledStart:new Date(now-60000).toISOString().replace(/\.\d{3}Z$/,'Z'),scheduledEnd:new Date(now+1800000).toISOString().replace(/\.\d{3}Z$/,'Z')})});
  check(response.ok,'ephemeral meeting creation failed');const meeting=await response.json();
  if(process.env.SESSIO_TEST_NATIVE){
    // Native synthetic publisher uses loopback WS because the generated TLS
    // certificate is intentionally not installed in the host trust store.
    nativeProxy=http.createServer((req,res)=>{
      const proxy=http.request({hostname:'127.0.0.1',port:19081,path:req.url,method:req.method,headers:req.headers},reply=>{
        const chunks=[];reply.on('data',chunk=>chunks.push(chunk));reply.on('end',()=>{
          const value=JSON.parse(Buffer.concat(chunks).toString());if(value.endpointUrl)value.endpointUrl='ws://127.0.0.1:19080';
          res.writeHead(reply.statusCode,{'Content-Type':'application/json'});res.end(JSON.stringify(value));
        });
      });proxy.on('error',()=>{res.writeHead(502);res.end();});req.pipe(proxy);
    });await new Promise(r=>nativeProxy.listen(19082,'127.0.0.1',r));
    const invitation=new URLSearchParams(new URL(meeting.invitationUrl).hash.slice(1));
    run(resolve(process.env.SESSIO_TEST_NATIVE),['--code',invitation.get('code'),'--passcode',invitation.get('passcode'),'--backend','http://127.0.0.1:19082','--name','Synthetic native'],{env:{...process.env,QT_QPA_PLATFORM:'offscreen'}});
  }
  browser=await chromium.launch({args:['--use-fake-device-for-media-stream','--use-fake-ui-for-media-stream','--ignore-certificate-errors']});
  const contexts=[], pages=[];
  for(let i=0;i<2;i++){
    const context=await browser.newContext({ignoreHTTPSErrors:true,locale:'ru-RU',permissions:['camera','microphone']});contexts.push(context);
    const page=await context.newPage();pages.push(page);await page.goto(meeting.invitationUrl);await page.getByLabel('Имя',{exact:true}).fill('Synthetic guest');
    await page.getByRole('button',{name:'Проверить устройства',exact:true}).click();await page.waitForFunction(()=>document.querySelector('meter')?.value>0);
    await page.getByRole('button',{name:'Подключиться',exact:true}).click();await page.getByRole('button',{name:'Завершить',exact:true}).waitFor({timeout:20000});
  }
  const participants=process.env.SESSIO_TEST_NATIVE?3:2;
  for(const page of pages){await page.locator('.tile').nth(participants-1).waitFor();await page.waitForFunction(()=>[...document.querySelectorAll('.tile video')].every(v=>v.videoWidth>0),{timeout:20000});
    await page.waitForFunction(()=>[...document.querySelectorAll('.tile audio')].some(a=>a.srcObject?.getAudioTracks().some(t=>t.readyState==='live')));}
  // Drop the actual signaling transport. Browser offline emulation does not
  // consistently close already-established WebSockets in Chromium.
  for(const socket of mediaSockets)socket.terminate();
  await pages[1].getByText('Восстановление соединения…',{exact:true}).waitFor({timeout:20000});
  await pages[1].getByText('Восстановление соединения…',{exact:true}).waitFor({state:'hidden',timeout:30000});
  await pages[1].waitForFunction(()=>[...document.querySelectorAll('.tile video')].every(v=>v.videoWidth>0),{timeout:20000});
  check(exchanges===2,'automatic reconnect must not exchange another guest token');
  await pages[1].evaluate(()=>{window.__testLocalTracks=document.querySelector('.tile video').srcObject.getTracks();});
  await pages[1].getByRole('button',{name:'Завершить',exact:true}).click();await pages[0].waitForFunction(n=>document.querySelectorAll('.tile').length===n,participants-1);
  await pages[1].waitForFunction(()=>window.__testLocalTracks.every(track=>track.readyState==='ended'));
  await pages[0].getByRole('button',{name:'Завершить',exact:true}).click();
  console.log('PASS: isolated backend invitation, HTTPS/WSS, duplicate-name independent guests, synthetic camera/microphone publication, remote video/audio subscription, network reconnect, leave propagation. Browser '+browser.version());
  if(process.env.SESSIO_TEST_NATIVE)console.log('PASS: C++ LiveKit native synthetic publisher redeemed legacy native token and browser subscribed to its media (loopback WS for native).');
} finally {
  await browser?.close();server?.close();nativeProxy?.close();for(const child of processes)child.kill('SIGTERM');spawnSync('podman',['rm','-f',name],{stdio:'ignore'});rmSync(dir,{recursive:true,force:true});
}
