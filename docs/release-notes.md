# 0.2.0-beta.21

[首次安装：下载固定环境包](https://github.com/xueyuannnn/okww-dualpc/releases/tag/environment-win10-11-x64-67a1e8aae098) · [安装与更新说明](https://github.com/xueyuannnn/okww-dualpc/blob/main/docs/usage.md#软件更新)

- 修复每日活跃度超过 100 后误报未完成，奖励仍领取到 100 点档位。
- 支持体力和备用体力以独立数字显示的页面。
- 调整远程模式的战斗结束判断，减少击杀后继续连招。
- 修复金色场景把无目标图标误判为锁定的问题，内置和 Python 工坊同步调整。
- 修复加载画面和首次进入 4C 时误触 Esc 的问题，内置任务与 Python 公共导航一并处理。

本次已做离线回归和保存画面的识别验证，远程客户端实战仍需验证。

首次安装将环境包和下方 `OK-WW-DualPC-update-win10-11-x64.zip` 内的 `OKWW-DualPC-full` 合并到同一目录。已有 beta.17 全量包可退出软件后，将增量包文件覆盖到旧 EXE 所在目录；更新前备份 `native-full-data`。以后可在“关于”中更新，无需逐版安装。

本版仍使用环境 `win10-11-x64-67a1e8aae098`。程序更新只需下方约 21 MB 的累计增量包；`OK-WW-DualPC-source.zip` 是开发源码，校验值见 `SHA256SUMS.txt`。


反馈：[lidada0202@gmail.com](mailto:lidada0202@gmail.com)
