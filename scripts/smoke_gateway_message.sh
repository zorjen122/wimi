#!/usr/bin/env bash
set -euo pipefail

# Verifies the first-phase Connection Gateway topology with G=2 and N=2.
# The test writes temporary users, friendship and message rows to chatServ.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  cat <<EOF
Usage: ./scripts/smoke_gateway_message.sh

Start the multi-node stack first:
  ./scripts/init_mysql.sh
  WIMI_STATE_CONFIG="\$PWD/server/conf/state-multi.yaml" \
  WIMI_MESSAGE_CONFIGS="\$PWD/server/conf/message-hunan-im.yaml \$PWD/server/conf/message-beijing-im.yaml" \
  WIMI_GATEWAY_CONFIGS="\$PWD/server/conf/gateway-hunan.yaml \$PWD/server/conf/gateway-beijing.yaml" \
    ./scripts/run_local_services.sh

Checks:
  - four established Gateway -> Message connections (2 x 2)
  - users on different Gateways have generation-fenced Redis leases
  - cross-Gateway direct text is accepted, sequenced and delivered
  - an identical retry returns the same message and sequence
  - a conflicting idempotency retry is rejected
  - conversation_seq sync repairs delivery state
EOF
  exit 0
fi

if [[ "$#" -gt 0 ]]; then
  echo "Unknown argument: $1" >&2
  exit 2
fi

: "${MYSQL_HOST:=127.0.0.1}"
: "${MYSQL_PORT:=3306}"
: "${WIMI_DB:=chatServ}"
: "${WIMI_DB_USER:=zorjen}"
: "${WIMI_DB_PASSWORD:=root}"
: "${WIMI_REDIS_HOST:=127.0.0.1}"
: "${WIMI_REDIS_PORT:=6380}"
: "${WIMI_REDIS_PASSWORD:=root}"
: "${GATEWAY_A_HOST:=127.0.0.1}"
: "${GATEWAY_A_PORT:=8090}"
: "${GATEWAY_B_HOST:=127.0.0.1}"
: "${GATEWAY_B_PORT:=8091}"
: "${GATEWAY_A_ID:=hunan-gateway}"
: "${GATEWAY_B_ID:=beijing-gateway}"
: "${GATE_URL:=http://127.0.0.1:18080}"
: "${MESSAGE_A_PORT:=50055}"
: "${MESSAGE_B_PORT:=50056}"
: "${EXPECTED_GATEWAY_MESSAGE_LINKS:=4}"

python3 - "$GATEWAY_A_HOST" "$GATEWAY_A_PORT" "$GATEWAY_B_HOST" "$GATEWAY_B_PORT" <<'PY'
import socket
import sys

for label, host, port in (
    ("gateway A", sys.argv[1], int(sys.argv[2])),
    ("gateway B", sys.argv[3], int(sys.argv[4])),
):
    try:
        with socket.create_connection((host, port), timeout=2):
            pass
    except OSError as exc:
        raise SystemExit(f"{label} is not reachable at {host}:{port}: {exc}")
PY

if command -v ss >/dev/null 2>&1; then
  established_links="$(ss -Htn state established 2>/dev/null | awk -v a=":$MESSAGE_A_PORT" -v b=":$MESSAGE_B_PORT" '$NF ~ (a "$") || $NF ~ (b "$") { count++ } END { print count + 0 }')"
  if [[ "$established_links" -lt "$EXPECTED_GATEWAY_MESSAGE_LINKS" ]]; then
    echo "expected at least $EXPECTED_GATEWAY_MESSAGE_LINKS Gateway -> Message TCP connections, found $established_links" >&2
    exit 1
  fi
  echo "G x N topology ok: $established_links Gateway -> Message connections"
fi

stamp="$(date +%s%N)"
uid_a=$((500000 + stamp % 100000))
uid_b=$((uid_a + 1))
friendship_id=$((940000000 + stamp % 10000000))
conversation_id=$((950000000 + stamp % 10000000))
group_id=$((700000 + stamp % 100000))
group_conversation_id=$((970000000 + stamp % 10000000))
user_a="gateway_a_${stamp}"
user_b="gateway_b_${stamp}"
payload="gateway_text_${stamp}"
client_message_id="client_${stamp}"
result_file="$(mktemp /tmp/wimi-gateway-message.XXXXXX.json)"
trap 'rm -f "$result_file"' EXIT

mysql_exec() {
  mysql --protocol=tcp -h"$MYSQL_HOST" -P"$MYSQL_PORT" \
    -u"$WIMI_DB_USER" -p"$WIMI_DB_PASSWORD" "$WIMI_DB" "$@"
}

mysql_exec <<SQL
INSERT INTO users (uid, username, password, email, createTime) VALUES
  ($uid_a, '$user_a', '123456', '$user_a@example.com', '2026-07-16 00:00:00'),
  ($uid_b, '$user_b', '123456', '$user_b@example.com', '2026-07-16 00:00:00');
INSERT INTO userInfo (uid, name, age, sex, headImageURL) VALUES
  ($uid_a, 'GatewayA', 21, 'test', '/images/gateway-a.png'),
  ($uid_b, 'GatewayB', 22, 'test', '/images/gateway-b.png');
INSERT INTO friends (friendshipId, uidA, uidB, createTime) VALUES
  ($friendship_id, $uid_a, $uid_b, '2026-07-16 00:00:00');
INSERT INTO groupInfo (groupId, name, createTime) VALUES
  ($group_id, 'GatewayGroup', '2026-07-16 00:00:00');
INSERT INTO conversations (conversationId, type, businessId, latestSeq, createTime) VALUES
  ($conversation_id, 1, $friendship_id, 0, '2026-07-16 00:00:00'),
  ($group_conversation_id, 2, $group_id, 0, '2026-07-16 00:00:00');
INSERT INTO conversationUserStates (conversationId, uid, joinedSeq) VALUES
  ($conversation_id, $uid_a, 1),
  ($conversation_id, $uid_b, 1),
  ($group_conversation_id, $uid_a, 1),
  ($group_conversation_id, $uid_b, 1);
INSERT INTO groupMembers (groupId, uid, role, joinTime, speech, memberName) VALUES
  ($group_id, $uid_a, 2, '2026-07-16 00:00:00', 0, 'GatewayA'),
  ($group_id, $uid_b, 0, '2026-07-16 00:00:00', 0, 'GatewayB');
SQL

UID_A="$uid_a" UID_B="$uid_b" USER_A="$user_a" USER_B="$user_b" \
CONVERSATION_ID="$conversation_id" PAYLOAD="$payload" \
GROUP_ID="$group_id" GROUP_CONVERSATION_ID="$group_conversation_id" \
CLIENT_MESSAGE_ID="$client_message_id" GATEWAY_A_HOST="$GATEWAY_A_HOST" \
GATEWAY_A_PORT="$GATEWAY_A_PORT" GATEWAY_B_HOST="$GATEWAY_B_HOST" \
GATEWAY_B_PORT="$GATEWAY_B_PORT" GATEWAY_A_ID="$GATEWAY_A_ID" \
GATEWAY_B_ID="$GATEWAY_B_ID" GATE_URL="$GATE_URL" \
WIMI_REDIS_HOST="$WIMI_REDIS_HOST" WIMI_REDIS_PORT="$WIMI_REDIS_PORT" \
WIMI_REDIS_PASSWORD="$WIMI_REDIS_PASSWORD" RESULT_FILE="$result_file" \
PYTHONPATH="$ROOT_DIR/scripts/lib${PYTHONPATH:+:$PYTHONPATH}" \
python3 - <<'PY'
import json
import os
import subprocess

from wimi_tcp_client import WimClient, request_chat_auth, require

ID_PULL_SESSION_MESSAGE_LIST_REQ = 1005
ID_TEXT_SEND_REQ = 1027
ID_GROUP_TEXT_SEND_REQ = 1043

uid_a = int(os.environ["UID_A"])
uid_b = int(os.environ["UID_B"])
conversation_id = int(os.environ["CONVERSATION_ID"])
group_id = int(os.environ["GROUP_ID"])
group_conversation_id = int(os.environ["GROUP_CONVERSATION_ID"])
payload = os.environ["PAYLOAD"]
client_message_id = os.environ["CLIENT_MESSAGE_ID"]


def lease(uid, device_id):
    value = subprocess.check_output(
        [
            "redis-cli", "-h", os.environ["WIMI_REDIS_HOST"],
            "-p", os.environ["WIMI_REDIS_PORT"],
            "-a", os.environ["WIMI_REDIS_PASSWORD"],
            "GET", f"im:session:{uid}:{device_id}",
        ],
        stderr=subprocess.DEVNULL,
        text=True,
    ).strip()
    require(value, f"missing session lease for {uid}")
    return json.loads(value)


auth_a = request_chat_auth(os.environ["USER_A"], "123456", os.environ["GATE_URL"])
auth_b = request_chat_auth(os.environ["USER_B"], "123456", os.environ["GATE_URL"])
a = WimClient(uid_a, os.environ["GATEWAY_A_HOST"],
              int(os.environ["GATEWAY_A_PORT"]), auth_token=auth_a["chatToken"])
b = WimClient(uid_b, os.environ["GATEWAY_B_HOST"],
              int(os.environ["GATEWAY_B_PORT"]), auth_token=auth_b["chatToken"])
a_secondary = WimClient(
    uid_a, os.environ["GATEWAY_B_HOST"], int(os.environ["GATEWAY_B_PORT"]),
    auth_token=auth_a["chatToken"])
b_secondary = WimClient(
    uid_b, os.environ["GATEWAY_A_HOST"], int(os.environ["GATEWAY_A_PORT"]),
    auth_token=auth_b["chatToken"])

try:
    a.login(init=False)
    b.login(init=False)
    a_secondary.login(init=False)
    b_secondary.login(init=False)
    lease_a = lease(uid_a, a.device_id)
    lease_b = lease(uid_b, b.device_id)
    lease_a_secondary = lease(uid_a, a_secondary.device_id)
    lease_b_secondary = lease(uid_b, b_secondary.device_id)
    require(lease_a["gatewayId"] == os.environ["GATEWAY_A_ID"], lease_a)
    require(lease_b["gatewayId"] == os.environ["GATEWAY_B_ID"], lease_b)
    require(lease_a_secondary["gatewayId"] == os.environ["GATEWAY_B_ID"],
            lease_a_secondary)
    require(lease_b_secondary["gatewayId"] == os.environ["GATEWAY_A_ID"],
            lease_b_secondary)
    require(int(lease_a["generation"]) > 0 and int(lease_b["generation"]) > 0,
            "lease generation was not allocated")

    command = {
        "seq": int(client_message_id.split("_")[-1]) % 9_000_000_000 + 1,
        "to": uid_b,
        "data": payload,
        "conversationId": conversation_id,
        "clientMessageId": client_message_id,
    }
    accepted = a.request(ID_TEXT_SEND_REQ, command)
    require(accepted.get("error") == 0, accepted)
    require(accepted.get("messageState") == 1, accepted)
    require(accepted.get("conversationId") == conversation_id, accepted)
    require(accepted.get("conversationSeq", 0) > 0, accepted)

    pushed = b.expect_async(
        ID_TEXT_SEND_REQ,
        lambda item: item.get("clientMessageId") == client_message_id,
    )
    require(pushed.get("messageId") == accepted.get("messageId"), pushed)
    require(pushed.get("conversationSeq") == accepted.get("conversationSeq"), pushed)
    pushed_secondary = b_secondary.expect_async(
        ID_TEXT_SEND_REQ,
        lambda item: item.get("clientMessageId") == client_message_id,
    )
    sender_secondary = a_secondary.expect_async(
        ID_TEXT_SEND_REQ,
        lambda item: item.get("clientMessageId") == client_message_id,
    )
    require(pushed_secondary.get("messageId") == accepted.get("messageId"),
            pushed_secondary)
    require(sender_secondary.get("messageId") == accepted.get("messageId"),
            sender_secondary)
    b.ack(
        pushed["seq"], receipt_type=1,
        conversation_id=conversation_id,
        conversation_seq=pushed["conversationSeq"],
    )
    b_secondary.ack(
        pushed_secondary["seq"], receipt_type=1,
        conversation_id=conversation_id,
        conversation_seq=pushed_secondary["conversationSeq"],
    )
    a_secondary.ack(
        sender_secondary["seq"], receipt_type=1,
        conversation_id=conversation_id,
        conversation_seq=sender_secondary["conversationSeq"],
    )
    a_secondary.quit(wait_response=True)
    b_secondary.quit(wait_response=True)

    duplicate = a.request(ID_TEXT_SEND_REQ, command)
    require(duplicate.get("error") == 0, duplicate)
    require(duplicate.get("messageId") == accepted.get("messageId"), duplicate)
    require(duplicate.get("conversationSeq") == accepted.get("conversationSeq"), duplicate)

    # Reuse the same sender/client_message_id while changing the content. This
    # proves the Message node compares the persisted command hash instead of
    # treating every duplicate idempotency key as a successful replay. Such a
    # conflict is deterministic, so the Gateway/client must not retry it.
    conflict = dict(command)
    conflict["data"] = payload + "_conflict"
    conflict_rsp = a.request(ID_TEXT_SEND_REQ, conflict)
    require(conflict_rsp.get("error", 0) != 0, conflict_rsp)
    require(not conflict_rsp.get("retryable", True), conflict_rsp)

    sync = b.request(
        ID_PULL_SESSION_MESSAGE_LIST_REQ,
        {"conversationId": conversation_id, "afterSeq": 0, "limit": 20},
    )
    require(sync.get("error") == 0, sync)
    matches = [item for item in sync["messageList"]
               if item["messageId"] == accepted["messageId"]]
    require(len(matches) == 1, sync)
    require(matches[0]["conversationSeq"] == accepted["conversationSeq"], sync)

    group_first = a.request(
        ID_GROUP_TEXT_SEND_REQ,
        {
            "seq": command["seq"] + 1,
            "groupId": group_id,
            "data": payload + "_group_a",
            "conversationId": group_conversation_id,
            "clientMessageId": client_message_id + "_group_a",
        },
    )
    require(group_first.get("error") == 0, group_first)
    require(group_first.get("conversationSeq") == 1, group_first)
    group_push_b = b.expect_async(
        ID_GROUP_TEXT_SEND_REQ,
        lambda item: item.get("clientMessageId") == client_message_id + "_group_a",
    )
    b.ack(group_push_b["seq"], conversation_id=group_conversation_id,
          conversation_seq=group_push_b["conversationSeq"])

    group_second = b.request(
        ID_GROUP_TEXT_SEND_REQ,
        {
            "seq": command["seq"] + 2,
            "groupId": group_id,
            "data": payload + "_group_b",
            "conversationId": group_conversation_id,
            "clientMessageId": client_message_id + "_group_b",
        },
    )
    require(group_second.get("error") == 0, group_second)
    require(group_second.get("conversationSeq") == 2, group_second)
    group_push_a = a.expect_async(
        ID_GROUP_TEXT_SEND_REQ,
        lambda item: item.get("clientMessageId") == client_message_id + "_group_b",
    )
    a.ack(group_push_a["seq"], conversation_id=group_conversation_id,
          conversation_seq=group_push_a["conversationSeq"])

    group_sync = a.request(
        ID_PULL_SESSION_MESSAGE_LIST_REQ,
        {"conversationId": group_conversation_id, "afterSeq": 0, "limit": 20},
    )
    require(group_sync.get("error") == 0, group_sync)
    require([item["conversationSeq"] for item in group_sync["messageList"]]
            == [1, 2], group_sync)

    with open(os.environ["RESULT_FILE"], "w", encoding="utf-8") as output:
        json.dump(accepted, output)
finally:
    a.quit()
    b.quit()
    a_secondary.close()
    b_secondary.close()
PY

message_id="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["messageId"])' "$result_file")"
conversation_seq="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["conversationSeq"])' "$result_file")"

row_count="$(mysql_exec -N -B -e "SELECT COUNT(*) FROM messages WHERE senderId=$uid_a AND clientMessageId='$client_message_id' AND messageId=$message_id AND conversationSeq=$conversation_seq;" 2>/dev/null)"
if [[ "$row_count" != "1" ]]; then
  echo "persistent idempotency failed: expected one row, got $row_count" >&2
  exit 1
fi

echo "cross-Gateway persistent message closure ok: message=$message_id seq=$conversation_seq"
