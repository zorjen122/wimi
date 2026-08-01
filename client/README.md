# WIMI Client

WIMI 的跨平台用户客户端，使用 Qt 6、QML 和 C++20。

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

使用本地 SQLite：

```bash
./build/client-clang/wimi-client --repository=sqlite --account=demo
./build/client-clang/wimi-client --repository=sqlite \
  --database=/tmp/wimi-client-demo.sqlite
```

未指定 `--database` 时，数据库写入 Qt 的应用本地数据目录，并按 `--account`
隔离。当前 SQLite 模式会在空数据库中写入可交互的演示数据。
纯表结构初始化脚本位于 `client/sql/init_client_sqlite.sql`

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

## Linux 桌面集成

Linux 构建使用 Qt DBus 调用 `org.freedesktop.Notifications`，不依赖特定桌面环境。
在设置页可以查看当前 session 是否提供通知服务并发送测试通知。无通知守护进程时
会明确显示不可用，不影响聊天主流程。

验证安装布局时可使用临时前缀：

```bash
cmake --install build/client-clang --prefix /tmp/wimi-client-install
```

## Android

Android 与桌面端共用 Qt executable 和 QML module，需要匹配的 Qt Android kit、JDK、
Android SDK 和 NDK。应用标识为 `org.wimi.client`；受支持的 Android 构建与部署配置
在公开仓库之外维护。

安装后可在“设置 → 服务端”配置设备可访问的 Auth Gate 地址。除非设备已明确通过端口
转发连接到开发主机，否则不要使用 `127.0.0.1`。
