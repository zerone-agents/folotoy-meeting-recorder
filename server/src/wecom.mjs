export function validateWecomWebhook(value){
  if(typeof value!=='string'||value.length>2048)throw new Error('invalid');
  let url;try{url=new URL(value)}catch{throw new Error('invalid')}
  const keys=[...url.searchParams.keys()];
  const authority=/^[A-Za-z][A-Za-z\d+.-]*:\/\/([^\/?#]*)/.exec(value.trim())?.[1]??'';
  if(url.protocol!=='https:'||url.hostname!=='qyapi.weixin.qq.com'||url.pathname!=='/cgi-bin/webhook/send'||authority.includes(':')||url.username||url.password||url.hash||url.port||keys.length!==1||keys[0]!=='key'||!/^[A-Za-z0-9_-]{16,128}$/.test(url.searchParams.get('key')??''))throw new Error('invalid');
  return url.toString();
}

export function cleanMeetingTitle(title,meetingId){
  const clean=value=>String(value).replace(/[\p{Cc}\p{Cf}]/gu,'').replace(/\s+/gu,' ').trim();
  const truncate=value=>{const chars=[...value];return chars.length>30?chars.slice(0,29).join('')+'…':value};
  return truncate(clean(title)||clean(meetingId));
}

function timestamp(seconds,timeZone){
  const parts=new Intl.DateTimeFormat('zh-CN',{timeZone,year:'numeric',month:'2-digit',day:'2-digit',hour:'2-digit',minute:'2-digit',hourCycle:'h23',timeZoneName:'longOffset'}).formatToParts(new Date(seconds*1000));
  const get=type=>parts.find(part=>part.type===type)?.value;
  const offset=(get('timeZoneName')??'UTC').replace('GMT','UTC').replace(/([+-])0(\d)(?=:)/,'$1$2').replace(/:00$/,'');
  return `${get('year')}-${get('month')}-${get('day')} ${get('hour')}:${get('minute')}（${offset}）`;
}

export function buildWecomCard({title,meetingId,completedAt,shareUrl,shareExpiresAt,timeZone=Intl.DateTimeFormat().resolvedOptions().timeZone}){
  return {msgtype:'template_card',template_card:{card_type:'text_notice',main_title:{title:'新会议纪要已生成',desc:cleanMeetingTitle(title,meetingId)},sub_title_text:`完成时间：${timestamp(completedAt,timeZone)}\n分享有效期至：${timestamp(shareExpiresAt,timeZone)}\n点击查看会议纪要`,card_action:{type:1,url:shareUrl}}};
}

export function buildWecomTestCard({baseUrl}){
  return {msgtype:'template_card',template_card:{card_type:'text_notice',main_title:{title:'企业微信机器人测试消息',desc:'配置验证成功'},sub_title_text:'此消息不包含会议纪要或分享链接。',card_action:{type:1,url:baseUrl}}};
}

export async function sendWecom({callbackUrl,payload,fetchImpl=fetch,timeoutMs=10000}){
  const controller=new AbortController(),timer=setTimeout(()=>controller.abort(),timeoutMs);
  try{
    const response=await fetchImpl(callbackUrl,{method:'POST',headers:{'content-type':'application/json'},body:JSON.stringify(payload),signal:controller.signal,redirect:'error'});
    if(response.status===408||response.status===429||response.status>=500)return {ok:false,code:`http_${response.status}`,retryable:true};
    if(response.status<200||response.status>=300)return {ok:false,code:`http_${response.status}`,retryable:false};
    let data;try{data=await response.json()}catch{return {ok:false,code:'network_error',retryable:true}}
    if(data?.errcode===0)return {ok:true};
    const code=Number.isSafeInteger(data?.errcode)?`wecom_${data.errcode}`:'network_error';
    return {ok:false,code,retryable:data?.errcode===45009||code==='network_error'};
  }catch(error){return {ok:false,code:error?.name==='AbortError'?'timeout':'network_error',retryable:true}}
  finally{clearTimeout(timer)}
}
