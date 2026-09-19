#ifndef I2C_DEVICE_H
#define I2C_DEVICE_H

#include <driver/i2c_master.h>

class I2cDevice {
public:
    I2cDevice(i2c_master_bus_handle_t i2c_bus, uint8_t addr);

protected:
    i2c_master_bus_handle_t i2c_bus_;
    i2c_master_dev_handle_t i2c_device_;
    uint8_t device_address_;

    void WriteReg(uint8_t reg, uint8_t value);
    void WriteRegs(uint8_t reg, const uint8_t* buffer, size_t length);
    uint8_t ReadReg(uint8_t reg);
    void ReadRegs(uint8_t reg, uint8_t* buffer, size_t length);
    // ★ 不 abort 的版本（★ 修「I2C 偶发失败 ⇒ abort ⇒ 设备重启」）
    //   原版用 ESP_ERROR_CHECK ⇒ 一次超时就 abort 重启。
    //   触摸这类【可恢复】读取必须用 Try*：失败返回错误码，
    //   由调用方决定（忽略/重试/复位总线）。
    esp_err_t TryReadRegs(uint8_t reg, uint8_t* buffer, size_t length);
    esp_err_t TryReadReg(uint8_t reg, uint8_t* out);

    esp_err_t ResetBus(const char* reason);
};

#endif  // I2C_DEVICE_H
