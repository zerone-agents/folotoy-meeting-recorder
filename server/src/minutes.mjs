import { createHash } from 'node:crypto';
import { evidenceError } from './evidence.mjs';
export function splitTranscriptText(text,maxPoints=200) {
  const points=Array.from(text), parts=[];
  let start=0;
  for(let i=0;i<points.length;i++){
    if(i-start+1>=maxPoints||/[。！？!?；;]/u.test(points[i])){
      parts.push(points.slice(start,i+1).join(''));
      start=i+1;
    }
  }
  if(start<points.length)parts.push(points.slice(start).join(''));
  return parts;
}

/** Stable transcript chunks use Unicode code-point offsets. */
export function segmentTranscript(text,versionId,maxPoints=200) {
  if(typeof text!=='string'||!text.trim()||typeof versionId!=='string'||!versionId||!Number.isInteger(maxPoints)||maxPoints<1) throw new Error('invalid_transcript');
  const segments=splitTranscriptText(text,maxPoints).map((text,i)=>({id:`s${i}`,text}));
  return {versionId,sha256:createHash('sha256').update(text).digest('hex'),segments};
}
/** Provider times are audio-relative seconds; absent or invalid values stay absent. */
export function segmentTimedTranscript(items,versionId) {
 if(!Array.isArray(items)||typeof versionId!=='string'||!versionId)throw new Error('invalid_transcript');
 if(typeof items.fullText==='string'){const canonical=segmentTranscript(items.fullText,versionId);canonical.timedAlternatives=items.map(({text,startSeconds,endSeconds})=>({text,startSeconds,endSeconds,timeSource:'vad_estimate'}));canonical.alignmentWarning=true;return canonical}
 const fromSse=items.some(item=>item?.timeSource==='asr_sse');
 const segments=[];for(const item of items){if(typeof item?.text!=='string'||!(fromSse?item.text:item.text.trim()))continue;const segment={id:`s${segments.length}`,text:item.text};if(item.comparison==='approximate')segment.asrComparison='approximate';const words=item.words;
  if(Number.isFinite(item.startSeconds)&&Number.isFinite(item.endSeconds)&&item.startSeconds>=0&&item.endSeconds>=item.startSeconds){segment.startSeconds=item.startSeconds;segment.endSeconds=item.endSeconds;segment.timeSource=item.timeSource==='asr_sse'?'asr_sse':'vad_estimate'}
  else if(Array.isArray(words)&&words.length&&words.every(w=>Number.isFinite(w?.start)&&Number.isFinite(w?.end)&&w.start>=0&&w.end>=w.start)&&words.every((w,i)=>i===0||w.start>=words[i-1].start)){segment.startSeconds=words[0].start;segment.endSeconds=words[words.length-1].end}
  segments.push(segment);
 }
 if(!segments.length)throw new Error('invalid_transcript');const text=segments.map(s=>s.text).join('');return {versionId,sha256:createHash('sha256').update(text).digest('hex'),segments};
}
/** A provider must return structured JSON; invalid evidence fails closed. */
export function validateMinutes(raw, transcript, truncated, reason) {
  if(!raw || typeof raw!=='object' || !transcript || typeof transcript.versionId!=='string') throw new Error('invalid_minutes');
  if(typeof raw.summary!=='string'||!raw.summary.trim()||!Array.isArray(raw.discussion)||!Array.isArray(raw.decisions)||!Array.isArray(raw.actions)||!Array.isArray(raw.openQuestions)) throw new Error('invalid_minutes');
  const actions=raw.actions.map(action=>{if(!action||typeof action!=='object')return action;const result={...action,owner:typeof action.owner==='string'&&action.owner.trim()?action.owner:'待确认',priority:typeof action.priority==='string'&&action.priority.trim()?action.priority:'待确认'};if(result.due==null||(typeof result.due==='string'&&!result.due.trim()))delete result.due;return result});
  const versions=new Map([[transcript.versionId,new Map(transcript.segments.map(s=>[s.id,s.text]))]]);
  for(const entry of [...raw.discussion,...raw.decisions,...actions]) {
    if(!entry || typeof entry.text!=='string'||!entry.text.trim()) throw new Error('invalid_evidence_format');
    const error=evidenceError(versions,entry.evidence);if(error)throw new Error(error);
  }
  for(const action of actions) if(action.due!==undefined&&(typeof action.due!=='string'||!action.due.trim())) throw new Error('invalid_minutes');
  if(raw.openQuestions.some(q=>typeof q!=='string'||!q.trim())) throw new Error('invalid_minutes');
  const seconds=transcript.durationSeconds;const duration=typeof seconds==='number'&&Number.isFinite(seconds)&&seconds>0&&seconds<=1800?(seconds<1?'少于1秒（录音时长）':seconds<60?`约${Math.round(seconds)}秒（录音时长）`:`约${Math.round(seconds/6)/10}分钟（录音时长）`):'待确认';
  const metadata=raw.meeting&&typeof raw.meeting==='object'?raw.meeting:{};let date='待确认';
  if(Number.isSafeInteger(transcript.recordedAt)&&transcript.recordedAt>=946684800&&transcript.recordedAt<=4102444800&&['trusted','unverified'].includes(transcript.timeTrust)){const time=new Date(transcript.recordedAt*1000).toISOString().slice(0,16).replace('T',' ');date=`${time} UTC（${transcript.timeTrust==='trusted'?'设备标记可信':'设备时间未验证'}）`}
  else if(typeof metadata.date==='string'&&metadata.date.trim()&&metadata.date.trim()!=='待确认'){const value=metadata.date.trim();date=value.endsWith('（转写推断，待核对）')?value:value+'（转写推断，待核对）'}
  const meeting={...metadata,date,duration};
  return {...raw,meeting,actions,incomplete:Boolean(truncated),incompleteReason:truncated?reason:null,reviewRequired:true,reviewReasons:['automatic_transcript','semantic_support_unverified',...(truncated?['meeting_incomplete']:[])]};
}
