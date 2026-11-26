import {test} from 'node:test';
import {strict as assert} from 'node:assert';
import {createHash} from 'node:crypto';
import {MeetingStore} from '../src/store.mjs';
import {createMeetingRouter} from '../src/routes.mjs';
import {sampleOgg} from './fixtures.mjs';
test('whole audio commits, retries and rejects mismatch',async()=>{
 const s=new MeetingStore(':memory:'),token='d'.repeat(40),audio=sampleOgg(),digest=createHash('sha256').update(audio).digest('hex');
 try {s.registerDevice('d',token);const app=createMeetingRouter(s,{adminToken:'a'.repeat(40)});const headers={authorization:`Bearer ${token}`,'content-type':'application/octet-stream','content-length':String(audio.length),'x-content-sha256':digest,'x-meeting-end-reason':'user','x-meeting-time-trust':'uncalibrated'};const send=h=>app.request('/meetings/m/audio',{method:'PUT',headers:h,body:audio});
 let r=await send({...headers,'x-content-sha256':'0'.repeat(64)});assert.equal(r.status,400);assert.equal(s.db.prepare("SELECT count(*) AS n FROM meetings WHERE id='m'").get().n,0);
 r=await send({...headers,'content-length':String(audio.length+1)});assert.equal(r.status,400);assert.equal(s.db.prepare("SELECT count(*) AS n FROM meetings WHERE id='m'").get().n,0);
 r=await send(headers);assert.equal(r.status,200);assert.equal(r.headers.get('x-meeting-upload-mode'),'whole-v1');const receipt=await r.json();assert.equal(receipt.committed,true);assert.equal(receipt.manifest.count,Math.ceil(audio.length/4096));
 r=await send(headers);assert.deepEqual(await r.json(),receipt);r=await send({...headers,'x-meeting-end-reason':'limit'});assert.equal(r.status,409);
 } finally{s.close()}
});

test('whole audio accepts matching legacy partial chunks',async()=>{
 const s=new MeetingStore(':memory:'), token='d'.repeat(40), audio=sampleOgg(), hash=b=>createHash('sha256').update(b).digest('hex');
 try{s.registerDevice('d',token);s.create('d','m');s.put('d','m',0,audio,hash(audio));const app=createMeetingRouter(s,{adminToken:'a'.repeat(40)});const r=await app.request('/meetings/m/audio',{method:'PUT',headers:{authorization:`Bearer ${token}`,'content-type':'application/octet-stream','content-length':String(audio.length),'x-content-sha256':hash(audio),'x-meeting-end-reason':'user','x-meeting-time-trust':'uncalibrated'},body:audio});assert.equal(r.status,200);assert.equal((await r.json()).committed,true)}finally{s.close()}
});
test('aborted whole audio leaves no meeting',async()=>{
 const s=new MeetingStore(':memory:'),token='d'.repeat(40),ctl=new AbortController();try{s.registerDevice('d',token);const app=createMeetingRouter(s,{adminToken:'a'.repeat(40)});const stream=new ReadableStream({start(c){c.enqueue(new Uint8Array([1]));ctl.abort()}});const req=new Request('http://local/meetings/m/audio',{method:'PUT',headers:{authorization:`Bearer ${token}`,'content-type':'application/octet-stream','content-length':'2','x-content-sha256':'0'.repeat(64),'x-meeting-end-reason':'user','x-meeting-time-trust':'uncalibrated'},body:stream,signal:ctl.signal,duplex:'half'});const r=await app.fetch(req);assert.equal(r.status,408);assert.equal(s.db.prepare('SELECT count(*) AS n FROM meetings').get().n,0)}finally{s.close()}
});

test('whole audio repairs matching corrupted partial and committed chunks, rejects changed stored digest',()=>{
 const s=new MeetingStore(':memory:'),audio=sampleOgg(),hash=b=>createHash('sha256').update(b).digest('hex'),manifest={digest:hash(audio),reason:'user',timeTrust:'uncalibrated',recordedAt:null};
 try {s.create('d','m');s.put('d','m',0,audio,hash(audio));s.db.prepare("UPDATE chunks SET data=X'00' WHERE meeting_id='m'").run();
 const receipt=s.putAudio('d','m',audio,manifest);assert.equal(receipt.committed,true);
 s.db.prepare("UPDATE chunks SET data=X'00' WHERE meeting_id='m'").run();s.db.prepare("UPDATE meetings SET state='failed',failed_stage='queued',last_error='integrity',attempts=3 WHERE id='m'").run();
 assert.equal(s.putAudio('d','m',audio,manifest).state,'queued');assert.equal(s.db.prepare("SELECT attempts FROM meetings WHERE id='m'").get().attempts,0);
 s.db.prepare("UPDATE chunks SET digest=? WHERE meeting_id='m'").run('0'.repeat(64));assert.throws(()=>s.putAudio('d','m',audio,manifest),/conflict/);
 }finally{s.close()}
});

test('whole audio repeats an existing receipt after available storage is reduced', () => {
 const audio = sampleOgg();
 const digest = createHash('sha256').update(audio).digest('hex');
 const store = new MeetingStore(':memory:', {storageLimitBytes: audio.length});
 const manifest = {digest, reason: 'user', timeTrust: 'uncalibrated', recordedAt: null};
 try {
  const receipt = store.putAudio('device', 'meeting', audio, manifest);
  store.storageLimitBytes = audio.length - 1;
  assert.deepEqual(store.putAudio('device', 'meeting', audio, manifest), receipt);
 } finally {
  store.close();
 }
});
