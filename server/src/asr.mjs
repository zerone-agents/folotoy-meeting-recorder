import {splitTranscriptText} from './minutes.mjs';

const MAX_TIMED_DELTAS = 100_000;
const SEGMENT_GAP_MS = 2000;

/** Attach audio-relative delta offsets only to the exact completed text. */
function bindTimestamps(final,deltas,durationSeconds){
  if(deltas.map(item=>item.text).join('')!==final)return final;
  let deltaIndex=0,deltaOffset=0,position=0,groupStart=0,previousStart=0,previousEnd;
  const sections=[];
  const spans=deltas.map(({text,start,end})=>{
    const valid=Number.isSafeInteger(start)&&Number.isSafeInteger(end)&&start>=previousStart&&
      end>=start&&end>0&&(!Number.isFinite(durationSeconds)||end<=Math.ceil(durationSeconds*1000));
    if(valid){
      if(previousEnd!==undefined&&start-previousEnd>=SEGMENT_GAP_MS){
        sections.push(final.slice(groupStart,position));
        groupStart=position;
      }
      previousStart=start;
      previousEnd=Math.max(previousEnd??end,end);
    }
    position+=text.length;
    return {text,start,end,valid};
  });
  sections.push(final.slice(groupStart));
  const segments=sections.flatMap(section=>splitTranscriptText(section)).map(text=>{
    let offset=0,first,last,missing=false;
    while(offset<text.length){
      const span=spans[deltaIndex];
      const count=Math.min(span.text.length-deltaOffset,text.length-offset);
      // The provider only locates the whole delta on the recording timeline.
      if(span.valid&&count===span.text.length){
        first??=span.start;
        last=Math.max(last??span.end,span.end);
      }else if(span.valid||/[^\s\p{P}]/u.test(text.slice(offset,offset+count))){
        missing=true;
      }
      offset+=count;
      deltaOffset+=count;
      if(deltaOffset===span.text.length){
        deltaIndex++;
        deltaOffset=0;
      }
    }
    if(missing||first===undefined||last<=first)return {text};
    return {text,startSeconds:first/1000,endSeconds:last/1000,timeSource:'asr_sse'};
  });
  return segments.some(segment=>segment.timeSource==='asr_sse')?segments:final;
}

/** Consume UTF-8 SSE incrementally; a completed event is the only success. */
export async function transcribe(audio, { apiKey, baseURL = 'https://api.stepfun.com/step_plan/v1', model = 'stepaudio-2.5-asr', fetchImpl = fetch, signal, onDelta, withTimestamps = false, durationSeconds } = {}) {
  if (!apiKey || !Buffer.isBuffer(audio) || !audio.length) throw new Error('invalid_asr_input');
  const response = await fetchImpl(`${baseURL.replace(/\/$/,'')}/audio/asr/sse`, {
    method:'POST', headers:{Authorization:`Bearer ${apiKey}`,Accept:'text/event-stream','Content-Type':'application/json'}, signal,
    body:JSON.stringify({audio:{data:audio.toString('base64'),input:{transcription:{language:'zh',model,enable_itn:true,...(withTimestamps&&{enable_timestamp:true})},format:{type:'ogg'}}}}),
  });
  if (!response.ok || !response.body) throw new Error(`asr_http_${response.status}`);
  let deltas=withTimestamps?[]:null, deltaLength=0;
  const decoder=new TextDecoder('utf-8',{fatal:true});let pending='', eventLines=[], eventSize=0, final;
  const decode=(bytes,options)=>{try{return decoder.decode(bytes,options)}catch{throw new Error('asr_invalid_event')}};
  const dispatch=()=>{
    const raw=eventLines.filter(line=>line.startsWith('data:')).map(line=>line.slice(5).trimStart()).join('\n');
    eventLines=[];
    eventSize=0;
    if(!raw||raw==='[DONE]')return;
    let event;
    try{event=JSON.parse(raw)}catch{throw new Error('asr_invalid_event')}
    if(event.type==='error')throw new Error('asr_provider_error');
    if(event.type==='transcript.text.delta'){
      onDelta?.();
      if(deltas&&typeof event.delta==='string'&&event.delta.length){
        deltaLength+=event.delta.length;
        if(deltas.length>=MAX_TIMED_DELTAS||deltaLength>1048576)deltas=null;
        else deltas.push({text:event.delta,start:event.start_time,end:event.end_time});
      }
    }
    if(event.type==='transcript.text.done'){
      if(typeof event.text!=='string'||!event.text.trim())throw new Error('asr_empty');
      final=event.text;
    }
  };
  for await(const chunk of response.body){pending+=decode(chunk,{stream:true});let i;while((i=pending.indexOf('\n'))>=0){let line=pending.slice(0,i).replace(/\r$/,'');pending=pending.slice(i+1);if(!line)dispatch();else {eventSize+=line.length;if(eventSize>1048576)throw new Error('asr_event_too_large');eventLines.push(line);}}if(pending.length>1048576)throw new Error('asr_event_too_large');}
  // Flush the decoder to detect incomplete UTF-8, but an SSE event is complete
  // only after its blank-line delimiter. Never publish a truncated final event.
  pending+=decode(); if(pending.length>1048576)throw new Error('asr_event_too_large');
  if(final===undefined)throw new Error('asr_incomplete');
  return deltas ? bindTimestamps(final,deltas,durationSeconds) : final;
}
