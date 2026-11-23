import {sampleChatCompletionStream} from './fixtures.mjs';
import {test} from 'node:test';
import {strict as assert} from 'node:assert';
import {mkdtempSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {createRuntime} from '@zerone-agent/agent-runtime';
import {registerMinutesAgent} from '../src/register-agent.mjs';
import {createRuntimeMinutes} from '../src/runtime-minutes.mjs';

test('real runtime invokes OpenAI-compatible provider and returns validated JSON transport',async()=>{
 const oldFetch=globalThis.fetch,oldKey=process.env.ZERONE_AGENT_HTTP_API_KEY;const dir=mkdtempSync(join(tmpdir(),'meeting-runtime-'));const key='k'.repeat(40);process.env.ZERONE_AGENT_HTTP_API_KEY=key;let upstreamRequest,finishReason='stop';
 globalThis.fetch=async(url,options)=>{
  assert.match(String(url),/\/chat\/completions$/);
  upstreamRequest=JSON.parse(options.body);
  assert.equal(upstreamRequest.model,'step-3.7-flash');
  assert.ok(!upstreamRequest.tools||upstreamRequest.tools.length===0);
  const text=finishReason==='length'
   ? '{"summary":"未完成'
   : JSON.stringify({summary:'测试',discussion:[],decisions:[],actions:[],openQuestions:[]});
  return sampleChatCompletionStream(text,{finishReason,outputTokens:finishReason==='length'?65_536:20_000});
 };

 let host;try{
  host=await createRuntime({server:{host:'127.0.0.1',port:0},agents:[{id:'minutes',description:'test',model:'step-3.7-flash',apiType:'openai-completions',baseURL:'https://invalid.test/v1',apiKey:'fixture',allowedTools:[],maxTurns:2,systemPrompt:'Return JSON'}]}, {configDir:dir});registerMinutesAgent(host,{apiKey:'fixture',baseURL:'https://invalid.test/v1'});await host.start();
  const original=host.app.request.bind(host.app);host.app.request=async(...args)=>{const r=await original(...args);if(!r.ok)console.error('runtime fixture response',r.status,await r.clone().text());return r};const run=createRuntimeMinutes(host,key);const out=await run({versionId:'v1',segments:[{id:'s0',text:'测试'}]});assert.equal(out.summary,'测试');assert.equal(upstreamRequest.max_tokens,65_536);assert.equal(upstreamRequest.stream,true);finishReason='length';await assert.rejects(()=>run({versionId:'v2',segments:[{id:'s0',text:'测试'}]}),/minutes_invalid_json/);
 }finally{if(host)await host.stop();globalThis.fetch=oldFetch;if(oldKey===undefined)delete process.env.ZERONE_AGENT_HTTP_API_KEY;else process.env.ZERONE_AGENT_HTTP_API_KEY=oldKey;rmSync(dir,{recursive:true,force:true})}
});
test('concurrent runtime sessions keep meeting inputs and results isolated',{timeout:5000},async()=>{
 const oldFetch=globalThis.fetch,oldKey=process.env.ZERONE_AGENT_HTTP_API_KEY,dir=mkdtempSync(join(tmpdir(),'meeting-runtime-parallel-')),key='k'.repeat(40);process.env.ZERONE_AGENT_HTTP_API_KEY=key;let host,calls=0,release;const barrier=new Promise(r=>release=r);const seen=[];
 globalThis.fetch=async(_url,options)=>{const body=JSON.parse(options.body),messages=JSON.stringify(body.messages),first=messages.includes('甲会议独特内容'),second=messages.includes('乙会议独特内容');assert.notEqual(first,second);seen.push(first?'甲':'乙');if(++calls===2)release();await Promise.race([barrier,new Promise((_,reject)=>{const timer=setTimeout(()=>reject(Error('parallel_timeout')),2000);timer.unref()})]);return sampleChatCompletionStream(JSON.stringify({summary:first?'甲会议纪要':'乙会议纪要',discussion:[],decisions:[],actions:[],openQuestions:[]}))};
 try{host=await createRuntime({server:{host:'127.0.0.1',port:0},agents:[{id:'minutes',description:'test',model:'step-3.7-flash',apiType:'openai-completions',baseURL:'https://invalid.test/v1',apiKey:'fixture',allowedTools:[],maxTurns:2}]},{configDir:dir});registerMinutesAgent(host,{apiKey:'fixture',baseURL:'https://invalid.test/v1'});await host.start();const run=createRuntimeMinutes(host,key);const [first,second]=await Promise.all([run({versionId:'one',segments:[{id:'s0',text:'甲会议独特内容'}]}),run({versionId:'two',segments:[{id:'s0',text:'乙会议独特内容'}]})]);assert.equal(first.summary,'甲会议纪要');assert.equal(second.summary,'乙会议纪要');assert.equal(seen.length,2);assert.equal(new Set(seen).size,2)}finally{if(host)await host.stop();globalThis.fetch=oldFetch;if(oldKey===undefined)delete process.env.ZERONE_AGENT_HTTP_API_KEY;else process.env.ZERONE_AGENT_HTTP_API_KEY=oldKey;rmSync(dir,{recursive:true,force:true})}
});
