# 会议服务 HTTP 接口（开发版）

接口挂载在 `/meeting-service`。对外使用 HTTPS；反向代理部署时，服务可在受限的本机网络使用 HTTP，TLS 由代理终止。当前协议版本为 1。客户端可在请求中发送 `X-Meeting-Protocol-Version: 1`；未提供时按版本 1 处理，其他值返回 HTTP 400 和 `unsupported_protocol_version`，不会执行操作。业务响应带 `X-Meeting-Protocol-Version: 1`；上游代理返回的错误可能没有该头。后续不兼容的语义须使用新版本，不能改变版本 1 的确认和删除规则。管理员和设备使用不同的 `Authorization: Bearer <token>`；管理员令牌和设备令牌不能互换。设备身份由令牌确定，客户端不得自行声明归属。所有 JSON 请求为 UTF-8，音频块是原始二进制。出错返回 `{"error":"..."}`，不要把令牌、录音或逐字稿写入日志。

管理员通过 `POST /devices`，提交 `{"id":"device-id"}` 创建或重绑定已撤销设备。返回 `{id,token,credentialId}`；服务端随机生成 256 位凭据，只在这一响应中提供，调用方须通过受保护渠道交付设备。设备已存在且未撤销时返回 409；响应丢失时，可由管理员调用 `GET /devices/:id` 获取当前 credentialId 和撤销状态，再撤销并生成新令牌；令牌本身无法找回。`POST /devices/:id/revoke` 提交 `{"credentialId":"...","eventId":"..."}` 撤销指定凭据，可重复调用，旧撤销请求不会撤销重新配对产生的新凭据；同一事件 ID 用于不同设备或凭据时返回 409。`GET /devices/:id/revocations` 可查看最近 100 条撤销事件（包括管理员身份和时间，不包含凭据秘密）。管理员令牌目前是本机单一共享密钥；完整的配对、离线撤销待办和独立管理身份仍待实现。

## 签名时间证明（开发中）

从 v0.1.3 开始提供此接口；服务部署后须先初始化签名器并配置代理，设备端尚需制备公钥和端到端验证。密钥轮换仍未实现。管理员通过 HTTPS 调用 `POST /admin/time-signer`，提交 `{"origin":"https://example.com","verifiedEpoch":整数秒}` 建立一次性签名身份；校验后的管理员时间必须与服务器时间相差不超过 30 秒。重复提交同一 origin 返回相同公钥，不能用此接口换钥或修改 origin。管理员可通过 HTTPS 的 `GET /admin/time-signer` 获取 `{origin,keyId,publicKey}`；公钥是 65 字节的 P-256 非压缩点十六进制，`keyId` 为该点的 SHA-256 十六进制。私钥保存在数据库中，不会经接口返回。

`GET /time-proof/:nonce` 不带设备或管理员令牌；`nonce` 是设备安全随机生成的非全零 32 字节小写十六进制。成功返回 `{keyId,message,signature}`，均为十六进制。`message` 是 `MTP1`、32 字节挑战、32 字节 origin SHA-256、8 字节大端 UTC 秒数的拼接；`signature` 是 P-256/SHA-256 的 DER 签名。设备必须使用已通过可信配网绑定的公钥验签、核对挑战与 origin，并执行已有时间下界和新鲜度检查；返回值或 HTTP 本身不证明可信时间。未制备、时钟回退、超出允许前跳或密钥损坏时返回 503；接口全局限流为每实例每分钟 60 次。

如果设备尚无可信时间，需要代理为**仅此时间证明路径**提供不携带令牌的 HTTP 启动入口，且禁止明文转发管理员、上传和其他业务接口；其他接口仍只经 HTTPS。代理的具体配置和端到端验收尚未完成。服务运行期间每小时保存时间下界；当前允许服务重启间断不超过 30 天，超过后将拒绝签发，可信恢复流程及密钥轮换尚待实现。时间签名只能证明消息来自持钥服务，不能独立证明服务器系统时钟准确。

设备上传顺序：

1. `POST /meetings/:id` 创建会议；同一设备可重试，其他设备不能取得会议。
2. `PUT /meetings/:id/chunks/:seq` 上传从 0 开始的连续块，请求头 `X-Content-SHA256` 为该块小写 SHA-256。每块最多 262144 字节，会议最多 20000000 字节；新上传的块序号为 0–8191。相同序号和内容可幂等重试，不同内容返回 409；即使已确认，重试原有相同块仍幂等。
3. `GET /meetings/:id` 查询块序号、哈希、大小和 `valid` 状态，用于重连后补传；`valid: false` 表示服务器保存的块与其记录哈希不一致，应使用原始块重传；已确认时同时返回 `committed: true` 和 `manifest`。

   内存受限的设备可使用 `GET /meetings/:id/chunks-window/:first` 分页查询，`first` 为 0–8191 内的 16 的倍数。返回 `{id,first,chunks}`，`chunks` 仅包含该 16 块范围内已保存的 `{seq,digest,size,valid}`。响应是上传诊断信息，并非持久化确认；设备仍须校验本地块并最终核对清单。服务端需要包含此接口的新版本；旧版返回 404 时可安全重传原始块。
4. `POST /meetings/:id/finalize` 提交 `{"count":块数,"bytes":总字节数,"digest":"拼接音频 SHA-256","reason":"user"}`。`reason` 可为 `user`、`limit`、`storage`、`interrupted`。服务端检查每块及整段音频，在同一 SQLite 事务持久化录音清单与待处理状态，成功响应包含 `id`、`committed: true` 和 `manifest`；设备必须逐项核对会议 ID、块数、字节数、整体哈希、结束原因、录音时间和时间可信标记后，才能按本地持久化状态机删除副本。确认响应丢失时可以查询或重试相同清单并核对。重试相同清单安全；缺块、哈希或清单不一致返回 409。
5. `POST /meetings/:id/cancel` 仅取消尚未确认的上传。若确认已完成则返回 `committed` 和相同清单；这不是删除服务端会议。即使创建请求尚未到达，取消也会为该设备和会议 ID 持久化取消状态；迟到或重试的创建请求不能重启上传，取消可安全重试。

管理员可用 `GET /admin/settings` 查询当前工作器并发数和已完成会议的保留期限（`null` 表示未启用自动删除），通过 `GET /admin/meetings` 和 `GET /admin/meetings/:id` 查看状态和结果，`GET /admin/meetings/:id/result` 查看结构化当前结果，`GET /admin/meetings/:id/audio` 以 Ogg Opus 导出已确认并通过完整性校验的音频，`GET /admin/meetings/:id/minutes.md` 导出中文 Markdown 会议纪要，`GET /admin/meetings/:id/versions` 查看历史，`GET /admin/meetings/:id/attempts` 查看最近 100 次处理尝试（不含租约秘密）；`POST /admin/meetings/:id/retry` 重试失败任务，`POST /admin/meetings/:id/retranscribe` 创建新的转写版本；`DELETE /admin/meetings/:id` 删除服务端会议并撤销运行任务。删除会清理当前数据库中的内容，但不能保证供应商、SQLite WAL 或备份中内容已被物理抹除。

典型错误：401 未认证、404 资源不可访问、409 状态或清单冲突、413 内容超限、422 音频容器无效或格式不受支持、429 当前请求过多、507 服务端音频块空间额度不足或 SQLite 报告数据库空间已满。不可使用错误码推测其他设备会议是否存在。

导出与结果查看需要管理员身份；响应禁止缓存。已经开始发送的导出响应不受后续删除请求回收，管理员应在删除前结束下载。导出只覆盖服务端已确认音频，不表示服务器以外的副本已删除。

会议列表每页最多 100 条，返回 `nextCursor`；非空时以 `GET /admin/meetings?before=<nextCursor>` 读取下一页，游标仅用于翻页。新增会议可能出现在第一页，重新读取可看到最新结果。

结构化结果的 `processing` 包含 `attempts`、`retryAt`（Unix 秒或 null）、`failedStage` 和安全错误码 `lastError`。失败时可根据阶段重试；例如 `asr_http_401` 表示检查 ASR 凭据，`integrity` 表示保存的音频校验失败。模型原始错误和凭据不会写入此字段。

已确认录音的查询、确认重试和取消协调会再次核验服务端音频。若返回 `409 integrity`，设备不得据此认定当前服务端副本完好或自动删除本地录音；设备可重传原始块：当块序号和哈希与服务端已记录的块一致、但保存的数据损坏时，服务端会修复该块并返回 `repaired: true`。设备应重传所需块并重新提交完整清单；只有整段校验通过后才返回可核对的确认。若 ASR 曾因完整性失败而停止，全部修复且整段校验通过后会自动重新排队；其他失败原因不会自动重试。如果块记录缺失、哈希元数据损坏或设备已没有原始副本，需要管理员从可信备份恢复。其他错误或响应丢失均不是确认。

最终清单可附带 `recordedAt`（UTC Unix 秒，允许 2000-01-01 至 2100-01-01）和 `timeTrust`（`trusted`、`unverified` 或 `uncalibrated`）。缺省为 `recordedAt: null`、`timeTrust: "uncalibrated"`；`trusted` 必须同时提供时间。设备不能确定或时间不在允许范围时应传 null。服务端保留原值，不把上传时间当录音时间，也不独立证明设备的可信标记；这些字段进入确认清单，重试时必须一致。管理结果的 `recordingTime` 返回原始时间和标记。

所有自动生成的纪要均标记 `reviewRequired: true`；`reviewReasons` 指出转录及语义支持未经人工核验，截断会议还会标记 `meeting_incomplete`。引用文本匹配只验证位置，不代表结论必然由原文支持。

结构化结果同时返回 `audioIntegrity` 和 `captureStatus`：前者为服务端读取时的音频完整性（`verified`、`failed` 或 `not_committed`）；后者为录音结束语义（`ended_by_user`、`truncated` 或 `unknown`）。这两者彼此独立：音频校验通过不代表整场会议录制完整；`ended_by_user` 也不证明整场会议均已录下。达到时长上限、存储不足或录音中断均标记 `truncated`。

设备在收到 `409 integrity` 后可调用 `GET /meetings/:id/repair-status`，获取 `damagedSequences`、`expectedCount`、`missingCount` 和 `overall`。该响应是诊断信息，不是上传确认。只可用原始块及其原哈希重传；若有缺失块，或 `overall` 为 `failed` 但没有损坏块，需管理员检查并从可信备份恢复。未确认上传的 `overall` 为 `not_committed`。

最终确认前，服务端会验证设备提交的 SCR1 帧边界及每包 20 毫秒的 Opus TOC，或 Ogg Opus 容器、CRC、单声道与按 Opus 包计算的时长上限；结构校验失败返回 422，保留已上传块而不返回确认。确认只证明字节、待处理状态和基本容器结构通过校验，不保证每个 Opus 包均可解码，也不能证明会议内容完整。设备端仍需在录音保存时验证自身文件及断电恢复状态。

Opus 包时长按 [RFC 6716](https://www.rfc-editor.org/rfc/rfc6716) 的 TOC 计算，Ogg 标记的时间使用 [RFC 7845](https://www.rfc-editor.org/rfc/rfc7845) 的 48 kHz 单位。校验不解码音频，也不能发现所有损坏的压缩内容。

单进程内每个设备最多同时处理 4 个请求，超出返回 `429 device_busy` 和 `Retry-After`，客户端应退避重试；全局并发限制仍单独生效。该机制不是多实例共享限速，也不替代边界网络防护。

每台设备最多保留 64 个处于 `uploading` 的会议；超过时创建新会议返回 `409 too_many_pending`。已有上传仍可重试或明确取消，其他设备不受该计数影响；服务端不会为了腾出名额自动删除未确认录音。升级前已存在的未完成上传保留原有块数上限，仍可补传缺失的高序号块并完成确认；新建上传使用 8,192 块上限。

逐字稿版本中的 `segments` 按顺序拼接后即为 ASR 接口返回的最终文本，`sha256` 是该文本 UTF-8 字节的哈希；分段仅按 Unicode 码点切分，不修改空白或做 Unicode 规范化。ASR 请求启用了供应商的文本归一化（ITN），供应商在返回前的原始识别结果不由本服务获取或保存；这里的原始文本指供应商返回的最终文本，而非 ITN 之前的文本。证据范围基于各片段的 Unicode 码点，不是 UTF-16 偏移。

管理员可用 `GET /admin/deletions` 查询待清理的删除墓碑，只返回会议 ID、设备归属及 `deleting` 状态，不返回录音或结果。每页最多 100 条，使用 `nextCursor` 和 `?before=` 翻页。清理失败或未完成时该状态保留，后台会重试；可重复提交该会议的 DELETE。启动后若个别会议清理失败，其墓碑仍保留，服务会继续处理其他会议并轮流重试；管理员应检查存储，不能将待清理状态视为清理完成。

ASR 运行期间，管理员结果接口的 `processing.asrProgress` 可返回 `{deltaEvents: 数量}`，代表本次请求已收到的增量事件数，不是时长百分比，也不包含中间文本。该信息仅保存在当前进程内，重试、重启、结束或删除后会清除；不可用时为 `null`。最终逐字稿仍须等待完成事件并持久化。

管理员可通过 `GET /admin/status` 查询各处理状态的会议数量和 `expiredLeases`（等待回收的过期租约数）。统计不包含会议内容、已删除或已取消记录，只反映查询时的数据库状态，不能证明模型供应商可用或工作器一定正常运行。

纪要模型可只输出版本和片段 ID，服务端附上该片段的完整原文及 Unicode 码点范围。若模型提供摘录但未提供范围，必须在该片段内有唯一精确匹配；找不到或存在多个匹配时不发布。若模型提供范围，则按提供值严格校验，不会静默修正。片段定位正确仍不证明该片段在语义上支持结论。

会议纪要 Markdown 参照 `meeting-minutes` 技能的会议纪要格式：包含会议基本信息、议题、待跟进事项表及关键结论；缺少的信息标记“待确认”。结果仍附带结构化引用并要求人工复核，录音截断时显示不完整提示。

新生成的逐字稿记录经校验音频计算的 `durationSeconds`；Markdown 的时长由该数值生成并注明“录音时长”，不采用模型推测的时长。旧转写没有该字段时显示“待确认”。时长只反映保存的音频，不证明整场会议均已录下。

纪要日期优先显示设备提交的录音时间（UTC，并保留设备可信或未验证标记）；无法使用设备时间时，如模型从转写推断日期则明确标记“转写推断，待核对”，否则显示“待确认”。设备的可信标记由设备提供，服务端不独立证明其准确。

`GET /health`（及 `HEAD /health`）无需认证，用于进程存活检查；正常返回 200（GET 为 `{"status":"ok","version":"v0.1.2"}`（版本为发布标签；本地运行默认使用 package.json 版本）），停机时返回 503。它不检查数据库、模型供应商或任务处理能力；反向代理须将 `/health` 转发到服务端。

### 临时分享

管理员通过 `POST /admin/meetings/:id/shares` 提交 JSON `{ "includeTranscript": false, "allowDownload": false, "days": 7 }`，有效期为 1–365 整数天。返回 `{token, expiresAt}`，其中过期时间是 Unix 秒。令牌只在创建时返回，请勿记录或公开。匿名页面为 `GET /share`，浏览器将令牌放入 URL fragment，并用 `Authorization: Bearer <token>` 请求 `GET /share/data`。接口只返回纪要 Markdown 和按权限可选的逐字稿；未完成重转写时返回 `unavailable: true`，无效、过期或已删除会议的链接返回 404。分享链接持有者均可访问，不需管理员令牌；会议进入删除状态后新请求失效，已开始读取的请求可能完成。分享链接不含音频；目前没有单独撤销或列表功能。`allowDownload` 默认为 false；为 true 时可通过 `GET /share/download/minutes` 下载 Markdown，且只有同时包含逐字稿时才可通过 `GET /share/download/transcript` 下载文本。下载接口使用相同分享凭证，未授权返回 404。旧链接迁移后默认禁止下载。

分享优先显示与当前生成纪要和逐字稿版本匹配的人工编辑；编辑过期或旧数据没有可验证的逐字稿版本时，改为显示当前自动生成纪要并提示。管理员重新保存编辑后，分享页可显示该编辑。

管理员可向 `POST /admin/meetings/:id/review` 提交详情接口返回的 `reviewKey`，确认当前版本已人工复核；若内容已变化，接口返回 409。复核状态绑定当前逐字稿和纪要，纪要保存或重新转写后需重新复核；分享页只对匹配版本显示已复核。人工编辑无法与当前转写匹配时，须先保存匹配版本才能确认复核。

人工编辑的正文会原样保留，包括与历史系统提示相同的文字。系统无法判断这类文字的来源；若正文包含类似复核状态的独立段落或引用，分享页和下载内容会提示读者以当前系统状态为准，不会静默删除正文。

### 会议纪要订阅

外部服务通过匿名 `POST /subscriptions` 提交 `{ "name": "服务名称", "callbackUrl": "https://subscriber.example/hooks", "callbackToken": "可选的回调令牌" }`，成功返回 HTTP 202 和 `{id,status:"pending"}`。名称最长 100 个字符；回调地址最多 2048 个字符，只允许 HTTPS，开发环境可使用 localhost 的 HTTP，不允许 URL 用户信息或 fragment。回调令牌若提供，须为 16–512 个非空白字符，服务发送回调时放入 `Authorization: Bearer <callbackToken>`。申请接口不会发送测试请求，批准前管理员应确认申请方及回调地址可信。

管理员通过 `GET /admin/subscriptions` 查看申请、状态及分享配置（不返回回调令牌）。调用 `POST /admin/subscriptions/:id/approve` 时可提交 `{ "days": 7, "includeTranscript": false, "allowDownload": false }` 配置分享：有效期为 1–365 整数天，逐字稿和下载默认关闭；空请求体使用默认值。批准后可通过 `PATCH /admin/subscriptions/:id` 修改配置，只需提交要变化的字段，其余保持不变。修改只影响之后生成的链接，已经推送的链接权限和有效期不变。调用 `POST /admin/subscriptions/:id/reject` 或 `POST /admin/subscriptions/:id/revoke` 可拒绝或撤销。只有 pending 可批准或拒绝，只有 approved 可修改配置或撤销，重复或状态不符返回 404。`GET /admin/subscription-deliveries` 返回事件、会议、尝试次数、完成时间及安全错误码，不返回回调令牌或分享凭证。

每次新纪要发布时，服务为当时所有 approved 订阅各创建一个事件；不补发批准前的会议，重新转写完成会创建新事件。后台按该订阅当前配置生成分享链接，然后向回调地址发送 JSON POST：

```json
{
  "event": "meeting.minutes.completed",
  "eventId": "全局唯一事件 ID",
  "meetingId": "会议 ID",
  "shareUrl": "https://meetings.example/meeting-service/share#凭证",
  "shareExpiresAt": 1799999999,
  "downloadUrls": {
    "minutes": "https://meetings.example/meeting-service/share/download/minutes",
    "transcript": "https://meetings.example/meeting-service/share/download/transcript"
  }
}
```

仅当 `allowDownload` 为 true 时包含 `downloadUrls`，同时提供纪要和逐字稿两个下载地址。订阅方需从 `shareUrl` 的 fragment 取出分享令牌，并以 `Authorization: Bearer <token>` 请求下载地址；未启用 `includeTranscript` 时，逐字稿下载地址返回 404。两个下载地址遵循与分享链接相同的权限和有效期。

任意 2xx 均视为送达。非 2xx、网络错误或 10 秒超时会按 5 秒至 5 分钟的指数退避持续重试；回调不得依赖重定向。投递为至少一次语义，接收方必须以 `eventId` 去重，分享链接也应作为敏感凭证保护。撤销订阅或删除会议会取消尚未送达的事件；已经发出的链接和响应无法撤回，会议删除后链接的新访问失效。服务必须配置 `MEETING_PUBLIC_BASE_URL` 为包含 `/meeting-service` 的外部访问前缀，否则不会发送回调。

## 人工编辑纪要

`PUT /admin/meetings/:id/minutes` 需要管理员认证，JSON 请求含 `markdown`、非负整数 `revision` 和结果接口返回的 `minutesSourceHash`。首次保存使用结果的版本号；服务端在事务中校验版本与当前模型结果，冲突返回 409，成功返回新版本与安全预览标记。人工内容不会覆盖模型结构化结果，结果接口及 Markdown 下载优先返回人工版本；重新处理后若模型结果变化，结果接口的 `minutesEditedStale` 提示人工版本基于旧结果。会议删除会清理当前数据库中的编辑。

转录段可含 `startSeconds`、`endSeconds` 和 `timeSource: vad_estimate`，是 VAD 估计而非精确逐字对齐；旧结果可能没有时间字段。ASR 服务以 SSE 文本与 WebSocket 分段文字做交叉校验：少量差异时 `segments` 保存 SSE 转录结果，`alignmentWarning` 为 true，`timedAlternatives` 是可能不完整的流式估计分段；差异超过阈值、数字或符号不一致时失败重试。二者一致不等于证明没有共同遗漏。
