import { Hono } from 'hono';
import { createHash, timingSafeEqual, randomBytes } from 'node:crypto';
import { StoreError } from './store.mjs';
import { toOgg,validateAudioStructure } from './audio.mjs';
import { meetingUi, meetingIcon } from './ui.mjs';
import { shareUi } from './share-ui.mjs';
import {renderMinutesMarkdown,minutesForDownload} from './markdown-minutes.mjs';
import {renderPreview} from './markdown-preview.mjs';
import {SubscriptionStore} from './subscriptions.mjs';
const validateUploadedAudio=audio=>{try{validateAudioStructure(audio)}catch(e){if(['invalid_audio','unsupported_audio'].includes(e?.message))throw new StoreError(e.message);throw e}};
const secureEqual=(a,b)=>timingSafeEqual(createHash('sha256').update(a).digest(),createHash('sha256').update(b).digest());
async function bounded(req,limit){
 const declared=Number(req.headers.get('content-length'));if(Number.isFinite(declared)&&declared>limit)throw new StoreError('limit');if(!req.body)throw new StoreError('invalid');
 const r=req.body.getReader(),signal=req.signal;let chunks=[],size=0,rejectAbort;
 const aborted=new Promise((_,reject)=>{rejectAbort=()=>reject(new StoreError('request_aborted'));if(signal.aborted)rejectAbort();else signal.addEventListener('abort',rejectAbort,{once:true})});
 try{for(;;){const {done,value}=await Promise.race([r.read(),aborted]);if(done)break;size+=value.length;if(size>limit){void r.cancel().catch(()=>{});throw new StoreError('limit')}chunks.push(Buffer.from(value))}if(signal.aborted)throw new StoreError('request_aborted');return Buffer.concat(chunks)}
 finally{signal.removeEventListener('abort',rejectAbort);if(signal.aborted)void r.cancel().catch(()=>{});r.releaseLock()}
}
async function json(req,limit=8192){try{return JSON.parse((await bounded(req,limit)).toString('utf8'))}catch(e){if(e instanceof StoreError)throw e;throw new StoreError('invalid')}}
/** Business router has its own authentication, regardless of runtime /v1 policy. */
export function createMeetingRouter(store,{adminToken,adminId='local-admin',retentionSeconds=null,workerConcurrency=2,maxDeviceRequests=4,onDelete=()=>{},getAsrProgress=()=>null,timeKeys=null,timeSeconds=()=>Math.floor(Date.now()/1000),timeMonotonic=()=>performance.now(),maxTimeProofPerMinute=60}){
 if(!Number.isSafeInteger(workerConcurrency)||workerConcurrency<1||workerConcurrency>4)throw new Error('invalid_worker_concurrency');
 if(!Number.isSafeInteger(maxDeviceRequests)||maxDeviceRequests<1||maxDeviceRequests>64)throw new Error('invalid_device_concurrency');
 if(typeof adminId!=='string'||!/^[A-Za-z0-9_-]{1,100}$/.test(adminId))throw new Error('invalid_admin_id');
 if(typeof adminToken!=='string'||adminToken.length<7||/\s/.test(adminToken))throw new Error('admin_token_required');
 if(!Number.isSafeInteger(maxTimeProofPerMinute)||maxTimeProofPerMinute<1||maxTimeProofPerMinute>1000||typeof timeSeconds!=='function'||typeof timeMonotonic!=='function')throw new Error('invalid_time_proof_options');
 const app=new Hono();const activeDevices=new Map();const subscriptions=new SubscriptionStore(store.db);
 app.onError((e,c)=>{if(e?.code==='ERR_SQLITE_ERROR'&&Number.isInteger(e.errcode)&&(e.errcode&255)===13)return c.json({error:'storage_full'},507);if(e instanceof StoreError){const status={not_found:404,invalid:400,limit:413,conflict:409,too_many_pending:409,missing_chunks:409,integrity:409,invalid_audio:422,unsupported_audio:422,storage_full:507,request_aborted:408}[e.code]??500;return c.json({error:e.code},status)}return c.json({error:'internal_error'},500)});
 app.use('*',async(c,next)=>{c.header('X-Meeting-Protocol-Version','1');c.header('Cache-Control','no-store');const requested=c.req.header('x-meeting-protocol-version');if(requested!==undefined&&requested!=='1')return c.json({error:'unsupported_protocol_version'},400);await next()});
 app.get('/ui',()=>meetingUi());
 app.post('/subscriptions',async c=>{const b=await json(c.req.raw);if(!b||Array.isArray(b)||typeof b!=='object'||Object.keys(b).some(k=>!['name','callbackUrl','callbackToken'].includes(k)))throw new StoreError('invalid');try{return c.json(subscriptions.apply(b.name,b.callbackUrl,b.callbackToken??null),202)}catch{throw new StoreError('invalid')}});
 app.get('/share',()=>shareUi());
 app.get('/share/data',c=>{c.header('Referrer-Policy','no-referrer');c.header('X-Content-Type-Options','nosniff');const match=/^Bearer ([A-Za-z0-9_-]{43})$/.exec(c.req.header('authorization')??'');const data=match?store.readShare(match[1]):null;return data?c.json(data.unavailable?data:{...data,minutesHtml:renderPreview(data.minutesMarkdown)}):c.json({error:'not_found'},404)});
 app.get('/favicon.svg',()=>meetingIcon());
 for(const kind of ['minutes','transcript'])app.get('/share/download/'+kind,c=>{c.header('Referrer-Policy','no-referrer');c.header('X-Content-Type-Options','nosniff');const match=/^Bearer ([A-Za-z0-9_-]{43})$/.exec(c.req.header('authorization')??'');const data=match?store.readShare(match[1]):null;if(!data||data.unavailable||!data.allowDownload||(kind==='transcript'&&typeof data.transcript!=='string'))return c.json({error:'not_found'},404);c.header('Content-Disposition',kind==='minutes'?'attachment; filename=meeting-minutes.md':'attachment; filename=meeting-transcript.txt');return c.body(kind==='minutes'?minutesForDownload(data.minutesMarkdown,data.reviewed):data.transcript,200,{'Content-Type':kind==='minutes'?'text/markdown; charset=utf-8':'text/plain; charset=utf-8'})});
 app.use('*',async(c,next)=>{c.header('Cache-Control','no-store');c.header('X-Content-Type-Options','nosniff');await next()});
 /* Optional signed-time bootstrap: no bearer token is sent on this path.
  * Authenticity comes from the device-pinned key and fresh nonce, not HTTP. */
 let timeWindow=0,timeCount=0;
 app.get('/time-proof/:nonce',c=>{
  if(!timeKeys)return c.json({error:'not_found'},404);
  const nonce=c.req.param('nonce');
  if(!/^[a-f0-9]{64}$/.test(nonce)||/^0{64}$/.test(nonce)||new URL(c.req.url).search)return c.json({error:'invalid'},400);
  const now=timeMonotonic();if(!Number.isFinite(now)||now<0)return c.json({error:'unavailable'},503);
  if(now<timeWindow||now-timeWindow>=60000){timeWindow=now;timeCount=0;}
  if(timeCount>=maxTimeProofPerMinute){c.header('Retry-After','60');return c.json({error:'busy'},429)}
  timeCount++;
  try{const proof=timeKeys.sign(Buffer.from(nonce,'hex'),timeSeconds());return c.json({keyId:proof.keyId,message:proof.message.toString('hex'),signature:proof.signature.toString('hex')})}
  catch{return c.json({error:'unavailable'},503)}
 });
 app.use('*',async(c,next)=>{const match=/^Bearer ([^\s]+)$/.exec(c.req.header('authorization')??'');if(!match)return c.json({error:'unauthorized'},401);const token=match[1];if(secureEqual(token,adminToken)){c.set('principal',{role:'admin',id:adminId});return next()}const id=store.authenticate(token);if(!id)return c.json({error:'unauthorized'},401);const active=activeDevices.get(id)??0;if(active>=maxDeviceRequests){c.header('Retry-After','1');return c.json({error:'device_busy'},429)}activeDevices.set(id,active+1);c.set('principal',{role:'device',id,token});try{return await next()}finally{const remaining=(activeDevices.get(id)??1)-1;if(remaining)activeDevices.set(id,remaining);else activeDevices.delete(id)}});
 const device=c=>{if(c.req.raw.signal.aborted)throw new StoreError('request_aborted');const p=c.get('principal');if(p.role!=='device'||store.authenticate(p.token)!==p.id)throw new StoreError('not_found');return p.id};
 const admin=c=>{if(c.get('principal').role!=='admin')throw new StoreError('not_found')};
 const subscriptionAction=(fn,id)=>{try{return fn(id)}catch(e){throw new StoreError(e.message==='not_found'?'not_found':'invalid')}};
 const subscriptionConfig=body=>{if(!body||Array.isArray(body)||typeof body!=='object'||Object.keys(body).some(k=>!['days','includeTranscript','allowDownload'].includes(k)))throw new StoreError('invalid');return body};
 app.get('/admin/subscriptions',c=>{admin(c);return c.json({subscriptions:subscriptions.list()})});
 app.get('/admin/subscription-deliveries',c=>{admin(c);return c.json({deliveries:subscriptions.deliveries()})});
 app.post('/admin/subscriptions/:id/approve',async c=>{admin(c);const body=c.req.raw.body?subscriptionConfig(await json(c.req.raw)):{};return c.json(subscriptionAction(id=>subscriptions.review(id,true,Math.floor(Date.now()/1000),body),c.req.param('id')))});
 app.patch('/admin/subscriptions/:id',async c=>{admin(c);const body=subscriptionConfig(await json(c.req.raw));return c.json(subscriptionAction(id=>subscriptions.updateShareConfig(id,body),c.req.param('id')))});
 app.post('/admin/subscriptions/:id/reject',c=>{admin(c);return c.json(subscriptionAction(id=>subscriptions.review(id,false),c.req.param('id')))});
 app.post('/admin/subscriptions/:id/revoke',c=>{admin(c);return c.json(subscriptionAction(id=>subscriptions.revoke(id),c.req.param('id')))});
 app.get('/admin/time-signer',c=>{admin(c);if(!timeKeys)return c.json({error:'not_found'},404);const info=timeKeys.getPublic();return info?c.json({origin:info.origin,keyId:info.keyId,publicKey:info.publicKey.toString('hex')}):c.json({error:'not_found'},404)});
 app.post('/admin/time-signer',async c=>{
  admin(c);if(!timeKeys)return c.json({error:'not_found'},404);
  const body=await json(c.req.raw),serverTime=timeSeconds();
  if(!body||Array.isArray(body)||typeof body!=='object'||Object.keys(body).some(k=>k!=='origin'&&k!=='verifiedEpoch')||
     typeof body.origin!=='string'||!Number.isSafeInteger(body.verifiedEpoch)||!Number.isSafeInteger(serverTime)||Math.abs(body.verifiedEpoch-serverTime)>30)return c.json({error:'invalid'},400);
  try{const info=timeKeys.initialize(body.origin,body.verifiedEpoch);return c.json({origin:info.origin,keyId:info.keyId,publicKey:info.publicKey.toString('hex')})}
  catch(e){if(e?.message==='invalid_origin'||e?.message==='invalid_time')return c.json({error:'invalid'},400);if(e?.message==='time_origin_conflict')return c.json({error:'conflict'},409);return c.json({error:'unavailable'},503)}
 });
 app.get('/admin/status',c=>{admin(c);return c.json(store.adminStatus())});
 app.get('/admin/settings',c=>{admin(c);return c.json({retentionSeconds,workerConcurrency,storageLimitBytes:store.storageLimitBytes,scope:'completed_meetings',legacyWithoutCompletionTime:'retained'})});
 app.get('/admin/deletions',c=>{admin(c);const raw=c.req.query('before');if(raw!==undefined&&!/^[1-9][0-9]*$/.test(raw))throw new StoreError('invalid');return c.json(store.adminPendingDeletions(raw===undefined?null:Number(raw)))});
 app.get('/admin/meetings',c=>{admin(c);const page=c.req.query('page'),size=c.req.query('pageSize');if(page!==undefined||size!==undefined){const offset=c.req.query('utcOffset')??'0';if(c.req.query('before')!==undefined||!(/^[1-9][0-9]*$/.test(page??'1'))||!(/^[1-9][0-9]*$/.test(size??'20'))||!(/^-?\d{1,3}$/.test(offset)))throw new StoreError('invalid');return c.json(store.adminPage({page:Number(page??1),pageSize:Number(size??20),search:c.req.query('search')??'',state:c.req.query('state')??'all',utcOffset:Number(offset)}))}const raw=c.req.query('before');if(raw!==undefined&&!/^[1-9][0-9]*$/.test(raw))throw new StoreError('invalid');return c.json(store.adminList(raw===undefined?null:Number(raw)))});
 app.post('/admin/meetings/:id/shares',async c=>{admin(c);const b=await json(c.req.raw);if(!b||Array.isArray(b)||typeof b!=='object'||Object.keys(b).some(k=>!['days','includeTranscript','allowDownload'].includes(k))||!Object.hasOwn(b,'days')||!Object.hasOwn(b,'includeTranscript')||(Object.hasOwn(b,'allowDownload')&&typeof b.allowDownload!=='boolean'))throw new StoreError('invalid');return c.json(store.createShare(c.req.param('id'),b.includeTranscript,b.days,Math.floor(Date.now()/1000),b.allowDownload??false),201)});
 app.get('/admin/meetings/:id/audio',c=>{admin(c);const audio=toOgg(store.adminAudio(c.req.param('id')));c.header('Cache-Control','no-store');c.header('X-Content-Type-Options','nosniff');c.header('Content-Disposition',`attachment; filename="${c.req.param('id')}.ogg"`);return c.body(audio,200,{'Content-Type':'audio/ogg'});});
 app.get('/admin/meetings/:id/minutes.md',c=>{admin(c);const m=store.adminGet(c.req.param('id'));if(!m.minutes&&!m.edited_minutes)throw new StoreError('conflict');c.header('Content-Disposition',`attachment; filename=\"meeting-${m.id}.md\"`);return c.body(minutesForDownload(m.edited_minutes??renderMinutesMarkdown(JSON.parse(m.minutes),{captureReason:m.reason,includeReviewNotice:false}),m.reviewed),200,{'Content-Type':'text/markdown; charset=utf-8'})});
 app.get('/admin/meetings/:id/result',c=>{admin(c);const m=store.adminResult(c.req.param('id'));c.header('Cache-Control','no-store');return c.json({id:m.id,state:m.state,reason:m.reason,reviewed:m.reviewed,reviewKey:m.reviewKey,audioIntegrity:m.audioIntegrity,captureStatus:m.reason===null?'unknown':m.reason==='user'?'ended_by_user':'truncated',recordingTime:{recordedAt:m.recorded_at,timeTrust:m.time_trust},processing:{attempts:m.attempts,retryAt:m.retry_at||null,failedStage:m.failed_stage,lastError:m.last_error,asrProgress:m.state==='asr_running'?getAsrProgress(m.id):null},transcript:m.transcript?JSON.parse(m.transcript):null,minutes:m.minutes?JSON.parse(m.minutes):null,minutesMarkdown:m.edited_minutes??(m.minutes?renderMinutesMarkdown(JSON.parse(m.minutes),{captureReason:m.reason,includeReviewNotice:false}):null),minutesHtml:(m.edited_minutes??(m.minutes?renderMinutesMarkdown(JSON.parse(m.minutes),{captureReason:m.reason,includeReviewNotice:false}):null))===null?null:renderPreview(m.edited_minutes??renderMinutesMarkdown(JSON.parse(m.minutes),{captureReason:m.reason,includeReviewNotice:false})),minutesRevision:m.minutes_revision,minutesSourceHash:m.minutes?createHash('sha256').update(m.minutes).digest('hex'):null,minutesEdited:m.edited_minutes!==null,minutesEditedStale:m.minutes_edited_stale,warning:m.reviewed?'当前纪要已人工复核。':'自动转录和纪要未经人工校对；引用匹配不能证明决定或行动项得到原文支持。'});});
 app.post('/admin/meetings/:id/review',async c=>{admin(c);const b=await json(c.req.raw);if(!b||Array.isArray(b)||Object.keys(b).join(',')!=='reviewKey')throw new StoreError('invalid');return c.json(store.markReviewed(c.req.param('id'),b.reviewKey))});
 app.put('/admin/meetings/:id/minutes',async c=>{admin(c);const body=await json(c.req.raw,1001000);if(!body||Object.keys(body).some(k=>!['markdown','revision','sourceHash'].includes(k)))throw new StoreError('invalid');return c.json({...store.adminSaveMinutes(c.req.param('id'),body.markdown,body.revision,body.sourceHash),html:renderPreview(body.markdown)});});
 app.get('/admin/meetings/:id/attempts',c=>{admin(c);return c.json({attempts:store.adminAttempts(c.req.param('id'))})});
 app.get('/admin/meetings/:id/versions',c=>{admin(c);return c.json(store.adminVersions(c.req.param('id')))});
 app.post('/admin/meetings/:id/retranscribe',c=>{admin(c);store.adminRetranscribe(c.req.param('id'));return c.json({ok:true})});
 app.get('/admin/meetings/:id',c=>{admin(c);return c.json(store.adminGet(c.req.param('id')))});
 app.post('/admin/meetings/:id/retry',c=>{admin(c);store.adminRetry(c.req.param('id'));return c.json({ok:true})});
 app.delete('/admin/meetings/:id',c=>{admin(c);store.adminBeginDelete(c.req.param('id'));onDelete(c.req.param('id'));store.finishDelete(c.req.param('id'));return c.json({state:'deleted'})});
 app.post('/devices',async c=>{admin(c);const b=await json(c.req.raw);if(!b||Object.keys(b).some(k=>k!=='id'))throw new StoreError('invalid');const token=randomBytes(32).toString('base64url');store.registerDevice(b.id,token);c.header('Cache-Control','no-store');return c.json({id:b.id,token,credentialId:createHash('sha256').update(token).digest('hex')},201)});
 app.get('/devices/:id/revocations',c=>{admin(c);c.header('Cache-Control','no-store');return c.json({events:store.revocationEvents(c.req.param('id'))})});
 app.get('/devices/:id',c=>{admin(c);c.header('Cache-Control','no-store');return c.json(store.deviceStatus(c.req.param('id')))});
 app.post('/devices/:id/revoke',async c=>{admin(c);const b=await json(c.req.raw);const result=store.revokeDevice(c.req.param('id'),b?.credentialId,b?.eventId,c.get('principal').id);return c.json(result)});
 app.post('/meetings/:id',c=>{store.create(device(c),c.req.param('id'),c.get('principal').token);return c.json({ok:true},201)});
 app.get('/meetings/:id/repair-status',c=>c.json(store.repairStatus(device(c),c.req.param('id'),c.get('principal').token)));
 app.get('/meetings/:id/chunks-window/:first',c=>{const first=c.req.param('first');if(!/^(0|[1-9][0-9]*)$/.test(first))throw new StoreError('invalid');return c.json(store.chunkWindow(device(c),c.req.param('id'),Number(first),c.get('principal').token))});
 app.get('/meetings/:id',c=>c.json(store.list(device(c),c.req.param('id'),c.get('principal').token,validateUploadedAudio)));
 app.put('/meetings/:id/chunks/:seq',async c=>{const seq=c.req.param('seq');if(!/^(0|[1-9][0-9]*)$/.test(seq))throw new StoreError('invalid');const bytes=await bounded(c.req.raw,262144);const created=store.put(device(c),c.req.param('id'),Number(seq),bytes,c.req.header('x-content-sha256')??'',c.get('principal').token);return c.json({created:created===true,repaired:created==='repaired'},created===true?201:200)});
 app.post('/meetings/:id/finalize',async c=>{const body=await json(c.req.raw);return c.json(store.finalize(device(c),c.req.param('id'),body,c.get('principal').token,validateUploadedAudio))});
 app.post('/meetings/:id/cancel',c=>c.json(store.cancel(device(c),c.req.param('id'),c.get('principal').token,validateUploadedAudio)));
 return app;
}
