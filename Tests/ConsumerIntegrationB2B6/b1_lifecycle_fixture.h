//只由测试消费者持有原事务工具，不修改生产资格或任务私有字段
static AppCameraConsumer_t b1l_usb;
//导出只读布局，供运行器解码已有状态而不执行诊断副作用
const uint32_t g_b1l_layout[]={offsetof(AppCameraContext_t,qualifications),sizeof(AppCameraContext_t)};
//在一切Camera准备之前，以Camera身份初始化唯一资格核心
static SysResult_t B1L_Init(void)
{
 SysResult_t r;
 Owner(RUNTIME_TARGET_CAMERA);r=B6_Init();
 if(r!=SYS_OK)return r;
 Owner(RUNTIME_TARGET_CAMERA);
 return App_CameraConsumerInit(&b1l_usb,APP_LIFECYCLE_USB,APP_CONSUMER_USB);
}
//首个消费者通过真实QUERY/ACQUIRE取得资格，不能再发旧Camera START
static SysResult_t B1L_StartUsb(void)
{
 AppLifecycleMessage_t m;AppCameraConsumerStatus_t s;uint32_t i;
 SysResult_t r=App_CameraConsumerBegin(&b1l_usb);
 if(r!=SYS_OK)return r;
 for(i=0;i<8;i++)
 {
  (void)App_CameraConsumerFlush(&b1l_usb,B6_Send,0);
  Owner(RUNTIME_TARGET_CAMERA);App_CameraBeginRound();B6_Camera();
  Owner(RUNTIME_TARGET_CAMERA);
  (void)App_CameraQualificationFlush(APP_CONSUMER_USB,B6_Reply,0);
  while(App_LifecycleMessagesReceive(APP_LIFECYCLE_CHANNEL_USB,&m))
  {
   r=App_CameraConsumerReceive(&b1l_usb,&m);
   if(r!=SYS_OK)return r;
  }
  r=App_CameraConsumerProcess(&b1l_usb);
  Owner(RUNTIME_TARGET_CAMERA);Round(RUNTIME_EVENT_BOOT_READY);
  App_CameraConsumerGetStatus(&b1l_usb,&s);
  if(s.phase==APP_CONSUMER_GRANTED)return SYS_OK;
  if(s.phase==APP_CONSUMER_FAILED)return s.result;
 }
 return SYS_ERR_TIMEOUT;
}
