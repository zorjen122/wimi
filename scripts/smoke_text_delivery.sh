#!/usr/bin/env bash
set -euo pipefail

# 作用：
#   专门验证在线文本消息投递语义。
#   通过原始 TCP 协议包登录 1001/1002，确认接收方收到 ID_TEXT_SEND_REQ，
#   ACK 后不会收到 ID_NULL，并检查 MySQL messages 已落库且状态更新为 DELIVERED。
# 前置条件：
#   已初始化 MySQL，并启动至少一个 Message 服务；默认连接 Connection Gateway 127.0.0.1:8090。

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  cat <<EOF
Usage: ./scripts/smoke_text_delivery.sh

Requires local services and the test database to be running, for example:
  ./scripts/init_mysql.sh
  ./scripts/run_local_services.sh

Verifies online text delivery with protocol-level checks plus MySQL persistence:
  - receiver gets ID_TEXT_SEND_REQ
  - receiver ACK does not produce ID_NULL
  - messages contains the delivered text row
  - ACK marks the message as DELIVERED
EOF
  exit 0
fi

if [[ "$#" -gt 0 ]]; then
  echo "Unknown argument: $1" >&2
  echo "Run with --help for usage." >&2
  exit 2
fi

: "${MYSQL_HOST:=127.0.0.1}"
: "${MYSQL_PORT:=3306}"
: "${WIMI_DB:=chatServ}"
: "${WIMI_DB_USER:=zorjen}"
: "${WIMI_DB_PASSWORD:=root}"
: "${CHAT_HOST:=127.0.0.1}"
: "${CHAT_PORT:=8090}"
: "${GATE_URL:=http://127.0.0.1:18080}"

stamp="$(date +%s%N)"
payload="online_text_persist_${stamp}"
result_file="$(mktemp /tmp/wimi-text-delivery.XXXXXX.json)"
trap 'rm -f "$result_file"' EXIT

UID_SENDER=1001 \
UID_RECEIVER=1002 \
PAYLOAD="$payload" \
CHAT_HOST="$CHAT_HOST" \
CHAT_PORT="$CHAT_PORT" \
GATE_URL="$GATE_URL" \
MYSQL_HOST="$MYSQL_HOST" \
MYSQL_PORT="$MYSQL_PORT" \
WIMI_DB="$WIMI_DB" \
WIMI_DB_USER="$WIMI_DB_USER" \
WIMI_DB_PASSWORD="$WIMI_DB_PASSWORD" \
RESULT_FILE="$result_file" \
PYTHONPATH="$ROOT_DIR/scripts/lib${PYTHONPATH:+:$PYTHONPATH}" \
python3 - <<'PY'
import json
import os
import socket
import subprocess
import time

from wimi_tcp_client import WimClient, request_chat_auth, require

UID_SENDER = int(os.environ["UID_SENDER"])
UID_RECEIVER = int(os.environ["UID_RECEIVER"])
PAYLOAD = os.environ["PAYLOAD"]
CHAT_HOST = os.environ["CHAT_HOST"]
CHAT_PORT = int(os.environ["CHAT_PORT"])
RESULT_FILE = os.environ["RESULT_FILE"]
GATE_URL = os.environ["GATE_URL"]
MYSQL_HOST = os.environ["MYSQL_HOST"]
MYSQL_PORT = os.environ["MYSQL_PORT"]
WIMI_DB = os.environ["WIMI_DB"]
WIMI_DB_USER = os.environ["WIMI_DB_USER"]
WIMI_DB_PASSWORD = os.environ["WIMI_DB_PASSWORD"]

ID_TEXT_SEND_REQ = 1027
ID_ACK = 1033
ID_NULL = 1034


sender_auth = request_chat_auth("zorjen", "123456", GATE_URL)
receiver_auth = request_chat_auth("alice", "123456", GATE_URL)
sender = WimClient(UID_SENDER, CHAT_HOST, CHAT_PORT,
                   auth_token=sender_auth["chatToken"])
receiver = WimClient(UID_RECEIVER, CHAT_HOST, CHAT_PORT,
                     auth_token=receiver_auth["chatToken"])


def message_status(message_id):
    output = subprocess.check_output(
        [
            "mysql", "--protocol=tcp", f"-h{MYSQL_HOST}", f"-P{MYSQL_PORT}",
            f"-u{WIMI_DB_USER}", f"-p{WIMI_DB_PASSWORD}", "-N", "-B", WIMI_DB,
            "-e", f"SELECT status FROM messages WHERE messageId={message_id}",
        ],
        stderr=subprocess.DEVNULL,
        text=True,
    )
    return int(output.strip())

try:
    sender.login()
    receiver.login()

    unauthenticated = WimClient(UID_SENDER, CHAT_HOST, CHAT_PORT)
    try:
        try:
            unauthenticated.login()
            raise AssertionError("chat login without token unexpectedly succeeded")
        except AssertionError as error:
            require("login failed" in str(error), f"unexpected auth error: {error}")
    finally:
        unauthenticated.close()

    forged_seq = int(time.time_ns() % 9_000_000_000 + 10_000_000_000)
    forged_rsp = sender.request(
        ID_TEXT_SEND_REQ,
        {
            "seq": forged_seq,
            "from": UID_RECEIVER,
            "to": UID_RECEIVER,
            "data": "forged-sender",
            "sessionKey": 0,
        },
    )
    require(forged_rsp.get("error") == 0, f"canonicalized send failed: {forged_rsp}")
    canonical_push = receiver.expect_async(
        ID_TEXT_SEND_REQ,
        lambda payload: payload.get("data") == "forged-sender",
    )
    require(canonical_push.get("from") == UID_SENDER,
            f"client from field overrode session principal: {canonical_push}")
    receiver.ack(int(canonical_push["seq"]))

    client_seq = int(time.time_ns() % 9_000_000_000 + 1_000_000_000)
    sender_rsp = sender.request(
        ID_TEXT_SEND_REQ,
        {
            "seq": client_seq,
            "from": UID_SENDER,
            "to": UID_RECEIVER,
            "data": PAYLOAD,
            "sessionKey": 0,
        },
    )
    require(sender_rsp.get("error") == 0, f"text send failed: {sender_rsp}")
    require(sender_rsp.get("seq") == client_seq, f"sender rsp seq mismatch: {sender_rsp}")
    require(sender_rsp.get("messageState") == 1, f"message was not accepted: {sender_rsp}")
    require(sender_rsp.get("messageId", 0) > 0, f"message id missing: {sender_rsp}")

    receiver_service, receiver_push = receiver.recv_packet()
    require(
        receiver_service == ID_TEXT_SEND_REQ,
        f"receiver did not get text push: {receiver_service}, {receiver_push}",
    )
    require(receiver_push.get("data") == PAYLOAD, f"payload mismatch: {receiver_push}")
    server_seq = int(receiver_push["seq"])

    sender.ack(server_seq)
    time.sleep(0.5)
    require(message_status(server_seq) == 1,
            "non-receiver ACK changed message status")

    receiver.ack(server_seq)
    time.sleep(0.5)
    require(message_status(server_seq) == 2,
            "receiver delivery ACK did not advance status")
    receiver.ack(server_seq, receipt_type=2)
    time.sleep(0.5)
    receiver.ack(server_seq, receipt_type=1)
    time.sleep(0.5)
    require(message_status(server_seq) == 3,
            "late delivery ACK regressed READ status")
    try:
        extra_service, extra_payload = receiver.recv_packet(timeout=0.75)
    except socket.timeout:
        extra_service = None
        extra_payload = None

    require(
        extra_service != ID_NULL,
        f"ACK unexpectedly produced ID_NULL: {extra_payload}",
    )
    require(
        extra_service is None,
        f"ACK produced unexpected packet {extra_service}: {extra_payload}",
    )

    with open(RESULT_FILE, "w", encoding="utf-8") as fp:
        json.dump({"server_seq": server_seq, "payload": PAYLOAD}, fp)
finally:
    sender.quit()
    receiver.quit()
PY

server_seq="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["server_seq"])' "$result_file")"

mysql_scalar() {
  mysql --protocol=tcp -h"$MYSQL_HOST" -P"$MYSQL_PORT" \
    -u"$WIMI_DB_USER" -p"$WIMI_DB_PASSWORD" "$WIMI_DB" \
    -N -B -e "$1" 2>/dev/null
}

for _ in {1..10}; do
  row_count="$(mysql_scalar "SELECT COUNT(*) FROM messages WHERE messageId = $server_seq AND senderId = 1001 AND receiverId = 1002 AND conversationId > 0 AND conversationSeq > 0 AND CAST(sessionKey AS UNSIGNED) = conversationId AND content = '$payload' AND status = 3 AND readDateTime <> '';")"
  if [[ "$row_count" == "1" ]]; then
    echo "online text persisted, delivered and read ok"
    break
  fi
  sleep 1
done

if [[ "${row_count:-0}" != "1" ]]; then
  echo "online text row did not reach READ"
  mysql --protocol=tcp -h"$MYSQL_HOST" -P"$MYSQL_PORT" \
    -u"$WIMI_DB_USER" -p"$WIMI_DB_PASSWORD" "$WIMI_DB" \
    -e "SELECT * FROM messages WHERE messageId = $server_seq;" || true
  exit 1
fi

echo "text delivery smoke ok"
