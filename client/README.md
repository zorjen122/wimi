# WIMI Client

WIMI 的跨平台用户客户端，使用 Qt 6、QML 和 C++20。当前阶段以 Linux Desktop
为首要平台，支持确定性的 Fake Scenario、本地 SQLite，以及 Auth Gate +
Connection Gateway 真实网络模式。网络代码已按当前服务端源码契约实现；真实服务端
联调已通过可选的 live network test 覆盖核心单聊链路。

## 当前能力

- 星空蓝浅色/深色语义主题。
- Expanded、Medium、Compact 三档响应式布局。
- 会话列表、消息时间线、输入框和发送状态演示。
- 13 个可由命令行切换的 Fake Scenario。
- C++ `QAbstractListModel` 与 repository 边界；QML 不保存业务事实。
- 每账号独立 SQLite、WAL、逐版本 schema migration、本地 outbox、草稿、远端会话
  映射与同步游标恢复；当前 schema 为 v3，拒绝由更高版本客户端创建的数据库。
- 出站消息与 outbox 同事务写入；持久 `ACCEPTED` 同事务写入服务端标识、推进游标
  并移除 outbox；入站批次支持幂等落库。
- Auth Gate HTTP adapter；Connection Gateway TCP/TLV/QtProtobuf 登录、心跳、重连、
  退避、请求超时和同类请求本地背压。客户端不链接 Google `libprotobuf`，Android
  运行库由 Qt kit 随包部署。
- 登录、验证码、注册和找回密码 UI 已接入 Auth Gate；注册或重置成功后返回登录。
- 好友/申请拉取和处理、单聊/群聊文本、增量消息同步、群创建/入群审批、文件上传
  请求，以及 DELIVERED、READ 两类 ACK。
- 文本 outbox 使用不变的逻辑幂等 ID 重试；持久 `ACCEPTED` 和入站推送落库后 ACK
  已接入现有 SQLite repository。
- Linux freedesktop Notifications adapter、设置页通知自检、标准 `.desktop` 入口
  和可缩放应用图标。
- C++ model 单元测试、QML 静态检查和离屏 UI smoke test。

## Linux 构建

Ubuntu 26.04 的 QtProtobuf 开发文件由 `qt6-grpc-dev` 一并提供：

```bash
sudo apt-get install qt6-grpc-dev
```

在 `client/` 目录执行：

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
```

或从仓库根目录执行：

```bash
cmake -S client -B build/client-clang \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON
cmake --build build/client-clang -j2
ctest --test-dir build/client-clang --output-on-failure
```

当前 Ubuntu 26.04 环境的 GCC 15.2 会生成 Binutils 2.42 不支持的 `.base64`
汇编指令，因此 Linux preset 明确使用系统 Clang。它属于当前工具链组合问题，
不是客户端源代码对 Clang 的依赖。

## 运行与场景

```bash
./build/client-clang/wimi-client --scenario=normal
./build/client-clang/wimi-client --scenario=offline-cached --theme=dark
./build/client-clang/wimi-client --scenario=long-content --width=390 --height=844
```

可用场景包括 `normal`、`empty-account`、`offline-cached`、
`first-bootstrap`、`dense-chat-list`、`send-lifecycle`、`send-unknown`、
`sync-gap`、`auth-expired`、`friend-requests`、`group-admin` 和
`long-content`、`large-history`。`large-history` 会为首个会话附加 2000 条消息，
用于模型装载和 QML ListView 虚拟化回归。

开发回归可通过 `--screenshot=/path/to/image.png` 在首帧稳定后抓图。

使用本地 SQLite：

```bash
./build/client-clang/wimi-client --repository=sqlite --account=demo
./build/client-clang/wimi-client --repository=sqlite \
  --database=/tmp/wimi-client-demo.sqlite
```

未指定 `--database` 时，数据库写入 Qt 的应用本地数据目录，并按 `--account`
隔离。当前 SQLite 模式会在空数据库中写入可交互的演示数据。
纯表结构初始化脚本位于 `client/sql/init_client_sqlite.sql`，与当前 schema v3 对齐。

## 真实服务模式

传入 Auth Gate 基础地址即可启用真实模式：

```bash
./build/client-clang/wimi-client \
  --gate-url=http://127.0.0.1:18080 \
  --account=my-account
```

真实模式默认使用 SQLite，且新数据库不会写入 Fake 演示数据。`--account` 只决定
本地数据库隔离名；建议为不同登录账号使用不同值。也可以用 `--database` 明确指定
数据库路径。登录成功后客户端会连接 Gate 返回的 Gateway 地址，自动拉取好友和申请、
恢复未决 outbox，并对已有远端会话执行增量同步。

真实服务核心单聊链路由下文的可选 live network test 覆盖；默认 CTest 仍不会主动访问
本地服务。
本地开发链路仍是明文 HTTP/TCP；公网部署前必须增加 TLS 和凭证安全存储。

## 当前服务请求边界

客户端只连接 Auth Gate 返回的 Connection Gateway，不发现 Message 节点，也不生成
或使用服务端内部 `gateway_message.proto`。客户端仅从公开 `tcp_message.proto` 生成
C++ 类型。

当前接入服务端已注册的请求：登录、心跳、退出、好友列表、好友申请列表、发起/回复
好友申请、单聊和群聊文本、单会话增量拉取、旧全量拉取、群创建、入群申请/审批、
文件上传及 ACK。`SEARCH_USER`、`REMOVE_FRIEND`、`GROUP_PULL_MEMBER`、
`GROUP_QUIT` 等虽然已有 Service ID，但服务端没有注册 handler，客户端本阶段不会
提供伪实现。群创建后会建立本地群会话并可发送群文本；文件上传已有 C++ 请求接口，
但服务端 `FILE_SEND` 仍为空实现，因此本阶段不伪造附件发送 UI。

## 静态检查

```bash
cmake --build build/client-clang --target wimi-client_qmllint -j2
```

无显示服务器时，UI 测试由 CTest 自动设置 `QT_QPA_PLATFORM=offscreen` 和软件
渲染后端。

## 真实服务集成测试

默认测试不会访问外部服务。需要验证客户端自己的 Qt Gate/Gateway adapter 时，先启动
本地服务，再显式启用 live test：

```bash
cmake -S client -B build/client-clang \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON \
  -DWIMI_ENABLE_LIVE_TESTS=ON
cmake --build build/client-clang --target wimi-client-live-network-tests -j2

WIMI_CLIENT_LIVE_GATE_URL=http://127.0.0.1:18080 \
WIMI_CLIENT_LIVE_USER_A=zorjen \
WIMI_CLIENT_LIVE_PASSWORD_A=123456 \
WIMI_CLIENT_LIVE_USER_B=alice \
WIMI_CLIENT_LIVE_PASSWORD_B=123456 \
ctest --test-dir build/client-clang -R '^client-live-network$' \
  --output-on-failure
```

该测试覆盖真实 Gate 登录、Gateway 鉴权、好友拉取、文本持久接受、接收端推送、
DELIVERED/READ 回执，以及按 `conversation_seq` 增量同步。测试账号必须已是
好友；未设置上述环境变量时，该测试会跳过而不是访问默认地址。

## Linux 桌面集成

Linux 构建使用 Qt DBus 调用 `org.freedesktop.Notifications`，不依赖特定桌面环境。
在设置页可以查看当前 session 是否提供通知服务并发送测试通知。无通知守护进程时
会明确显示不可用，不影响聊天主流程。

验证安装布局时可使用临时前缀：

```bash
cmake --install build/client-clang --prefix /tmp/wimi-client-install
```

这会安装 `bin/wimi-client`、`share/applications/wimi-client.desktop` 和
`share/icons/hicolor/scalable/apps/wimi-client.svg`。Secret Service 暂不接入，等待
服务端正式 token/refresh 契约确定后再设计密钥生命周期。

## Android

Android 与桌面端共用 Qt executable 和 QML module，需要匹配的 Qt Android kit、JDK、
Android SDK 和 NDK。应用标识为 `org.wimi.client`；受支持的 Android 构建与部署配置
在公开仓库之外维护。

安装后可在“设置 → 服务端”配置设备可访问的 Auth Gate 地址。除非设备已明确通过端口
转发连接到开发主机，否则不要使用 `127.0.0.1`。
