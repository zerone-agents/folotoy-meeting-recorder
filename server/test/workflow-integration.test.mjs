import {sampleOgg} from './fixtures.mjs';
import {test} from 'node:test';
import {strict as assert} from 'node:assert';
import {mkdtempSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {createHash} from 'node:crypto';
import {createRuntime} from '@zerone-agent/agent-runtime';
import {MeetingStore} from '../src/store.mjs';
import {createMeetingRouter} from '../src/routes.mjs';
import {registerMinutesAgent} from '../src/register-agent.mjs';
import {createRuntimeMinutes} from '../src/runtime-minutes.mjs';
import {transcribe} from '../src/asr.mjs';
import {drainOnce} from '../src/worker.mjs';

test('complete upload survives restart and runs through real runtime with fixture providers',async()=>{
 const dir=mkdtempSync(join(tmpdir(),'meeting-workflow-')),path=join(dir,'db'),oldFetch=globalThis.fetch,oldKey=process.env.ZERONE_AGENT_HTTP_API_KEY;
 const runtimeKey='r'.repeat(40),admin='a'.repeat(40),token='d'.repeat(40),audio=sampleOgg(),hash=createHash('sha256').update(audio).digest('hex');process.env.ZERONE_AGENT_HTTP_API_KEY=runtimeKey;
 const minutesProvider={apiKey:'fixture',baseURL:'https://minutes.fixture.invalid/custom/v1',model:'custom-minutes'};
 let host,store;const calls=[];
 globalThis.fetch=async(url,options)=>{const u=String(url);calls.push(u);if(u.endsWith('/audio/asr/sse')){const body=JSON.parse(options.body);assert.equal(body.audio.input.transcription.model,'stepaudio-2.5-asr');assert.deepEqual(Buffer.from(body.audio.data,'base64'),audio);return new Response('data: {"type":"transcript.text.delta","delta":"测试"}\n\ndata: {"type":"transcript.text.done","text":"测试会议"}\n\n',{headers:{'content-type':'text/event-stream'}})}
  assert.equal(u,`${minutesProvider.baseURL}/chat/completions`);const body=JSON.parse(options.body);assert.equal(body.model,minutesProvider.model);assert.ok(!body.tools||body.tools.length===0);return Response.json({id:'fixture',object:'chat.completion',created:1,model:body.model,choices:[{index:0,message:{role:'assistant',content:JSON.stringify({summary:'测试会议',discussion:[],decisions:[],actions:[],openQuestions:[]})},finish_reason:'stop'}],usage:{prompt_tokens:1,completion_tokens:1,total_tokens:2}})};
 try {
  store=new MeetingStore(path);store.registerDevice('device',token);let app=createMeetingRouter(store,{adminToken:admin});const request=(path,key,options={})=>app.request(path,{...options,headers:{authorization:`Bearer ${key}`,...options.headers}});
  assert.equal((await request('/meetings/m',token,{method:'POST'})).status,201);assert.equal((await request('/meetings/m/chunks/0',token,{method:'PUT',body:audio,headers:{'x-content-sha256':hash}})).status,201);
  assert.equal((await request('/meetings/m/finalize',token,{method:'POST',body:JSON.stringify({count:1,bytes:audio.length,digest:hash,reason:'user'})})).status,200);
  store.close();store=new MeetingStore(path);assert.equal(store.list('device','m').state,'queued');
  host=await createRuntime({server:{host:'127.0.0.1',port:0},agents:[{id:'minutes',description:'test',...minutesProvider,apiType:'openai-completions',allowedTools:[],maxTurns:2}]},{configDir:dir});registerMinutesAgent(host,minutesProvider);await host.start();
  const adapters={asr:(bytes,{signal})=>transcribe(bytes,{apiKey:'fixture',baseURL:'https://fixture.invalid/v1',signal}),minutes:createRuntimeMinutes(host,runtimeKey)};await drainOnce(store,adapters);await drainOnce(store,adapters);assert.equal(store.list('device','m').state,'completed');
  app=createMeetingRouter(store,{adminToken:admin});const response=await request('/admin/meetings/m/result',admin);const result=await response.json();assert.equal(result.transcript.segments[0].text,'测试会议');assert.equal(result.minutes.summary,'测试会议');assert.equal(calls.length,2);
  const exported=await request('/admin/meetings/m/audio',admin);assert.deepEqual(Buffer.from(await exported.arrayBuffer()),audio);
 }finally{if(host)await host.stop();if(store)store.close();globalThis.fetch=oldFetch;if(oldKey===undefined)delete process.env.ZERONE_AGENT_HTTP_API_KEY;else process.env.ZERONE_AGENT_HTTP_API_KEY=oldKey;rmSync(dir,{recursive:true,force:true})}
});
