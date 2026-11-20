# 本机开发运行

当前服务仍需安全审计。设备端已有有限实测，详见 [设备实测记录](../research/2026-11-03-agent-commissioning-upload-test.md)。以下是可信本机的 HTTPS 开发步骤；反向代理和 Compose 模式见 [部署说明](../deploy/README.md)。

## 准备

1. 安装 Node.js 22.13 或更高版本，在 `server/` 运行 `npm ci` 和 `npm test`。HTTPS 进程测试需要允许本机回环地址监听；音频解码测试需要 `ffmpeg`、`ffprobe`，证书测试需要 `openssl`。
2. 准备当前用户独占访问的数据目录（权限 `0700`）以及私有的 TLS 私钥文件（权限 `0600`）。设置 `MEETING_DATA_DIR`、`MEETING_TLS_CERT` 和 `MEETING_TLS_KEY` 为绝对路径。服务不会自动放宽现有目录权限。
3. 证书必须在有效期内，与私钥匹配，并包含客户端实际访问的主机名。浏览器、设备或 `curl` 必须信任证书链并校验主机名；本机测试用证书只能证明本机测试，设备联网还需确定可达地址、证书信任及首次校时方案。
4. 生成并妥善保管独立的随机管理员令牌，设置为 `MEETING_ADMIN_TOKEN`；设置另一个不同的随机长值为 `ZERONE_AGENT_HTTP_API_KEY`，该值仅用于服务进程内 Runtime 接口。管理员令牌至少 7 个字符，Runtime 密钥至少 32 个字符，均不含空白。`MEETING_ASR_API_KEY` 和 `MEETING_MINUTES_API_KEY` 由安全的本机凭据渠道注入；不要把任何令牌写入仓库或命令历史。
5. 可选设置 `MEETING_PORT`（1–65535，默认 3443）、`MEETING_HOST`（默认 `127.0.0.1`）、`MEETING_WORKER_CONCURRENCY`（1–4，默认 2）、`MEETING_STORAGE_LIMIT_BYTES`（正整数，逻辑音频字节上限）和 `MEETING_RETENTION_DAYS`（正整数；不设置则不自动删除）。配额不包含 SQLite 索引、WAL 和备份。改变监听地址前须确认网络防护、TLS 和设备信任，公网暴露还需要边界限流。

## 运行与检查

在已安全注入上述环境变量的 shell 中进入 `server/`，执行 `npm start`。出现 `meeting service listening` 后，使用信任的证书和匹配的主机名访问 `https://localhost:3443/meeting-service/ui`；输入管理员令牌并查看会议。管理员接口及设备上传协议见 [API.md](API.md)。

`GET /meeting-service/admin/status` 提供各阶段数量和过期租约数；`GET /meeting-service/admin/deletions` 显示待清理墓碑；`GET /meeting-service/admin/settings` 显示保留、存储和工作器设置。这些接口均需管理员令牌，不应把令牌写入命令行参数、日志或工单。统计不证明模型服务可用；服务调用 StepFun 可能产生费用。

停止时向进程发送 SIGTERM 或在前台按 Ctrl-C，等待进程退出。服务会停止领取任务、尝试取消模型请求并释放租约；外部供应商仍可能完成请求并计费。设备遇到连接中断时必须查询并核对完整清单后才能删除副本。

## 备份、删除和故障

使用 [BACKUP.md](BACKUP.md) 的一致快照与离线恢复流程；保管最新安全状态，勿覆盖唯一副本。删除只清理当前服务数据库中的逻辑内容，历史备份、SQLite WAL 和供应商副本有各自的处理边界，见 [PROVIDER-DATA.md](PROVIDER-DATA.md)。设置保留期限和备份策略前须明确业务要求及供应商的账户级条款。

如果任务失败，先通过管理员结果接口查看安全错误码和失败阶段；认证、权限、限流、音频损坏和证据校验失败需要分别排查。排障时请勿把录音、逐字稿或密钥附到工单。设备配网和短录音端到端已有有限实测；其他凭据撤销与故障条件的范围见 [设备实测记录](../research/2026-11-03-agent-commissioning-upload-test.md)和历史 [验收追踪](ACCEPTANCE.md)。

### 纪要 Runtime 失败日志

纪要 Runtime 请求或结果校验失败时，服务默认在会议数据库保存最新一次失败的请求方法、路径、完整正文以及响应状态和正文。管理员可通过 `GET /meeting-service/admin/meetings/:id/minutes-failure` 查询。数据包含逐字稿和模型输出，授权与会议详情一致，认证请求头不进入记录。没有收到响应时 `response` 为 `null`。下一次失败覆盖旧诊断，成功、重新转录或删除会清理记录。历史备份按会议数据的备份规则处理。
