# Windows 兼容维护

当前发布的 0.2.0-beta.16 完整包支持 Windows 10／11 x64。Windows 7 SP1 x64 为单独移植目标，尚未具备可发布的完整包。现有版本不能通过替换 `dxgi.dll` 获得 Win7 兼容性。

## 两套构建，共用业务逻辑

主版本继续使用当前运行库。Win7 版单独固定界面、推理和 Python 运行库，在平台接口层处理差异；每日任务、战斗状态、队伍识别和 KMBox 协议继续共用实现。

| 部分 | 当前状态 | Win7 适配内容 |
| --- | --- | --- |
| 界面 | Qt 6.8 | 选择支持 Win7 的 Qt 构建，适配 Qt 版本差异并检查全部二级界面 |
| Rust | `x86_64-pc-windows-msvc` | 单独构建 `x86_64-win7-windows-msvc` 标准库与内核，不混用主版本静态库 |
| 系统 API | 源码中的线程名称和 DPI 接口已改为按能力动态调用，缺失时使用系统 DPI | 继续检查主程序、插件和运行库的导入表，移除强制依赖的新系统接口 |
| 视频采集 | 主版本含 DXGI、Win32、DirectShow、Media Foundation | Win7 首版以 Win32 和驱动支持的 DirectShow 采集卡为目标，Media Foundation 需单独验证；不提供现有 DXGI 桌面复制模式 |
| 输入 | SendInput、KMBox Net | 验证坐标、按住／释放、网络回复、暂停和异常释放；不改变连招时序 |
| OCR 与模型推理 | OpenVINO 2026.4.1，PP-OCRv6 small | 选择可在 Win7 加载的推理后端，复用模型与字典配对规则，用同组截图回放核对结果 |
| Python 工坊 | Python 3.12；项目与 ok-script 要求 Python 3.11 以上 | 单独处理解释器、PySide、NumPy、OpenCV 和语法／标准库差异；不能只把解释器替换为 Python 3.8 |
| 更新 | 当前只有主版本完整包入口 | Win7 使用独立资产和更新筛选，禁止自动装入主版本运行库；个人数据迁移和回滚需验证 |

## 发布前验收

首次 Win7 版本必须在干净的 Win7 SP1 x64 环境完成验证，不能仅凭 Windows 11 上编译通过或静态扫描结果发布。

1. 完整解压后启动：主程序、Qt 插件、推理库和便携 Python 均能加载，不依赖开发机环境。
2. Win32 画面采集、分辨率缩放和预览；采集卡另用真实设备及对应 Win7 驱动验证。
3. 同一批每日页面、队伍头像、战斗血条和 OCR 样本的离线回放，对比现代版识别结果。
4. KMBox 与 SendInput 的输入、暂停、异常释放和重连。普通任务结束后，实时触发仍按原有生命周期工作。
5. 工坊联网刷新、缓存离线导入、内置／工坊切换、通用脚本与活动流程。新版 Python 语法不兼容时给出具体原因，不得标记为“已兼容”。
6. 定时任务、个人配置保存、更新下载、校验、替换、回滚及重新启动。

Win7 运行库固定后，普通任务修复仍从同一份业务代码构建两个版本。涉及依赖、采集、输入、模型或更新器的变更必须重新执行对应平台验收；缺少 Win7 验证的产物继续标为实验版，不替代可用的 Win7 正式包。

首个 Win7 完整包验收前，主版本继续按原流程发布，并保持当前系统要求；准备阶段的接口回退不等同于已经支持 Win7。

源码检查可运行 `python tools_release/audit_win7.py --check-optional-apis --output win7-audit.json`。该工具列出当前依赖、Python 3.8 语法和标准库审查项，并阻止重新引入清单中的新系统 API 直接调用；它不会检查全部传递依赖，也不能证明完整包可在 Win7 运行。

## 依据

- [Qt 6.8 的 Windows 支持范围](https://doc.qt.io/qt-6.8/windows.html)：从 Windows 10 1809 起。
- [Rust Win7 目标](https://doc.rust-lang.org/rustc/platform-support/win7-windows-msvc.html)：Tier 3，不提供预编译标准库，需自建工具链或标准库。
- [Python Windows 支持](https://docs.python.org/3.12/using/windows.html)：官方 Win7 路线停留在 Python 3.8，当前工坊环境不能直接照搬。
- [DXGI 桌面复制](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgioutput1-duplicateoutput)：Win7 平台更新上的 `DuplicateOutput` 返回 `E_NOTIMPL`。
- [线程名称接口](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setthreaddescription)：`SetThreadDescription` 从 Windows 10 1607 起提供，需保留可选调用路径。
