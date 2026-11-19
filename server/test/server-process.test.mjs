import {test} from 'node:test';
import {strict as assert} from 'node:assert';
import {mkdtempSync,rmSync,readFileSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {spawn,spawnSync} from 'node:child_process';
import {createServer} from 'node:net';
import {request} from 'node:https';
import {once} from 'node:events';
import {createHash,createPublicKey,verify} from 'node:crypto';
import {sampleOgg} from './fixtures.mjs';

const listen=server=>new Promise((resolve,reject)=>{server.once('error',reject);server.listen(0,'127.0.0.1',resolve)});
const call=(port,path,headers={},ca,method='GET',data)=>new Promise((resolve,reject)=>{const req=request({hostname:'localhost',family:4,port,path,headers,ca,method,timeout:5000},res=>{let body='';res.setEncoding('utf8');res.on('data',s=>body+=s);res.on('end',()=>resolve({status:res.statusCode,body}))});req.on('timeout',()=>req.destroy(new Error('timeout')));req.on('error',reject);req.end(data)});
const dropBody=(port,path,headers,ca,data)=>new Promise((resolve,reject)=>{const req=request({hostname:'localhost',family:4,port,path,headers,ca,method:'POST',timeout:5000},res=>{const status=res.statusCode;res.destroy();resolve(status)});req.on('error',reject);req.on('timeout',()=>req.destroy(new Error('timeout')));req.end(data)});
const dropUpload=(port,path,headers,ca,data)=>new Promise((resolve,reject)=>{const req=request({hostname:'localhost',family:4,port,path,headers:{...headers,'content-length':data.length},ca,method:'PUT',timeout:5000});req.on('error',e=>{if(e.code==='ECONNRESET')resolve();else reject(e)});req.on('timeout',()=>req.destroy(new Error('timeout')));req.write(data.subarray(0,Math.max(1,Math.floor(data.length/2))),()=>setTimeout(()=>req.destroy(),30))});
test('actual HTTPS process enforces authentication and recovers upload and confirmation after crashes', {timeout:20000},async t=>{
 if(spawnSync('openssl',['version'],{stdio:'ignore'}).status!==0){t.skip('openssl required');return}
 const probe=createServer();try{await listen(probe)}catch(e){if(e.code==='EPERM'||e.code==='EACCES'){t.skip('local TCP listening not permitted');return}throw e}const port=probe.address().port;await new Promise(r=>probe.close(r));
 const dir=mkdtempSync(join(tmpdir(),'meeting-server-process-')),cert=join(dir,'cert.pem'),key=join(dir,'key.pem'),admin='a'.repeat(40);let proc;
 try{
  assert.equal(spawnSync('openssl',['req','-x509','-newkey','rsa:2048','-nodes','-keyout',key,'-out',cert,'-days','1','-subj','/CN=localhost','-addext','subjectAltName=DNS:localhost'],{stdio:'ignore'}).status,0);
  const env={...process.env,MEETING_DATA_DIR:join(dir,'data'),MEETING_TLS_CERT:cert,MEETING_TLS_KEY:key,MEETING_PORT:String(port),MEETING_HOST:'127.0.0.1',MEETING_WORKER_CONCURRENCY:'1',MEETING_ADMIN_TOKEN:admin,ZERONE_AGENT_HTTP_API_KEY:'r'.repeat(40),MEETING_ASR_API_KEY:'offline-asr',MEETING_MINUTES_API_KEY:'offline-minutes'};
  const start=async()=>{proc=spawn(process.execPath,['src/index.mjs'],{cwd:new URL('..',import.meta.url),env,stdio:['ignore','pipe','pipe']});let output='';proc.stderr.on('data',d=>output+=d);await new Promise((resolve,reject)=>{const timer=setTimeout(()=>reject(new Error('server startup timeout: '+output)),8000);proc.stdout.on('data',d=>{if(d.toString().includes('meeting service listening')){clearTimeout(timer);resolve()}});proc.once('exit',code=>{clearTimeout(timer);reject(new Error('server exited '+code+': '+output))})})};
  const stop=async()=>{const done=once(proc,'exit');proc.kill('SIGTERM');const [code,signal]=await done;assert.equal(signal,null);assert.equal(code,0);proc=null};
  const crash=async()=>{const done=once(proc,'exit');proc.kill('SIGKILL');const [code,signal]=await done;assert.equal(code,null);assert.equal(signal,'SIGKILL');proc=null};
  await start();await assert.rejects(call(port,'/meeting-service/admin/settings'),/self.signed|certificate/i);
  const ca=readFileSync(cert);assert.equal((await call(port,'/meeting-service/admin/settings',{},ca)).status,401);assert.equal((await call(port,'/v1/models',{authorization:`Bearer ${'r'.repeat(40)}`},ca)).status,404);
  const settings=await call(port,'/meeting-service/admin/settings',{authorization:`Bearer ${admin}`},ca);assert.equal(settings.status,200);assert.equal(JSON.parse(settings.body).retentionSeconds,null);assert.equal(JSON.parse(settings.body).workerConcurrency,1);
  const nonce='01'.repeat(32),timePath='/meeting-service/time-proof/'+nonce;
  assert.equal((await call(port,timePath,{},ca)).status,503);
  const provision=JSON.stringify({origin:`https://localhost:${port}`,verifiedEpoch:Math.floor(Date.now()/1000)});
  assert.equal((await call(port,'/meeting-service/admin/time-signer',{'content-type':'application/json'},ca,'POST',provision)).status,401);
  const initialized=await call(port,'/meeting-service/admin/time-signer',{authorization:`Bearer ${admin}`,'content-type':'application/json'},ca,'POST',provision);assert.equal(initialized.status,200);
  const publicInfo=JSON.parse(initialized.body),point=Buffer.from(publicInfo.publicKey,'hex');assert.equal(point.length,65);
  const timeProof=await call(port,timePath,{},ca);assert.equal(timeProof.status,200);const signed=JSON.parse(timeProof.body);
  const publicKey=createPublicKey({key:{kty:'EC',crv:'P-256',x:point.subarray(1,33).toString('base64url'),y:point.subarray(33).toString('base64url')},format:'jwk'});
  assert.equal(signed.keyId,publicInfo.keyId);assert.ok(verify('sha256',Buffer.from(signed.message,'hex'),publicKey,Buffer.from(signed.signature,'hex')));
  const registered=await call(port,'/meeting-service/devices',{authorization:`Bearer ${admin}`,'content-type':'application/json'},ca,'POST',JSON.stringify({id:'device-1'}));assert.equal(registered.status,201);const token=JSON.parse(registered.body).token,device={authorization:`Bearer ${token}`},audio=sampleOgg(),digest=createHash('sha256').update(audio).digest('hex'),path='/meeting-service/meetings/meeting-1';
  assert.equal((await call(port,path,device,ca,'POST')).status,201);await dropUpload(port,path+'/chunks/0',{...device,'x-content-sha256':digest},ca,audio);const interrupted=JSON.parse((await call(port,path,device,ca)).body);assert.equal(interrupted.chunks.length,0);assert.equal((await call(port,path+'/chunks/0',{...device,'x-content-sha256':digest},ca,'PUT',audio)).status,201);
  await crash();await start();const prior=JSON.parse((await call(port,path,device,ca)).body);assert.equal(prior.state,'uploading');assert.equal(prior.chunks[0].digest,digest);
  const afterRestart=JSON.parse((await call(port,'/meeting-service/admin/time-signer',{authorization:`Bearer ${admin}`},ca)).body);assert.deepEqual(afterRestart,publicInfo);
  assert.equal((await call(port,'/meeting-service/time-proof/'+'02'.repeat(32),{},ca)).status,200);
  const manifest={count:1,bytes:audio.length,digest,reason:'user'};const status=await dropBody(port,path+'/finalize',{...device,'content-type':'application/json'},ca,JSON.stringify(manifest));assert.equal(status,200);
  await crash();await start();const recovered=JSON.parse((await call(port,path,device,ca)).body);assert.equal(recovered.committed,true);assert.deepEqual(recovered.manifest,{...manifest,recordedAt:null,timeTrust:'uncalibrated'});const retry=await call(port,path+'/finalize',{...device,'content-type':'application/json'},ca,'POST',JSON.stringify(manifest));assert.equal(retry.status,200);assert.deepEqual(JSON.parse(retry.body).manifest,recovered.manifest);await stop();
 }finally{if(proc){proc.kill('SIGKILL');await once(proc,'exit').catch(()=>{})}rmSync(dir,{recursive:true,force:true})}
});
test('HTTP mode starts without certificates and retains authentication', {timeout:15000},async()=>{
 const probe=createServer();await listen(probe);const port=probe.address().port;await new Promise(r=>probe.close(r));const dir=mkdtempSync(join(tmpdir(),'meeting-http-')),admin='meeting-recorder';let proc;
 try{const env={...process.env,MEETING_DATA_DIR:join(dir,'data'),MEETING_HTTP:'true',MEETING_PORT:String(port),MEETING_HOST:'127.0.0.1',MEETING_ADMIN_TOKEN:admin,ZERONE_AGENT_HTTP_API_KEY:'r'.repeat(40),MEETING_ASR_API_KEY:'offline-asr',MEETING_MINUTES_API_KEY:'offline-minutes'};delete env.MEETING_TLS_CERT;delete env.MEETING_TLS_KEY;proc=spawn(process.execPath,['src/index.mjs'],{cwd:new URL('..',import.meta.url),env,stdio:['ignore','pipe','pipe']});let errors='';proc.stderr.on('data',d=>errors+=d);await new Promise((resolve,reject)=>{const timer=setTimeout(()=>reject(new Error(errors||'startup timeout')),8000);proc.stdout.on('data',d=>{if(d.toString().includes('meeting service listening')){clearTimeout(timer);resolve()}});proc.once('exit',code=>{clearTimeout(timer);reject(new Error('exit '+code+errors))})});const {request: http}=await import('node:http');const get=headers=>new Promise((resolve,reject)=>{const req=http({hostname:'127.0.0.1',port,path:'/meeting-service/admin/settings',headers},res=>{res.resume();res.on('end',()=>resolve(res.statusCode))});req.on('error',reject);req.end()});assert.equal(await get({}),401);assert.equal(await get({authorization:`Bearer ${admin}`}),200);const root=await fetch(`http://127.0.0.1:${port}/`,{redirect:'manual'});assert.equal(root.status,302);assert.equal(root.headers.get('location'),'/meeting-service/ui');assert.equal((await fetch(`http://127.0.0.1:${port}/meeting-service/ui`)).status,200);assert.equal((await fetch(`http://127.0.0.1:${port}/health`)).status,200);const done=once(proc,'exit');proc.kill('SIGTERM');assert.equal((await done)[0],0);proc=null;
 }finally{if(proc){proc.kill('SIGKILL');await once(proc,'exit').catch(()=>{})}rmSync(dir,{recursive:true,force:true})}
});
