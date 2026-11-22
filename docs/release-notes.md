# 0.2.0-beta.19

[首次安装：下载固定环境包](https://github.com/xueyuannnn/okww-dualpc/releases/tag/environment-win10-11-x64-67a1e8aae098) · [安装与更新说明](https://github.com/xueyuannnn/okww-dualpc/blob/v0.2.0-beta.19/docs/usage.md#软件更新)

- 优化 DXGI、Win32 远程画面的队伍数字识别，减少漏识别后把三人队误判为两人队的情况。
- 当前角色不明时先复核画面，切人时短暂丢失界面会等待恢复。
- 增加远程战斗特征短暂丢失后的缓冲，明确完成条件仍立即生效。
- Python 工坊复用宿主同一帧的队伍识别结果，减少两侧判断不同步。

采集卡仍使用原有队伍识别流程。本次针对远程模式的修改已做离线回归，实际游戏表现需继续验证。

首次安装将环境包和下方 `OK-WW-DualPC-update-win10-11-x64.zip` 内的 `OKWW-DualPC-full` 合并到同一目录。已有 beta.17 全量包可退出软件后，将增量包文件覆盖到旧 EXE 所在目录；更新前备份 `native-full-data`。以后可在“关于”中更新，无需逐版安装。

本版仍使用环境 `win10-11-x64-67a1e8aae098`。程序更新只需下方约 21 MB 的累计增量包；`OK-WW-DualPC-source.zip` 是开发源码，校验值见 `SHA256SUMS.txt`。


反馈：[lidada0202@gmail.com](mailto:lidada0202@gmail.com)
