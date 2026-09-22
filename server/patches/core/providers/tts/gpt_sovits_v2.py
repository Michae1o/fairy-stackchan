import requests
from config.logger import setup_logging
from core.providers.tts.base import TTSProviderBase
from core.utils.util import parse_string_to_list

# ── Fairy 音色后处理（P05 档，用户 2026-09-17 选定）──────────────────
#  值来自 tools/tts-pitch-opt.py 的 P05 档（已客观验证单调）：
#    暖度 +5dB(100-300Hz) / 削尖 -2dB(1k-4k) / 降调 -0.5 半音 / 语速 1.11
#  为什么放这里：text_to_speak 是 TTS 的唯一出口，在此处理下游全自动受益。
#  库缺失时降级为「原样返回」，绝不让音色处理拖垮整条对话链。
try:
    import io as _io

    import librosa as _librosa
    import numpy as _np
    import soundfile as _sf
    from scipy.signal import butter as _butter
    from scipy.signal import filtfilt as _filtfilt

    _POST_OK = True
except Exception:  # pragma: no cover
    _POST_OK = False

TAG = __name__
logger = setup_logging()


def _fairy_bg(y, sr, lo, hi, gain_db):
    """带增益（暖度/削尖用）"""
    nyq = sr / 2.0
    lo_n, hi_n = max(lo / nyq, 1e-4), min(hi / nyq, 0.999)
    if lo_n >= hi_n or len(y) <= 24:
        return y
    b, a = _butter(2, [lo_n, hi_n], btype="band")
    return y + _filtfilt(b, a, y) * (10 ** (gain_db / 20.0) - 1.0)


def _fairy_postprocess(wav_bytes, warm_db=0.0, cut_db=0.0,
                       pitch_steps=0.0, speed=1.0):
    """Fairy 音色后处理（默认【全部关闭】= 原味输出）。

    ★ 2026-09-17 修正：用户已换成【自己的 Fairy 微调模型】，
      微调模型自带音色特征，不需要再叠加"降调/加暖"这类补救处理
      （那是给「底模 + cand4 参考」用的）。
      之前默认值 (-0.5半音, 1.11语速, +5dB暖) 会让设备听到的声音
      与「直连 api_v2 的原味合成」明显不同 —— 用户一耳朵就听出来了。

    现在默认全中性 → 等价于"原封不动"。
    若日后确实想加料，改这里的默认值或传参即可（逻辑保留）。
    """
    if not _POST_OK or not wav_bytes:
        return wav_bytes
    # 全中性 → 直接返回原字节，连解码都省了（零开销、零失真）
    if (abs(warm_db) < 1e-6 and abs(cut_db) < 1e-6
            and abs(pitch_steps) < 1e-6 and abs(speed - 1.0) < 1e-6):
        return wav_bytes
    try:
        y, sr = _sf.read(_io.BytesIO(wav_bytes))
        if y.ndim > 1:
            y = y.mean(axis=1)
        y = y.astype(_np.float32)

        # ① 暖度：提升低频（去"冷/薄"）
        y = _fairy_bg(y, sr, 100, 300, warm_db)
        y = _fairy_bg(y, sr, 200, 500, warm_db * 0.5)
        # ② 削尖：衰减 1k-4k（去"尖/刺"）
        y = _fairy_bg(y, sr, 1000, 4000, cut_db)
        y = y.astype(_np.float32)

        # ③ 整段一次性降调（无接缝）
        if pitch_steps:
            y = _librosa.effects.pitch_shift(y, sr=sr, n_steps=pitch_steps)
        # ④ 语速（后处理时间压缩，不改音高）
        if speed and abs(speed - 1.0) > 1e-6:
            y = _librosa.effects.time_stretch(y, rate=speed)

        buf = _io.BytesIO()
        _sf.write(buf, y.astype(_np.float32), sr, format="WAV",
                  subtype="PCM_16")
        return buf.getvalue()
    except Exception as e:  # 音色处理失败不应影响对话
        logger.bind(tag=TAG).warning(f"Fairy 音色后处理失败，使用原始音频: {e}")
        return wav_bytes


class TTSProvider(TTSProviderBase):
    def __init__(self, config, delete_audio_file):
        super().__init__(config, delete_audio_file)
        self.url = config.get("url")
        self.text_lang = config.get("text_lang", "zh")
        self.ref_audio_path = config.get('ref_audio') if config.get('ref_audio') else config.get("ref_audio_path")
        self.prompt_text = config.get('ref_text') if config.get('ref_text') else config.get("prompt_text")
        self.prompt_lang = config.get("prompt_lang", "zh")

        # 处理空字符串的情况
        top_k = config.get("top_k", "5")
        top_p = config.get("top_p", "1")
        temperature = config.get("temperature", "1")
        batch_threshold = config.get("batch_threshold", "0.75")
        batch_size = config.get("batch_size", "1")
        speed_factor = config.get("speed_factor", "1.0")
        seed = config.get("seed", "-1")
        repetition_penalty = config.get("repetition_penalty", "1.35")

        self.top_k = int(top_k) if top_k else 5
        self.top_p = float(top_p) if top_p else 1
        self.temperature = float(temperature) if temperature else 1
        self.batch_threshold = float(batch_threshold) if batch_threshold else 0.75
        self.batch_size = int(batch_size) if batch_size else 1
        self.speed_factor = float(speed_factor) if speed_factor else 1.0
        self.seed = int(seed) if seed else -1
        self.repetition_penalty = (
            float(repetition_penalty) if repetition_penalty else 1.35
        )

        self.text_split_method = config.get("text_split_method", "cut0")

        self.split_bucket = str(config.get("split_bucket", True)).lower() in (
            "true",
            "1",
            "yes",
        )
        self.return_fragment = str(config.get("return_fragment", False)).lower() in (
            "true",
            "1",
            "yes",
        )

        self.streaming_mode = str(config.get("streaming_mode", False)).lower() in (
            "true",
            "1",
            "yes",
        )

        self.parallel_infer = str(config.get("parallel_infer", True)).lower() in (
            "true",
            "1",
            "yes",
        )

        self.aux_ref_audio_paths = parse_string_to_list(
            config.get("aux_ref_audio_paths")
        )
        self.audio_file_type = config.get("format", "wav")

    async def text_to_speak(self, text, output_file):
        request_json = {
            "text": text,
            "text_lang": self.text_lang,
            "ref_audio_path": self.ref_audio_path,
            "aux_ref_audio_paths": self.aux_ref_audio_paths,
            "prompt_text": self.prompt_text,
            "prompt_lang": self.prompt_lang,
            "top_k": self.top_k,
            "top_p": self.top_p,
            "temperature": self.temperature,
            "text_split_method": self.text_split_method,
            "batch_size": self.batch_size,
            "batch_threshold": self.batch_threshold,
            "split_bucket": self.split_bucket,
            "return_fragment": self.return_fragment,
            "speed_factor": self.speed_factor,
            "streaming_mode": self.streaming_mode,
            "seed": self.seed,
            "parallel_infer": self.parallel_infer,
            "repetition_penalty": self.repetition_penalty,
        }

        resp = requests.post(
            self.url, json=request_json, timeout=self.tts_timeout
        )
        if resp.status_code == 200:
            # ★ 音色后处理（P05 档）：加暖 + 削尖 + 降调 + 语速
            audio = _fairy_postprocess(resp.content)
            if output_file:
                with open(output_file, "wb") as file:
                    file.write(audio)
            else:
                return audio
        else:
            error_msg = f"GPT_SoVITS_V2 TTS请求失败: {resp.status_code} - {resp.text}"
            logger.bind(tag=TAG).error(error_msg)
            raise Exception(error_msg)
