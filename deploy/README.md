# 单机容器部署

镜像支持 HTTP 模式和命名 volume 初始化。2026-11 的测试部署已用于有限设备联调；新的部署环境仍需自行验收。服务是单实例 SQLite 应用，不支持多个副本或共享网络文件系统。生产使用前须完成设备联调、安全审计、证书管理及备份策略。

1. Compose 使用 Docker 管理的 `meeting_data` 命名 volume 保存数据库、录音块、WAL 和任务。新 volume 初始化时从镜像复制非 root 用户所有的 0700 目录；已有 volume 必须保证目录由容器内 node 用户拥有且权限为 0700。HTTPS 由反向代理终止。
2. 启动前通过环境变量提供 `MEETING_ADMIN_TOKEN`（至少 7 个非空白字符）、`ZERONE_AGENT_HTTP_API_KEY`（至少 32 个非空白字符）和 `STEPFUN_API_KEY`；管理员令牌与 Runtime 密钥须不同。Compose 通过 `${变量名}` 引用它们；不要将密钥提交到 Git，Docker 管理员仍可查看容器环境变量。需要订阅推送时，另将 `MEETING_PUBLIC_BASE_URL` 设为外部可访问的完整服务前缀（含 `/meeting-service`）；留空会停用回调发送。
3. 默认使用 Docker Hub 的 `zeroneai/meeting-recorder:latest`和 Docker 命名 volume。无需手动创建数据目录。该镜像须包含 HTTP 模式；旧版 v0.1.0 不支持。执行 `docker compose config -q`，再执行 `docker compose up -d`。默认仅监听本机 3443；反向代理通过 `http://127.0.0.1:3443` 转发 `/meeting-service/`，对外提供有效 HTTPS 证书。保持本机绑定，不要将明文服务直接暴露到网络。
4. 通过反向代理，使用受信任 CA 和匹配的主机名访问服务根路径 `/`，服务会跳转到 `/meeting-service/ui`；管理员状态接口需要令牌。停止用 `docker compose stop`，等待任务排水完成。

不要使用 `docker compose down -v`，它会删除持久数据卷。命名 volume 不是备份，须将数据库一致性快照及最新安全状态保存到独立位置。升级前执行 [备份与恢复](../server/BACKUP.md)：保全完整数据库和最新安全状态。确认新版兼容矩阵、镜像摘要以及备份可恢复；停止旧实例后升级。仅在证明旧版能读迁移后数据库时允许直接切回旧摘要，否则离线执行恢复工具；缺失最新安全状态或已确认数据时拒绝上线。备份频率、异地复制和灾难恢复目标须由部署方确定，工具本身不提供这些能力。

发布流程与 agent-hub 一致：推送 `v*` Git 标签后运行测试并构建，向配置的 Docker Hub 和 SWR 推送版本标签；稳定版本还更新 `latest`，预发布版本不更新 `latest`。配置变量 `REGISTRY_HOST`、`REGISTRY_IMAGE`、`DOCKERHUB_IMAGE` 和对应 secrets `DOCKER_REGISTRY_USER`、`DOCKER_REGISTRY_PASSWORD`、`DOCKERHUB_USERNAME`、`DOCKERHUB_TOKEN`。与 agent-hub 相同，该流程不检查重复版本，仓库没有配置不可变规则时已有标签可能被覆盖；生产部署应固定镜像 digest。

2026-11-02 本机容器检查：使用此前构建的测试镜像和临时卷，分别运行并销毁容器；合成的已确认字节与待上传块在新容器中可读取。容器内备份和恢复工具成功运行，恢复库保留上述合成数据。此测试未启动 HTTPS 服务，未验证真实 Opus 解码、删除/撤销交错、跨版本迁移或断电；临时卷已清理。

Docker Hub 文档提供不可变标签设置；华为 SWR 官方文档中的不可变规则适用于企业版。尚未核实目标仓库类型、实际规则和发布凭据；不能假设基础版具备此能力，发布流程与 agent-hub 一致，不提供重复版本保护。

## 设备启动时的签名时间

从 v0.1.3 开始包含时间签名器；已有部署完成过有限线上设备校时实测（见[记录](../research/2026-11-03-agent-commissioning-upload-test.md)）。新部署仍须初始化签名器、配置代理并验证链路，升级镜像本身不代表自动校时已可用。

[nginx-time-proof.conf](nginx-time-proof.conf) 是独立的 HTTP 虚拟主机示例：将 `server_name` 改为设备配置的 HTTPS 服务域名，后端地址按实际部署修改。它仅允许不带查询参数的时间证明 GET，其他 HTTP 请求返回 404；管理员接口、设备上传和会议查询仍通过已有的 HTTPS 虚拟主机。设备只在此路径使用明文 HTTP，证明必须经已绑定公钥验证；设备令牌不应通过 HTTP 发送。不要把该示例简单叠加到已有的同名 80 端口虚拟主机上，需先合并配置并检查实际路由、监听和重定向顺序。

新服务部署后，管理员须通过 HTTPS 初始化并查询时间签名器，绑定与设备配置完全相同的 origin；设备需要 Agent 通过受信任的管理连接和设备专用维护固件制备对应公钥。签名私钥和时间下界存于服务数据库，备份与恢复须遵守 [BACKUP.md](../server/BACKUP.md)。设备还需要能够访问该域名的 80 端口；若运营商或代理强制重定向，校时会失败，录音保留在设备上。

示例已使用官方 `nginx:1.27-alpine` 临时容器和模拟后端验证：只有时间证明路径被转发，认证头不会透传；其他路径、查询参数和非 GET 请求被拒绝。另外以真实服务端签名路由和设备同款验签代码完成了代理链路联测。这不是线上反向代理验证，也不证明设备能够访问该端口。复现代理测试需预先取得该镜像并运行 `python3 deploy/tests/test_nginx_time_proxy.py`；激活 ESP-IDF 后，在 `firmware/` 下设置 `MEETING_TEST_PROXY=1` 并运行 `bash tools/test_time_signature_interop.sh` 可复现链路联测，两者均不需要构建镜像。
