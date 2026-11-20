import {test} from 'node:test';import {strict as a} from 'node:assert';import {createRuntimeMinutes,transcriptBatches} from '../src/runtime-minutes.mjs';
const t={versionId:'v',segments:[{id:'s0',text:'abc'}]};
test('runtime adapter rejects nonterminal and invalid JSON',async()=>{for(const body of [{state:'failed',text:'{}'},{state:'completed',text:'not json'}]){const m=createRuntimeMinutes({app:{request:async()=>Response.json(body)}},'key');await a.rejects(()=>m(t))}});
test('runtime adapter validates and combines batches against original segments',async()=>{const transcript={versionId:'v',segments:[{id:'s0',text:'abc'},{id:'s1',text:'你好呀'}]};let calls=0;const m=createRuntimeMinutes({app:{request:async(_url,options)=>{const prompt=JSON.parse(options.body).message;const part=JSON.parse(prompt.slice(prompt.indexOf('转写：\n')+4));const seg=part.segments[0];calls++;return Response.json({state:'completed',text:JSON.stringify({summary:seg.text,discussion:[{text:seg.text,evidence:[{versionId:'v',segmentId:seg.id,start:0,end:1,quote:Array.from(seg.text)[0]}]}],decisions:[],actions:[],openQuestions:[]})})}}},'key',{maxPoints:3});const result=await m(transcript);a.equal(calls,2);a.equal(result.summary,'abc\n\n你好呀');a.deepEqual(result.discussion.map(x=>x.evidence[0].segmentId),['s0','s1']);a.equal(transcriptBatches(transcript,3).length,2)});
test('a batch cannot cite a segment from another batch',async()=>{const t={versionId:'v',segments:[{id:'s0',text:'abc'},{id:'s1',text:'def'}]};const m=createRuntimeMinutes({app:{request:async()=>Response.json({state:'completed',text:JSON.stringify({summary:'x',discussion:[{text:'x',evidence:[{versionId:'v',segmentId:'s1',start:0,end:1,quote:'d'}]}],decisions:[],actions:[],openQuestions:[]})})}},'key',{maxPoints:3});await a.rejects(()=>m(t),/invalid_evidence/)});

test('runtime must report completed or provide the pinned runtime success envelope',async()=>{
 const text=JSON.stringify({summary:'完整外观',discussion:[],decisions:[],actions:[],openQuestions:[]});
 for(const body of [{text},{state:null,text},{state:'running',text},null]){
  const run=createRuntimeMinutes({app:{request:async()=>Response.json(body)}},'key');await a.rejects(()=>run(t),/minutes_incomplete/);
 }
});

test('accepts pinned runtime success envelope without a state field',async()=>{
 const text=JSON.stringify({summary:'完成',discussion:[],decisions:[],actions:[],openQuestions:[]});
 const run=createRuntimeMinutes({app:{request:async()=>Response.json({runId:'run',sessionId:'session',numTurns:1,text})}},'key');
 a.equal((await run(t)).summary,'完成');
});
test('runtime locates a unique model quote without trusting model-calculated offsets',async()=>{const transcript={versionId:'v',segments:[{id:'s0',text:'开头😀确定方案结束'}]},data={summary:'讨论',discussion:[{text:'讨论',evidence:[{versionId:'v',segmentId:'s0',quote:'😀确定方案'}]}],decisions:[],actions:[],openQuestions:[]},run=createRuntimeMinutes({app:{request:async()=>Response.json({state:'completed',text:JSON.stringify(data)})}},'key');const result=await run(transcript);a.equal(result.discussion[0].evidence[0].start,2);a.equal(result.discussion[0].evidence[0].end,7);await a.rejects(()=>run({versionId:'v',segments:[{id:'s0',text:'😀确定方案😀确定方案'}]}),/invalid_evidence_ambiguous/)});
test('runtime creates exact evidence from a model-selected segment identifier',async()=>{const transcript={versionId:'v',segments:[{id:'s0',text:'第一句。'},{id:'s1',text:'第二句😀。'}]},data={summary:'讨论',discussion:[{text:'第二句',evidence:[{versionId:'v',segmentId:'s1'}]}],decisions:[],actions:[],openQuestions:[]},run=createRuntimeMinutes({app:{request:async()=>Response.json({state:'completed',text:JSON.stringify(data)})}},'key');const result=await run(transcript);a.deepEqual(result.discussion[0].evidence[0],{versionId:'v',segmentId:'s1',start:0,end:5,quote:'第二句😀。'});});
test('batch metadata is combined only when known values agree',async()=>{const transcript={versionId:'v',segments:[{id:'s0',text:'甲'},{id:'s1',text:'乙'}]};let n=0;const run=createRuntimeMinutes({app:{request:async()=>Response.json({state:'completed',text:JSON.stringify({meeting:++n===1?{topic:'项目',date:'周一'}:{topic:'项目',date:'周二',format:'线上'},summary:'摘要',discussion:[],decisions:[],actions:[],openQuestions:[]})})}},'key',{maxPoints:1});const result=await run(transcript);a.equal(result.meeting.topic,'项目');a.equal(result.meeting.date,'待确认');a.equal(result.meeting.format,'线上');a.equal(result.meeting.participants,'待确认')});
test('accepts only complete JSON or a single complete JSON code block',async()=>{const {parseMinutesJson}=await import('../src/runtime-minutes.mjs');a.deepEqual(parseMinutesJson('  {"ok":true}  '),{ok:true});a.deepEqual(parseMinutesJson('```json\n{"ok":true}\n```'),{ok:true});for(const text of ['```json\n{"ok":true}','说明\n```json\n{"ok":true}\n```','```json\n{"ok":true}\n```\n说明','```json\n{"ok":','```js\n{"ok":true}\n```'])a.throws(()=>parseMinutesJson(text),/minutes_invalid_json/)});
test('meeting-minutes prompt addresses suspected ASR noise without changing cited transcript',async()=>{const transcript={versionId:'v',segments:[{id:'s0',text:'佛罗托项目继续验证。'}]};let prompt;const run=createRuntimeMinutes({app:{request:async(_url,options)=>{prompt=JSON.parse(options.body).message;return Response.json({state:'completed',text:JSON.stringify({summary:'讨论项目',discussion:[{text:'FoloToy 项目继续验证',evidence:[{versionId:'v',segmentId:'s0'}]}],decisions:[],actions:[],openQuestions:[]})})}}},'key');const result=await run(transcript);a.match(prompt,/疑似识别幻觉/);a.match(prompt,/上下文有充分依据/);a.equal(result.discussion[0].evidence[0].quote,'佛罗托项目继续验证。');a.equal(transcript.segments[0].text,'佛罗托项目继续验证。')});

test('default minutes batching keeps a 10,628-character transcript in one request', async () => {
 const transcript = {
  versionId: 'v',
  segments: Array.from({length: 176}, (_, i) => ({
   id: `s${i}`,
   text: '字'.repeat(i === 175 ? 128 : 60),
  })),
 };
 let calls = 0;
 const host = {app: {request: async () => {
  calls++;
  return Response.json({
   state: 'completed',
   text: JSON.stringify({summary: '纪要', discussion: [], decisions: [], actions: [], openQuestions: []}),
  });
 }}};
 await createRuntimeMinutes(host, 'fixture')(transcript);
 a.equal(calls, 1);
});

test('Runtime diagnostics log complete failed exchanges without request credentials', async () => {
 const diagnostics = [];
 const rawResponse = JSON.stringify({
  state: 'completed',
  text: JSON.stringify({
   summary: '纪要',
   discussion: [{text: '议题', evidence: [{versionId: 'wrong', segmentId: 's0'}]}],
  }),
 });
 let requestBody;
 const host = {app: {request: async (_path, options) => {
  requestBody = options.body;
  return new Response(rawResponse);
 }}};
 const credential = 'test-runtime-secret';
 await a.rejects(createRuntimeMinutes(host, credential)(t, {onFailure: entry => diagnostics.push(entry)}), /invalid_evidence_version/);
 a.equal(diagnostics.length, 1);
 const [entry] = diagnostics;
 a.equal(entry.request.body, requestBody);
 a.deepEqual(entry.response, {status: 200, body: rawResponse});
 a.equal(entry.error, 'invalid_evidence_version');
 a.equal(JSON.stringify(entry).includes(credential), false);
});

test('an aborted Runtime request does not capture meeting content', async () => {
 const controller = new AbortController();
 const host = {app: {request: async () => {
  controller.abort(new Error('meeting_deleted'));
  throw controller.signal.reason;
 }}};
 await a.rejects(createRuntimeMinutes(host, 'key')(t, {
  signal: controller.signal,
  onFailure: () => a.fail('cancelled meeting content must not be captured'),
 }), /meeting_deleted/);
});

test('oversized Runtime response is cancelled and only a bounded prefix is saved', async () => {
 let cancelled = false;
 const chunk = new Uint8Array(512 * 1024).fill(97);
 const stream = new ReadableStream({
  pull(controller) { controller.enqueue(chunk); },
  cancel() { cancelled = true; },
 });
 const host = {app: {request: async () => new Response(stream)}};
 const failures = [];
 await a.rejects(createRuntimeMinutes(host, 'key')(t, {
  onFailure: record => failures.push(record),
 }), /minutes_response_too_large/);
 a.equal(cancelled, true);
 a.equal(failures.length, 1);
 a.equal(failures[0].response.truncated, true);
 a.equal(Buffer.byteLength(failures[0].response.body), 1024 * 1024);
});
