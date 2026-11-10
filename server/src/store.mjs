import { DatabaseSync } from 'node:sqlite';
import { createHash, randomBytes, randomUUID } from 'node:crypto';
import {tombstoneMetadataClean} from './tombstone.mjs';
import {renderMinutesMarkdown,containsReviewNotices} from './markdown-minutes.mjs';
import {validateShareSchema} from './share-schema.mjs';
import {initializeSubscriptions,queueSubscriptionDeliveries} from './subscriptions.mjs';

const hash = (data) => createHash('sha256').update(data).digest('hex');
const view = data => Buffer.from(data.buffer,data.byteOffset,data.byteLength);
const receipt = m => ({count:m.chunks,bytes:m.bytes,digest:m.digest,reason:m.reason,recordedAt:m.recorded_at,timeTrust:m.time_trust});
const MAX_NEW_CHUNKS=8192,MAX_PENDING_MEETINGS=64;
const validId = (id) => typeof id === 'string' && /^[A-Za-z0-9_-]{1,100}$/.test(id);
export class StoreError extends Error { constructor(code) { super(code); this.code = code; } }
const fail = (code) => { throw new StoreError(code); };

/** SQLite owns both chunk bytes and task metadata, so confirmation is atomic. */
export class MeetingStore {
  constructor(path,{storageLimitBytes=null}={}) {
    if(storageLimitBytes!==null&&(!Number.isSafeInteger(storageLimitBytes)||storageLimitBytes<1))fail('invalid');this.storageLimitBytes=storageLimitBytes;
    this.db = new DatabaseSync(path);
    this.db.exec(`PRAGMA busy_timeout=5000; PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL; PRAGMA foreign_keys=ON; PRAGMA secure_delete=ON;
      CREATE TABLE IF NOT EXISTS service_metadata (key TEXT PRIMARY KEY, value TEXT NOT NULL);
      CREATE TABLE IF NOT EXISTS devices (id TEXT PRIMARY KEY, token_hash TEXT NOT NULL UNIQUE, revoked INTEGER NOT NULL DEFAULT 0);
      CREATE TABLE IF NOT EXISTS revoked_tokens (token_hash TEXT PRIMARY KEY);
      CREATE TABLE IF NOT EXISTS device_credentials (token_hash TEXT PRIMARY KEY, device_id TEXT NOT NULL);
      CREATE TABLE IF NOT EXISTS revocation_events (event_id TEXT PRIMARY KEY, device_id TEXT NOT NULL, credential_id TEXT NOT NULL, created_at INTEGER NOT NULL, actor_id TEXT);
      CREATE TABLE IF NOT EXISTS meetings (id TEXT PRIMARY KEY, owner TEXT NOT NULL, state TEXT NOT NULL, reason TEXT, chunks INTEGER, bytes INTEGER, digest TEXT, generation INTEGER NOT NULL DEFAULT 0, lease_token TEXT, lease_until INTEGER, transcript TEXT, minutes TEXT, attempts INTEGER NOT NULL DEFAULT 0, last_error TEXT, failed_stage TEXT, retry_at INTEGER NOT NULL DEFAULT 0);
      CREATE TABLE IF NOT EXISTS transcript_versions (meeting_id TEXT NOT NULL REFERENCES meetings(id), version_id TEXT NOT NULL, data TEXT NOT NULL, PRIMARY KEY(meeting_id,version_id));
      CREATE TABLE IF NOT EXISTS processing_attempts (meeting_id TEXT NOT NULL REFERENCES meetings(id), generation INTEGER NOT NULL, stage TEXT NOT NULL, started_at INTEGER NOT NULL, ended_at INTEGER, outcome TEXT, error TEXT, PRIMARY KEY(meeting_id,generation));
      CREATE TABLE IF NOT EXISTS minutes_batches (meeting_id TEXT NOT NULL REFERENCES meetings(id), transcript_version TEXT NOT NULL, batch_key TEXT NOT NULL, data TEXT NOT NULL, PRIMARY KEY(meeting_id,transcript_version,batch_key));
      CREATE TABLE IF NOT EXISTS minutes_versions (meeting_id TEXT NOT NULL REFERENCES meetings(id), generation INTEGER NOT NULL, transcript_version TEXT NOT NULL, data TEXT NOT NULL, PRIMARY KEY(meeting_id,generation));
      CREATE TABLE IF NOT EXISTS meeting_reviews (meeting_id TEXT PRIMARY KEY REFERENCES meetings(id), review_key TEXT NOT NULL);
      CREATE TABLE IF NOT EXISTS minutes_edits (meeting_id TEXT PRIMARY KEY REFERENCES meetings(id), markdown TEXT NOT NULL, revision INTEGER NOT NULL, source_hash TEXT);
      CREATE TABLE IF NOT EXISTS chunks (meeting_id TEXT NOT NULL REFERENCES meetings(id), seq INTEGER NOT NULL, digest TEXT NOT NULL, data BLOB NOT NULL, PRIMARY KEY(meeting_id, seq));`);
    if(!this.db.prepare('PRAGMA table_info(minutes_edits)').all().some(c=>c.name==='source_hash'))this.db.exec('ALTER TABLE minutes_edits ADD COLUMN source_hash TEXT');
    if(!this.db.prepare('PRAGMA table_info(minutes_edits)').all().some(c=>c.name==='source_version'))this.db.exec('ALTER TABLE minutes_edits ADD COLUMN source_version TEXT');
    this.db.prepare("INSERT OR IGNORE INTO service_metadata(key,value) VALUES('instance_id',?)").run(randomUUID());
    const columns=new Set(this.db.prepare('PRAGMA table_info(meetings)').all().map(c=>c.name));
    if(!columns.has('failed_stage'))this.db.exec('ALTER TABLE meetings ADD COLUMN failed_stage TEXT');
    if(!columns.has('retry_at'))this.db.exec('ALTER TABLE meetings ADD COLUMN retry_at INTEGER NOT NULL DEFAULT 0');
    if(!columns.has('completed_at'))this.db.exec('ALTER TABLE meetings ADD COLUMN completed_at INTEGER');
    if(!columns.has('recorded_at'))this.db.exec('ALTER TABLE meetings ADD COLUMN recorded_at INTEGER');
    if(!columns.has('time_trust'))this.db.exec("ALTER TABLE meetings ADD COLUMN time_trust TEXT NOT NULL DEFAULT 'uncalibrated'");
    if(!columns.has('chunk_limit'))this.db.exec('ALTER TABLE meetings ADD COLUMN chunk_limit INTEGER NOT NULL DEFAULT 100000');
    if(!this.db.prepare('PRAGMA table_info(revocation_events)').all().some(c=>c.name==='actor_id'))this.db.exec('ALTER TABLE revocation_events ADD COLUMN actor_id TEXT');
    this.db.exec('INSERT OR IGNORE INTO device_credentials(token_hash,device_id) SELECT token_hash,id FROM devices');
    // Upgrade older tombstones to retain only identifiers and fencing state.
    this.db.exec("UPDATE meetings SET reason=NULL,chunks=NULL,bytes=NULL,digest=NULL,recorded_at=NULL,time_trust='uncalibrated',completed_at=NULL,transcript=NULL,minutes=NULL,attempts=0,last_error=NULL,failed_stage=NULL,retry_at=0 WHERE state='deleted' AND (reason IS NOT NULL OR chunks IS NOT NULL OR bytes IS NOT NULL OR digest IS NOT NULL OR recorded_at IS NOT NULL OR time_trust!='uncalibrated' OR completed_at IS NOT NULL OR transcript IS NOT NULL OR minutes IS NOT NULL OR attempts!=0 OR last_error IS NOT NULL OR failed_stage IS NOT NULL OR retry_at!=0)");
    this.tx(()=>{
      const table=this.db.prepare("SELECT 1 FROM sqlite_master WHERE type='table' AND name='meeting_shares'").get();
      const marker=this.db.prepare("SELECT value FROM service_metadata WHERE key='share_schema'").get();
      if(!table&&!marker){this.db.exec("CREATE TABLE meeting_shares (token_hash TEXT PRIMARY KEY, meeting_id TEXT NOT NULL REFERENCES meetings(id), include_transcript INTEGER NOT NULL CHECK(include_transcript IN (0,1)), expires_at INTEGER NOT NULL); INSERT INTO service_metadata(key,value) VALUES('share_schema','1')");}
      else if(!table||!['1','2'].includes(marker?.value))throw Error('invalid_share_schema');
      const current=this.db.prepare("SELECT value FROM service_metadata WHERE key='share_schema'").get().value;
      validateShareSchema(this.db,Number(current));
      if(current==='1'){this.db.exec("ALTER TABLE meeting_shares ADD COLUMN allow_download INTEGER NOT NULL DEFAULT 0 CHECK(allow_download IN (0,1)); UPDATE service_metadata SET value='2' WHERE key='share_schema'");validateShareSchema(this.db,2);}
    });
    initializeSubscriptions(this.db);
    // Complete deletions interrupted after the durable tombstone was written.
    this.deleteCursor=0;this.recoverDeletes();
    // Upgrade databases created before version history was introduced atomically.
    this.tx(()=>{
      for(const row of this.db.prepare("SELECT id,generation,transcript,minutes FROM meetings WHERE state NOT IN ('deleted','deleting') AND transcript IS NOT NULL").all()) {
        let transcript; try { transcript=JSON.parse(row.transcript); } catch { fail('invalid_stored_transcript'); }
        if(typeof transcript.versionId!=='string'||!transcript.versionId||!Array.isArray(transcript.segments)||!transcript.segments.length) fail('invalid_stored_transcript');
        this.db.prepare('INSERT OR IGNORE INTO transcript_versions(meeting_id,version_id,data) VALUES(?,?,?)').run(row.id,transcript.versionId,row.transcript);
        if(row.minutes!==null && !this.db.prepare('SELECT 1 FROM minutes_versions WHERE meeting_id=? AND transcript_version=? AND data=?').get(row.id,transcript.versionId,row.minutes)) {
          try { JSON.parse(row.minutes); } catch { fail('invalid_stored_minutes'); }
          this.db.prepare('INSERT INTO minutes_versions(meeting_id,generation,transcript_version,data) VALUES(?,?,?,?)').run(row.id,row.generation,transcript.versionId,row.minutes);
        }
      }
    });
  }
  reviewSnapshot(m,edit){
    if(m.state!=='completed'||!m.minutes||!m.transcript)return null;
    let transcript;try{transcript=JSON.parse(m.transcript)}catch{return null}
    if(!transcript?.versionId||!Array.isArray(transcript.segments)||!this.db.prepare('SELECT 1 FROM minutes_versions WHERE meeting_id=? AND transcript_version=? AND data=?').get(m.id,transcript.versionId,m.minutes))return null;
    if(edit&&(edit.source_hash!==hash(m.minutes)||edit.source_version!==transcript.versionId))return null;
    return hash(JSON.stringify([m.transcript,m.minutes,edit?.markdown??null,edit?.revision??0]));
  }
  markReviewed(id,key){if(typeof key!=='string'||!/^[a-f0-9]{64}$/.test(key))fail('invalid');return this.tx(()=>{const m=this.adminGet(id);if(!m.reviewKey||m.reviewKey!==key)fail('conflict');this.db.prepare('INSERT INTO meeting_reviews(meeting_id,review_key) VALUES(?,?) ON CONFLICT(meeting_id) DO UPDATE SET review_key=excluded.review_key').run(id,key);return {reviewed:true};});}
  createShare(id,includeTranscript,days,now=Math.floor(Date.now()/1000),allowDownload=false) {
    if(!validId(id)||typeof includeTranscript!=='boolean'||typeof allowDownload!=='boolean'||!Number.isSafeInteger(days)||days<1||days>365||!Number.isSafeInteger(now)||now<0)fail('invalid');
    const token=randomBytes(32).toString('base64url'),expiresAt=now+days*86400;
    return this.tx(()=>{const m=this.db.prepare("SELECT minutes FROM meetings WHERE id=? AND state='completed'").get(id);if(!m||!m.minutes)fail('conflict');this.db.prepare('INSERT INTO meeting_shares(token_hash,meeting_id,include_transcript,expires_at,allow_download) VALUES(?,?,?,?,?)').run(hash(token),id,includeTranscript?1:0,expiresAt,allowDownload?1:0);return {token,expiresAt};});
  }
  readShare(token,now=Math.floor(Date.now()/1000)) {
    if(typeof token!=='string'||!/^[A-Za-z0-9_-]{43}$/.test(token)||!Number.isSafeInteger(now))return null;
    this.db.exec('BEGIN');try{
      const m=this.db.prepare("SELECT s.include_transcript,s.allow_download,m.id,m.state,m.reason,m.minutes,m.transcript FROM meeting_shares s JOIN meetings m ON m.id=s.meeting_id WHERE s.token_hash=? AND s.expires_at>? AND m.state NOT IN ('deleting','deleted','cancelled')").get(hash(token),now);
      if(!m){this.db.exec('COMMIT');return null}
      if(m.state!=='completed'||!m.minutes||!m.transcript){this.db.exec('COMMIT');return {unavailable:true}}
      const transcript=JSON.parse(m.transcript),minutes=JSON.parse(m.minutes);
      const version=this.db.prepare('SELECT 1 FROM minutes_versions WHERE meeting_id=? AND transcript_version=? AND data=?').get(m.id,transcript.versionId,m.minutes);
      if(!version||!Array.isArray(transcript.segments)){this.db.exec('COMMIT');return {unavailable:true}}
      const edit=this.db.prepare('SELECT markdown,revision,source_hash,source_version FROM minutes_edits WHERE meeting_id=?').get(m.id);
      const editMatches=Boolean(edit&&edit.source_hash===hash(m.minutes)&&edit.source_version===transcript.versionId);
      const reviewKey=this.reviewSnapshot(m,edit),reviewed=Boolean(reviewKey&&this.db.prepare('SELECT 1 FROM meeting_reviews WHERE meeting_id=? AND review_key=?').get(m.id,reviewKey));
      const result={reviewed,allowDownload:Boolean(m.allow_download),minutesMarkdown:editMatches?edit.markdown:renderMinutesMarkdown(minutes,{captureReason:m.reason,includeReviewNotice:false}),minutesEditedStale:Boolean(edit&&!editMatches),containsReviewNotice:Boolean(editMatches&&containsReviewNotices(edit.markdown)),incomplete:['limit','storage','interrupted'].includes(m.reason)||Boolean(minutes.incomplete)};
      if(m.include_transcript)result.transcript=transcript.segments.map(s=>s.text).join('');
      this.db.exec('COMMIT');return result;
    }catch(e){this.db.exec('ROLLBACK');throw e}
  }
  purgeShares(now=Math.floor(Date.now()/1000)){return this.db.prepare('DELETE FROM meeting_shares WHERE expires_at<=?').run(now).changes}
  close() { this.db.close(); }
  expireCompleted(retentionSeconds,limit=20) { if(!Number.isSafeInteger(retentionSeconds)||retentionSeconds<1||!Number.isSafeInteger(limit)||limit<1)fail('invalid');
    const rows=this.db.prepare("SELECT id,owner FROM meetings WHERE state='completed' AND completed_at IS NOT NULL AND completed_at<=CAST(strftime('%s','now') AS INTEGER)-? ORDER BY completed_at LIMIT ?").all(retentionSeconds,limit);
    for(const row of rows){const changed=this.tx(()=>this.db.prepare("UPDATE meetings SET state='deleting',generation=generation+1,lease_token=NULL,lease_until=NULL WHERE id=? AND state='completed' AND completed_at IS NOT NULL AND completed_at<=CAST(strftime('%s','now') AS INTEGER)-?").run(row.id,retentionSeconds).changes);if(changed)try{this.finishDelete(row.id)}catch{/* Keep the tombstone; periodic recovery will retry. */}}return rows.length;
  }
  finishDeleteInTransaction(id) { this.db.prepare('DELETE FROM meeting_reviews WHERE meeting_id=?').run(id);this.db.prepare('DELETE FROM minutes_edits WHERE meeting_id=?').run(id);this.db.prepare('DELETE FROM meeting_shares WHERE meeting_id=?').run(id);this.db.prepare('DELETE FROM subscription_deliveries WHERE meeting_id=?').run(id);this.db.prepare('DELETE FROM chunks WHERE meeting_id=?').run(id);this.db.prepare('DELETE FROM transcript_versions WHERE meeting_id=?').run(id);this.db.prepare('DELETE FROM minutes_versions WHERE meeting_id=?').run(id);this.db.prepare('DELETE FROM minutes_batches WHERE meeting_id=?').run(id);this.db.prepare('DELETE FROM processing_attempts WHERE meeting_id=?').run(id);const remains=this.db.prepare('SELECT 1 FROM meeting_shares WHERE meeting_id=? UNION ALL SELECT 1 FROM subscription_deliveries WHERE meeting_id=? UNION ALL SELECT 1 FROM chunks WHERE meeting_id=? UNION ALL SELECT 1 FROM transcript_versions WHERE meeting_id=? UNION ALL SELECT 1 FROM minutes_versions WHERE meeting_id=? UNION ALL SELECT 1 FROM minutes_batches WHERE meeting_id=? UNION ALL SELECT 1 FROM processing_attempts WHERE meeting_id=? LIMIT 1').get(id,id,id,id,id,id,id);if(this.db.prepare('SELECT 1 FROM meeting_reviews WHERE meeting_id=? UNION ALL SELECT 1 FROM minutes_edits WHERE meeting_id=?').get(id,id))fail('deletion_incomplete');if(remains)fail('deletion_incomplete');const changed=this.db.prepare("UPDATE meetings SET state='deleted',reason=NULL,chunks=NULL,bytes=NULL,digest=NULL,recorded_at=NULL,time_trust='uncalibrated',completed_at=NULL,transcript=NULL,minutes=NULL,attempts=0,last_error=NULL,failed_stage=NULL,retry_at=0 WHERE id=? AND state='deleting'").run(id);if(changed.changes!==1||!tombstoneMetadataClean(this.db,id,'deleted'))fail('deletion_incomplete'); }
  recoverDeletes(limit=20) { if(!Number.isSafeInteger(limit)||limit<1||limit>1000)fail('invalid');
    let rows=this.db.prepare("SELECT rowid AS cursor,id FROM meetings WHERE state='deleting' AND rowid>? ORDER BY rowid LIMIT ?").all(this.deleteCursor,limit);
    if(!rows.length&&this.deleteCursor){this.deleteCursor=0;rows=this.db.prepare("SELECT rowid AS cursor,id FROM meetings WHERE state='deleting' ORDER BY rowid LIMIT ?").all(limit)}
    let failed=0;for(const row of rows){this.deleteCursor=row.cursor;try{this.finishDelete(row.id)}catch{failed++}}return {attempted:rows.length,failed};
  }
  ready() { return this.db.prepare("SELECT id,state FROM meetings WHERE (state IN ('queued','minutes_queued') AND retry_at<=CAST(strftime('%s','now') AS INTEGER)) OR (state IN ('asr_running','minutes_running') AND lease_until<=CAST(strftime('%s','now') AS INTEGER)) ORDER BY rowid LIMIT 20").all(); }
  job(lease) { const m=this.db.prepare("SELECT id,state,reason,transcript,recorded_at,time_trust FROM meetings WHERE id=? AND state=? AND generation=? AND lease_token=? AND lease_until>CAST(strftime('%s','now') AS INTEGER)").get(lease.id,lease.stage,lease.generation,lease.token); if(!m) fail('stale_lease'); return m; }
  closeAttempt(lease,outcome,error=null) { this.db.prepare("UPDATE processing_attempts SET ended_at=CAST(strftime('%s','now') AS INTEGER),outcome=?,error=? WHERE meeting_id=? AND generation=? AND ended_at IS NULL").run(outcome,error,lease.id,lease.generation); }
  release(lease) { const stage=lease.stage==='asr_running'?'queued':lease.stage==='minutes_running'?'minutes_queued':null;if(!stage)fail('invalid');return this.tx(()=>{const changed=this.db.prepare("UPDATE meetings SET state=?,attempts=MAX(0,attempts-1),retry_at=0,lease_token=NULL,lease_until=NULL WHERE id=? AND state=? AND generation=? AND lease_token=? AND lease_until>CAST(strftime('%s','now') AS INTEGER)").run(stage,lease.id,lease.stage,lease.generation,lease.token).changes===1;if(changed)this.closeAttempt(lease,'released');return changed;}); }
  retry(lease, code, maxAttempts=3) { if(!/^[a-z0-9_]{1,80}$/.test(code)) code='processing_failed'; const stage=lease.stage==='asr_running'?'queued':'minutes_queued';return this.tx(()=>{const changed=this.db.prepare("UPDATE meetings SET state=CASE WHEN attempts>=? THEN 'failed' ELSE ? END,failed_stage=CASE WHEN attempts>=? THEN ? ELSE NULL END,retry_at=CAST(strftime('%s','now') AS INTEGER)+MIN(60,5*attempts),last_error=?,lease_token=NULL,lease_until=NULL WHERE id=? AND state=? AND generation=? AND lease_token=? AND lease_until>CAST(strftime('%s','now') AS INTEGER)").run(maxAttempts,stage,maxAttempts,stage,code,lease.id,lease.stage,lease.generation,lease.token).changes===1;if(changed)this.closeAttempt(lease,'failed',code);return changed;}); }
  tx(fn) { this.db.exec('BEGIN IMMEDIATE'); try { const v=fn(); this.db.exec('COMMIT'); return v; } catch(e) { try{this.db.exec('ROLLBACK')}catch{} throw e; } }
  registerDevice(id, token) { if(!validId(id) || typeof token!=='string' || token.length<32 || token.length>512) fail('invalid'); return this.tx(()=>{const digest=hash(Buffer.from(token));if(this.db.prepare('SELECT 1 FROM revoked_tokens WHERE token_hash=?').get(digest)) fail('conflict');const prior=this.db.prepare('SELECT revoked FROM devices WHERE id=?').get(id);if(prior && !prior.revoked)fail('conflict');if(prior)this.db.prepare('UPDATE devices SET token_hash=?,revoked=0 WHERE id=?').run(digest,id);else this.db.prepare('INSERT INTO devices(id,token_hash,revoked) VALUES(?,?,0)').run(id,digest);this.db.prepare('INSERT INTO device_credentials(token_hash,device_id) VALUES(?,?)').run(digest,id);}); }
  revocationEvents(id) { this.deviceStatus(id);return this.db.prepare('SELECT event_id AS eventId,credential_id AS credentialId,created_at AS createdAt,actor_id AS actorId FROM revocation_events WHERE device_id=? ORDER BY rowid DESC LIMIT 100').all(id); }
  deviceStatus(id) { const row=this.db.prepare('SELECT id,token_hash AS credentialId,revoked FROM devices WHERE id=?').get(id);if(!row)fail('not_found');return {...row,revoked:Boolean(row.revoked)}; }
  authenticate(token) { if(typeof token!=='string' || !token || token.length>512) return null; return this.db.prepare('SELECT id FROM devices WHERE token_hash=? AND revoked=0').get(hash(Buffer.from(token)))?.id ?? null; }
  revokeDevice(id, credentialId, eventId, actorId='local-admin') { if(!validId(id)||!validId(eventId)||!validId(actorId)||!/^[a-f0-9]{64}$/.test(credentialId??''))fail('invalid');return this.tx(()=>{
    const previous=this.db.prepare('SELECT device_id,credential_id FROM revocation_events WHERE event_id=?').get(eventId);
    if(previous){if(previous.device_id!==id||previous.credential_id!==credentialId)fail('conflict');return {eventId,revoked:true};}
    const row=this.db.prepare('SELECT token_hash FROM device_credentials WHERE token_hash=? AND device_id=?').get(credentialId,id);if(!row)fail('not_found');
    this.db.prepare('INSERT OR IGNORE INTO revoked_tokens(token_hash) VALUES(?)').run(row.token_hash);this.db.prepare('UPDATE devices SET revoked=1 WHERE id=? AND token_hash=?').run(id,row.token_hash);
    this.db.prepare("INSERT INTO revocation_events(event_id,device_id,credential_id,created_at,actor_id) VALUES(?,?,?,CAST(strftime('%s','now') AS INTEGER),?)").run(eventId,id,credentialId,actorId);return {eventId,revoked:true};
  }); }
  authorize(owner,token) { if(token!==undefined&&this.authenticate(token)!==owner)fail('not_found'); }
  owned(owner,id) { const m=this.db.prepare('SELECT * FROM meetings WHERE id=? AND owner=?').get(id,owner); if(!m || m.state==='deleted' || m.state==='deleting') fail('not_found'); return m; }
  create(owner,id,token) { if(!validId(owner)||!validId(id)) fail('invalid'); return this.tx(()=> { this.authorize(owner,token);const prior=this.db.prepare('SELECT * FROM meetings WHERE id=?').get(id); if(prior) { if(prior.owner!==owner || ['deleted','deleting'].includes(prior.state)) fail('not_found'); if(prior.state==='cancelled') fail('conflict'); return; } const pending=this.db.prepare("SELECT COUNT(*) AS n FROM meetings WHERE owner=? AND state='uploading'").get(owner).n;if(pending>=MAX_PENDING_MEETINGS)fail('too_many_pending');this.db.prepare("INSERT INTO meetings(id,owner,state,chunk_limit) VALUES(?,?,'uploading',?)").run(id,owner,MAX_NEW_CHUNKS); }); }
  put(owner,id,seq,data,digest,token) { if(!Number.isSafeInteger(seq)||seq<0||seq>100000||!Buffer.isBuffer(data)||data.length===0||data.length>262144||!/^[a-f0-9]{64}$/.test(digest)||hash(data)!==digest) fail('invalid'); return this.tx(()=>{this.authorize(owner,token);let m=this.owned(owner,id);const old=this.db.prepare('SELECT digest,data FROM chunks WHERE meeting_id=? AND seq=?').get(id,seq);if(old){if(old.digest!==digest)fail('conflict');if(hash(old.data)!==old.digest){if(this.storageLimitBytes!==null){const used=this.db.prepare('SELECT COALESCE(SUM(length(data)),0) AS n FROM chunks').get().n;if(used-old.data.length+data.length>this.storageLimitBytes)fail('storage_full');}this.db.prepare('UPDATE chunks SET data=? WHERE meeting_id=? AND seq=?').run(data,id,seq);
      if(m.chunks!==null && m.last_error==='integrity' && ((m.state==='failed'&&m.failed_stage==='queued')||m.state==='queued')){
        let intact=false;try{this.checkedAudio(id,m);intact=true}catch(e){if(!(e instanceof StoreError)||e.code!=='integrity')throw e;}
        if(intact)this.db.prepare("UPDATE meetings SET state='queued',attempts=0,last_error=NULL,failed_stage=NULL,retry_at=0 WHERE id=? AND last_error='integrity' AND (state='queued' OR (state='failed' AND failed_stage='queued'))").run(id);
      }
      return 'repaired';}return false;}if(m.state!=='uploading')fail('conflict');if(seq>=m.chunk_limit)fail('limit'); const size=this.db.prepare('SELECT COALESCE(SUM(length(data)),0) AS n FROM chunks WHERE meeting_id=?').get(id).n; if(size+data.length>20000000) fail('limit');if(this.storageLimitBytes!==null){const used=this.db.prepare('SELECT COALESCE(SUM(length(data)),0) AS n FROM chunks').get().n;if(used+data.length>this.storageLimitBytes)fail('storage_full');} this.db.prepare('INSERT INTO chunks VALUES(?,?,?,?)').run(id,seq,digest,data); return true;}); }
  repairStatus(owner,id,token) { return this.tx(()=>{
    this.authorize(owner,token);const m=this.owned(owner,id);if(m.state==='cancelled')fail('conflict');
    const rows=this.db.prepare('SELECT seq,digest,data FROM chunks WHERE meeting_id=? ORDER BY seq').all(id),damaged=[];
    for(const row of rows)if(hash(row.data)!==row.digest)damaged.push(row.seq);
    let overall='not_committed',missingCount=null;if(m.chunks!==null){missingCount=Math.max(0,m.chunks-rows.filter(row=>row.seq>=0&&row.seq<m.chunks).length);try{this.checkedAudio(id,m);overall='verified'}catch(e){if(!(e instanceof StoreError)||e.code!=='integrity')throw e;overall='failed'}}
    return {id:m.id,overall,expectedCount:m.chunks,missingCount,damagedSequences:damaged};
  }); }
  chunkWindow(owner,id,first,token) { if(!Number.isSafeInteger(first)||first<0||first>8191||first%16!==0)fail('invalid');return this.tx(()=>{this.authorize(owner,token);const m=this.owned(owner,id);if(m.state==='cancelled')fail('conflict');const chunks=this.db.prepare('SELECT seq,digest,data FROM chunks WHERE meeting_id=? AND seq>=? AND seq<? ORDER BY seq').all(id,first,first+16).map(row=>({seq:row.seq,digest:row.digest,size:row.data.length,valid:hash(row.data)===row.digest}));return {id:m.id,first,chunks};}); }
  list(owner,id,token,validateAudio) { return this.tx(()=>{this.authorize(owner,token);const m=this.owned(owner,id);if(m.chunks!==null){const audio=this.checkedAudio(id,m);if(validateAudio)validateAudio(audio);}const chunks=this.db.prepare('SELECT seq,digest,data FROM chunks WHERE meeting_id=? ORDER BY seq').all(id).map(row=>({seq:row.seq,digest:row.digest,size:row.data.length,valid:hash(row.data)===row.digest}));return {id:m.id,state:m.state,committed:m.chunks!==null,manifest:m.chunks!==null?receipt(m):null,chunks};}); }
  finalize(owner,id,manifest,token,validateAudio) { const {count,bytes,digest,reason,recordedAt=null,timeTrust='uncalibrated'}=manifest??{}; if(!['trusted','unverified','uncalibrated'].includes(timeTrust)||(recordedAt!==null&&(!Number.isSafeInteger(recordedAt)||recordedAt<946684800||recordedAt>4102444800))||(timeTrust==='trusted'&&recordedAt===null))fail('invalid');if(!Number.isSafeInteger(count)||count<1||count>100000||!Number.isSafeInteger(bytes)||bytes<1||bytes>20000000||!/^[a-f0-9]{64}$/.test(digest??'')||!['user','limit','storage','interrupted'].includes(reason)) fail('invalid'); return this.tx(()=>{this.authorize(owner,token);const m=this.owned(owner,id); if(m.state!=='uploading') { if(m.chunks===count && m.bytes===bytes && m.digest===digest && m.reason===reason && m.recorded_at===recordedAt && m.time_trust===timeTrust){const audio=this.checkedAudio(id,m);if(validateAudio)validateAudio(audio);return {id:m.id,state:m.state,committed:true,manifest:receipt(m)};} fail('conflict'); } const rows=this.db.prepare('SELECT seq,data,digest FROM chunks WHERE meeting_id=? ORDER BY seq').all(id); if(rows.length!==count || rows.some((x,i)=>x.seq!==i)) fail('missing_chunks'); const h=createHash('sha256'); let n=0; for(const row of rows){if(hash(row.data)!==row.digest)fail('integrity');h.update(row.data);n+=row.data.length;} if(n!==bytes||h.digest('hex')!==digest) fail('integrity');if(validateAudio)validateAudio(Buffer.concat(rows.map(row=>view(row.data))));this.db.prepare("UPDATE meetings SET state='queued',reason=?,chunks=?,bytes=?,digest=?,recorded_at=?,time_trust=? WHERE id=?").run(reason,count,bytes,digest,recordedAt,timeTrust,id); return {id:m.id,state:'queued',committed:true,manifest:{count,bytes,digest,reason,recordedAt,timeTrust}}; }); }
  cancel(owner,id,token,validateAudio) { if(!validId(owner)||!validId(id))fail('invalid');return this.tx(()=>{this.authorize(owner,token);const existing=this.db.prepare('SELECT * FROM meetings WHERE id=?').get(id);if(!existing){this.db.prepare("INSERT INTO meetings(id,owner,state,chunk_limit) VALUES(?,?,'cancelled',?)").run(id,owner,MAX_NEW_CHUNKS);return {state:'cancelled'};}const m=this.owned(owner,id); if(m.state==='cancelled'){if(this.db.prepare('SELECT 1 FROM chunks WHERE meeting_id=? LIMIT 1').get(id))fail('deletion_incomplete');return {state:'cancelled'};} if(m.state!=='uploading'){const audio=this.checkedAudio(id,m);if(validateAudio)validateAudio(audio);return {id:m.id,state:'committed',committed:true,manifest:receipt(m)};} this.db.prepare('DELETE FROM chunks WHERE meeting_id=?').run(id);if(this.db.prepare('SELECT 1 FROM chunks WHERE meeting_id=? LIMIT 1').get(id))fail('deletion_incomplete');const changed=this.db.prepare("UPDATE meetings SET state='cancelled' WHERE id=? AND state='uploading'").run(id);if(changed.changes!==1)fail('deletion_incomplete');return {state:'cancelled'};}); }
  audio(lease) { return this.tx(()=>{const m=this.db.prepare("SELECT chunks,bytes,digest FROM meetings WHERE id=? AND state=? AND generation=? AND lease_token=? AND lease_until>CAST(strftime('%s','now') AS INTEGER)").get(lease.id,lease.stage,lease.generation,lease.token);if(!m)fail('stale_lease');const audio=this.checkedAudio(lease.id,m);if(!this.db.prepare("SELECT 1 FROM meetings WHERE id=? AND state=? AND generation=? AND lease_token=? AND lease_until>CAST(strftime('%s','now') AS INTEGER)").get(lease.id,lease.stage,lease.generation,lease.token))fail('stale_lease');return audio;}); }
  renew(lease, ttlSeconds=60) { if(!Number.isInteger(ttlSeconds)||ttlSeconds<1) fail('invalid'); const r=this.db.prepare("UPDATE meetings SET lease_until=CAST(strftime('%s','now') AS INTEGER)+? WHERE id=? AND state=? AND generation=? AND lease_token=? AND lease_until>CAST(strftime('%s','now') AS INTEGER)").run(ttlSeconds,lease.id,lease.stage,lease.generation,lease.token); return r.changes===1; }
  claim(id,stage,ttlSeconds=60) { if(!['queued','minutes_queued'].includes(stage)||!Number.isInteger(ttlSeconds)||ttlSeconds<1) fail('invalid'); return this.tx(()=>{const m=this.db.prepare('SELECT * FROM meetings WHERE id=?').get(id); const running=stage==='queued'?'asr_running':'minutes_running'; const now=this.db.prepare("SELECT CAST(strftime('%s','now') AS INTEGER) AS t").get().t; if(!m || !(m.state===stage && m.retry_at<=now || (m.state===running && m.lease_until<=now))) return null; if(m.state===running)this.closeAttempt({id,generation:m.generation},'lease_expired');if(m.attempts>=3){this.db.prepare("UPDATE meetings SET state='failed',failed_stage=?,last_error='attempt_limit',lease_token=NULL,lease_until=NULL WHERE id=?").run(stage,id);return null} const token=randomBytes(24).toString('hex');const generation=m.generation+1; this.db.prepare('UPDATE meetings SET state=?,generation=?,lease_token=?,lease_until=?,attempts=attempts+1 WHERE id=?').run(running,generation,token,now+ttlSeconds,id);this.db.prepare('INSERT INTO processing_attempts(meeting_id,generation,stage,started_at) VALUES(?,?,?,?)').run(id,generation,running,now); return {id,stage:running,generation,token}; }); }
  batchGet(lease,version,key) { const m=this.job(lease);if(lease.stage!=='minutes_running'||JSON.parse(m.transcript).versionId!==version||!/^[a-f0-9]{64}$/.test(key))fail('stale_lease');return this.db.prepare('SELECT data FROM minutes_batches WHERE meeting_id=? AND transcript_version=? AND batch_key=?').get(lease.id,version,key)?.data??null; }
  batchPut(lease,version,key,data) { if(typeof data!=='string'||data.length>1000000)fail('invalid');return this.tx(()=>{const m=this.job(lease);if(lease.stage!=='minutes_running'||JSON.parse(m.transcript).versionId!==version||!/^[a-f0-9]{64}$/.test(key))fail('stale_lease');this.db.prepare('INSERT OR REPLACE INTO minutes_batches(meeting_id,transcript_version,batch_key,data) VALUES(?,?,?,?)').run(lease.id,version,key,data);}); }
  publish(lease,value) {
    const next=lease.stage==='asr_running'?'minutes_queued':lease.stage==='minutes_running'?'completed':null;
    if(!next||typeof value!=='string'||!value.trim()) fail('invalid');
    return this.tx(()=>{
      const m=this.db.prepare("SELECT transcript FROM meetings WHERE id=? AND state=? AND generation=? AND lease_token=? AND lease_until>CAST(strftime('%s','now') AS INTEGER)").get(lease.id,lease.stage,lease.generation,lease.token);
      if(!m)return false;
      let data;try{data=JSON.parse(value)}catch{fail('invalid')}
      if(lease.stage==='asr_running'){
        if(typeof data.versionId!=='string'||!data.versionId||!Array.isArray(data.segments)||!data.segments.length)fail('invalid');
        this.db.prepare('INSERT INTO transcript_versions(meeting_id,version_id,data) VALUES(?,?,?)').run(lease.id,data.versionId,value);
      }else{
        let transcript;try{transcript=JSON.parse(m.transcript)}catch{fail('invalid')}
        if(!transcript?.versionId||!this.db.prepare('SELECT 1 FROM transcript_versions WHERE meeting_id=? AND version_id=?').get(lease.id,transcript.versionId))fail('invalid');
        this.db.prepare('INSERT INTO minutes_versions(meeting_id,generation,transcript_version,data) VALUES(?,?,?,?)').run(lease.id,lease.generation,transcript.versionId,value);this.db.prepare('DELETE FROM minutes_batches WHERE meeting_id=?').run(lease.id);
      }
      const col=lease.stage==='asr_running'?'transcript':'minutes';
      const completed=lease.stage==='minutes_running'?"completed_at=CAST(strftime('%s','now') AS INTEGER),":'';
      const res=this.db.prepare(`UPDATE meetings SET ${col}=?,state=?,${completed}lease_token=NULL,lease_until=NULL,attempts=0,last_error=NULL,failed_stage=NULL,retry_at=0 WHERE id=? AND state=? AND generation=? AND lease_token=? AND lease_until>CAST(strftime('%s','now') AS INTEGER)`).run(value,next,lease.id,lease.stage,lease.generation,lease.token);
      if(res.changes!==1) fail('stale_lease');this.closeAttempt(lease,'succeeded');if(next==='completed')queueSubscriptionDeliveries(this.db,lease.id,lease.generation);return true;
    });
  }
  adminRetry(id) { return this.tx(()=>{const m=this.db.prepare("SELECT * FROM meetings WHERE id=? AND state='failed' AND failed_stage IN ('queued','minutes_queued')").get(id);if(!m)fail('conflict');if(m.last_error==='integrity'&&m.failed_stage==='queued')this.checkedAudio(id,m);this.db.prepare("UPDATE meetings SET state=failed_stage,failed_stage=NULL,attempts=0,last_error=NULL,retry_at=0 WHERE id=?").run(id);}); }
  adminRetranscribe(id) { return this.tx(()=>{const r=this.db.prepare("UPDATE meetings SET state='queued',transcript=NULL,minutes=NULL,completed_at=NULL,attempts=0,last_error=NULL,failed_stage=NULL,retry_at=0,generation=generation+1,lease_token=NULL,lease_until=NULL WHERE id=? AND state IN ('completed','failed') AND chunks IS NOT NULL").run(id);if(r.changes!==1)fail('conflict');this.db.prepare('UPDATE minutes_edits SET source_hash=NULL,source_version=NULL WHERE meeting_id=?').run(id);this.db.prepare('DELETE FROM minutes_batches WHERE meeting_id=?').run(id);}); }
  adminAttempts(id) { this.adminGet(id);return this.db.prepare('SELECT generation,stage,started_at,ended_at,outcome,error FROM processing_attempts WHERE meeting_id=? ORDER BY generation DESC LIMIT 100').all(id); }
  adminVersions(id) { this.adminGet(id);return {transcripts:this.db.prepare('SELECT version_id,data FROM transcript_versions WHERE meeting_id=? ORDER BY rowid').all(id),minutes:this.db.prepare('SELECT generation,transcript_version,data FROM minutes_versions WHERE meeting_id=? ORDER BY rowid').all(id)}; }
  checkedAudio(id,m) { if(m.chunks===null)fail('conflict');
    const rows=this.db.prepare('SELECT seq,digest,data FROM chunks WHERE meeting_id=? ORDER BY seq').all(id);
    if(rows.length!==m.chunks)fail('integrity');const h=createHash('sha256');let size=0;const out=[];
    for(let i=0;i<rows.length;i++){const row=rows[i],bytes=view(row.data);if(row.seq!==i||hash(bytes)!==row.digest)fail('integrity');h.update(bytes);size+=bytes.length;out.push(bytes);}
    if(size!==m.bytes||h.digest('hex')!==m.digest)fail('integrity');return Buffer.concat(out);
  }
  adminAudio(id) { return this.tx(()=>{const m=this.db.prepare("SELECT state,chunks,bytes,digest FROM meetings WHERE id=? AND state NOT IN ('deleted','deleting','cancelled')").get(id);if(!m)fail('not_found');return this.checkedAudio(id,m);}); }
  adminStatus() { const states=['uploading','queued','asr_running','minutes_queued','minutes_running','completed','failed','deleting'];const counts=Object.fromEntries(states.map(state=>[state,0]));for(const row of this.db.prepare("SELECT state,COUNT(*) AS n FROM meetings WHERE state NOT IN ('deleted','cancelled') GROUP BY state").all())if(Object.hasOwn(counts,row.state))counts[row.state]=row.n;const expiredLeases=this.db.prepare("SELECT COUNT(*) AS n FROM meetings WHERE state IN ('asr_running','minutes_running') AND lease_until<=CAST(strftime('%s','now') AS INTEGER)").get().n;return {counts,expiredLeases}; }
  adminPage({page=1,pageSize=20,search='',state='all',utcOffset=0}={}) {
    if(!Number.isSafeInteger(page)||page<1||![10,20,50,100].includes(pageSize)||typeof search!=='string'||search.length>200||!['all','processing','completed','failed'].includes(state)||!Number.isInteger(utcOffset)||utcOffset< -840||utcOffset>840)fail('invalid');
    search=search.trim();
    const title="CASE WHEN state='completed' AND json_valid(minutes) AND json_type(minutes,'$.meeting.topic')='text' THEN trim(replace(replace(json_extract(minutes,'$.meeting.topic'),char(10),' '),char(13),' ')) ELSE '' END";
    const time=`CASE WHEN recorded_at IS NULL OR time_trust='uncalibrated' THEN '录音时间未校准' ELSE printf('%d/%d/%d %s',strftime('%Y',recorded_at,'unixepoch','${utcOffset} minutes'),strftime('%m',recorded_at,'unixepoch','${utcOffset} minutes'),strftime('%d',recorded_at,'unixepoch','${utcOffset} minutes'),strftime('%H:%M:%S',recorded_at,'unixepoch','${utcOffset} minutes')) END`;
    const status="CASE state WHEN 'uploading' THEN '上传中' WHEN 'queued' THEN '等待转录' WHEN 'asr_running' THEN '转录中' WHEN 'minutes_queued' THEN '等待生成纪要' WHEN 'minutes_running' THEN '生成纪要中' WHEN 'completed' THEN '处理完成，需人工复核' WHEN 'failed' THEN '处理失败' WHEN 'cancelled' THEN '上传已取消' ELSE state END";
    const filter=`state NOT IN ('deleted','deleting') AND (?='all' OR (?='completed' AND state='completed') OR (?='failed' AND state='failed') OR (?='processing' AND state NOT IN ('completed','failed'))) AND (?='' OR instr(lower(id),lower(?))>0 OR instr(lower(${title}),lower(?))>0 OR instr(${time},?)>0 OR instr(${status},?)>0)`;
    const args=[state,state,state,state,search,search,search,search,search];
    return this.tx(()=>{const total=this.db.prepare(`SELECT count(*) AS n FROM meetings WHERE ${filter}`).get(...args).n,totalPages=Math.ceil(total/pageSize),actualPage=Math.min(page,Math.max(1,totalPages));const rows=this.db.prepare(`SELECT id,owner,state,reason,bytes,recorded_at,time_trust,completed_at,last_error,${title} AS title FROM meetings WHERE ${filter} ORDER BY rowid DESC LIMIT ? OFFSET ?`).all(...args,pageSize,(actualPage-1)*pageSize);return {meetings:rows.map(row=>({...row,reviewed:row.state==='completed'&&Boolean(this.db.prepare('SELECT 1 FROM meeting_reviews WHERE meeting_id=?').get(row.id))&&this.adminGet(row.id).reviewed,title:row.title&&row.title!=='待确认'?row.title.slice(0,120):null})),page:actualPage,pageSize,total,totalPages}});
  }
  adminList(before=null,limit=100) { if(before!==null&&(!Number.isSafeInteger(before)||before<1)||!Number.isSafeInteger(limit)||limit<1||limit>100)fail('invalid');
    const rows=this.db.prepare("SELECT rowid AS cursor,id,owner,state,reason,bytes,recorded_at,time_trust,completed_at,last_error FROM meetings WHERE state NOT IN ('deleted','deleting') AND (? IS NULL OR rowid<?) ORDER BY rowid DESC LIMIT ?").all(before,before,limit+1);
    const hasMore=rows.length>limit;const page=rows.slice(0,limit);return {meetings:page.map(({cursor,...item})=>({...item,reviewed:item.state==='completed'&&Boolean(this.db.prepare('SELECT 1 FROM meeting_reviews WHERE meeting_id=?').get(item.id))&&this.adminGet(item.id).reviewed})),nextCursor:hasMore?page[page.length-1].cursor:null};
  }
  adminPendingDeletions(before=null,limit=100) { if(before!==null&&(!Number.isSafeInteger(before)||before<1)||!Number.isSafeInteger(limit)||limit<1||limit>100)fail('invalid');
    const rows=this.db.prepare("SELECT rowid AS cursor,id,owner FROM meetings WHERE state='deleting' AND (? IS NULL OR rowid<?) ORDER BY rowid DESC LIMIT ?").all(before,before,limit+1);
    const page=rows.slice(0,limit);return {deletions:page.map(({cursor,...item})=>({...item,state:'deleting'})),nextCursor:rows.length>limit?page[page.length-1].cursor:null};
  }
  adminSaveMinutes(id,markdown,revision,sourceHash) { if(typeof markdown!=='string'||!markdown.trim()||Buffer.byteLength(markdown)>1000000||!Number.isSafeInteger(revision)||revision<0||!/^[a-f0-9]{64}$/.test(sourceHash??''))fail('invalid');return this.tx(()=>{const m=this.adminGet(id);if(m.state!=='completed'||!m.minutes||hash(Buffer.from(m.minutes))!==sourceHash)fail('conflict');const current=this.db.prepare('SELECT revision FROM minutes_edits WHERE meeting_id=?').get(id)?.revision??0;if(current!==revision)fail('conflict');const next=revision+1;let version=null;try{version=JSON.parse(m.transcript)?.versionId??null}catch{}this.db.prepare('INSERT INTO minutes_edits(meeting_id,markdown,revision,source_hash,source_version) VALUES(?,?,?,?,?) ON CONFLICT(meeting_id) DO UPDATE SET markdown=excluded.markdown,revision=excluded.revision,source_hash=excluded.source_hash,source_version=excluded.source_version').run(id,markdown,next,sourceHash,version);return {revision:next};}); }
  adminResult(id) { return this.tx(()=>{const m=this.adminGet(id);let audioIntegrity='not_committed';if(m.chunks!==null){try{this.checkedAudio(id,m);audioIntegrity='verified'}catch(e){if(!(e instanceof StoreError)||e.code!=='integrity')throw e;audioIntegrity='failed'}}return {...m,audioIntegrity};}); }
  adminGet(id) { const m=this.db.prepare("SELECT id,owner,state,reason,chunks,bytes,digest,recorded_at,time_trust,transcript,minutes,completed_at,attempts,retry_at,failed_stage,last_error FROM meetings WHERE id=? AND state NOT IN ('deleted','deleting')").get(id); if(!m) fail('not_found');const edit=this.db.prepare('SELECT markdown,revision,source_hash,source_version FROM minutes_edits WHERE meeting_id=?').get(id);const reviewKey=this.reviewSnapshot(m,edit);const reviewed=Boolean(reviewKey&&this.db.prepare('SELECT 1 FROM meeting_reviews WHERE meeting_id=? AND review_key=?').get(id,reviewKey));return {...m,reviewKey,reviewed,edited_minutes:edit?.markdown??null,minutes_revision:edit?.revision??0,minutes_edited_stale:Boolean(edit&&(!m.minutes||edit.source_hash!==hash(Buffer.from(m.minutes))))}; }
  adminBeginDelete(id) { const m=this.db.prepare("SELECT owner FROM meetings WHERE id=?").get(id);if(!m)fail('not_found');this.beginDelete(m.owner,id); }
  adminDelete(id) { this.adminBeginDelete(id);this.finishDelete(id); }
  beginDelete(owner,id) { return this.tx(()=>{const m=this.db.prepare('SELECT state FROM meetings WHERE id=? AND owner=?').get(id,owner);if(!m)fail('not_found');if(['deleting','deleted'].includes(m.state))return;this.db.prepare("UPDATE meetings SET state='deleting',generation=generation+1,lease_token=NULL,lease_until=NULL WHERE id=?").run(id);}); }
  finishDelete(id) { return this.tx(()=>{const m=this.db.prepare('SELECT state FROM meetings WHERE id=?').get(id);if(!m)fail('not_found');if(m.state==='deleted')return;if(m.state!=='deleting')fail('conflict');this.finishDeleteInTransaction(id);}); }
  delete(owner,id) { this.beginDelete(owner,id);this.finishDelete(id); }
}
