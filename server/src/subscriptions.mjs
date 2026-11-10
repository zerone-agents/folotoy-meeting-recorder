import { randomUUID } from 'node:crypto';

const validUrl = (value, allowLocalHttp = true) => {
  if (typeof value !== 'string' || value.length > 2048) return null;
  try {
    const url = new URL(value);
    const local = ['localhost', '127.0.0.1', '[::1]'].includes(url.hostname);
    if (url.protocol !== 'https:' && !(allowLocalHttp && local && url.protocol === 'http:')) return null;
    if (url.username || url.password || url.hash) return null;
    return url.toString();
  } catch { return null; }
};

export function publicBaseUrl(value) {
  const normalized = validUrl(value, true);
  if (!normalized || new URL(normalized).search) throw new Error('invalid_public_base_url');
  return normalized.replace(/\/$/, '');
}

const shareConfig = ({ days = 7, includeTranscript = false, allowDownload = false } = {}) => {
  if (!Number.isSafeInteger(days) || days < 1 || days > 365 || typeof includeTranscript !== 'boolean' || typeof allowDownload !== 'boolean') throw new Error('invalid');
  return { days, includeTranscript, allowDownload };
};

export function initializeSubscriptions(db) {
  db.exec(`CREATE TABLE IF NOT EXISTS meeting_subscriptions (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    callback_url TEXT NOT NULL,
    callback_token TEXT,
    status TEXT NOT NULL CHECK(status IN ('pending','approved','revoked')),
    created_at INTEGER NOT NULL,
    reviewed_at INTEGER
  );
  CREATE TABLE IF NOT EXISTS subscription_deliveries (
    event_id TEXT PRIMARY KEY,
    subscription_id TEXT NOT NULL REFERENCES meeting_subscriptions(id),
    meeting_id TEXT NOT NULL REFERENCES meetings(id),
    meeting_generation INTEGER NOT NULL,
    attempts INTEGER NOT NULL DEFAULT 0,
    next_attempt_at INTEGER NOT NULL DEFAULT 0,
    created_at INTEGER NOT NULL,
    delivered_at INTEGER,
    last_error TEXT,
    UNIQUE(subscription_id,meeting_id,meeting_generation)
  );`);
  const columns = new Set(db.prepare('PRAGMA table_info(meeting_subscriptions)').all().map(column => column.name));
  if (!columns.has('share_days')) db.exec('ALTER TABLE meeting_subscriptions ADD COLUMN share_days INTEGER NOT NULL DEFAULT 7 CHECK(share_days BETWEEN 1 AND 365)');
  if (!columns.has('include_transcript')) db.exec('ALTER TABLE meeting_subscriptions ADD COLUMN include_transcript INTEGER NOT NULL DEFAULT 0 CHECK(include_transcript IN (0,1))');
  if (!columns.has('allow_download')) db.exec('ALTER TABLE meeting_subscriptions ADD COLUMN allow_download INTEGER NOT NULL DEFAULT 0 CHECK(allow_download IN (0,1))');
}

export class SubscriptionStore {
  constructor(db) { this.db = db; }
  apply(name, callbackUrl, callbackToken = null, now = Math.floor(Date.now() / 1000)) {
    const url = validUrl(callbackUrl);
    if (typeof name !== 'string' || !name.trim() || name.length > 100 || !url ||
        (callbackToken !== null && (typeof callbackToken !== 'string' || callbackToken.length < 16 || callbackToken.length > 512 || /\s/.test(callbackToken))) ||
        !Number.isSafeInteger(now) || now < 0) throw new Error('invalid');
    const id = randomUUID();
    this.db.prepare("INSERT INTO meeting_subscriptions(id,name,callback_url,callback_token,status,created_at) VALUES(?,?,?,?,'pending',?)")
      .run(id, name.trim(), url, callbackToken, now);
    return { id, status: 'pending' };
  }
  list() {
    return this.db.prepare('SELECT id,name,callback_url AS callbackUrl,status,share_days AS days,include_transcript AS includeTranscript,allow_download AS allowDownload,created_at AS createdAt,reviewed_at AS reviewedAt FROM meeting_subscriptions ORDER BY created_at,rowid').all()
      .map(row => ({ ...row, includeTranscript: Boolean(row.includeTranscript), allowDownload: Boolean(row.allowDownload) }));
  }
  review(id, approve, now = Math.floor(Date.now() / 1000), config = {}) {
    if (typeof id !== 'string' || !Number.isSafeInteger(now) || now < 0) throw new Error('invalid');
    const share = shareConfig(config), status = approve ? 'approved' : 'revoked';
    const result = this.db.prepare("UPDATE meeting_subscriptions SET status=?,reviewed_at=?,share_days=?,include_transcript=?,allow_download=? WHERE id=? AND status='pending'")
      .run(status, now, share.days, share.includeTranscript ? 1 : 0, share.allowDownload ? 1 : 0, id);
    if (!result.changes) throw new Error('not_found');
    if (!approve) this.db.prepare('DELETE FROM subscription_deliveries WHERE subscription_id=? AND delivered_at IS NULL').run(id);
    return { id, status, ...share };
  }
  updateShareConfig(id, config) {
    if (typeof id !== 'string') throw new Error('invalid');
    const current = this.db.prepare("SELECT share_days AS days,include_transcript AS includeTranscript,allow_download AS allowDownload FROM meeting_subscriptions WHERE id=? AND status='approved'").get(id);
    if (!current) throw new Error('not_found');
    const share = shareConfig({ days: current.days, includeTranscript: Boolean(current.includeTranscript), allowDownload: Boolean(current.allowDownload), ...config });
    this.db.prepare("UPDATE meeting_subscriptions SET share_days=?,include_transcript=?,allow_download=? WHERE id=?")
      .run(share.days, share.includeTranscript ? 1 : 0, share.allowDownload ? 1 : 0, id);
    return { id, status: 'approved', ...share };
  }
  revoke(id, now = Math.floor(Date.now() / 1000)) {
    if (typeof id !== 'string' || !Number.isSafeInteger(now) || now < 0) throw new Error('invalid');
    const result = this.db.prepare("UPDATE meeting_subscriptions SET status='revoked',reviewed_at=? WHERE id=? AND status='approved'").run(now, id);
    if (!result.changes) throw new Error('not_found');
    this.db.prepare('DELETE FROM subscription_deliveries WHERE subscription_id=? AND delivered_at IS NULL').run(id);
    return { id, status: 'revoked' };
  }
  deliveries() {
    return this.db.prepare('SELECT event_id AS eventId,subscription_id AS subscriptionId,meeting_id AS meetingId,meeting_generation AS meetingGeneration,attempts,delivered_at AS deliveredAt,last_error AS lastError FROM subscription_deliveries ORDER BY created_at,event_id').all();
  }
}

export function queueSubscriptionDeliveries(db, meetingId, generation, now = Math.floor(Date.now() / 1000)) {
  const subscriptions = db.prepare("SELECT id FROM meeting_subscriptions WHERE status='approved'").all();
  for (const subscription of subscriptions) {
    db.prepare(`INSERT OR IGNORE INTO subscription_deliveries(event_id,subscription_id,meeting_id,meeting_generation,created_at)
      VALUES(?,?,?,?,?)`).run(randomUUID(), subscription.id, meetingId, generation, now);
  }
}

async function deliver(store, baseUrl, row, fetchImpl, timeoutMs) {
  const subscription = store.db.prepare("SELECT callback_url,callback_token,share_days,include_transcript,allow_download FROM meeting_subscriptions WHERE id=? AND status='approved'").get(row.subscription_id);
  if (!subscription) {
    store.db.prepare('DELETE FROM subscription_deliveries WHERE event_id=?').run(row.event_id);
    return;
  }
  const share = store.createShare(row.meeting_id, Boolean(subscription.include_transcript), subscription.share_days, Math.floor(Date.now() / 1000), Boolean(subscription.allow_download));
  const payload = { event: 'meeting.minutes.completed', eventId: row.event_id, meetingId: row.meeting_id,
    shareUrl: `${baseUrl}/share#${share.token}`, shareExpiresAt: share.expiresAt };
  if (subscription.allow_download) payload.downloadUrls = {
    minutes: `${baseUrl}/share/download/minutes`,
    transcript: `${baseUrl}/share/download/transcript`
  };
  const body = JSON.stringify(payload);
  const headers = { 'content-type': 'application/json' };
  if (subscription.callback_token) headers.authorization = `Bearer ${subscription.callback_token}`;
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), timeoutMs);
  try {
    const response = await fetchImpl(subscription.callback_url, { method: 'POST', headers, body, signal: controller.signal, redirect: 'error' });
    if (!response.ok) throw new Error(`http_${response.status}`);
    store.db.prepare('UPDATE subscription_deliveries SET delivered_at=CAST(strftime(\'%s\',\'now\') AS INTEGER),last_error=NULL WHERE event_id=?').run(row.event_id);
  } catch (error) {
    const attempts = row.attempts + 1;
    const code = error?.name === 'AbortError' ? 'timeout' : /^http_\d+$/.test(error?.message) ? error.message : 'network_error';
    store.db.prepare("UPDATE subscription_deliveries SET attempts=?,next_attempt_at=CAST(strftime('%s','now') AS INTEGER)+?,last_error=? WHERE event_id=?")
      .run(attempts, Math.min(300, 5 * 2 ** Math.min(attempts - 1, 6)), code, row.event_id);
  } finally { clearTimeout(timer); }
}

export async function drainSubscriptions(store, { baseUrl, fetchImpl = fetch, timeoutMs = 10000, limit = 20 } = {}) {
  const normalized = publicBaseUrl(baseUrl);
  const rows = store.db.prepare(`SELECT d.event_id,d.subscription_id,d.meeting_id,d.attempts
    FROM subscription_deliveries d JOIN meeting_subscriptions s ON s.id=d.subscription_id
    JOIN meetings m ON m.id=d.meeting_id
    WHERE d.delivered_at IS NULL AND d.next_attempt_at<=CAST(strftime('%s','now') AS INTEGER)
      AND s.status='approved' AND m.state='completed' AND m.minutes IS NOT NULL
    ORDER BY d.created_at,d.event_id LIMIT ?`).all(limit);
  for (const row of rows) await deliver(store, normalized, row, fetchImpl, timeoutMs);
  return rows.length;
}

export function startSubscriptionWorker(store, { baseUrl = null, intervalMs = 3000, onError = () => {}, ...options } = {}) {
  if (!Number.isSafeInteger(intervalMs) || intervalMs < 1) throw new Error('invalid_interval');
  let active = false, stopped = false;
  const tick = async () => {
    if (active || stopped || !baseUrl) return;
    active = true;
    try { await drainSubscriptions(store, { baseUrl, ...options }); }
    catch { onError('subscription_delivery_failed'); }
    finally { active = false; }
  };
  void tick();
  const timer = setInterval(tick, intervalMs);
  return async () => { stopped = true; clearInterval(timer); while (active) await new Promise(resolve => setTimeout(resolve, 5)); };
}
