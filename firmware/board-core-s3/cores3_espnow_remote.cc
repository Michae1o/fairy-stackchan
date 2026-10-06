#include "cores3_espnow_remote.h"

#include <cstring>

#include <esp_log.h>
#include <esp_now.h>
#include <esp_timer.h>
#include <esp_wifi.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "EspNowRemote"

namespace {

// 遥控器广播地址（官方 esp_now_init.c 的 ESPNOW_ADDR_BROADCAST）
const uint8_t kBroadcastAddr[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

constexpr int32_t kRawNone = -100000;   // 「还没有包」哨兵

// ── 遥控器「找设备」用的探测包（本项目自定义，2026-09-30）──────────
//   遥控器不知道设备在哪个信道 ⇒ 它按 Start 时先扫一遍所有信道，
//   在每个信道上广播一个【探测包】；设备听到就用广播回一句
//   「我在信道 N」，遥控器锁定该信道，之后正常发数据。
//   ⇒ 用户不用再看路由器信道、不用调遥控器（换热点/换路由器都不怕）。
//
//   探测包 = 2 字节   [0]=0xC5 magic  [1]=0x01 请求
//   应答包 = 3 字节   [0]=0xC5 magic  [1]=0x02 应答  [2]=设备当前信道
//
//   ★ 用固定的 2/3 字节长度 + magic：设备的解析只认【长度 8 的数据包】，
//     其余长度一律丢掉 ⇒ 不会把别的 ESP-NOW 流量误当成遥控器指令。
constexpr uint8_t kMagic    = 0xC5;
constexpr uint8_t kProbeReq = 0x01;
constexpr uint8_t kProbeAck = 0x02;

// 回调入口。ESP-NOW 的接收回调是 C 函数指针，只能这样拿到实例。
CoreS3EspNowRemote* g_self = nullptr;

// 遥控器单位是 0.1°，直接除 10 得「度」，再夹进设备量程
float ClampYawDeg(float deg) {
    if (deg < -128.0f) return -128.0f;
    if (deg > 128.0f) return 128.0f;
    return deg;
}
float ClampPitchDeg(float deg) {
    if (deg < 3.0f) return 3.0f;
    if (deg > 87.0f) return 87.0f;
    return deg;
}

// ⛔ 这个函数跑在 WiFi 任务上下文：只解析 + 置标志。
//    绝不碰 LVGL / UART / 舵机（会拖垮 WiFi，音频也会断）。
void EspNowRecvCb(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    (void)info;
    if (g_self != nullptr) {
        g_self->OnPacket(data, len);
    }
}

}  // namespace

bool CoreS3EspNowRemote::Start() {
    if (started_.exchange(true)) {
        return true;
    }
    g_self = this;

    // ★★ 直接初始化，【不再等 / 不再轮询 WiFi】——
    //   本函数只由板卡的 StartNetwork() 调用，而 StartNetwork() 内部先走
    //   WifiManager::Initialize() → esp_wifi_init()，所以此处 WiFi 一定已就绪。
    //
    //   ⛔⛔ 血的教训（2026-10-06 实测：设备白屏 + 重启循环 26 次）：
    //   上一版在构造函数里起后台任务「轮询 esp_now_init()，若返回
    //   ESP_ERR_WIFI_NOT_INIT 就 200ms 后重试」——【这个假设是错的】：
    //   esp_now_init() 在 esp_wifi_init() 之前调用【不会】优雅返回错误，
    //   而是直接 LoadProhibited 崩溃（addr2line 指向 esp_now_init，
    //   EXCVADDR 0x4c）。
    //   ⇒ esp_now_init() 只能在 WiFi 模块就绪之后调用，没有"安全探针"。
    esp_err_t err = esp_now_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_init 失败: %s", esp_err_to_name(err));
        started_.store(false);
        return false;
    }

    err = esp_now_register_recv_cb(EspNowRecvCb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_register_recv_cb 失败: %s", esp_err_to_name(err));
        started_.store(false);
        return false;
    }

    RefreshPeer();          // 按当前实际信道建广播 peer
    ready_.store(true);

    // ★ 后台任务：盯信道变化（设备连上路由器 / 路由器换了信道 ⇒ 重建 peer）
    //   没连上时射频通常停在默认信道 1；连上后 = 路由器信道。
    //   （遥控器那边扫信道找设备，所以这里不需要额外通知谁。）
    BaseType_t ok = xTaskCreate(
        [](void* arg) {
            auto* self = static_cast<CoreS3EspNowRemote*>(arg);
            uint8_t last = 0xFF;
            while (true) {
                const uint8_t ch = self->CurrentChannel();
                if (ch != last) {
                    self->RefreshPeer();
                    last = ch;
                }
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        },
        "espnow_chan", 3072, this, 3, nullptr);

    if (ok != pdPASS) {
        ESP_LOGW(TAG, "信道监控任务创建失败（ESP-NOW 本体已在工作，不影响遥控）");
    }
    return true;
}

uint8_t CoreS3EspNowRemote::CurrentChannel() {
    // ① 已连路由器 ⇒ 射频 = 路由器主信道（这条是射频的【实际】值）
    wifi_ap_record_t ap = {};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        return ap.primary;
    }
    // ② 未连接（没配网 / 路由器没开）⇒ 读射频实际停着的信道，通常是 1
    uint8_t primary = 0;
    wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
    if (esp_wifi_get_channel(&primary, &second) == ESP_OK) {
        return primary;
    }
    return 0;
}

void CoreS3EspNowRemote::RefreshPeer() {
    const uint8_t ch = CurrentChannel();

    // 先删旧的（可能还没建过，返回值忽略）
    esp_now_del_peer(kBroadcastAddr);

    // ★★ 头号要点：peer.channel = 0 ⇒ 用【当前】信道
    //   ⛔ 绝不能写死信道值，更不能调 esp_wifi_set_channel
    //      （那会断掉设备与服务器的那条 WiFi 链路）
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, kBroadcastAddr, sizeof(peer.peer_addr));
    peer.channel = 0;
    peer.ifidx   = WIFI_IF_STA;
    peer.encrypt = false;

    esp_err_t err = esp_now_add_peer(&peer);
    if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
        ESP_LOGE(TAG, "esp_now_add_peer(广播) 失败: %s", esp_err_to_name(err));
        return;
    }

    channel_.store(ch);

    wifi_ap_record_t ap = {};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        ESP_LOGW(TAG, "ESP-NOW 接收就绪（设备已连 WiFi，射频在信道 %u）", (unsigned)ch);
        ESP_LOGW(TAG, "★ 遥控器 SETUP 页的 Channel 要调成 %u 才对得上（屏幕可改，1~14）",
                 (unsigned)ch);
    } else {
        ESP_LOGW(TAG, "ESP-NOW 接收就绪（设备当前没连 WiFi，射频在信道 %u）", (unsigned)ch);
        ESP_LOGI(TAG, "★ 遥控器保持 %u 就能用（出厂默认就是 1）—— "
                      "遥控器和机器人是点对点，不需要路由器", (unsigned)ch);
    }
}

void CoreS3EspNowRemote::ReplyChannel() {
    const uint8_t ch = CurrentChannel();
    const uint8_t ack[3] = {kMagic, kProbeAck, ch};
    esp_err_t err = esp_now_send(kBroadcastAddr, ack, sizeof(ack));
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "遥控器在找设备 → 已应答：我在信道 %u", (unsigned)ch);
    } else {
        ESP_LOGW(TAG, "应答遥控器失败: %s", esp_err_to_name(err));
    }
}

void CoreS3EspNowRemote::OnPacket(const uint8_t* data, int len) {
    if (data == nullptr) {
        return;
    }

    // ① 遥控器在「找设备」⇒ 用广播回一句「我在信道 N」
    //    ★ 回调里调 esp_now_send 是安全的（异步投递；官方 esp-now 组件的
    //      转发功能就是这么做的）
    if (len == 2 && data[0] == kMagic && data[1] == kProbeReq) {
        ReplyChannel();
        return;
    }

    // ② 数据包：官方格式固定 8 字节
    //    ⛔ 长度不是 8 的一律丢掉 —— 这样别的 ESP-NOW 流量（含带额外包装的）
    //       不会被误当成摇杆指令让机器人乱动
    if (len != 8) {
        return;
    }

    int16_t yaw_raw   = 0;
    int16_t pitch_raw = 0;
    int16_t speed_raw = 0;
    memcpy(&yaw_raw,   data + 1, sizeof(int16_t));
    memcpy(&pitch_raw, data + 3, sizeof(int16_t));
    memcpy(&speed_raw, data + 5, sizeof(int16_t));
    const uint8_t btn_b = data[7];

    // ★ 量程校验（官方 yaw ±1280 / pitch 0~900 / speed 0~1000）：
    //   挡掉解析出来的垃圾值，宁可不动也别乱动
    if (yaw_raw < -1280 || yaw_raw > 1280) {
        return;
    }
    if (pitch_raw < 0 || pitch_raw > 900) {
        return;
    }
    if (speed_raw < 0 || speed_raw > 1000) {
        return;
    }

    // 只存原始值，换算留给消费侧（回调里越少做事越好）
    pend_yaw_raw_.store(yaw_raw);
    pend_pitch_raw_.store(pitch_raw);
    pend_speed_raw_.store(speed_raw);
    seq_.fetch_add(1);

    last_packet_ms_.store(esp_timer_get_time() / 1000);
    packet_count_.fetch_add(1);

    // BtnB 的「按下」上升沿 ⇒ 板上拿去切对话状态
    if (btn_b != last_btn_b_) {
        last_btn_b_ = btn_b;
        if (btn_b != 0) {
            btn_pressed_.store(true);
        }
    }
}

bool CoreS3EspNowRemote::TakeCommand(Command* out) {
    if (out == nullptr) {
        return false;
    }
    const uint32_t s = seq_.load();
    if (s == consumed_seq_.load()) {
        return false;      // 没有新包
    }
    const int32_t yaw_raw   = pend_yaw_raw_.load();
    const int32_t pitch_raw = pend_pitch_raw_.load();
    const int32_t speed_raw = pend_speed_raw_.load();
    consumed_seq_.store(s);

    if (yaw_raw == kRawNone) {
        return false;
    }

    out->yaw_deg   = ClampYawDeg((float)yaw_raw / 10.0f);
    out->pitch_deg = ClampPitchDeg((float)pitch_raw / 10.0f);
    out->speed     = (speed_raw > 0 && speed_raw <= 1000)
                         ? (uint16_t)speed_raw : 600;
    return true;
}

bool CoreS3EspNowRemote::TakeButtonPress() {
    return btn_pressed_.exchange(false);
}
