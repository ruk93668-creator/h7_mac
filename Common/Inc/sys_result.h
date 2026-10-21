#ifndef SYS_RESULT_H
#define SYS_RESULT_H

#include <stdint.h>

/* One operation result: zero is success; negative values are failures.
 * Results must not be combined with bitwise operations.
 */
typedef int32_t SysResult_t;

#define SYS_OK                    (0)  /* 操作成功 */
#define SYS_ERR_INVALID_ARG       (-1)  /* 参数无效：空指针、越界值或非法配置 */
#define SYS_ERR_NOT_FOUND         (-2)  /* 未找到目标对象，例如 Handle 没有匹配的有效对象 */
#define SYS_ERR_ALREADY_EXISTS    (-3)  /* 目标已存在，例如重复注册或重复订阅 */
#define SYS_ERR_INVALID_STATE     (-4)  /* 当前状态不允许执行该操作 */
#define SYS_ERR_BUSY              (-5)  /* 目标或资源暂时忙，当前无法执行，可在稍后重试 */
#define SYS_ERR_NO_RESOURCE       (-6)  /* 可用资源不足，例如对象池已满或队列无空位 */
#define SYS_ERR_NOT_SUPPORTED     (-7)  /* 当前模块或实现不支持该操作 */
#define SYS_ERR_TIMEOUT           (-8)  /* 等待操作或条件满足时超过规定时间 */
#define SYS_ERR_IO                (-9)  /* 输入输出操作失败，例如底层读写或传输失败 */
#define SYS_ERR_PROTOCOL          (-10)  /* 协议校验失败，例如格式、标识或版本不匹配 */

#endif /* SYS_RESULT_H */
