import { DatabaseSync } from 'node:sqlite';
import { openSync, closeSync, unlinkSync, fsyncSync } from 'node:fs';
import { resolve, dirname } from 'node:path';
import { createHash } from 'node:crypto';
import { MeetingStore } from './store.mjs';
import { acquireServiceLock } from './service-lock.mjs';
import {rejectExistingSidecars} from './output-path.mjs';
import {tombstoneMetadataClean} from './tombstone.mjs';
import {validateShareSchema} from './share-schema.mjs';
import {reconcileTimeKeyState} from './time-key-store.mjs';
import {readSubscriptionSnapshot,restoreSubscriptionSnapshot} from './subscription-restore.mjs';

/** Restore to a new file using a newer, authoritative safety ledger.
 * Stop every service/worker before calling and keep it stopped through cutover. */
export function restoreDatabase({backupPath, safetyPath, outputPath}) {
 const backup=resolve(backupPath), safety=resolve(safetyPath), output=resolve(outputPath);
 if(new Set([backup,safety,output]).size!==3) throw new Error('distinct_paths_required');
 // The output is reserved exclusively; never overwrite an existing database.
 let source, snapshot, target, lock, reserved=false, success=false;
 try {
  lock=acquireServiceLock(safety);
  const fd=openSync(output,'wx',0o600);closeSync(fd);reserved=true;rejectExistingSidecars(output);
  source=new DatabaseSync(safety,{readOnly:true});source.exec('BEGIN');
  const safetyCheck=source.prepare('PRAGMA quick_check').all();if(safetyCheck.length!==1||Object.values(safetyCheck[0])[0]!=='ok')throw new Error('safety_integrity_failed');
  const subscriptionSnapshot=readSubscriptionSnapshot(source);
  if(source.prepare('PRAGMA foreign_key_check').all().length)throw new Error('safety_foreign_key_failed');
  const instance=source.prepare("SELECT value FROM service_metadata WHERE key='instance_id'").get()?.value;
  if(!instance)throw new Error('missing_safety_identity');
  const shareTable=source.prepare("SELECT 1 FROM sqlite_master WHERE type='table' AND name='meeting_shares'").get();
  const shareMarker=source.prepare("SELECT value FROM service_metadata WHERE key='share_schema'").get();
  if(Boolean(shareTable)!==Boolean(shareMarker)||shareMarker&&!['1','2'].includes(shareMarker.value))throw new Error('invalid_share_schema');
  let shareRows=[];const shareStatus=shareTable?'verified':'unverified_invalidated';
  if(shareTable){
   validateShareSchema(source,Number(shareMarker.value));
   shareRows=source.prepare('SELECT token_hash,meeting_id,include_transcript,expires_at,'+(shareMarker.value==='2'?'allow_download':'0 AS allow_download')+' FROM meeting_shares').all();
   for(const row of shareRows)if(!/^[a-f0-9]{64}$/.test(row.token_hash)||typeof row.meeting_id!=='string'||![0,1].includes(row.include_transcript)||!Number.isSafeInteger(row.expires_at)||row.expires_at<0)throw new Error('invalid_share_state');
  }
  const tombstones=source.prepare("SELECT id,owner,state FROM meetings WHERE state IN ('deleting','deleted','cancelled')").all();
  const revocationEvents=source.prepare('SELECT event_id,device_id,credential_id,created_at,actor_id FROM revocation_events').all();
  const revoked=source.prepare('SELECT token_hash FROM revoked_tokens').all();
  const devices=source.prepare('SELECT id,token_hash,revoked FROM devices').all();
  const credentials=source.prepare('SELECT token_hash,device_id FROM device_credentials').all();
  const transcripts=source.prepare("SELECT v.meeting_id,v.version_id,v.data FROM transcript_versions v JOIN meetings m ON m.id=v.meeting_id WHERE m.state NOT IN ('deleting','deleted','cancelled')").all();
  const minutes=source.prepare("SELECT v.meeting_id,v.generation,v.transcript_version,v.data FROM minutes_versions v JOIN meetings m ON m.id=v.meeting_id WHERE m.state NOT IN ('deleting','deleted','cancelled')").all();
  const hasReviews=source.prepare("SELECT 1 FROM sqlite_master WHERE type='table' AND name='meeting_reviews'").get();
  const reviews=hasReviews?source.prepare('SELECT meeting_id,review_key FROM meeting_reviews').all():[];
  for(const review of reviews)if(typeof review.meeting_id!=='string'||!/^[a-f0-9]{64}$/.test(review.review_key))throw new Error('invalid_review_state');
  const hasEdits=source.prepare("SELECT 1 FROM sqlite_master WHERE type='table' AND name='minutes_edits'").get();
  const hasEditVersion=hasEdits&&source.prepare('PRAGMA table_info(minutes_edits)').all().some(c=>c.name==='source_version');
  const edits=hasEdits?source.prepare(`SELECT e.meeting_id,e.markdown,e.revision,e.source_hash,${hasEditVersion?'e.source_version':'NULL AS source_version'} FROM minutes_edits e JOIN meetings m ON m.id=e.meeting_id WHERE m.state NOT IN ('deleting','deleted','cancelled')`).all():[];
  const pending=source.prepare("SELECT id,owner,chunk_limit FROM meetings WHERE state='uploading'").all();
  const acknowledged=source.prepare("SELECT id,owner,chunks,bytes,digest,reason,recorded_at,time_trust,state,transcript,minutes,generation,attempts,last_error,failed_stage,retry_at,completed_at FROM meetings WHERE chunks IS NOT NULL AND state NOT IN ('deleting','deleted','cancelled')").all();
  const attempts=source.prepare("SELECT a.meeting_id,a.generation,a.stage,a.started_at,a.ended_at,a.outcome,a.error FROM processing_attempts a JOIN meetings m ON m.id=a.meeting_id WHERE m.state NOT IN ('deleting','deleted','cancelled')").all();
  const checkpoints=source.prepare("SELECT b.meeting_id,b.transcript_version,b.batch_key,b.data FROM minutes_batches b JOIN meetings m ON m.id=b.meeting_id WHERE m.state NOT IN ('deleting','deleted','cancelled')").all();
  snapshot=new DatabaseSync(backup,{readOnly:true});
  snapshot.prepare('VACUUM INTO ?').run(output);snapshot.close();snapshot=undefined;
  target=new DatabaseSync(output);
  if(target.prepare("SELECT value FROM service_metadata WHERE key='instance_id'").get()?.value!==instance)throw new Error('database_identity_mismatch');
  target.close();target=undefined;
  // Upgrade only the isolated output copy, after verifying its service identity.
  new MeetingStore(output).close();
  target=new DatabaseSync(output);
  target.exec('PRAGMA foreign_keys=ON; PRAGMA synchronous=FULL; BEGIN IMMEDIATE');
  try {
   reconcileTimeKeyState(source,target);
   target.exec('DELETE FROM meeting_shares');
   // Preserve the latest unconfirmed upload state and its per-meeting limit.
   // The device still owns its original audio until a final receipt is returned.
   for(const row of pending){
    const saved=target.prepare('SELECT owner,state FROM meetings WHERE id=?').get(row.id);
    if(saved&&(saved.owner!==row.owner||saved.state!=='uploading'))throw new Error('pending_upload_conflict');
    if(!Number.isSafeInteger(row.chunk_limit)||row.chunk_limit<1||row.chunk_limit>100000)throw new Error('invalid_pending_limit');
    if(!saved)target.prepare("INSERT INTO meetings(id,owner,state,chunk_limit) VALUES(?,?,'uploading',?)").run(row.id,row.owner,row.chunk_limit);
    else target.prepare('UPDATE meetings SET chunk_limit=? WHERE id=?').run(row.chunk_limit,row.id);
    target.prepare('DELETE FROM chunks WHERE meeting_id=?').run(row.id);
    const chunks=source.prepare('SELECT seq,digest,data FROM chunks WHERE meeting_id=? ORDER BY seq').all(row.id);let total=0;
    for(const chunk of chunks){if(!Number.isSafeInteger(chunk.seq)||chunk.seq<0||chunk.seq>100000||chunk.data.length<1||chunk.data.length>262144||!/^[a-f0-9]{64}$/.test(chunk.digest))throw new Error('invalid_pending_chunk');total+=chunk.data.length;if(total>20000000)throw new Error('invalid_pending_chunk');target.prepare('INSERT INTO chunks(meeting_id,seq,digest,data) VALUES(?,?,?,?)').run(row.id,chunk.seq,chunk.digest,chunk.data);}
   }
   for(const saved of target.prepare("SELECT id FROM meetings WHERE state='uploading'").all())if(!pending.some(row=>row.id===saved.id)&&!acknowledged.some(row=>row.id===saved.id)&&!tombstones.some(row=>row.id===saved.id))throw new Error('pending_upload_not_in_safety');
   // A missing latest record is not proof that an older meeting should reappear.
   const known=new Set([...pending,...acknowledged,...tombstones].map(row=>row.id));
   for(const saved of target.prepare("SELECT id FROM meetings WHERE state NOT IN ('deleted','deleting','cancelled')").all())if(!known.has(saved.id))throw new Error('meeting_not_in_safety');
   // A durable device acknowledgment cannot be rolled back to an earlier upload.
   for(const row of acknowledged){
    const saved=target.prepare('SELECT owner,chunks,bytes,digest,reason,recorded_at,time_trust,state FROM meetings WHERE id=?').get(row.id);
    if(!saved || ['deleted','deleting','cancelled'].includes(saved.state) || ['owner','chunks','bytes','digest','reason','recorded_at','time_trust'].some(key=>saved[key]!==row[key]))throw new Error('acknowledged_audio_missing');
    const chunks=target.prepare('SELECT seq,data,digest FROM chunks WHERE meeting_id=? ORDER BY seq').all(row.id);const total=createHash('sha256');let bytes=0;
    if(chunks.length!==row.chunks)throw new Error('acknowledged_audio_missing');
    for(let i=0;i<chunks.length;i++){const chunk=chunks[i];if(chunk.seq!==i || createHash('sha256').update(chunk.data).digest('hex')!==chunk.digest)throw new Error('acknowledged_audio_corrupt');total.update(chunk.data);bytes+=chunk.data.length;}
    if(bytes!==row.bytes || total.digest('hex')!==row.digest)throw new Error('acknowledged_audio_corrupt');
   }
   for(const row of transcripts){const saved=target.prepare('SELECT data FROM transcript_versions WHERE meeting_id=? AND version_id=?').get(row.meeting_id,row.version_id);if(!saved||saved.data!==row.data)throw new Error('transcript_history_missing');}
   for(const row of minutes){const saved=target.prepare('SELECT transcript_version,data FROM minutes_versions WHERE meeting_id=? AND generation=?').get(row.meeting_id,row.generation);if(!saved||saved.transcript_version!==row.transcript_version||saved.data!==row.data)throw new Error('minutes_history_missing');}
   // Apply the latest logical processing state; old backups must not resurrect
   // an obsolete current result after a later retry or retranscription.
   for(const row of acknowledged){
    const wasRunning=['asr_running','minutes_running'].includes(row.state);
    const state=row.state==='asr_running'?'queued':row.state==='minutes_running'?'minutes_queued':row.state;
    target.prepare('UPDATE meetings SET state=?,transcript=?,minutes=?,generation=MAX(generation,?)+1,attempts=?,last_error=?,failed_stage=?,retry_at=?,completed_at=?,lease_token=NULL,lease_until=NULL WHERE id=?').run(state,row.transcript,row.minutes,row.generation,row.attempts,row.last_error,row.failed_stage,wasRunning?0:row.retry_at,row.completed_at,row.id);
    target.prepare('DELETE FROM minutes_batches WHERE meeting_id=?').run(row.id);
    target.prepare('DELETE FROM processing_attempts WHERE meeting_id=?').run(row.id);
    for(const attempt of attempts.filter(a=>a.meeting_id===row.id)){const open=attempt.ended_at===null;target.prepare("INSERT INTO processing_attempts(meeting_id,generation,stage,started_at,ended_at,outcome,error) VALUES(?,?,?,?,CASE WHEN ? THEN CAST(strftime('%s','now') AS INTEGER) ELSE ? END,?,?)").run(row.id,attempt.generation,attempt.stage,attempt.started_at,open?1:0,attempt.ended_at,open?'recovered':attempt.outcome,attempt.error);}
    let version=null;if(row.transcript!==null){try{version=JSON.parse(row.transcript).versionId}catch{throw new Error('invalid_stored_transcript')}}
    for(const checkpoint of checkpoints.filter(c=>c.meeting_id===row.id)){if(!version||checkpoint.transcript_version!==version)throw new Error('checkpoint_version_mismatch');target.prepare('INSERT INTO minutes_batches(meeting_id,transcript_version,batch_key,data) VALUES(?,?,?,?)').run(row.id,version,checkpoint.batch_key,checkpoint.data);}
   }
   if(!hasEdits&&target.prepare('SELECT 1 FROM minutes_edits LIMIT 1').get())throw new Error('edited_minutes_safety_missing');
   target.exec('DELETE FROM minutes_edits');
   for(const edit of edits){if(!target.prepare("SELECT 1 FROM meetings WHERE id=? AND state NOT IN ('deleting','deleted','cancelled')").get(edit.meeting_id))throw new Error('edited_minutes_meeting_missing');target.prepare('INSERT INTO minutes_edits(meeting_id,markdown,revision,source_hash,source_version) VALUES(?,?,?,?,?)').run(edit.meeting_id,edit.markdown,edit.revision,edit.source_hash,edit.source_version);}
   for(const row of tombstones){
    const existing=target.prepare('SELECT owner FROM meetings WHERE id=?').get(row.id);
    if(existing && existing.owner!==row.owner)throw new Error('meeting_owner_mismatch');
    if(!existing)target.prepare('INSERT INTO meetings(id,owner,state) VALUES(?,?,?)').run(row.id,row.owner,row.state==='cancelled'?'cancelled':'deleted');
    target.prepare("UPDATE meetings SET state=?,generation=generation+1,lease_token=NULL,lease_until=NULL,reason=NULL,chunks=NULL,bytes=NULL,digest=NULL,recorded_at=NULL,time_trust='uncalibrated',completed_at=NULL,transcript=NULL,minutes=NULL,attempts=0,last_error=NULL,failed_stage=NULL,retry_at=0 WHERE id=?").run(row.state==='cancelled'?'cancelled':'deleted',row.id);
    target.prepare('DELETE FROM minutes_edits WHERE meeting_id=?').run(row.id);
    target.prepare('DELETE FROM chunks WHERE meeting_id=?').run(row.id);
    target.prepare('DELETE FROM transcript_versions WHERE meeting_id=?').run(row.id);
    target.prepare('DELETE FROM minutes_versions WHERE meeting_id=?').run(row.id);
    target.prepare('DELETE FROM minutes_batches WHERE meeting_id=?').run(row.id);
    target.prepare('DELETE FROM processing_attempts WHERE meeting_id=?').run(row.id);
    if(target.prepare('SELECT 1 FROM minutes_edits WHERE meeting_id=?').get(row.id))throw new Error('deletion_incomplete');
    const remains=target.prepare('SELECT 1 FROM chunks WHERE meeting_id=? UNION ALL SELECT 1 FROM transcript_versions WHERE meeting_id=? UNION ALL SELECT 1 FROM minutes_versions WHERE meeting_id=? UNION ALL SELECT 1 FROM minutes_batches WHERE meeting_id=? UNION ALL SELECT 1 FROM processing_attempts WHERE meeting_id=? LIMIT 1').get(row.id,row.id,row.id,row.id,row.id);
    if(remains||!tombstoneMetadataClean(target,row.id,row.state==='cancelled'?'cancelled':'deleted'))throw new Error('deletion_incomplete');
   }
   const now=Math.floor(Date.now()/1000);
   target.exec('DELETE FROM meeting_reviews');
   for(const review of reviews){const meeting=target.prepare("SELECT state FROM meetings WHERE id=?").get(review.meeting_id);if(!meeting)throw new Error('invalid_review_state');if(!['deleting','deleted','cancelled'].includes(meeting.state))target.prepare('INSERT INTO meeting_reviews(meeting_id,review_key) VALUES(?,?)').run(review.meeting_id,review.review_key);}
   for(const row of shareRows){
    const current=source.prepare("SELECT state FROM meetings WHERE id=?").get(row.meeting_id);
    const restored=target.prepare("SELECT state FROM meetings WHERE id=?").get(row.meeting_id);
    if(!current||!restored)throw new Error('invalid_share_state');
    if(row.expires_at>now&&!['deleting','deleted','cancelled'].includes(current.state)&&!['deleting','deleted','cancelled'].includes(restored.state))target.prepare('INSERT INTO meeting_shares(token_hash,meeting_id,include_transcript,expires_at,allow_download) VALUES(?,?,?,?,?)').run(row.token_hash,row.meeting_id,row.include_transcript,row.expires_at,row.allow_download);
   }
   for(const event of revocationEvents){const existing=target.prepare('SELECT device_id,credential_id FROM revocation_events WHERE event_id=?').get(event.event_id);if(existing&&(existing.device_id!==event.device_id||existing.credential_id!==event.credential_id))throw new Error('revocation_event_conflict');target.prepare('INSERT OR IGNORE INTO revocation_events(event_id,device_id,credential_id,created_at,actor_id) VALUES(?,?,?,?,?)').run(event.event_id,event.device_id,event.credential_id,event.created_at,event.actor_id);}
   for(const {token_hash} of revoked)target.prepare('INSERT OR IGNORE INTO revoked_tokens(token_hash) VALUES(?)').run(token_hash);
   // Preserve current identity and all credential ownership needed for delayed revocations.
   for(const credential of credentials){const prior=target.prepare('SELECT device_id FROM device_credentials WHERE token_hash=?').get(credential.token_hash);if(prior&&prior.device_id!==credential.device_id)throw new Error('credential_owner_mismatch');target.prepare('INSERT OR IGNORE INTO device_credentials(token_hash,device_id) VALUES(?,?)').run(credential.token_hash,credential.device_id);}
   for(const current of target.prepare('SELECT id FROM devices').all())if(!devices.some(device=>device.id===current.id))throw new Error('safety_device_missing');
   target.exec('DELETE FROM devices');
   for(const device of devices){if(!device.revoked&&target.prepare('SELECT 1 FROM revoked_tokens WHERE token_hash=?').get(device.token_hash))throw new Error('active_credential_revoked');target.prepare('INSERT INTO devices(id,token_hash,revoked) VALUES(?,?,?)').run(device.id,device.token_hash,device.revoked);}
   if(target.prepare('PRAGMA foreign_key_check').all().length)throw new Error('restore_foreign_key_failed');
   restoreSubscriptionSnapshot(target,subscriptionSnapshot);
   target.exec("UPDATE devices SET revoked=1 WHERE token_hash IN (SELECT token_hash FROM revoked_tokens); UPDATE meetings SET state=CASE state WHEN 'asr_running' THEN 'queued' ELSE 'minutes_queued' END, generation=generation+1, lease_token=NULL,lease_until=NULL,retry_at=0 WHERE state IN ('asr_running','minutes_running'); COMMIT");
  }catch(e){target.exec('ROLLBACK');throw e}
  const outputCheck=target.prepare('PRAGMA quick_check').all();if(outputCheck.length!==1||Object.values(outputCheck[0])[0]!=='ok')throw new Error('restore_integrity_failed');if(target.prepare('PRAGMA foreign_key_check').all().length)throw new Error('restore_foreign_key_failed');
  target.close();target=undefined;source.exec('COMMIT');source.close();source=undefined;
  const file=openSync(output,'r');try{fsyncSync(file)}finally{closeSync(file)}const parent=openSync(dirname(output),'r');try{fsyncSync(parent)}finally{closeSync(parent)}success=true;
  return {tombstones:tombstones.length,revocations:revoked.length,shareStatus};
 }finally{
  try{target?.close()}catch{}try{snapshot?.close()}catch{}try{source?.close()}catch{}
  try{lock?.close()}catch{}
  if(reserved&&!success){try{unlinkSync(output)}catch{}}
 }
}
