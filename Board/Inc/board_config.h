#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

/* PCB hardware connection definitions belong here. */

/* IPC Config*/
#define IPC_SHARED_BASE        0x38000000UL
#define IPC_SHARED_SIZE        0x00001000UL

#define HSEM_M7_TO_M4_ID         1U
#define HSEM_M4_TO_M7_ID         2U

/* DEVICE Config */

//OLED
#define BOARD_HAS_OLED         1
#define BOARD_OLED_AUTO_INIT   1

#endif /* BOARD_CONFIG_H */
