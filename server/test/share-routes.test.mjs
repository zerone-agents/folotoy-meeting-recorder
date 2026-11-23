import {test} from 'node:test';
import assert from 'node:assert/strict';
import {MeetingStore} from '../src/store.mjs';
import {createMeetingRouter} from '../src/routes.mjs';
test('administrator creates a share and anonymous holder can read it',async()=>{
 const s=new MeetingStore(':memory:');
 try{
 const t=JSON.stringify({versionId:'v',segments:[{text:'private transcript'}]}),m=JSON.stringify({summary:'shared minutes'});
 s.db.prepare("INSERT INTO meetings(id,owner,state,transcript,minutes) VALUES('m','d','completed',?,?)").run(t,m);
 s.db.prepare("INSERT INTO minutes_versions VALUES('m',1,'v',?)").run(m);
 const app=createMeetingRouter(s,{adminToken:'admin123'});
 const created=await app.request('/admin/meetings/m/shares',{method:'POST',headers:{authorization:'Bearer admin123','content-type':'application/json'},body:JSON.stringify({days:7,includeTranscript:false})});
 assert.equal(created.status,201);const {token}=await created.json();
 const shared=await app.request('/share/data',{headers:{authorization:'Bearer '+token}});assert.equal(shared.status,200);
 const data=await shared.json();assert.match(data.minutesMarkdown,/shared minutes/);assert.equal(data.transcript,undefined);
 assert.equal((await app.request('/admin/meetings')).status,401);
 }finally{s.close()}
});
test('share page is anonymous and does not send fragment in requests',async()=>{const s=new MeetingStore(':memory:');try{const app=createMeetingRouter(s,{adminToken:'admin123'});const r=await app.request('/share');assert.equal(r.status,200);assert.match(r.headers.get('referrer-policy'),/no-referrer/);const html=await r.text();assert.match(html,/location.hash/);assert.match(html,/人工/)}finally{s.close()}});
test('only exact share GET routes are anonymous and invalid requests fail closed',async()=>{const s=new MeetingStore(':memory:');try{const app=createMeetingRouter(s,{adminToken:'admin123'});for(const [path,method] of [['/share','POST'],['/share/data','POST'],['/share/other','GET'],['/admin/meetings/x/shares','POST']])assert.equal((await app.request(path,{method})).status,401);for(const token of ['wrong','x'.repeat(43)]){const r=await app.request('/share/data',{headers:{authorization:'Bearer '+token}});assert.equal(r.status,404);assert.equal(r.headers.get('cache-control'),'no-store');assert.equal(r.headers.get('referrer-policy'),'no-referrer')}const invalid=await app.request('/admin/meetings/x/shares',{method:'POST',headers:{authorization:'Bearer admin123','content-type':'application/json'},body:JSON.stringify({days:7,includeTranscript:false,extra:true})});assert.equal(invalid.status,400)}finally{s.close()}});
test('download endpoints enforce both download and transcript permissions',async()=>{const s=new MeetingStore(':memory:');try{const t=JSON.stringify({versionId:'v',segments:[{text:'original transcript'}]}),m=JSON.stringify({summary:'original minutes'});s.db.prepare("INSERT INTO meetings(id,owner,state,transcript,minutes) VALUES('m','d','completed',?,?)").run(t,m);s.db.prepare("INSERT INTO minutes_versions VALUES('m',1,'v',?)").run(m);const app=createMeetingRouter(s,{adminToken:'admin123'});async function create(includeTranscript,allowDownload){const r=await app.request('/admin/meetings/m/shares',{method:'POST',headers:{authorization:'Bearer admin123','content-type':'application/json'},body:JSON.stringify({days:7,includeTranscript,allowDownload})});assert.equal(r.status,201);return (await r.json()).token}async function get(token,kind){return app.request('/share/download/'+kind,{headers:{authorization:'Bearer '+token}})}const disabled=await create(true,false),minutesOnly=await create(false,true),both=await create(true,true);assert.equal((await get(disabled,'minutes')).status,404);assert.equal((await get(disabled,'transcript')).status,404);assert.equal((await get(minutesOnly,'transcript')).status,404);const md=await get(minutesOnly,'minutes');assert.equal(md.status,200);assert.match(await md.text(),/original minutes/);const txt=await get(both,'transcript');assert.equal(txt.status,200);assert.equal(await txt.text(),'[时间戳不可用]\noriginal transcript');assert.match(txt.headers.get('content-disposition'),/attachment/)}finally{s.close()}});
