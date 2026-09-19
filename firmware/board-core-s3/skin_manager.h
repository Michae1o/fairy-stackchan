// 皮肤切换：Fairy（自建服务器） ⟷ 官方几何脸（官方服务器）
//
// ★ 用户需求（原话）：
//   「我的服务器是 fairy 专用，如果不换服务器，官方表情搭配 fairy 人格会很违和，
//     所以我要官方表情搭配小智服务器，而且还能带出去。」
//   「切皮肤自动切服务器」
//
// ★ 逻辑：
//   切到 geometry → 显示几何脸 + 把 OTA 地址写成【官方】（能带出去用）
//   切到 fairy    → 显示 GIF 脸 + 把 OTA 地址写成【自建】
//   ⇒ 因为与服务器是开机建的长连接，改完必须【重启】才生效（约 5-8s）
//
// ★ 关键依据（读 ota.cc 确认）：
//   Ota::GetCheckVersionUrl() 是【NVS 优先】：
//       url = settings.GetString("ota_url");
//       if (url.empty()) url = CONFIG_OTA_URL;
//   ⇒ 写 NVS 的 wifi/ota_url 即可换服务器，【不用改 ota.cc】

#pragma once

#include <string>

namespace stackchan_skin {

// 皮肤标识
enum class Skin {
    Fairy = 0,      // GIF 脸 + 自建服务器（Fairy 人格/音色）
    Geometry = 1,   // 官方几何脸 + 官方服务器（官方人格）
};

// NVS 键
extern const char* kNvsNamespace;   // "fairy_skin"
extern const char* kKeySkin;        // "current"     → 0/1
extern const char* kWifiNs;         // "wifi"
extern const char* kKeyOtaUrl;      // "ota_url"

// 两套服务器地址
extern const char* kOtaUrlSelf;     // 自建（Fairy）
extern const char* kOtaUrlOfficial; // 官方（api.tenclass.net）

// 读当前皮肤（没存过 → Fairy）
Skin LoadSkin();
const char* SkinName(Skin s);

// 只写 NVS，不重启（供"只保存"场景）
void SaveSkin(Skin s);

// ★ 核心：切皮肤 = 存皮肤 + 换 OTA 地址 + 重启
//   reboot=false 时只写不重启（便于测试）
void SwitchSkin(Skin s, bool reboot = true);
// ★★ 把「当前用的自建服务器地址」记下来
//   为什么需要：切回 Fairy 时要用它当 OTA 地址。
//   若只依赖 CONFIG_OTA_URL 兜底，一旦它等于官方
//   地址（开源版默认值），切回 Fairy 就会连官方。
void RememberSelfOtaIfCustom(const std::string& url);


// 语音/控制台入口用：按名字切（"fairy"/"geometry"/"official"/"小表情"）
// 返回 false 表示名字不认识
bool SwitchSkinByName(const std::string& name, bool reboot = true);

// ★ 注册 MCP 工具（self.skin.set / self.skin.get）—— 板卡初始化时调
void RegisterMcpTools();

}  // namespace stackchan_skin
