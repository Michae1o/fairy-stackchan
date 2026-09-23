# Fairy StackChan

[中文](README.md) | **English**

Turn an M5Stack **StackChan** (CoreS3 desktop robot) into **Fairy** from *Zenless Zone Zero*:
full-screen emotes + DeepSeek chat + GPT‑SoVITS zero-shot voice + a web console +
head-pat / shake / look-around interactions.

> ℹ️ **Docs language:** this English README + [`INSTALL.en.md`](INSTALL.en.md) cover the overview
> and the whole install path. The deeper documents (`firmware/README.md`,
> `firmware-bin/README.md`, `CONSOLE.md`, `CHECKLIST.md`, …) are **Chinese-only for now** —
> open an Issue if you need one of them.

---

## What this repo is (read this first)

> ★★ **This repo is a "patch package" — not a complete project.**
>
> It contains only the **changed files + scripts + docs**: there is **no upstream source tree**
> here — no `main/`, no `CMakeLists.txt`, no `idf_component.yml` — and no ready-made images,
> audio or model weights either.
> Usage is always two steps: **① get a copy of the upstream project → ② overlay this repo on
> top of it** (then build, or start the server).
> **Cloning this repo alone will not run.** Where to overlay and which upstream version to use:
> [`INSTALL.en.md`](INSTALL.en.md) §0.5 (versions) and §1.2 / §2.2 (overlay steps).
> Want to skip compiling? Use the prebuilt firmware from
> [Releases](https://github.com/Michae1o/fairy-stackchan/releases) —
> see [`firmware-bin/README.md`](firmware-bin/README.md) (Chinese).

**A step-by-step guide to turning your own StackChan into Fairy.**

Everything from the author's production setup (the one running at home every day) is here:
**code, config, parameters and the pitfalls he hit** — minus three things:

1. **Personal info** — LAN IPs, API keys, absolute local paths
2. **Ready-made art assets** — Fairy's emote GIFs (copyright belongs to the original rights holder)
3. **Audio and model weights** — reference audio, TTS fine-tune weights

⇒ Clone it and you can build the same thing: **generate the assets with the code in this repo**,
or bring your own.

---

## What you end up with

- **Two skins, switched by a short press of the power button**
  - `Fairy` — full-screen GIF emotes (320×240), pairs with your self-hosted server
  - `Geometry` — the official geometric face (drawn by M5Stack's original code), talks to the official server
- **Two wake words**: `Hi Fairy` + `你好小智` (both work in the same firmware)
- **24 emotion names** (23 → `idle.gif`, `thinking` → `thinking.gif`)
- **Interactions**: head-pat → happy + floating hearts; shake → dizzy spin; idle head-turning (off by default)
- **Tap the screen** → the status bar shows briefly (WiFi left / battery right, hides after ~3 s)
- **Two MCP tools** (so the AI can change the device itself): set the server URL, switch skin
- **Web console**: desktop `/admin` (status light, hardware toggles, skins, TTS preview, chat in the
  browser, **voiceprint management**) and mobile `/m` — see [`CONSOLE.md`](CONSOLE.md) (Chinese)
- **Speaker recognition (voiceprint)**: every utterance is matched against registered voiceprints,
  so it knows **who is speaking**; the console's 🗣️ page lets you **add people and enrol them by
  recording 6 seconds in the browser** (uses the self-hosted
  [`voiceprint-api`](https://github.com/xinnan-tech/voiceprint-api) — see [`server/README.md`](server/README.md))
- **Voice**: GPT‑SoVITS zero-shot cloning (with Fairy-style tone post-processing on the server), no training needed
- **Switch skin** three ways: short-press the power button / ask by voice / click the card in the console

---

## Two routes, pick one

### Route A: no compiling — just flash

> ★★ **Prerequisite: you need a working server first.**
> "No compile" only skips **building the firmware**; it does **not** mean you can skip the server.
> After flashing, the device needs something to connect to — otherwise it just talks to the official
> server (no persona / voice / console of yours).
> ⇒ How to stand up the server: [`INSTALL.en.md`](INSTALL.en.md) §1 (**do this line first**).

1. Get the **server** running and verified per [`INSTALL.en.md`](INSTALL.en.md) §1
   (criteria: `/admin` returns 200 + `tools/test_server_e2e.py` shows four ✅)
2. Download `fairy-stackchan-universal.bin` from
   [Releases](https://github.com/Michae1o/fairy-stackchan/releases)
3. Flash it per [`firmware-bin/README.md`](firmware-bin/README.md) (three methods, including a
   browser-based flasher; Chinese)
4. On the device's setup page → "Advanced" → enter your server URL
   ⇒ the serial log then shows `WS: Connecting to ws://…`

> ⚠️ That prebuilt firmware has **2 emote GIFs drawn by the author embedded** (~2 MB) so you can
> see it work out of the box; personal, non-commercial use only — see [`CREDITS.md`](CREDITS.md).

### Route B: modify / build it yourself

All steps: **[`INSTALL.en.md`](INSTALL.en.md)** (the standard install SOP: prerequisites, order,
per-step criteria, error table).

Shortest path:

```bash
git clone <this repo>
python3 -m pip install pillow            # ① needed to generate assets (★ install it into the python you actually use)
python3 tools/make_face.py --out fairy-assets          # ② generate the emote assets
python3 tools/apply_to_upstream.py <upstream xiaozhi-esp32>    # ③ apply the firmware changes
python3 tools/apply_to_server.py   <upstream xiaozhi-server>   # ④ apply the server changes
```

(Want one command that runs all of the above plus the build? `bash tools/repro_all.sh --all` —
see "For users who work through an AI agent" below.)

Both overlay scripts are **idempotent** (re-running just prints "already there, skipping");
after that, build and flash as usual with `idf.py build flash`.

> ★ **Which upstream version this package is based on** (two of them — don't mix them up):
> **tested on the author's device** = firmware `5d54beb7` (2026‑09‑15) + server `4745186c` (2026‑09‑17);
> **compile-verified for this package's changes** = firmware `4632dc51f` (09‑20) + server `788f5301f` (09‑21).
> For a 100% reproduction use the first pair
> (how to pin a version + what to do when upstream moves on ⇒ [`INSTALL.en.md`](INSTALL.en.md) §0.5).

---

## Repo map

| Directory / file | What's inside |
|---|---|
| [`INSTALL.en.md`](INSTALL.en.md) · [`INSTALL.md`](INSTALL.md) | **The install SOP**: prerequisites → server → firmware → voice → verification → error table |
| [`CONSOLE.md`](CONSOLE.md) | **Console manual** (desktop `/admin` 8 tabs · mobile `/m` 4 tabs · FAQ · API) — Chinese |
| [`firmware/`](firmware/README.md) | **All** firmware changes (board layer / avatar & decorators / local components / display layer) + `CMakeLists.append.txt` — Chinese |
| [`firmware-bin/`](firmware-bin/README.md) | Prebuilt firmware + three flashing methods + first-connection setup + troubleshooting — Chinese |
| [`server/`](server/README.md) | **All** server-side changes (10 files, laid out mirroring the upstream tree) — Chinese |
| [`tools/`](tools/) | 9 scripts: generate assets / apply firmware changes / apply server changes / verify artifacts / **verify the server link (simulated device)** / **one-command reproduction (`repro_all.sh`)** / **voiceprint registration (CLI)** / **voiceprint end-to-end self-test** / **start / restart all services** |
| [`fairy-assets/`](fairy-assets/README.md) | **Empty directory** — this is where Fairy's emote GIFs go; not shipped (see the README inside) |
| [`voice-package/`](voice-package/README.md) | Voice: zero-shot cloning / fine-tuning, how to record reference audio — Chinese |
| [`CREDITS.md`](CREDITS.md) | Sources, licensing, copyright notes for assets and voices |
| [`CHECKLIST.md`](CHECKLIST.md) | The author's own pre-release checklist (users can ignore it) |

---

## What's included / what's not

| | Contents |
|---|---|
| **Included** | Upstream **changed source** (firmware: board layer / avatar & decorators / local components / display layer; server: 10 files) · 9 tool scripts · docs with parameters and how-to · 1 console UI screenshot (`docs/`) · prebuilt firmware (in Releases) |
| **Not included** | ★ **The upstream source tree itself** (`main/`, `CMakeLists.txt` — clone upstream for those) · any **character art assets** · reference audio · TTS fine-tune weights · the author's LAN IP / keys / local paths |

For ways to get emote assets, voice and weights, see
[`fairy-assets/README.md`](fairy-assets/README.md) and [`voice-package/README.md`](voice-package/README.md)
(both Chinese).

---

## For users who work through an AI agent

If you don't write much code yourself and mostly let an AI agent (Claude Code / Cursor / Hermes …)
do the work, hand it this repo and have it go in this order:

1. Read **`INSTALL.en.md`** (it is the single source of truth); **follow the three lines in §0.7** —
   **server line first** (verifiable without a device) → firmware line → then join them up
2. Use **`tools/apply_to_upstream.py`** for firmware changes and **`tools/apply_to_server.py`**
   for server changes — don't let it hand-copy the patches, these two scripts self-check
3. Once the server is up, verify the whole link with
   **`python3 tools/test_server_e2e.py --audio ref.wav`**
   (simulates a device doing handshake → ASR → LLM → TTS; four ✅ before moving on)
4. Before building: **`python3 tools/verify_artifact.py --gif-dir fairy-assets`** to check asset specs
5. After building: **`python3 tools/verify_artifact.py build/merged-binary.bin`** to check the artifact
   (it reports whether both wake words are in, whether both MCP tools are in, whether any private
   IP leaked, and how many GIFs are embedded)
6. ★ **One command if you want it easy**: `bash tools/repro_all.sh --all` — runs steps 2–5
   (including the build) and prints the criteria; logs land in `./repro-work/`
   (`--no-build` skips the build; `--firmware` / `--server` run a single line; on Windows use WSL or Git Bash)

---

## License

Code is MIT (see [`LICENSE`](LICENSE));
**third-party code used by this package and their licenses** (including the Apache-2.0 attribution
and the full license text for Fairy-DSH) are in [`THIRD-PARTY-LICENSES.md`](THIRD-PARTY-LICENSES.md);
copyright and sources for the character, voice and assets are in [`CREDITS.md`](CREDITS.md).
