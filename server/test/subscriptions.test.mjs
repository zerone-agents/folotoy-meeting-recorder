import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { MeetingStore } from '../src/store.mjs';
import { SubscriptionStore, drainSubscriptions, publicBaseUrl } from '../src/subscriptions.mjs';
import { createMeetingRouter } from '../src/routes.mjs';

const completedAt = (store, id) => store.db.prepare('SELECT completed_at AS completedAt FROM meetings WHERE id=?').get(id).completedAt;

function completedMeeting(store, id = 'm', title = null) {
  const audio = Buffer.from(`audio-${id}`), digest = createHash('sha256').update(audio).digest('hex');
  store.create('d', id);store.put('d', id, 0, audio, digest);
  store.finalize('d', id, { count: 1, bytes: audio.length, digest, reason: 'user' });
  store.publish(store.claim(id, 'queued'), JSON.stringify({ versionId: `v-${id}`, segments: [{ id: 's0', text: 'private transcript' }] }));
  store.publish(store.claim(id, 'minutes_queued'), JSON.stringify({ summary: 'completed minutes', ...(title === null ? {} : { meeting: { topic: title } }) }));
}

test('only approved subscriptions receive newly completed minutes as a minutes-only share', async () => {
  const store = new MeetingStore(':memory:'), subscriptions = new SubscriptionStore(store.db);
  try {
    const pending = subscriptions.apply('pending service', 'https://subscriber.example/hooks');
    const approved = subscriptions.apply('approved service', 'https://subscriber.example/approved', 'callback-secret-123');
    subscriptions.review(approved.id, true);
    completedMeeting(store);
    assert.equal(subscriptions.deliveries().length, 1);
    let request;
    await drainSubscriptions(store, { baseUrl: 'https://meetings.example/meeting-service', fetchImpl: async (url, options) => { request = { url, options }; return new Response(null, { status: 204 }); } });
    assert.equal(request.url, 'https://subscriber.example/approved');
    assert.equal(request.options.headers.authorization, 'Bearer callback-secret-123');
    const event = JSON.parse(request.options.body);
    assert.equal(event.event, 'meeting.minutes.completed');
    assert.equal(event.meetingId, 'm');
    assert.equal(event.title, 'm');
    assert.equal(event.completedAt, completedAt(store, 'm'));
    assert.match(event.shareUrl, /^https:\/\/meetings\.example\/meeting-service\/share#[A-Za-z0-9_-]{43}$/);
    const token = event.shareUrl.split('#')[1], shared = store.readShare(token);
    assert.match(shared.minutesMarkdown, /completed minutes/);
    assert.equal(shared.transcript, undefined);
    assert.equal(shared.allowDownload, false);
    assert.equal('downloadUrls' in event, false);
    assert.ok(event.shareExpiresAt > Math.floor(Date.now() / 1000) + 6 * 86400);
    assert.equal(subscriptions.deliveries()[0].deliveredAt > 0, true);
    assert.equal(store.db.prepare('SELECT COUNT(*) AS n FROM subscription_deliveries WHERE subscription_id=?').get(pending.id).n, 0);
  } finally { store.close(); }
});

test('callback includes the cleaned meeting title and completion time', async () => {
  const store = new MeetingStore(':memory:'), subscriptions = new SubscriptionStore(store.db);
  try {
    const { id } = subscriptions.apply('service', 'https://subscriber.example/hooks');subscriptions.review(id, true);
    completedMeeting(store, 'm', '  产品\u0000\n  周会  ');
    let event;
    await drainSubscriptions(store, { baseUrl: 'https://example.com/meeting-service', now: () => 2000, fetchImpl: async (_url, options) => { event = JSON.parse(options.body);return new Response(null, { status: 200 }); } });
    assert.equal(event.title, '产品 周会');
    assert.equal(event.completedAt, completedAt(store, 'm'));
  } finally { store.close(); }
});

test('failed callbacks retry with the same event and revocation cancels pending delivery', async () => {
  const store = new MeetingStore(':memory:'), subscriptions = new SubscriptionStore(store.db);
  try {
    const { id } = subscriptions.apply('service', 'https://subscriber.example/hooks');subscriptions.review(id, true);completedMeeting(store);
    const calls = [];
    await drainSubscriptions(store, { baseUrl: 'https://example.com/meeting-service', fetchImpl: async () => { calls.push(1); return new Response(null, { status: 503 }); } });
    let delivery = subscriptions.deliveries()[0];assert.equal(delivery.attempts, 1);assert.equal(delivery.lastError, 'http_503');assert.equal(delivery.deliveredAt, null);assert.equal(delivery.failedAt, null);assert.equal('payloadJson' in delivery, false);
    store.db.prepare('UPDATE subscription_deliveries SET next_attempt_at=0').run();
    await drainSubscriptions(store, { baseUrl: 'https://example.com/meeting-service', fetchImpl: async (_url, options) => { calls.push(JSON.parse(options.body).eventId); return new Response(null, { status: 200 }); } });
    assert.equal(calls[1], delivery.eventId);
    subscriptions.revoke(id);completedMeeting(store, 'later');
    assert.equal(subscriptions.deliveries().length, 1);
  } finally { store.close(); }
});

test('retranscription creates a distinct event but approval does not backfill old meetings', () => {
  const store = new MeetingStore(':memory:'), subscriptions = new SubscriptionStore(store.db);
  try {
    completedMeeting(store, 'before');
    const { id } = subscriptions.apply('service', 'https://subscriber.example/hooks');subscriptions.review(id, true);
    assert.equal(subscriptions.deliveries().length, 0);
    store.adminRetranscribe('before');
    store.publish(store.claim('before', 'queued'), JSON.stringify({ versionId: 'new', segments: [{ id: 's0', text: 'new' }] }));
    store.publish(store.claim('before', 'minutes_queued'), JSON.stringify({ summary: 'new minutes' }));
    completedMeeting(store, 'after');
    assert.equal(subscriptions.deliveries().length, 2);
    assert.equal(new Set(subscriptions.deliveries().map(item => item.eventId)).size, 2);
  } finally { store.close(); }
});

test('subscription application and administration enforce validation and authentication', async () => {
  const store = new MeetingStore(':memory:');
  try {
    const app = createMeetingRouter(store, { adminToken: 'admin123' });
    assert.equal((await app.request('/admin/subscriptions')).status, 401);
    const invalid = await app.request('/subscriptions', { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ name: 'bad', callbackUrl: 'http://insecure.example/hook' }) });
    assert.equal(invalid.status, 400);
    const applied = await app.request('/subscriptions', { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ name: 'service', callbackUrl: 'https://subscriber.example/hook', callbackToken: 'callback-secret-123' }) });
    assert.equal(applied.status, 202);const { id } = await applied.json();
    const listed = await app.request('/admin/subscriptions', { headers: { authorization: 'Bearer admin123' } });
    const body = await listed.json();assert.equal(body.subscriptions[0].status, 'pending');assert.equal(JSON.stringify(body).includes('callback-secret'), false);
    const approved = await app.request(`/admin/subscriptions/${id}/approve`, { method: 'POST', headers: { authorization: 'Bearer admin123' } });
    assert.equal(approved.status, 200);assert.equal((await approved.json()).status, 'approved');
    assert.equal((await app.request(`/admin/subscriptions/${id}/approve`, { method: 'POST', headers: { authorization: 'Bearer admin123' } })).status, 404);
  } finally { store.close(); }
});

test('deleting a meeting removes pending deliveries and generated shares remain subject to deletion', () => {
  const store = new MeetingStore(':memory:'), subscriptions = new SubscriptionStore(store.db);
  try {
    const { id } = subscriptions.apply('service', 'https://subscriber.example/hooks');subscriptions.review(id, true);completedMeeting(store);
    const share = store.createShare('m', false, 7);store.adminDelete('m');
    assert.equal(subscriptions.deliveries().length, 0);assert.equal(store.readShare(share.token), null);
  } finally { store.close(); }
});

test('public base URL rejects credentials, fragments and non-local plain HTTP', () => {
  assert.equal(publicBaseUrl('https://example.com/meeting-service/'), 'https://example.com/meeting-service');
  for (const value of ['http://example.com', 'https://user:pass@example.com', 'https://example.com/#fragment', 'not-a-url']) assert.throws(() => publicBaseUrl(value));
  assert.equal(publicBaseUrl('http://localhost:3443/meeting-service'), 'http://localhost:3443/meeting-service');
});


test('administrator configures shares at approval and updates them for future deliveries', async () => {
  const store = new MeetingStore(':memory:');
  try {
    const app = createMeetingRouter(store, { adminToken: 'admin123' });
    const applied = await app.request('/subscriptions', { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ name: 'service', callbackUrl: 'https://subscriber.example/hooks' }) });
    const { id } = await applied.json(), auth = { authorization: 'Bearer admin123', 'content-type': 'application/json' };
    const approved = await app.request(`/admin/subscriptions/${id}/approve`, { method: 'POST', headers: auth, body: JSON.stringify({ days: 30, includeTranscript: true, allowDownload: true }) });
    assert.equal(approved.status, 200);assert.deepEqual(await approved.json(), { id, status: 'approved', days: 30, includeTranscript: true, allowDownload: true });
    const updated = await app.request(`/admin/subscriptions/${id}`, { method: 'PATCH', headers: auth, body: JSON.stringify({ days: 14 }) });
    assert.deepEqual(await updated.json(), { id, status: 'approved', days: 14, includeTranscript: true, allowDownload: true });
    completedMeeting(store);
    let event;
    await drainSubscriptions(store, { baseUrl: 'https://example.com/meeting-service', fetchImpl: async (_url, options) => { event = JSON.parse(options.body); return new Response(null, { status: 200 }); } });
    const token = event.shareUrl.split('#')[1], shared = store.readShare(token);
    assert.equal(shared.transcript, 'private transcript');assert.equal(shared.allowDownload, true);
    assert.deepEqual(event.downloadUrls, {
      minutes: 'https://example.com/meeting-service/share/download/minutes',
      transcript: 'https://example.com/meeting-service/share/download/transcript'
    });
    for (const kind of ['minutes', 'transcript']) {
      const downloaded = await app.request(event.downloadUrls[kind].replace('https://example.com/meeting-service', ''), { headers: { authorization: `Bearer ${token}` } });
      assert.equal(downloaded.status, 200);
    }
    assert.ok(event.shareExpiresAt > Math.floor(Date.now() / 1000) + 13 * 86400);
    assert.ok(event.shareExpiresAt < Math.floor(Date.now() / 1000) + 15 * 86400);
    const invalid = await app.request(`/admin/subscriptions/${id}`, { method: 'PATCH', headers: auth, body: JSON.stringify({ days: 0 }) });
    assert.equal(invalid.status, 400);
  } finally { store.close(); }
});


test('ordinary retries reuse identical body and link after configuration changes',async()=>{
  const store=new MeetingStore(':memory:'),subscriptions=new SubscriptionStore(store.db);
  try{
    const {id}=subscriptions.apply('service','https://subscriber.example/hooks');subscriptions.review(id,true);completedMeeting(store);
    const bodies=[];let now=1000;
    const options={baseUrl:'https://example.com',now:()=>now,fetchImpl:async(_url,request)=>{bodies.push(request.body);return new Response(null,{status:bodies.length===1?503:200})}};
    await drainSubscriptions(store,options);subscriptions.updateShareConfig(id,{days:30});
    store.db.prepare('UPDATE subscription_deliveries SET next_attempt_at=0').run();await drainSubscriptions(store,options);
    assert.equal(bodies[0],bodies[1]);
    assert.equal(store.db.prepare('SELECT COUNT(*) AS n FROM meeting_shares').get().n,1);
    assert.equal(store.db.prepare('SELECT payload_json FROM subscription_deliveries').get().payload_json,null);
  }finally{store.close()}
});

test('expiry during retry becomes terminal without another upstream attempt',async()=>{
  const store=new MeetingStore(':memory:'),subscriptions=new SubscriptionStore(store.db);
  try{
    const {id}=subscriptions.apply('service','https://subscriber.example/hooks');subscriptions.review(id,true);completedMeeting(store);
    let now=1000,calls=0;
    const options={baseUrl:'https://example.com',now:()=>now,fetchImpl:async()=>{calls++;return new Response(null,{status:503})}};
    await drainSubscriptions(store,options);now+=8*86400;
    store.db.prepare('UPDATE subscription_deliveries SET next_attempt_at=0').run();await drainSubscriptions(store,options);
    const row=store.db.prepare('SELECT attempts,failed_at,last_error,payload_json FROM subscription_deliveries').get();
    assert.equal(calls,1);assert.equal(row.attempts,1);assert.equal(row.last_error,'share_expired');assert.ok(row.failed_at);assert.equal(row.payload_json,null);
  }finally{store.close()}
});


test('ordinary webhook terminal responses stop retries and clear payload',async()=>{
  const store=new MeetingStore(':memory:'),subscriptions=new SubscriptionStore(store.db);
  try{
    const {id}=subscriptions.apply('service','https://subscriber.example/hooks');subscriptions.review(id,true);completedMeeting(store);
    let calls=0;
    await drainSubscriptions(store,{baseUrl:'https://example.com',now:()=>1000,fetchImpl:async()=>{calls++;return new Response(null,{status:400})}});
    store.db.prepare('UPDATE subscription_deliveries SET next_attempt_at=0').run();
    await drainSubscriptions(store,{baseUrl:'https://example.com',now:()=>1004,fetchImpl:async()=>{calls++;return new Response(null,{status:204})}});
    const row=store.db.prepare('SELECT attempts,failed_at,last_error,payload_json FROM subscription_deliveries').get();
    assert.equal(calls,1);assert.equal(row.attempts,1);assert.equal(row.last_error,'http_400');assert.ok(row.failed_at);assert.equal(row.payload_json,null);
  }finally{store.close()}
});

test('ordinary webhook thrown errors use safe retryable classifications',async()=>{
  for(const [error,code] of [[new Error('raw detail'),'network_error'],[new DOMException('aborted','AbortError'),'timeout']]){
    const store=new MeetingStore(':memory:'),subscriptions=new SubscriptionStore(store.db);
    try{
      const {id}=subscriptions.apply('service','https://subscriber.example/hooks');subscriptions.review(id,true);completedMeeting(store);
      await drainSubscriptions(store,{baseUrl:'https://example.com',now:()=>1000,fetchImpl:async()=>{throw error}});
      const row=store.db.prepare('SELECT attempts,next_attempt_at,failed_at,last_error,payload_json FROM subscription_deliveries').get();
      assert.equal(row.attempts,1);assert.equal(row.next_attempt_at,1005);assert.equal(row.failed_at,null);assert.equal(row.last_error,code);assert.equal(typeof row.payload_json,'string');
    }finally{store.close()}
  }
});


test('reject and revoke remove only pending deliveries and preserve terminal audit',()=>{
  for(const action of ['reject','revoke']){
    const store=new MeetingStore(':memory:'),subscriptions=new SubscriptionStore(store.db);
    try{
      const {id}=subscriptions.apply('service','https://subscriber.example/hooks');completedMeeting(store);
      if(action==='revoke')subscriptions.review(id,true);
      const insert=store.db.prepare('INSERT INTO subscription_deliveries(event_id,subscription_id,meeting_id,meeting_generation,attempts,created_at,delivered_at,failed_at,last_error) VALUES(?,?,?,?,?,?,?,?,?)');
      insert.run('pending',id,'m',0,0,1,null,null,null);
      insert.run('delivered',id,'m',1,1,1,2,null,null);
      insert.run('failed',id,'m',2,1,1,null,3,'http_400');
      if(action==='reject')subscriptions.review(id,false,4);else subscriptions.revoke(id,4);
      assert.deepEqual(subscriptions.deliveries().map(row=>row.eventId).sort(),['delivered','failed']);
      assert.equal(store.db.prepare('SELECT status FROM meeting_subscriptions WHERE id=?').get(id).status,'revoked');
    }finally{store.close()}
  }
});

test('ordinary retries freeze bearer and download fields and retain approval',async()=>{
  const store=new MeetingStore(':memory:'),subscriptions=new SubscriptionStore(store.db);
  try{
    const {id}=subscriptions.apply('service','https://subscriber.example/hooks','callback-secret-123');subscriptions.review(id,true,1000,{includeTranscript:true,allowDownload:true});completedMeeting(store);
    const requests=[];let now=1000,calls=0;
    const options={baseUrl:'https://example.com',now:()=>now,fetchImpl:async(_url,request)=>{requests.push({headers:{...request.headers},body:JSON.parse(request.body)});calls++;return new Response(null,{status:calls===1?503:204})}};
    await drainSubscriptions(store,options);
    const delayed=store.db.prepare('SELECT attempts,next_attempt_at,delivered_at,failed_at,last_error,payload_json FROM subscription_deliveries').get();
    assert.equal(delayed.attempts,1);assert.equal(delayed.next_attempt_at,1005);assert.equal(delayed.delivered_at,null);assert.equal(delayed.failed_at,null);assert.equal(delayed.last_error,'http_503');assert.equal(typeof delayed.payload_json,'string');
    await drainSubscriptions(store,{...options,now:()=>1004});assert.equal(calls,1);
    now=1005;await drainSubscriptions(store,options);
    assert.equal(calls,2);assert.deepEqual(requests[0],requests[1]);assert.equal(requests[1].headers.authorization,'Bearer callback-secret-123');
    assert.deepEqual(requests[1].body.downloadUrls,{minutes:'https://example.com/share/download/minutes',transcript:'https://example.com/share/download/transcript'});
    const delivered=store.db.prepare('SELECT attempts,delivered_at,failed_at,last_error,payload_json FROM subscription_deliveries').get();
    assert.equal(delivered.attempts,1);assert.equal(delivered.delivered_at,1005);assert.equal(delivered.failed_at,null);assert.equal(delivered.last_error,null);assert.equal(delivered.payload_json,null);
    assert.equal(store.db.prepare('SELECT status FROM meeting_subscriptions WHERE id=?').get(id).status,'approved');
  }finally{store.close()}
});

test('ordinary pending delivery expires exactly at share_expires_at',async()=>{
  const store=new MeetingStore(':memory:'),subscriptions=new SubscriptionStore(store.db);
  try{
    const {id}=subscriptions.apply('service','https://subscriber.example/hooks');subscriptions.review(id,true);completedMeeting(store);
    let now=1000,calls=0;
    await drainSubscriptions(store,{baseUrl:'https://example.com',now:()=>now,fetchImpl:async()=>{calls++;return new Response(null,{status:503})}});
    const expiry=store.db.prepare('SELECT share_expires_at FROM subscription_deliveries').get().share_expires_at;
    now=expiry;store.db.prepare('UPDATE subscription_deliveries SET next_attempt_at=?').run(expiry);
    await drainSubscriptions(store,{baseUrl:'https://example.com',now:()=>now,fetchImpl:async()=>{calls++;return new Response(null,{status:204})}});
    const row=store.db.prepare('SELECT attempts,delivered_at,failed_at,last_error,payload_json FROM subscription_deliveries').get();
    assert.equal(calls,1);assert.equal(row.attempts,1);assert.equal(row.delivered_at,null);assert.equal(row.failed_at,expiry);assert.equal(row.last_error,'share_expired');assert.equal(row.payload_json,null);
    assert.equal(store.db.prepare('SELECT status FROM meeting_subscriptions WHERE id=?').get(id).status,'approved');
  }finally{store.close()}
});
