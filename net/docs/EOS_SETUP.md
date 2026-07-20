# Epic Online Services (EOS) Setup Guide — human-only steps

> EOS provides free NAT punch-through + relay + lobby on Epic's infrastructure,
> usable by any engine. The steps below MUST be done by a human on the Epic
> Dev Portal — once done, Claude integrates the code side.

## Step 1 — Account & Product

1. Go to https://dev.epicgames.com/portal → sign in with an Epic account
   (create one if needed, free).
2. Accept the Epic Online Services terms.
3. **Create Product** → name it (e.g. `WuxingSkills`).

## Step 2 — Get the credential set (Product Settings → SDK Credentials)

Copy these 5 values (they go into `eos_keys.cfg` in Step 5):

- `ProductId`
- `SandboxId` (use the Dev sandbox)
- `DeploymentId` (the Dev sandbox's deployment)
- `ClientId` + `ClientSecret` (created in Step 3)

## Step 3 — Client & Client Policy

1. Product Settings → **Clients** → Add New Client (e.g. `WuxingGameClient`).
2. Create a **Client Policy** of type `Peer2Peer` (or Custom) with these
   permissions:
   - **Connect**: `createUser` (for anonymous Device ID auth)
   - **P2P**: full read/write (relay + punch)
   - **Lobby**: create / join / search / member update
3. Assign that policy to the client you just created → copy `ClientId` /
   `ClientSecret`.

> The game uses **Device ID auth** (EOS Connect) — players do NOT need an
> Epic account and get no login popup. Epic Account Services does not need
> to be enabled.

## Step 4 — Download the SDK

1. Dev Portal → **SDK** → Download → choose the **C SDK** (not the
   UE/Unity-specific version), latest release.
2. Unpack it into the repo at `third_party/eos-sdk/` so the layout is:
   ```
   third_party/eos-sdk/SDK/Include/eos_sdk.h ...
   third_party/eos-sdk/SDK/Bin/libEOSSDK-Mac-Shipping.dylib   (macOS)
   third_party/eos-sdk/SDK/Bin/libEOSSDK-Linux-Shipping.so    (if available)
   third_party/eos-sdk/SDK/Bin/Android/...                    (mobile, later)
   ```
   (This directory is already in `.gitignore` — never commit Epic's SDK.)

## Step 5 — Local keys file (DO NOT commit)

Create `eos_keys.cfg` at the repo root (already gitignored) with this
template:

```
product_id = xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
sandbox_id = xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
deployment_id = xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
client_id = xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
client_secret = xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
```

## Step 6 — Build & run

```bash
# macOS: the downloaded dylib is Gatekeeper-quarantined — clear it ONCE:
xattr -d com.apple.quarantine third_party/eos-sdk/SDK/Bin/libEOSSDK-Mac-Shipping.dylib
codesign --force --sign - third_party/eos-sdk/SDK/Bin/libEOSSDK-Mac-Shipping.dylib

cmake -S . -B build -DWUXING_EOS=ON
cmake --build build -j4

# Option 1 — UI: run ./build/wuxing, from the main menu pick
#   "4. CREATE ONLINE ROOM"  → join code shown large on screen during the match
#   "5. ENTER ROOM CODE"     → type the 5-character code → ENTER
./build/wuxing

# Option 2 — CLI (dev): host prints the code to the terminal, client joins with it:
./build/wuxing --host-online
./build/wuxing --join-online <CODE>
```

ENet is still used for LAN/dev (`--host` / `--join <ip>` as before).

### Debugging / testing on ONE machine

- `WUXING_EOS_VERBOSE=1` — turns on the SDK's detailed logs when auth/lobby
  misbehaves.
- `WUXING_EOS_FRESH_DEVICE=1` — discards the machine's device id so the next
  login creates a NEW anonymous user. Required when testing host+join on the
  same machine: Epic's lobby search HIDES lobbies the searching user is
  already a member of, so two instances sharing a device id would never see
  each other's room.
  ```bash
  ./build/wuxing --host-online                                  # window 1 → prints the code
  WUXING_EOS_FRESH_DEVICE=1 ./build/wuxing --join-online <CODE> # window 2
  ```

See `docs/PROGRESS.md` for landing/verification status and `docs/LANDMINES.md`
for the macOS quarantine/CFRunLoop lessons hit while integrating this.
