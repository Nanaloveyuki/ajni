# 通用双向 JNI：单线程执行、受检句柄与可选宿主

状态：已接受并实现，作为 ajni 0.3.0 发布契约（固定工具链要求包版本为 0.x.y）。2026-10-16 完成 runtime/unsafe/invocation 与 Android seam 迁移；真实 Linux/JDK21 两种宿主通过，Android 双 ABI 构建与 API37 x86_64 模拟器 8 项 instrumentation 通过。本文记录接口契约和验证边界；重大接口变化同步更新，架构替代时标记 superseded。

## 目标与现状

通用闭环是：Java 创建/加载 ajni → MoonBit 构造 Java 对象并调用方法 → Java 同步回调 MoonBit → 返回有类型的数据；对象跨调用存活，异常可恢复，关闭后拒绝调用。桌面 native 程序也可创建 JVM 后进入相同核心。Android 使用已有 ART，WebView 不参与通用闭环验收。

根包保持描述符和文本工具；新增 `runtime` 复用 `JniClass`、`JniType`、`JniMethod`，描述符包不依赖 JNI 头、JDK 链接或 Android。Android/WebView 共用 core VM/env lease、异常和字符串转换，feature 注册与状态仍由适配器持有。

采用 MoonBit native 的 C codegen，生成 C 与版本匹配的 runtime 由宿主构建系统链接。复杂 struct/enum 不跨 C ABI；仅使用明确的标量、opaque pointer、受管理文本/数组和 FuncRef trampoline。typed error 在 MoonBit 内捕获后转换，不能直接作为 C ABI 返回值。

## 模块与依赖方向

| 包/模块 | 职责 | 依赖 |
| --- | --- | --- |
| 根包 `Nanaloveyuki/ajni` | 描述符、现有文本工具 | core |
| 新 `Nanaloveyuki/ajni/runtime` | 受检对象句柄、方法/字段/构造器、数组、Java 异常、回调 | 根包、私有 C JNI adapter |
| 新 `Nanaloveyuki/ajni/unsafe` | 显式借用原始 VM/Env/ref、ABI 正确的 native 注册入口 | runtime；由消费者保证原始值有效 |
| 新 `Nanaloveyuki/ajni/invocation` | 桌面显式 JVM 库路径、创建参数、创建一次 JVM | runtime、可选功能包内依赖 dynlib |
| `Nanaloveyuki/ajni/android` | 已有 ART 接入、生命周期、Surface、main Looper | runtime、根包 |
| `Nanaloveyuki/ajni/webview` | 浏览器命令与事件 | Android/runtime |
| `android_runtime` / 消费者 executable 包 | 导出实际被链接的 MoonBit wrapper、安装用户处理器 | 所需 feature 包 |

runtime 不依赖 invocation、Android、WebView、sync 或 dynlib。JNI 头由实际宿主编译提供：桌面 JDK 或 Android NDK，不手抄 JNI 函数表。非 native backend 显式报告 unsupported，不制造成功。

## 执行线程与进入规则

一个链接的 MoonBit runtime 采用一个 owner OS thread。宿主在该线程显式初始化，顺序为 runtime 初始化 → 包顶层初始化 → 建立 session → 发布 ready。`JNI_OnLoad` 负责保存 VM 和注册 native，不默认把执行该 hook 的线程当作 owner；Android 在 main Looper 的 initialize 确定 owner。桌面 invocation 在调用创建接口的线程确定 owner。

C adapter 在调用任何 MoonBit 导出、查找 MoonBit closure、retain/release managed 值或分配 MoonBit 对象之前检查 session 状态与 owner。线程身份使用 OS thread identity，不使用 JNIEnv 地址判断。外部错误线程只接触 C 控制记录和本线程 JNI，抛 `IllegalStateException`，不进入 MoonBit。不能依赖进入 MoonBit 后才执行的 `sync.Mutex` 来保护进入过程。

允许 owner 上 Java → MoonBit → Java → MoonBit 的同步重入，使用 native active depth 记录调用深度，不在调用用户 Java/MoonBit 时持有 registry mutex。其他线程不会等待 owner，也不排队同步请求，避免 owner 等待 Java worker 而 worker 等待 owner。用户 Java 方法自身 join 一个回调 worker 时，worker 必须得到明确错误而不是无限等待。

Android 的异步事件由 Kotlin main Looper 投递后进入 JNI；core 不创建线程或事件循环。外部纯 MoonBit 程序仍须遵守 owner 契约：MoonBit 没有 Send/Sync，传递 managed 对象后再检查线程已不足以使该传递安全。这里的“受检”保证 native adapter 管辖的入口和句柄有效性，不承诺语言级跨线程安全。

`GetEnv` / attach / detach 与 MoonBit owner 是两份契约。只 detach adapter 本次自行 attach 的线程，绝不 detach Java 调入线程；Env 仅在当前调用/线程有效，不保存供其他线程复用。

## 资源与句柄

公开类型名称以 `JavaObject`、`JavaClass` 区别于现有描述符类型 `JniClass`。所有公开对象引用统一持有 global ref；local ref 只存在于 C 的当前调用/frame 中，不向 MoonBit 返回 local ref。这样不用假装普通 MoonBit closure scope 能提供 borrow checker：即使 closure 保存了返回值，引用也不会因 JNI 返回而失效。

| 资源 | 持有与释放规则 |
| --- | --- |
| `Session` | C 控制记录存 VM、owner、generation、状态、active depth；一个 runtime 至多一个 live session |
| `JavaObject` / `JavaClass` | MoonBit external object 持有 native 记录；记录持有 global ref 和 session 身份 |
| `JavaMethod` / `JavaField` | 存 descriptor、static/instance 种类、解析 ID，并持有 declaring class；不把 ID 当 jobject |
| 回调 token | Java 仅持有单调递增、检查溢出的整数 ID；ID 不复用，不使用裸 MoonBit/C 地址 |
| 每次 JNI 调用 | C 创建 local frame，检查结果/异常，将返回引用提升为 global 后 pop；失败清理已取得资源 |
| Java 异常 | 保留原 Throwable 的 global ref；描述字符串仅用于诊断，不替代异常对象 |

每次操作检查 owner、session/generation、记录 open 状态和句柄 VM 一致性，检查必须在使用 JNI ref 之前完成。对象允许 Java null，用 `Option[JavaObject]` 表达；null 不是句柄错误。对象身份比较调用 `IsSameObject`，不比较指针值。

句柄 `close` 幂等；多个 MoonBit 别名共享同一 native 记录，关闭后所有别名得到 Closed。记录本身在 wrapper 最后释放前不 free，不能通过一个已释放指针检查 alive。Session 关闭使所有子资源失效并删除 global refs；尚存 wrapper 仍可安全报告 Closed，最终只释放控制记录。owner 线程上的 finalizer 是遗漏 close 的兜底，不能在 finalizer 中执行用户回调或 DestroyJavaVM。
已接纳的 lookup/call/field 操作持有独立 declaring 引用和必要的 local snapshots；同步重入中关闭公开句柄只影响后续操作，不使挂起调用使用的 JNI ref 失效。

Session 与子记录之间不建立 managed/native 强引用环：session registry 拥有 native resource entry，wrapper 对 native 控制记录保活；close 解绑 registry、删除 JNI 资源，最后 wrapper drop 回收控制记录。记录计数和 registry 的原生同步不等于 MoonBit 对象可跨线程共享。

Session close 只允许 owner 且 active depth 为零；从回调内关闭返回 Busy，保持 Open，不半关闭。关闭顺序：停止接纳 → 使 token 失效 → 释放回调存储引用 → 删除对象/Throwable/class global refs → 标记 Closed。错误清理不覆盖原始业务异常。Android Activity shutdown 只关闭 Android feature 状态，不销毁通用 session；进程级 session 在宿主显式终止时关闭。

## MoonBit 侧接口形状

以下接口已导出，精确签名见 `src/runtime/pkg.generated.mbti`。可恢复失败使用独立的 `RuntimeError`，现有根包 `JniError` 不混入句柄和 VM 生命周期状态。

| 操作 | 设计形状 |
| --- | --- |
| 类 | `session.find_class(JniClass) -> JavaClass raise RuntimeError` |
| 方法查找 | `class.instance_method(name, JniMethod)`、`class.static_method(name, JniMethod)` |
| 构造器 | `class.lookup_constructor(parameter_types) -> JavaConstructor`，避免编译器保留字 `constructor`，内部生成 `<init>` 的 void descriptor |
| 创建对象 | `ctor.new(arguments : ArrayView[JavaValue]) -> JavaObject` |
| 调用 | `method.call(arguments, receiver?=object) -> JavaValue`；static 禁止 receiver，instance 必须有 receiver |
| 字段 | `class.instance_field/static_field(name, JniType)`、`field.get/set(receiver, value)` |
| 文本 | `session.new_string(String)`、`object.to_string_value() -> String`，精确 UTF-16 code-unit 往返 |
| 数组 | `new_primitive_array`、`read/write_region`、`new_object_array`、`get/set_element`；offset/length 在 JNI 前校验 |
| 引用 | `object.close()`、`object.is_same(other)`；长期引用无需额外 promote |
| 回调 | `session.callback(JniMethod, handler) -> JavaCallback`，提供可传给 Java 的 callback object |
| 生命周期 | `session.close() -> Unit raise RuntimeError`；不拥有被宿主注入的 JVM |

`JavaValue` 包含 Boolean、Byte、Char、Short、Int、Long、Float、Double、Object(Option[JavaObject])、Void。Boolean 不与 Int 混用；Byte/Short/Char 在构造或调用前检查范围；参数没有 Void。方法 descriptor 在 lookup 时解析并缓存，调用前检查数量、类型、instance 显式参数槽不超过 254、receiver 是否属于 declaring class、对象参数是否可赋值。返回值 variant 由 descriptor 决定，不能把 Java null/0/false 当错误。

C 按真实 jni.h 构造 `jvalue[]`，使用 `NewObjectA` / `Call<Type>MethodA` / `CallStatic<Type>MethodA`，不绑定 C varargs。短参数列表使用固定上限栈缓冲，较长列表按已验证参数数目分配，不按每个参数单独分配。不引入 libffi。

首次对象返回用 global ref 换取普通 MoonBit 值的可保存性，代价是 JNI global-ref 创建/删除。性能优化以实际数据为据；不先引入用户可见的 local-reference arena。原始 local-ref 快速路径只放 unsafe。

数组默认 region copy，不暴露 `GetPrimitiveArrayCritical` 指针，也不在 pinned 状态调用 Java。DirectByteBuffer/零拷贝、弱引用、Monitor、完整反射、绑定生成器不属于闭环所需内容。

## 双向异常

| 情况 | 行为 |
| --- | --- |
| MoonBit 调 Java 后存在 pending exception | C 取得 Throwable 并清除 pending，保存 global ref，MoonBit raise `JavaThrown(throwable)` |
| handler 捕获 JavaThrown 后继续 | 后续 JNI 调用正常；没有残留 pending exception |
| JavaThrown 从 MoonBit 回调返回 Java | C 使用原 Throwable `Throw`，保留身份/cause，不只重建 message |
| 可恢复 MoonBit 业务错误 | 导出 wrapper 内捕获并返回显式 native status/error payload；C 抛 host `AjniException` |
| 无异常但 JNI 返回分配/查找失败值 | 显式 NativeFailure；对象 null 是否正常取决于具体 JNI 操作 |
| 已有 pending exception 的不合法入口 | 不进入用户 MoonBit；只做允许的异常检查/清理，保留原异常 |
| native panic、OOM 导致 runtime abort | 进程失败；不承诺 Java catch，不修改 runtime 或用 longjmp 绕过栈清理 |

错误路径包含 global-ref 创建失败和异常文本转换再次抛异常：优先保存原始异常，不为诊断调用任意 Java toString。若异常保活分配失败，入口按宿主方向保留可返回的 Java 异常或报告不可恢复资源失败，不继续执行用户代码。每项 JNI 可能失败的调用独立检查 exception，不使用返回值短路跳过检查。
`JavaThrown` 可由用户构造，因此回调边界在 `Throw` 前检查 payload 是 open 的 `java/lang/Throwable`；非 Throwable 映射为 `IllegalArgumentException`，不向 JNI 传无效对象。

## Java → MoonBit 回调，不需要动态 trampoline

新增小型、无 Android 依赖的 Java companion：`AjniCallback implements AutoCloseable`。Java 侧提供 `Object invoke(Object... args)`，仅调用一个固定 static native dispatcher，descriptor 为 `(J[Ljava/lang/Object;)Ljava/lang/Object;`。token 查找和 owner/session 检查在 C 内完成。

注册时保存 method descriptor 与 MoonBit closure。C 依据 descriptor 解箱参数，MoonBit handler 接收 JavaValue 列表并返回 JavaValue；返回基本类型按对应 Java wrapper 装箱，Void 返回 null，对象返回有效 local ref。object-array 中真实 null 仅可对应引用参数。Java companion 不假装可直接实现任意 SAM/interface；Android listener/业务接口用显式 Java/Kotlin adapter 委托该对象即可。

closure 在 owner 注册时转移一个 owned 引用到 registry，调用时 retain 一个独立临时引用，调用结束释放；注销释放存储引用。调用中 close 只禁止后续调用，当前临时引用使已接纳调用可以完成。Java `close` 可来自其他线程：C 仅标记 token tombstone 并排入 native 待释放列表；不从该线程 decref closure，owner 在下次进入或 session close 时 drain。重复 close 幂等，无下一次进入时由 session close 收尾；不依赖 Java Cleaner 执行 MoonBit。

迟到 token 返回 Closed，不 dereference 旧 closure；错误线程返回 WrongThread。查 token、retain closure 与 close 的竞态由 native registry lock 保护，但释放锁后才调用 MoonBit。owner 自身同步重入正常建立新的 active frame。

任意 Java `native` 声明的直接绑定仍可用 unsafe：由消费 executable 导出匹配签名的 MoonBit函数，C 提供 `JNIEXPORT`/`JNICALL` 的真实入口，再 `RegisterNatives`。`NativeMethod` descriptor 只证明声明形状，不证明 function pointer ABI；不给任意地址贴上“安全注册”标签，也不在这一阶段实现动态机器码 trampoline。

## ClassLoader、构建与宿主生命周期

桌面基础 fixture 使用系统 loader；通用 session 允许显式设置 application ClassLoader global ref。应用类解析使用该 loader 的 loadClass；内部名转二进制类名，数组类按 JNI/Java 规则单独处理。bootstrap/array 查找失败保留原异常，不无条件回退到另一个 loader。JNI_OnLoad 保存 companion class 引用以注册固定 dispatcher；不能依赖 native-attached thread 的默认 FindClass 找到应用类。
成员 descriptor 中的引用类型使用 declaring class 的 defining ClassLoader，通过 `Class.forName(name, false, loader)` 解析；不能用 session loader 替代导入 class 的 loader。callback descriptor 仍按 session loader 解析。

class/method/field 缓存归 session，class global ref 保证 ID 使用期间类不卸载，不按裸名字跨 ClassLoader 复用。session close 才释放相关缓存。JNI_OnUnload 不能在未知线程上执行 managed cleanup；正常关闭必须先由 owner 完成；未关闭的 live session 阻止/延后宿主卸载，不向用户承诺任意 ClassLoader 热卸载。

核心 C 文件不定义全局唯一 JNI_OnLoad，由 Android或桌面 companion glue 统一注册 core/feature native。消费者 executable wrapper 决定导出集，不能指望依赖包的 exports 自动传递。CMake 收集所选包全部 native stubs，包括可选 invocation 的 dynlib.c；不因 moon.mod 添加依赖就假定 CMake 已链接它。

pin MoonBit compiler/runtime 一起更新；记录生成 C 所需初始化次序。Android 继续 API24、arm64-v8a/x86_64，沿用实际 NDK jni.h 和 consumer keep rules，新增 companion 的 JNI成员同样保留。桌面先验证本机 Linux/JDK21，Windows/macOS 支持需各自 ABI/loader 验证后宣称。

## sync 与 dynlib 的复用决定

`sync` 当前 README 明确不支持 Android。`unsafe.Mutex` 是非递归锁，且加锁前已进入 MoonBit，不能保护 JNI→MoonBit 的整个进入过程；其 boxed 值还能逃逸，不能自动成为 heap 的锁。故 core 不增加 sync 依赖，也不为 JNI 改造兄弟项目。若将来需要异步消息通道，可借鉴 OwnedBytes 的 native-owned copy 规则，但必须另行设计调度和端点生命周期。

`dynlib` 适合 invocation 包：用户提供完整 JVM 动态库路径 → 私有持有 Library → resolve JNI_CreateJavaVM → 取得地址 → 专用 C bridge 按 `jint (JNICALL *)(JavaVM **, void **, void *)` 调用。JavaVMOption/InitArgs 在 C 按真实头构造。核心与 Android不需要它。

Symbol 持有 Library 不阻止 Library::close，已导出的 UInt64 地址也无法撤销。因此 invocation 不向用户暴露 Library，创建成功后加载引用保留至进程退出；session.close 只关闭 ajni 资源，不 DestroyJavaVM、不 dlclose libjvm。创建前 load/resolve 失败可关闭；创建调用发生后的失败按 VM 实际状态处理，不盲目卸载。首个闭环不支持 JVM 销毁/重建或自动发现安装路径。

## 实施顺序与可观察验收

每一步交付真实行为与回归，不向消费者发布无实现 API。

1. **抽取通用 C/runtime seam**：新 session、owner检查、句柄与异常；保留根描述符包纯净。真实 Java→MoonBit 入口安装 session，验证同线程重入成功，错误线程在 MoonBit调用计数改变前失败，关闭后旧句柄被拒绝。
2. **MoonBit 调 Java**：实现实例/静态方法、构造器、字段、UTF-16 字符串、primitive/object 数组。Java fixture 有确定的对象状态和字节内容，检查 null、NUL、代理项、数组边界、参数错误、关闭别名、错误 VM/session；用真实 Get/Set/Call 而非mock。
3. **Java 回调 MoonBit**：固定 companion dispatcher、closure registry、跨线程 close与 owner drain。验证带捕获 closure、嵌套回调、调用内注销、迟到 token、错误线程；Java原 Throwable identity 与 MoonBit错误映射均有断言。此时 Java-hosted 通用双向闭环完整。
4. **桌面 invocation**：复用 dynlib 与专用调用桥，native-hosted闭环同样执行，无预先链接 libjvm 要求；失败库路径、缺失符号和创建失败有可观察错误，成功路径保活 libjvm至退出。
5. **Android 迁移与设备证据**：通用 session由主线程管理，迁移现有 VM/lease/异常/字符串管理到同一核心，保持 lifecycle/Surface合同。设备执行完整 callback闭环、Activity重建及shutdown；WebView只增加feature验收，不作为core依赖。旧helper/重复global状态在迁移完成时删除。

JVM smoke 使用 `java -Xcheck:jni`，构建生成 C + 真实 runtime，断言业务结果、对象状态和异常身份；返回0或没有warning本身不算完成。保留可复跑 fixture/命令，CI实际执行。NDK编译、APK和instrumentation编译是静态证据；ART/真机/emulator场景单独记录，不升级为已通过。

## 设计依据与已知证据

2026-10-16 已运行的临时探针：moonc0.10.14、JDK21 checked JNI；10,000次 Java→C→MoonBit→C→Java→C→MoonBit，capturing closure、六组 UTF-16、global-ref 对象、原 Java 异常返回、串行第二线程通过；另有 native launcher 创建 JVM 再双向调用通过。该探针已删除，不作为未来可重复验收入口，也不证明本设计尚未实现的受检句柄、dispatcher 或 owner gate 已通过。
同日补充 owner-gate 探针：同一 owner 线程的 3 层同步重入成功；foreign Java thread 在 MoonBit 入口前收到 `IllegalStateException`，MoonBit 调用计数未增加；JDK21 `-Xcheck:jni` 无告警。该探针同样已删除。

2026-10-16 永久 fixture：`python3 scripts/run-jvm-smoke.py`，临时隔离 workspace、生成 C 与 pin runtime、真实 JDK21 `-Xcheck:jni`。Java-hosted 验证构造/方法/字段、八种 primitive、primitive/object 数组、Unicode 类/成员/descriptor、NUL/emoji/未配对代理项、对象身份、参数/边界失败、嵌套捕获闭包、调用内 close、foreign close/拒绝、Busy、原 Throwable identity、关闭后重开与旧 generation。native-hosted 通过 dynlib 动态加载同一 JVM，执行双向调用与 unsafe 注册；坏路径、缺失符号、无效 JVM option 分进程验证。两种宿主均通过，无 checked-JNI 告警。
安全复查回归：非 Throwable `JavaThrown` 在修复前以 checked-JVM SIGABRT 复现，修复后 Java 收到 `IllegalArgumentException` 且后续回调返回 73。隔离 child-first loader 的 imported class/参数调用返回 42；descriptor 解析回调关闭原 class wrapper 后，挂起 lookup 仍完成，原 wrapper 后续为 Closed，独立 method 仍返回 42。以上均纳入永久 fixture，最终双宿主重跑通过。

Android：`:host:checkJniContract` 以真实 NDK29 API24 对 arm64-v8a/x86_64、exports/callback 两配置检查 core/feature C 与 12 个 bridge native；debug APK、instrumentation APK、host/app lint 通过。用户 GUI 重启模拟器解决 adb offline 后，API37 x86_64 执行 `am instrument -w -r -e class dev.nanaloveyuki.ajni.demo.NativeBridgeInstrumentedTest dev.nanaloveyuki.ajni.demo.test/androidx.test.runner.AndroidJUnitRunner`，8 项全部通过：真实 typed callback/状态累积、foreign reject/close、observer 捕获异常不误抛、ActivityScenario.recreate、feature shutdown/reinit、Unicode、WebView 创建销毁/容器重建/消息与资源完成。demo 实际启动，截图观测 Surface 尺寸及渲染。arm64 设备、API24 运行、R8 lookup 和未覆盖失败注入不在该证据内。

- [MoonBit FFI](https://docs.moonbitlang.com/en/latest/language/ffi.html)：C ABI、FuncRef、ownership；latest文档与pin编译器有版本差异，以实际编译验证为准。
- [JNI函数规范](https://docs.oracle.com/en/java/javase/21/docs/specs/jni/functions.html)：A系列调用、引用、异常、RegisterNatives。
- [JNI Invocation](https://docs.oracle.com/en/java/javase/21/docs/specs/jni/invocation.html)：VM创建、线程、加载/卸载约束。
- [Android JNI tips](https://developer.android.com/ndk/guides/jni-tips)：线程局部Env、引用、字符串与ClassLoader。
- 本地现有实现：`src/android/ajni_bridge.c`、`android/app/src/main/cpp/CMakeLists.txt`；兄弟项目：`../sync/README.md`、`../sync/src/unsafe/mutex/`、`../dynlib/src/library.mbt`、`../dynlib/src/dynlib.c`。兄弟目录是本地参考，不是发布包依赖声明。
