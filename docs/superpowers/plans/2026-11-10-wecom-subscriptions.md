# 企业微信机器人会议纪要推送 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在现有统一订阅体系中增加企业微信群机器人渠道，由管理员配置分享权限并可靠推送可点击的会议纪要卡片。

**Architecture:** `wecom.mjs` 独占企业微信地址、消息和响应协议；现有订阅协调器负责渠道无关的分享固化、投递状态与重试。投递载荷在首次发送前与分享在同一 SQLite 事务中固化，按 Webhook 哈希持久化限流；导出备份净化秘密，恢复只以最新安全库为权威。管理 UI 复用紧凑订阅列表，新增独立企业微信添加窗口。

**Tech Stack:** Node.js >=22.13.0、node:test、node:sqlite、Hono、原生 Fetch/Response、现有 HTML/CSS/JavaScript 管理界面；不新增第三方依赖。

## Global Constraints

- 设计权威为 `docs/superpowers/specs/2026-11-10-wecom-subscriptions-design.md`；不得实现扫码、OAuth、图文教程、“打开企业微信”、正文卡片或企业微信额外下载链接。
- 企业微信仅接受 `https://qyapi.weixin.qq.com/cgi-bin/webhook/send?key=...`，`key` 为 16–128 位 `[A-Za-z0-9_-]`；不得有用户信息、fragment 或其他查询参数。
- 普通 Webhook 匿名申请、管理员审批和回调 JSON 必须兼容；企业微信只能管理员创建并直接 `approved`。
- 任何读取接口、日志和安全错误均不得输出完整企业微信 Webhook、key、回调令牌或分享令牌。
- 首次尝试时分享创建和完整载荷固化必须处于同一事务；重试复用原链接与正文。送达或终态失败清除载荷。
- 每次实际发送前检查 `share_expires_at`；到期写入 `share_expired` 终态，不发送、不增加 attempts、不新建分享。
- 同一 Webhook 的正式发送、重试和测试至少间隔 3 秒；按 SHA-256(Webhook) 持久化时间槽。
- 企业微信仅在 HTTP 2xx 且 JSON `errcode:0` 时成功；原始错误正文不得入库。
- 导出备份中的 `meeting_subscriptions`、`subscription_deliveries`、`wecom_rate_limits` 必须为零行；安全库保留恢复所需状态。
- 真实企业微信消息只能由用户明确提供测试机器人并要求实测时发送；自动化一律使用 HTTP 测试替身。
- 每个任务只暂存列出的文件；不要提交根目录本地预览文件、`.scratch/`、日志或 PID 文件。

---

### Task 1: 企业微信纯函数与完整消息契约

**Files:**
- Create: `server/src/wecom.mjs`
- Test: `server/test/wecom.test.mjs`

**Interfaces:**
- Produces: `validateWecomWebhook(value: unknown): string`，成功返回规范化 URL，失败抛出 `Error('invalid')`。
- Produces: `cleanMeetingTitle(title: unknown, meetingId: string): string`。
- Produces: `buildWecomCard({title,meetingId,completedAt,shareUrl,shareExpiresAt,timeZone}): object`。
- Produces: `buildWecomTestCard({baseUrl}): object`。
- Produces: `sendWecom({callbackUrl,payload,fetchImpl=fetch,timeoutMs=10000}): Promise<{ok:true}|{ok:false,code:string,retryable:boolean}>`。

- [ ] **Step 1: Write the failing protocol tests**

Create `server/test/wecom.test.mjs` with these tests and keep the expected card as a literal snapshot:

```js
import {test} from 'node:test';
import assert from 'node:assert/strict';
import {validateWecomWebhook,cleanMeetingTitle,buildWecomCard,buildWecomTestCard,sendWecom} from '../src/wecom.mjs';

const key='a'.repeat(16);
const url=`https://qyapi.weixin.qq.com/cgi-bin/webhook/send?key=${key}`;

test('WeCom webhook accepts only the canonical endpoint and key',()=>{
  assert.equal(validateWecomWebhook(url),url);
  for(const value of [null,url.replace('https:','http:'),url.replace('qyapi.weixin.qq.com','evil.example'),url.replace('/cgi-bin/webhook/send','/other'),url+'&debug=1',url+'#x',url.replace('https://','https://user@'),url.replace(key,'short')])assert.throws(()=>validateWecomWebhook(value),/invalid/);
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
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd server && node --test test/wecom.test.mjs`
Expected: FAIL because `../src/wecom.mjs` does not exist.

- [ ] **Step 3: Implement the complete protocol boundary**

Create `server/src/wecom.mjs`:

```js
export function validateWecomWebhook(value){
  if(typeof value!=='string'||value.length>2048)throw new Error('invalid');
  let url;try{url=new URL(value)}catch{throw new Error('invalid')}
  const keys=[...url.searchParams.keys()];
  if(url.protocol!=='https:'||url.hostname!=='qyapi.weixin.qq.com'||url.pathname!=='/cgi-bin/webhook/send'||url.username||url.password||url.hash||url.port||keys.length!==1||keys[0]!=='key'||!/^[A-Za-z0-9_-]{16,128}$/.test(url.searchParams.get('key')??''))throw new Error('invalid');
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
  const offset=(get('timeZoneName')??'UTC').replace('GMT','UTC');
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
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd server && node --test test/wecom.test.mjs`
Expected: 5 tests PASS, 0 FAIL; snapshot timestamps must match the literal values above.

- [ ] **Step 5: Commit**

```bash
git add server/src/wecom.mjs server/test/wecom.test.mjs
git commit -m "feat: add WeCom message protocol"
```

---

### Task 2: 渠道数据库迁移、管理员创建与脱敏读取

**Files:**
- Modify: `server/src/subscriptions.mjs:20-97`
- Test: `server/test/subscriptions.test.mjs`
- Test: `server/test/wecom.test.mjs`

**Interfaces:**
- Consumes: `validateWecomWebhook()` from Task 1.
- Produces: `SubscriptionStore.createWecom({name,callbackUrl,days,includeTranscript,allowDownload,now}): {id,status:'approved',channel:'wecom',days,includeTranscript,allowDownload}`。
- Produces: `SubscriptionStore.wecomTarget(id): {id,callbackUrl}`，仅供测试接口内部使用。
- Produces: `initializeSubscriptions(db)` 创建 `channel`、`payload_json`、`share_expires_at`、`failed_at` 和 `wecom_rate_limits`，并维护 `subscription_schema=1`。
- `list()` 对企业微信返回 `callbackUrl:'https://qyapi.weixin.qq.com/cgi-bin/webhook/send'`，普通订阅原样返回。

- [ ] **Step 1: Write failing migration and redaction tests**

Append to `server/test/wecom.test.mjs`:

```js
import {DatabaseSync} from 'node:sqlite';
import {mkdtempSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {MeetingStore} from '../src/store.mjs';
import {SubscriptionStore,initializeSubscriptions} from '../src/subscriptions.mjs';

test('WeCom subscriptions are approved immediately and never list their key',()=>{
  const store=new MeetingStore(':memory:'),subscriptions=new SubscriptionStore(store.db);
  try{
    const made=subscriptions.createWecom({name:'产品群',callbackUrl:url,days:7,includeTranscript:false,allowDownload:false,now:100});
    assert.equal(made.status,'approved');assert.equal(made.channel,'wecom');
    const listed=subscriptions.list()[0];
    assert.equal(listed.channel,'wecom');assert.equal(listed.callbackUrl,'https://qyapi.weixin.qq.com/cgi-bin/webhook/send');
    assert.equal(JSON.stringify(listed).includes(key),false);
    assert.equal(store.db.prepare("SELECT value FROM service_metadata WHERE key='subscription_schema'").get().value,'1');
  }finally{store.close()}
});

test('new schema without channel fails closed instead of defaulting to webhook',()=>{
  const db=new DatabaseSync(':memory:');
  try{
    db.exec(`CREATE TABLE service_metadata(key TEXT PRIMARY KEY,value TEXT NOT NULL);
      CREATE TABLE meetings(id TEXT PRIMARY KEY);
      CREATE TABLE meeting_subscriptions(id TEXT PRIMARY KEY,name TEXT NOT NULL,callback_url TEXT NOT NULL,callback_token TEXT,status TEXT NOT NULL,created_at INTEGER NOT NULL,reviewed_at INTEGER,share_days INTEGER NOT NULL DEFAULT 7,include_transcript INTEGER NOT NULL DEFAULT 0,allow_download INTEGER NOT NULL DEFAULT 0);
      INSERT INTO service_metadata VALUES('subscription_schema','1');`);
    assert.throws(()=>initializeSubscriptions(db),/invalid_subscription_state/);
  }finally{db.close()}
});

test('explicit pre-channel legacy database migrates ordinary rows to webhook',()=>{
  const directory=mkdtempSync(join(tmpdir(),'wecom-migration-')),path=join(directory,'legacy.sqlite');
  const seeded=new MeetingStore(path),subscriptions=new SubscriptionStore(seeded.db);
  const applied=subscriptions.apply('旧服务','https://subscriber.example/hooks',null,1);subscriptions.review(applied.id,true,1);seeded.close();
  const legacy=new DatabaseSync(path);
  try{legacy.exec("ALTER TABLE meeting_subscriptions DROP COLUMN channel; DELETE FROM service_metadata WHERE key='subscription_schema';")}finally{legacy.close()}
  const store=new MeetingStore(path);
  try{
    assert.equal(store.db.prepare('SELECT channel FROM meeting_subscriptions WHERE id=?').get(applied.id).channel,'webhook');
    assert.equal(store.db.prepare("SELECT value FROM service_metadata WHERE key='subscription_schema'").get().value,'1');
  }finally{store.close();rmSync(directory,{recursive:true,force:true})}
});
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd server && node --test test/wecom.test.mjs test/subscriptions.test.mjs`
Expected: FAIL with `subscriptions.createWecom is not a function` and missing channel/marker assertions; the legacy test must fail before implementation rather than being skipped.

- [ ] **Step 3: Implement schema and store methods**

In `initializeSubscriptions(db)`, use this schema migration and append the new delivery columns only when absent:

```js
const table=db.prepare("SELECT 1 FROM sqlite_master WHERE type='table' AND name='meeting_subscriptions'").get();
const marker=db.prepare("SELECT value FROM service_metadata WHERE key='subscription_schema'").get()?.value;
const subscriptionColumns=()=>new Set(db.prepare('PRAGMA table_info(meeting_subscriptions)').all().map(column=>column.name));
if(table&&marker==='1'&&!subscriptionColumns().has('channel'))throw new Error('invalid_subscription_state');
if(marker!==undefined&&marker!=='1')throw new Error('invalid_subscription_state');
if(!subscriptionColumns().has('channel'))db.exec("ALTER TABLE meeting_subscriptions ADD COLUMN channel TEXT NOT NULL DEFAULT 'webhook' CHECK(channel IN ('webhook','wecom'))");
db.exec(`CREATE TABLE IF NOT EXISTS wecom_rate_limits(webhook_hash TEXT PRIMARY KEY,next_allowed_at INTEGER NOT NULL CHECK(next_allowed_at>=0));
INSERT INTO service_metadata(key,value) VALUES('subscription_schema','1') ON CONFLICT(key) DO UPDATE SET value='1';`);
for(const [name,definition] of [['payload_json','TEXT'],['share_expires_at','INTEGER'],['failed_at','INTEGER']]){
  const columns=new Set(db.prepare('PRAGMA table_info(subscription_deliveries)').all().map(column=>column.name));
  if(!columns.has(name))db.exec(`ALTER TABLE subscription_deliveries ADD COLUMN ${name} ${definition}`);
}
```

Add to `SubscriptionStore`, reusing the existing `shareConfig` validator:

```js
createWecom({name,callbackUrl,days=7,includeTranscript=false,allowDownload=false,now=Math.floor(Date.now()/1000)}){
  const url=validateWecomWebhook(callbackUrl),share=shareConfig({days,includeTranscript,allowDownload});
  if(typeof name!=='string'||!name.trim()||name.length>100||!Number.isSafeInteger(now)||now<0)throw new Error('invalid');
  const id=randomUUID();
  this.db.prepare("INSERT INTO meeting_subscriptions(id,name,callback_url,status,created_at,reviewed_at,share_days,include_transcript,allow_download,channel) VALUES(?,?,?,'approved',?,?,?,?,?,?,'wecom')").run(id,name.trim(),url,now,now,share.days,share.includeTranscript?1:0,share.allowDownload?1:0);
  return {id,status:'approved',channel:'wecom',...share};
}
wecomTarget(id){
  const row=this.db.prepare("SELECT id,callback_url AS callbackUrl FROM meeting_subscriptions WHERE id=? AND channel='wecom' AND status='approved'").get(id);
  if(!row)throw new Error('not_found');return row;
}
```

Change the list query to select `channel`, then map URLs without mutating stored values:

```js
.map(row=>({...row,callbackUrl:row.channel==='wecom'?'https://qyapi.weixin.qq.com/cgi-bin/webhook/send':row.callbackUrl,includeTranscript:Boolean(row.includeTranscript),allowDownload:Boolean(row.allowDownload)}));
```

Import `validateWecomWebhook` at the top. Extend `deliveries()` to return `failedAt` and keep `lastError` as the safe code; do not select `payload_json`.

- [ ] **Step 4: Run migration and ordinary regression tests**

Run: `cd server && node --test test/wecom.test.mjs test/subscriptions.test.mjs`
Expected: all PASS, including all pre-existing ordinary subscription tests.

- [ ] **Step 5: Commit**

```bash
git add server/src/subscriptions.mjs server/test/subscriptions.test.mjs server/test/wecom.test.mjs
git commit -m "feat: add WeCom subscription channel"
```

---

### Task 3: 管理 API、测试卡片与共享限流入口

**Files:**
- Modify: `server/src/subscriptions.mjs`
- Modify: `server/src/routes.mjs:21-61`
- Test: `server/test/wecom.test.mjs`

**Interfaces:**
- Produces: `reserveWecomSlot(db,callbackUrl,now): {ok:true,reservedAt:number}|{ok:false,retryAt:number}`；在 `BEGIN IMMEDIATE` 中原子预留，`next_allowed_at=max(now,previous)+3`。
- Produces: `testWecom({db,callbackUrl,baseUrl,fetchImpl,now}): Promise<{status:number,body:object}>`。
- Routes consume `createWecom`, `wecomTarget`, `testWecom` and existing `publicBaseUrl()`。
- `createMeetingRouter` adds optional `subscriptionFetchImpl=fetch` and `publicBaseUrl=null` options; production wiring passes them in Task 5。

- [ ] **Step 1: Write failing API tests**

Append router tests using an administrator header and a fetch spy:

```js
import {createMeetingRouter} from '../src/routes.mjs';

const adminHeaders={authorization:'Bearer admin123','content-type':'application/json'};
const post=(app,path,body,headers=adminHeaders)=>app.request(path,{method:'POST',headers,body:JSON.stringify(body)});

test('administrator creates WeCom and tests saved or unsaved bots without shares',async()=>{
  const store=new MeetingStore(':memory:');let calls=[];
  try{
    const app=createMeetingRouter(store,{adminToken:'admin123',subscriptionFetchImpl:async(_url,options)=>{calls.push(JSON.parse(options.body));return Response.json({errcode:0})},publicBaseUrl:'https://meetings.example/meeting-service'});
    assert.equal((await post(app,'/admin/subscriptions/wecom',{name:'群',callbackUrl:url})).status,201);
    assert.equal((await app.request('/admin/subscriptions/wecom',{method:'POST',headers:{'content-type':'application/json'},body:'{}'})).status,401);
    const listed=await(await app.request('/admin/subscriptions',{headers:{authorization:'Bearer admin123'}})).json();
    assert.equal((await post(app,'/admin/subscriptions/wecom/test',{callbackUrl:url})).status,200);
    store.db.prepare('DELETE FROM wecom_rate_limits').run();
    assert.equal((await post(app,`/admin/subscriptions/${listed.subscriptions[0].id}/test`,{})).status,200);
    assert.equal(store.db.prepare('SELECT COUNT(*) AS n FROM meeting_shares').get().n,0);
    assert.equal(store.db.prepare('SELECT COUNT(*) AS n FROM subscription_deliveries').get().n,0);
    assert.equal(calls.length,2);
    assert.equal(calls.every(card=>card.template_card.main_title.title==='企业微信机器人测试消息'),true);
  }finally{store.close()}
});

test('test endpoint reports validation, limits and safe upstream errors',async()=>{
  const store=new MeetingStore(':memory:');
  try{
    const app=createMeetingRouter(store,{adminToken:'admin123',subscriptionFetchImpl:async()=>Response.json({errcode:93000,errmsg:'do not expose'}),publicBaseUrl:'https://example.com'});
    assert.equal((await post(app,'/admin/subscriptions/wecom/test',{callbackUrl:'https://evil.example/?key='+key})).status,400);
    const failed=await post(app,'/admin/subscriptions/wecom/test',{callbackUrl:url});
    assert.equal(failed.status,502);assert.deepEqual(await failed.json(),{ok:false,error:'wecom_93000'});
    const limited=await post(app,'/admin/subscriptions/wecom/test',{callbackUrl:url});
    assert.equal(limited.status,429);assert.deepEqual(await limited.json(),{ok:false,error:'rate_limited'});
  }finally{store.close()}
});

test('rate slots persist across restart and the twenty-first send waits',()=>{
  const directory=mkdtempSync(join(tmpdir(),'wecom-rate-')),path=join(directory,'rate.sqlite');
  let store=new MeetingStore(path);
  try{
    assert.deepEqual(reserveWecomSlot(store.db,url,1000),{ok:true,reservedAt:1000});
    store.close();store=new MeetingStore(path);
    assert.deepEqual(reserveWecomSlot(store.db,url,1001),{ok:false,retryAt:1003});
    for(let now=1003;now<=1057;now+=3)assert.equal(reserveWecomSlot(store.db,url,now).ok,true);
    assert.deepEqual(reserveWecomSlot(store.db,url,1059),{ok:false,retryAt:1060});
    assert.deepEqual(reserveWecomSlot(store.db,url,1060),{ok:true,reservedAt:1060});
  }finally{store.close();rmSync(directory,{recursive:true,force:true})}
});
```

Import `reserveWecomSlot` from `../src/subscriptions.mjs` alongside the Task 2 imports.

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd server && node --test test/wecom.test.mjs`
Expected: FAIL with 404 for new routes and `reserveWecomSlot` missing.

- [ ] **Step 3: Implement the limiter and synchronous test sender**

In `server/src/subscriptions.mjs`, import `createHash`, `buildWecomTestCard`, and `sendWecom`, then add:

```js
export function reserveWecomSlot(db,callbackUrl,now){
  if(!Number.isSafeInteger(now)||now<0)throw new Error('invalid');
  const digest=createHash('sha256').update(callbackUrl).digest('hex');
  db.exec('BEGIN IMMEDIATE');
  try{
    const current=db.prepare('SELECT next_allowed_at FROM wecom_rate_limits WHERE webhook_hash=?').get(digest)?.next_allowed_at??0;
    if(now<current){db.exec('COMMIT');return {ok:false,retryAt:current}}
    db.prepare('INSERT INTO wecom_rate_limits(webhook_hash,next_allowed_at) VALUES(?,?) ON CONFLICT(webhook_hash) DO UPDATE SET next_allowed_at=excluded.next_allowed_at').run(digest,now+3);
    db.exec('COMMIT');return {ok:true,reservedAt:now};
  }catch(error){try{db.exec('ROLLBACK')}catch{}throw error}
}

export async function testWecom({db,callbackUrl,baseUrl,fetchImpl=fetch,now=Math.floor(Date.now()/1000)}){
  let normalized;try{normalized=validateWecomWebhook(callbackUrl)}catch{return {status:400,body:{ok:false,error:'invalid'}}}
  const slot=reserveWecomSlot(db,normalized,now);if(!slot.ok)return {status:429,body:{ok:false,error:'rate_limited'}};
  const result=await sendWecom({callbackUrl:normalized,payload:buildWecomTestCard({baseUrl:publicBaseUrl(baseUrl)}),fetchImpl});
  return result.ok?{status:200,body:{ok:true}}:{status:502,body:{ok:false,error:result.code}};
}
```

Register these routes immediately before `/admin/subscriptions/:id/approve`, so `/wecom/test` cannot be captured as an ID:

```js
app.post('/admin/subscriptions/wecom',async c=>{admin(c);const b=await json(c.req.raw);if(!b||Array.isArray(b)||Object.keys(b).some(k=>!['name','callbackUrl','days','includeTranscript','allowDownload'].includes(k)))throw new StoreError('invalid');try{return c.json(subscriptions.createWecom(b),201)}catch{throw new StoreError('invalid')}});
app.post('/admin/subscriptions/wecom/test',async c=>{admin(c);const b=await json(c.req.raw);if(!b||Object.keys(b).join(',')!=='callbackUrl')throw new StoreError('invalid');const result=await testWecom({db:store.db,callbackUrl:b.callbackUrl,baseUrl:publicBaseUrlOption,fetchImpl:subscriptionFetchImpl});return c.json(result.body,result.status)});
app.post('/admin/subscriptions/:id/test',async c=>{admin(c);const target=subscriptions.wecomTarget(c.req.param('id'));const result=await testWecom({db:store.db,callbackUrl:target.callbackUrl,baseUrl:publicBaseUrlOption,fetchImpl:subscriptionFetchImpl});return c.json(result.body,result.status)});
```

Destructure `subscriptionFetchImpl=fetch` and rename the new public URL option to `publicBaseUrl:publicBaseUrlOption=null` in `createMeetingRouter`; if it is null, test routes return `{ok:false,error:'invalid'}` with 400 before consuming a rate slot. Catch only `invalid`/`not_found` from `wecomTarget` and map to `StoreError`; never return raw errors.

- [ ] **Step 4: Run API and compatibility tests**

Run: `cd server && node --test test/wecom.test.mjs test/subscriptions.test.mjs`
Expected: all PASS; 21st reservation and restart tests demonstrate persisted throttling; no shares or formal events are created by tests.

- [ ] **Step 5: Commit**

```bash
git add server/src/subscriptions.mjs server/src/routes.mjs server/test/wecom.test.mjs
git commit -m "feat: add WeCom administration and test delivery"
```

---

### Task 4: 分享与载荷原子固化、重试一致性和过期终态

**Files:**
- Modify: `server/src/store.mjs:82-86`
- Modify: `server/src/subscriptions.mjs:99-147`
- Test: `server/test/subscriptions.test.mjs`
- Test: `server/test/wecom.test.mjs`

**Interfaces:**
- Produces: `MeetingStore.createShareInTransaction(id,includeTranscript,days,now,allowDownload): {token,expiresAt}`；不得自行 BEGIN/COMMIT。
- `MeetingStore.createShare(...)` becomes a transaction wrapper around `createShareInTransaction`。
- Produces internal `materializeDelivery(store,row,baseUrl,now)` returning `{callbackUrl,headers,body,shareExpiresAt,channel}` and persisting `payload_json`/`share_expires_at` atomically。
- `drainSubscriptions` accepts `now:()=>number` for deterministic expiry tests; default returns current Unix seconds。

- [ ] **Step 1: Write failing retry and expiry tests**

Append to `server/test/subscriptions.test.mjs`:

```js
test('ordinary retries reuse identical body and link after configuration changes',async()=>{
  const store=new MeetingStore(':memory:'),subscriptions=new SubscriptionStore(store.db);
  try{
    const {id}=subscriptions.apply('service','https://subscriber.example/hooks');subscriptions.review(id,true);completedMeeting(store);
    const bodies=[];let now=1000;
    const options={baseUrl:'https://example.com',now:()=>now,fetchImpl:async(_url,request)=>{bodies.push(request.body);return new Response(null,{status:bodies.length===1?503:200})}};
    await drainSubscriptions(store,options);subscriptions.updateShareConfig(id,{days:30});
    store.db.prepare('UPDATE subscription_deliveries SET next_attempt_at=0').run();await drainSubscriptions(store,options);
    assert.equal(bodies[0],bodies[1]);
    assert.equal(store.db.prepare('SELECT COUNT(*) AS n FROM meeting_shares').get().n,1);
    assert.equal(store.db.prepare('SELECT payload_json FROM subscription_deliveries').get().payload_json,null);
  }finally{store.close()}
});

test('expiry during retry becomes terminal without another upstream attempt',async()=>{
  const store=new MeetingStore(':memory:'),subscriptions=new SubscriptionStore(store.db);
  try{
    const {id}=subscriptions.apply('service','https://subscriber.example/hooks');subscriptions.review(id,true);completedMeeting(store);
    let now=1000,calls=0;
    const options={baseUrl:'https://example.com',now:()=>now,fetchImpl:async()=>{calls++;return new Response(null,{status:503})}};
    await drainSubscriptions(store,options);now+=8*86400;
    store.db.prepare('UPDATE subscription_deliveries SET next_attempt_at=0').run();await drainSubscriptions(store,options);
    const row=store.db.prepare('SELECT attempts,failed_at,last_error,payload_json FROM subscription_deliveries').get();
    assert.equal(calls,1);assert.equal(row.attempts,1);assert.equal(row.last_error,'share_expired');assert.ok(row.failed_at);assert.equal(row.payload_json,null);
  }finally{store.close()}
});
```

Append the following to `server/test/wecom.test.mjs`; import `drainSubscriptions` and `queueSubscriptionDeliveries` from `../src/subscriptions.mjs`:

```js
function wecomCompletedMeeting(store,id='m'){
  store.create('d',id);
  store.db.prepare("UPDATE meetings SET state='completed',minutes=?,completed_at=1794301200 WHERE id=?").run(JSON.stringify({meeting:{topic:'产品周会'},summary:'synthetic'}),id);
  queueSubscriptionDeliveries(store.db,id,1,1000);
}

test('WeCom retry freezes card body and configuration',async()=>{
  const store=new MeetingStore(':memory:'),subscriptions=new SubscriptionStore(store.db);
  try{
    const bot=subscriptions.createWecom({name:'群',callbackUrl:url,now:1});wecomCompletedMeeting(store);
    const bodies=[];let calls=0;
    const options={baseUrl:'https://example.com',now:()=>1000+calls*3,fetchImpl:async(_url,request)=>{bodies.push(request.body);calls++;return Response.json({errcode:calls===1?45009:0})}};
    await drainSubscriptions(store,options);subscriptions.updateShareConfig(bot.id,{days:30});
    store.db.prepare('UPDATE subscription_deliveries SET next_attempt_at=0').run();await drainSubscriptions(store,options);
    assert.equal(bodies[0],bodies[1]);assert.equal(store.db.prepare('SELECT COUNT(*) AS n FROM meeting_shares').get().n,1);
  }finally{store.close()}
});

test('share and payload persistence roll back atomically',async()=>{
  for(const trigger of [
    "CREATE TRIGGER fail_share BEFORE INSERT ON meeting_shares BEGIN SELECT RAISE(ABORT,'blocked'); END",
    "CREATE TRIGGER fail_payload BEFORE UPDATE OF payload_json ON subscription_deliveries WHEN NEW.payload_json IS NOT NULL BEGIN SELECT RAISE(ABORT,'blocked'); END"
  ]){
    const store=new MeetingStore(':memory:'),subscriptions=new SubscriptionStore(store.db);
    try{
      subscriptions.createWecom({name:'群',callbackUrl:url,now:1});wecomCompletedMeeting(store);store.db.exec(trigger);
      await assert.rejects(()=>drainSubscriptions(store,{baseUrl:'https://example.com',now:()=>1000,fetchImpl:async()=>Response.json({errcode:0})}));
      assert.equal(store.db.prepare('SELECT COUNT(*) AS n FROM meeting_shares').get().n,0);
      assert.equal(store.db.prepare('SELECT payload_json FROM subscription_deliveries').get().payload_json,null);
    }finally{store.close()}
  }
});
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd server && node --test test/subscriptions.test.mjs test/wecom.test.mjs`
Expected: FAIL because bodies differ across retries, `now` is not honored, and `share_expired` is absent.

- [ ] **Step 3: Implement atomic materialization**

Refactor `createShare` without changing its public signature:

```js
createShareInTransaction(id,includeTranscript,days,now=Math.floor(Date.now()/1000),allowDownload=false){
  if(!validId(id)||typeof includeTranscript!=='boolean'||typeof allowDownload!=='boolean'||!Number.isSafeInteger(days)||days<1||days>365||!Number.isSafeInteger(now)||now<0)fail('invalid');
  const token=randomBytes(32).toString('base64url'),expiresAt=now+days*86400;
  const m=this.db.prepare("SELECT minutes FROM meetings WHERE id=? AND state='completed'").get(id);if(!m||!m.minutes)fail('conflict');
  this.db.prepare('INSERT INTO meeting_shares(token_hash,meeting_id,include_transcript,expires_at,allow_download) VALUES(?,?,?,?,?)').run(hash(token),id,includeTranscript?1:0,expiresAt,allowDownload?1:0);
  return {token,expiresAt};
}
createShare(id,includeTranscript,days,now=Math.floor(Date.now()/1000),allowDownload=false){return this.tx(()=>this.createShareInTransaction(id,includeTranscript,days,now,allowDownload))}
```

In `subscriptions.mjs`, implement materialization inside `store.tx()` and never call the transaction-opening `createShare()` from it:

```js
function materializeDelivery(store,row,baseUrl,now){
  return store.tx(()=>{
    const existing=store.db.prepare('SELECT payload_json,share_expires_at FROM subscription_deliveries WHERE event_id=?').get(row.event_id);
    const subscription=store.db.prepare("SELECT * FROM meeting_subscriptions WHERE id=? AND status='approved'").get(row.subscription_id);
    if(!subscription)return null;
    if(existing?.payload_json){const saved=JSON.parse(existing.payload_json);return {channel:subscription.channel,callbackUrl:subscription.callback_url,headers:saved.headers,body:saved.body,shareExpiresAt:existing.share_expires_at}}
    const share=store.createShareInTransaction(row.meeting_id,Boolean(subscription.include_transcript),subscription.share_days,now,Boolean(subscription.allow_download));
    const shareUrl=`${baseUrl}/share#${share.token}`;
    let payload,headers={'content-type':'application/json'};
    if(subscription.channel==='wecom'){
      const meeting=store.db.prepare("SELECT completed_at,CASE WHEN json_valid(minutes) AND json_type(minutes,'$.meeting.topic')='text' THEN json_extract(minutes,'$.meeting.topic') ELSE NULL END AS title FROM meetings WHERE id=?").get(row.meeting_id);
      payload=buildWecomCard({title:meeting.title,meetingId:row.meeting_id,completedAt:meeting.completed_at??now,shareUrl,shareExpiresAt:share.expiresAt});
    }else{
      payload={event:'meeting.minutes.completed',eventId:row.event_id,meetingId:row.meeting_id,shareUrl,shareExpiresAt:share.expiresAt};
      if(subscription.allow_download)payload.downloadUrls={minutes:`${baseUrl}/share/download/minutes`,transcript:`${baseUrl}/share/download/transcript`};
      if(subscription.callback_token)headers.authorization=`Bearer ${subscription.callback_token}`;
    }
    const frozen={headers,body:JSON.stringify(payload)};
    store.db.prepare('UPDATE subscription_deliveries SET payload_json=?,share_expires_at=? WHERE event_id=?').run(JSON.stringify(frozen),share.expiresAt,row.event_id);
    return {channel:subscription.channel,callbackUrl:subscription.callback_url,...frozen,shareExpiresAt:share.expiresAt};
  });
}
```

Before every network call, if `now() >= shareExpiresAt`, execute `UPDATE subscription_deliveries SET failed_at=?,last_error='share_expired',payload_json=NULL WHERE event_id=?`; return without incrementing attempts. On successful delivery clear `payload_json`, set `delivered_at` from injected `now()`, and leave terminal rows excluded by `WHERE failed_at IS NULL`.

- [ ] **Step 4: Run retry, expiry and ordinary regression tests**

Run: `cd server && node --test test/subscriptions.test.mjs test/wecom.test.mjs`
Expected: all PASS; exact body equality and exactly one share prove frozen retry semantics; expiry makes no second request.

- [ ] **Step 5: Commit**

```bash
git add server/src/store.mjs server/src/subscriptions.mjs server/test/subscriptions.test.mjs server/test/wecom.test.mjs
git commit -m "fix: freeze subscription delivery payloads"
```

---

### Task 5: 正式投递限流、企业微信错误分类与生产接线

**Files:**
- Modify: `server/src/subscriptions.mjs`
- Modify: `server/src/index.mjs:44-54`
- Test: `server/test/wecom.test.mjs`
- Test: `server/test/subscriptions.test.mjs`

**Interfaces:**
- Consumes: Task 1 `sendWecom`, Task 3 `reserveWecomSlot`, Task 4 materialized delivery。
- Produces delivery states: pending (`delivered_at` and `failed_at` null), delivered (`delivered_at` set), terminal (`failed_at` set and safe `last_error`)。
- `createMeetingRouter` production receives `publicBaseUrl` and the same outbound-capable fetch policy used by the worker; ordinary webhook behavior remains unchanged。

- [ ] **Step 1: Write failing terminal and throttling tests**

Append table-driven tests:

```js
test('WeCom terminal errors stop retries and clear secrets',async()=>{
  const terminal=[93000,93001,93004,93006,93008,93019,93017,99999];
  for(const errcode of terminal){
    const store=new MeetingStore(':memory:'),subscriptions=new SubscriptionStore(store.db);
    try{
      subscriptions.createWecom({name:'群',callbackUrl:url,now:1});wecomCompletedMeeting(store);
      let calls=0;
      await drainSubscriptions(store,{baseUrl:'https://example.com',now:()=>1000,fetchImpl:async()=>{calls++;return Response.json({errcode,errmsg:'raw secret'})}});
      store.db.prepare('UPDATE subscription_deliveries SET next_attempt_at=0').run();
      await drainSubscriptions(store,{baseUrl:'https://example.com',now:()=>1004,fetchImpl:async()=>{calls++;return Response.json({errcode:0})}});
      const row=store.db.prepare('SELECT attempts,failed_at,last_error,payload_json FROM subscription_deliveries').get();
      assert.equal(calls,1);assert.equal(row.attempts,1);assert.equal(row.last_error,`wecom_${errcode}`);assert.ok(row.failed_at);assert.equal(row.payload_json,null);
    }finally{store.close()}
  }
});

test('WeCom retryable errors retain frozen payload and exponential backoff',async()=>{
  const cases=[
    [async()=>Response.json({errcode:45009}),'wecom_45009'],
    [async()=>{throw new Error('raw detail')},'network_error'],
    [async()=>{throw new DOMException('aborted','AbortError')},'timeout'],
    ...[408,429,500,503].map(status=>[async()=>new Response(null,{status}),`http_${status}`])
  ];
  for(const [fetchImpl,code] of cases){
    const store=new MeetingStore(':memory:'),subscriptions=new SubscriptionStore(store.db);
    try{
      subscriptions.createWecom({name:'群',callbackUrl:url,now:1});wecomCompletedMeeting(store);
      await drainSubscriptions(store,{baseUrl:'https://example.com',now:()=>1000,fetchImpl});
      const row=store.db.prepare('SELECT attempts,next_attempt_at,failed_at,last_error,payload_json FROM subscription_deliveries').get();
      assert.equal(row.attempts,1);assert.equal(row.next_attempt_at,1005);assert.equal(row.failed_at,null);assert.equal(row.last_error,code);assert.equal(typeof row.payload_json,'string');
    }finally{store.close()}
  }
});

test('formal delivery and test delivery consume the same rate slot',async()=>{
  const store=new MeetingStore(':memory:'),subscriptions=new SubscriptionStore(store.db);
  try{
    subscriptions.createWecom({name:'群',callbackUrl:url,now:1});wecomCompletedMeeting(store);
    await drainSubscriptions(store,{baseUrl:'https://example.com',now:()=>1000,fetchImpl:async()=>Response.json({errcode:0})});
    const limited=await testWecom({db:store.db,callbackUrl:url,baseUrl:'https://example.com',now:1001,fetchImpl:async()=>Response.json({errcode:0})});
    assert.equal(limited.status,429);
    subscriptions.createWecom({name:'同机器人重复订阅',callbackUrl:url,now:1});wecomCompletedMeeting(store,'second');
    const tested=await testWecom({db:store.db,callbackUrl:url,baseUrl:'https://example.com',now:1004,fetchImpl:async()=>Response.json({errcode:0})});
    assert.equal(tested.status,200);
    await drainSubscriptions(store,{baseUrl:'https://example.com',now:()=>1005,fetchImpl:async()=>Response.json({errcode:0})});
    assert.equal(store.db.prepare("SELECT attempts FROM subscription_deliveries WHERE meeting_id='second'").get().attempts,0);
  }finally{store.close()}
});
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd server && node --test test/wecom.test.mjs test/subscriptions.test.mjs`
Expected: FAIL because business errors are treated as HTTP success and terminal filtering/rate integration is missing.

- [ ] **Step 3: Implement channel send and terminal transitions**

Use a channel-specific sender after materialization and before reservation expiry check:

```js
async function sendMaterialized(materialized,fetchImpl,timeoutMs){
  if(materialized.channel==='wecom')return sendWecom({callbackUrl:materialized.callbackUrl,payload:JSON.parse(materialized.body),fetchImpl,timeoutMs});
  const response=await fetchImpl(materialized.callbackUrl,{method:'POST',headers:materialized.headers,body:materialized.body,signal:AbortSignal.timeout(timeoutMs),redirect:'error'});
  return response.ok?{ok:true}:{ok:false,code:`http_${response.status}`,retryable:response.status===408||response.status===429||response.status>=500};
}
```

In `deliver()`, for WeCom call `reserveWecomSlot(store.db,materialized.callbackUrl,now())` immediately before sending. If no slot is available, leave attempts/backoff unchanged and return. For terminal outcomes run:

```js
store.db.prepare('UPDATE subscription_deliveries SET attempts=?,failed_at=?,last_error=?,payload_json=NULL WHERE event_id=?').run(row.attempts+1,now(),result.code,row.event_id);
```

For retryable outcomes preserve the existing exponential formula and payload. For success set `delivered_at`, clear `last_error` and `payload_json`. Convert thrown ordinary-network errors and `AbortError` to safe codes without persisting messages. Select only rows with `failed_at IS NULL` and use injected `now()` consistently.

In `server/src/index.mjs`, pass the configured public base URL to both router and worker:

```js
const meetingPublicBaseUrl=process.env.MEETING_PUBLIC_BASE_URL??null;
host.app.route('/meeting-service',createMeetingRouter(store,{adminToken:admin,adminId:process.env.MEETING_ADMIN_ID??'local-admin',retentionSeconds,workerConcurrency,timeKeys,publicBaseUrl:meetingPublicBaseUrl,onDelete:id=>activeWork.get(id)?.abort(),getAsrProgress:id=>asrProgress.get(id)??null}));
const stopSubscriptions=startSubscriptionWorker(store,{baseUrl:meetingPublicBaseUrl,onError:code=>console.error(code)});
```

Keep route construction in its existing location; declare `meetingPublicBaseUrl` before `host.app.route()` and remove the duplicate inline environment read at worker startup.

- [ ] **Step 4: Run all delivery tests**

Run: `cd server && node --test test/wecom.test.mjs test/subscriptions.test.mjs`
Expected: all PASS; permanent errors never issue a second request and no raw upstream message appears in database rows.

- [ ] **Step 5: Commit**

```bash
git add server/src/subscriptions.mjs server/src/index.mjs server/test/wecom.test.mjs server/test/subscriptions.test.mjs
git commit -m "feat: deliver reliable WeCom notifications"
```

---

### Task 6: 净化备份与最新安全库恢复对账

**Files:**
- Modify: `server/src/backup.mjs:17-22`
- Create: `server/src/subscription-restore.mjs`
- Modify: `server/src/restore.mjs:24-34,58-63,137-142`
- Test: `server/test/backup.test.mjs`
- Create: `server/test/subscription-restore.test.mjs`
- Modify: `server/test/restore.test.mjs`

**Interfaces:**
- Produces: `readSubscriptionSnapshot(source): null|{subscriptions:Array,deliveries:Array,rateLimits:Array}`；只从安全库读取并完整验证。
- Produces: `restoreSubscriptionSnapshot(target,snapshot): void`；在调用方恢复事务内先清空三张表，再重建安全库状态。
- `backupDatabase()` validates all three sensitive tables contain zero rows before read-only final verification。

- [ ] **Step 1: Write failing sanitization and reconciliation tests**

Create `server/test/subscription-restore.test.mjs` with real temporary databases and cover these exact scenarios:

```js
import {test} from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {MeetingStore} from '../src/store.mjs';
import {SubscriptionStore} from '../src/subscriptions.mjs';
import {readSubscriptionSnapshot,restoreSubscriptionSnapshot} from '../src/subscription-restore.mjs';

test('snapshot restores revoked, pending, terminal and rate state from safety',()=>{
  const safety=new MeetingStore(':memory:'),target=new MeetingStore(':memory:'),subscriptions=new SubscriptionStore(safety.db);
  try{
    const bot=subscriptions.createWecom({name:'群',callbackUrl:'https://qyapi.weixin.qq.com/cgi-bin/webhook/send?key=aaaaaaaaaaaaaaaa',now:1});
    subscriptions.revoke(bot.id,2);
    const snapshot=readSubscriptionSnapshot(safety.db);
    target.exec('BEGIN IMMEDIATE');restoreSubscriptionSnapshot(target.db,snapshot);target.exec('COMMIT');
    assert.equal(target.db.prepare('SELECT status FROM meeting_subscriptions WHERE id=?').get(bot.id).status,'revoked');
    assert.deepEqual(readSubscriptionSnapshot(target.db),snapshot);
  }finally{safety.close();target.close()}
});

test('missing subscription schema restores no subscriptions and corrupt channel fails closed',()=>{
  const store=new MeetingStore(':memory:');
  try{
    assert.throws(()=>{store.db.prepare("UPDATE meeting_subscriptions SET channel='broken'").run()},/CHECK|constraint/);
    store.db.prepare("DELETE FROM service_metadata WHERE key='subscription_schema'").run();
    assert.equal(readSubscriptionSnapshot(store.db),null);
  }finally{store.close()}
});
```

In integration tests, create a backup before later safety changes, then perform restore and assert: later revoked bot remains revoked; later added subscription and event remain; pending retry retains identical payload; delivered/terminal rows have null payload and never requeue; deleted/cancelled meeting events do not return; all rate slots match safety. Add corruption cases for bad channel, invalid status, invalid timestamps, malformed payload JSON, terminal row with payload, delivered row with payload, and foreign-key violation; every case throws exactly `invalid_subscription_state` and leaves no output database.

Extend `server/test/backup.test.mjs` to seed all three sensitive tables, call `backupDatabase`, open the export read-only, and assert each `COUNT(*)===0` while schema remains present. Verify the source safety database still contains its rows and credentials.

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd server && node --test test/backup.test.mjs test/restore.test.mjs test/subscription-restore.test.mjs`
Expected: FAIL because `subscription-restore.mjs` is missing and exported rows are not sanitized.

- [ ] **Step 3: Implement sanitization and strict snapshot restoration**

In `backupDatabase`, immediately after cleanup and before opening the read-only `copy`, run a writable transaction on the export:

```js
const sanitized=new DatabaseSync(output);
try{
  sanitized.exec('PRAGMA foreign_keys=ON; BEGIN IMMEDIATE');
  sanitized.exec('DELETE FROM subscription_deliveries; DELETE FROM wecom_rate_limits; DELETE FROM meeting_subscriptions; COMMIT');
}finally{sanitized.close()}
```

In final read-only verification, add:

```js
for(const table of ['meeting_subscriptions','subscription_deliveries','wecom_rate_limits'])if(copy.prepare(`SELECT COUNT(*) AS n FROM ${table}`).get().n!==0)throw new Error('backup_subscription_secrets');
```

Create `subscription-restore.mjs` with one explicit validator per row and no secret-bearing error text:

```js
const integer=value=>Number.isSafeInteger(value)&&value>=0;
const safeCode=value=>value===null||/^[a-z0-9_]{1,80}$/.test(value);
const invalid=()=>{throw new Error('invalid_subscription_state')};
const table=(db,name)=>db.prepare("SELECT 1 FROM sqlite_master WHERE type='table' AND name=?").get(name);
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
    if(row.channel==='wecom'&&(row.status==='pending'||row.callback_token!==null))invalid();ids.add(row.id);
  }
  for(const row of deliveries){
    if(typeof row.event_id!=='string'||!ids.has(row.subscription_id)||typeof row.meeting_id!=='string'||!integer(row.meeting_generation)||!integer(row.attempts)||!integer(row.next_attempt_at)||!integer(row.created_at)||!safeCode(row.last_error))invalid();
    if(row.delivered_at!==null&&!integer(row.delivered_at))invalid();
    if(row.failed_at!==null&&!integer(row.failed_at))invalid();
    if(row.delivered_at!==null&&row.failed_at!==null)invalid();
    if((row.delivered_at!==null||row.failed_at!==null)&&row.payload_json!==null)invalid();
    if(row.payload_json===null&&row.delivered_at===null&&row.failed_at===null&&row.attempts>0&&row.last_error===null)invalid();
    if(row.payload_json!==null){let saved;try{saved=JSON.parse(row.payload_json)}catch{invalid()}if(typeof saved?.body!=='string'||!saved.headers||typeof saved.headers!=='object'||!integer(row.share_expires_at))invalid()}
    const meeting=db.prepare('SELECT state FROM meetings WHERE id=?').get(row.meeting_id);if(!meeting)invalid();
    if(['deleted','deleting','cancelled'].includes(meeting.state)&&row.delivered_at===null&&row.failed_at===null)invalid();
  }
  for(const row of rateLimits)if(!/^[a-f0-9]{64}$/.test(row.webhook_hash)||!integer(row.next_allowed_at))invalid();
  return {subscriptions,deliveries,rateLimits};
}
```

Implement `restoreSubscriptionSnapshot(target,snapshot)` by returning immediately for null, otherwise deleting deliveries, limits and subscriptions, then inserting every field with parameterized SQL. Preserve event IDs, timestamps, attempts, errors, payload and expiry exactly; after inserts run `PRAGMA foreign_key_check` and throw `invalid_subscription_state` on any row. Call `readSubscriptionSnapshot(source)` before source transaction ends, and call `restoreSubscriptionSnapshot(target,snapshot)` after tombstone/share reconciliation but before COMMIT. Do not read subscription rows from `snapshot` (the sanitized export). Catch validation errors without wrapping them in messages containing row values.

- [ ] **Step 4: Run backup and restore suites**

Run: `cd server && node --test test/backup.test.mjs test/restore.test.mjs test/subscription-restore.test.mjs test/subscriptions.test.mjs test/wecom.test.mjs`
Expected: all PASS; every corrupt case fails closed with `invalid_subscription_state`, and sanitized backup never deletes safety state.

- [ ] **Step 5: Commit**

```bash
git add server/src/backup.mjs server/src/restore.mjs server/src/subscription-restore.mjs server/test/backup.test.mjs server/test/restore.test.mjs server/test/subscription-restore.test.mjs
git commit -m "fix: reconcile sanitized subscription backups"
```

---

### Task 7: 企业微信管理界面与终态展示

**Files:**
- Create: `server/src/wecom-ui.mjs`
- Modify: `server/src/subscription-ui.mjs`
- Modify: `server/src/ui.mjs:1-2,44-50`
- Test: `server/test/ui-workflow.test.mjs`

**Interfaces:**
- Produces: `wecomStyles`, `wecomDialog`, `wecomScript` exports composed into `meetingUi()`。
- `wecomScript` calls `openSubscriptions()` after a successful save and uses global `request()`。
- Existing list reads `channel`, `failedAt`, `lastError`; WeCom rows add a test button and never expose key。

- [ ] **Step 1: Write failing UI workflow tests**

Extend the existing VM `Element` only with `showModal`, `close`, `disabled`, `checked`, and `onsubmit` behavior needed by the test. Add:

```js
test('WeCom dialog tests without saving then saves approved configuration',async()=>{
  const admin='a'.repeat(40),store=new MeetingStore(':memory:');
  try{
    const app=createMeetingRouter(store,{adminToken:admin,publicBaseUrl:'https://example.com',subscriptionFetchImpl:async()=>Response.json({errcode:0})});
    const html=await meetingUi().text();assert.match(html,/添加企业微信机器人/);assert.doesNotMatch(html,/打开企业微信|二维码|扫码/);
    const script=html.match(/<script>([\s\S]*?)<\/script>/)[1];
    const names=['token','status','list','detail','more','load','pending','wecom-open','wecom-dialog','wecom-form','wecom-name','wecom-url','wecom-days','wecom-transcript','wecom-download','wecom-test','wecom-save','wecom-error','wecom-close','subscriptions-dialog','subscription-list','subscription-error','subscriptions-close','subscriptions-refresh'];
    const ids=Object.fromEntries(names.map(id=>[id,new Element()]));ids.token.value=admin;ids['wecom-dialog'].showModal=()=>{};ids['wecom-dialog'].close=()=>{};
    const context={document:{getElementById:id=>ids[id],createElement:tag=>new Element(tag)},location:{href:'https://local/meeting-service/ui'},URL,setTimeout,confirm:()=>true,fetch:(url,options)=>app.request(new URL(url).pathname.replace('/meeting-service','')+new URL(url).search,options)};
    runInNewContext(script,context);ids['wecom-open'].click();ids['wecom-name'].value='产品群';ids['wecom-url'].value='https://qyapi.weixin.qq.com/cgi-bin/webhook/send?key=aaaaaaaaaaaaaaaa';ids['wecom-days'].value='7';
    await ids['wecom-test'].click();assert.equal(store.db.prepare('SELECT COUNT(*) AS n FROM meeting_subscriptions').get().n,0);
    store.db.prepare('DELETE FROM wecom_rate_limits').run();await ids['wecom-form'].onsubmit({preventDefault(){}});
    const row=store.db.prepare('SELECT channel,status,share_days FROM meeting_subscriptions').get();assert.deepEqual(row,{channel:'wecom',status:'approved',share_days:7});
  }finally{store.close()}
});
```

Add these assertions to the workflow after saving and seeding one terminal delivery row:

```js
assert.match(ids['subscription-list'].textContent,/企业微信/);
assert.doesNotMatch(ids['subscription-list'].textContent,/aaaaaaaaaaaaaaaa/);
assert.match(html,/failedAt\?'终态失败':x\.deliveredAt\?'已送达':'待推送'/);
await context.openSubscriptions();
assert.match(ids['subscription-list'].textContent,/终态失败.*wecom_93000/);
assert.equal(ids['subscription-list'].children[0].children.filter(x=>x.className==='subscription-actions')[0].children.some(x=>x.textContent==='测试推送'),true);
assert.match(html,/margin-right:14px/);
```

Keep the existing fixed-width action-button and compact-row tests unchanged.

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd server && node --test test/ui-workflow.test.mjs`
Expected: FAIL because the add button/dialog and WeCom list behavior do not exist.

- [ ] **Step 3: Implement the dialog and list integration**

Create `wecom-ui.mjs` with this exact form contract and no instructional/installation links:

```js
export const wecomStyles=`#wecom-dialog{width:min(520px,calc(100vw - 32px));border:1px solid var(--line);border-radius:14px;padding:24px;background:#fff;color:var(--ink)}#wecom-dialog h2{margin:0 0 16px}#wecom-dialog label{margin-top:12px}#wecom-dialog .wecom-options{display:flex;gap:14px;align-items:center}#wecom-dialog .wecom-actions{display:flex;justify-content:flex-end;gap:8px;margin-top:20px}#wecom-dialog .wecom-actions button{width:96px}#wecom-error{color:#a54831}`;
export const wecomDialog=`<dialog id="wecom-dialog" aria-labelledby="wecom-title"><h2 id="wecom-title">添加企业微信机器人</h2><form id="wecom-form"><label for="wecom-name">机器人名称</label><input id="wecom-name" maxlength="100" required><label for="wecom-url">Webhook 地址</label><input id="wecom-url" type="url" required><label for="wecom-days">分享有效天数</label><input id="wecom-days" type="number" min="1" max="365" step="1" value="7" required><div class="wecom-options"><label><input id="wecom-transcript" type="checkbox">包含逐字稿</label><label><input id="wecom-download" type="checkbox">允许下载</label></div><p id="wecom-error" role="alert"></p><div class="wecom-actions"><button id="wecom-close" type="button">取消</button><button id="wecom-test" type="button">测试推送</button><button id="wecom-save" class="primary" type="submit">保存并启用</button></div></form></dialog>`;
export const wecomScript=`
const wecomDialog=document.getElementById('wecom-dialog'),wecomForm=document.getElementById('wecom-form'),wecomTest=document.getElementById('wecom-test'),wecomSave=document.getElementById('wecom-save'),wecomError=document.getElementById('wecom-error');
function wecomBusy(value){wecomTest.disabled=value;wecomSave.disabled=value}
function wecomValues(){return {name:document.getElementById('wecom-name').value.trim(),callbackUrl:document.getElementById('wecom-url').value.trim(),days:Number(document.getElementById('wecom-days').value),includeTranscript:document.getElementById('wecom-transcript').checked,allowDownload:document.getElementById('wecom-download').checked}}
document.getElementById('wecom-open').addEventListener('click',()=>{wecomForm.reset();document.getElementById('wecom-days').value='7';wecomError.textContent='';wecomDialog.showModal()});
document.getElementById('wecom-close').addEventListener('click',()=>wecomDialog.close());
wecomTest.addEventListener('click',async()=>{wecomBusy(true);wecomError.textContent='';try{await request('admin/subscriptions/wecom/test',{method:'POST',headers:{'content-type':'application/json'},body:JSON.stringify({callbackUrl:wecomValues().callbackUrl})});wecomError.textContent='测试推送成功，请确认群内已收到。'}catch(error){wecomError.textContent=error.message}finally{wecomBusy(false)}});
wecomForm.onsubmit=async event=>{event.preventDefault();const value=wecomValues();if(!Number.isInteger(value.days)||value.days<1||value.days>365){wecomError.textContent='请输入 1 到 365 的整数天。';return}wecomBusy(true);wecomError.textContent='';try{await request('admin/subscriptions/wecom',{method:'POST',headers:{'content-type':'application/json'},body:JSON.stringify(value)});wecomDialog.close();await openSubscriptions()}catch(error){wecomError.textContent=error.message}finally{wecomBusy(false)}};
`;
```

Add `添加企业微信机器人` beside refresh in `subscriptionDialog`. In list rendering, prefix the badge with `item.channel==='wecom'?'企业微信 · ':''`, add `测试推送` for approved WeCom rows, and derive delivery status with `x.failedAt?'终态失败':x.deliveredAt?'已送达':'待推送'`.

Compose imports, `${wecomScript}`, `${wecomDialog}`, and `${wecomStyles}` in `ui.mjs`; CSP remains hash-based and no inline event handlers are added.

- [ ] **Step 4: Run UI and API tests**

Run: `cd server && node --test test/ui-workflow.test.mjs test/wecom.test.mjs`
Expected: all PASS; test-before-save creates zero subscriptions and saved row is approved WeCom.

- [ ] **Step 5: Commit**

```bash
git add server/src/wecom-ui.mjs server/src/subscription-ui.mjs server/src/ui.mjs server/test/ui-workflow.test.mjs
git commit -m "feat: add WeCom subscription management UI"
```

---

### Task 8: 文档、全量回归与真实客户端上线门槛

**Files:**
- Modify: `server/API.md`
- Modify: `server/README.md`
- Modify: `server/BACKUP.md`
- Test: `server/test/wecom.test.mjs`
- Test: `server/test/subscription-restore.test.mjs`

**Interfaces:**
- Documents the three admin routes, safe response codes, redacted reads, frozen retries, rate limiting and sanitized backup authority。
- Produces a recorded manual acceptance result containing desktop/mobile platform, client version, date and synthetic meeting ID; no real webhook or token enters the repository。

- [ ] **Step 1: Add failing documentation contract checks**

Append a test that reads the docs and asserts all public contract anchors exist:

```js
import {readFileSync} from 'node:fs';
test('operator docs include WeCom routes and recovery boundaries',()=>{
  const api=readFileSync(new URL('../API.md',import.meta.url),'utf8');
  const backup=readFileSync(new URL('../BACKUP.md',import.meta.url),'utf8');
  for(const route of ['/admin/subscriptions/wecom','/admin/subscriptions/wecom/test','/admin/subscriptions/:id/test'])assert.ok(api.includes(route));
  for(const text of ['rate_limited','wecom_45009','share_expired'])assert.ok(api.includes(text));
  for(const table of ['meeting_subscriptions','subscription_deliveries','wecom_rate_limits'])assert.ok(backup.includes(table));
  assert.match(backup,/最新安全库/);assert.match(backup,/净化/);
});
```

- [ ] **Step 2: Run documentation checks to verify they fail**

Run: `cd server && node --test test/wecom.test.mjs`
Expected: FAIL on missing route and sanitized-backup documentation anchors.

- [ ] **Step 3: Write exact operator documentation**

In `server/API.md`, document request/response JSON from the design, administrator authentication, `201` creation, `200` test success, `400 invalid`, `429 rate_limited`, `502` safe upstream code, no-key list preview, terminal delivery fields, 3-second shared rate interval and frozen payload semantics. Include a complete formal `template_card` example and explicitly state that WeCom cards contain no body or extra download links.

In `server/README.md`, document `MEETING_PUBLIC_BASE_URL` as required for tests and automatic shares, plus the local HTTP test-double workflow. In `server/BACKUP.md`, state that export clears all rows from the three sensitive tables, latest safety database is authoritative, revoked/terminal/cancelled states cannot be resurrected, and corrupt state fails with `invalid_subscription_state`.

- [ ] **Step 4: Run complete automated verification**

Run: `cd server && npm test && cd .. && git diff --check`
Expected: all existing and new tests PASS, 0 FAIL; `git diff --check` exits 0. Record the exact totals in the final implementation summary rather than reusing the earlier 272-test baseline.

- [ ] **Step 5: Commit docs and verification tests**

```bash
git add server/API.md server/README.md server/BACKUP.md server/test/wecom.test.mjs server/test/subscription-restore.test.mjs
git commit -m "docs: describe WeCom delivery and recovery"
```

- [ ] **Step 6: Perform the blocking real-client acceptance with synthetic data**

Use a dedicated test group and a synthetic short-lived, minutes-only, no-download share. Do not send to a real group unless the user explicitly supplies the bot and requests this test. Record desktop and mobile client platform/version/date, click each card, and verify the correct synthetic minutes render. Inspect service logs and verify the fragment token is absent. Stop release if either client loses the fragment; execute Task 9 instead of moving the token into query.

---

### Task 9: Conditional fragment-loss fallback with hashed redirect codes

**Execute only when Task 8 observes fragment loss on either client.**

**Files:**
- Create: `server/src/share-redirect.mjs`
- Modify: `server/src/store.mjs`
- Modify: `server/src/subscriptions.mjs`
- Modify: `server/src/routes.mjs`
- Modify: `server/src/backup.mjs`
- Modify: `server/src/restore.mjs`
- Test: `server/test/wecom.test.mjs`
- Test: `server/test/restore.test.mjs`

**Interfaces:**
- Produces: `createShareRedirectInTransaction(store,shareToken,expiresAt): {code}` with a 256-bit base64url code and SHA-256-only code persistence。
- Produces: `resolveShareRedirect(db,code,now): string|null`，返回分享 token 或 null。
- Produces: `GET /share/redirect/:code` returning 303 to `/share#token` only when unexpired and the share/meeting remains valid; no one-time consumption。
- 安全库的 `share_redirects` 保存 `code_hash`、`share_token` 和 `expires_at`；现有分享只存哈希，因此 303 所需 token 必须在安全库此表中保存。导出备份清空整表，日志仍不得记录 token。
- WeCom card uses `${baseUrl}/share/redirect/${code}`; ordinary Webhook JSON remains `share#token`。

- [ ] **Step 1: Write failing redirect tests**

```js
test('hashed redirect supports repeated clicks and expires with its share',async()=>{
  const store=new MeetingStore(':memory:');
  try{
    store.create('d','m');store.db.prepare("UPDATE meetings SET state='completed',minutes='{}' WHERE id='m'").run();
    const share=store.createShare('m',false,1,1000,false);
    const {code}=store.tx(()=>createShareRedirectInTransaction(store,share.token,share.expiresAt));
    const app=createMeetingRouter(store,{adminToken:'admin123',publicBaseUrl:'https://example.com'});
    for(let click=0;click<2;click++){
      const response=await app.request('/share/redirect/'+code);
      assert.equal(response.status,303);assert.equal(response.headers.get('location'),'https://example.com/share#'+share.token);
    }
    const saved=store.db.prepare('SELECT code_hash,share_token FROM share_redirects').get();
    assert.match(saved.code_hash,/^[a-f0-9]{64}$/);assert.equal(saved.code_hash===code,false);assert.equal(saved.share_token,share.token);
    assert.equal((await app.request('/share/redirect/unknown')).status,404);
    store.db.prepare('UPDATE share_redirects SET expires_at=999').run();
    assert.equal((await app.request('/share/redirect/'+code)).status,404);
  }finally{store.close()}
});
```

Import `createShareRedirectInTransaction` from `../src/share-redirect.mjs`. Extend the backup/restore integration test with `assert.equal(exported.prepare('SELECT COUNT(*) AS n FROM share_redirects').get().n,0)`, restore from safety, and repeat the 303 assertion. In the formal WeCom test assert `card_action.url` matches `/\/share\/redirect\/[A-Za-z0-9_-]{43}$/` and has no query; retain the existing ordinary Webhook `share#token` assertion verbatim.

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd server && node --test test/wecom.test.mjs test/restore.test.mjs`
Expected: FAIL with 404 for `/share/redirect/:code` and missing redirect schema.

- [ ] **Step 3: Implement hashed redirect persistence and anonymous 303 route**

Create `server/src/share-redirect.mjs`:

```js
import {createHash,randomBytes} from 'node:crypto';
const hash=value=>createHash('sha256').update(value).digest('hex');
export function initializeShareRedirects(db){
  db.exec(`CREATE TABLE IF NOT EXISTS share_redirects(code_hash TEXT PRIMARY KEY,share_token TEXT NOT NULL,expires_at INTEGER NOT NULL CHECK(expires_at>=0))`);
}
export function createShareRedirectInTransaction(store,shareToken,expiresAt){
  if(!/^[A-Za-z0-9_-]{43}$/.test(shareToken)||!Number.isSafeInteger(expiresAt)||expiresAt<0)throw new Error('invalid');
  if(!store.db.prepare('SELECT 1 FROM meeting_shares WHERE token_hash=? AND expires_at=?').get(hash(shareToken),expiresAt))throw new Error('invalid_share_state');
  const code=randomBytes(32).toString('base64url');
  store.db.prepare('INSERT INTO share_redirects(code_hash,share_token,expires_at) VALUES(?,?,?)').run(hash(code),shareToken,expiresAt);
  return {code};
}
export function resolveShareRedirect(db,code,now){
  if(!/^[A-Za-z0-9_-]{43}$/.test(code)||!Number.isSafeInteger(now)||now<0)return null;
  const redirect=db.prepare('SELECT share_token,expires_at FROM share_redirects WHERE code_hash=? AND expires_at>?').get(hash(code),now);
  if(!redirect)return null;
  const valid=db.prepare("SELECT 1 FROM meeting_shares s JOIN meetings m ON m.id=s.meeting_id WHERE s.token_hash=? AND s.expires_at>? AND s.expires_at=? AND m.state NOT IN ('deleting','deleted','cancelled')").get(hash(redirect.share_token),now,redirect.expires_at);
  return valid?redirect.share_token:null;
}
```

Call `initializeShareRedirects(this.db)` in `MeetingStore` after share schema initialization. During WeCom materialization, create the redirect in the same transaction and pass `${baseUrl}/share/redirect/${code}` to `buildWecomCard`; leave the ordinary branch unchanged. Register this anonymous route beside `/share`:

```js
app.get('/share/redirect/:code',c=>{
  const token=resolveShareRedirect(store.db,c.req.param('code'),Math.floor(Date.now()/1000));
  if(!token||!publicBaseUrlOption)return c.json({error:'not_found'},404);
  c.header('Cache-Control','no-store');c.header('Referrer-Policy','no-referrer');
  return c.redirect(publicBaseUrl(publicBaseUrlOption)+'/share#'+token,303);
});
```

Add `share_redirects` to export sanitization and zero-row verification. Extend the Task 6 safety snapshot with redirect rows, validate code hashes, token syntax and matching share expiry, restore them only for live unexpired shares, and delete target redirect rows before rebuilding. Never include token values in validation errors.

- [ ] **Step 4: Run fallback regression tests**

Run: `cd server && npm test && cd .. && git diff --check`
Expected: all tests PASS, including multi-click, expiry, deletion, sanitized backup and ordinary webhook compatibility.

- [ ] **Step 5: Commit fallback**

```bash
git add server/src/share-redirect.mjs server/src/store.mjs server/src/subscriptions.mjs server/src/routes.mjs server/src/backup.mjs server/src/restore.mjs server/test/wecom.test.mjs server/test/restore.test.mjs
git commit -m "fix: preserve share access from WeCom cards"
```

- [ ] **Step 6: Repeat the blocking real-client acceptance**

Repeat Task 8 Step 6 on desktop and mobile with a fresh synthetic share. Both clients must display the correct minutes before release; record that redirect codes may appear in controlled access logs while fragments, callback tokens and complete Webhooks remain absent.
