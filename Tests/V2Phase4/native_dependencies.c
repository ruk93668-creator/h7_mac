/* Test-only trap dependencies. Actual validators are never stubbed.
 * Any hardware/physical FIFO execution fails emulation immediately. */
__asm void Native_UnboundTrap(void) { BKPT 0
 B Native_UnboundTrap
}
void BSP_Camera_Init(void){Native_UnboundTrap();}
void BSP_Camera_ReadReg(void){Native_UnboundTrap();}
void BSP_Camera_WriteReg(void){Native_UnboundTrap();}
void OV5640_ColorbarModeConfig(void){Native_UnboundTrap();}
void OV5640_Init(void){Native_UnboundTrap();}
void OV5640_RegisterBusIO(void){Native_UnboundTrap();}
void OV5640_SetPCLK(void){Native_UnboundTrap();}
void OV5640_Start(void){Native_UnboundTrap();}
void OV5640_Stop(void){Native_UnboundTrap();}
void Platform_TimeGetMs(void){Native_UnboundTrap();}
void BSP_DCMI_ProcessJpeg(void){Native_UnboundTrap();}
void BSP_DCMI_ReplaceTarget(void){Native_UnboundTrap();}
void BSP_DCMI_SetEventCallback(void){Native_UnboundTrap();}
void BSP_DCMI_StartContinuous(void){Native_UnboundTrap();}
void BSP_DCMI_StartJpegSnapshot(void){Native_UnboundTrap();}
void BSP_DCMI_StartSnapshot(void){Native_UnboundTrap();}
void BSP_DCMI_Stop(void){Native_UnboundTrap();}
void BSP_DCMI_TakeEvents(void){Native_UnboundTrap();}
void CameraMjpegFifo_GetFrame(void){Native_UnboundTrap();}
void CameraMjpegFifo_GetStats(void){Native_UnboundTrap();}
void CameraMjpegFifo_HasLoan(void){Native_UnboundTrap();}
void CameraMjpegFifo_OnStopped(void){Native_UnboundTrap();}
void CameraMjpegFifo_Process(void){Native_UnboundTrap();}
void CameraMjpegFifo_ReleaseFrame(void){Native_UnboundTrap();}
void CameraMjpegFifo_Start(void){Native_UnboundTrap();}
