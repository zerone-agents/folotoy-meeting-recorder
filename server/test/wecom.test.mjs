import {test} from 'node:test';
import assert from 'node:assert/strict';
import {validateWecomWebhook,cleanMeetingTitle,buildWecomCard,buildWecomTestCard,sendWecom} from '../src/wecom.mjs';

const key='a'.repeat(16);
const url=`https://qyapi.weixin.qq.com/cgi-bin/webhook/send?key=${key}`;

test('WeCom webhook accepts only the canonical endpoint and key',()=>{
  assert.equal(validateWecomWebhook(url),url);
  for(const value of [null,url.replace('https:','http:'),url.replace('qyapi.weixin.qq.com','evil.example'),url.replace('/cgi-bin/webhook/send','/other'),url+'&debug=1',url+'#x',url.replace('https://','https://user@'),url.replace('qyapi.weixin.qq.com','qyapi.weixin.qq.com:443'),url.replace(key,'short')])assert.throws(()=>validateWecomWebhook(value),/invalid/);
});

test('meeting title removes controls, falls back and truncates by code point',()=>{
  assert.equal(cleanMeetingTitle('  产品\u0000\u200e\n  周会  ','m'),'产品 周会');
  assert.equal(cleanMeetingTitle('\u200e','meeting-id'),'meeting-id');
  assert.equal(cleanMeetingTitle('😀'.repeat(31),'m'),'😀'.repeat(29)+'…');
});

test('formal card snapshot contains only the share link',()=>{
  const card=buildWecomCard({title:'产品周会',meetingId:'m',completedAt:1794301200,shareUrl:'https://meetings.example/meeting-service/share#token',shareExpiresAt:1794906000,timeZone:'Asia/Shanghai'});
  assert.deepEqual(card,{msgtype:'template_card',template_card:{card_type:'text_notice',main_title:{title:'新会议纪要已生成',desc:'产品周会'},sub_title_text:'完成时间：2026-11-10 17:00（UTC+8）\n分享有效期至：2026-11-17 17:00（UTC+8）\n点击查看会议纪要',card_action:{type:1,url:'https://meetings.example/meeting-service/share#token'}}});
  assert.equal(JSON.stringify(card).includes('download'),false);
});

test('test card contains no meeting or share credentials',()=>{
  const card=buildWecomTestCard({baseUrl:'https://meetings.example/meeting-service'});
  assert.equal(card.template_card.main_title.title,'企业微信机器人测试消息');
  assert.deepEqual(card.template_card.card_action,{type:1,url:'https://meetings.example/meeting-service'});
  assert.equal(JSON.stringify(card).includes('token'),false);
});

test('WeCom success and safe error classification',async()=>{
  assert.deepEqual(await sendWecom({callbackUrl:url,payload:{},fetchImpl:async()=>Response.json({errcode:0})}),{ok:true});
  assert.deepEqual(await sendWecom({callbackUrl:url,payload:{},fetchImpl:async()=>Response.json({errcode:45009})}),{ok:false,code:'wecom_45009',retryable:true});
  assert.deepEqual(await sendWecom({callbackUrl:url,payload:{},fetchImpl:async()=>Response.json({errcode:93000,errmsg:'secret webhook'})}),{ok:false,code:'wecom_93000',retryable:false});
  assert.deepEqual(await sendWecom({callbackUrl:url,payload:{},fetchImpl:async()=>new Response(null,{status:503})}),{ok:false,code:'http_503',retryable:true});
  assert.deepEqual(await sendWecom({callbackUrl:url,payload:{},fetchImpl:async()=>{throw new Error('raw network detail')}}),{ok:false,code:'network_error',retryable:true});
});
