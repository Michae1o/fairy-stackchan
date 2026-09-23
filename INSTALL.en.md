# INSTALL — from zero to talking (standard SOP)

[中文](INSTALL.md) | **English**

> This document is **the single source of truth for installing**. Do it in order; every step tells
> you *how to know it worked*.
> ★ **The section order = the order you work in**: §1 server (line 1, no device needed) →
> §2 firmware (line 2) → §3 joining them up (line 3). Overview in §0.7.
> ⛔ Don't skip steps: skipping doesn't error immediately, it blows up much later
> (the doc marks "what happens if you do this out of order").
>
> This repo gives you **changed sources + scripts**, not a ready-to-use whole project —
> you need a copy of the upstream project and then overlay these changes.
> The three repositories and how they relate: §0.5.

---

## §0 Prerequisites

### 0.1 Hardware

- A **StackChan kit** (M5Stack **CoreS3** main unit)
- A **USB‑C data cable** (one that carries data, not charge-only)
- **2.4 GHz WiFi** (ESP32 does not support 5 GHz)
- A computer (Windows / macOS / Linux all work; this doc uses Windows as the example)

### 0.2 Software

| What to install | Version | What it's for |
|---|---|---|
| **ESP-IDF** | **v6.1** (the version this project was tested with; v5.x is theoretically OK but untested) | Building the firmware |
| Python | 3.10+ | Running the scripts in `tools/` |
| **Pillow** | any recent version (`python3 -m pip install pillow`) | **Required to generate emote assets (`make_face.py`)**. ★ Install it into **the python you actually run the scripts with**: after activating ESP‑IDF, `python` is IDF's own bundled environment, which is *not* the same one as your system python |
| **esptool** | **v5** (`python -m esptool` works) | Flashing (command-line route) |
| GPT‑SoVITS | any recent version | Voice (optional, see §1.5) |

See the official docs for installing ESP‑IDF (on Windows the official installer is the easiest route).
After installing, `idf.py --version` must print a version number in a **new** terminal.

### 0.3 Network

- Where this project needs the network: ① downloading the three repos ② ESP‑IDF downloading
  managed components on the first build (`managed_components/`, see the error notes in §2.4)
  ③ installing the server's dependencies
- **If GitHub is unreachable** (common in some regions) use one of these:

```bash
# Option A: go through a proxy (replace the port with your own)
git -c http.proxy=http://127.0.0.1:<your proxy port> clone https://github.com/78/xiaozhi-esp32.git

# Option B: download the tarball directly (no git needed)
curl -L -o xiaozhi-esp32.tar.gz https://codeload.github.com/78/xiaozhi-esp32/tar.gz/refs/heads/main
```

### 0.4 Time and disk

- Disk: **≈6 GB** (IDF toolchain ~2 GB + the two upstream source trees + the `build/` directory)
- First build: **15–25 minutes** (later builds 2–4 minutes)
- First dependency install for the server: 5–15 minutes

### 0.5 Three repositories — don't mix them up (★ the most common mistake)

| Repository | What it is | What you do with it |
|---|---|---|
| **This repo** `fairy-stackchan` | **Changes only** (source files + scripts + docs) | `git clone` it; paths in commands are relative to it |
| Upstream **firmware** `78/xiaozhi-esp32` | A complete, buildable firmware project | **Overlay** this repo's firmware changes onto it (§2.2) |
| Upstream **server** `xinnan-tech/xiaozhi-esp32-server` | A complete, runnable server | **Overlay** this repo's server changes onto it (§1.2) |

> ⚠️ Anywhere the docs write `xiaozhi-esp32/...` they mean the **upstream firmware**;
> `tools/...` and `firmware/...` mean **this repo**. `cd` into your clone of this repo first.

**★ Which upstream version this package is based on (two of them — don't mix them up)**

| Purpose | Upstream | Version | Date |
|---|---|---|---|
| ★★ **Tested on the author's own device** (use this for a 100% reproduction) | firmware `78/xiaozhi-esp32` | **`5d54beb7`** | 2026-09-15 |
| | server `xinnan-tech/xiaozhi-esp32-server` | **`4745186c`** | 2026-09-17 |
| **Compile-verification** baseline for this package's changes (built from a clean upstream, **no real hardware**) | firmware | **`4632dc51f`** | 2026-09-20 |
| | server | **`788f5301f`** | 2026-09-21 |
| **ESP‑IDF** | | **v6.1** | — |

The two rows differ very little: between those two firmware versions upstream only added one test
file (`scripts/tests/test_ogg_demuxer.py`); between the two server versions only 5 `tests/test_*.py`
files appeared. **The actual code is essentially the same.**
⇒ This package's patches apply on both (the scripts check every anchor and report any that miss,
naming the file).

**How to pin a version** (replace `<sha>` with a value from the table)

```bash
# firmware
curl -L -o xiaozhi-esp32.tar.gz https://codeload.github.com/78/xiaozhi-esp32/tar.gz/5d54beb7
tar -xzf xiaozhi-esp32.tar.gz          # extracts to a xiaozhi-esp32-5d54beb7... directory
# same idea for the server: https://codeload.github.com/xinnan-tech/xiaozhi-esp32-server/tar.gz/4745186c
```

**What if upstream moves on** (two options, pick one)

- **Stay on the latest main**: just run `tools/apply_to_upstream.py` / `apply_to_server.py` as usual.
  They report every step — anything that doesn't apply is listed with its filename, so you can
  compare by hand (the places to change are still the same: `main/CMakeLists.txt` in §2.2 and the
  server anchors in §1.2).
- **Just want stability**: use the ★★ row above (the version tested on the author's device) and
  don't follow the latest main.

> ℹ️ This package's own version = the newest commit on the repo's `main` (this page last updated
> 2026-09-22). When upstream updates again, this table will follow; you can also compare the shas
> above yourself.

### 0.6 Two shortcuts (skippable)

- Don't want to draw emotes yourself ⇒ generate them with one command: `tools/make_face.py` (§2.3)
- Don't want to build firmware ⇒ flash the prebuilt firmware from Releases, see
  [`firmware-bin/README.md`](firmware-bin/README.md)
  ⚠️ but **you still need the server first** (§1) — "no compile" skips only the compile, not the server

---

### 0.7 ★ Recommended order of work (three lines — follow this order, don't skip)

> **The order isn't arbitrary: server first, device second.**
> The server line **needs no device and no compiling to verify**, so mistakes are cheap;
> the device line takes ten-plus minutes per build and flashing is irreversible — so getting the
> server working first eliminates most misjudgements.
>
> ★ Principle: **get it working first, then swap in your own parts** — first run the whole link
> with upstream defaults, then replace them one at a time (ASR / LLM / TTS) so problems stay easy
> to localise.
>
> ★★ **Want to see whether this whole thing even runs on your machine?** One command:
> ```bash
> bash tools/repro_all.sh --all            # download fresh upstream → overlay both sides → generate assets
>                                          # → build firmware → start server and check /admin → print criteria
> bash tools/repro_all.sh --all --no-build # same but without the build (saves ten-plus minutes)
> ```
> It overlays **only files from this repo** onto a fresh upstream, then prints which criteria passed
> and which failed; all logs stay in the working directory (default `./repro-work`).
> On Windows, run it in WSL or Git Bash.

**Line 1: server (do this first — no device involved)**

```text
① Fetch the server source + install its dependencies (§1.1)
② Apply the server changes: tools/apply_to_server.py (§1.2)
③ Configure: ★ first get it running with 【upstream defaults】 — local ASR (no key needed),
     don't hook up your own voice yet; fill in your own LLM key (§1.3)
④ Start it: python app.py ⇒ /admin returns 200 (§1.4)
⑤ ★★ The criterion for line 1 (without flashing anything): run the repo's simulated-device script
     python3 tools/test_server_e2e.py --audio ref.wav
     ⇒ four ✅ means it works: server hello | ASR text | LLM answer | TTS audio frames > 0
⑥ Swap in your voice (GPT-SoVITS on :9880, §1.5) ⇒ run ⑤ again
     ⇒ only after this passes is "the server line" really working
```

**Line 2: firmware (only touch the device once the server works)**

```text
⑦ Back up the factory firmware first (★ the only irreversible step — don't skip it):
     python -m esptool --chip esp32s3 -p <port> read-flash 0 0x1000000 factory-backup.bin
     criterion: you get a 16,777,216-byte file; keep it safe
⑧ Install ESP-IDF v6.1 (§2.4.0) ⇒ `idf.py --version` prints a version in a new terminal
⑨ Get this package + the upstream firmware (§2.1); copy it to a 【separate working copy】 to modify/build (§2.4 rule 1)
⑩ Apply the firmware changes: python3 tools/apply_to_upstream.py <upstream copy> (§2.2)
⑪ Generate assets: tools/make_face.py --out fairy-assets
     ⇒ tools/verify_artifact.py --gif-dir fairy-assets (§2.3)
⑫ Build: set-target → build → merge-bin (§2.4)
     the criterion must include **build/xiaozhi.bin exists**
⑬ Verify the artifact: tools/verify_artifact.py build/merged-binary.bin (§2.4)
⑭ Flash (§2.5) ⇒ the serial log shows `WS: Connecting to ws://…` (§2.6)
```

**Line 3: joining them up (point the device at your server)**

```text
⑮ Make the device find your server (§3, two ways in):
     · universal firmware ⇒ device setup page → "Advanced" → "Custom OTA URL":
                    http://<server IP>:<http_port>/xiaozhi/ota/    (default 8003)
     · address compiled into the firmware (§2.4.2) ⇒ **just do WiFi setup**, leave the setup page blank
⑯ End-to-end run: say the wake word → talk ⇒ the on-screen face reacts and the speaker uses your voice
```

**Why this order**

- **Line 1 needs neither the device nor compiling** ⇒ clean up the server side first;
  otherwise, after flashing, the device can't connect and you'll suspect the firmware (the most
  common misdiagnosis).

- **⑪ must come before ⑫**: assets are packed into the `assets` partition **at build time**, so
  changing assets after building means building again.
- **⑦ comes first, always**: it's the only irreversible step in the whole flow.
- **Don't build for every tiny change**: the first build takes 15–25 minutes, later ones 2–4
  ⇒ batch your changes and build once.

**★ Five hard-won rules (following them saves hours)**

```text
1. Don't build directly in a source tree you want to keep
   Building creates/modifies sdkconfig, build/, managed_components/
   ⇒ copy it out and build there (e.g. cp -r xiaozhi-esp32 xiaozhi-esp32-build)

2. Copy sdkconfig.defaults* FIRST, then delete sdkconfig and let it regenerate
   Wrong order ⇒ your preset options don't take effect (classic symptom: app partition is too small)

3. Use `idf.py set-target esp32s3` + write CONFIG_BOARD_TYPE_M5STACK_CORE_S3=y by hand
   ⛔ Don't use upstream's scripts/build.py directly: it reads board_type from the board's
      config.json, and the sanitized config doesn't have that key ⇒ it errors with
      board_type not found

4. The criterion must include 【the artifact file exists】 (build/xiaozhi.bin)
   "✅ build complete" alone can be fooled by "it says success but produced nothing"

5. If it won't build, check for zombie processes first: `idf.py` / `ninja` / `cc1plus`
   fighting over sdkconfig
   ⇒ symptom: "stuck in the configure stage, 0 processes, no error"
   ⇒ fix: kill them (pkill -9 -f idf.py / ninja / cc1plus), then restore from sdkconfig.old
   ⇒ remember: **0 processes + no error = interrupted, not failed**; don't reinstall the toolchain
```

## §1 Line 1: server (`xinnan-tech/xiaozhi-esp32-server`) ★ do this first, no device involved

> This line can be fully verified **without a device and without compiling** — it's the first half
> of "get it working, then swap in your own parts".

For the device to hold a conversation you need a server: **speech recognition → the brain
(DeepSeek) → the voice (TTS)** all live there.

### 1.1 Get the source + install dependencies

```bash
git clone https://github.com/xinnan-tech/xiaozhi-esp32-server.git
cd xiaozhi-esp32-server/main/xiaozhi-server
pip install -r requirements.txt -i https://pypi.tuna.tsinghua.edu.cn/simple
```

> If PyPI is slow or times out, switch mirrors (the above uses the Tsinghua mirror). Once it's done,
> `python -c "import app"` must not raise.

### 1.2 Overlay this repo's server changes

```bash
cd fairy-stackchan
python3 tools/apply_to_server.py <upstream server directory>
```

**10 files changed + 2 in-place wiring points** (checked item by item against upstream `main`):

| Category | Files |
|---|---|
| **New** (6 — upstream has no such files) | `core/api/admin_handler.py` (console backend), `core/api/admin_page.html` (desktop `/admin`), `core/api/admin_mobile.html` (mobile `/m`), `core/api/device_registry.py` (device connection registry), `core/api/chat_llm.py` (console web chat), `core/utils/chat_log.py` (chat log + status light) |
| **Overwrite** (4 — upstream has them, this project changed them) | `core/api/ota_handler.py`, `core/utils/dialogue.py`, `core/handle/sendAudioHandle.py`, `core/providers/tts/gpt_sovits_v2.py` |
| **In-place wiring** (2 — the script inserts a few lines) | `core/http_server.py` (mounts the `/admin` routes), `core/connection.py` (register/unregister connections) |

⛔ Skip the two wiring points and it's broken: `/admin` returns 404, or the console opens but shows
no device and commands do nothing. The wiring code and its criteria (6 strings) are in
[`server/README.md`](server/README.md) §①-2.

What each file does: [`server/README.md`](server/README.md) (Chinese).

**Criterion**: the script prints **13/13 self-checks passed**; the 4 overwritten files are all
**additions only, nothing removed**.
⛔ Overwriting replaces the whole file (+14 to +80 lines), so after an upstream update you merge by
hand — if upstream differs a lot the script warns you; don't force it.

> ⚠️ Installing only part of it causes cascading errors: `admin_handler.py` itself imports `chat_log`
> / `chat_llm`, and without them the console returns 500. If you install, install all 10.

> ★★ **The console needs no separate setup** — it comes from this very step:
> `core/api/admin_page.html` (desktop `/admin`) + `core/api/admin_mobile.html` (mobile `/m`) are new
> files, and the wiring step (`admin_handler.register(app)` in `http_server.py`) mounts both routes
> automatically when the server starts. ⇒ **Apply this patch, start the server, and the console is
> there** — no extra install step. How to use it (8 tabs / mobile differences / FAQ / callable APIs):
> [`CONSOLE.md`](CONSOLE.md) (Chinese).

### 1.3 Configure `config.yaml`

> ★★ **You must create `data/.config.yaml` first** — upstream **checks for this file on startup**,
> and without it you get `FileNotFoundError: 找不到 data/.config.yaml` (it is *not* "just edit
> config.yaml").

```bash
cd <upstream server>/main/xiaozhi-server
mkdir -p data                                   # Windows: mkdir data
cp config.yaml data/.config.yaml                # Windows: copy config.yaml data\.config.yaml
# from here on, 【you edit data/.config.yaml】 — that copy
```

Fill in these in `data/.config.yaml`:

```yaml
server:
  ip: 0.0.0.0
  port: 8000                 # the device connects over WebSocket on this
  http_port: 8003            # OTA / console use this one
  auth_key: <a random string of 32+ chars>   # ★ required, otherwise the vision endpoint's auth
                                             #   randomises itself after each restart
                                             #   generate: python -c "import secrets; print(secrets.token_hex(32))"

LLM:
  DeepSeekLLM:
    api_key: <your DeepSeek key>
    base_url: https://api.deepseek.com
    model_name: deepseek-chat

TTS:
  GPTSoVITS:                 # voice (see §1.5)
    api_url: http://127.0.0.1:9880
```

(Key names follow the upstream version you got; the above are the names the author uses.
★ The `LLM` provider name must match the field name in upstream's `config.yaml`; if it doesn't,
your startup log will show 「配置错误: LLM 的 API key 未设置」.)

### 1.4 Start it + criteria

```bash
cd <upstream server>/main/xiaozhi-server
python app.py
```

**Success criteria** (all four must pass for the server to count as OK):

1. Console `http://<server IP>:8003/admin` → **HTTP 200**, page title `Fairy 控制台`
2. Mobile `http://<server IP>:8003/m` → **200**
3. `http://<server IP>:8003/admin/api/state` → **200** (returns JSON)
   — a 200 here means both "chat log + device registry" are wired correctly
4. ★★ **The whole voice link** (no device needed):

```bash
python3 tools/test_server_e2e.py --audio <a 5–10 s human-speech wav> \
        --url ws://<server IP>:8000/xiaozhi/v1/
```

It **simulates a device**: handshake → send audio → check whether the server replies with
`ASR text` / `LLM answer` / `TTS audio frames`. Four ✅ means this line works:

```text
server hello ✅ | ASR text ✅ | LLM answer ✅ | TTS frames > 0 ✅
```

If it fails, it tells you which link to check (ASR config / LLM key / voice service).
**Don't go flash firmware with an unverified server** — you'll end up blaming the firmware.

> **How to use the console** (what each of the 9 desktop tabs does, how mobile `/m` differs,
> "takes effect live vs needs a restart", FAQ, callable APIs) ⇒ [`CONSOLE.md`](CONSOLE.md) (Chinese)

### 1.5 Last step of line 1: bring in your own voice (GPT‑SoVITS)

This repo does **not** ship reference audio or fine-tune weights (the audio's rights belong to the
speaker; weights are big files).

- **Zero-shot cloning (no training)**: 3–10 seconds of clean speech as the reference, run the
  GPT‑SoVITS API (default :9880), and point §1.3 at it. How to record/cut the reference audio and
  the trade-offs of each route ⇒ [`voice-package/README.md`](voice-package/README.md) (Chinese)
- **Fine-tuning (advanced)**: train on your own dataset; this project's approach and parameters are
  in the same document
- The server also has a **Fairy tone post-processing** layer (warmth / de-harshness / pitch drop /
  speed), **all off by default** — turn it on in `gpt_sovits_v2.py` if you want it

### 1.6 Optional: speaker recognition (knows *who* is speaking)

**Not required** — everything works without it, it just can't tell **who** is talking.
With it, every utterance is matched against registered voiceprints and the speaker's name
is added to the context.

- Needs a **separate voiceprint service** (not in upstream):
  [`xinnan-tech/voiceprint-api`](https://github.com/xinnan-tech/voiceprint-api) (3D-Speaker) on port **8005**.
  ★ This project patches it from **MySQL to SQLite**; the patch lives in `server/third-party-patches/`
  (**without it, every entry in the console shows "not enrolled"**)
- Put `voiceprint.url` (with the key) + `speakers` (format `"id,name,description"`) into `data/.config.yaml`
- Then use the console's **🗣️** tab: "add speaker" + "record 6 seconds"

★ **Full steps: the "Speaker recognition" section of [`server/README.md`](server/README.md)** (Chinese)
— it includes a criterion for each step, FAQ, and why the microphone only works on localhost.

---

## §2 Line 2: firmware (touch the device only once the server works)

### 2.1 Get the three repositories

```bash
git clone <this repo>  fairy-stackchan
git clone https://github.com/78/xiaozhi-esp32.git          # upstream firmware
# the server comes later (§1.1)
```

### 2.2 Overlay this repo's firmware changes onto upstream (★ use the script, don't hand-copy)

```bash

cd fairy-stackchan
python3 tools/apply_to_upstream.py <upstream xiaozhi-esp32 directory>
```

**What the script does** (= steps ①–⑨ below; doing it by hand is the same list):

```text
① firmware/board-core-s3/*      → xiaozhi-esp32/main/boards/m5stack/core-s3/
② firmware/board-common/*       → xiaozhi-esp32/main/boards/common/
③ firmware/components/*         → xiaozhi-esp32/components/
④ firmware/ota.cc               → xiaozhi-esp32/main/ota.cc
⑤ firmware/display/*            → xiaozhi-esp32/main/display/
⑥ append the wake word "Hi Fairy" to sdkconfig.defaults.esp32s3 (upstream only ships 「你好小智」)
⑦ edit main/CMakeLists.txt: list the sub-directory sources explicitly + PRIV_REQUIRES + set the emoji collection to fairy
⑧ edit scripts/build_default_assets.py: add the fairy emoji-collection branch
⑨ create fairy-assets/: write the emotion alias table (★ you supply the GIFs, see §2.3)
```

**Criterion**: the script prints "overwritten / already there, skipping" for every file, ending
with `✅ 完成：N 个文件` (idempotent — re-running only says "already there, skipping", it never
inserts twice).

**Things you must know**:

- **⑥ The wake word can only go into `sdkconfig.defaults.esp32s3`**, ⛔ never into `sdkconfig` —
  `sdkconfig` is regenerated by `set-target` and the line is lost (symptom: only 「你好小智」
  wakes the device).
- **Before ⑤ the upstream `components/` directory must exist**: upstream doesn't have it, and the
  script `mkdir -p`s it itself; doing it by hand you must create it first or `cp` errors with
  "target doesn't exist".
- **Don't hand-edit the source list in `main/CMakeLists.txt`**: every line has a reason there
  (GLOB doesn't recurse, `.cpp` files aren't picked up, `.c` assets must be listed individually) —
  deleting one line means a link-time error. Details: [`firmware/README.md`](firmware/README.md)
  (Chinese).
- **`--dry-run`** shows what would change without writing; **`--no-cmake`** only overlays files and
  leaves CMake alone.

### 2.3 Emote assets (the face on the device)

**This repo ships no GIFs** (copyright). Three ways to get them:

```bash
# ① generate from code (zero copyright risk; one command)
python3 -m pip install pillow        # ★ only needed once: make_face.py uses it to draw
python3 tools/make_face.py --out fairy-assets
python3 tools/verify_artifact.py --gif-dir fairy-assets     # check the specs first, then continue

# ② draw your own / ③ use ready-made assets ⇒ specs are in fairy-assets/README.md and firmware/README.md
```

★ **Mind which python Pillow goes into**: install it into **the python you're actually running**.
If you activated ESP‑IDF first (`export.sh` / the IDF terminal from the Start menu), then `python`
is **IDF's bundled environment** (`…/.espressif/python_env/…/bin/python3`), which is *not* your
system `python3` — whichever environment runs the script is the one that needs
`python -m pip install pillow`.

(Without Pillow, `make_face.py` tells you plainly 「需要 Pillow：pip install pillow」 — install it and
re-run; or use routes ②/③ instead.)

Asset specs (what `make_face.py` produces):

| Item | Value |
|---|---|
| Size | **320×240** (must be exactly this; the firmware blits it 1:1) |
| Format | GIF (a static `.png` works too) |
| Frame delay | 50 ms (20 fps) |
| Looping | **infinite** |
| Size on disk | ≤ 8 MB per file and ≤ 8 MB total (that's the assets partition) |
| File names | anything, but you need an `_emote_aliases.json` mapping emotion names → file names |

Wiring the assets into upstream (the script does it together with ①–⑨; you can also run it alone):

```bash
python3 tools/apply_to_upstream.py <upstream directory> --gif-dir fairy-assets
```

### 2.4 Building

#### 2.4.0 Install ESP-IDF first (Windows)

1. Download the **ESP-IDF v6.1 Windows installer** (or offline installer) from Espressif's official
   docs and click through; it will ask which components you want —
   **the defaults (everything) are fine** (you need Python and the toolchain)
2. After installing, the Start menu gets a shortcut like **"ESP-IDF 6.1 PowerShell"** — **open your
   terminal with that one** (it sets PATH for you; a plain PowerShell gives `command not found`)
3. Criterion: in that terminal

```powershell
idf.py --version          # prints a version (v6.1) and you're good
```

> Linux / macOS: `git clone --recursive` ESP‑IDF, run `./install.sh esp32s3`, then
> `source ./export.sh` each session (or put it in `~/.bashrc`).

#### 2.4.1 Build it

```bash
cd <copy of upstream xiaozhi-esp32>
idf.py set-target esp32s3      # criterion: Target set to 'esp32s3'
idf.py build                   # criterion: Project build complete.
                               #           AND build/xiaozhi.bin exists
```

- **The first build takes ten-plus minutes** (7 minutes on the author's machine; depends on your
  machine and whether components are already downloaded) — the first run downloads managed
  components (`managed_components/`) and builds LVGL etc. from scratch
- **`set-target` regenerates `sdkconfig`** ⇒ that's why changes must go into `sdkconfig.defaults*` (§2.2)
- Then merge a full-device image (bootloader + partition table, convenient for flashing the whole chip):

```bash
idf.py merge-bin               # ⇒ build/merged-binary.bin
```

> ⛔ Do **not** add `-o build/merged-binary.bin` after `merge-bin`: `idf.py` already runs inside
> `build/`, so adding `build/` gives you `build/build/...` and a `FileNotFoundError`.
> If you want to specify it, use a bare file name: `-o my.bin`.

**Verify the artifact** (don't stop at "build succeeded"):

```bash
python3 tools/verify_artifact.py <copy>/build/merged-binary.bin
```

It reports whether both wake words are in, whether both MCP tools are in, whether any of the
author's private IPs leaked, and how many GIFs are embedded (0 means the assets didn't get in).

#### 2.4.2 (Optional) compile the server address **into the firmware**

Then after flashing **you only do WiFi setup** and leave the setup page blank (joining up: §3
method B).

```text
Two routes, pick by how you build:

· Building with scripts/build.py
    Put the address in 【the board's config.json】:
      main/boards/m5stack/core-s3/config.json
      "CONFIG_OTA_URL=\"http://<your server IP>:8003/xiaozhi/ota/\""
    (★ only scripts/build.py reads this key; it has no effect with idf.py)

· Building with idf.py (what this project uses)
    Put the address in sdkconfig.defaults / sdkconfig.defaults.esp32s3:
      CONFIG_OTA_URL="http://<your server IP>:8003/xiaozhi/ota/"
    (★ it must be in defaults to survive set-target regenerating sdkconfig — §0.7 rule 2)
```

**Criterion**: after flashing and configuring WiFi, the serial log shows
`WS: Connecting to ws://<your server IP>:8000/…` **without** you entering any address in the setup page.

> ★ **Address priority (confirmed in source, `main/ota.cc`)**:
> ① the device's NVS `wifi/ota_url` is read first ② only if it's empty does it use the
> compile-time `CONFIG_OTA_URL`
> ⇒ what you compile in is a **default**; you can still override it later from the setup page
> (§3 method A) — the two don't conflict.
> ⇒ The firmware also "remembers self-hosted addresses": when it gets a custom address it stores it
> in `fairy_skin/self_ota`, so switching skins or rebooting doesn't lose it (details in
> [`firmware/README.md`](firmware/README.md), Chinese).

⛔ **A firmware with a hard-coded address is for your own use only** — don't distribute it publicly
(it would hand out your LAN IP).

**If the build is stuck, check these three first**

| Symptom | Check first |
|---|---|
| Stuck in the configure stage, 0 processes, no error | zombie processes fighting (§0.7 rule 5) |
| `app partition is too small` | `sdkconfig` was rebuilt in the wrong order (§0.7 rule 2) |
| Stuck at `Downloading…` | the network can't reach the managed components ⇒ retry through a proxy |

### 2.5 Flashing

**Find the serial port first** (don't guess this one):

```text
Windows: Device Manager → Ports (COM & LPT) ⇒ something like "USB Serial Device (COM5)"
         or from the command line: python -m serial.tools.list_ports
Linux/macOS: ls /dev/ttyACM* /dev/cu.usbmodem*
```

**Method 1: `idf.py`**

```bash
idf.py -p <port> flash         # criterion: Hash of data verified.
idf.py -p <port> monitor       # watch the log (Ctrl+] to exit)
```

**Method 2: flash with `esptool` directly (★ the most reliable; this project uses it)**

```bash
# the full merged image is flashed from 0x0
python -m esptool --chip esp32s3 -p <port> -b 460800 \
    --before default-reset --after hard-reset \
    write-flash --flash-mode dio --flash-size 16MB --flash-freq 80m \
    0x0 build/merged-binary.bin
```

> Three flashing methods (including a **browser-based flasher needing no install**) and the
> prebuilt firmware ⇒ [`firmware-bin/README.md`](firmware-bin/README.md) (Chinese)

**If flashing fails, try these in order**

```text
① Swap the cable for a 【data】 cable, try another USB port (the most common real cause)
② Close anything holding the serial port (monitor, a serial terminal, another idf.py)
③ Drop the baud rate to 115200 and retry
④ Erase the whole flash first, then flash:
     python -m esptool --chip esp32s3 -p <port> erase-flash
⑤ Enter download mode manually: hold BOOT (or GPIO0) → tap RST → release BOOT, then flash
⑥ On Linux "permission denied" ⇒ sudo usermod -aG dialout $USER (log back in for it to take effect)
```

**★ Changing only the emote assets doesn't need a full flash** (assets live in their own partition):

```bash
python -m esptool --chip esp32s3 -p <port> write-flash \
    <assets partition offset> build/generated_assets.bin
```

> The offset is in the partition table (this project uses `partitions/v2/16m.csv` — the offset
> column of the `assets` row). If unsure, just flash the whole thing at `0x0`: slower but can't go
> wrong.

**Backup and rollback** (if you already did §0.7 ⓪, this is the restore):

```bash
# read the factory firmware (★ the only irreversible step — do it first)
python -m esptool --chip esp32s3 -p <port> read-flash 0 0x1000000 factory-backup.bin
# to go back to factory: write the backup back over the whole chip
python -m esptool --chip esp32s3 -p <port> -b 460800 write-flash 0x0 factory-backup.bin
```

### 2.6 Confirm on the device (firmware only counts as OK once this passes)

In `idf.py monitor` you should see, in order:

1. Boot self-test (screen lights up, an emote shows)
2. WiFi connects
3. **`WS: Connecting to ws://<your server IP>:8000/xiaozhi/v1/`** ← this is the key criterion
4. Say the wake word (**Hi Fairy** or **你好小智**) — the screen reacts

> If it can't reach the server, **don't suspect the firmware first**: §3 has the proper way to make
> the device connect.

---

## §3 Line 3: joining them up (point the device at your server)

> Both previous lines verified on their own — this step connects them: **make the device find your
> server**.

The device may be pointing somewhere else by default. **Two ways in — pick the one matching the
firmware you have:**

#### Method A: universal firmware (this is the route for the prebuilt firmware) — fill in the address on the setup page

1. On boot, if it can't connect to WiFi (or if you tap the screen), it enters **setup mode** and
   shows a hotspot name
2. Connect your computer/phone to that hotspot and open `192.168.4.1` in a browser
3. Switch to the **`Advanced`** tab → fill in **"Custom OTA URL"**:
   `http://<your server IP>:<http_port>/xiaozhi/ota/`
   (`<http_port>` = `server.http_port` from your config, **default 8003**; if you changed it, use your port)
4. Save → the device reboots → the serial log should show `WS: Connecting to ws://<your IP>:8000/...`

That field writes the device's NVS `wifi/ota_url` (the same key the firmware reads), so
**no compiling and no re-flashing**; you can also just say the IP out loud and let the device set it.

#### Method B: you built it yourself **and already compiled the address in** — just do WiFi setup

```text
After flashing you only need 【WiFi setup】: connect it to your home WiFi (enter the WiFi password),
leave everything else on the setup page 【blank】 — the address is already in the firmware
(how to compile it in: §2.4.2)

criterion: after WiFi setup the serial log 【immediately】 shows
           WS: Connecting to ws://<your server IP>:8000/…
           and you never entered any address anywhere
```

> Good for "giving it to a friend": build a firmware with the address baked in, they flash it and do
> WiFi setup, and it connects to your server.
> ⚠️ The trade-off: such a firmware is **for your own use only — don't distribute it** (your LAN IP
> is inside it).
>
> The two methods **don't conflict**: what you compile in is a default (`CONFIG_OTA_URL`), and
> `wifi/ota_url` in NVS takes **priority** — if you ever change servers, fill the setup page once
> more to override it.

#### How to confirm which server the device is on right now

```text
Serial log: SkinManager: skin saved: <skin>, ota_url=<actual address>
Console  : the "Device" tab described in CONSOLE.md lists both addresses (Fairy server / official server)
```

---

**⑯ Full end-to-end check** (passing this means everything works):

```text
Say the wake word (Hi Fairy / 你好小智) → talk ⇒ the on-screen face animates and the reply uses your voice
Tap the screen ⇒ the status bar appears briefly (WiFi left / battery right)
Pat its head → happy + floating hearts | shake it hard → dizzy
Open the console (CONSOLE.md) ⇒ the device shows as online and the status light follows the conversation
```

---

## §4 Verification checklist (do all of it, don't skip)

| What to verify | Command | Passing criterion |
|---|---|---|
| **Server link** (verify this first) | `python3 tools/test_server_e2e.py --audio ref.wav` | four ✅: hello / ASR / LLM / TTS frames > 0 |
| Emote assets | `python3 tools/verify_artifact.py --gif-dir fairy-assets` | 320×240, infinite loop, ≤ 8 MB total |
| Firmware artifact | `python3 tools/verify_artifact.py <upstream>/build/merged-binary.bin` | both wake words ✅, both MCP tools ✅, no LAN IP ✅, embedded GIF > 0 |
| Server console | open `/admin` in a browser | HTTP 200 + `Fairy 控制台` |
| Device | `idf.py monitor` | `WS: Connecting to ws://<your IP>:8000/...` |
| End to end | say the wake word → talk | the screen reacts and the speaker answers |

---

## §5 Common errors

| Symptom | Real cause | What to do |
|---|---|---|
| `idf.py: command not found` | ESP‑IDF env not installed / terminal not refreshed | Open a new terminal, run IDF's `export.ps1` (or use the shortcut), retry |
| `make_face.py` says 「需要 Pillow：pip install pillow」 | That python has no Pillow (★ common: installed into the system python while the script runs in the activated IDF env) | Install **into the environment that runs the script**: `python -m pip install pillow` (or point `--gifs` at assets you drew yourself) |
| Server: `FileNotFoundError: 找不到 data/.config.yaml` | Upstream **requires** this file | `mkdir data` + `cp config.yaml data/.config.yaml`, then edit that copy (§1.3) |
| Server: `Could not find Opus library` | conda env not activated ⇒ `Library\bin` not on PATH (common on Windows) | `conda activate <env>` first; or add `<conda env>\Library\bin` to PATH |
| Startup log 「配置错误: LLM 的 API key 未设置」 | The LLM section isn't filled in / wrong field name | Edit the LLM section of `data/.config.yaml` (§1.3) |
| `undefined reference to ...` (board-layer symbols) | The `CMakeLists.txt` list from §2.2 didn't apply | Re-run `apply_to_upstream.py`; by hand, check those lines in `main/CMakeLists.txt` |
| `undefined reference to bmi270_init` | Managed component didn't download (network) | Delete `build/` and `managed_components/` and rebuild (don't hand-add BMI270's `.c` files) |
| `app partition is too small` | Partition table didn't take effect | Make sure `sdkconfig.defaults*` has `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions/v2/16m.csv"`, and **copy defaults BEFORE deleting sdkconfig** |
| Only 「你好小智」 wakes it, "Hi Fairy" doesn't | The wake word didn't go into `sdkconfig.defaults.esp32s3` (putting it in `sdkconfig` gets overwritten) | See §2.2 ⑥, re-run the script |
| Stuck downloading / build halted | Managed components couldn't be fetched | Retry through a proxy, or build once just to pull the components down |
| Serial port won't open / is busy | Something else holds it (monitor, serial terminal) | Close them all; try another data cable; try another USB port |
| Build succeeded but the device shows no emote | The GIF didn't get into assets | `verify_artifact.py` → "embedded GIF count"; 0 means it didn't ⇒ re-run §2.3 |
| Device never connects to the server | OTA address mismatch | Fill it in on the setup page per §3; watch the serial log to see which IP it's actually trying |
| Browser `/admin` returns 404 | Server changes not fully applied | Re-run §1.2 and confirm 13/13 |
| A console toggle "jumps back" | The server didn't persist the state | Fixed in this project (`_persist_hw_value`); if you modified it, do the same |
| Server outputs audio but the device is silent | Mismatched sample rate / stream not closing / WiFi power save | All three must hold: identical sample rates, responses with `Connection: close`, WiFi modem sleep off |

---

Whatever step you're stuck on, find its criterion in `tools/verify_artifact.py` and the
`README.md` of the matching directory — **confirm the facts with a criterion first, then change
things**.
