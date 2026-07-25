# Server Configs

This directory is the canonical location for YAML configs used by local server
nodes and test clients.

Naming follows the owning node or role:

- `message-hunan-im.yaml`: primary Message node on gRPC `50055`.
- `message-beijing-im.yaml`: secondary Message node on gRPC `50056`.
- `gateway-hunan.yaml`: primary Connection Gateway on `8090`.
- `gateway-beijing.yaml`: secondary Connection Gateway on `8091`.
- `gate.yaml`: Auth/API Gate HTTP、验证码和 SMTP 配置。
- `state-single.yaml`: one Gateway and one Message topology.
- `state-multi.yaml`: two Gateway and two Message topology.
- `test-client.yaml`: TCP test client config.
- `public-test.yaml`: public DAO unit-test config.

Runtime scripts default to this directory. Override paths with environment
variables such as `WIMI_STATE_CONFIG`, `WIMI_GATE_CONFIG`, `WIMI_MESSAGE_CONFIGS`, or
`WIMI_GATEWAY_CONFIGS` when a test needs a custom topology.

Development may use `transportSecurity.mode: insecure`. A config with
`environment: production` is rejected unless `mode: mtls`; the Gateway
certificate peer identity must match its registered `gateway_id`.

验证码由 Gate 进程直接处理。本地 `gate.yaml` 关闭邮件发送并通过响应返回验证码；
真实环境应设置 `verification.exposeCodeInResponse: false`、启用 SMTP，并用
`WIMI_VERIFY_EMAIL_USER`、`WIMI_VERIFY_EMAIL_PASS` 和可选的
`WIMI_VERIFY_SMTP_URL` 注入邮件凭据。`WIMI_VERIFY_SEND_EMAIL` 与
`WIMI_VERIFY_EXPOSE_CODE` 可覆盖对应布尔配置。
