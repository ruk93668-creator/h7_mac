# Production Diff

```diff
diff --git a/CM7/Application/app_usb.c b/CM7/Application/app_usb.c
index d18a048..b5102b2 100644
--- a/CM7/Application/app_usb.c
+++ b/CM7/Application/app_usb.c
@@ -152,8 +152,9 @@ void App_USBProcess(void)
         else
         {
             s_usb.fault = result;
+            // Preserve START failure while stopping Mode before releasing its grant.
             if (s_usb.camera.granted != 0U)
-                (void)App_CameraConsumerEnd(&s_usb.camera, result, SYS_OK);
+                s_usb.stopping = true;
         }
         s_usb.ack.result = result;
         s_usb.ack_pending = true;
@@ -200,9 +201,7 @@ void App_USBFinishRound(bool uvc_this_round)
         //BUSY继续等待，失败ACK发出后仍保留停止要求供后台重试
         if (s_usb.stop_command && result != SYS_ERR_BUSY)
         {
-            //业务STOP ACK仅表达USB停止；资格释放仍保留独立WAIT_REPLY
-            if (result == SYS_OK && s_usb.lifecycle_initialized && s_usb.camera.granted != 0U)
-                (void)App_CameraConsumerEnd(&s_usb.camera, s_usb.fault, SYS_OK);
+            // Deliver the original command result once; background recovery cannot rewrite it.
             s_usb.ack.result = result;
             s_usb.ack_pending = true;
             s_usb.stop_command = false;
@@ -212,6 +211,14 @@ void App_USBFinishRound(bool uvc_this_round)
         if (result != SYS_OK && result != SYS_ERR_BUSY && s_usb.fault == SYS_OK)
             s_usb.fault = result;
     }
+    // Release follows completed USB work, independently of the STOP command ACK.
+    // A staged RELEASE changes phase; an unstaged attempt remains GRANTED for retry.
+    // Granted START failures also enter stopping, so quiescence here follows Mode Stop success.
+    if (!s_usb.started && !s_usb.stopping && !s_usb.start_command &&
+        s_usb.lifecycle_initialized && s_usb.camera.granted != 0U &&
+        s_usb.camera.phase == APP_CONSUMER_GRANTED && App_UvcBridgeUsbIsIdle())
+        (void)App_CameraConsumerEnd(&s_usb.camera, s_usb.fault, SYS_OK);
+
     App_USBObserve(APP_USB_DIAG_STATE);
     //累计EOF跨Class保留，保持原采样和取时顺序
     total = USB_UVC_GetCompletedFrameCount();

```
