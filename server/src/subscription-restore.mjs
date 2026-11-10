const integer=value=>Number.isSafeInteger(value)&&value>=0;
const safeCode=value=>value===null||/^[a-z0-9_]{1,80}$/.test(value);
const invalid=()=>{throw new Error('invalid_subscription_state')};
const table=(db,name)=>db.prepare("SELECT 1 FROM sqlite_master WHERE type='table' AND name=?").get(name);

/** Read and fully validate the authoritative subscription state. */
export function readSubscriptionSnapshot(db){
 const marker=db.prepare("SELECT value FROM service_metadata WHERE key='subscription_schema'").get()?.value;
 if(!marker)return null;
 if(marker!=='1'||!table(db,'meeting_subscriptions')||!table(db,'subscription_deliveries')||!table(db,'wecom_rate_limits'))invalid();
 const subscriptions=db.prepare('SELECT * FROM meeting_subscriptions ORDER BY id').all();
 const deliveries=db.prepare('SELECT * FROM subscription_deliveries ORDER BY event_id').all();
 const rateLimits=db.prepare('SELECT webhook_hash,next_allowed_at FROM wecom_rate_limits ORDER BY webhook_hash').all();
 const ids=new Set();
 for(const row of subscriptions){
  if(typeof row.id!=='string'||!row.id||ids.has(row.id)||!['webhook','wecom'].includes(row.channel)||!['pending','approved','revoked'].includes(row.status)||typeof row.name!=='string'||typeof row.callback_url!=='string'||!integer(row.created_at)||!integer(row.share_days)||row.share_days<1||row.share_days>365||![0,1].includes(row.include_transcript)||![0,1].includes(row.allow_download))invalid();
  if(row.reviewed_at!==null&&!integer(row.reviewed_at))invalid();
  if(row.channel==='wecom'&&(row.status==='pending'||row.callback_token!==null))invalid();
  ids.add(row.id);
 }
 for(const row of deliveries){
  if(typeof row.event_id!=='string'||!row.event_id||!ids.has(row.subscription_id)||typeof row.meeting_id!=='string'||!integer(row.meeting_generation)||!integer(row.attempts)||!integer(row.next_attempt_at)||!integer(row.created_at)||!safeCode(row.last_error))invalid();
  if(row.delivered_at!==null&&!integer(row.delivered_at))invalid();
  if(row.failed_at!==null&&!integer(row.failed_at))invalid();
  if(row.share_expires_at!==null&&!integer(row.share_expires_at))invalid();
  if(row.delivered_at!==null&&row.failed_at!==null)invalid();
  if((row.delivered_at!==null||row.failed_at!==null)&&row.payload_json!==null)invalid();
  if(row.payload_json===null&&row.delivered_at===null&&row.failed_at===null&&row.attempts>0&&row.last_error===null)invalid();
  if(row.payload_json!==null){
   let saved;try{saved=JSON.parse(row.payload_json)}catch{invalid()}
   if(typeof saved?.body!=='string'||!saved.headers||typeof saved.headers!=='object'||Array.isArray(saved.headers)||!integer(row.share_expires_at))invalid();
  }
  const meeting=db.prepare('SELECT state FROM meetings WHERE id=?').get(row.meeting_id);if(!meeting)invalid();
  if(['deleted','deleting','cancelled'].includes(meeting.state)&&row.delivered_at===null&&row.failed_at===null)invalid();
 }
 for(const row of rateLimits)if(!/^[a-f0-9]{64}$/.test(row.webhook_hash)||!integer(row.next_allowed_at))invalid();
 return {subscriptions,deliveries,rateLimits};
}

/** Replace subscription state inside the caller's restore transaction. */
export function restoreSubscriptionSnapshot(target,snapshot){
 target.exec('DELETE FROM subscription_deliveries; DELETE FROM wecom_rate_limits; DELETE FROM meeting_subscriptions');
 if(snapshot===null)return;
 const subscription=target.prepare('INSERT INTO meeting_subscriptions(id,name,callback_url,callback_token,status,created_at,reviewed_at,share_days,include_transcript,allow_download,channel) VALUES(?,?,?,?,?,?,?,?,?,?,?)');
 for(const row of snapshot.subscriptions)subscription.run(row.id,row.name,row.callback_url,row.callback_token,row.status,row.created_at,row.reviewed_at,row.share_days,row.include_transcript,row.allow_download,row.channel);
 const delivery=target.prepare('INSERT INTO subscription_deliveries(event_id,subscription_id,meeting_id,meeting_generation,attempts,next_attempt_at,created_at,delivered_at,last_error,payload_json,share_expires_at,failed_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,?)');
 for(const row of snapshot.deliveries)delivery.run(row.event_id,row.subscription_id,row.meeting_id,row.meeting_generation,row.attempts,row.next_attempt_at,row.created_at,row.delivered_at,row.last_error,row.payload_json,row.share_expires_at,row.failed_at);
 const rate=target.prepare('INSERT INTO wecom_rate_limits(webhook_hash,next_allowed_at) VALUES(?,?)');
 for(const row of snapshot.rateLimits)rate.run(row.webhook_hash,row.next_allowed_at);
 if(target.prepare('PRAGMA foreign_key_check').all().length)invalid();
}
