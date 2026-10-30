; 合入目标板自己的Flash加载区，地址由前一个执行区决定
ER_FW_INIT +0 ALIGN 4
{
    *(.framework_init)
}
