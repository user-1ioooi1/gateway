# 插件化协议网关

基于 iMX6ULL 的插件化协议网关，支持多协议采集、消息总线、告警规则、MQTT 上云和热更新。

---

## 目录结构

```
gateway/
├── core/                   # 主程序
│   ├── main.c
│   ├── plugin_manager.c/h  # 插件生命周期管理
│   ├── message_bus.c/h     # 消息总线（发布订阅）
│   ├── signal_handler.c/h  # 信号处理
│   └── bus_stress_test.c   # 压测
├── utils/                  # 公共工具库
│   ├── logger.c/h          # 日志系统
│   ├── config.c/h          # 配置加载器
│   ├── message.h           # 统一消息结构体
│   ├── plugin.h            # 插件接口定义
│   └── file_utils.c/h      # 文件工具（mtime/MD5）
├── plugins/                # 插件目录
│   ├── modbus/             # Modbus RTU 插件
│   ├── can/                # SocketCAN 插件
│   ├── mqtt/               # MQTT 上云插件
│   ├── processor/          # 过滤告警插件
│   └── fake/               # 测试用假数据插件
├── third_party/            # 第三方库
│   └── cJSON/
├── config/
│   └── gateway.json        # 网关配置文件
└── cmake/
    └── arm-linux-gnueabihf.cmake  # 交叉编译工具链
```

---

## 依赖
| 库 | 版本 | 用途 |
|----|------|------|
| libmodbus | >= 3.1.6 | Modbus RTU 通信 |
| libmosquitto | >= 2.0 | MQTT 客户端 |
| cJSON | 1.7.x | JSON 解析 |

### Ubuntu 安装依赖

```bash
sudo apt install libmodbus-dev libmosquitto-dev mosquitto can-utils
```

---

## 编译

### x86 本机编译（开发调试）

```bash
mkdir build && cd build
cmake ..
make -j4
```

### 交叉编译到 iMX6ULL

**1. 先交叉编译依赖库**

```bash
# 编译 libmodbus
./configure \
    --host=arm-buildroot-linux-gnueabihf \
    --prefix=${SYSROOT}/usr \
    CC=arm-buildroot-linux-gnueabihf-gcc \
    CFLAGS="-march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard"
make -j4 && make install

# 编译 libmosquitto
cmake .. \
    -DCMAKE_TOOLCHAIN_FILE=/path/to/gateway/cmake/arm-linux-gnueabihf.cmake \
    -DCMAKE_INSTALL_PREFIX=${SYSROOT}/usr \
    -DWITH_BROKER=OFF -DWITH_APPS=OFF -DWITH_TLS=ON
make -j4 && make install
```

**2. 编译 gateway**

```bash
mkdir build-arm && cd build-arm
cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/arm-linux-gnueabihf.cmake \
         -DCMAKE_BUILD_TYPE=Release
make -j4
```

---

## 配置

编辑 `config/gateway.json`

### 配置说明

| 字段 | 说明 |
|------|------|
| `type` | 插件类型：`south`（采集）/ `processor`（处理）/ `north`（上报） |
| `so_path` | 插件 .so 文件路径 |
| `subscribe_topics` | 插件订阅的消息总线 topic，支持通配符 `+` 和 `#` |

---

## 运行

```bash
# x86 本机
cd build
./bin/gateway

# iMX6ULL 开发板
export LD_LIBRARY_PATH=/path/to/lib:$LD_LIBRARY_PATH
./gateway
```

## 热更新

修改 `gateway.json` 后发送信号触发热更新，无需重启进程：

```bash
kill -SIGUSR1 $(pgrep gateway)
```

热更新支持五种情况：

| 情况 | 触发条件 | 处理方式 |
|------|---------|---------|
| A | 插件无变化 | 跳过 |
| B | 只改配置 | reinit（stop→init→start） |
| C | .so 文件变化 | reload（dlclose→dlopen→init→start） |
| D | 新增插件 | load |
| E | 删除插件 | unload（取消订阅→stop→destroy→dlclose） |

---

## 部署到 iMX6ULL

需要复制到开发板的文件：

```
gateway              # 可执行文件
plugins/             # 所有插件 .so
lib/
  libutils.so        # 工具库
  libcjson.so        # JSON 库
  libmodbus.so.5     # Modbus 库
  libmosquitto.so.1  # MQTT 库
  libssl.so.1.1      # OpenSSL（mosquitto 依赖）
  libcrypto.so.1.1
config/
  gateway.json       # 配置文件
```

---


## 第三方库许可证

| 库 | 许可证 | 链接 |
|----|--------|------|
| cJSON | MIT | https://github.com/DaveGamble/cJSON |
| libmodbus | LGPL v2.1 | https://libmodbus.org |
| Eclipse Mosquitto | EPL 2.0 | https://mosquitto.org |
