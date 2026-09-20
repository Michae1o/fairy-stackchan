// 皮肤切换实现
#include "skin_manager.h"

#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstring>
#include <string>

#include "mcp_server.h"
#include "settings.h"

#define TAG "SkinManager"

namespace stackchan_skin {

const char* kNvsNamespace = "fairy_skin";
const char* kKeySkin      = "current";
const char* kWifiNs       = "wifi";
const char* kKeyOtaUrl    = "ota_url";

// 两套服务器
//   官方 = tenclass（小智官方后端，出厂默认）
//   自建 = ★ 不写死！自动识别（见 SelfOtaUrl()）
const char* kOtaUrlOfficial = "https://api.tenclass.net/xiaozhi/ota/";

// ★ 自建服务器地址的【自动识别】（用户要求：「硬编码的私货搞成自动识别」）
//
//   核心思想：**"自建服务器地址"就是设备当前正在用的那个 OTA 地址**
//     —— 它由 wifi/ota_url（NVS）或 CONFIG_OTA_URL（编译期）决定，
//        两种来源都代表"部署者自己的服务器"
//
//   实现：
//     ① 切到【官方】之前，把当前 NVS 的 ota_url 存进 fairy_skin/self_ota
//     ② 切回【Fairy】时读回它
//     ③ 没记录（首次就切官方）→ 用 CONFIG_OTA_URL 兜底
//
//   ⇒ 开源后：部署者烧自己的服务器地址，切来切去都不会丢，零配置
const char* kKeySelfOta  = "self_ota";   // 存自建地址的键
// ★ 注意：ota_url 的键定义在上面（kWifiNs 那段），此处不再重复定义

// 读当前生效的 OTA 地址（与 ota.cc 的取值顺序保持一致：NVS 优先）
static std::string CurrentOtaUrl() {
    Settings wf(kWifiNs, false);
    std::string u = wf.GetString(kKeyOtaUrl, "");
    if (!u.empty()) {
        return u;
    }
    // 兜底：编译期注入的（开源者烧自己的服务器地址）
    return std::string(CONFIG_OTA_URL);
}

// 取「自建服务器 OTA 地址」
static std::string SelfOtaUrl() {
    // ① 优先用记录下来的（上次切到官方前存的）
    {
        Settings st(kNvsNamespace, false);
        std::string u = st.GetString(kKeySelfOta, "");
        if (!u.empty()) {
            return u;
        }
    }
    // ② 没记录 ⇒ 用当前生效的
    return CurrentOtaUrl();
}

// 切到官方【之前】把自建地址记下来
static void RememberSelfOta() {
    std::string cur = CurrentOtaUrl();
    // 别把官方地址记成"自建"
    if (cur.find("api.tenclass.net") != std::string::npos) {
        return;
    }
    Settings st(kNvsNamespace, true);
    st.SetString(kKeySelfOta, cur);
    ESP_LOGI(TAG, "已记住自建 OTA 地址: %s", cur.c_str());
}

// ★★ 把「当前用的自建服务器地址」记下来
//   为什么需要：切回 Fairy 时要用它作 OTA 地址。
//   若只靠 CONFIG_OTA_URL 兜底，而它等于官方地址（开源版默认），
//   切回 Fairy 就会连官方服务器 ⇒ 用户看到"Fairy 没指向自己服务器"。
//   ⛔ 不能写死 IP（用户要求"自动识别"）⇒ 用【设备实际在用的地址】。
// ★ 从 WebSocket 地址推导 OTA 地址并记住
//   为什么要它：开源固件不含 CONFIG_OTA_URL，
//   若让用户"对着设备念 IP"体验极差（ASR 还容易听错）。
//   ⇒ 改为：设备连上服务器后，用【它正在连的地址】自动推导并记住。
//   推导规则：
//       ws://<IP>:<wsPort>/xiaozhi/v1/  ⇒  http://<IP>:<httpPort>/xiaozhi/ota/
//       其中 httpPort 优先用 OTA 响应里的提示（存在 fairy_skin/http_port），
//       没有则用 8003 兜底。
void RememberOtaFromWsUrl(const std::string& ws_url) {
    if (ws_url.empty()) {
        return;
    }
    // 只处理 ws://host:port/... 形式
    const std::string scheme = "ws://";
    if (ws_url.compare(0, scheme.size(), scheme) != 0) {
        return;
    }
    std::string rest = ws_url.substr(scheme.size());
    // 取出 host（到 ':' 或 '/' 为止）
    size_t colon = rest.find(':');
    size_t slash = rest.find('/');
    std::string host;
    if (colon != std::string::npos) {
        host = rest.substr(0, colon);
    } else if (slash != std::string::npos) {
        host = rest.substr(0, slash);
    } else {
        host = rest;
    }
    if (host.empty()) {
        return;
    }
    // HTTP 端口：优先用已记录的（服务器告知），否则 8003
    std::string http_port = "8003";
    {
        Settings st(kNvsNamespace, false);
        std::string p = st.GetString("http_port", "");
        if (!p.empty()) {
            http_port = p;
        }
    }
    std::string ota = "http://" + host + ":" + http_port + "/xiaozhi/ota/";
    Settings st(kNvsNamespace, false);
    if (st.GetString(kKeySelfOta, "") == ota) {
        return;                       // 没变，不写（省 NVS）
    }
    Settings stw(kNvsNamespace, true);
    stw.SetString(kKeySelfOta, ota);
    ESP_LOGI(TAG, "已从服务器地址推导出自建 OTA 地址: %s", ota.c_str());
}

void RememberSelfOtaIfCustom(const std::string& url) {
    if (url.empty()) {
        return;
    }
    // 官方地址不算"自建"
    if (url.find("api.tenclass.net") != std::string::npos) {
        return;
    }
    Settings st(kNvsNamespace, false);
    if (st.GetString(kKeySelfOta, "") == url) {
        return;                       // 没变，不写（省 NVS 擦写）
    }
    Settings stw(kNvsNamespace, true);
    stw.SetString(kKeySelfOta, url);
    ESP_LOGI(TAG, "已记住自建 OTA 地址（自动识别）: %s", url.c_str());
}

Skin LoadSkin() {
    Settings s(kNvsNamespace, false);
    int v = s.GetInt(kKeySkin, 0);
    return (v == 1) ? Skin::Geometry : Skin::Fairy;
}

const char* SkinName(Skin s) {
    return (s == Skin::Geometry) ? "geometry" : "fairy";
}

// 取某套皮肤该用的 OTA 地址（自建走自动识别）
static std::string OtaUrlFor(Skin s) {
    return (s == Skin::Geometry) ? std::string(kOtaUrlOfficial)
                                 : SelfOtaUrl();
}

void SaveSkin(Skin s) {
    // ★ 切到官方【之前】：先把当前自建地址记下来（下次切回来要用）
    if (s == Skin::Geometry) {
        RememberSelfOta();
    }
    {
        Settings st(kNvsNamespace, true);
        st.SetInt(kKeySkin, (s == Skin::Geometry) ? 1 : 0);
    }
    // ★ 同时换 OTA 地址（ota.cc 是 NVS 优先，所以写这里就生效）
    std::string url = OtaUrlFor(s);
    if (url.empty()) {
        ESP_LOGE(TAG, "自建 OTA 地址为空！请在控制台设置，或烧固件时"
                      "指定 CONFIG_OTA_URL");
        return;
    }
    {
        Settings wf(kWifiNs, true);
        wf.SetString(kKeyOtaUrl, url);
    }
    ESP_LOGI(TAG, "skin saved: %s, ota_url=%s", SkinName(s), url.c_str());
}

static void RebootTask(void*) {
    vTaskDelay(pdMS_TO_TICKS(800));   // 给日志/响应留点时间
    ESP_LOGW(TAG, "rebooting to apply skin change...");
    esp_restart();
}

void SwitchSkin(Skin s, bool reboot) {
    ESP_LOGW(TAG, "=== SwitchSkin(%s, reboot=%d) ===", SkinName(s),
             reboot ? 1 : 0);
    SaveSkin(s);
    if (reboot) {
        xTaskCreate(RebootTask, "skin_reboot", 2048, nullptr, 5, nullptr);
    }
}

bool SwitchSkinByName(const std::string& name, bool reboot) {
    std::string n = name;
    for (auto& c : n) {
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    }
    if (n == "fairy" || n == "0" || n == "gif") {
        SwitchSkin(Skin::Fairy, reboot);
        return true;
    }
    if (n == "geometry" || n == "1" || n == "official"
        || n == "小表情" || n == "官方表情" || n == "几何脸") {
        SwitchSkin(Skin::Geometry, reboot);
        return true;
    }
    ESP_LOGW(TAG, "未知皮肤名: %s", name.c_str());
    return false;
}

// ── MCP 工具（语音切换入口）──────────────────────────────────
//   用户说「换成官方小表情」「切回 Fairy」时，AI 调这个。
void RegisterMcpTools() {
    auto& mcp = McpServer::GetInstance();

    mcp.AddTool("self.skin.set",
        "切换机器人的外观皮肤（同时会切换它连的服务器，设备会重启，约10秒）。"
        "fairy=当前的 Fairy 形象（配自己的服务器，有人格和专属音色）；"
        "geometry=官方小表情（几何脸，会眨眼/有情绪，配官方服务器，可带出门用）。"
        "用户说「换成官方表情」「切回 Fairy」「我要带出去用」时调用。",
        PropertyList({
            // ★ 两者都给默认值：PropertyList::operator[] 找不到字段会
            //   esp_system_abort() ⇒ AI 漏传就崩设备。
            //   string 也能带默认值（Property 第三个参数是模板 T）。
            Property("skin", kPropertyTypeString, std::string("fairy")),
            Property("reboot", kPropertyTypeBoolean, true),
        }),
        [](const PropertyList& p) -> ReturnValue {
            std::string sk = p["skin"].value<std::string>();
            bool rb = p["reboot"].value<bool>();
            if (!SwitchSkinByName(sk, rb)) {
                return std::string("未知皮肤: " + sk +
                                   "（可用 fairy / geometry）");
            }
            Skin s = LoadSkin();
            char buf[160];
            snprintf(buf, sizeof(buf),
                     "已切换到 %s（服务器：%s）%s",
                     SkinName(s),
                     (s == Skin::Geometry) ? "官方" : "自建",
                     rb ? "，设备正在重启，约10秒后生效" : "");
            return std::string(buf);
        });

    mcp.AddTool("self.skin.get",
        "查询机器人当前用的是哪套皮肤。",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            Skin s = LoadSkin();
            char buf[128];
            snprintf(buf, sizeof(buf), "当前皮肤=%s（%s）", SkinName(s),
                     (s == Skin::Geometry)
                         ? "官方几何脸 + 官方服务器"
                         : "Fairy + 自建服务器");
            return std::string(buf);
        });


    // ★★ 设置自建服务器地址（通用固件必需：没编译期 CONFIG_OTA_URL 时，
    //    用户只能靠这个入口告诉设备"我的服务器在哪"）
    mcp.AddTool("self.server.set_ota_url",
        "设置机器人要连接的【自建服务器】地址。"
        "当用户说「我的服务器是 …」「把服务器改成 …」"
        "「连我自己的服务器」「换一个服务器地址」时调用。"
        "url 必须是完整的 OTA 地址，形如 "
        "http://<你的电脑IP>:<端口>/xiaozhi/ota/ 。"
        "设置后设备会重启，约 10 秒后连上新服务器。",
        PropertyList({
            Property("url", kPropertyTypeString, std::string("")),
            Property("reboot", kPropertyTypeBoolean, true),
        }),
        [](const PropertyList& p) -> ReturnValue {
            std::string u = p["url"].value<std::string>();
            bool rb = p["reboot"].value<bool>();
            if (u.empty()) {
                return std::string("请提供服务器地址，"
                                   "形如 http://192.168.1.100:8003/xiaozhi/ota/");
            }
            if (u.find("://") == std::string::npos) {
                u = "http://" + u;
            }
            if (u.find("/xiaozhi/ota/") == std::string::npos) {
                // 只给了 IP:端口 ⇒ 自动补全路径
                while (!u.empty() && u.back() == '/') {
                    u.pop_back();
                }
                u += "/xiaozhi/ota/";
            }
            // ① 记成"自建地址"（切皮肤要回到这儿）
            {
                Settings st(kNvsNamespace, true);
                st.SetString(kKeySelfOta, u);
            }
            // ② 写成当前生效的 OTA 地址（ota.cc 是 NVS 优先）
            {
                Settings wf(kWifiNs, true);
                wf.SetString(kKeyOtaUrl, u);
            }
            ESP_LOGW(TAG, "server ota url set by user: %s", u.c_str());
            char buf[220];
            snprintf(buf, sizeof(buf),
                     "已记住你的服务器地址：%s%s",
                     u.c_str(),
                     rb ? "，设备正在重启，约10秒后连上" : "");
            if (rb) {
                xTaskCreate(RebootTask, "skin_reboot", 2048, nullptr, 5,
                            nullptr);
            }
            return std::string(buf);
        });

    ESP_LOGI(TAG, "registered 3 MCP tools for skin");
}

}  // namespace stackchan_skin
