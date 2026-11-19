/** Consume UTF-8 SSE incrementally; a completed event is the only success. */
export async function transcribe(audio, { apiKey, baseURL = 'https://api.stepfun.com/step_plan/v1', model = 'stepaudio-2.5-asr', fetchImpl = fetch, signal, onDelta } = {}) {
  if (!apiKey || !Buffer.isBuffer(audio) || !audio.length) throw new Error('invalid_asr_input');
  const response = await fetchImpl(`${baseURL.replace(/\/$/,'')}/audio/asr/sse`, {
    method:'POST', headers:{Authorization:`Bearer ${apiKey}`,Accept:'text/event-stream','Content-Type':'application/json'}, signal,
    body:JSON.stringify({audio:{data:audio.toString('base64'),input:{transcription:{language:'zh',model,enable_itn:true},format:{type:'ogg'}}}}),
  });
  if (!response.ok || !response.body) throw new Error(`asr_http_${response.status}`);
  const decoder=new TextDecoder('utf-8',{fatal:true});let pending='', eventLines=[], eventSize=0, final;
  const decode=(bytes,options)=>{try{return decoder.decode(bytes,options)}catch{throw new Error('asr_invalid_event')}};
  const dispatch=()=>{const raw=eventLines.filter(x=>x.startsWith('data:')).map(x=>x.slice(5).trimStart()).join('\n');eventLines=[];eventSize=0;if(!raw || raw==='[DONE]')return;let event;try{event=JSON.parse(raw);}catch{throw new Error('asr_invalid_event');} if(event.type==='error')throw new Error('asr_provider_error');if(event.type==='transcript.text.delta')onDelta?.();if(event.type==='transcript.text.done'){if(typeof event.text!=='string'||!event.text.trim())throw new Error('asr_empty');final=event.text;}};
  for await(const chunk of response.body){pending+=decode(chunk,{stream:true});let i;while((i=pending.indexOf('\n'))>=0){let line=pending.slice(0,i).replace(/\r$/,'');pending=pending.slice(i+1);if(!line)dispatch();else {eventSize+=line.length;if(eventSize>1048576)throw new Error('asr_event_too_large');eventLines.push(line);}}if(pending.length>1048576)throw new Error('asr_event_too_large');}
  // Flush the decoder to detect incomplete UTF-8, but an SSE event is complete
  // only after its blank-line delimiter. Never publish a truncated final event.
  pending+=decode(); if(pending.length>1048576)throw new Error('asr_event_too_large');
  if(final===undefined)throw new Error('asr_incomplete');return final;
}
