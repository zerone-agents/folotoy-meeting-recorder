import {test} from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {MeetingStore} from '../src/store.mjs';
import {SubscriptionStore} from '../src/subscriptions.mjs';
import {readSubscriptionSnapshot,restoreSubscriptionSnapshot} from '../src/subscription-restore.mjs';

test('snapshot restores revoked, pending, terminal and rate state from safety',()=>{
 const safety=new MeetingStore(':memory:'),target=new MeetingStore(':memory:'),subscriptions=new SubscriptionStore(safety.db);
 try{
  safety.create('d','m');target.create('d','m');
  const revoked=subscriptions.createWecom({name:'群',callbackUrl:'https://qyapi.weixin.qq.com/cgi-bin/webhook/send?key=aaaaaaaaaaaaaaaa',now:1});
  subscriptions.revoke(revoked.id,2);
  const approved=subscriptions.createWecom({name:'新群',callbackUrl:'https://qyapi.weixin.qq.com/cgi-bin/webhook/send?key=bbbbbbbbbbbbbbbb',days:30,includeTranscript:true,allowDownload:true,now:3});
  const pending=subscriptions.apply('Hook','https://example.test/hook','1234567890123456',4);
  const frozen=JSON.stringify({headers:{'content-type':'application/json',authorization:'Bearer 1234567890123456'},body:'{"event":"meeting.minutes.completed"}'});
  const insert=safety.db.prepare('INSERT INTO subscription_deliveries(event_id,subscription_id,meeting_id,meeting_generation,attempts,next_attempt_at,created_at,delivered_at,last_error,payload_json,share_expires_at,failed_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,?)');
  insert.run('retry',approved.id,'m',0,2,99,5,null,'http_503',frozen,1000,null);
  insert.run('delivered',approved.id,'m',1,1,0,6,7,null,null,1000,null);
  insert.run('failed',approved.id,'m',2,3,0,8,null,'http_400',null,1000,9);
  safety.db.prepare('INSERT INTO wecom_rate_limits(webhook_hash,next_allowed_at) VALUES(?,?)').run('a'.repeat(64),12);
  target.db.prepare("INSERT INTO meeting_subscriptions(id,name,callback_url,status,created_at) VALUES('stale','stale','https://stale.test','approved',0)").run();
  const snapshot=readSubscriptionSnapshot(safety.db);
  target.db.exec('BEGIN IMMEDIATE');restoreSubscriptionSnapshot(target.db,snapshot);target.db.exec('COMMIT');
  assert.equal(target.db.prepare('SELECT status FROM meeting_subscriptions WHERE id=?').get(revoked.id).status,'revoked');
  assert.equal(target.db.prepare("SELECT COUNT(*) AS n FROM meeting_subscriptions WHERE id='stale'").get().n,0);
  assert.equal(target.db.prepare("SELECT payload_json FROM subscription_deliveries WHERE event_id='retry'").get().payload_json,frozen);
  assert.equal(target.db.prepare("SELECT payload_json FROM subscription_deliveries WHERE event_id='delivered'").get().payload_json,null);
  assert.equal(target.db.prepare("SELECT payload_json FROM subscription_deliveries WHERE event_id='failed'").get().payload_json,null);
  assert.equal(target.db.prepare('SELECT next_allowed_at FROM wecom_rate_limits').get().next_allowed_at,12);
  assert.deepEqual(readSubscriptionSnapshot(target.db),snapshot);
  assert.equal(readSubscriptionSnapshot(target.db).subscriptions.find(row=>row.id===pending.id).status,'pending');
 }finally{safety.close();target.close()}
});

test('missing subscription schema restores no subscriptions and corrupt channel fails closed',()=>{
 const store=new MeetingStore(':memory:');
 try{
  new SubscriptionStore(store.db).createWecom({name:'群',callbackUrl:'https://qyapi.weixin.qq.com/cgi-bin/webhook/send?key=dddddddddddddddd',now:1});
  assert.throws(()=>{store.db.prepare("UPDATE meeting_subscriptions SET channel='broken'").run()},/CHECK|constraint/);
  store.db.prepare("DELETE FROM service_metadata WHERE key='subscription_schema'").run();
  assert.equal(readSubscriptionSnapshot(store.db),null);
 }finally{store.close()}
});

test('null snapshot clears all existing subscription state',()=>{
 const target=new MeetingStore(':memory:'),subscriptions=new SubscriptionStore(target.db);
 try{
  target.create('d','m');const bot=subscriptions.createWecom({name:'群',callbackUrl:'https://qyapi.weixin.qq.com/cgi-bin/webhook/send?key=cccccccccccccccc',now:1});
  target.db.prepare("INSERT INTO subscription_deliveries(event_id,subscription_id,meeting_id,meeting_generation,created_at) VALUES('event',?,'m',0,1)").run(bot.id);
  target.db.prepare('INSERT INTO wecom_rate_limits(webhook_hash,next_allowed_at) VALUES(?,?)').run('a'.repeat(64),2);
  restoreSubscriptionSnapshot(target.db,null);
  for(const table of ['meeting_subscriptions','subscription_deliveries','wecom_rate_limits'])assert.equal(target.db.prepare(`SELECT COUNT(*) AS n FROM ${table}`).get().n,0);
 }finally{target.close()}
});
