# ajni 缺陷分类与修复计划

## 范围

mbt（MoonBit）1.0 正式发布前，ajni 以修复现有行为的 bug、纠正构建问题和补齐文档为主，不正式扩充功能。

本计划保留 core、Android、可选 WebView 的现有分包和支持范围。通用 Java 调用/字段/构造器 API、公开对象引用体系、绑定生成器、数组/DirectBuffer API、native peer、桌面 JVM invocation、新 ABI 和更多 WebView 控件均不进入当前修复清单。1.0 发布后再评估，不视为自动承诺。

Android/WebView 动态测试暂缓；当前仅做源码审查、类型/语法/格式检查，以及工具链齐备时的编译和 lint。不得把 host stub 的成功返回、静态检查或 APK 构建当成 ART/WebView 运行验证。修复完成但没有设备验证时，分别记录“静态检查通过”和“动态验证暂缓”。

以下 17 项已在 `0.2.5` 实现。验收限于源码审查和静态编译；Android/WebView 动态验证仍暂缓。

## 0.2.5 修复状态

| 条目 | 已实现的约束 | 剩余验证 |
| --- | --- | --- |
| JNI-01 | `FindClass` 取得有效异常类；保留已有 pending exception，清理 local ref | checked JVM/ART 失败支路暂缓 |
| JNI-02 | MoonBit 调出独立检查异常并转成错误；Java 调入保留异常回抛，不在 pending 下继续工作 | checked JNI 动态场景暂缓 |
| JNI-03 | native 成功后发布 Kotlin ready；既有错误区分未初始化、运行期结束、Java 和 native 失败 | Android 初始化/调度失败暂缓 |
| TEXT-01 | 按 UTF-16 code unit 的 MUTF-8 权重累计；class 名最多 65,533 字节，为 `L`/`;` 留空间；native 名最多 65,535；数组/方法聚合校验 | 边界回归和属性测试已编译，未执行 |
| TEXT-02 | 保留 static 的 255 显式槽；说明 instance/interface 的隐式 `this` 和声明层边界 | 当前注册表与编译后 static descriptor 已核对 |
| TEXT-03 | 非法 UTF-8 每个未消费字节替换为 U+FFFD；拒绝 overlong、代理项、越界和截断作为合法码点 | 转换动态场景暂缓 |
| HOST-01 | 主线程初始化/关闭；运行期、容器和视图身份隔离旧任务；瞬态 create token 保留排队 CREATE→命令顺序 | Android 队列、重入和重建暂缓 |
| HOST-02 | 宿主保存 pause 状态，新视图继承 | pause→create 仪器回归已编译，未执行 |
| WEB-01 | 分配前验证 initial URL/base URL；配置/注册/parent/load 失败进入统一回滚 | WebView 分配与 load 失败暂缓 |
| WEB-02 | wrong-handle 不消费请求；future 完成先于唯一 remove；关闭/销毁覆盖 pending | timeout=0、错误响应和销毁竞态暂缓 |
| WEB-03 | 只允许 `100..299`/`400..599`；非法响应 400、超限 413、真正超时 504、打断 503、其他失败 500 | WebResourceResponse 动态场景暂缓 |
| WEB-04 | renderer gone 复用 state 清理并发既有 FAILED/DESTROYED；无重试 | API 26+ renderer 退出暂缓 |
| BUILD-01 | host 分发精确 JNI consumer rules；实际 Kotlin bytecode descriptor 与 C/规则一致 | R8 混淆消费者 lookup 暂缓 |
| BUILD-02 | Gradle 8.10.2 官方 Wrapper、ZIP SHA256；CI 显式安装并核验 `.moon-version` | Wrapper 已运行；CI 全链仍待执行 |
| BUILD-03 | 各 ABI/config 独立 target-dir；跟踪源码、配置、C/header、依赖和工具链输入及集合变化 | CMake 配置和生成 target 已执行；NDK 双 ABI 增量暂缓 |
| BUILD-04 | 每个 native command 后检查退出码；两种 Gradle 分支同 cwd、finally 恢复 | PowerShell 故障注入未执行 |
| VERIFY-01 | 非 Android ready=false，操作映射现有 NativeFailure；CI 编译测试但不运行 | 未以 host 返回值声称平台验证 |

## 证据口径

评估基线是提交 `81c328e29218afa16b8061730c8be2b176dbdc76` 所对应的本地源码。工具链为 MoonBit `0.1.20260920` / `moonc 0.10.14`。

- **已复现**：前一轮调查已执行对应路径，记录结果；这些历史验证不属于当前静态检查，也不代表 Android 上已经验证。
- **源码确认**：调用、顺序或配置可直接从源码确认；未声称运行结果。
- **[INFERENCE]**：根据源码推导的故障表现，仍待动态验证。
- **静态验收**：当前可审查的不变量、调用契约及编译结果；不能替代动态验收。

前一轮调查结果：

| 验证 | 观察结果 | 覆盖限制 |
| --- | --- | --- |
| `moon check --target native` | 成功 | 不编译 Android 条件分支 |
| `moon test --target native` | 35/35 通过 | Android/WebView stub 在非 Android 环境直接返回成功 |
| native release build/run | 成功 | `android_runtime` 主入口为空，不证明 Android 行为 |
| 临时真实 JDK JVM + `-Xcheck:jni` | 复现错误 `ThrowNew` 引用导致 JVM 终止、回调异常残留、overlong UTF-8 被接受 | 直接调用 C 函数；无 ART、Surface 或 WebView 执行 |
| MoonBit 描述符临时运行 | 接受 UTF-16 长度 22,005、UTF-8/MUTF-8 长度 66,005 的描述符 | 说明长度校验缺口；未执行 classfile 加载 |

临时验证文件已删除。本文不是可重放测试报告；修复时需要另外记录静态检查和剩余验证项。

## 修复分类

P0：错误支路可终止进程，应首先修复。P1：现有接口的失败处理、状态或资源正确性。P2：边界契约和构建可靠性。证据强度与优先级分开，不把静态风险写成已复现 bug。

### A. JNI 调用与错误传播

#### JNI-01 / P0：`ThrowNew` 接收了类名字符串

- 位置：[ajni_bridge.c](src/android/ajni_bridge.c)，`ajni_native_initialize`、`ajni_native_start_worker`。
- 已复现：第二参数是 `char *` 类名，而 JNI 要求 `jclass`。真实 JDK 头给出类型诊断；worker 启动失败支路在 checked JVM 中以 `Bad global or local ref passed to JNI` 终止。
- 修复范围：取得有效异常类引用，检查查类失败；只使用有效 `jclass` 抛出异常，并清理临时引用。不引入新的异常框架。
- 静态验收：两个错误支路均满足 JNI prototype；真实 JNI 头下不再有该参数类型错误；异常类查找失败时不会继续 `ThrowNew`。

#### JNI-02 / P1：短路表达式跳过异常检查

- 位置：[ajni_bridge.c](src/android/ajni_bridge.c)，`ajni_enqueue_ui_callback`、初始化和 lease 失败路径；[ajni_webview_bridge.c](src/webview/ajni_webview_bridge.c)，注册、command、bounds 调用。
- 已复现：`ajni_enqueue_ui_callback` 调用抛异常的 Java 方法后，`queued == JNI_TRUE && ajni_check_exception(...)` 跳过右侧，留下 pending exception。其他 `NULL || ...`、`false && ...` 位置属于源码确认，需逐一检查。
- 修复范围：将异常处理与返回值判断分开；明确 Java 调入时回抛、MoonBit 调出时转换失败的责任。保留 pending exception 是否正确取决于边界，不能一律 clear。
- 静态验收：可能抛异常的操作都有独立失败处理；失败后不继续执行禁止在 pending exception 下调用的 JNI 操作；清理不吞掉无关的外层异常。

#### JNI-03 / P1：初始化和调度失败不能准确传给调用方

- 位置：[ajni_bridge.c](src/android/ajni_bridge.c)，`ajni_check_exception`、`ajni_native_initialize`；[NativeBridge.kt](android/host/src/main/java/dev/nanaloveyuki/ajni/host/NativeBridge.kt)，`initialize`；[Android runtime](src/android/runtime.mbt)，`post_to_ui`、`start_worker`；[WebView runtime](src/webview/runtime.mbt)，`command`、`set_bounds`。
- 源码确认：helper 只 describe/clear + bool。部分初始化失败可在清除异常后正常返回，Kotlin 的 `initialized` 已先设为 true；MoonBit 调度失败通常统一变成 `NotInitialized`。`JavaException`、`RuntimeDestroyed` 已定义，但未接入实际失败传播。
- 修复范围：失败初始化不得发布 ready；利用现有错误类型传递实际失败原因，区分未初始化、Java 异常和 native 操作失败。不增加通用 Throwable API。
- 静态验收：初始化状态只在 native 成功后成立；每个失败出口都有明确上报路径；不能以日志替代可观察失败。

### B. 字符串与描述符校验

#### TEXT-01 / P1：描述符上限按 UTF-16 长度计算

- 位置：[jni_descriptor.mbt](src/jni_descriptor.mbt)，`JniMethod::new`、`JniClass::parse`、`NativeMethod::new`。
- 已复现：22,000 个“中”作为类名生成的描述符有 66,005 个编码字节，但 `descriptor.length()` 为 22,005，校验接受它。
- 修复范围：按 classfile 的 Modified UTF-8 字节数检查 65,535 上限，并核对 class/method name 的同类限制。普通文本 UTF-8 与 JNI/classfile MUTF-8 不混用。
- 静态验收：ASCII、NUL、BMP 非 ASCII、补充字符的计数规则符合 MUTF-8；边界按字节而不是 MoonBit code unit 判定。不新增字符串公共 API。

#### TEXT-02 / P2：参数槽校验缺少实例调用上下文

- 位置：[jni_descriptor.mbt](src/jni_descriptor.mbt)，`JniMethod::new`、`NativeMethod`。
- 源码确认：显式参数允许 255 槽，但 JVM 的实例/接口调用上限包含隐式 `this`；同一个 descriptor 本身不区分 static/instance。
- 修复范围：核对现有注册方法的调用形态，并准确说明声明层只验证 descriptor 形状。所有现有 Kotlin native 入口是 static，不能把 255 显式槽一律改成 254，也不为未来实例调用新增 API。
- 静态验收：static/instance 限制没有混淆；当前文档不声称 descriptor 验证证明了函数指针 ABI 或所有调用形态合法。

#### TEXT-03 / P2：C 解码器接受 overlong UTF-8

- 位置：[ajni_bridge.c](src/android/ajni_bridge.c)，`ajni_string_from_utf8`。
- 已复现：`C0 AF` 和 `E0 80 AF` 均被解码成 U+002F。
- 修复范围：补齐最短编码、代理项、最大码点与截断检查；沿用明确的非法输入替换/拒绝策略，保持合法中文、NUL、emoji 的转换行为。
- 静态验收：非法编码不会被当成其伪装的 ASCII/Unicode 字符；检查所有分支的输入推进、输出容量和分配溢出条件。未发现可利用漏洞，不标为已确认安全漏洞。

### C. Android 生命周期和 WebView 资源终结

本类仅进行静态检查。下列故障表现均为 [INFERENCE]，Android/WebView 动态验证暂缓。

#### HOST-01 / P1：旧任务可跨 shutdown/reinitialize 或容器替换执行

- 位置：[NativeBridge.kt](android/host/src/main/java/dev/nanaloveyuki/ajni/host/NativeBridge.kt)，初始化、关闭、排队回调；[AjniWebViewHost.kt](android/host/src/main/java/dev/nanaloveyuki/ajni/host/AjniWebViewHost.kt)，`attach`、`command`、`setBounds`、EVAL 回调。
- 源码确认：任务只检查当前布尔值/容器；initialize/shutdown 没有统一线程约束；EVAL 结果缺少视图身份核对。
- 风险：旧任务在新运行期看到 true，或重用 handle 后把迟到结果送给新视图；并发初始化/关闭可能使 Kotlin/native 状态不一致。
- 修复范围：统一现有入口的线程与状态契约，为异步任务核对所属运行期/容器/视图身份。身份字段属于内部 bug 修复，不增加异步 API。
- 静态验收：成功初始化前不对外 ready；shutdown、容器替换、视图销毁使旧任务失效；同 handle 的新对象不能接收旧结果。

#### HOST-02 / P1：暂停后创建的 WebView 不继承暂停态

- 位置：[AjniWebViewHost.kt](android/host/src/main/java/dev/nanaloveyuki/ajni/host/AjniWebViewHost.kt)，`ViewState`、`pause`、创建路径。
- 源码确认：pause 只遍历现有视图，新 state 固定 `paused=false`。
- 修复范围：记录宿主现有暂停态，创建时应用；不扩充生命周期种类。
- 静态验收：pause→create 与 create→pause 的目标状态一致，resume/destroy 的状态转换成对。

#### WEB-01 / P1：创建失败回滚不完整

- 位置：[AjniWebViewHost.kt](android/host/src/main/java/dev/nanaloveyuki/ajni/host/AjniWebViewHost.kt)，`createWithOptions`、`navigate`、`loadHtml`。
- 源码确认：先注册到 Map、addView、发 CREATED，再验证/执行初始导航；catch 只移除 scripts 并 destroy view，listener 配置还在 try 外。
- 风险：非法 initial URL/base URL 留下已销毁的注册视图和子节点，占用 handle。
- 修复范围：发布成功前验证初始参数；从首次资源分配开始覆盖完整清理，不改变 create 的功能范围。
- 静态验收：每个失败出口均清理 Map、parent、listener、scripts 与相关 pending；失败 create 不留下可被后续命令使用的 state。

#### WEB-02 / P1：错误资产响应丢失未完成请求

- 位置：[AjniWebViewHost.kt](android/host/src/main/java/dev/nanaloveyuki/ajni/host/AjniWebViewHost.kt)，`respondAsset`、`DynamicAssetPathHandler.handle`、`completePendingAssets`。
- 源码确认：先 remove pending，再核对 handle、解码和验证响应；异常时 future 没有完成。timeout=0 使用无限等待，detach/destroy 只能终结仍在 Map 的请求。
- 风险：错误 handle 或 malformed response 让资源线程等待到超时，甚至无法被销毁唤醒。
- 修复范围：校验、归属检查和终结顺序一致；失败时保留可完成请求，或显式完成错误响应；不新增重试机制。
- 静态验收：每个移除 pending 的分支均完成其 future 或已有完成证明；wrong-handle 不移除别人的请求；detach/destroy 能覆盖全部未完成请求。

#### WEB-03 / P1：接受平台不支持的 3xx 状态并误报超时

- 位置：[AjniWebViewHost.kt](android/host/src/main/java/dev/nanaloveyuki/ajni/host/AjniWebViewHost.kt)，`respondAsset`、`toWebResponse`、资源线程 catch。
- 源码确认：接受 `100..599`；Android `WebResourceResponse` 仅接受 `[100,299]` 或 `[400,599]`。构造失败会落入统一 504 timeout 分支。
- 修复范围：按平台可表示范围校验；区别真正 timeout 和非法响应/构造失败，不增加重定向支持。
- 静态验收：不把 3xx 传给响应构造器；非 timeout 错误不标为 timeout；失败仍满足 WEB-02 的终结约束。

#### WEB-04 / P2：renderer 退出未进入现有销毁路径

- 位置：[AjniWebViewHost.kt](android/host/src/main/java/dev/nanaloveyuki/ajni/host/AjniWebViewHost.kt)，`configureClients`。
- 源码确认：未处理 `onRenderProcessGone`。API 26+ 默认处理可能使宿主随 renderer 故障退出。
- 修复范围：在支持的 API 上清理受影响视图、终结 pending，并使用现有失败/销毁事件上报；不加入自动恢复或重建策略。
- 静态验收：API 24 兼容性保留；退出处理与普通 destroy 的资源清理一致；无隐式重试。

### D. 构建、发布消费和验证误判

#### BUILD-01 / P1：native 可达 Kotlin 成员没有随库保留规则

- 位置：[host Gradle](android/host/build.gradle.kts)、[NativeBridge.kt](android/host/src/main/java/dev/nanaloveyuki/ajni/host/NativeBridge.kt)、两个 C stub 的字符串查找/注册表、[app Gradle](android/app/build.gradle.kts)。
- 源码确认：无 consumer keep rules；C 硬编码类名和 upcall 方法名；demo release 关闭 minify。
- [INFERENCE]：R8 消费者中纯 native 可达的方法可能被改名或删除；未运行混淆产物，不能声称已经失败。
- 修复范围：随现有 host library 分发准确的 JNI consumer rules，不新增 AAR/Maven 发布功能。
- 静态验收：规则覆盖真实字符串引用，限制到必需成员；配置/编译可检查，混淆后 runtime lookup 仍标为待验证。

#### BUILD-02 / P2：本地与 CI 工具链不完全可重复

- 位置：[.moon-version](.moon-version)、[workflow](.github/workflows/build.yml)、Android 构建入口。
- 源码确认：仓库没有 Gradle Wrapper；CI 固定 Gradle 8.10.2，但 MoonBit installer 没显式接收项目版本。installer 默认安装 latest；不能仅凭 `.moon-version` 存在认定安装已钉版。
- 修复范围：固定并校验现有 Gradle/MoonBit 版本，不升级 API/ABI 或新增构建平台。
- 静态验收：CI 安装命令与项目版本一致；如补 Wrapper，版本/校验值一致；本地调用无需猜测系统 Gradle 版本。

#### BUILD-03 / P2：MoonBit→C 增量依赖不完整

- 位置：[CMakeLists.txt](android/app/src/main/cpp/CMakeLists.txt)，`add_custom_command DEPENDS`。
- 源码确认：手写依赖缺 `src/strings.mbt`、部分 package/module 配置等输入。
- [INFERENCE]：只改未声明输入的 Gradle 增量构建可能不触发 moon build，继续使用旧生成 C。
- 修复范围：补齐或可靠跟踪现有编译输入，不另建构建系统。
- 静态验收：源码、导入/链接配置及 native header 变化均能触发生成；两个 ABI 使用同一生成输出的调度不会互相破坏。

#### BUILD-04 / P2：PowerShell 未显式传播 native exit code

- 位置：[build-android.ps1](scripts/build-android.ps1)，moon/gradle 调用及 Wrapper 分支。
- 源码确认：`ErrorActionPreference=Stop` 下没有 `$LASTEXITCODE` 检查；Wrapper 分支也未切到 Android 根目录。
- [INFERENCE]：失败后可能继续消费旧生成物，或不同启动目录影响 Wrapper 构建。
- 修复范围：失败立即停止并向调用方传递非零退出；两种 Gradle 入口采用相同工作目录。
- 静态验收：每个 native command 的 exit code 被检查；`finally` 仍恢复目录；无失败后继续的路径。

#### VERIFY-01 / P1：非 Android stub 的成功不等于运行成功

- 位置：[ajni_bridge.c](src/android/ajni_bridge.c)、[ajni_webview_bridge.c](src/webview/ajni_webview_bridge.c) 的非 Android 分支；host tests、[仪器测试](android/app/src/androidTest/java/dev/nanaloveyuki/ajni/demo/NativeBridgeInstrumentedTest.kt)、[workflow](.github/workflows/build.yml)。
- 源码确认：非 Android runtime/post/start/WebView 命令返回 1；CI 只 assembleDebug，未执行仪器测试。WebView 仪器测试的 observer 在 JNI 前触发，不能证明 MoonBit handler 收到了正确事件。
- 修复范围：明确测试后端和支持后端的区别，防止 unsupported 操作被描述为真实成功。当前不增加设备任务、模拟平台实现或新的测试框架。
- 静态验收：文档/测试名称/结果报告准确说明后端；类型检查、编译、host test 和设备执行分别记录，不互相替代。

## 当前静态检查

必需检查不启动 Android/WebView；`moon test` 只编译，不执行：

```sh
moon fmt --check
moon check --target native
moon test --target native --build-only
moon info --target native
```

只有工具链齐备时才做 Android 编译/lint，例如：

```sh
./android/gradlew -p android :host:compileDebugKotlin :host:lintDebug :app:assembleDebug :app:compileDebugAndroidTestKotlin --no-daemon
```

Gradle 使用项目要求的 8.10.2。安装 Gradle 不会提供 Android SDK platform、NDK 或 CMake；缺少依赖时记录具体前提，不为完成文档工作额外搭建模拟器。以上 Gradle task 是编译/静态分析，不是设备测试；native host check 不覆盖 `__ANDROID__` 分支，C 分支需真实 JNI/NDK 头及对应编译配置另行检查。

审查 JNI 修改时逐一核对：prototype、static/instance ABI、异常状态、引用创建/释放、attach ownership 和失败出口。审查 Kotlin 修改时核对：主线程契约、初始化发布顺序、旧任务身份、创建回滚，以及 pending future 的唯一终结。

暂不执行 connected tests、模拟器/真机测试、WebView UI/renderer smoke、checked JVM 动态场景或以 mock/host 成功替代平台验证。后续恢复动态验证时，按条目补失败场景，不把当前静态验收升级为完整行为验证。

本次实际结果：

- `moon fmt --check`、`moon check --target native`、`moon test --target native --build-only`、`moon info --target native` 成功。测试未执行。保留一个 `unused_package` warning：WebView 不再调用 Android 的 ready 预检查，但仍必须依赖其 native stub 来链接共享 lease/error 符号；没有删除该依赖或禁用 warning。
- 使用项目 Kotlin 2.0.21 compiler、官方 Android 35 `android.jar`、WebKit 1.12.1 及既有 test 依赖直接编译实际 host 和仪器测试源码，均成功。`javap -private -s` 核对私有 JNI static 签名；未加载 NativeBridge 或运行测试。
- 两个 C stub 的 `__ANDROID_API__=24/35` 分支通过 GCC `-std=c11 -Wall -Wextra -Werror -fsyntax-only`，使用真实 JDK JNI/MoonBit 头和临时 Android 函数声明。该检查覆盖 JNI prototype，但临时声明不是 NDK，不能声称完成 Android native build。
- 实际 CMake 配置及 `generate_moonbit_android_runtime` target 成功，生成 C 语法检查成功。生成代码有 GCC missing-braces warning，未修改或压掉工具链输出。
- bundled `./gradlew --version` 实际运行并显示 8.10.2。Gradle Android compile task 在配置阶段报告 `SDK location not found`；本机缺完整 SDK/NDK，APK、lint、R8 和双 ABI native 构建未通过验收。未运行设备、模拟器、checked JVM 或 WebView 动态场景。


## 成熟项目对照：仅保留与修 bug 有关的模式

| 参考 | 可借鉴的修复约束 | 当前不引入 |
| --- | --- | --- |
| [jni-rs，固定源码](https://github.com/jni-rs/jni-rs/tree/dbfb17647988b81ff37b897ce93cc85ee4c84863) | 区分检查/捕获/清除异常；只释放自己拥有的 attachment/ref；JNI 编码独立于标准 UTF-8 | Env/JavaVM 公共 API、宏绑定和 invocation |
| [fbjni 的注册入口](https://github.com/facebookincubator/fbjni/blob/bbd15f2efb2d9409ed7671cc205f1f07f9042454/cxx/fbjni/detail/Registration.h#L88-L107) | 函数 ABI、descriptor、入口异常处理应一致；用已有桥逐项核对 | typed binding、Hybrid/native peer、发布体系 |
| [jni-bind 的签名实现](https://github.com/google/jni-bind/blob/e6fc6d3ff3ee1bf444a7dfc0bf01ac83b8bf362d/implementation/signature.h#L34-L108) | method/constructor/field 和 static/instance 语义不可混淆；缓存引用必须有释放责任 | schema DSL、生成器、全套对象操作 |

参考库的能力范围不同，不能认为三者都具备完整双向异常转换、direct-buffer 所有权或 Android 设备 CI。ajni 现有 lease、UTF-16 路径和可选包边界应保留，不为比较重造一套封装。

## 规范依据

- [JNI ThrowNew 参数](https://docs.oracle.com/en/java/javase/21/docs/specs/jni/functions.html#thrownew)：第二参数为 Throwable 子类的 `jclass`。
- [JVM method descriptor](https://docs.oracle.com/javase/specs/jvms/se21/html/jvms-4.html#jvms-4.3.3)：参数槽上限与隐式 `this`。
- [CONSTANT_Utf8_info](https://docs.oracle.com/javase/specs/jvms/se21/html/jvms-4.html#jvms-4.4.7)：length 是 MUTF-8 字节数。
- [Android JNI tips](https://developer.android.com/ndk/guides/jni-tips)：线程局部 JNIEnv、引用/异常规则与 checked JNI。
- [WebResourceResponse](https://developer.android.com/reference/android/webkit/WebResourceResponse)：状态码范围不支持 3xx。
- [WebViewClient](https://developer.android.com/reference/android/webkit/WebViewClient)：renderer 退出处理及导航回调。
- [JNI consumer keep rules](https://developer.android.com/topic/performance/app-optimization/keep-rule-examples#jni)：native→Java 字符串可达成员的保留规则。

可信 origin 是消息权限边界，不能自动等同导航/资源网络沙箱。当前只澄清这一契约，不新增封网、CSP 或导航策略。同样，不把缺少 32 位 ABI、桌面 JVM 或性能 benchmark 列为当前 bug。
