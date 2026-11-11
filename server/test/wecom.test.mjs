import {test} from 'node:test';
import assert from 'node:assert/strict';
import {validateWecomWebhook,cleanMeetingTitle,buildWecomCard,buildWecomTestCard,sendWecom} from '../src/wecom.mjs';
import {DatabaseSync} from 'node:sqlite';
import {mkdtempSync,readFileSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {MeetingStore} from '../src/store.mjs';
import {SubscriptionStore,initializeSubscriptions,reserveWecomSlot,testWecom,drainSubscriptions,queueSubscriptionDeliveries} from '../src/subscriptions.mjs';
import {createMeetingRouter} from '../src/routes.mjs';

const key='a'.repeat(16);
const adminHeaders={authorization:'Bearer admin123','content-type':'application/json'};
const post=(app,path,body,headers=adminHeaders)=>app.request(path,{method:'POST',headers,body:JSON.stringify(body)});
const url=`https://qyapi.weixin.qq.com/cgi-bin/webhook/send?key=${key}`;

test('WeCom webhook accepts only the canonical endpoint and key',()=>{
  assert.equal(validateWecomWebhook(url),url);
  for(const value of [null,url.replace('https:','http:'),url.replace('qyapi.weixin.qq.com','evil.example'),url.replace('/cgi-bin/webhook/send','/other'),url+'&debug=1',url+'#x',url.replace('https://','https://user@'),url.replace('qyapi.weixin.qq.com','qyapi.weixin.qq.com:443'),url.replace(key,'short')])assert.throws(()=>validateWecomWebhook(value),/invalid/);
});

test('meeting title removes controls, falls back and truncates by code point',()=>{
  assert.equal(cleanMeetingTitle('  产品\u0000\u200e\n  周会  ','m'),'产品 周会');
  assert.equal(cleanMeetingTitle('\u200e','meeting-id'),'meeting-id');
  assert.equal(cleanMeetingTitle('😀'.repeat(31),'m'),'😀'.repeat(29)+'…');
});

test('formal card snapshot contains only the share link',()=>{
  const card=buildWecomCard({title:'产品周会',meetingId:'m',completedAt:1794301200,shareUrl:'https://meetings.example/meeting-service/share#token',shareExpiresAt:1794906000,timeZone:'Asia/Shanghai'});
  assert.deepEqual(card,{msgtype:'template_card',template_card:{card_type:'text_notice',main_title:{title:'新会议纪要已生成',desc:'产品周会'},sub_title_text:'完成时间：2026-11-10 17:00（UTC+8）\n分享有效期至：2026-11-17 17:00（UTC+8）\n点击查看会议纪要',card_action:{type:1,url:'https://meetings.example/meeting-service/share#token'}}});
  assert.equal(JSON.stringify(card).includes('download'),false);
});

test('test card contains no meeting or share credentials',()=>{
  const card=buildWecomTestCard({baseUrl:'https://meetings.example/meeting-service'});
  assert.equal(card.template_card.main_title.title,'企业微信机器人测试消息');
  assert.deepEqual(card.template_card.card_action,{type:1,url:'https://meetings.example/meeting-service'});
  assert.equal(JSON.stringify(card).includes('token'),false);
});

test('WeCom success and safe error classification',async()=>{
  assert.deepEqual(await sendWecom({callbackUrl:url,payload:{},fetchImpl:async()=>Response.json({errcode:0})}),{ok:true});
  assert.deepEqual(await sendWecom({callbackUrl:url,payload:{},fetchImpl:async()=>Response.json({errcode:45009})}),{ok:false,code:'wecom_45009',retryable:true});
  assert.deepEqual(await sendWecom({callbackUrl:url,payload:{},fetchImpl:async()=>Response.json({errcode:93000,errmsg:'secret webhook'})}),{ok:false,code:'wecom_93000',retryable:false});
  assert.deepEqual(await sendWecom({callbackUrl:url,payload:{},fetchImpl:async()=>new Response(null,{status:503})}),{ok:false,code:'http_503',retryable:true});
  assert.deepEqual(await sendWecom({callbackUrl:url,payload:{},fetchImpl:async()=>{throw new Error('raw network detail')}}),{ok:false,code:'network_error',retryable:true});
});

test('WeCom subscriptions are approved immediately and never list their key',()=>{
  const store=new MeetingStore(':memory:'),subscriptions=new SubscriptionStore(store.db);
  try{
    const made=subscriptions.createWecom({name:'产品群',callbackUrl:url,days:7,includeTranscript:false,allowDownload:false,now:100});
    assert.equal(made.status,'approved');assert.equal(made.channel,'wecom');
    const listed=subscriptions.list()[0];
    assert.equal(listed.channel,'wecom');assert.equal(listed.callbackUrl,'https://qyapi.weixin.qq.com/cgi-bin/webhook/send');
    assert.equal(JSON.stringify(listed).includes(key),false);
    const target=subscriptions.wecomTarget(made.id);
    assert.equal(target.id,made.id);assert.equal(target.callbackUrl,url);
    assert.equal(store.db.prepare("SELECT value FROM service_metadata WHERE key='subscription_schema'").get().value,'1');
  }finally{store.close()}
});

test('new schema without channel fails closed instead of defaulting to webhook',()=>{
  const db=new DatabaseSync(':memory:');
  try{
    db.exec(`CREATE TABLE service_metadata(key TEXT PRIMARY KEY,value TEXT NOT NULL);
      CREATE TABLE meetings(id TEXT PRIMARY KEY);
      CREATE TABLE meeting_subscriptions(id TEXT PRIMARY KEY,name TEXT NOT NULL,callback_url TEXT NOT NULL,callback_token TEXT,status TEXT NOT NULL,created_at INTEGER NOT NULL,reviewed_at INTEGER,share_days INTEGER NOT NULL DEFAULT 7,include_transcript INTEGER NOT NULL DEFAULT 0,allow_download INTEGER NOT NULL DEFAULT 0);
      INSERT INTO service_metadata VALUES('subscription_schema','1');`);
    assert.throws(()=>initializeSubscriptions(db),/invalid_subscription_state/);
  }finally{db.close()}
});

test('marked schema rejects a corrupt channel definition',()=>{
  const db=new DatabaseSync(':memory:');
  try{
    db.exec(`CREATE TABLE service_metadata(key TEXT PRIMARY KEY,value TEXT NOT NULL);
      CREATE TABLE meetings(id TEXT PRIMARY KEY);
      CREATE TABLE meeting_subscriptions(id TEXT PRIMARY KEY,name TEXT NOT NULL,callback_url TEXT NOT NULL,callback_token TEXT,status TEXT NOT NULL,created_at INTEGER NOT NULL,reviewed_at INTEGER,share_days INTEGER NOT NULL DEFAULT 7,include_transcript INTEGER NOT NULL DEFAULT 0,allow_download INTEGER NOT NULL DEFAULT 0,channel TEXT NOT NULL DEFAULT 'webhook');
      INSERT INTO service_metadata VALUES('subscription_schema','1');`);
    assert.throws(()=>initializeSubscriptions(db),{message:'invalid_subscription_state'});
  }finally{db.close()}
});

test('marked schema rejects a constrained lookalike archived_channel declaration',()=>{
  const db=new DatabaseSync(':memory:');
  try{
    db.exec(`CREATE TABLE service_metadata(key TEXT PRIMARY KEY,value TEXT NOT NULL);
      CREATE TABLE meetings(id TEXT PRIMARY KEY);
      CREATE TABLE meeting_subscriptions(id TEXT PRIMARY KEY,name TEXT NOT NULL,callback_url TEXT NOT NULL,callback_token TEXT,status TEXT NOT NULL,created_at INTEGER NOT NULL,reviewed_at INTEGER,share_days INTEGER NOT NULL DEFAULT 7,include_transcript INTEGER NOT NULL DEFAULT 0,allow_download INTEGER NOT NULL DEFAULT 0,channel TEXT,archived_channel TEXT NOT NULL DEFAULT 'webhook' CHECK(channel IN ('webhook','wecom')));
      INSERT INTO service_metadata VALUES('subscription_schema','1');`);
    assert.throws(()=>initializeSubscriptions(db),{message:'invalid_subscription_state'});
  }finally{db.close()}
});

test('marked schema rejects stored invalid channels before redacted listing',()=>{
  const db=new DatabaseSync(':memory:');
  try{
    db.exec(`CREATE TABLE service_metadata(key TEXT PRIMARY KEY,value TEXT NOT NULL);
      CREATE TABLE meetings(id TEXT PRIMARY KEY);
      CREATE TABLE meeting_subscriptions(id TEXT PRIMARY KEY,name TEXT NOT NULL,callback_url TEXT NOT NULL,callback_token TEXT,status TEXT NOT NULL,created_at INTEGER NOT NULL,reviewed_at INTEGER,share_days INTEGER NOT NULL DEFAULT 7,include_transcript INTEGER NOT NULL DEFAULT 0,allow_download INTEGER NOT NULL DEFAULT 0,channel TEXT NOT NULL DEFAULT 'webhook' CHECK(channel IN ('webhook','wecom')));
      INSERT INTO service_metadata VALUES('subscription_schema','1');
      PRAGMA ignore_check_constraints=ON;`);
    db.prepare("INSERT INTO meeting_subscriptions(id,name,callback_url,status,created_at,channel) VALUES('bad','secret','https://qyapi.weixin.qq.com/cgi-bin/webhook/send?key=secret-key','approved',1,'unknown')").run();
    assert.throws(()=>initializeSubscriptions(db),{message:'invalid_subscription_state'});
  }finally{db.close()}
});

test('explicit pre-channel legacy database migrates ordinary rows to webhook',()=>{
  const directory=mkdtempSync(join(tmpdir(),'wecom-migration-')),path=join(directory,'legacy.sqlite');
  const seeded=new MeetingStore(path),subscriptions=new SubscriptionStore(seeded.db);
  const applied=subscriptions.apply('旧服务','https://subscriber.example/hooks',null,1);subscriptions.review(applied.id,true,1);seeded.close();
  const legacy=new DatabaseSync(path);
  try{legacy.exec("ALTER TABLE meeting_subscriptions DROP COLUMN channel; DELETE FROM service_metadata WHERE key='subscription_schema';")}finally{legacy.close()}
  const store=new MeetingStore(path);
  try{
    assert.equal(store.db.prepare('SELECT channel FROM meeting_subscriptions WHERE id=?').get(applied.id).channel,'webhook');
    assert.equal(store.db.prepare("SELECT value FROM service_metadata WHERE key='subscription_schema'").get().value,'1');
    assert.deepEqual(subscriptionsColumns(store.db),['payload_json','share_expires_at','failed_at']);
    assert.equal(store.db.prepare("SELECT 1 FROM sqlite_master WHERE type='table' AND name='wecom_rate_limits'").get()['1'],1);
  }finally{store.close();rmSync(directory,{recursive:true,force:true})}
});

function subscriptionsColumns(db){
  const columns=new Set(db.prepare('PRAGMA table_info(subscription_deliveries)').all().map(column=>column.name));
  return ['payload_json','share_expires_at','failed_at'].filter(column=>columns.has(column));
}

test('administrator creates WeCom and tests saved or unsaved bots without shares',async()=>{
  const store=new MeetingStore(':memory:');let calls=[];
  try{
    const app=createMeetingRouter(store,{adminToken:'admin123',subscriptionFetchImpl:async(_url,options)=>{calls.push(JSON.parse(options.body));return Response.json({errcode:0})},publicBaseUrl:'https://meetings.example/meeting-service'});
    assert.equal((await post(app,'/admin/subscriptions/wecom',{name:'群',callbackUrl:url})).status,201);
    assert.equal((await app.request('/admin/subscriptions/wecom',{method:'POST',headers:{'content-type':'application/json'},body:'{}'})).status,401);
    const listed=await(await app.request('/admin/subscriptions',{headers:{authorization:'Bearer admin123'}})).json();
    assert.equal((await post(app,'/admin/subscriptions/wecom/test',{callbackUrl:url})).status,200);
    store.db.prepare('DELETE FROM wecom_rate_limits').run();
    assert.equal((await post(app,`/admin/subscriptions/${listed.subscriptions[0].id}/test`,{})).status,200);
    assert.equal(store.db.prepare('SELECT COUNT(*) AS n FROM meeting_shares').get().n,0);
    assert.equal(store.db.prepare('SELECT COUNT(*) AS n FROM subscription_deliveries').get().n,0);
    assert.equal(calls.length,2);
    assert.equal(calls.every(card=>card.template_card.main_title.title==='企业微信机器人测试消息'),true);
  }finally{store.close()}
});

test('test endpoint reports validation, limits and safe upstream errors',async()=>{
  const store=new MeetingStore(':memory:');
  try{
    const app=createMeetingRouter(store,{adminToken:'admin123',subscriptionFetchImpl:async()=>Response.json({errcode:93000,errmsg:'do not expose'}),publicBaseUrl:'https://example.com'});
    assert.equal((await post(app,'/admin/subscriptions/wecom/test',{callbackUrl:'https://evil.example/?key='+key})).status,400);
    const failed=await post(app,'/admin/subscriptions/wecom/test',{callbackUrl:url});
    assert.equal(failed.status,502);assert.deepEqual(await failed.json(),{ok:false,error:'wecom_93000'});
    const limited=await post(app,'/admin/subscriptions/wecom/test',{callbackUrl:url});
    assert.equal(limited.status,429);assert.deepEqual(await limited.json(),{ok:false,error:'rate_limited'});
  }finally{store.close()}
});

test('missing public base URL is rejected before reserving a rate slot',async()=>{
  const store=new MeetingStore(':memory:');let calls=0;
  try{
    const app=createMeetingRouter(store,{adminToken:'admin123',subscriptionFetchImpl:async()=>{calls++;return Response.json({errcode:0})}});
    const response=await post(app,'/admin/subscriptions/wecom/test',{callbackUrl:url});
    assert.equal(response.status,400);assert.deepEqual(await response.json(),{ok:false,error:'invalid'});
    assert.equal(store.db.prepare('SELECT COUNT(*) AS n FROM wecom_rate_limits').get().n,0);
    assert.equal(calls,0);
  }finally{store.close()}
});

test('rate slots persist across restart and the twenty-first send waits',()=>{
  const directory=mkdtempSync(join(tmpdir(),'wecom-rate-')),path=join(directory,'rate.sqlite');
  let store=new MeetingStore(path);
  try{
    assert.deepEqual(reserveWecomSlot(store.db,url,1000),{ok:true,reservedAt:1000});
    store.close();store=new MeetingStore(path);
    assert.deepEqual(reserveWecomSlot(store.db,url,1001),{ok:false,retryAt:1003});
    for(let now=1003;now<=1057;now+=3)assert.equal(reserveWecomSlot(store.db,url,now).ok,true);
    assert.deepEqual(reserveWecomSlot(store.db,url,1059),{ok:false,retryAt:1060});
    assert.deepEqual(reserveWecomSlot(store.db,url,1060),{ok:true,reservedAt:1060});
  }finally{store.close();rmSync(directory,{recursive:true,force:true})}
});


function wecomCompletedMeeting(store,id='m'){
  store.create('d',id);
  store.db.prepare("UPDATE meetings SET state='completed',minutes=?,completed_at=1794301200 WHERE id=?").run(JSON.stringify({meeting:{topic:'产品周会'},summary:'synthetic'}),id);
  queueSubscriptionDeliveries(store.db,id,1,1000);
}

test('WeCom retry freezes card body and configuration',async()=>{
  const store=new MeetingStore(':memory:'),subscriptions=new SubscriptionStore(store.db);
  try{
    const bot=subscriptions.createWecom({name:'群',callbackUrl:url,now:1});wecomCompletedMeeting(store);
    const bodies=[];let calls=0;
    const options={baseUrl:'https://example.com',now:()=>1000+calls*3,fetchImpl:async(_url,request)=>{bodies.push(request.body);calls++;return Response.json({errcode:calls===1?45009:0})}};
    await drainSubscriptions(store,options);subscriptions.updateShareConfig(bot.id,{days:30});
    store.db.prepare('UPDATE subscription_deliveries SET next_attempt_at=0').run();await drainSubscriptions(store,options);
    assert.equal(bodies[0],bodies[1]);assert.equal(store.db.prepare('SELECT COUNT(*) AS n FROM meeting_shares').get().n,1);
  }finally{store.close()}
});

test('share and payload persistence roll back atomically',async()=>{
  for(const trigger of [
    "CREATE TRIGGER fail_share BEFORE INSERT ON meeting_shares BEGIN SELECT RAISE(ABORT,'blocked'); END",
    "CREATE TRIGGER fail_payload BEFORE UPDATE OF payload_json ON subscription_deliveries WHEN NEW.payload_json IS NOT NULL BEGIN SELECT RAISE(ABORT,'blocked'); END"
  ]){
    const store=new MeetingStore(':memory:'),subscriptions=new SubscriptionStore(store.db);
    try{
      subscriptions.createWecom({name:'群',callbackUrl:url,now:1});wecomCompletedMeeting(store);store.db.exec(trigger);
      await assert.rejects(()=>drainSubscriptions(store,{baseUrl:'https://example.com',now:()=>1000,fetchImpl:async()=>Response.json({errcode:0})}));
      assert.equal(store.db.prepare('SELECT COUNT(*) AS n FROM meeting_shares').get().n,0);
      assert.equal(store.db.prepare('SELECT payload_json FROM subscription_deliveries').get().payload_json,null);
    }finally{store.close()}
  }
});


test('WeCom terminal errors stop retries and clear secrets',async()=>{
  const terminal=[93000,93001,93004,93006,93008,93019,93017,99999];
  for(const errcode of terminal){
    const store=new MeetingStore(':memory:'),subscriptions=new SubscriptionStore(store.db);
    try{
      subscriptions.createWecom({name:'群',callbackUrl:url,now:1});wecomCompletedMeeting(store);
      let calls=0;
      await drainSubscriptions(store,{baseUrl:'https://example.com',now:()=>1000,fetchImpl:async()=>{calls++;return Response.json({errcode,errmsg:'raw secret'})}});
      store.db.prepare('UPDATE subscription_deliveries SET next_attempt_at=0').run();
      await drainSubscriptions(store,{baseUrl:'https://example.com',now:()=>1004,fetchImpl:async()=>{calls++;return Response.json({errcode:0})}});
      const row=store.db.prepare('SELECT attempts,failed_at,last_error,payload_json FROM subscription_deliveries').get();
      assert.equal(calls,1);assert.equal(row.attempts,1);assert.equal(row.last_error,`wecom_${errcode}`);assert.ok(row.failed_at);assert.equal(row.payload_json,null);
    }finally{store.close()}
  }
});

test('WeCom retryable errors retain frozen payload and exponential backoff',async()=>{
  const cases=[
    [async()=>Response.json({errcode:45009}),'wecom_45009'],
    [async()=>{throw new Error('raw detail')},'network_error'],
    [async()=>{throw new DOMException('aborted','AbortError')},'timeout'],
    ...[408,429,500,503].map(status=>[async()=>new Response(null,{status}),`http_${status}`])
  ];
  for(const [fetchImpl,code] of cases){
    const store=new MeetingStore(':memory:'),subscriptions=new SubscriptionStore(store.db);
    try{
      subscriptions.createWecom({name:'群',callbackUrl:url,now:1});wecomCompletedMeeting(store);
      await drainSubscriptions(store,{baseUrl:'https://example.com',now:()=>1000,fetchImpl});
      const row=store.db.prepare('SELECT attempts,next_attempt_at,failed_at,last_error,payload_json FROM subscription_deliveries').get();
      assert.equal(row.attempts,1);assert.equal(row.next_attempt_at,1005);assert.equal(row.failed_at,null);assert.equal(row.last_error,code);assert.equal(typeof row.payload_json,'string');
    }finally{store.close()}
  }
});

test('formal delivery and test delivery consume the same rate slot',async()=>{
  const store=new MeetingStore(':memory:'),subscriptions=new SubscriptionStore(store.db);
  try{
    subscriptions.createWecom({name:'群',callbackUrl:url,now:1});wecomCompletedMeeting(store);
    await drainSubscriptions(store,{baseUrl:'https://example.com',now:()=>1000,fetchImpl:async()=>Response.json({errcode:0})});
    const limited=await testWecom({db:store.db,callbackUrl:url,baseUrl:'https://example.com',now:1001,fetchImpl:async()=>Response.json({errcode:0})});
    assert.equal(limited.status,429);
    subscriptions.createWecom({name:'同机器人重复订阅',callbackUrl:url,now:1});wecomCompletedMeeting(store,'second');
    const tested=await testWecom({db:store.db,callbackUrl:url,baseUrl:'https://example.com',now:1004,fetchImpl:async()=>Response.json({errcode:0})});
    assert.equal(tested.status,200);
    await drainSubscriptions(store,{baseUrl:'https://example.com',now:()=>1005,fetchImpl:async()=>Response.json({errcode:0})});
    assert.equal(store.db.prepare("SELECT attempts FROM subscription_deliveries WHERE meeting_id='second'").get().attempts,0);
  }finally{store.close()}
});

test('operator docs include WeCom routes and recovery boundaries',()=>{
  const api=readFileSync(new URL('../API.md',import.meta.url),'utf8');
  const backup=readFileSync(new URL('../BACKUP.md',import.meta.url),'utf8');
  for(const route of ['/admin/subscriptions/wecom','/admin/subscriptions/wecom/test','/admin/subscriptions/:id/test'])assert.ok(api.includes(route));
  for(const text of ['rate_limited','wecom_45009','share_expired'])assert.ok(api.includes(text));
  for(const table of ['meeting_subscriptions','subscription_deliveries','wecom_rate_limits'])assert.ok(backup.includes(table));
  assert.match(backup,/最新安全库/);assert.match(backup,/净化/);
});
