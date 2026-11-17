//Ethernet 板级适配接口，不暴露 HAL 类型
#ifndef ETH_BOARD_H
#define ETH_BOARD_H

//任务上下文准备 RMII 时钟、GPIO 和 PHY 复位，HAL 时基必须正常运行
//不启动 MAC/DMA，不更改系统时钟树或现有中断优先级
void EthBoard_Prepare(void);

#endif
