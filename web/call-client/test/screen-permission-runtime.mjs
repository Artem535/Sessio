// Explicit local capability gate; synthetic tracks, ephemeral credentials only.
import { spawn, spawnSync } from 'node:child_process';
import { mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { createHmac } from 'node:crypto';
import https from 'node:https';
import net from 'node:net';
import { createRequire } from 'node:module';
const require=createRequire(process.env.SESSIO_GATE_MODULES || import.meta.url);
const {chromium}=require('@playwright/test');
const {WebSocket,WebSocketServer}=require('ws');
const sdk=require.resolve('livekit-client').replace(/livekit-client\.umd\.js$/, 'livekit-client.umd.js');
const dir=mkdtempSync(`${tmpdir()}/sessio-screen-gate-`), name=`sessio-screen-gate-${process.pid}`;
const key='devkey',secret='secret',room='screen-capability',identity='owner';
const jwt=video=>{const enc=v=>Buffer.from(JSON.stringify(v)).toString('base64url');const data=enc({alg:'HS256',typ:'JWT'})+'.'+enc({iss:key,sub:identity,nbf:Math.floor(Date.now()/1000)-10,exp:Math.floor(Date.now()/1000)+600,video});return data+'.'+createHmac('sha256',secret).update(data).digest('base64url');};
const token=jwt({roomJoin:true,room,canPublish:true,canSubscribe:true,canPublishSources:['camera','microphone']});
const admin=jwt({roomAdmin:true,room});
const check=(condition,message)=>{if(!condition)throw new Error(message);};
const delay=ms=>new Promise(r=>setTimeout(r,ms));
async function api(method,body){const r=await fetch(`http://127.0.0.1:19090/twirp/livekit.RoomService/${method}`,{method:'POST',headers:{Authorization:`Bearer ${admin}`,'Content-Type':'application/json'},body:JSON.stringify(body)});check(r.ok,`RoomService ${method} failed (${r.status})`);return r.json();}
const participant=()=>api('GetParticipant',{room,identity});
const permission=sources=>api('UpdateParticipant',{room,identity,permission:{canPublish:true,canSubscribe:true,canPublishData:true,canPublishSources:sources}});
let browser,server,child;
try {
  const probe=net.createServer();await new Promise((resolve,reject)=>{probe.once('error',reject);probe.listen(19090,'127.0.0.1',resolve);});await new Promise(resolve=>probe.close(resolve));
  check(spawnSync('openssl',['req','-x509','-newkey','rsa:2048','-nodes','-keyout',`${dir}/key.pem`,'-out',`${dir}/cert.pem`,'-days','1','-subj','/CN=localhost'],{stdio:'ignore'}).status===0,'certificate generation failed');
  child=spawn('podman',['run','--rm','--name',name,'--network=host','docker.io/livekit/livekit-server@sha256:5d3dcc475d064536d9948ebe4eeab8e3b24d6f07a46f6d71a3415a2901bbdc52','--dev','--bind','127.0.0.1','--port','19090'],{stdio:'ignore'});
  for(let i=0;i<100;i++){try{if((await fetch('http://127.0.0.1:19090')).ok)break;}catch{}await delay(100);}
  server=https.createServer({key:readFileSync(`${dir}/key.pem`),cert:readFileSync(`${dir}/cert.pem`)},(req,res)=>{res.setHeader('Content-Type',req.url==='/sdk.js'?'text/javascript':'text/html');res.end(req.url==='/sdk.js'?readFileSync(sdk):'<script src="/sdk.js"></script>');});
  const wss=new WebSocketServer({noServer:true});server.on('upgrade',(req,socket,head)=>{
    const upstream=new WebSocket(`ws://127.0.0.1:19090${req.url.slice(8)}`);
    upstream.on('open',()=>wss.handleUpgrade(req,socket,head,client=>{
      client.on('message',(data,binary)=>upstream.send(data,{binary}));upstream.on('message',(data,binary)=>client.send(data,{binary}));
      client.on('close',()=>upstream.close());upstream.on('close',()=>client.close());client.on('error',()=>upstream.close());
    }));upstream.on('error',()=>socket.destroy());
  });
  await new Promise(r=>server.listen(19444,'127.0.0.1',r));
  browser=await chromium.launch({args:['--use-fake-device-for-media-stream','--use-fake-ui-for-media-stream','--ignore-certificate-errors']});
  const context=await browser.newContext({ignoreHTTPSErrors:true,permissions:['camera','microphone']});const page=await context.newPage();
  await page.goto('https://localhost:19444');
  console.log('Browser '+browser.version()+'; SDK '+await page.evaluate(()=>window.LivekitClient.version));
  await page.evaluate(async token=>{const L=window.LivekitClient;window.room=new L.Room();await window.room.connect('wss://localhost:19444/livekit',token);const tracks=await L.createLocalTracks({audio:true,video:true});for(const track of tracks)await window.room.localParticipant.publishTrack(track,{source:track.kind==='audio'?L.Track.Source.Microphone:L.Track.Source.Camera});window.screen=()=>{const c=document.createElement('canvas');c.width=640;c.height=360;c.getContext('2d').fillRect(0,0,640,360);const stream=c.captureStream(5);setInterval(()=>{c.getContext('2d').fillStyle=`rgb(${Date.now()%255},50,100)`;c.getContext('2d').fillRect(0,0,640,360);},200);return new L.LocalVideoTrack(stream.getVideoTracks()[0]);};},token);
  await delay(1000);const initial=await participant();check(initial.tracks.length===2,'camera/mic initial publication absent');const cameraMic=initial.tracks.map(t=>t.sid).sort();
  const denied=await page.evaluate(async()=>{const track=window.screen();try{await window.room.localParticipant.publishTrack(track,{source:window.LivekitClient.Track.Source.ScreenShare});return false;}catch{return true;}finally{track.stop();}});
  check(denied,'baseline screen publication was accepted');console.log('PASS baseline camera/mic published; screen denied');
  await permission(['CAMERA','MICROPHONE','SCREEN_SHARE']);await delay(300);
  const grantedToken=await page.evaluate(()=>window.room.engine.token);
  await page.evaluate(async()=>{window.screenTrack=window.screen();await window.room.localParticipant.publishTrack(window.screenTrack,{source:window.LivekitClient.Track.Source.ScreenShare});});
  await delay(1000);const granted=await participant();console.log('grant sources '+JSON.stringify(granted.tracks.map(t=>t.source))+' permission '+JSON.stringify(granted.permission));check(granted.tracks.length===3,'screen grant did not publish');console.log('PASS selective screen grant publishes third track');
  await permission(['CAMERA','MICROPHONE']);await delay(1500);
  const revoked=await participant();const screenTracks=revoked.tracks.filter(t=>t.source==='SCREEN_SHARE');
  check(cameraMic.every(sid=>revoked.tracks.some(t=>t.sid===sid)),'selective revoke removed camera/mic');
  console.log(`OBSERVED selective revoke: camera/mic SIDs preserved; ${screenTracks.length} screen track(s) remain; allowed sources ${JSON.stringify(revoked.permission.can_publish_sources)}`);
  // If server leaves the existing track published, cooperative client unpublish
  // is not a security boundary and acquisition must remain fail-closed.
  if(screenTracks.length)throw new Error('CAPABILITY BLOCK: selective source revoke leaves live screen publication; cannot enforce expiry against an uncooperative connected owner');
  console.log('PASS selective revoke removed screen while preserving camera/mic');
  const republishDenied=await page.evaluate(async()=>{const track=window.screen();try{await window.room.localParticipant.publishTrack(track,{source:window.LivekitClient.Track.Source.ScreenShare});return false;}catch{return true;}finally{track.stop();}});
  check(republishDenied,'connected owner republished screen after source revoke');console.log('PASS connected owner screen republish denied after revoke');
  check(typeof grantedToken==='string','SDK refresh token unavailable for old-JWT probe');
  const grantedClaims=JSON.parse(Buffer.from(grantedToken.split('.')[1],'base64url').toString());
  check(grantedClaims.video?.canPublishSources?.includes('screen_share'),'server-refreshed token missing screen grant; invalid stale-grant probe');
  console.log('OBSERVED server-refreshed JWT screen grant true; TTL approximately '+(grantedClaims.exp-Math.floor(Date.now()/1000))+' seconds');
  await page.evaluate(async oldToken=>{await window.room.disconnect();window.room=new window.LivekitClient.Room();await window.room.connect('wss://localhost:19444/livekit',oldToken);},grantedToken);
  const stalePermission=await participant();console.log('OBSERVED old grant-token new connection allowed sources '+JSON.stringify(stalePermission.permission.can_publish_sources));
  const staleDenied=await page.evaluate(async()=>{window.staleScreenTrack=window.screen();try{await window.room.localParticipant.publishTrack(window.staleScreenTrack,{source:window.LivekitClient.Track.Source.ScreenShare});return false;}catch{window.staleScreenTrack.stop();return true;}});
  await delay(1000);const staleTracks=(await participant()).tracks.filter(t=>t.source==='SCREEN_SHARE');
  console.log('OBSERVED old grant-token screen publication count '+staleTracks.length);
  check(staleDenied && staleTracks.length===0,'CAPABILITY BLOCK: old server-refreshed grant JWT permits screen publication after revoke on a new connection');
  console.log('PASS old server-refreshed JWT cannot republish screen after revoke');
} finally {
  await browser?.close();server?.close();child?.kill('SIGTERM');spawnSync('podman',['rm','-f',name],{stdio:'ignore'});rmSync(dir,{recursive:true,force:true});
}
