# 会议纪要订阅使用指南

会议纪要完成后，服务可以主动向外部服务或企业微信群机器人发送一个自动生成的只读分享链接。本文分别说明两种接入流程；完整字段、状态码和错误语义见[服务端 API](../server/API.md#会议纪要订阅)。

## 开始前：配置公网服务地址

两种推送都要求部署服务时设置 `MEETING_PUBLIC_BASE_URL`。它必须是接收方能够访问的完整服务前缀，生产环境使用 HTTPS，并包含 `/meeting-service`：

```bash
MEETING_PUBLIC_BASE_URL=https://meetings.example.com/meeting-service
```

不要在末尾添加 `/`。如果未配置或配置无效，企业微信测试推送返回 HTTP 400，正式自动投递也不会启动。`localhost` 和 `127.0.0.1` 只适用于本机测试，不能供外部服务或企业微信客户端访问。

分享链接是限时只读凭证，应像密码一样保护，不要写入公开日志或发送到无关群聊。默认配置为 **7 天、仅纪要、禁止下载**。

## 流程一：外部服务向我们申请订阅

### 1. 外部服务准备回调接口

外部服务先提供一个可公网访问的 HTTPS POST 接口，用于接收会议完成事件。建议该接口：

- 验证请求中的 `Authorization: Bearer <callbackToken>`（如果申请时提供了令牌）。
- 使用 `eventId` 去重；投递采用至少一次语义，同一事件可能重复到达。
- 不依赖 HTTP 重定向，接收后返回任意 2xx。
- 保护回调中的分享链接和下载地址，不记录完整凭证。

### 2. 提交订阅申请

外部服务调用匿名申请接口，无需管理员令牌：

```bash
curl -X POST 'https://meetings.example.com/meeting-service/subscriptions' \
  -H 'Content-Type: application/json' \
  -d '{
    "name": "知识库服务",
    "callbackUrl": "https://subscriber.example/hooks/meeting-minutes",
    "callbackToken": "替换为至少16位的随机回调令牌"
  }'
```

`callbackToken` 可选；若不提供，回调请求不带 Bearer 令牌。申请成功返回 HTTP 202：

```json
{
  "id": "订阅 ID",
  "status": "pending"
}
```

此时只创建了待审批申请，不会向回调地址发送测试请求，也不会投递会议纪要。

### 3. 管理员审批并配置分享

1. 打开 `https://meetings.example.com/meeting-service/ui`。
2. 使用管理员令牌登录，点击右上角“订阅管理”。
3. 核对申请名称和完整回调地址，确认申请方可信。
4. 配置分享有效天数（1–365）、是否包含逐字稿、是否允许下载。
5. 点击“批准订阅”；不可信的申请点击“拒绝”。

批准后仍可随时修改配置或撤销订阅。配置修改只影响之后创建的分享；已推送链接和已冻结的待重试载荷不会改变。

### 4. 外部服务接收会议完成事件

批准后的每一场新会议完成时，服务向回调地址发送 JSON POST。未配置回调令牌时，请求只有 `Content-Type: application/json`；配置后还会包含 `Authorization: Bearer <callbackToken>`。

```json
{
  "event": "meeting.minutes.completed",
  "eventId": "全局唯一事件 ID",
  "meetingId": "会议 ID",
  "title": "产品周会",
  "completedAt": 1799900000,
  "shareUrl": "https://meetings.example.com/meeting-service/share#分享凭证",
  "shareExpiresAt": 1799999999
}
```

`title` 是清理后的会议名称，缺失或空白时回退为 `meetingId`；`completedAt` 是纪要完成时间的 Unix 秒。旧版本回调可能不包含这两个新增字段，订阅方应宽容解析并提供回退显示。

当“允许下载”开启时，回调还包含两个下载地址：

```json
{
  "downloadUrls": {
    "minutes": "https://meetings.example.com/meeting-service/share/download/minutes",
    "transcript": "https://meetings.example.com/meeting-service/share/download/transcript"
  }
}
```

外部服务需从 `shareUrl` 的 `#` 后取出分享令牌，并使用 `Authorization: Bearer <分享令牌>` 请求下载地址。只有同时开启“包含逐字稿”时，逐字稿下载才可用；两个地址与分享链接具有相同的有效期。

### 5. 处理失败、重试与撤销

- HTTP 408、429、5xx、网络错误和 10 秒超时会按 5 秒至 5 分钟的指数退避重试。
- 其他 HTTP 4xx 属于终态失败，不再重试；管理界面显示安全的错误码。
- 如果分享到期时仍未送达，事件以 `share_expired` 终止。
- 撤销订阅会取消尚未送达的事件，但不能收回已经发出的响应；会议删除后，对应分享链接的新访问失效。
- 不补发批准前的历史会议；重新转写完成会生成新事件。

## 流程二：管理员主动添加企业微信机器人

### 1. 在企业微信群创建机器人

1. 在企业微信中打开一个内部群聊。
2. 桌面端点击群聊右上角“…”，手机端进入群设置。
3. 找到“群机器人”，选择“添加”并新建机器人。
4. 填写机器人名称，例如“会议纪要助手”。
5. 在机器人详情页复制完整 Webhook 地址。

不同企业微信版本的入口名称可能略有差异；如果没有“群机器人”，请联系企业管理员确认权限。Webhook 地址格式如下：

```text
https://qyapi.weixin.qq.com/cgi-bin/webhook/send?key=机器人密钥
```

Webhook 是发送凭证，不要公开发布。服务只接受上述规范 HTTPS 主机和路径，key 只能由字母、数字、下划线或连字符组成，长度为 16–128；不允许其他查询参数、fragment、用户信息或显式端口。

### 2. 在管理页面添加并测试

1. 打开会议服务工作台，使用管理员令牌登录。
2. 点击“订阅管理” → “添加企业微信机器人”。
3. 填写机器人名称和完整 Webhook 地址。
4. 配置有效天数、是否包含逐字稿、是否允许下载。
5. 点击“测试推送”，确认群内收到“企业微信机器人测试消息”卡片。
6. 测试成功后，“确认”按钮由灰色变为可点击；点击“确认”创建并立即批准订阅。

测试消息不包含真实会议、分享链接或分享令牌，也不会创建订阅。修改 Webhook 后必须重新测试；同一 Webhook 的测试和正式推送共享每 3 秒一个发送时隙，遇到限流提示可稍后重试。

### 3. 接收正式会议纪要卡片

每一场新会议完成时，服务生成分享链接并向群里发送“新会议纪要已生成”卡片。卡片包含会议名称、完成时间、分享有效期和点击入口，不包含纪要正文或单独的下载地址。

企业微信的“允许下载”只影响分享页面中的下载权限，不会在群消息中额外发送下载链接。会议名称缺失或只有空白时使用会议 ID。

### 4. 后续管理与排错

管理员可以在“订阅管理”中修改分享配置、再次测试或撤销机器人，并查看最近投递状态。

- **HTTP 400 / `invalid`**：检查 `MEETING_PUBLIC_BASE_URL` 和 Webhook 格式。
- **HTTP 429 / `rate_limited`**：同一机器人操作过于频繁，稍后重试。
- **`wecom_93000`**：Webhook 无效或机器人已移出群聊，需要重新创建或更换机器人。
- **终态失败**：不会无限重试；修正原因后可使用测试推送验证，新会议仍会按有效订阅投递。
- **重复卡片**：当前允许同一 Webhook 绑定多条有效订阅，每条订阅都会推送一次；如不需要重复消息，请撤销多余订阅。相同 Webhook 的多个订阅仍共享发送额度。

## 相关文档

- [服务端 API：会议纪要订阅](../server/API.md#会议纪要订阅)
- [部署与反向代理](../deploy/README.md)
- [备份、净化与恢复](../server/BACKUP.md)
