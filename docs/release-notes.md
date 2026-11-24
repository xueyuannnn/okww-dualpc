# 0.2.0-beta.28

[首次安装：下载固定环境包](https://github.com/xueyuannnn/okww-dualpc/releases/tag/environment-win10-11-x64-67a1e8aae098) · [安装与更新说明](https://github.com/xueyuannnn/okww-dualpc/blob/main/docs/usage.md#软件更新)

- 修复选择本机／远程画面后，Python 工坊因配置字段不兼容而无法启动的问题。
- 采集方式合并为采集卡、DXGI 单机模式、DXGI 远程模式，移除自动判断与启动选择弹窗。
- DXGI 可选择显示器或可见窗口客户区采集，输入坐标使用相同的窗口范围。
- 修复停止普通任务后，实时任务复用 OCR 请求时出现识别异常或张量尺寸冲突的问题。
- 包含每日页面确认、索拉指南选行、“直接挑战”按钮识别与中文目录修复。
- 指南滚动后等待列表稳定，避免反复复位导致回弹、选行失败。

停止普通任务后继续执行已启用的实时任务；双机设置的停止负责结束线程和设备连接。

首次安装将环境包和下方 `OK-WW-DualPC-update-win10-11-x64.zip` 内的 `OKWW-DualPC-full` 合并到同一目录。已有 beta.17 全量包可退出软件后，将增量包文件覆盖到旧 EXE 所在目录；更新前备份 `native-full-data`。以后可在“关于”中更新，无需逐版安装。

本版仍使用环境 `win10-11-x64-67a1e8aae098`。程序更新只需下方约 21 MB 的累计增量包；`OK-WW-DualPC-source.zip` 是开发源码，校验值见 `SHA256SUMS.txt`。


反馈：[lidada0202@gmail.com](mailto:lidada0202@gmail.com)
