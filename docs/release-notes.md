# 0.2.0-beta.18

[首次安装：下载固定环境包](https://github.com/xueyuannnn/okww-dualpc/releases/tag/environment-win10-11-x64-67a1e8aae098) · [安装与更新说明](https://github.com/xueyuannnn/okww-dualpc/blob/v0.2.0-beta.18/docs/usage.md#软件更新)

- 下载改为固定环境包＋版本累计增量包，后续无需逐版更新。
- 增量更新校验环境并保留个人配置，程序和脚本按目标版本完整替换。
- 程序包不再重复携带运行库、模型和完整源码，源码另行提供。
- 修复每日领取确认受 OCR 抖动干扰的问题，等待远程反馈时不重复点击按钮。
- 加强远程画面的每日页面识别，小字和数字识别失败时局部复核，领取结果需确认后再继续。

首次安装将环境包和下方 `OK-WW-DualPC-update-win10-11-x64.zip` 内的 `OKWW-DualPC-full` 合并到同一目录。已有 beta.17 全量包可退出软件后，将增量包文件覆盖到旧 EXE 所在目录；更新前备份 `native-full-data`。以后可在“关于”中更新，无需逐版安装。

本版仍使用环境 `win10-11-x64-67a1e8aae098`。程序更新只需下方约 21 MB 的累计增量包；`OK-WW-DualPC-source.zip` 是开发源码，校验值见 `SHA256SUMS.txt`。

本次已完成离线回归，远程游戏实战仍需继续验证。

反馈：[lidada0202@gmail.com](mailto:lidada0202@gmail.com)
