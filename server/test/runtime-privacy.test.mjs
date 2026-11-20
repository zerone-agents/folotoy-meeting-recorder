import {test} from 'node:test';
import {strict as assert} from 'node:assert';
import {mkdtempSync,rmSync,readdirSync,readFileSync,statSync} from 'node:fs';
import {tmpdir} from 'node:os';import {join} from 'node:path';
import {createRuntime} from '@zerone-agent/agent-runtime';
import {registerMinutesAgent} from '../src/register-agent.mjs';
import {createRuntimeMinutes} from '../src/runtime-minutes.mjs';

test('configured runtime keeps synthetic transcript out of storage and regular logs',async()=>{
 const dir=mkdtempSync(join(tmpdir(),'meeting-runtime-privacy-')),oldFetch=globalThis.fetch,oldKey=process.env.ZERONE_AGENT_HTTP_API_KEY,key='r'.repeat(40),sentinel='MEETING_PRIVATE_SENTINEL_87246';process.env.ZERONE_AGENT_HTTP_API_KEY=key;let host;const logs=[],original={log:console.log,warn:console.warn,error:console.error};
 for(const name of Object.keys(original))console[name]=(...args)=>logs.push(args.map(String).join(' '));
 globalThis.fetch=async()=>Response.json({error:{message:sentinel}},{status:403});
 try{host=await createRuntime({server:{host:'127.0.0.1',port:0},agents:[{id:'minutes',description:'test',model:'step-3.7-flash',apiType:'openai-completions',baseURL:'https://invalid.test/v1',apiKey:'fixture',allowedTools:[],maxTurns:2}]},{configDir:dir});registerMinutesAgent(host,{apiKey:'fixture',baseURL:'https://invalid.test/v1'});await host.start();await assert.rejects(()=>createRuntimeMinutes(host,key)({versionId:'v1',segments:[{id:'s0',text:sentinel}]}));await host.stop();host=null;
  const diagnostics=logs.filter(line=>line.includes('"event":"minutes_runtime_failure"'));assert.equal(diagnostics.length,1);assert.ok(JSON.parse(diagnostics[0]).request.body.includes(sentinel));assert.equal(logs.filter(line=>!diagnostics.includes(line)).join('\n').includes(sentinel),false);assert.equal(logs.join('\n').includes(key),false);const walk=folder=>readdirSync(folder).flatMap(name=>{const path=join(folder,name);return statSync(path).isDirectory()?walk(path):[path]});for(const path of walk(dir))assert.equal(readFileSync(path).includes(Buffer.from(sentinel)),false);
 }finally{if(host)await host.stop();globalThis.fetch=oldFetch;if(oldKey===undefined)delete process.env.ZERONE_AGENT_HTTP_API_KEY;else process.env.ZERONE_AGENT_HTTP_API_KEY=oldKey;for(const name of Object.keys(original))console[name]=original[name];rmSync(dir,{recursive:true,force:true})}
});
