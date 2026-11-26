import {createAdaptorServer} from '@hono/node-server';
import {test} from 'node:test';import {strict as a} from 'node:assert';import {EventEmitter} from 'node:events';import {mkdtempSync,rmSync,readFileSync} from 'node:fs';import {tmpdir} from 'node:os';import {join} from 'node:path';import {spawnSync} from 'node:child_process';import {portFromEnv,requireToken,workerConcurrencyFromEnv} from '../src/config.mjs';import {meetingServerTimeouts,validateTls,waitForListening} from '../src/startup.mjs';
test('port and credentials reject malformed values before service start',()=>{a.equal(portFromEnv(),3443);a.equal(portFromEnv('443'),443);for(const value of ['','0','65536','1.5',' 443','-1'])a.throws(()=>portFromEnv(value),/MEETING_PORT/);a.throws(()=>requireToken({KEY:'abc def'},'KEY'),/credential/);a.equal(requireToken({KEY:'abc'},'KEY'),'abc')});
test('listener startup waits for success and rejects bind failure',async()=>{const ok=new EventEmitter(),yes=waitForListening(ok);ok.emit('listening');await yes;const broken=new EventEmitter(),no=waitForListening(broken);broken.emit('error',new Error('bind_failed'));await a.rejects(no,/bind_failed/)});
test('TLS validation rejects mismatched keys and invalid dates',t=>{if(spawnSync('openssl',['version'],{stdio:'ignore'}).status!==0){t.skip('openssl required');return}const dir=mkdtempSync(join(tmpdir(),'meeting-tls-'));try{const cert=join(dir,'cert'),key=join(dir,'key'),other=join(dir,'other'),otherCert=join(dir,'othercert');for(const [k,c] of [[key,cert],[other,otherCert]])a.equal(spawnSync('openssl',['req','-x509','-newkey','rsa:2048','-nodes','-keyout',k,'-out',c,'-days','1','-subj','/CN=localhost'],{stdio:'ignore'}).status,0);const c=readFileSync(cert),k=readFileSync(key);a.doesNotThrow(()=>validateTls(c,k));a.throws(()=>validateTls(c,readFileSync(other)));a.throws(()=>validateTls(c,k,Date.UTC(1999,0,1)),/not currently valid/)}finally{rmSync(dir,{recursive:true,force:true})}});

test('worker concurrency is explicit and bounded',()=>{a.equal(workerConcurrencyFromEnv(),2);for(const n of ['1','2','3','4'])a.equal(workerConcurrencyFromEnv(n),Number(n));for(const n of ['','0','5','01','1.5',' 2'])a.throws(()=>workerConcurrencyFromEnv(n),/MEETING_WORKER_CONCURRENCY/)});

test('the HTTP receive budget covers the whole-file upload deadline', () => {
 const server = createAdaptorServer({fetch: () => new Response(null), serverOptions: meetingServerTimeouts});
 a.equal(server.requestTimeout, 180000);
 a.equal(server.headersTimeout, 15000);
 a.equal(server.keepAliveTimeout, 5000);
});
