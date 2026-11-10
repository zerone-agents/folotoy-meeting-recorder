import {test} from 'node:test';
import assert from 'node:assert/strict';
import {validateWecomWebhook,cleanMeetingTitle,buildWecomCard,buildWecomTestCard,sendWecom} from '../src/wecom.mjs';
import {DatabaseSync} from 'node:sqlite';
import {mkdtempSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {MeetingStore} from '../src/store.mjs';
import {SubscriptionStore,initializeSubscriptions} from '../src/subscriptions.mjs';

const key='a'.repeat(16);
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
