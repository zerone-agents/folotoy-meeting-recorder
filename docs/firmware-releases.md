# 固件发布与下载

固件以 `firmware-v<主版本>.<次版本>.<修订号>` 命名，例如 `firmware-v0.1.0`。固件内嵌版本为 `0.1.0`。服务端容器继续使用 `v*` 标签发布。

## 下载与校验

在 [GitHub Releases](https://github.com/zerone-agents/folotoy-meeting-recorder/releases) 打开所需的 `Meeting Recorder Firmware vX.Y.Z` 版本，下载同一发行版的附件。

- `meeting-recorder-firmware-vX.Y.Z-full.bin` 是完整烧录镜像。
- `SHA256SUMS` 用于校验下载文件。在下载目录运行 `shasum -a 256 -c SHA256SUMS`。
- `firmware-release.json` 记录固件版本、源码提交、构建工具版本、下载文件哈希和烧录起始地址。

完整镜像从 `0x0` 烧录，会覆盖配置所在的区域。保留录音和配置时，应先备份所需数据，并按已验证的分段写入流程操作。当前固件需要预先制备的兼容录音存储。自动上传还需要自建会议服务和设备首次制备。 [烧录与数据说明](../firmware/docs/development/engineering/firmware-layout.zh_CN.md#烧录与已存数据) · [会议服务部署](../deploy/README.md)

## 发布新版本

对将要发布的源码提交创建新的固件标签并推送。例如从已确认的提交发布 `0.1.0`。

```sh
git tag -a firmware-v0.1.0 <源码提交> -m 'Meeting Recorder Firmware v0.1.0'
git push origin refs/tags/firmware-v0.1.0
```

工作流使用 ESP-IDF 5.5.3 和仓库中的会议录音构建配置，校验完整镜像和内嵌版本，再将三个附件发布到 GitHub Release。涉及固件或发布工作流的 PR 会先以 `0.0.0` 做构建检查，产物保留 7 天，正式固件通过 Release 留存。 [固件发布工作流](../.github/workflows/firmware-release.yml)

后续版本使用新的标签递增发布。重跑工作流时，GitHub CLI 会拒绝覆盖同标签已发布的 Release。如果上传中断并留下草稿，先检查草稿及附件，再决定是否继续该次发布。
