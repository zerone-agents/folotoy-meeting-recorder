# Meeting Recorder for FoloToy AI Passport

把 FoloToy AI Passport 用作会议录音卡：设备离线录音并在联网后上传，服务端转录、生成会议纪要，用户在网页中查看和编辑。

本仓库包含固件原型、会议服务、部署配置、调研与测试记录。项目仍处于原型阶段；请先阅读[已验证范围和限制](#当前状态与限制)，再决定是否用于实际会议。

## 工作方式

```text
AI Passport              会议服务                         浏览器
离线采音 → Opus 持久化 → Wi-Fi 分块上传 → 完整性确认 → ASR → 纪要 → 查看 / 编辑 / 下载
```

- **设备端**：录音保存后自动上传；断网时保留录音并重试。设备在服务端确认保存后才执行相应清理。初版连续录音上限为 30 分钟。
- **服务端**：使用 [agent-runtime](https://github.com/zerone-agents/agent-runtime) 承载会议路由，SQLite 保存音频块和处理状态；StepFun `stepaudio-2.5-asr` 负责转录，`step-3.7-flash` 默认生成纪要。
- **工作台**：按主题、时间或状态查找会议，查看转录和纪要，编辑 Markdown 纪要并下载结果。转录时间是估算，结果需要人工核对。

## 仓库结构

| 目录 | 内容 |
| --- | --- |
| [`firmware/`](firmware/) | 基于 AI Passport / soundcore 参考项目的 ESP32-C3 固件、板级支持、构建与设备测试工具。 |
| [`server/`](server/) | Node.js 会议服务、设备接口、转录和纪要工作器、Web 工作台及测试。 |
| [`deploy/`](deploy/) | Docker Compose、反向代理时间证明示例和容器验收脚本。 |
| [`docs/`](docs/) | 架构、部署、设计和实施计划；见[文档索引](docs/README.md)。 |
| [`research/`](research/) | 产品调研、模型测试和硬件实测记录；见[调研索引](research/README.md)。 |
| [`notes/`](notes/) | 项目历程和后续分享素材。 |

## 快速开始

### 运行服务

服务需要 Node.js **22.13.0 或更高版本**、FFmpeg/ffprobe、可用的 StepFun 凭据和独立的管理员及 Runtime 凭据。本机执行：

```bash
cd server
npm ci
npm test
```

启动前按[本机运行说明](server/OPERATIONS.md)设置必需的环境变量与 TLS，或使用[容器部署说明](deploy/README.md)通过反向代理提供 HTTPS。服务页面为 `/meeting-service/ui`，健康检查为 `/health`。运行真实模型验证会发送所选音频并可能产生费用。

### 固件与设备

目标设备是 ESP32-C3、8 MB Flash、无 PSRAM，构建基线为 ESP-IDF 5.5.3。设备编译、配网与写入前先阅读[固件说明](firmware/README.zh_CN.md)、[应用配置和构建说明](firmware/docs/development/engineering/build-and-test.zh_CN.md)及[设备实测记录](research/2026-11-03-agent-commissioning-upload-test.md)。刷写和分区变更可能影响已有数据；首次初始化录音分区及恢复策略见[自动上传设计](docs/superpowers/specs/2026-11-02-scr1-auto-upload-design.md)。

## 当前状态与限制

- 已在真实设备上验证过 30 分钟录音、自动上传和特定断网、关机恢复及上传抢占场景；具体测试条件和未覆盖范围见[设备实测记录](research/2026-11-03-agent-commissioning-upload-test.md)。一次测试不能证明所有断电、网络或硬件条件均可靠。
- v0.1.7 曾完成短录音设备直传、服务端转录和纪要生成；页面编辑、刷新及下载也在测试会议上验证。30 分钟模型覆盖测试主要使用带标记的合成样本，尚不足以证明真实多人会议准确率、语义质量或续航表现。详见 [ASR 与端到端测试](research/2026-10-30-stepfun-asr.md)。
- 录音、转录和纪要可能包含敏感信息。服务仍需安全审计；部署者应配置 HTTPS、访问控制、供应商数据政策、备份与保留策略。删除当前数据不等于自动抹除历史备份或供应商副本。
- 参考固件原有 README 描述的是上游 soundcore 玩法，不是本项目每项功能的当前保证。固件来源和许可证见 [NOTICE](NOTICE)。

## 文档与参与

从[文档索引](docs/README.md)、[会议纪要订阅使用指南](docs/subscriptions-guide.md)、[服务端接口](server/API.md)、[备份与恢复](server/BACKUP.md)和[项目历程](notes/2026-11-06-ai-passport-meeting-recorder-timeline.md)开始。欢迎提交问题与改进，约定见 [CONTRIBUTING.md](CONTRIBUTING.md)；安全问题请参见 [SECURITY.md](SECURITY.md)。

## 许可证

本项目自有代码与文档使用 [Apache License 2.0](LICENSE)。固件中的上游内容保留 [MIT 许可证](firmware/LICENSE)，Noto CJK 字体遵循 [SIL Open Font License 1.1](firmware/assets/fonts/OFL-Noto-CJK.txt)；其他第三方内容及依赖的条款见其各自声明和 [NOTICE](NOTICE)。FoloToy、soundcore、Anker 等名称和商标属于各自权利人，本项目不是其官方产品。
