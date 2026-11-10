import { DatabaseSync } from 'node:sqlite';
import { openSync,closeSync,fsyncSync,unlinkSync } from 'node:fs';
import { dirname,resolve } from 'node:path';
import { createHash } from 'node:crypto';
import { MeetingStore } from './store.mjs';
import {rejectExistingSidecars} from './output-path.mjs';
import {validateTimeKeyState} from './time-key-store.mjs';

/** Create a new consistent SQLite snapshot, including committed WAL contents. */
export function backupDatabase({sourcePath,outputPath}){
 const source=resolve(sourcePath),output=resolve(outputPath);if(source===output)throw new Error('distinct_paths_required');
 const fd=openSync(output,'wx',0o600);closeSync(fd);let db,copy,success=false;
 try{
  rejectExistingSidecars(output);
  db=new DatabaseSync(source,{readOnly:true});const id=db.prepare("SELECT value FROM service_metadata WHERE key='instance_id'").get()?.value;if(!id)throw new Error('missing_database_identity');
  db.prepare('VACUUM INTO ?').run(output);db.close();db=undefined;
  // Complete any pending deletion in the isolated copy before exporting it.
  const cleanup=new MeetingStore(output);try{for(const row of cleanup.db.prepare("SELECT id FROM meetings WHERE state='deleting'").all())cleanup.finishDelete(row.id);}finally{cleanup.close();}
  const sanitized=new DatabaseSync(output);
  try{
   sanitized.exec('PRAGMA foreign_keys=ON; BEGIN IMMEDIATE');
   sanitized.exec('DELETE FROM subscription_deliveries; DELETE FROM wecom_rate_limits; DELETE FROM meeting_subscriptions; COMMIT');
  }finally{sanitized.close()}
  copy=new DatabaseSync(output,{readOnly:true});if(copy.prepare("SELECT value FROM service_metadata WHERE key='instance_id'").get()?.value!==id)throw new Error('backup_identity_mismatch');
  validateTimeKeyState(copy);
  for(const table of ['meeting_subscriptions','subscription_deliveries','wecom_rate_limits'])if(copy.prepare(`SELECT COUNT(*) AS n FROM ${table}`).get().n!==0)throw new Error('backup_subscription_secrets');
  const check=copy.prepare('PRAGMA quick_check').all();if(check.length!==1||Object.values(check[0])[0]!=='ok')throw new Error('backup_integrity_failed');if(copy.prepare('PRAGMA foreign_key_check').all().length)throw new Error('backup_foreign_key_failed');
  const retained=copy.prepare("SELECT 1 FROM meetings m WHERE m.state IN ('deleted','cancelled') AND (m.reason IS NOT NULL OR m.chunks IS NOT NULL OR m.bytes IS NOT NULL OR m.digest IS NOT NULL OR m.recorded_at IS NOT NULL OR m.time_trust!='uncalibrated' OR m.completed_at IS NOT NULL OR m.transcript IS NOT NULL OR m.minutes IS NOT NULL OR m.attempts!=0 OR m.last_error IS NOT NULL OR m.failed_stage IS NOT NULL OR m.retry_at!=0 OR m.lease_token IS NOT NULL OR m.lease_until IS NOT NULL OR EXISTS(SELECT 1 FROM chunks c WHERE c.meeting_id=m.id) OR EXISTS(SELECT 1 FROM transcript_versions t WHERE t.meeting_id=m.id) OR EXISTS(SELECT 1 FROM minutes_versions v WHERE v.meeting_id=m.id) OR EXISTS(SELECT 1 FROM minutes_batches b WHERE b.meeting_id=m.id) OR EXISTS(SELECT 1 FROM processing_attempts a WHERE a.meeting_id=m.id) OR EXISTS(SELECT 1 FROM minutes_edits e WHERE e.meeting_id=m.id)) LIMIT 1").get();if(retained)throw new Error('backup_deleted_content');
  for(const meeting of copy.prepare("SELECT id,chunks,bytes,digest FROM meetings WHERE chunks IS NOT NULL AND state NOT IN ('deleted','deleting','cancelled')").all()){
   const rows=copy.prepare('SELECT seq,digest,data FROM chunks WHERE meeting_id=? ORDER BY seq').all(meeting.id),whole=createHash('sha256');let bytes=0;
   if(rows.length!==meeting.chunks)throw new Error('backup_audio_integrity_failed');
   for(let i=0;i<rows.length;i++){const row=rows[i];if(row.seq!==i||createHash('sha256').update(row.data).digest('hex')!==row.digest)throw new Error('backup_audio_integrity_failed');whole.update(row.data);bytes+=row.data.length;}
   if(bytes!==meeting.bytes||whole.digest('hex')!==meeting.digest)throw new Error('backup_audio_integrity_failed');
  }
  copy.close();copy=undefined;
  const file=openSync(output,'r');try{fsyncSync(file)}finally{closeSync(file)}
  const parent=openSync(dirname(output),'r');try{fsyncSync(parent)}finally{closeSync(parent)}
  success=true;return {status:'ok'};
 }finally{try{copy?.close()}catch{}try{db?.close()}catch{}if(!success){try{unlinkSync(output)}catch{}}}
}
