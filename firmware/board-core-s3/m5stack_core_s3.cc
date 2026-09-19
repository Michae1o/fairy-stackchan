#include "wifi_board.h"
#include "cores3_audio_codec.h"
#include "display/lcd_display.h"
#include "application.h"
#include "config.h"
#include "power_save_timer.h"
#include "i2c_device.h"
#include "axp2101.h"
#include "cores3_py32_led.h"   // 12 颗 RGB（PY32 IO 扩展）
#include "cores3_servo.h"      // 两个飞特总线舵机（UART1）
#include "stackchan_sensor.h"
#include "stackchan_geometry_display.h"  // ★ 官方几何脸（第二套皮肤）
#include "skin_manager.h"      // ★ 皮肤切换（切服务器 + 重启）
#include "mcp_server.h"        // ★ 待机扭头开关（MCP 工具）
#include "settings.h"          // ★ NVS 持久化（开关存这里）

#include <lvgl.h>
#include <esp_log.h>
#include <atomic>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_ili9341.h>
#include <esp_timer.h>
#include "esp_video.h"

#define TAG "M5StackCoreS3Board"

class Pmic : public Axp2101 {
public:
    // Power Init
    Pmic(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : Axp2101(i2c_bus, addr) {
        uint8_t data = ReadReg(0x90);
        data |= 0b10110100;
        WriteReg(0x90, data);
        WriteReg(0x99, (0b11110 - 5));
        WriteReg(0x97, (0b11110 - 2));
        WriteReg(0x69, 0b00110101);
        WriteReg(0x30, 0b111111);
        WriteReg(0x90, 0xBF);
        WriteReg(0x94, 33 - 5);
        WriteReg(0x95, 33 - 5);
    }

    void SetBrightness(uint8_t brightness) {
        brightness = ((brightness + 641) >> 5);
        WriteReg(0x99, brightness);
    }

    // ★ 电源键（用户要求「改成电源键按一下」）
    //
    //   AXP2101 中断寄存器（数据手册 §5.7）：
    //     0x40 = IRQ Enable1   0x41 = IRQ Enable2
    //     0x48 = IRQ Status1   0x49 = IRQ Status2
    //     0x4A = IRQ Status3
    //   其中 PWR_KEY_SHORT_IRQ 在 Status2(0x49) 的 bit3
    //        PWR_KEY_LONG_IRQ  在 Status2(0x49) 的 bit2
    //   ★ 保守做法：两个都读，只认【短按】，且读完清标志
    //
    //   返回 true = 检测到一次短按
    bool ConsumeShortPress() {
        // 使能短按中断（0x42 = IRQ Enable2 的 bit3）
        uint8_t en2 = ReadReg(0x42);
        if ((en2 & 0b00001000) == 0) {
            WriteReg(0x42, (uint8_t)(en2 | 0b00001000));
        }
        // 清长按（避免长按关机时被当成短按）
        uint8_t st2 = ReadReg(0x49);
        if (st2 & 0b00000100) {            // bit2 = 长按
            WriteReg(0x49, st2);           // 写回清
            return false;                  // 长按让 AXP 自己处理（关机）
        }
        if (st2 & 0b00001000) {            // bit3 = 短按
            WriteReg(0x49, st2);           // 写回清
            return true;
        }
        return false;
    }
};

class CustomBacklight : public Backlight {
public:
    CustomBacklight(Pmic *pmic) : pmic_(pmic) {}

    void SetBrightnessImpl(uint8_t brightness) override {
        pmic_->SetBrightness(target_brightness_);
        brightness_ = target_brightness_;
    }

private:
    Pmic *pmic_;
};

class Aw9523 : public I2cDevice {
public:
    // Exanpd IO Init
    Aw9523(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
        WriteReg(0x02, 0b00000111);  // P0
        WriteReg(0x03, 0b10001111);  // P1
        WriteReg(0x04, 0b00011000);  // CONFIG_P0
        WriteReg(0x05, 0b00001100);  // CONFIG_P1
        WriteReg(0x11, 0b00010000);  // GCR P0 port is Push-Pull mode.
        WriteReg(0x12, 0b11111111);  // LEDMODE_P0
        WriteReg(0x13, 0b11111111);  // LEDMODE_P1
    }

    void ResetAw88298() {
        ESP_LOGI(TAG, "Reset AW88298");
        WriteReg(0x02, 0b00000011);
        vTaskDelay(pdMS_TO_TICKS(10));
        WriteReg(0x02, 0b00000111);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    void ResetIli9342() {
        ESP_LOGI(TAG, "Reset IlI9342");
        WriteReg(0x03, 0b10000001);
        vTaskDelay(pdMS_TO_TICKS(20));
        WriteReg(0x03, 0b10000011);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
};

class Ft6336 : public I2cDevice {
public:
    struct TouchPoint_t {
        int num = 0;
        int x = -1;
        int y = -1;
    };
    
    Ft6336(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
        // ★ 用 Try（探测失败不该 abort；原 ReadReg 会 abort）
        uint8_t chip_id = 0;
        if (TryReadReg(0xA3, &chip_id) == ESP_OK) {
            ESP_LOGI(TAG, "Get chip ID: 0x%02X", chip_id);
        } else {
            ESP_LOGW(TAG, "FT6336 探测失败（触摸可能不可用，但不影响启动）");
        }
        read_buffer_ = new uint8_t[6];
    }

    ~Ft6336() {
        delete[] read_buffer_;
    }

    void UpdateTouchPoint() {
        // ★★★ 修「点屏幕就重启」（addr2line 定位到本函数 L143）
        //   崩溃链：UpdateTouchPoint → I2cDevice::ReadRegs
        //           → _esp_error_check_failed → abort
        //   真因：原实现用 ReadRegs（内含 ESP_ERROR_CHECK），
        //         而 I2C 读【会偶发失败】（Fairy 皮肤持续解 GIF 时总线繁忙）
        //         ⇒ 一次超时就把可恢复错误当致命 ⇒ abort ⇒ 重启
        //
        //   修法（健壮化，不是"少读几次"）：
        //     ① 用不 abort 的 TryReadRegs
        //     ② 失败：限频打日志 + 连续 5 次复位 I2C 总线
        //     ③ ★ 清空触摸点 —— 当作"没按"
        //        不能让旧坐标"粘住"，否则用户以为还在按
        esp_err_t err = TryReadRegs(0x02, read_buffer_, 6);
        if (err != ESP_OK) {
            fail_count_++;
            if (fail_count_ <= 3 || (fail_count_ % 100) == 0) {
                ESP_LOGW(TAG, "FT6336 读取失败(%s)，累计 %d 次",
                         esp_err_to_name(err), fail_count_);
            }
            if (fail_count_ >= 5) {
                ResetBus("FT6336 连续读取失败");
                fail_count_ = 0;
            }
            tp_.num = 0;      // ★ 当作没触摸，避免坐标粘住
            tp_.x = -1;
            tp_.y = -1;
            return;
        }
        fail_count_ = 0;
        tp_.num = read_buffer_[0] & 0x0F;
        tp_.x = ((read_buffer_[1] & 0x0F) << 8) | read_buffer_[2];
        tp_.y = ((read_buffer_[3] & 0x0F) << 8) | read_buffer_[4];
    }

    inline const TouchPoint_t& GetTouchPoint() {
        return tp_;
    }

private:
    uint8_t* read_buffer_ = nullptr;
    TouchPoint_t tp_;
    // ★ 连续 I2C 读失败计数（≥5 次触发总线复位）
    int fail_count_ = 0;
};

class M5StackCoreS3Board : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    Pmic* pmic_;
    Aw9523* aw9523_;
    Ft6336* ft6336_;
    LcdDisplay* display_;
    EspVideo* camera_;
    esp_timer_handle_t touchpad_timer_;
    PowerSaveTimer* power_save_timer_;
    CoreS3Py32Led* fairy_led_ = nullptr;   // 12 颗 RGB（PY32 IO 扩展）
    CoreS3Servo* servo_ = nullptr;         // yaw/pitch 舵机
    // ★ 几何脸显示（display_ 的实际类型，用于切皮肤）
    //   display_ 保持 LcdDisplay* 以复用框架接口，这里存一份具体类型
    // ★ 传感器（摸头 + 摇晃）与待机小动作任务
    StackChanSensor* sensor_ = nullptr;
    TaskHandle_t sensor_task_ = nullptr;
    TaskHandle_t idle_motion_task_ = nullptr;
    // ★ 舵机跟随 / 视线跟踪（用户要求：「跟踪我的脸，舵机跟随我的脸移动，默认关闭」）
    //   实现思路：摄像头出帧 → 人脸检测 → 人脸中心相对画面的偏移 → 舵机 yaw/pitch 跟随
    //   ⚠️ 代价：摄像头常开 + 每帧检测要跑模型（CoreS3 跑得动，但会占 CPU/发热）
    //   ⇒ 默认 false（用户明确要求），菜单里开关，存 NVS（namespace "fairy_gaze"）
    std::atomic<bool> gaze_follow_{false};
    TaskHandle_t gaze_task_ = nullptr;
    uint32_t gaze_detect_cnt_ = 0;     // 限频用（不是每帧都检测）
    bool last_speaking_ = false;

    // ★★★ LVGL 输入设备（indev）—— 修「菜单点不动」
    //   真因：CoreS3 从没把 FT6336 注册成 LVGL 输入设备
    //        ⇒ LVGL 收不到点击 ⇒ 任何 LVGL 按钮都点不动
    //   read_cb 在 LVGL 任务里跑，绝不能做 I2C ⇒ 只读下面 3 个缓存
    lv_indev_t* lvgl_indev_ = nullptr;
    std::atomic<bool> lvgl_touch_pressed_{false};
    std::atomic<int> lvgl_touch_x_{-1};
    std::atomic<int> lvgl_touch_y_{-1};

    // ★ 本地菜单（不依赖服务器）
    std::atomic<bool> menu_open_{false};

    // ★ 菜单创建是否已派发（lv_async_call 只派发一次）
    std::atomic<bool> menu_build_dispatched_{false};
    std::atomic<int> menu_sel_{0};      // 0=切皮肤 1=转头 2=关闭
    std::atomic<int64_t> menu_open_ms_{0};
    std::atomic<uint32_t> pwr_poll_cnt_{0};
    // ★ 本地菜单的 LVGL 对象（真·可点菜单）
    lv_obj_t* menu_root_ = nullptr;      // 半透明遮罩
    lv_obj_t* menu_card_ = nullptr;      // 中央卡片
    lv_obj_t* menu_btns_[3] = {nullptr, nullptr, nullptr};
    lv_obj_t* menu_txts_[3] = {nullptr, nullptr, nullptr};
    // ★ 待机扭头开关（NVS 持久化，默认开）
    // ★ 用户要求（2026-09-19）：「待机转头请默认调成关闭」
    //   原因：默认开着会一直自己转头，用户觉得吵/费舵机
    //   ⇒ 默认 false，需要的人在菜单/控制台里打开（存 NVS）
    std::atomic<bool> auto_motion_{false};
    StackChanGeometryDisplay* geometry_display_ = nullptr;

    void InitializePowerSaveTimer() {
        // 熄屏 300 秒（保留暗屏省电），但【永不自动关机】
        // seconds_to_shutdown = -1 表示禁用关机回调（框架语义，见 power_save_timer.h）
        power_save_timer_ = new PowerSaveTimer(-1, 300, -1);
        power_save_timer_->OnEnterSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(true);
            GetBacklight()->SetBrightness(10);
        });
        power_save_timer_->OnExitSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(false);
            GetBacklight()->RestoreBrightness();
        });
        power_save_timer_->OnShutdownRequest([this]() {
            pmic_->PowerOff();
        });
        power_save_timer_->SetEnabled(true);
    }

    void InitializeI2c() {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = (i2c_port_t)1,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
    }

    void I2cDetect() {
        uint8_t address;
        printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\r\n");
        for (int i = 0; i < 128; i += 16) {
            printf("%02x: ", i);
            for (int j = 0; j < 16; j++) {
                fflush(stdout);
                address = i + j;
                esp_err_t ret = i2c_master_probe(i2c_bus_, address, pdMS_TO_TICKS(200));
                if (ret == ESP_OK) {
                    printf("%02x ", address);
                } else if (ret == ESP_ERR_TIMEOUT) {
                    printf("UU ");
                } else {
                    printf("-- ");
                }
            }
            printf("\r\n");
        }
    }

    void InitializeAxp2101() {
        ESP_LOGI(TAG, "Init AXP2101");
        pmic_ = new Pmic(i2c_bus_, 0x34);
    }

    void InitializeAw9523() {
        ESP_LOGI(TAG, "Init AW9523");
        aw9523_ = new Aw9523(i2c_bus_, 0x58);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    // ══════════════════════════════════════════════════════════════
    // ★ 本地菜单（用户需求：「不依赖服务器的设备上的UI菜单」）
    //
    //   理由：在外面连不上自建服务器时，控制台和语音都用不了
    //        ⇒ 设备本地必须有个入口
    //
    //   入口：① 屏幕长按 ≥500ms  ② 电源键短按
    //   选择：电源键短按 → 轮转
    //   确认：屏幕短按
    // ══════════════════════════════════════════════════════════════
    static constexpr int kMenuItems = 3;
    static constexpr int64_t kMenuTimeoutMs = 8000;   // 8 秒无操作自动关

    bool MenuCanOpen() {
        // 只在空闲时弹（说话/聆听时语音优先，避免打架）
        auto st = Application::GetInstance().GetDeviceState();
        return st == kDeviceStateIdle || st == kDeviceStateStarting;
    }

    // ══════════════════════════════════════════════════════════════
    // ★ 本地菜单 —— 真·可点 UI（用户要求：「浮现出一个手指能选择的菜单，
    //   直接用手点击相应位置即可」）
    //
    //   为什么不用 ShowNotification：
    //     那是文字提示条，不能点，还得靠电源键换项 —— 用户说"太简陋"
    //
    //   现在：屏幕中央弹一个卡片，三个大按钮竖排，手指直接点目标项
    // ══════════════════════════════════════════════════════════════

    // 按钮回调：统一入口，靠 user_data 区分第几项
    static void MenuBtnCb(lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
            return;
        }
        int idx = (int)(intptr_t)lv_event_get_user_data(e);
        // ★ 用 Board::GetInstance()（实测存在，board.h L62）
        //   ⛔ 别用 Application::GetBoard() —— 那个 API 不存在（查过了）
        auto& brd = (M5StackCoreS3Board&)Board::GetInstance();
        brd.MenuActivate(idx);
    }

    // 执行第 idx 项
    void MenuActivate(int idx) {
        ESP_LOGI(TAG, "菜单点击第 %d 项", idx);
        if (idx == 0) {
            // ① 待机转头 开/关（本地 NVS）
            bool now = auto_motion_.load();
            auto_motion_.store(!now);
            SaveAutoMotionToNvs(!now);
            ESP_LOGI(TAG, "菜单：待机转头 → %s", now ? "关" : "开");
            MenuRefreshLabels();
        } else if (idx == 1) {
            // ② 舵机跟随（视线跟踪）开/关（本地 NVS）
            //    ★ 用户要求「默认关闭」—— 见 gaze_follow_ 初值 false
            bool now = gaze_follow_.load();
            gaze_follow_.store(!now);
            SaveGazeFollowToNvs(!now);
            ESP_LOGI(TAG, "菜单：舵机跟随 → %s", now ? "关" : "开");
            MenuRefreshLabels();
        } else {
            // ③ 关闭菜单
            MenuClose();
        }
    }

    void MenuSetText(int i, const char* s) {
        // ★ 注意：本函数必须由【已持锁】的调用者使用（避免嵌套加锁死锁）
        if (i >= 0 && i < 3 && menu_txts_[i] != nullptr) {
            lv_label_set_text(menu_txts_[i], s);
        }
    }

    // 无锁版（调用者必须已持有 LVGL 锁）
    void MenuRefreshLabelsUnlocked() {
        char b0[64], b1[64];
        snprintf(b0, sizeof(b0), "待机转头\n(%s)",
                 auto_motion_.load() ? "开" : "关");
        snprintf(b1, sizeof(b1), "舵机跟随\n(%s)",
                 gaze_follow_.load() ? "开" : "关");
        if (menu_txts_[0]) lv_label_set_text(menu_txts_[0], b0);
        if (menu_txts_[1]) lv_label_set_text(menu_txts_[1], b1);
        if (menu_txts_[2]) lv_label_set_text(menu_txts_[2], "关闭菜单");
    }

    // 带锁版（给非 LVGL 上下文的调用者）
    void MenuRefreshLabels() {
        if (display_ == nullptr) {
            return;
        }
        DisplayLockGuard lock(display_);
        MenuRefreshLabelsUnlocked();
    }

    // ★★★ 让菜单 UI 在【LVGL 任务】里创建（★ 真正修「点屏重启」）
    //
    //   ★ 我上一版判断错了方向（虽然方向对但放错了位置）：
    //     · LVGL 对象必须在【LVGL 任务上下文】创建 —— 这点对
    //     · 但 ApplyBootSkin() 是在【板卡初始化流程】（main 任务）里调的
    //     · 板卡初始化 ≠ LVGL 任务 ⇒ BuildMenuAtInit() 仍在错误上下文
    //     ⇒ LVGL 内部断言失败 ⇒ abort() ⇒ 反复重启
    //       （抓到的崩溃：abort() at PC 0x403866fa，日志停在
    //         "初始化菜单 UI（在正确的 LVGL 上下文）" 之后）
    //
    //   ★ 正解：lv_async_call() 把创建操作【派发到 LVGL 任务】
    //     —— 这是 LVGL 官方推荐的跨任务操作方式。
    //
    //   流程：
    //     ApplyBootSkin（main 任务）→ 派发 → LVGL 任务执行 → 建菜单
    void BuildMenuAtInit() {
        if (display_ == nullptr) {
            ESP_LOGW(TAG, "display_ 为空，菜单不建");
            return;
        }
        if (menu_build_dispatched_.exchange(true)) {
            return;   // 只派发一次
        }
        ESP_LOGI(TAG, "派发菜单创建到 LVGL 任务（lv_async_call）");
        // ★ user_data 传 this —— 回调里再拿回来
        lv_async_call(&M5StackCoreS3Board::MenuBuildAsync, this);
    }

    // ★ lv_async_call 的回调：运行在【LVGL 任务】里，可安全创建对象
    static void MenuBuildAsync(void* user_data) {
        auto* self = static_cast<M5StackCoreS3Board*>(user_data);
        if (self == nullptr) {
            return;
        }
        ESP_LOGI(TAG, "在 LVGL 任务里创建菜单 UI");
        self->MenuBuild();        // 内部自带 DisplayLockGuard
        ESP_LOGI(TAG, "菜单 UI 初始化完成 root=%p", (void*)self->menu_root_);
    }

    void MenuBuild() {
        if (menu_root_ != nullptr) {
            return;                  // ★ 幂等：已建过就不重复建
        }
        if (display_ == nullptr) {
            ESP_LOGW(TAG, "MenuBuild: display_ 为空");
            return;
        }
        // ★★★ 必须加锁！LVGL 非线程安全，本函数在 esp_timer 线程里被调用
        //     （用户实测：不加锁 ⇒ 菜单点不动、关不掉）
        DisplayLockGuard lock(display_);
        lv_obj_t* scr = lv_screen_active();
        if (scr == nullptr) {
            return;
        }
        // 半透明遮罩（吃掉下层点击，防误触）
        // ★ 用板卡的 DISPLAY_WIDTH/HEIGHT 宏（config.h 里定义）
        //   ⛔ 别用 width_/height_ —— 那是 Display 类的成员，板卡里没有
        menu_root_ = lv_obj_create(scr);
        lv_obj_set_size(menu_root_, DISPLAY_WIDTH, DISPLAY_HEIGHT);
        lv_obj_center(menu_root_);
        // ★ 遮罩：极淡的黑（用户要"黑底白字"但别把脸全挡住）
        lv_obj_set_style_bg_color(menu_root_, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(menu_root_, LV_OPA_30, 0);
        lv_obj_set_style_border_width(menu_root_, 0, 0);
        lv_obj_set_style_radius(menu_root_, 0, 0);
        lv_obj_remove_flag(menu_root_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(menu_root_, LV_OBJ_FLAG_CLICKABLE);   // 挡住下层
        // ★★★ 关键：菜单【建好就隐藏】！
        //   否则它是一个【全屏 + CLICKABLE】的遮罩，
        //   会把状态栏的 ☰ 按钮整个盖住 ⇒ 用户「☰ 可见但点不动」
        //   （半透明 40% ⇒ 视觉上还能看到 ☰，但点击全被它吃掉）
        lv_obj_add_flag(menu_root_, LV_OBJ_FLAG_HIDDEN);

        // ★ 抽屉（用户要求：「点开菜单，就从侧边滑出菜单选项」）
        //   贴右边缘、竖长条，宽 150
        menu_card_ = lv_obj_create(menu_root_);
        lv_obj_set_size(menu_card_, 160, DISPLAY_HEIGHT - 20);
        lv_obj_align(menu_card_, LV_ALIGN_RIGHT_MID, -6, 0);
        // ★★ 用户要求：「直接给我做成黑底白字的样式」
        //   ⇒ 纯黑底（不透明）+ 白字 + 平面按钮（无圆角无边框无阴影）
        lv_obj_set_style_bg_color(menu_card_, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(menu_card_, LV_OPA_COVER, 0);   // 不透明纯黑
        lv_obj_set_style_border_width(menu_card_, 1, 0);
        lv_obj_set_style_border_color(menu_card_, lv_color_hex(0x444444), 0);
        lv_obj_set_style_radius(menu_card_, 0, 0);              // 直角，更"平面"
        lv_obj_set_style_pad_all(menu_card_, 10, 0);
        lv_obj_remove_flag(menu_card_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(menu_card_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(menu_card_, LV_FLEX_ALIGN_SPACE_EVENLY,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        // 三个大按钮（高 52，手指好点）
        for (int i = 0; i < 3; i++) {
            lv_obj_t* btn = lv_button_create(menu_card_);
            lv_obj_set_size(btn, 140, 56);   // 大一点，手指好点
            // ★★ 用户要求：黑底白字、平面
            //   底 = 黑，字 = 白，按下 = 反色（白底黑字）作为反馈
            lv_obj_set_style_bg_color(btn, lv_color_black(), 0);
            lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(btn, lv_color_white(), LV_STATE_PRESSED);
            lv_obj_set_style_radius(btn, 0, 0);              // 直角
            lv_obj_set_style_border_width(btn, 1, 0);
            lv_obj_set_style_border_color(btn, lv_color_hex(0x555555), 0);
            lv_obj_set_style_shadow_width(btn, 0, 0);
            lv_obj_set_style_pad_all(btn, 4, 0);
            lv_obj_add_event_cb(btn, MenuBtnCb, LV_EVENT_CLICKED,
                                (void*)(intptr_t)i);
            lv_obj_t* lb = lv_label_create(btn);
            lv_obj_set_style_text_color(lb, lv_color_white(), 0);
            // 按下时反色（白底 ⇒ 黑字）
            lv_obj_set_style_text_color(lb, lv_color_black(),
                                        LV_STATE_PRESSED);
            lv_obj_set_style_text_align(lb, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_center(lb);
            menu_btns_[i] = btn;
            menu_txts_[i] = lb;
        }
        MenuRefreshLabels();
        lv_obj_add_flag(menu_root_, LV_OBJ_FLAG_HIDDEN);   // 默认隐藏
        ESP_LOGI(TAG, "菜单 UI 已建好");
    }

    void MenuOpen() {
        // ★ 用户决定：菜单功能停用（2026-09-19）
        //   ⇒ 任何路径都开不了菜单（语音/按钮/热区）
        ESP_LOGI(TAG, "菜单已停用，忽略打开请求");
        return;
        if (menu_open_.load()) {
            return;                  // 已开，忽略（点按钮才算）
        }
        if (!MenuCanOpen()) {
            ESP_LOGI(TAG, "菜单：非空闲状态，忽略");
            return;
        }
        // ★★★ 修「点屏幕就重启」：
        //   本函数在 esp_timer 线程被调用，【绝不能在这里创建 LVGL 对象】。
        //   菜单 UI 已在初始化时建好（见 BuildMenuAtInit）。
        //   若还没建（初始化失败），直接拒绝 —— 不要再建，否则设备崩。
        if (menu_root_ == nullptr) {
            ESP_LOGW(TAG, "菜单对象未初始化，忽略（避免在错误上下文建 LVGL 对象）");
            return;
        }
        {
            // ★ 运行时只做「显示」—— 加锁即可（不涉及对象创建）
            DisplayLockGuard lock(display_);
            MenuRefreshLabelsUnlocked();
            lv_obj_clear_flag(menu_root_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(menu_root_);
            // ★★★ 菜单遮罩是【全屏 CLICKABLE】，把 ☰ 一起提到最前，
            //   否则菜单打开后 ☰ 被盖住 ⇒ 关不掉菜单
            if (display_ != nullptr) {
                display_->RaiseMenuButton();
            }
        }
        menu_open_.store(true);
        menu_open_ms_.store(esp_timer_get_time() / 1000);
        ESP_LOGI(TAG, "本地菜单已打开");
    }

    void MenuClose() {
        if (!menu_open_.load()) {
            return;
        }
        menu_open_.store(false);
        if (menu_root_ != nullptr && display_ != nullptr) {
            DisplayLockGuard lock(display_);   // ★ 加锁
            lv_obj_add_flag(menu_root_, LV_OBJ_FLAG_HIDDEN);
        }
        ESP_LOGI(TAG, "本地菜单已关闭");
    }

    // 菜单超时检查（由轮询定时器调）
    void MenuTick() {
        if (!menu_open_.load()) {
            return;
        }
        int64_t now_ms = esp_timer_get_time() / 1000;
        if (now_ms - menu_open_ms_.load() > kMenuTimeoutMs) {
            ESP_LOGI(TAG, "菜单超时自动关闭");
            MenuClose();
        }
    }

    // ★★★ 驱动 avatar 的每帧更新（装饰器动画 + 自动销毁）
    //   官方：stackchan.h L136 `_avatar->update();`
    //   ⛔ 不调 ⇒ 爱心/晕眩静止 + 永不过期盖住表情（用户实测）
    void PollAvatarTick() {
        if (geometry_display_ == nullptr) {
            return;
        }
        geometry_display_->AvatarTick();
    }

    // ★ 说话嘴动画驱动（用户报：「说话的时候嘴怎么不会动，只有一条线」）
    //   读设备状态 → 告诉几何脸层"在说话/说完了"
    //   TickMouth 内部自己限频到 180ms（与官方 SpeakingModifier 一致）
    void PollSpeaking() {
        if (geometry_display_ == nullptr) {
            return;
        }
        auto st = Application::GetInstance().GetDeviceState();
        bool speaking = (st == kDeviceStateSpeaking);
        if (speaking != last_speaking_) {
            last_speaking_ = speaking;
            geometry_display_->SetSpeaking(speaking);
        }
        geometry_display_->TickMouth();
    }

    // 电源键轮询（挂在触摸轮询定时器里，20ms 一次 ⇒ 每 5 次查一遍 = 100ms）
    void PollPowerKey() {
        uint32_t c = pwr_poll_cnt_.load() + 1;
        pwr_poll_cnt_.store(c);
        if (c % 5 != 0) {
            return;              // 100ms 查一次足够（避免 I2C 太频繁）
        }
        if (pmic_ == nullptr) {
            return;
        }
        if (pmic_->ConsumeShortPress()) {
            // ★ 用户要求：「把短按电源改成切换皮肤，从菜单里把切换皮肤移走」
            //   ⇒ 电源键短按 = 本地切皮肤（不需要服务器、不需要连网）
            auto cur = stackchan_skin::LoadSkin();
            auto nxt = (cur == stackchan_skin::Skin::Geometry)
                           ? stackchan_skin::Skin::Fairy
                           : stackchan_skin::Skin::Geometry;
            ESP_LOGI(TAG, "电源键短按 → 切皮肤 %s → %s",
                     stackchan_skin::SkinName(cur),
                     stackchan_skin::SkinName(nxt));
            if (menu_open_.load()) {
                MenuClose();
            }
            stackchan_skin::SwitchSkin(nxt, true);   // 写 NVS + 换 OTA + 重启
        }
    }

    void PollTouchpad() {
        static bool was_touched = false;
        static int64_t touch_start_time = 0;
        const int64_t TOUCH_THRESHOLD_MS = 500;  // 触摸时长阈值，超过500ms视为长按
        
        ft6336_->UpdateTouchPoint();
        auto& touch_point = ft6336_->GetTouchPoint();

        // ★ 诊断日志：触摸驱动读到的原始坐标（限频 1 秒）
        //   与 read_cb 的日志对比：
        //     · 都有 ⇒ 坐标正常，问题在 LVGL 命中判定
        //     · 只有这里 ⇒ LVGL 的 read_cb 没被调用（indev 没接好）
        if (touch_point.num > 0) {
            static int64_t last_pt_log = 0;
            int64_t now_ms = esp_timer_get_time() / 1000;
            if (now_ms - last_pt_log > 1000) {
                last_pt_log = now_ms;
                ESP_LOGI(TAG, "FT6336 触摸: num=%d (%d,%d)",
                         touch_point.num, touch_point.x, touch_point.y);
            }
        }

        // ★ 同步给 LVGL 输入设备（缓存坐标，read_cb 只读缓存）
        {
            bool pressed = (touch_point.num > 0);
            lvgl_touch_pressed_.store(pressed);
            if (pressed) {
                lvgl_touch_x_.store(touch_point.x);
                lvgl_touch_y_.store(touch_point.y);
            }
        }
        
        // 检测触摸开始
        if (touch_point.num > 0 && !was_touched) {
            was_touched = true;
            touch_start_time = esp_timer_get_time() / 1000; // 转换为毫秒
        }
        // ★ 用户要求：「不要使用长按的方式呼出菜单，只用菜单横线呼出」
        //   ⇒ 长按不再开菜单（整个分支删除）
        //   注意：touch_long_handled_ 的判定也一并去掉，避免残留行为
        // 检测触摸释放
        else if (touch_point.num == 0 && was_touched) {
            was_touched = false;
            int64_t touch_duration = (esp_timer_get_time() / 1000) - touch_start_time;
            
            // ★ 用户要求：长按不再呼出菜单（只用状态栏 ☰）
            //   ⇒ 抬起时的长按判定整段删除
            //   ⇒ 触摸只做两件事：
            //       ① 短按（菜单没开）→ 原行为（状态栏提示 + 切聊天状态）
            //       ② 菜单开着 → 交给 LVGL（按钮自己处理点击）
            (void)touch_duration;   // 保留变量避免 unused 警告
            // 短按：菜单开着 ⇒ 不干预（让 LVGL 按钮自己处理点击）
            //   ★ 用户要求「直接用手点击相应位置即可」⇒ 不要再用"点屏=确认"
            if (menu_open_.load()) {
                return;
            }
            // 短触（菜单没开）⇒ 原行为：状态栏 + 聊天
            if (display_ != nullptr) {
                display_->ShowTopBarTemporarily();
            }
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        }
    }

    // ══════════════════════════════════════════════════════════════
    // ★★★ LVGL 输入设备（indev）—— 菜单「点不动」的真因
    //
    //   问题：CoreS3 板卡从没把 FT6336 注册成 LVGL 输入设备。
    //         FT6336 只被 PollTouchpad() 读（供小智自身逻辑），
    //         而 LVGL 完全不知道屏幕被点过
    //         ⇒ 任何 LVGL 按钮（我的菜单、以后加的 UI）都点不动。
    //
    //   对比其他板卡：都有 lvgl_port_add_touch() 或手写 lv_indev_create()
    //
    //   修法：建一个 LVGL 指针设备，read_cb 只用【缓存坐标】
    //     ⚠️ read_cb 在 LVGL 任务上下文里跑，绝不能做 I2C 读
    //        ⇒ 缓存由 PollTouchpad()（20ms）刷新
    // ══════════════════════════════════════════════════════════════
    // ★ read_cb 在 LVGL 任务上下文里被调用 ⇒ 必须加 LVGL 锁
    //   （官方 hal.cpp L286/300 用 hal_bridge::lock()/unlock() 包住）
    static void LvglTouchReadCb(lv_indev_t* indev, lv_indev_data_t* data) {
        M5StackCoreS3Board* self =
            (M5StackCoreS3Board*)lv_indev_get_user_data(indev);
        if (self == nullptr) {
            data->state = LV_INDEV_STATE_RELEASED;
            return;
        }
        // ★ 只读缓存（无 I2C —— I2C 会阻塞 LVGL 任务）
        if (self->lvgl_touch_pressed_.load()) {
            data->state   = LV_INDEV_STATE_PRESSED;
            data->point.x = self->lvgl_touch_x_.load();
            data->point.y = self->lvgl_touch_y_.load();
            // ★ 诊断日志：确认 LVGL 真在收触摸（限频 1 秒，防刷屏）
            //   与 PollTouchpad 的日志对比：
            //     · 两条都有 ⇒ 坐标正常，问题在 LVGL 命中判定
            //     · 只有 PollTouchpad 有 ⇒ indev 没接进 LVGL 输入循环
            static int64_t s_last_touchdbg = 0;
            int64_t now_dbg = esp_timer_get_time() / 1000;
            if (now_dbg - s_last_touchdbg > 1000) {
                s_last_touchdbg = now_dbg;
                ESP_LOGI("TOUCHDBG", "LVGL read_cb PRESSED (%d,%d)",
                         (int)data->point.x, (int)data->point.y);
            }
        } else {
            data->state = LV_INDEV_STATE_RELEASED;
        }
    }

    void InitializeLvglInput() {
        if (lvgl_indev_ != nullptr) {
            return;
        }
        // ★ 对齐官方 hal.cpp L303-317：
        //   · 注册过程包在 LVGL 锁内（disply_lvgl_lock/unlock）
        //   · set_group / set_display 两句不能少
        //     （缺 set_group ⇒ 键盘/编码器类事件收不到；
        //       缺 set_display ⇒ 多显示器时路由不到正确的屏）
        if (display_ != nullptr) {
            DisplayLockGuard lock(display_);
            lvgl_indev_ = lv_indev_create();
            lv_indev_set_type(lvgl_indev_, LV_INDEV_TYPE_POINTER);
            lv_indev_set_read_cb(lvgl_indev_, LvglTouchReadCb);
            lv_indev_set_user_data(lvgl_indev_, this);
            // ★ 官方同款（hal.cpp L313-314）
            lv_indev_set_group(lvgl_indev_, lv_group_get_default());
            lv_indev_set_display(lvgl_indev_, lv_display_get_default());
        } else {
            lvgl_indev_ = lv_indev_create();
            lv_indev_set_type(lvgl_indev_, LV_INDEV_TYPE_POINTER);
            lv_indev_set_read_cb(lvgl_indev_, LvglTouchReadCb);
            lv_indev_set_user_data(lvgl_indev_, this);
        }
        ESP_LOGI(TAG, "LVGL 输入设备已注册（触摸可用）");
    }

    void InitializeFt6336TouchPad() {
        ESP_LOGI(TAG, "Init FT6336");
        ft6336_ = new Ft6336(i2c_bus_, 0x38);
        
        // 创建定时器，20ms 间隔
        esp_timer_create_args_t timer_args = {
            .callback = [](void* arg) {
                M5StackCoreS3Board* board = (M5StackCoreS3Board*)arg;
                board->PollTouchpad();
                board->PollPowerKey();   // ★ 电源键轮询（内部 100ms 一次）
                board->MenuTick();       // ★ 菜单超时自动关闭
                board->PollSpeaking();   // ★ 说话时驱动嘴开合
                board->PollAvatarTick(); // ★★★ 驱动 avatar 每帧更新（装饰器动画+销毁）
            },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "touchpad_timer",
            .skip_unhandled_events = true,
        };
        
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &touchpad_timer_));
        ESP_ERROR_CHECK(esp_timer_start_periodic(touchpad_timer_, 20 * 1000));
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = GPIO_NUM_37;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = GPIO_NUM_36;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeIli9342Display() {
        ESP_LOGI(TAG, "Init IlI9342");

        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = GPIO_NUM_3;
        io_config.dc_gpio_num = GPIO_NUM_35;
        io_config.spi_mode = 2;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io, &panel_config, &panel));
        
        esp_lcd_panel_reset(panel);
        aw9523_->ResetIli9342();

        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, true);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);

        // ★ 用自定义显示类（含官方几何脸，第二套皮肤）
        //   它继承 SpiLcdDisplay，多的能力是 SetGeometryVisible()
        auto* geom = new StackChanGeometryDisplay(
            panel_io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT,
            DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X,
            DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
        display_ = geom;
        geometry_display_ = geom;
        // ★ 注入开机皮肤钩子：SetupUI 建好 UI 后回调 ApplyBootSkin()
        geom->SetBootSkinHook([this]() { ApplyBootSkin(); });
        ESP_LOGI(TAG, "using StackChanGeometryDisplay");
    }

     void InitializeCamera() {
        static esp_cam_ctlr_dvp_pin_config_t dvp_pin_config = {
            .data_width = CAM_CTLR_DATA_WIDTH_8,
            .data_io = {
                [0] = CAMERA_PIN_D0,
                [1] = CAMERA_PIN_D1,
                [2] = CAMERA_PIN_D2,
                [3] = CAMERA_PIN_D3,
                [4] = CAMERA_PIN_D4,
                [5] = CAMERA_PIN_D5,
                [6] = CAMERA_PIN_D6,
                [7] = CAMERA_PIN_D7,
            },
            .vsync_io = CAMERA_PIN_VSYNC,
            .de_io = CAMERA_PIN_HREF,
            .pclk_io = CAMERA_PIN_PCLK,
            .xclk_io = CAMERA_PIN_XCLK,
        };

        esp_video_init_sccb_config_t sccb_config = {
            .init_sccb = false,
            .i2c_handle = i2c_bus_,
            .freq = 100000,
        };

        esp_video_init_dvp_config_t dvp_config = {
            .sccb_config = sccb_config,
            .reset_pin = CAMERA_PIN_RESET,
            .pwdn_pin = CAMERA_PIN_PWDN,
            .dvp_pin = dvp_pin_config,
            .xclk_freq = XCLK_FREQ_HZ,
        };

        esp_video_init_config_t video_config = {
            .dvp = &dvp_config,
        };

        camera_ = new EspVideo(video_config);
    }

public:
    M5StackCoreS3Board() {
        InitializePowerSaveTimer();
        InitializeI2c();
        InitializeAxp2101();
        InitializeAw9523();
        I2cDetect();
        InitializeSpi();
        InitializeIli9342Display();
        InitializeCamera();
        InitializeFt6336TouchPad();
        InitializeLvglInput();   // ★ LVGL 输入设备（否则按钮点不动）
        InitializeFairyRgb();
        InitializeServo();
        InitializeSensors();   // ★ 传感器（摸头/摇晃）+ 待机小动作
        // ★ 皮肤：读 NVS 决定默认显示哪套（几何脸要在 UI 建好后才能挂）
        //   这里只注册 MCP 工具 + 记下皮肤；实际切换在 SetupUI 之后
        stackchan_skin::RegisterMcpTools();
        ESP_LOGW(TAG, "skin at boot = %s",
                 stackchan_skin::SkinName(stackchan_skin::LoadSkin()));
        GetBacklight()->RestoreBrightness();
    }

    // ★ 应用启动皮肤：必须等 UI 建好（SetupUI）后调用
    void ApplyBootSkin() {
        if (geometry_display_ == nullptr) {
            return;
        }
        auto skin = stackchan_skin::LoadSkin();
        bool geom = (skin == stackchan_skin::Skin::Geometry);
        geometry_display_->SetGeometryVisible(geom);
        ESP_LOGW(TAG, "boot skin applied: %s",
                 stackchan_skin::SkinName(skin));
        // ★ 顺便接上状态栏菜单按钮（此时 UI 已建好）
        HookMenuButton();
        // ★★★ 在这里把菜单 UI 建好（★ 修「点屏重启」）
        //   必须在 LVGL 上下文创建对象；运行时只显示/隐藏
        BuildMenuAtInit();
    }

    // ★ 接线：状态栏最右的 ☰ 按钮 → 打开/关闭侧边抽屉菜单
    //   （用户要求：「状态栏最右边加入菜单选项的图标，
    //     然后点开菜单，就从侧边滑出菜单选项」）
    void HookMenuButton() {
        // ★★★ 用户决定（2026-09-19）：「算了你tm这个功能别做了」
        //   ⇒ 菜单功能整体停用：
        //     · 不再给 ☰ 接线（点了也不做事）
        //     · 菜单 UI 不显示
        //     · 待机转头/舵机跟随改用【语音控制】（MCP 工具）
        //   注意：菜单 UI 对象仍在（初始化时建过），但不显示、不可达 ——
        //        保留对象而不删，避免动到初始化时序（更稳）。
        ESP_LOGI(TAG, "菜单功能已按用户要求停用（改用语音控制）");
    }

    virtual Led* GetLed() override {
        return fairy_led_;
    }

    // ★★★ 在 OTA 上报里带上【当前皮肤】（2026-09-19）
    //   修用户反馈：「之前按电源键切换皮肤，服务器也能切，不知为何不行了」
    //
    //   机制：设备每次连服务器都走 OTA，携带 GetSystemInfoJson()，
    //         其中 "board" 段是本函数的返回值。服务器 ota_handler
    //         会解析 data_json["board"] ⇒ 只要这里带上 skin，
    //         服务器就能在设备上线时自动对齐配置。
    //
    //   为什么必须这样：按电源键切皮肤只写设备自己的 NVS，
    //   不通知服务器；OTA 是【设备 → 服务器】的现成通道。
    std::string GetBoardJson() override {
        std::string json = WifiBoard::GetBoardJson();
        std::string skin =
            stackchan_skin::SkinName(stackchan_skin::LoadSkin());
        if (!skin.empty() && !json.empty() && json.back() == '}') {
            json.pop_back();                       // 去掉末尾 }
            if (!json.empty() && json.back() != '{') {
                json += ",";                       // 非空对象要加逗号
            }
            json += "\"skin\":\"" + skin + "\"}";
        }
        return json;
    }

    // StackChan 的 12 颗 RGB 挂在 PY32 IO 扩展上（不是直连 GPIO），
    // 所以需要先起 I2C，再让 Led 自己初始化 IO 扩展。
    void InitializeFairyRgb() {
        if (i2c_bus_ == nullptr) {
            ESP_LOGW(TAG, "i2c bus not ready, skip RGB init");
            return;
        }
        fairy_led_ = new CoreS3Py32Led(i2c_bus_);
        if (!fairy_led_->Init()) {
            ESP_LOGW(TAG, "Fairy RGB init failed (PY32 not found?)");
        } else {
            fairy_led_->RegisterMcpTools();  // 让 AI 能控制灯（关灯/换色）
            ESP_LOGI(TAG, "RGB ready, MCP tools registered");
        }
    }

    // StackChan 的两个飞特总线舵机走 UART1（1Mbps, TX=GPIO6, RX=GPIO7）。
    // 舵机供电由 PY32 IO 扩展 pin0(VM EN) 控制 —— RGB 的 Init() 里已使能，
    // 所以本函数必须【在 InitializeFairyRgb() 之后】调用。
    // ★ 传感器：SI12T 触摸 + BMI270 摇晃，并起两个后台任务
    //   ① 轮询任务：摸头 → 开心；摇晃 → 晕眩
    //   ② 待机小动作：idle 时随机轻微转头（还原出厂固件那个"没事动一动"）
    void InitializeSensors() {
        if (i2c_bus_ == nullptr) {
            ESP_LOGW(TAG, "i2c bus not ready, skip sensors");
            return;
        }
        sensor_ = new StackChanSensor(i2c_bus_);
        if (!sensor_->Init()) {
            ESP_LOGW(TAG, "传感器初始化失败（触摸/IMU 都不可用）");
        } else {
            sensor_->RegisterMcpTools();
        }

        // ① 轮询任务（10ms 一次：触摸要快速响应）
        xTaskCreate(
            [](void* arg) {
                auto* self = static_cast<M5StackCoreS3Board*>(arg);
                while (true) {
                    if (self->sensor_ != nullptr) {
                        self->sensor_->Poll();
                        if (self->sensor_->TakePetEvent()) {
                            self->ReactPet();
                        }
                        if (self->sensor_->TakeShakeEvent()) {
                            self->ReactShake();
                        }
                    }
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
            },
            "sens_poll", 3072, this, 3, &sensor_task_);

        // ② 待机小动作任务（每 6~14 秒随机转头一次，只在 idle 时动）
        xTaskCreate(
            [](void* arg) {
                auto* self = static_cast<M5StackCoreS3Board*>(arg);
                while (true) {
                    // 随机间隔 6~14 秒
                    uint32_t wait_ms = 6000 + (esp_random() % 8000);
                    vTaskDelay(pdMS_TO_TICKS(wait_ms));
                    if (self->servo_ == nullptr) {
                        continue;
                    }
                    // ★ 开关关了就跳过（用户可在控制台关掉）
                    if (!self->auto_motion_.load()) {
                        continue;
                    }
                    auto st = Application::GetInstance().GetDeviceState();
                    if (st != kDeviceStateIdle) {
                        continue;   // 说话/听的时候别乱动
                    }
                    // 随机取一个目标 yaw（±40 度内）
                    int delta = (int)(esp_random() % 81) - 40;
                    if (delta > -12 && delta < 12) {
                        delta = (delta >= 0) ? 18 : -18;   // 避免几乎不动的角度
                    }
                    // ★ 恢复原始语义：MoveTo(yaw, pitch)
                    //   pitch=45°（默认俯仰），speed 用默认 600
                    //   （此前我误以为 45 是 speed，改成了 GetPitchAngle()
                    //     ⇒ 待机转头不生效，已回滚）
                    self->servo_->MoveTo(
                        static_cast<float>(delta), 45.0f);
                    ESP_LOGI(TAG, "idle motion: yaw=%d", delta);
                    // 停 2 秒后回正
                    vTaskDelay(pdMS_TO_TICKS(2000));
                    if (Application::GetInstance().GetDeviceState() ==
                        kDeviceStateIdle) {
                        // ★ 恢复：回正（pitch=45）
                        self->servo_->MoveTo(0.0f, 45.0f);
                    }
                }
            },
            "idle_mot", 3072, this, 2, &idle_motion_task_);

        // ★ 待机扭头开关：从 NVS 读（没存过 = 默认开）并注册 MCP 工具
        LoadAutoMotionFromNvs();
        RegisterMotionMcpTools();
        // ★ 视线跟踪开关：从 NVS 读（用户要求默认关）
        LoadGazeFollowFromNvs();
        ESP_LOGI(TAG, "传感器任务与待机小动作任务已启动");
    }

    // ★ 待机扭头开关：从 NVS 读（没存过 = 默认开）
    void LoadAutoMotionFromNvs() {
        Settings settings("fairy_motion", false);
        // 哨兵值：没存过返回 -1 ⇒ 保持默认（开）
        int32_t v = settings.GetInt("auto_motion", -1);
        if (v >= 0) {
            auto_motion_.store(v != 0);
        }
        ESP_LOGI(TAG, "auto_motion NVS=%d → %s", (int)v,
                 auto_motion_.load() ? "开" : "关");
    }

    // ★ 视线跟踪开关的 NVS 读写（namespace "fairy_gaze"，与灯光/舵机隔离）
    void LoadGazeFollowFromNvs() {
        Settings settings("fairy_gaze", false);
        int32_t v = settings.GetInt("follow", -1);
        if (v >= 0) {
            gaze_follow_.store(v != 0);
        }
        ESP_LOGI(TAG, "gaze_follow NVS=%d → %s（默认关）", (int)v,
                 gaze_follow_.load() ? "开" : "关");
    }

    void SaveGazeFollowToNvs(bool on) {
        Settings settings("fairy_gaze", true);
        settings.SetInt("follow", on ? 1 : 0);
    }

    void SaveAutoMotionToNvs(bool on) {
        Settings settings("fairy_motion", true);
        settings.SetInt("auto_motion", on ? 1 : 0);
    }

    // ★ 注册 MCP 工具（让 AI/控制台都能开关待机扭头）
    void RegisterMotionMcpTools() {
        auto& mcp = McpServer::GetInstance();

        mcp.AddTool("self.motion.set_auto",
            "开启或关闭「待机时自己随机轻微转头」这个动作，并永久保存。"
            "enabled 传 true=开启 / false=关闭。"
            "用户说「别乱动」「待机别转头」时传 false。",
            PropertyList({
                Property("enabled", kPropertyTypeBoolean, true),
            }),
            [this](const PropertyList& p) -> ReturnValue {
                bool on = p["enabled"].value<bool>();
                auto_motion_.store(on);
                SaveAutoMotionToNvs(on);
                return std::string(std::string("待机自动转头已") +
                                   (on ? "开启" : "关闭") + "并保存");
            });

        mcp.AddTool("self.motion.get_auto",
            "查询「待机时自动转头」当前是开还是关。",
            PropertyList(),
            [this](const PropertyList& p) -> ReturnValue {
                (void)p;
                return std::string(auto_motion_.load() ? "开" : "关");
            });
    }

    // ★ 摸头反应：开心表情（几何脸模式下走官方几何表情）
    void ReactPet() {
        ESP_LOGI(TAG, "摸头 → 开心 + 冒爱心");
        auto display = Board::GetInstance().GetDisplay();
        if (display != nullptr) {
            display->SetEmotion("happy");
        }
        // ★ 冒爱心（用户要求：「冒爱心」）
        //   装饰器代码早就在，之前是【漏调用】⇒ 从不显示
        if (geometry_display_ != nullptr) {
            geometry_display_->ShowHeart();
        }
        // 顺带轻轻抬头（像被摸头的反应）
        if (servo_ != nullptr) {
            // ★★ 恢复原始：摸头轻轻【抬头】到 55°
            //   （此前我误改成 GetPitchAngle() ⇒ 头不动 ⇒ 用户报"不会抬头"）
            servo_->MoveTo(0.0f, 55.0f);
            vTaskDelay(pdMS_TO_TICKS(700));
        }
    }

    // ★ 摇晃反应：晕眩（用户要求：「甩晕」）
    //   IMU 现已真正启用（BMI270 @ 0x69，与 SI12T 的 0x68 不冲突）
    void ReactShake() {
        ESP_LOGI(TAG, "摇晃 → 晕眩");
        auto display = Board::GetInstance().GetDisplay();
        if (display != nullptr) {
            display->SetEmotion("doubtful");
        }
        // ★ 转圈晕眩装饰器
        if (geometry_display_ != nullptr) {
            geometry_display_->ShowDizzy();
        }
        // 晕眩：左右快速摆两下
        if (servo_ != nullptr) {
            // ★ 恢复原始：甩晕左右摆头（pitch=45）
            servo_->MoveTo(-30.0f, 45.0f);
            vTaskDelay(pdMS_TO_TICKS(180));
            servo_->MoveTo(30.0f, 45.0f);
            vTaskDelay(pdMS_TO_TICKS(180));
            servo_->MoveTo(0.0f, 45.0f);
        }
    }

    void InitializeServo() {
        servo_ = new CoreS3Servo();
        if (servo_->Init()) {
            servo_->RegisterMcpTools();   // 让 AI 能控制转头
            ESP_LOGI(TAG, "servo ready, MCP tools registered");
        } else {
            ESP_LOGW(TAG, "servo init failed (check wiring / servo power)");
        }
    }

    virtual AudioCodec* GetAudioCodec() override {
        static CoreS3AudioCodec audio_codec(i2c_bus_,
            AUDIO_INPUT_SAMPLE_RATE,
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK,
            AUDIO_I2S_GPIO_BCLK,
            AUDIO_I2S_GPIO_WS,
            AUDIO_I2S_GPIO_DOUT,
            AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_AW88298_ADDR,
            AUDIO_CODEC_ES7210_ADDR,
            AUDIO_INPUT_REFERENCE);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Camera* GetCamera() override {
        return camera_;
    }

    virtual bool GetBatteryLevel(int &level, bool& charging, bool& discharging) override {
        static bool last_discharging = false;
        charging = pmic_->IsCharging();
        discharging = pmic_->IsDischarging();
        if (discharging != last_discharging) {
            power_save_timer_->SetEnabled(discharging);
            last_discharging = discharging;
        }

        level = pmic_->GetBatteryLevel();
        return true;
    }

    virtual void SetPowerSaveLevel(PowerSaveLevel level) override {
        if (level != PowerSaveLevel::LOW_POWER) {
            power_save_timer_->WakeUp();
        }
        WifiBoard::SetPowerSaveLevel(level);
    }

    virtual Backlight *GetBacklight() override {
        static CustomBacklight backlight(pmic_);
        return &backlight;
    }
};

DECLARE_BOARD(M5StackCoreS3Board);
