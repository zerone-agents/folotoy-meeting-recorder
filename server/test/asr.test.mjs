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
