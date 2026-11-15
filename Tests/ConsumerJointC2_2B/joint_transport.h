//四项静态生命周期运输桩，容量与生产通道一致；不代替Tx/Rx/Replay或消费者状态
static AppLifecycleMessage_t joint_messages[3][4];
static uint32_t joint_heads[3],joint_counts[3];
//只允许真实所属任务和消息来源匹配后提交，不把通知作为交付
SysResult_t App_LifecycleMessagesSend(AppLifecycleChannel_t channel,const AppLifecycleMessage_t*m)
{
 uint32_t c=(uint32_t)channel,role=integration_owner==RUNTIME_TARGET_CAMERA?APP_LIFECYCLE_CAMERA:integration_owner==RUNTIME_TARGET_USB?APP_LIFECYCLE_USB:integration_owner==RUNTIME_TARGET_STORAGE?APP_LIFECYCLE_STORAGE:0;
 if(!m||!App_LifecycleValidate(m))return SYS_ERR_INVALID_ARG;
 if(c>=3||m->source!=role||m->target!=c+1)return SYS_ERR_INVALID_STATE;
 if(joint_counts[c]==4)return SYS_ERR_NO_RESOURCE;
 joint_messages[c][(joint_heads[c]+joint_counts[c])%4]=*m;joint_counts[c]++;return SYS_OK;
}
//接收只从任务自身通道取完整消息；通知丢失不影响队列内容
bool App_LifecycleMessagesReceive(AppLifecycleChannel_t channel,AppLifecycleMessage_t*m)
{
 uint32_t c=(uint32_t)channel,expected=integration_owner==RUNTIME_TARGET_CAMERA?0:integration_owner==RUNTIME_TARGET_USB?1:integration_owner==RUNTIME_TARGET_STORAGE?2:99;
 if(c>=3||c!=expected||!m||!joint_counts[c])return false;
 *m=joint_messages[c][joint_heads[c]];joint_heads[c]=(joint_heads[c]+1)%4;joint_counts[c]--;return true;
}
//原消费者Flush回调仅投递Camera通道
static SysResult_t Joint_Send(void*p,const AppLifecycleMessage_t*m){(void)p;return App_LifecycleMessagesSend(APP_LIFECYCLE_CHANNEL_CAMERA,m);}
