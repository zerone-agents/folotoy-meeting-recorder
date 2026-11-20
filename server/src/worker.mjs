import { randomUUID } from 'node:crypto';
import { toOgg,validateAudioStructure } from './audio.mjs';
import { segmentTranscript,segmentTimedTranscript,validateMinutes } from './minutes.mjs';
const failureCodes=new Set(['invalid_evidence','invalid_evidence_missing','invalid_evidence_format','invalid_evidence_version','invalid_evidence_segment','invalid_evidence_range','invalid_evidence_quote','invalid_evidence_ambiguous','invalid_minutes','provider_timeout','integrity','invalid_audio','unsupported_audio','invalid_transcript','asr_incomplete','asr_empty','asr_invalid_event','asr_event_too_large','asr_provider_error','minutes_incomplete','minutes_invalid_json','minutes_provider_failed','asr_http_400','asr_http_401','asr_http_403','asr_http_413','asr_http_429','asr_http_500','asr_http_502','asr_http_503','asr_http_504']);
/** One bounded claim. External calls are at-least-once; publication is fenced. */
export async function processOne(store,id,stage,{asr,minutes,leaseSeconds=90,activeWork,asrProgress,shutdownSignal,requestTimeoutMs=600000}={}) {
 if(!Number.isSafeInteger(requestTimeoutMs)||requestTimeoutMs<1)throw new Error('invalid_timeout');
 const lease=store.claim(id,stage,leaseSeconds); if(!lease)return false;
 const abort=new AbortController(); activeWork?.set(id,abort); let lost=false;const progress={deltaEvents:0};if(lease.stage==='asr_running')asrProgress?.set(id,progress);
 const timeout=setTimeout(()=>abort.abort(new Error('provider_timeout')),requestTimeoutMs);
 const call=async fn=>{const cancelled=new Promise((_,reject)=>{if(abort.signal.aborted)reject(abort.signal.reason);else abort.signal.addEventListener('abort',()=>reject(abort.signal.reason),{once:true})});return Promise.race([Promise.resolve().then(fn),cancelled])};
 const timer=setInterval(()=>{try{if(!store.renew(lease,leaseSeconds)){lost=true;abort.abort()}}catch{lost=true;abort.abort()}},Math.max(1000,leaseSeconds*300));
 try{
  const job=store.job(lease);
  if(lease.stage==='asr_running'){
   const savedAudio=store.audio(lease),durationSeconds=validateAudioStructure(savedAudio).durationSeconds,audio=toOgg(savedAudio);const text=await call(()=>asr(audio,{signal:abort.signal,durationSeconds,onDelta:()=>{if(!abort.signal.aborted&&!lost&&asrProgress?.get(id)===progress)progress.deltaEvents=Math.min(Number.MAX_SAFE_INTEGER,progress.deltaEvents+1)}}));
   const transcript=Array.isArray(text)?segmentTimedTranscript(text,randomUUID()):segmentTranscript(text,randomUUID());transcript.durationSeconds=durationSeconds;transcript.recordedAt=job.recorded_at;transcript.timeTrust=job.time_trust;
   if(!lost)store.publish(lease,JSON.stringify(transcript));
  }else{
   const transcript={...JSON.parse(job.transcript),recordedAt:job.recorded_at,timeTrust:job.time_trust};const raw=await call(()=>minutes(transcript,{signal:abort.signal,onFailure:record=>store.saveMinutesFailure(lease,transcript.versionId,record),checkpoint:{get:key=>store.batchGet(lease,transcript.versionId,key),put:(key,data)=>store.batchPut(lease,transcript.versionId,key,data)}}));
   const out=validateMinutes(raw,transcript,job.reason!=='user',job.reason);
   if(!lost)store.publish(lease,JSON.stringify(out));
  }
 }catch(e){if(!lost){if(shutdownSignal?.aborted)store.release(lease);else store.retry(lease, failureCodes.has(e?.message)?e.message:'processing_failed');}}
 finally{clearTimeout(timeout);clearInterval(timer);if(activeWork?.get(id)===abort)activeWork.delete(id);if(asrProgress?.get(id)===progress)asrProgress.delete(id)}return true;
}
export async function drainOnce(store,adapters){const limit=adapters.maxConcurrentWorkers??1;if(!Number.isSafeInteger(limit)||limit<1||limit>4)throw new Error('invalid_worker_concurrency');const jobs=store.ready();let next=0;const run=async()=>{while(!adapters.shutdownSignal?.aborted&&next<jobs.length){const job=jobs[next++];const stage=job.state==='asr_running'?'queued':job.state==='minutes_running'?'minutes_queued':job.state;await processOne(store,job.id,stage,adapters)}};const results=await Promise.allSettled(Array.from({length:Math.min(limit,jobs.length)},run));const failed=results.find(result=>result.status==='rejected');if(failed)throw failed.reason;}
