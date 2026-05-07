#ifndef MODBUS_CFG_H
#define MODBUS_CFG_H


#define MAX_REGS 16


typedef struct {
    uint16_t addr; 
    char     key[32];    // 数据键名，如 "temperature"
    char     unit[16];   // 单位，如 "℃"
    double scale; //缩放系数，原始值 * scale = 实际值
} reg_def_t;


typedef struct{
    char device[32]; //串口设备名
    int  baud;
    int      poll_interval_ms,response_timeout_s,response_timeout_us; 
    uint8_t  slave_id;
    char parity; //奇偶检验位
    uint8_t data_bit;
    uint8_t stop_bit;
    char     topic_prefix[64];    // topic 前缀，如 "device/modbus"
    reg_def_t regs[MAX_REGS];    // 寄存器列表
    int       reg_count;          // 寄存器数量
} modbus_cfg_t;

#endif
