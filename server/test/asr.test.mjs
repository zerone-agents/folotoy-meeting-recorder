import { test } from 'node:test';import { strict as a } from 'node:assert';import { transcribe } from '../src/asr.mjs';
const fake=(chunks)=>async()=>new Response(new ReadableStream({start(c){for(const x of chunks)c.enqueue(new TextEncoder().encode(x));c.close()}}),{status:200});
test('accepts split UTF-8 SSE and uses final text',async()=>{let result=await transcribe(Buffer.from('audio'),{apiKey:'test',fetchImpl:fake(['data: {"type":"transcript.text.delta","delta":"旧"}\n\n','data: {"type":"transcript.text.done",','"text":"最终"}\r\n\r\n'])});a.equal(result,'最终')});
test('rejects incomplete stream and provider errors',async()=>{await a.rejects(()=>transcribe(Buffer.from('x'),{apiKey:'test',fetchImpl:fake(['data: {"type":"transcript.text.delta","delta":"partial"}\n\n'])}),/asr_incomplete/);await a.rejects(()=>transcribe(Buffer.from('x'),{apiKey:'test',fetchImpl:fake(['data: {"type":"error"}\n\n'])}),/asr_provider_error/)});
test('rejects oversized multiline SSE events',async()=>{const lines=Array.from({length:1100},()=>`data: ${'x'.repeat(1000)}\n`);await a.rejects(()=>transcribe(Buffer.from('x'),{apiKey:'test',fetchImpl:fake(lines)}),/asr_event_too_large/)});

test('rejects invalid or truncated UTF-8 instead of silently changing transcript text',async()=>{
 const make=bytes=>async()=>new Response(new ReadableStream({start(c){c.enqueue(bytes);c.close()}}));
 const prefix=new TextEncoder().encode('data: {\"type\":\"transcript.text.done\",\"text\":\"');
 const suffix=new TextEncoder().encode('\"}\n\n');
 await a.rejects(()=>transcribe(Buffer.from('x'),{apiKey:'test',fetchImpl:make(Buffer.concat([prefix,Buffer.from([0xff]),suffix]))}),/asr_invalid_event/);
 await a.rejects(()=>transcribe(Buffer.from('x'),{apiKey:'test',fetchImpl:make(Buffer.concat([prefix,Buffer.from([0xe4,0xb8])]))}),/asr_invalid_event/);
});

test('does not accept a final event interrupted before its SSE delimiter',async()=>{
 const event='data: {\"type\":\"transcript.text.done\",\"text\":\"最终\"}';
 await a.rejects(()=>transcribe(Buffer.from('x'),{apiKey:'test',fetchImpl:fake([event])}),/asr_incomplete/);
 await a.rejects(()=>transcribe(Buffer.from('x'),{apiKey:'test',fetchImpl:fake([event+'\n'])}),/asr_incomplete/);
 a.equal(await transcribe(Buffer.from('x'),{apiKey:'test',fetchImpl:fake([event+'\n\n'])}),'最终');
});
test('provider error body is never included in the ASR error',async()=>{const secret='sensitive-audio-transcript-or-key';const provider=async()=>new Response(secret,{status:403});await a.rejects(()=>transcribe(Buffer.from('x'),{apiKey:'test',fetchImpl:provider}),error=>{a.equal(error.message,'asr_http_403');a.equal(error.message.includes(secret),false);return true})});

test('uses the configured ASR base URL and model', async () => {
 const fetchImpl = async (url, request) => {
  a.equal(String(url), 'https://asr.example.test/custom/v1/audio/asr/sse');
  a.equal(JSON.parse(request.body).audio.input.transcription.model, 'custom-asr');
  return fake(['data: {"type":"transcript.text.done","text":"最终"}\n\n'])();
 };
 const result = await transcribe(Buffer.from('audio'), {
  apiKey: 'test',
  baseURL: 'https://asr.example.test/custom/v1',
  model: 'custom-asr',
  fetchImpl,
 });
 a.equal(result, '最终');
});

test('maps exact SSE deltas to sentence offsets and leaves unsupported sentences untimed', async () => {
 const events = [
  {type:'transcript.text.delta',delta:'第一句',start_time:960,end_time:1840},
  {type:'transcript.text.delta',delta:'。'},
  {type:'transcript.text.delta',delta:'第二句。',start_time:2000,end_time:5500},
  {type:'transcript.text.delta',delta:'第三句。',start_time:3200,end_time:4000},
  {type:'transcript.text.done',text:'第一句。第二句。第三句。'},
 ];
 const result = await transcribe(Buffer.from('audio'), {
  apiKey:'test',withTimestamps:true,durationSeconds:5,
  fetchImpl:fake(events.map(event=>`data: ${JSON.stringify(event)}\n\n`)),
 });
 a.deepEqual(result, [
  {text:'第一句。',startSeconds:0.96,endSeconds:1.84,timeSource:'asr_sse'},
  {text:'第二句。'},
  {text:'第三句。',startSeconds:3.2,endSeconds:4,timeSource:'asr_sse'},
 ]);
});

test('keeps final text when delta text differs or offsets are unavailable', async () => {
 for(const delta of [
  {delta:'前缀。',start_time:100,end_time:200},
  {delta:'完整文本。',start_time:0,end_time:0},
 ]){
  const result = await transcribe(Buffer.from('audio'), {
   apiKey:'test',withTimestamps:true,
   fetchImpl:fake([
    `data: ${JSON.stringify({type:'transcript.text.delta',...delta})}\n\n`,
    'data: {"type":"transcript.text.done","text":"完整文本。"}\n\n',
   ]),
  });
  a.equal(result,'完整文本。');
 }
});

test('uses audio pauses even without sentence punctuation and accepts shared nonzero offsets', async () => {
 const events = [
  {type:'transcript.text.delta',delta:'开场',start_time:960,end_time:1200},
  {type:'transcript.text.delta',delta:'介绍',start_time:1200,end_time:1200},
  {type:'transcript.text.delta',delta:'中途议题',start_time:899920,end_time:904400},
  {type:'transcript.text.delta',delta:'结尾。',start_time:1769920,end_time:1774080},
  {type:'transcript.text.done',text:'开场介绍中途议题结尾。'},
 ];
 const result = await transcribe(Buffer.from('audio'), {
  apiKey:'test',withTimestamps:true,durationSeconds:1800,
  fetchImpl:fake(events.map(event=>`data: ${JSON.stringify(event)}\n\n`)),
 });
 a.deepEqual(result.map(({text,startSeconds,endSeconds})=>[text,startSeconds,endSeconds]), [
  ['开场介绍',0.96,1.2],['中途议题',899.92,904.4],['结尾。',1769.92,1774.08],
 ]);
});

test('withholds timestamps from sentences that share one provider delta', async () => {
 const events = [
  {type:'transcript.text.delta',delta:'前文。',start_time:0,end_time:450},
  {type:'transcript.text.delta',delta:'第一句。第二句。第三',start_time:450,end_time:2400},
  {type:'transcript.text.delta',delta:'句。',start_time:2400,end_time:2800},
  {type:'transcript.text.delta',delta:'尾声。',start_time:2800,end_time:3500},
  {type:'transcript.text.done',text:'前文。第一句。第二句。第三句。尾声。'},
 ];
 const result = await transcribe(Buffer.from('audio'), {
  apiKey:'test',withTimestamps:true,durationSeconds:4,
  fetchImpl:fake(events.map(event=>`data: ${JSON.stringify(event)}\n\n`)),
 });
 a.deepEqual(result, [
  {text:'前文。',startSeconds:0,endSeconds:0.45,timeSource:'asr_sse'},
  {text:'第一句。'},
  {text:'第二句。'},
  {text:'第三句。'},
  {text:'尾声。',startSeconds:2.8,endSeconds:3.5,timeSource:'asr_sse'},
 ]);
});

test('withholds timestamps from both parts of a delta that crosses the length limit', async () => {
 const longDelta = '字'.repeat(201)+'。';
 const events = [
  {type:'transcript.text.delta',delta:longDelta,start_time:0,end_time:12000},
  {type:'transcript.text.delta',delta:'结尾。',start_time:12000,end_time:13000},
  {type:'transcript.text.done',text:longDelta+'结尾。'},
 ];
 const result = await transcribe(Buffer.from('audio'), {
  apiKey:'test',withTimestamps:true,durationSeconds:13,
  fetchImpl:fake(events.map(event=>`data: ${JSON.stringify(event)}\n\n`)),
 });
 a.deepEqual(result, [
  {text:'字'.repeat(200)},
  {text:'字。'},
  {text:'结尾。',startSeconds:12,endSeconds:13,timeSource:'asr_sse'},
 ]);
});
