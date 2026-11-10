import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { MeetingStore } from '../src/store.mjs';
import { SubscriptionStore, drainSubscriptions, publicBaseUrl } from '../src/subscriptions.mjs';
import { createMeetingRouter } from '../src/routes.mjs';

function completedMeeting(store, id = 'm') {
  const audio = Buffer.from(`audio-${id}`), digest = createHash('sha256').update(audio).digest('hex');
  store.create('d', id);store.put('d', id, 0, audio, digest);
  store.finalize('d', id, { count: 1, bytes: audio.length, digest, reason: 'user' });
  store.publish(store.claim(id, 'queued'), JSON.stringify({ versionId: `v-${id}`, segments: [{ id: 's0', text: 'private transcript' }] }));
  store.publish(store.claim(id, 'minutes_queued'), JSON.stringify({ summary: 'completed minutes' }));
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

test('failed callbacks retry with the same event and revocation cancels pending delivery', async () => {
  const store = new MeetingStore(':memory:'), subscriptions = new SubscriptionStore(store.db);
  try {
    const { id } = subscriptions.apply('service', 'https://subscriber.example/hooks');subscriptions.review(id, true);completedMeeting(store);
    const calls = [];
    await drainSubscriptions(store, { baseUrl: 'https://example.com/meeting-service', fetchImpl: async () => { calls.push(1); return new Response(null, { status: 503 }); } });
    let delivery = subscriptions.deliveries()[0];assert.equal(delivery.attempts, 1);assert.equal(delivery.lastError, 'http_503');assert.equal(delivery.deliveredAt, null);
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
