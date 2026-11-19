/** Live provider probe; sends the explicitly selected local recording to StepFun. */
import { createRuntime } from '@zerone-agent/agent-runtime';
import { createHash,randomBytes } from 'node:crypto';
import { mkdtempSync,readFileSync,rmSync } from 'node:fs';
import { join,resolve } from 'node:path';
import { tmpdir } from 'node:os';
import { MeetingStore } from './store.mjs';
import { createMeetingRouter } from './routes.mjs';
import { transcribe } from './asr.mjs';
import { registerMinutesAgent } from './register-agent.mjs';
import { createRuntimeMinutes } from './runtime-minutes.mjs';
import { drainOnce } from './worker.mjs';
import {renderMinutesMarkdown} from './markdown-minutes.mjs';

const apiKey=process.env.STEPFUN_API_KEY;
const audioPath=process.argv[2], distinctMarkers=process.argv[3]==='--distinct-markers',noReference=process.argv[3]==='--no-reference';
if(process.argv.length>4 || (process.argv[3] && !distinctMarkers && !noReference))throw Error('invalid_probe_mode');
if(!apiKey||!audioPath){console.error('Set STEPFUN_API_KEY securely and provide an audio file path. This sends the file to StepFun.');process.exit(2)}
const audio=readFileSync(resolve(audioPath));if(!audio.length||audio.length>20000000)throw Error('invalid_audio_size');
const expected='今天是会议录音测试。我们讨论三个问题：第一，确认产品需求；第二，安排开发计划；第三，检查交付结果。项目负责人是小王，预计十一月十五日完成第一版。会议结束后，请把录音上传，生成文字和会议纪要。';
function characterError(reference,text){const norm=s=>Array.from(s.replace(/[\p{P}\p{S}\s]/gu,''));const a=norm(reference),b=norm(text);let prior=Array.from({length:b.length+1},(_,i)=>i);for(let i=0;i<a.length;i++){const next=[i+1];for(let j=0;j<b.length;j++)next.push(Math.min(prior[j+1]+1,next[j]+1,prior[j]+(a[i]===b[j]?0:1)));prior=next}return {referenceCharacters:a.length,editDistance:prior[b.length],characterErrorRate:Math.round(prior[b.length]/a.length*1000)/1000}}
const dir=mkdtempSync(join(tmpdir(),'meeting-live-')),runtimeKey=randomBytes(32).toString('base64url'),admin=randomBytes(32).toString('base64url'),device=randomBytes(32).toString('base64url'),oldKey=process.env.ZERONE_AGENT_HTTP_API_KEY;process.env.ZERONE_AGENT_HTTP_API_KEY=runtimeKey;
let host,store;
try{
 store=new MeetingStore(join(dir,'meeting.sqlite'));store.registerDevice('fixture-device',device);const app=createMeetingRouter(store,{adminToken:admin});
 const req=(path,method,body,headers={})=>app.request(path,{method,body,headers:{authorization:`Bearer ${device}`,...headers}});
 const digest=createHash('sha256').update(audio).digest('hex');let response=await req('/meetings/live','POST');if(!response.ok)throw Error('create_failed');const chunkSize=262144;let count=0;for(let offset=0;offset<audio.length;offset+=chunkSize){const chunk=audio.subarray(offset,offset+chunkSize),hash=createHash('sha256').update(chunk).digest('hex');response=await req(`/meetings/live/chunks/${count++}`,'PUT',chunk,{'x-content-sha256':hash});if(!response.ok)throw Error('upload_failed')}
 response=await req('/meetings/live/finalize','POST',JSON.stringify({count,bytes:audio.length,digest,reason:'user'}));if(!response.ok)throw Error('finalize_failed');
 const config={server:{host:'127.0.0.1',port:0},agents:[{id:'minutes',description:'Live probe',model:'step-3.7-flash',apiType:'openai-completions',baseURL:'https://api.stepfun.com/step_plan/v1',apiKey,allowedTools:[],maxTurns:2}]};
 host=await createRuntime(config,{configDir:dir});registerMinutesAgent(host,{apiKey});await host.start();let deltaEvents=0;const asrProgress=new Map();const adapters={asrProgress,asr:(bytes,{signal,onDelta})=>transcribe(bytes,{apiKey,baseURL:process.env.MEETING_ASR_BASE_URL,model:process.env.MEETING_ASR_MODEL,signal,onDelta:()=>{onDelta();deltaEvents=asrProgress.get('live')?.deltaEvents??0}}),minutes:createRuntimeMinutes(host,runtimeKey),requestTimeoutMs:180000};
 await drainOnce(store,adapters);let meeting=store.adminGet('live');if(meeting.state!=='minutes_queued')throw Error(`asr_${meeting.last_error||meeting.state}`);const transcript=JSON.parse(meeting.transcript).segments.map(x=>x.text).join('');
 let minutesAttempts=0;do{await drainOnce(store,adapters);minutesAttempts++;meeting=store.adminGet('live');if(meeting.state==='completed'||meeting.state==='failed'||!noReference)break;store.db.prepare('UPDATE meetings SET retry_at=0 WHERE id=?').run('live');}while(minutesAttempts<3);if(meeting.state!=='completed')throw Error(`minutes_${meeting.last_error||meeting.state}`);const minutes=JSON.parse(meeting.minutes),markdown=renderMinutesMarkdown(minutes);if(!markdown.startsWith('# 会议纪要：')||!markdown.includes('| 事项 | 负责方 | 优先级 |')||!markdown.includes('关键结论'))throw Error('minutes_invalid_format');
 const markers=['会议开头标记，青苹果已经放在桌子上','会议中间标记，蓝色火车将在星期三出发','会议结尾标记，紫色风筝最后飞过河流'];
 const positions=markers.map(x=>transcript.indexOf(x));const asr=distinctMarkers?{deltaEvents,markersPresent:positions.map(p=>p>=0),ordered:positions.every((p,i)=>p>=0&&(i===0||p>positions[i-1]))}:noReference?{deltaEvents,transcriptCharacters:Array.from(transcript).length}:{deltaEvents,...characterError(expected,transcript)};
 const passed=!distinctMarkers||asr.ordered;
 if(!passed)process.exitCode=1;
 console.log(JSON.stringify({status:passed?'passed':'failed',audioBytes:audio.length,asr,minutes:{attempts:minutesAttempts,markdownFormat:true,summaryPresent:Boolean(minutes.summary),discussion:minutes.discussion.length,decisions:minutes.decisions.length,actions:minutes.actions.length,openQuestions:minutes.openQuestions.length,evidenceValidated:true}}));
}catch(error){const message=String(error?.message||'unknown');const safe=/^(?:create_failed|upload_failed|finalize_failed|(?:asr|minutes)_[a-z0-9_]+)$/.test(message)?message:'live_probe_failed';console.error(JSON.stringify({status:'failed',error:safe}));process.exitCode=1}
finally{if(host)await host.stop();if(store)store.close();if(oldKey===undefined)delete process.env.ZERONE_AGENT_HTTP_API_KEY;else process.env.ZERONE_AGENT_HTTP_API_KEY=oldKey;rmSync(dir,{recursive:true,force:true})}
