#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────
#  一键复现：从【全新上游】+ 只用本仓库的文件/脚本，跑完两条线并打印判据
#
#  用法（Windows 请在 WSL 或 Git Bash 里跑）：
#    bash tools/repro_all.sh --all              # 两条线全跑（含编译固件，约十几分钟）
#    bash tools/repro_all.sh --firmware         # 只跑固件线
#    bash tools/repro_all.sh --server           # 只跑服务器线
#
#  可选参数：
#    --work <dir>         工作目录（默认 ./repro-work，可重复用）
#    --proxy <url>        下载上游走代理（例：http://127.0.0.1:<你的端口>）
#    --fw-upstream <dir>  已有上游固件目录 ⇒ 跳过下载
#    --srv-upstream <dir> 已有上游服务器目录 ⇒ 跳过下载（那个 tarball 有 147MB）
#    --no-build           固件线只做「打改动 + 生成素材 + 校验」，不编译
#    --gifs <dir>         用你自己的素材目录（默认用 tools/make_face.py 生成一套）
#    --start-server       服务器线跑完自检后真的起一次服务，验 /admin 是否 200
#    --port <n>           起服务时用的 http 端口（默认 8102，避免和你的服务撞车）
#
#  前提：python3 / curl / tar；要编译还需要 ESP-IDF（idf.py 在 PATH 里）
#  退出码：0 = 全部判据通过；1 = 有失败项（下面会列出来）
# ─────────────────────────────────────────────────────────────────
set -u
cd "$(dirname "$0")/.." || exit 2          # 仓库根目录
ROOT="$(pwd)"
WORK="$ROOT/repro-work"
PROXY=""
FW_UP=""; SRV_UP=""; GIFS=""
DO_FW=0; DO_SRV=0; DO_BUILD=1; START_SRV=0
HTTP_PORT=8102

while [ $# -gt 0 ]; do
  case "$1" in
    --all) DO_FW=1; DO_SRV=1 ;;
    --firmware) DO_FW=1 ;;
    --server) DO_SRV=1 ;;
    --work) WORK="$2"; shift ;;
    --proxy) PROXY="$2"; shift ;;
    --fw-upstream) FW_UP="$2"; shift ;;
    --srv-upstream) SRV_UP="$2"; shift ;;
    --gifs) GIFS="$2"; shift ;;
    --no-build) DO_BUILD=0 ;;
    --start-server) START_SRV=1 ;;
    --port) HTTP_PORT="$2"; shift ;;
    -h|--help) sed -n '2,25p' "$0"; exit 0 ;;
    *) echo "未知参数：$1（用 --help 看用法）"; exit 2 ;;
  esac
  shift
done
[ $DO_FW -eq 0 ] && [ $DO_SRV -eq 0 ] && { echo "请至少给一个：--firmware / --server / --all"; exit 2; }

FAILS=()
ok()  { printf '  ✅ %s\n' "$*"; }
ng()  { printf '  ❌ %s\n' "$*"; FAILS+=("$*"); }
info(){ printf '     %s\n' "$*"; }
sect(){ printf '\n\033[1m══ %s\033[0m\n' "$*"; }
CURL=(curl -sS -L --retry 2 -m 600)
[ -n "$PROXY" ] && CURL+=(--proxy "$PROXY")

mkdir -p "$WORK"
PY=python3; command -v $PY >/dev/null || PY=python

# 下载（★ WSL 里 127.0.0.1 是 WSL 自己的 localhost，连不到 Windows 的代理 ⇒ 自动换成网关 IP 重试）
dl() {  # dl <url> <输出文件>
  local url="$1" out="$2" rc=0
  "${CURL[@]}" -o "$out" "$url" || rc=$?
  if [ $rc -ne 0 ] && [ -n "$PROXY" ] && [ -n "${WSL_DISTRO_NAME:-}" ]; then
    local host p2
    host=$(awk '/^nameserver/{print $2; exit}' /etc/resolv.conf 2>/dev/null)
    p2="${PROXY/127.0.0.1/$host}"; p2="${p2/localhost/$host}"
    if [ -n "$host" ] && [ "$p2" != "$PROXY" ]; then
      info "WSL 连不上 $PROXY（那是 Windows 的 localhost）⇒ 用 $p2 重试"
      rc=0
      curl -sS -L --retry 2 -m 600 --proxy "$p2" -o "$out" "$url" || rc=$?
      [ $rc -eq 0 ] && PROXY="$p2"
    fi
  fi
  return $rc
}

# ══════════════════════════════════════════════════════════════
sect "0. 环境"
info "仓库根目录 : $ROOT"
info "工作目录   : $WORK"
info "python     : $($PY --version 2>&1)"
info "python 路径 : $(command -v $PY)"
if $PY -c "import PIL" >/dev/null 2>&1; then
  ok "Pillow 已装（生成表情素材要用）"
  HAVE_PIL=1
else
  info "提醒：这个 python 没装 Pillow ⇒ 生成素材那步会失败"
  info "★ 要装进【你正在用的这个 python】：$PY -m pip install pillow"
  info "  （激活了 ESP-IDF 之后，python 就是 IDF 自带的环境，和系统那个不是同一个）"
  HAVE_PIL=0
fi
if [ $DO_BUILD -eq 1 ] && [ $DO_FW -eq 1 ]; then
  if command -v idf.py >/dev/null; then
    ok "idf.py 在：$(command -v idf.py)"
  else
    ng "找不到 idf.py ⇒ 先激活 ESP-IDF 环境（或用 --no-build 跳过编译）"
    DO_BUILD=0
  fi
fi

# ══════════════════════════════════════════════════════════════
if [ $DO_FW -eq 1 ]; then
  sect "1. 固件线（线二）"
  FW="$FW_UP"
  if [ -z "$FW" ]; then
    FW="$WORK/xiaozhi-esp32-main"
    if [ ! -d "$FW" ]; then
      info "下载上游固件 tarball…"
      dl "https://codeload.github.com/78/xiaozhi-esp32/tar.gz/refs/heads/main" \
         "$WORK/xiaozhi-esp32.tar.gz" || true
      [ -s "$WORK/xiaozhi-esp32.tar.gz" ] || { ng "上游固件下载失败（可加 --proxy，或用 --fw-upstream 指一个已有目录）"; }
      tar -xzf "$WORK/xiaozhi-esp32.tar.gz" -C "$WORK" 2>/dev/null
    fi
  fi
  if [ -d "$FW" ]; then
    ok "上游固件目录：$FW"
    info "① 打改动（apply_to_upstream）"
    if $PY tools/apply_to_upstream.py "$FW" > "$WORK/fw-apply.log" 2>&1; then
      ok "apply_to_upstream 跑完（见 $WORK/fw-apply.log）"
    else
      ng "apply_to_upstream 失败 ⇒ 看 $WORK/fw-apply.log"
    fi

    info "② 准备表情素材"
    if [ -z "$GIFS" ]; then
      if [ $HAVE_PIL -eq 0 ]; then
        ng "跳过素材生成：缺 Pillow（make_face.py 要靠它画图）"
        info "装上再跑：$PY -m pip install pillow   ← 注意要装进【你正在用的这个 python】"
        info "已经有自己画好的素材？也可以直接：--gifs <你的素材目录>"
        GIFS=""
      else
        GIFS="$FW/fairy-assets"
        if $PY tools/make_face.py --out "$GIFS" > "$WORK/fw-face.log" 2>&1; then
          ok "make_face 生成素材 → $GIFS"
        else
          ng "make_face 失败 ⇒ 看 $WORK/fw-face.log"
          GIFS=""
        fi
      fi
    else
      ok "用你指定的素材：$GIFS"
    fi

    if [ -n "$GIFS" ]; then
      info "③ 叠加素材并验规格"
      $PY tools/apply_to_upstream.py "$FW" --gif-dir "$GIFS" > "$WORK/fw-apply2.log" 2>&1 \
        && ok "第二次 apply（幂等）通过" || ng "第二次 apply 失败 ⇒ 看 $WORK/fw-apply2.log"
      if $PY tools/verify_artifact.py --gif-dir "$GIFS" > "$WORK/fw-gifcheck.log" 2>&1; then
        ok "素材规格校验通过"
      else
        ng "素材规格校验失败 ⇒ 看 $WORK/fw-gifcheck.log"
      fi
    else
      info "③ 跳过素材（没素材也编得出来，只是设备上没有 Fairy 的脸）"
    fi

    if [ $DO_BUILD -eq 1 ]; then
      info "④ 编译（首次十几分钟，耐心等）"
      ( cd "$FW" && idf.py set-target esp32s3 && idf.py build && idf.py merge-bin ) \
        > "$WORK/fw-build.log" 2>&1
      if [ -f "$FW/build/xiaozhi.bin" ]; then
        ok "build/xiaozhi.bin 存在（$(stat -c%s "$FW/build/xiaozhi.bin" 2>/dev/null || echo '?') 字节）"
      else
        ng "没有 build/xiaozhi.bin ⇒ 看 $WORK/fw-build.log 最后 40 行"
      fi
      if [ -f "$FW/build/merged-binary.bin" ]; then
        ok "build/merged-binary.bin 存在（$(stat -c%s "$FW/build/merged-binary.bin") 字节）"
        info "⑤ 校验产物"
        if $PY tools/verify_artifact.py "$FW/build/merged-binary.bin"; then
          ok "产物校验通过（唤醒词 / MCP 工具 / 内嵌 GIF / 无内网 IP）"
        else
          ng "产物校验失败（见上面的输出）"
        fi
      else
        ng "没有 build/merged-binary.bin（merge-bin 没跑成？）"
      fi
    else
      info "④ 已跳过编译（--no-build）"
    fi
  fi
fi

# ══════════════════════════════════════════════════════════════
if [ $DO_SRV -eq 1 ]; then
  sect "2. 服务器线（线一）"
  SRV="$SRV_UP"
  if [ -z "$SRV" ]; then
    SRV="$WORK/xiaozhi-esp32-server-main"
    if [ ! -d "$SRV" ]; then
      info "下载上游服务器 tarball（约 147MB）…"
      dl "https://codeload.github.com/xinnan-tech/xiaozhi-esp32-server/tar.gz/refs/heads/main" \
         "$WORK/srv.tar.gz" || true
      [ -s "$WORK/srv.tar.gz" ] || ng "上游服务器下载失败（可加 --proxy，或用 --srv-upstream）"
      tar -xzf "$WORK/srv.tar.gz" -C "$WORK" 2>/dev/null
    fi
  fi
  if [ -d "$SRV" ]; then
    ok "上游服务器目录：$SRV"
    if $PY tools/apply_to_server.py "$SRV" > "$WORK/srv-apply.log" 2>&1; then
      n13=$(grep -c '✅' "$WORK/srv-apply.log" || true)
      ok "apply_to_server 跑完（自检里 ✅ 共 $n13 项，见 $WORK/srv-apply.log）"
    else
      ng "apply_to_server 失败 ⇒ 看 $WORK/srv-apply.log"
    fi
    APPDIR="$SRV/main/xiaozhi-server"
    info "① 建 data/.config.yaml（上游必需，缺了起不来）"
    mkdir -p "$APPDIR/data"
    if [ -f "$APPDIR/data/.config.yaml" ]; then
      ok "已存在，保留不动"
    else
      cp "$APPDIR/config.yaml" "$APPDIR/data/.config.yaml" && ok "已从 config.yaml 复制一份"
    fi
    if [ $START_SRV -eq 1 ]; then
      info "② 真的起一次服务（端口 $HTTP_PORT）并验 /admin"
      # ★ 先确认这个 Python 环境装过服务器依赖（没装就直接说清，别让用户以为是自己做错了）
      if ! $PY -c "import opuslib_next, websockets" >/dev/null 2>&1; then
        ng "当前 python 环境缺服务器依赖 ⇒ 先装依赖并激活对应环境，再回来跑"
        info "例：pip install -r \"$APPDIR/requirements.txt\" ／ 或 conda activate <你的环境>"
        info "（Windows 上还要保证 <conda环境>\\Library\\bin 在 PATH 里，否则报 Could not find Opus library）"
      else
      TMPCFG="$APPDIR/data/.config.yaml"
      $PY - "$TMPCFG" "$HTTP_PORT" <<'PYEOF'
import io, re, sys
p, port = sys.argv[1], sys.argv[2]
t = io.open(p, encoding="utf-8").read()
t = re.sub(r"^(\s*)port:\s*\d+", r"\g<1>port: %d" % (int(port) + 1), t, count=1, flags=re.M)
t = re.sub(r"^(\s*)http_port:\s*\d+", r"\g<1>http_port: %d" % int(port), t, count=1, flags=re.M)
io.open(p, "w", encoding="utf-8", newline="\n").write(t)
print("    已把端口改成 ws=%d / http=%d（只改这次复现用的那份）" % (int(port) + 1, int(port)))
PYEOF
      ( cd "$APPDIR" && $PY app.py > "$WORK/srv-run.log" 2>&1 & echo $! > "$WORK/srv.pid" )
      for i in $(seq 1 60); do
        code=$(curl -s --noproxy '*' -m 3 -o /dev/null -w '%{http_code}' "http://127.0.0.1:$HTTP_PORT/admin" || true)
        [ "$code" = "200" ] && break
        sleep 2
      done
      if [ "$code" = "200" ]; then
        ok "/admin → 200"
        for u in m admin/api/state; do
          c=$(curl -s --noproxy '*' -m 3 -o /dev/null -w '%{http_code}' "http://127.0.0.1:$HTTP_PORT/$u")
          [ "$c" = "200" ] && ok "/$u → 200" || ng "/$u → $c"
        done
        info "整条语音链路的判据（要你填真实 Key 才能四环全过）："
        info "  $PY tools/test_server_e2e.py --url ws://127.0.0.1:$((HTTP_PORT+1))/xiaozhi/v1/ --audio <一段wav>"
      else
        ng "/admin 没起来（HTTP $code）⇒ 看 $WORK/srv-run.log"
        info "常见原因：没激活 conda 环境（Windows 上会报 Could not find Opus library）"
      fi
      [ -f "$WORK/srv.pid" ] && kill "$(cat "$WORK/srv.pid")" 2>/dev/null && ok "已停掉这次起的服务"
      fi
    else
      info "② 想连服务一起验证，加 --start-server"
    fi
  fi
fi

# ══════════════════════════════════════════════════════════════
sect "汇总"
if [ ${#FAILS[@]} -eq 0 ]; then
  echo "  🎉 全部判据通过。日志都在：$WORK/"
  echo "  下一步（要真机才有意义）：烧录 → 设备配网页填 OTA 地址 → 说话"
  exit 0
else
  echo "  失败 ${#FAILS[@]} 项："
  for f in "${FAILS[@]}"; do echo "   - $f"; done
  exit 1
fi
