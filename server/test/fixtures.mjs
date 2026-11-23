import {toOgg} from '../src/audio.mjs';
export function sampleOgg(){const b=Buffer.alloc(100);b.write('SCR1');b.writeUInt32LE(1,20);b[96]=3;b.set([0xf8,0xff,0xfe],97);return toOgg(b)}

export function sampleChatCompletionStream(text,{finishReason='stop',outputTokens=1}={}) {
 const middle=Math.ceil(text.length/2);
 const events=[
  {choices:[{index:0,delta:{content:text.slice(0,middle)},finish_reason:null}]},
  {choices:[{index:0,delta:{content:text.slice(middle)},finish_reason:null}]},
 ];
 if(finishReason!==null)events.push(
  {choices:[{index:0,delta:{},finish_reason:finishReason}]},
  {choices:[],usage:{prompt_tokens:1,completion_tokens:outputTokens,total_tokens:1+outputTokens}},
 );
 const body=events.map(event=>`data: ${JSON.stringify(event)}\n\n`).join('')+(finishReason===null?'':'data: [DONE]\n\n');
 return new Response(body,{headers:{'content-type':'text/event-stream'}});
}
