# Public Protocols

`server/public/proto` is the canonical home for C++ service protocols shared by
more than one module.

- `tcp_message.proto`: TLV body payload used by chat TCP clients and servers.
- `gateway_message.proto`: bidirectional Connection Gateway-to-Message stream.
- `file.proto`: chat-to-file gRPC upload/send RPC.
- `state.proto`: Auth Gate placement and versioned Message topology RPCs.

The C++ build generates these files once in `server/public` as the `imProto`
target. Service targets depend on `imPublic`/`imProto` instead of generating
their own copies. Email verification is an in-process Gate capability and does
not require an internal RPC protocol.
