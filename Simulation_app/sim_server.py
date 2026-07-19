import sys
import time
import math
import struct
import argparse
import asyncio
from datetime import datetime

# 尝试载入第三方库
try:
    import serial
except ImportError:
    serial = None

try:
    from bless import (
        BlessServer,
        BlessGATTCharacteristic,
        GATTCharacteristicProperties,
        GATTAttributePermissions
    )
except ImportError:
    BlessServer = None

# ─── 特斯拉自定义 BLE UUID ──────────────────────────────────────
# Service UUID: 00000211-b2d1-43f0-9b88-960cebf8b91e
# Write UUID:   00000212-b2d1-43f0-9b88-960cebf8b91e
# Read (Notify) UUID: 00000213-b2d1-43f0-9b88-960cebf8b91e
SVC_UUID = "00000211-b2d1-43f0-9b88-960cebf8b91e"
WR_UUID  = "00000212-b2d1-43f0-9b88-960cebf8b91e"
RD_UUID  = "00000213-b2d1-43f0-9b88-960cebf8b91e"

# ─── 数据载荷打包格式 ──────────────────────────────────────────
# 对应 C++ 的 packed SimPayload 结构体
# f: speed_kmh
# f: motor_power_kw
# i: battery_level
# f: battery_range_km
# c: gear
# 6B: doors (fl, fr, rl, rr, frunk, trunk)
# B: locked
# B: charging
# f: charge_power_kw
# f: inside_temp
# f: outside_temp
# 4f: tpms (fl, fr, rl, rr)
PAYLOAD_FORMAT = "<ffifc6BBBBfff4f"

def pack_telemetry(data):
    """
    将字典数据序列化为紧凑的二进制 SimPayload
    """
    gear_byte = data['gear'].encode('ascii')
    doors = [
        int(data['door_open_fl']),
        int(data['door_open_fr']),
        int(data['door_open_rl']),
        int(data['door_open_rr']),
        int(data['door_open_trunk_front']),
        int(data['door_open_trunk_rear'])
    ]
    return struct.pack(
        PAYLOAD_FORMAT,
        data['speed_kmh'],
        data['motor_power_kw'],
        data['battery_level'],
        data['battery_range_km'],
        gear_byte,
        *doors,
        int(data['locked']),
        int(data['charging']),
        data['charge_power_kw'],
        data['inside_temp'],
        data['outside_temp'],
        data['tpms_fl'],
        data['tpms_fr'],
        data['tpms_rl'],
        data['tpms_rr']
    )

def generate_mock_frame(frame_counter):
    """
    根据时间轴帧数（0 ~ 1030 帧）生成仿真的车机遥测状态数据。
    20Hz 刷新下，全长剧本运行时间约为 51.5 秒。
    """
    loop_frame = frame_counter % 1030
    
    # 默认基础状态
    data = {
        'speed_kmh': 0.0,
        'motor_power_kw': 0.0,
        'battery_level': 62,
        'battery_range_km': 248.0,
        'gear': 'P',
        'door_open_fl': False,
        'door_open_fr': False,
        'door_open_rl': False,
        'door_open_rr': False,
        'door_open_trunk_front': False,
        'door_open_trunk_rear': False,
        'locked': True,
        'charging': False,
        'charge_power_kw': 0.0,
        'inside_temp': 35.0,
        'outside_temp': 16.0,
        'tpms_fl': 2.3,
        'tpms_fr': 2.3,
        'tpms_rl': 2.3,
        'tpms_rr': 2.3
    }

    # 阶段 0：解锁上车（0 ~ 100帧）
    if loop_frame < 100:
        data['locked'] = False
        data['door_open_fl'] = True
        data['door_open_rl'] = True
        data['tpms_fl'] = 2.4  # 低压报警
        data['tpms_fr'] = 2.8
        data['tpms_rl'] = 3.3  # 高压报警
        data['tpms_rr'] = 2.9

    # 阶段 1：准备起步（100 ~ 160帧）
    elif 100 <= loop_frame < 160:
        data['locked'] = False
        data['motor_power_kw'] = 1.5
        data['tpms_fl'] = 2.4
        data['tpms_fr'] = 2.8
        data['tpms_rl'] = 3.3
        data['tpms_rr'] = 2.9

    # 阶段 2：挂D档起步（160 ~ 240帧）
    elif 240 > loop_frame >= 160:
        data['locked'] = False
        data['gear'] = 'D'
        if loop_frame < 200:
            data['speed_kmh'] = 0.0
            data['motor_power_kw'] = 0.0
        else:
            ratio = (loop_frame - 200) / 40.0
            data['speed_kmh'] = ratio * 30.0
            data['motor_power_kw'] = 12.0
        data['tpms_fl'] = 2.4
        data['tpms_fr'] = 2.8
        data['tpms_rl'] = 3.3
        data['tpms_rr'] = 2.9

    # 阶段 3：强力超车（240 ~ 360帧）
    elif 360 > loop_frame >= 240:
        data['locked'] = False
        ratio = (loop_frame - 240) / 120.0
        data['gear'] = 'D'
        data['speed_kmh'] = 30.0 + ratio * 90.0
        data['motor_power_kw'] = 25.0 + ratio * 60.0
        data['tpms_fl'] = 2.4
        data['tpms_fr'] = 2.8 + ratio * 0.3
        data['tpms_rl'] = 3.3
        data['tpms_rr'] = 2.9 + ratio * 0.4
        data['inside_temp'] = 35.0 - ratio * 13.0

    # 阶段 4：能量回收重踩刹车（360 ~ 460帧）
    elif 460 > loop_frame >= 360:
        data['locked'] = False
        data['gear'] = 'D'
        if loop_frame < 410:
            ratio = (loop_frame - 360) / 50.0
            data['speed_kmh'] = 120.0 - ratio * 30.0
            data['motor_power_kw'] = -12.5  # 轻度回收
        else:
            ratio = (loop_frame - 410) / 50.0
            data['speed_kmh'] = 90.0 - ratio * 80.0
            data['motor_power_kw'] = -12.5 - ratio * 47.5  # 强力回收
        data['tpms_fl'] = 2.4
        data['tpms_fr'] = 3.1
        data['tpms_rl'] = 3.3
        data['tpms_rr'] = 3.3

    # 阶段 5：巡航模式 FSD (定速 80 ~ 85)（460 ~ 580帧）
    elif 580 > loop_frame >= 460:
        data['locked'] = False
        data['gear'] = '?'  # 巡航档
        if loop_frame < 500:
            data['speed_kmh'] = 80.0
            data['motor_power_kw'] = 8.5
        elif loop_frame < 540:
            r = (loop_frame - 500) / 40.0
            data['speed_kmh'] = 80.0 + r * 5.0
            data['motor_power_kw'] = 12.0
        else:
            r = (loop_frame - 540) / 40.0
            data['speed_kmh'] = 85.0 - r * 5.0
            data['motor_power_kw'] = 4.0
        data['tpms_fl'] = 2.4
        data['tpms_fr'] = 3.1
        data['tpms_rl'] = 3.3
        data['tpms_rr'] = 3.3

    # 阶段 6：倒车挂R档（580 ~ 700帧）
    elif 700 > loop_frame >= 580:
        data['locked'] = False
        data['gear'] = 'R'
        if loop_frame < 620:
            data['speed_kmh'] = 0.0
            data['motor_power_kw'] = 0.0
        elif loop_frame < 660:
            r = (loop_frame - 620) / 40.0
            data['speed_kmh'] = r * 6.0
            data['motor_power_kw'] = r * 5.0
        else:
            r = (loop_frame - 660) / 40.0
            data['speed_kmh'] = 6.0 - r * 6.0
            data['motor_power_kw'] = 4.0 - r * 4.0
        ratio = (loop_frame - 580) / 120.0
        data['tpms_fl'] = 2.4
        data['tpms_fr'] = 3.1 - ratio * 0.3
        data['tpms_rl'] = 3.3
        data['tpms_rr'] = 3.3 - ratio * 0.4

    # 阶段 7：开门（700 ~ 780帧）
    elif 780 > loop_frame >= 700:
        data['locked'] = False
        data['door_open_fl'] = True
        data['tpms_fl'] = 2.4
        data['tpms_fr'] = 2.8
        data['tpms_rl'] = 3.3
        data['tpms_rr'] = 2.9

    # 阶段 8：插枪充电中（780 ~ 1030帧）
    else:
        data['locked'] = True
        data['charging'] = True
        data['charge_power_kw'] = 7.2
        ratio = (loop_frame - 780) / 250.0
        data['battery_level'] = min(100, 99 + int(ratio * 2.0))
        data['battery_range_km'] = 248.0 + ratio * 12.0
        data['tpms_fl'] = 2.4
        data['tpms_fr'] = 2.8
        data['tpms_rl'] = 3.3
        data['tpms_rr'] = 2.9

    return data

async def run_ble_simulation():
    """
    低功耗蓝牙 BLE 广播仿真模式
    """
    if not BlessServer:
        print("[-] 错误: 未安装 bless 蓝牙外设模拟库，请运行 pip install -r requirements.txt 安装。")
        return

    print("[+] 正在配置 BLE 外设服务器...")
    server = BlessServer(name="Tesla-BLE-Simulation")
    
    # 写入处理 (响应客户端控制)
    def on_write(characteristic, value):
        print(f"[BLE-Write] 接收到控制命令: {value.hex()}")

    # 添加自定义 Tesla 属性
    server.add_new_service(SVC_UUID)
    
    # 模拟特斯拉 Write 和 Notify 属性特征值
    server.add_new_characteristic(
        SVC_UUID,
        WR_UUID,
        GATTCharacteristicProperties.write | GATTCharacteristicProperties.write_without_response,
        GATTAttributePermissions.writeable,
        value=bytearray()
    )
    
    server.add_new_characteristic(
        SVC_UUID,
        RD_UUID,
        GATTCharacteristicProperties.notify,
        GATTAttributePermissions.readable,
        value=bytearray()
    )
    
    server.write_callbacks[WR_UUID] = on_write

    print(f"[+] 启动广播: Service UUID = {SVC_UUID}")
    await server.start()
    print("[+] 广播发布成功！等待 ESP32 蓝牙寻址连接...")

    frame = 0
    try:
        while True:
            # 持续监听并以 20Hz 推送 Notify 数据包
            if server.is_connected:
                mock_data = generate_mock_frame(frame)
                payload = pack_telemetry(mock_data)
                
                # 更新 Notify 数据源通知客户端
                # 在 bless 库中，通过对 read 属性写值来触发 Notify
                server.get_characteristic(RD_UUID).value = payload
                server.update_value(SVC_UUID, RD_UUID)
                
                if frame % 40 == 0:
                    print(f"[BLE-Notify] 正在推送遥测: Speed={mock_data['speed_kmh']:.1f} KM/H, Gear={mock_data['gear']}, SOC={mock_data['battery_level']}%")
                frame += 1
            else:
                if frame > 0:
                    print("[-] ESP32 断开蓝牙连接，等待重新连接...")
                    frame = 0
            
            await asyncio.sleep(0.05) # 50ms 相当于 20Hz
    except KeyboardInterrupt:
        print("[+] 正在停止 BLE 服务器...")
        await server.stop()

def run_uart_simulation(port, baudrate=115200):
    """
    串口数据注入仿真模式
    """
    if not serial:
        print("[-] 错误: 未安装 pyserial 库，请运行 pip install -r requirements.txt 安装。")
        return

    print(f"[+] 正在打开串口: {port} (波特率: {baudrate})...")
    try:
        ser = serial.Serial(port, baudrate, timeout=1)
    except Exception as e:
        print(f"[-] 错误: 无法打开串口 {port}: {e}")
        return

    print("[+] 串口打开成功！正在通过串口线以 20Hz 实时注入车机遥测包...")
    print("[+] 提示: 请将 ESP32 上的 USB 调试串口插入电脑并确保端口号正确。")

    frame = 0
    # 数据包头包尾
    FRAME_HEAD = b'\xAA\xBB'
    FRAME_TAIL = b'\xCC\xDD'

    try:
        while True:
            mock_data = generate_mock_frame(frame)
            payload = pack_telemetry(mock_data)
            
            # 打包成防干扰帧: [0xAA 0xBB] + [2字节长度] + [数据体] + [0xCC 0xDD]
            length_bytes = struct.pack("<H", len(payload))
            frame_bytes = FRAME_HEAD + length_bytes + payload + FRAME_TAIL
            
            ser.write(frame_bytes)
            ser.flush()
            
            if frame % 40 == 0:
                print(f"[UART-Inject] 注入遥测数据包: Speed={mock_data['speed_kmh']:.1f} KM/H, Gear={mock_data['gear']}, SOC={mock_data['battery_level']}%")
            
            frame += 1
            time.sleep(0.05) # 20Hz
    except KeyboardInterrupt:
        print("[+] 正在关闭串口...")
        ser.close()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Tesla Dash HMI Simulation Server")
    parser.add_argument("--mode", type=str, choices=["ble", "uart"], default="uart",
                        help="仿真模式: ble (低功耗蓝牙无线外设) 或 uart (串口极速注入)")
    parser.add_argument("--port", type=str, default="COM10",
                        help="串口仿真所连接的 ESP32 串口端口号 (仅在 --mode uart 时生效)")
    args = parser.parse_args()

    print("==========================================================")
    print("      Tesla BLE HMI Simulation Server (Windows Client)    ")
    print("==========================================================")
    
    if args.mode == "uart":
        run_uart_simulation(args.port)
    else:
        # BLE 模式属于异步任务
        asyncio.run(run_ble_simulation())
