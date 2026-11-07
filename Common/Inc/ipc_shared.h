#ifndef IPC_SHARED_H
#define IPC_SHARED_H

#include "ipc_status.h"
#include "ipc_config.h"
#include <stddef.h>
#include <stdint.h>

#define IPC_MAGIC              0x49504331UL
#define IPC_VERSION            3UL
#define IPC_HEADER_BYTES       64UL
#define IPC_STATUS_BYTES       128UL
#define IPC_LAYOUT_BYTES       320UL
#define IPC_FEATURE_SEQLOCK    1UL
#define IPC_FEATURE_APPLICATION 2UL
#define IPC_FEATURE_FLAGS (IPC_FEATURE_SEQLOCK | IPC_FEATURE_APPLICATION)
#define IPC_READ_ATTEMPTS      3U

//CM7独占协议头，冷启动完成后保持不变
typedef struct __attribute__((aligned(32)))
{
    uint32_t magic;
    uint32_t version;
    uint32_t layout_bytes;
    uint32_t header_bytes;
    uint32_t status_block_bytes;
    uint32_t feature_flags;
    uint32_t reserved[10];
} IpcHeader_t;

//每个块仅由所属核心写入，所有线字段明确为32位
typedef struct __attribute__((aligned(32)))
{
    uint32_t sequence;
    uint32_t owner_id;
    uint32_t block_bytes;
    uint32_t ready;
    uint32_t heartbeat;
    uint32_t fault_flags;
    int32_t last_result;

    //V3正式业务载荷：CM7发布，CM4在本核块中保持全零
    IpcApplicationStatus_t application;

    //剩余保留字段仍必须清零

    uint32_t reserved[5];
} IpcStatusBlock_t;

//320B协议对象放在原4KiB UNINIT窗口，未启用启动代号或业务快照
typedef struct __attribute__((aligned(32)))
{
    IpcHeader_t header;
    IpcStatusBlock_t m7;
    IpcStatusBlock_t m4;
} IpcShared_t;

//探测类型实际对齐，兼容ARMCC5而不依赖C11断言
typedef struct
{
    uint8_t prefix;
    IpcShared_t value;
} IpcAlignmentProbe_t;

//分别检查头和状态块的类型对齐，避免只验证最终对象地址
typedef struct { uint8_t prefix; IpcHeader_t value; } IpcHeaderAlignmentProbe_t;
typedef struct { uint8_t prefix; IpcStatusBlock_t value; } IpcBlockAlignmentProbe_t;

#define IPC_LAYOUT_ASSERT(name, expression) typedef char name[(expression) ? 1 : -1]
IPC_LAYOUT_ASSERT(Ipc_U32Size, sizeof(uint32_t) == 4U);
IPC_LAYOUT_ASSERT(Ipc_I32Size, sizeof(int32_t) == 4U);
IPC_LAYOUT_ASSERT(Ipc_HeaderSize, sizeof(IpcHeader_t) == IPC_HEADER_BYTES);
IPC_LAYOUT_ASSERT(Ipc_BlockSize, sizeof(IpcStatusBlock_t) == IPC_STATUS_BYTES);
IPC_LAYOUT_ASSERT(Ipc_SnapshotSize, sizeof(IpcStatusSnapshot_t) == IPC_STATUS_BYTES);
IPC_LAYOUT_ASSERT(Ipc_TotalSize, sizeof(IpcShared_t) == IPC_LAYOUT_BYTES);
IPC_LAYOUT_ASSERT(Ipc_HeaderOffset, offsetof(IpcShared_t, header) == 0U);
IPC_LAYOUT_ASSERT(Ipc_M7Offset, offsetof(IpcShared_t, m7) == 0x40U);
IPC_LAYOUT_ASSERT(Ipc_M4Offset, offsetof(IpcShared_t, m4) == 0xC0U);
IPC_LAYOUT_ASSERT(Ipc_HeaderAlignment, offsetof(IpcHeaderAlignmentProbe_t, value) == 32U);
IPC_LAYOUT_ASSERT(Ipc_BlockAlignment, offsetof(IpcBlockAlignmentProbe_t, value) == 32U);
IPC_LAYOUT_ASSERT(Ipc_TypeAlignment, offsetof(IpcAlignmentProbe_t, value) == 32U);
IPC_LAYOUT_ASSERT(Ipc_BaseAlignment, (IPC_SHARED_BASE % 32U) == 0U);
IPC_LAYOUT_ASSERT(Ipc_WindowSize, sizeof(IpcShared_t) <= IPC_SHARED_WINDOW_BYTES);
IPC_LAYOUT_ASSERT(Ipc_SequenceOffset, offsetof(IpcStatusBlock_t, sequence) == 0U);
IPC_LAYOUT_ASSERT(Ipc_OwnerOffset, offsetof(IpcStatusBlock_t, owner_id) == 4U);
IPC_LAYOUT_ASSERT(Ipc_BytesOffset, offsetof(IpcStatusBlock_t, block_bytes) == 8U);
IPC_LAYOUT_ASSERT(Ipc_ReadyOffset, offsetof(IpcStatusBlock_t, ready) == 12U);
IPC_LAYOUT_ASSERT(Ipc_HeartbeatOffset, offsetof(IpcStatusBlock_t, heartbeat) == 16U);
IPC_LAYOUT_ASSERT(Ipc_FaultOffset, offsetof(IpcStatusBlock_t, fault_flags) == 20U);
IPC_LAYOUT_ASSERT(Ipc_ResultOffset, offsetof(IpcStatusBlock_t, last_result) == 24U);
IPC_LAYOUT_ASSERT(Ipc_ReservedOffset, offsetof(IpcStatusBlock_t, reserved) == 108U);
IPC_LAYOUT_ASSERT(Ipc_HeaderVersionOffset, offsetof(IpcHeader_t, version) == 4U);
IPC_LAYOUT_ASSERT(Ipc_HeaderLayoutOffset, offsetof(IpcHeader_t, layout_bytes) == 8U);
IPC_LAYOUT_ASSERT(Ipc_HeaderBytesOffset, offsetof(IpcHeader_t, header_bytes) == 12U);
IPC_LAYOUT_ASSERT(Ipc_HeaderBlockOffset, offsetof(IpcHeader_t, status_block_bytes) == 16U);
IPC_LAYOUT_ASSERT(Ipc_HeaderFeaturesOffset, offsetof(IpcHeader_t, feature_flags) == 20U);
IPC_LAYOUT_ASSERT(Ipc_HeaderReservedOffset, offsetof(IpcHeader_t, reserved) == 24U);
IPC_LAYOUT_ASSERT(Ipc_Snapshot_sequence, offsetof(IpcStatusSnapshot_t, sequence) == 0U);
IPC_LAYOUT_ASSERT(Ipc_Snapshot_owner_id, offsetof(IpcStatusSnapshot_t, owner_id) == 4U);
IPC_LAYOUT_ASSERT(Ipc_Snapshot_block_bytes, offsetof(IpcStatusSnapshot_t, block_bytes) == 8U);
IPC_LAYOUT_ASSERT(Ipc_Snapshot_ready, offsetof(IpcStatusSnapshot_t, ready) == 12U);
IPC_LAYOUT_ASSERT(Ipc_Snapshot_heartbeat, offsetof(IpcStatusSnapshot_t, heartbeat) == 16U);
IPC_LAYOUT_ASSERT(Ipc_Snapshot_fault_flags, offsetof(IpcStatusSnapshot_t, fault_flags) == 20U);
IPC_LAYOUT_ASSERT(Ipc_Snapshot_last_result, offsetof(IpcStatusSnapshot_t, last_result) == 24U);
IPC_LAYOUT_ASSERT(Ipc_Snapshot_reserved, offsetof(IpcStatusSnapshot_t, reserved) == 108U);
IPC_LAYOUT_ASSERT(Ipc_ApplicationOffset, offsetof(IpcStatusBlock_t, application) == 28U);
IPC_LAYOUT_ASSERT(Ipc_SnapshotApplicationOffset, offsetof(IpcStatusSnapshot_t, application) == 28U);
#undef IPC_LAYOUT_ASSERT

//两核ELF各保留一个同地址符号，不由启动库清零
extern volatile IpcShared_t g_ipc;

#endif
