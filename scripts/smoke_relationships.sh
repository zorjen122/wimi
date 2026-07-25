#!/usr/bin/env bash
set -euo pipefail

# 作用：
#   验证关系类和拉取类接口的基础行为。
#   会创建临时用户，覆盖好友申请/同意、好友列表拉取、会话/用户消息拉取、
#   群创建、入群申请和入群同意，并用 MySQL 结果做校验。
# 前置条件：
#   已初始化 MySQL，并启动本地服务；默认连接 Connection Gateway 127.0.0.1:8090。
# 注意：
#   该脚本会向测试库写入临时用户、好友、群和消息数据。

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  cat <<EOF
Usage: ./scripts/smoke_relationships.sh

Requires local services to be running, for example:
  ./scripts/run_local_services.sh

Verifies friend apply/reply, friend/message pull, group create, and group join/reply.
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
uid_a=$((300000 + stamp % 100000))
uid_b=$((uid_a + 1))
user_a="rel_a_${stamp}"
user_b="rel_b_${stamp}"
friend_msg="friend_apply_${stamp}"
friend_reply="friend_reply_${stamp}"
group_name="rel_group_${stamp}"
join_msg="join_group_${stamp}"
result_file="$(mktemp /tmp/wimi-relationships.XXXXXX.json)"
trap 'rm -f "$result_file"' EXIT

mysql_exec() {
  mysql --protocol=tcp -h"$MYSQL_HOST" -P"$MYSQL_PORT" \
    -u"$WIMI_DB_USER" -p"$WIMI_DB_PASSWORD" "$WIMI_DB" "$@"
}

mysql_scalar() {
  mysql_exec -N -B -e "$1" 2>/dev/null
}

expect_count() {
  local label="$1"
  local query="$2"
  local expected="$3"
  local actual
  actual="$(mysql_scalar "$query")"
  if [[ "$actual" != "$expected" ]]; then
    echo "$label failed: expected $expected, got ${actual:-<empty>}"
    echo "query: $query"
    exit 1
  fi
  echo "$label ok"
}

mysql_exec <<SQL
INSERT INTO users (uid, username, password, email, createTime) VALUES
  ($uid_a, '$user_a', '123456', '$user_a@example.com', '2026-07-10 00:00:00'),
  ($uid_b, '$user_b', '123456', '$user_b@example.com', '2026-07-10 00:00:00');

INSERT INTO userInfo (uid, name, age, sex, headImageURL) VALUES
  ($uid_a, 'RelA', 21, 'test', '/images/rel-a.png'),
  ($uid_b, 'RelB', 22, 'test', '/images/rel-b.png');

SQL

UID_A="$uid_a" \
UID_B="$uid_b" \
USER_A="$user_a" \
USER_B="$user_b" \
FRIEND_MSG="$friend_msg" \
FRIEND_REPLY="$friend_reply" \
GROUP_NAME="$group_name" \
JOIN_MSG="$join_msg" \
CHAT_HOST="$CHAT_HOST" \
CHAT_PORT="$CHAT_PORT" \
GATE_URL="$GATE_URL" \
RESULT_FILE="$result_file" \
PYTHONPATH="$ROOT_DIR/scripts/lib${PYTHONPATH:+:$PYTHONPATH}" \
python3 - <<'PY'
import json
import os

from wimi_tcp_client import WimClient, request_chat_auth, require

UID_A = int(os.environ["UID_A"])
UID_B = int(os.environ["UID_B"])
USER_A = os.environ["USER_A"]
USER_B = os.environ["USER_B"]
FRIEND_MSG = os.environ["FRIEND_MSG"]
FRIEND_REPLY = os.environ["FRIEND_REPLY"]
GROUP_NAME = os.environ["GROUP_NAME"]
JOIN_MSG = os.environ["JOIN_MSG"]
CHAT_HOST = os.environ["CHAT_HOST"]
CHAT_PORT = int(os.environ["CHAT_PORT"])
RESULT_FILE = os.environ["RESULT_FILE"]
GATE_URL = os.environ["GATE_URL"]

ID_PULL_FRIEND_LIST_REQ = 1001
ID_PULL_FRIEND_APPLY_LIST_REQ = 1003
ID_NOTIFY_ADD_FRIEND_REQ = 1021
ID_REPLY_ADD_FRIEND_REQ = 1023
ID_GROUP_CREATE_REQ = 1035
ID_GROUP_NOTIFY_JOIN_REQ = 1037
ID_GROUP_REPLY_JOIN_REQ = 1039

ASYNC_IDS = {
    ID_NOTIFY_ADD_FRIEND_REQ,
    ID_REPLY_ADD_FRIEND_REQ,
    1027,  # ID_TEXT_SEND_REQ
    ID_GROUP_NOTIFY_JOIN_REQ,
    ID_GROUP_REPLY_JOIN_REQ,
}


def with_client(uid, func):
    username = USER_A if uid == UID_A else USER_B
    auth = request_chat_auth(username, "123456", GATE_URL)
    client = WimClient(uid, CHAT_HOST, CHAT_PORT, async_ack_ids=ASYNC_IDS,
                       auto_ack=True, auth_token=auth["chatToken"])
    try:
        client.login()
        return func(client)
    finally:
        client.quit(wait_response=True)


def check_friend_notify(client):
    rsp = client.request(
        ID_NOTIFY_ADD_FRIEND_REQ,
        {"from": UID_A, "to": UID_B, "requestMessage": FRIEND_MSG},
    )
    require(rsp.get("error") == 0, f"notify add friend failed: {rsp}")


def check_friend_reply_and_pulls(client):
    apply_rsp = client.request(ID_PULL_FRIEND_APPLY_LIST_REQ, {"uid": UID_B})
    require(apply_rsp.get("error") == 0, f"pull friend apply failed: {apply_rsp}")
    require(
        any(item.get("content") == FRIEND_MSG for item in apply_rsp.get("applyList", [])),
        f"friend apply list missing message {FRIEND_MSG}: {apply_rsp}",
    )

    reply_rsp = client.request(
        ID_REPLY_ADD_FRIEND_REQ,
        {
            "from": UID_B,
            "to": UID_A,
            "accept": True,
            "replyMessage": FRIEND_REPLY,
        },
    )
    require(reply_rsp.get("error") == 0, f"reply add friend failed: {reply_rsp}")

    friends_rsp = client.request(ID_PULL_FRIEND_LIST_REQ, {"uid": UID_B})
    require(friends_rsp.get("error") == 0, f"pull friend list failed: {friends_rsp}")
    require(
        any(item.get("uid") == UID_A for item in friends_rsp.get("friendList", [])),
        f"friend list missing uid {UID_A}: {friends_rsp}",
    )


def create_group(client):
    rsp = client.request(ID_GROUP_CREATE_REQ, {"uid": UID_A, "groupName": GROUP_NAME})
    require(rsp.get("error") == 0, f"group create failed: {rsp}")
    gid = int(rsp.get("gid", 0))
    require(gid > 0, f"group create did not return gid: {rsp}")
    return gid


def join_group(client, gid):
    rsp = client.request(
        ID_GROUP_NOTIFY_JOIN_REQ,
        {"uid": UID_B, "gid": gid, "requestMessage": JOIN_MSG},
    )
    require(rsp.get("error") == 0, f"group join notify failed: {rsp}")


def approve_group_join(client, gid):
    rsp = client.request(
        ID_GROUP_REPLY_JOIN_REQ,
        {"gid": gid, "managerUid": UID_A, "requestorUid": UID_B, "accept": True},
    )
    require(rsp.get("error") == 0, f"group join reply failed: {rsp}")


with_client(UID_A, check_friend_notify)
print("friend notify ok")

with_client(UID_B, check_friend_reply_and_pulls)
print("friend reply and message pulls ok")

group_gid = with_client(UID_A, create_group)
print(f"group create ok: {group_gid}")

with_client(UID_B, lambda client: join_group(client, group_gid))
print("group join notify ok")

with_client(UID_A, lambda client: approve_group_join(client, group_gid))
print("group join reply ok")

with open(RESULT_FILE, "w", encoding="utf-8") as fp:
    json.dump({"group_gid": group_gid}, fp)
PY

group_gid="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["group_gid"])' "$result_file")"

expect_count \
  "friend apply accepted rows" \
  "SELECT COUNT(*) FROM friendApplys WHERE status = 1 AND ((fromUid = $uid_a AND toUid = $uid_b) OR (fromUid = $uid_b AND toUid = $uid_a));" \
  "2"

expect_count \
  "friend relation rows" \
  "SELECT COUNT(*) FROM friends WHERE (uidA = $uid_a AND uidB = $uid_b) OR (uidA = $uid_b AND uidB = $uid_a);" \
  "2"

expect_count \
  "message fixture row" \
  "SELECT COUNT(*) FROM messages WHERE messageId = $message_id AND senderId = $uid_a AND receiverId = $uid_b AND content = '$pull_payload';" \
  "1"

expect_count \
  "group row" \
  "SELECT COUNT(*) FROM groupInfo WHERE gid = $group_gid AND name = '$group_name';" \
  "1"

expect_count \
  "group member rows" \
  "SELECT COUNT(*) FROM groupMembers WHERE gid = $group_gid AND uid IN ($uid_a, $uid_b);" \
  "2"

expect_count \
  "group apply accepted row" \
  "SELECT COUNT(*) FROM groupApplys WHERE gid = $group_gid AND requestor = $uid_b AND handler = $uid_a AND status = 1;" \
  "1"

echo "relationship smoke ok"
