# Tesla BLE Dashboard — 项目上下文

## 硬件
- ESP32-S3 (芯片版本 v0.2, Flash 2MB, PSRAM 8MB)
- OLED: SSD1306 I2C, 128x32, SDA=GPIO41, SCL=GPIO42, addr=0x3C
- VIN: LRWYGCFS2PC792568
- Tesla BLE 名称: Sa7785a96101d0c3fC (SHA1 前8字节)

## 开发环境
- ESP-IDF v5.5.4 (需安装到 C:\Espressif\ 或 macOS 等效路径)
- 编译器: xtensa-esp-elf-gcc 14.2.0 (esp-14.2.0_20260121)
- Python: idf5.5_py3.10_env
- COM口: 台式机 COM7，笔记本需要 `ls /dev/cu.*` 确认

## 项目架构
```
main/
├── main.cpp              # 主循环 + 配对流程
├── ble_adapter.cpp/.h    # NimBLE GATT 客户端 (BleAdapter 接口实现)
├── storage_adapter.cpp/.h # NVS 持久化 (StorageAdapter 接口实现)
├── display.cpp/.h        # SSD1306 I2C 自驱 (无外部依赖)
├── dash_data.h           # 仪表数据结构 + 单位转换
├── CMakeLists.txt        # 主组件构建
└── Kconfig.projbuild     # OLED 引脚配置 (menuconfig)
components/tesla-ble/     # yoziru/tesla-ble v5.1.1 (本地克隆，无 .git)
sdkconfig.defaults        # 默认 Kconfig 值
build_only.bat            # 仅编译
flash.bat                 # 编译 + COM7 烧录
PROJECT_PLAN.md           # 详细设计文档
```

## 编译烧录
```bash
# macOS
export IDF_PATH=~/esp/esp-idf-v5.5.4
# 设置目标
idf.py set-target esp32s3
# 编译
idf.py build
# 烧录 (替换为实际串口)
idf.py -p /dev/cu.usbmodem* flash
# 监控
idf.py -p /dev/cu.usbmodem* monitor
```

## 已知问题与修复历史
1. ~~BLE 名称后缀过滤太严格~~ → 已支持 C/D/R/P 全部后缀
2. ~~被动扫描收不到设备名~~ → 改为主动扫描 (passive=0)
3. ~~设备名在 AD 结构中间，从 data[0] 比较失败~~ → 添加 parse_ble_name() 解析 AD 结构
4. ~~NimBLE host 栈溢出~~ → 增大到 8KB，断连后用延迟扫描避免回调递归
5. ~~服务发现超时~~ → 连接后 MTU 交换 + 1.5s 稳定延迟
6. ~~旧无效密钥导致跳过配对~~ → 连接后先测试密钥有效性，无效则自动清除重配

## 当前状态
- 编译通过，bin 大小 ~700KB
- BLE 扫描正常：能找到 Sa7785a96101d0c3fC
- BLE 连接：靠近车辆时能连接成功 (已确认)
- 服务发现：加了 MTU 交换 + 延迟后待验证
- 配对流程：连接后自动检测密钥有效性，无效则清除重配

## 关键 NimBLE API 注意事项 (ESP-IDF v5.5)
- GATT 服务发现回调: ble_gatt_disc_svc_fn(conn, error, svc, arg) — svc=NULL 表示完成
- GATT 特征发现回调: ble_gatt_chr_fn(conn, error, chr, arg) — chr=NULL 表示完成
- CCCD 写入回调: ble_gatt_attr_fn (不是 ble_gatt_access_ctxt)
- ble_hs_cfg 无 sync_cb_arg 字段 → 用静态 instance_ 指针
- esp_nimble_hci_init() 替代 esp_nimble_hci_and_controller_init()
- nimble_port_freertos_init(TaskFunction_t) 只接受一个参数
- 结构体: ble_gap_disc_params, ble_gap_conn_params (无 _t 后缀)

## nanopb 字段名 (yoziru/tesla-ble 生成代码)
- ShiftState: which_type (不是 which_ShiftType)
- DriveState.speed_float: which_optional_speed_float / optional_speed_float.speed_float
- DriveState.speed: which_optional_speed / optional_speed.speed
- DriveState.odometer: which_optional_odometer_in_hundredths_of_a_mile / optional_odometer_in_hundredths_of_a_mile.odometer_in_hundredths_of_a_mile
- VCSEC sleep: VCSEC_VehicleSleepStatus_E_VEHICLE_SLEEP_STATUS_AWAKE
- Keys role: Keys_Role_ROLE_OWNER (全局命名空间，不在 TeslaBLE:: 下)

## ESP-IDF build.cmake 修改
文件: C:\Espressif\frameworks\esp-idf-v5.5.4\tools\cmake\build.cmake:137
添加了 "-Wno-error=format" (xtensa int32_t=long int 导致格式警告)
macOS 上需要做同样的修改。

## Tesla BLE 协议要点
- Service UUID: 00000211-b2d1-43f0-9b88-960cebf8b91e
- Write char: 00000212-..., Read/Notify char: 00000213-...
- 认证: ECDH secp256r1 + AES-128-GCM
- 消息: 2字节大端长度前缀 + RoutableMessage protobuf
- 两个域: VCSEC (车锁/唤醒/配对), Infotainment (驾驶数据)
- 数据: DriveState.speed_float (时速), ShiftState (P/R/N/D), odometer
