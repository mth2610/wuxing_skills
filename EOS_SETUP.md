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

## Bước 6 — Build & chạy (ĐÃ XONG — net/net_eos.c landed 07/2026)

```bash
# macOS: dylib tải về bị Gatekeeper chặn (quarantine) — gỡ MỘT LẦN:
xattr -d com.apple.quarantine third_party/eos-sdk/SDK/Bin/libEOSSDK-Mac-Shipping.dylib
codesign --force --sign - third_party/eos-sdk/SDK/Bin/libEOSSDK-Mac-Shipping.dylib

cmake -S . -B build -DWUXING_EOS=ON
cmake --build build -j4

# Cách 1 — GIAO DIỆN: chạy ./build/wuxing, ở menu chính bấm
#   "4. TAO PHONG ONLINE"  → mã phòng hiện to giữa màn hình trong trận
#   "5. NHAP MA VAO PHONG" → gõ mã 5 ký tự → ENTER
./build/wuxing

# Cách 2 — CLI (dev): host in mã ra terminal, khách join bằng mã:
./build/wuxing --host-online
./build/wuxing --join-online <MÃ>
```

ENet vẫn giữ cho LAN/dev (`--host` / `--join <ip>` như cũ).

### Debug / test trên MỘT máy

- `WUXING_EOS_VERBOSE=1` — mở log chi tiết của SDK khi auth/lobby trục trặc.
- `WUXING_EOS_FRESH_DEVICE=1` — xoá device id của máy để lần login sau tạo
  user ẩn danh MỚI. Bắt buộc khi tự test host+join trên cùng một máy:
  lobby search của Epic ẨN các lobby mà chính user đang ở trong, nên hai
  instance dùng chung device id sẽ không bao giờ thấy phòng của nhau.
  ```bash
  ./build/wuxing --host-online                                  # cửa sổ 1 → in mã
  WUXING_EOS_FRESH_DEVICE=1 ./build/wuxing --join-online <MÃ>   # cửa sổ 2
  ```

Đã kiểm chứng end-to-end 2026-07-12: hai instance qua lobby + P2P thật của
Epic, host spawn hero cho khách, phiên P2P giữ ổn định không rớt.
