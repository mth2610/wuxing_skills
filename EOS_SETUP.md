# HƯỚNG DẪN CHUẨN BỊ EPIC ONLINE SERVICES (EOS) — việc của anh Thang

> EOS cho free NAT punch-through + relay + lobby trên hạ tầng Epic, mọi
> engine đều dùng được. Các bước dưới đây BẮT BUỘC chính chủ làm trên Epic
> Dev Portal — xong thì Claude tích hợp phần code.

## Bước 1 — Tài khoản & Product

1. Vào https://dev.epicgames.com/portal → đăng nhập bằng tài khoản Epic
   (tạo mới nếu chưa có, free).
2. Chấp nhận điều khoản Epic Online Services.
3. **Create Product** → đặt tên (vd `WuxingSkills`).

## Bước 2 — Lấy bộ định danh (Product Settings → SDK Credentials)

Chép đủ 5 giá trị này (sẽ dán vào `eos_keys.cfg` ở Bước 5):

- `ProductId`
- `SandboxId`  (dùng sandbox Dev)
- `DeploymentId` (deployment của sandbox Dev)
- `ClientId` + `ClientSecret` (tạo ở Bước 3)

## Bước 3 — Client & Client Policy

1. Product Settings → **Clients** → Add New Client (vd `WuxingGameClient`).
2. Tạo **Client Policy** kiểu `Peer2Peer` (hoặc Custom) với các quyền:
   - **Connect**: `createUser` (cho Device ID auth ẩn danh)
   - **P2P**: đủ read/write (relay + punch)
   - **Lobby**: create / join / search / member update
3. Gán policy đó cho client vừa tạo → chép `ClientId` / `ClientSecret`.

> Game dùng **Device ID auth** (EOS Connect) — người chơi KHÔNG cần tài
> khoản Epic, không popup đăng nhập. Không cần bật Epic Account Services.

## Bước 4 — Tải SDK

1. Dev Portal → **SDK** → Download → chọn **C SDK** (không phải phiên bản
   riêng cho UE/Unity), bản mới nhất.
2. Giải nén vào repo tại: `third_party/eos-sdk/`
   sao cho có cấu trúc:
   ```
   third_party/eos-sdk/SDK/Include/eos_sdk.h ...
   third_party/eos-sdk/SDK/Bin/libEOSSDK-Mac-Shipping.dylib   (macOS)
   third_party/eos-sdk/SDK/Bin/libEOSSDK-Linux-Shipping.so    (nếu có)
   third_party/eos-sdk/SDK/Bin/Android/...                    (sau này cho mobile)
   ```
   (Thư mục này đã nằm trong `.gitignore` — không commit SDK của Epic.)

## Bước 5 — File keys local (KHÔNG commit)

Tạo file `eos_keys.cfg` ở gốc repo (đã gitignore) theo mẫu:

```
product_id = xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
sandbox_id = xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
deployment_id = xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
client_id = xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
client_secret = xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
```

## Bước 6 — Báo Claude

Xong 5 bước trên thì nhắn "EOS sẵn sàng" — Claude sẽ:
1. Bật `WUXING_EOS=ON` trong CMake, link SDK, viết `net/net_eos.c`
   (init platform → Device ID login → tạo/join lobby bằng **mã phòng 4-6
   ký tự** → mở kênh P2P → thay tầng ENet khi chơi online).
2. Luồng người chơi cuối: host bấm "TẠO PHÒNG" → game hiện mã (vd `TX7K`)
   → bạn bè nhập mã là vào, xuyên NAT, không cần IP, không cần cài gì.
3. ENet vẫn giữ cho LAN/dev (`--host` / `--join <ip>` như cũ).
