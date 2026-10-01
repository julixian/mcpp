---
subject: design
status: landed
---

# 工具与工具链的来源：声明、编程决定、可观察

日期：2026-10-01。状态：已落地（mcpp 2026.10.1.3,mcpp#755)。

生态侧的设计记录在 mcpp-plugins `.agents/docs/2026-10-01-ecosystem-build-plugin-framework-design.md`(v3)。本记录只写引擎这一侧:为什么是这些机制,以及每个决定的理由。

## 1. 问题

一次构建用到三类东西,它们的来源此前各有一套规则,而其中两类根本无法由使用者陈述。

**插件声明的载荷与是否使用无关,都在构建程序运行前下载。** 这是时序的后果:
`[feature-xlings.<f>]` 的条目在特性打开、target 选择器为真时由 prepare 供给
(features.cpp 的 `step6_xlings_workspace_from_graph`),而构建程序在那之后才运行。于是
`build.mcpp` 里写 `o.cmake = "/usr/bin/cmake"` 也照样下载 `xim:cmake`;离线时整个构建被拒,
构建程序根本没有运行的机会。实测记录在 mcpp-plugins 的
`.agents/docs/2026-10-01-payload-source-verify.sh`:一个替身插件在 `MCPP_NO_AUTO_INSTALL=1`
下三种配置的读数。

**下载由「声明了」触发,而不是「用到了」。** `dist-apk` 的 `bundletool` 只在
`--format aab` 时用到,却每次 Android 构建都装;`dist-appimage` 的工具每次 Linux 构建都装。
插件清单自己记下了这件事的代价:「provisioning runs before the build program learns
`--format`」。

**主工具链只能是托管载荷。** PATH 上的编译器被拒,理由是无法识别、无法复现(docs/20),
而这条理由对「一棵被命名、被识别、被记录的树」并不成立——`msvc@system` 就是反例,它定位
机器上的 VS 并照常驱动。自建 trunk、厂商交叉工具链因此只能写 xim 配方,还要改核心的
`to_xim_package`。

**输出不区分来源。** 一次构建说不出「这次用的 cmake 是谁的」,出错时也无从归因。

## 2. 决定与理由

### 2.1 一个「来源」概念,五个类

主体是 `toolchain.build`、`toolchain.bootstrap`、`payload:<ns>:<name>`、
`tool:<module>:<name>`;类是 `managed`、`pinned`、`custom`、`program`、`host`。

类划出的线只有一条:**是生态选的,还是人或机器选的**。`managed` 与 `pinned` 都是生态的,
所以它们合起来是「默认」,而默认的输出必须与本机制存在之前逐字相同——这是无感升级的判据,
e2e 与 framework-lab 的 golden 都按它断言。

`host` 单独成类,不与 `custom` 合并:它把产物与机器状态绑在一起,而其他几类不会。判据是
「版本有没有被陈述」,不是「路径像不像系统目录」——后者是猜测。

### 2.2 记录只有一份

`SourceDecision` 一个主体一条,写进已有的 `resolution.json`(新增 `sources` 键,没有读者
按 `schema_version` 分支),并由输出、`mcpp why`、机器输出、`--managed-only` 共同读取。

**理由是这个代码库已经付过的代价。** 同一个答案被两处分别推导,失败形态是「装了 A、答了
B,而且什么都没说」——`mcpp.xlings.address_set` 的文件头记着这件事。所以 `fillXpkgDirs`
与供给集合共用一次统一的结果,来源记录也只有一处写入。

### 2.3 覆盖:环境变量 > 清单 > 全局配置

与 `[indices]` 的「项目 > 全局」方向一致,但环境变量排在清单之前。理由是 CI 与发行版打包
要在不改清单的前提下换工具,而 `[tools.overrides]` 的文档已经把环境变量定位成这件事的出口。
风险(环境意外覆盖项目的明确决定)由「每条覆盖都显示并记录」抵掉:
`Using xim:cmake ← … [custom · env MCPP_XLINGS_OVERRIDE_XIM_CMAKE]`。

覆盖只认根。依赖替消费方决定来源,就是替使用者做决定;`[tools.overrides]` 已经立下同一条
规矩。

**覆盖参与版本校验,但不参与裁决。** 它陈述的是「从哪里来」,不是「要哪一版」,所以键不带
版本;写了 `version` 时按 `addrset::override_violation` 与每条落败的要求比较,没写时记一条
note 点出未被校验的要求。引擎**不**运行任意程序去问版本:每个工具的 `--version` 格式不同,
那是插件的知识。

### 2.4 按需供给:请求 + 重跑,而不是「构建程序之后再供给」

`rules-cuda` 读工具包头文件里的版本,`rules-qt` 读 SDK 文件,`dist-apk` 从 platform 目录读
API level,`dist-wix` 检查 payload 里的文件是否存在——这些成员在**规划时**就需要载荷。把
供给整体移到构建程序之后会让它们规划失败。

所以是:程序请求 → 引擎批量安装 → 只重跑请求过的程序。请求的那次运行被**丢弃**
(`run_build_program` 在 `dirs::apply` 与 `write_cache` 之前返回),所以没有要撤销的状态,
也没有半应用的指令集。最多三轮,第三轮仍有新请求就报错并点名——终止条件不依赖被调用方
改变状态,这是 `resolve_target_toolchain` 的递归曾经付过的代价。

稳态零开销:下一次构建载荷已安装,第一轮就能拿到答案,`contract_hash` 因此与第二轮相同,
缓存命中。

### 2.5 工具链:bootstrap 与 build 两段,`path:` 进入 spec

这两个角色**本来就存在**:交叉构建时 `build.mcpp` 由一个宿主工具链编译
(`xlings.cpp` 的 G3 分支)。本次只是给它们命名,并让 build 这一侧可以独立配置。

`path:<dir>` 做成 `ToolchainSpec` 的一种拼法,而不是第二条解析路径。理由是
`parse_toolchain_spec` 有十几个调用点,第二条路径意味着十几处都要学会它;做成一种拼法后,
不认识它的调用点自然报错而不是静默走错。族由 `<root>/bin` 里有哪个驱动决定——驱动是事实,
清单里的 `family` 只用于核对。

**不写入那棵树。** 托管载荷的 `clang++.cfg` 是这套机制的**产物**(docs/91 §5.1),写给直接
调用 clang 的人;mcpp 自己的调用绕过它。一棵不属于 mcpp 的树因此不该被写入,
`resolve_clang_driver` 改为在 `localRoot` 非空时按「驱动旁有 libc++」开启模型,而不是按
cfg 文件是否存在。

**身份按内容,不按版本。** 一个 trunk 驱动可以在版本不变时被重建,所以驱动与每个
`tools` 程序的路径、大小、修改时间进入 `driverIdent`(于是进入指纹),并写入
`local-toolchain.stamp` 供三条快速路径比较。不读字节:一个驱动几百 MB,而每次 prepare 都要
读它。

**`mcpp.lock` 不记工具链**,本次也没有加。lock 的内容是依赖解析的结果,而工具链不是被解析
的依赖;一台没有这棵树的机器在读到声明处就被拒绝,这已经是「不可移植」要的那句话。设计稿
曾写成「lock 中记为 `local`」,那是没有实现的断言,文档与规范按实际行为更正。

### 2.6 工具链阶段:两遍 prepare,第一遍不说话

构建程序需要它的宿主模块,宿主模块来自依赖图;而依赖图的解析需要工具链
(`cfg(compiler = ...)`、`requires`)。这是一个真实的环,所以它被切成两遍:
第一遍用 bootstrap 走到宿主模块注册,运行工具链阶段,然后以一个内部信号结束;
第二遍从头用陈述的工具链。

第一遍**不叙述**:它要说的每一句,第二遍都会就真正发生的那次构建再说一次。实现是
`driver.cpp` 的 `QuietPass`,而不是在每个输出点加条件——后者是会漏的那种。

工具链阶段**只能**陈述工具链:它运行在依赖图之前,那里的一条 flag、一个源文件或一个
action 描述的是一次还不存在的构建;一个载荷请求也无法回答,因为声明它的图还没读。引擎
按名拒绝,而不是静默丢弃。

它有自己的 `artifactsDir`(`target/.build-mcpp/toolchain-phase`):与构建阶段共用一份缓存
记录,会让两者每次构建互相失效。

## 3. 验证

- 单元:`tests/unit/test_sources.cpp`(15 例)——清单键的解析与拒绝、协议 15 的指令、
  覆盖的版本校验、`path:` 的族判定。
- e2e:873(覆盖)、874(按需)、875(按路径命名的工具链)、876(工具链阶段)、
  877(`mcpp why` 与机器输出)。判据统一是 `MCPP_NO_AUTO_INSTALL=1`:构建成功即「没有要求
  下载」,而拒绝会点名它本要装的东西。
- 既有 e2e:以关键词选出的 62 个用例通过;219 在**已发布的 2026.10.1.2** 上同样失败(本机
  glibc 顺序),658 需要连接 Android 设备。
- 生态:mcpp-plugins 0.19.0 的 11 个 consumer fixture 与 27 例 `plugin-logic` 通过;
  `tests/cmake-consumer` 在 `MCPP_NO_AUTO_INSTALL=1` 下,由 `build.mcpp` 点名 cmake 即可
  构建——这正是本次要做成的那件事。

## 4. 已知边界

- `mcpp build` 没有 `--format json`,所以来源的机器形态走 `mcpp why sources`,而不是构建
  事件流。docs/50 §3 把 `ndjson` 记为保留,本次不动它。
- 覆盖与按需供给都只作用于 xlings 载荷。依赖包 `kind = "bin"` 的宿主工具仍走
  `[tools.overrides]`:两者的键空间与语义不同(一个是 `<pkg>:<tool>` 的程序,一个是
  `ns:name` 的目录),合并会让一张表有两种键。
- 构建程序的编译命令在超过预算时走响应文件。这条路径在本仓库的 CI 里**到不了**:Linux 与 macOS
  的预算是 128 KiB,而只有 Windows 上 `capture_exec` 所经的 shell 是 8191 字节。覆盖它的是
  mcpp-plugins 的 `all-rules-compile`(导入十五个宿主模块)与两个单测——每种 tokenize 语法
  一个,因为 clang 与 GCC 把反斜杠当转义,cl 与 clang-cl 不当。
- 工具链描述数据化(核心读描述文件、不再硬编码 `to_xim_package`)不在本次范围;
  `[toolchain] { path }` 的字段已经与那份描述同形,所以它是后续的第三种载体,而不是改写。
