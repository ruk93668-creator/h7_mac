#ifndef V2_CAMERA_FORMAT_H
#define V2_CAMERA_FORMAT_H
#include "../../Core/media_format.h"
#include "camera_config.h"
/* Pure: actual OV5640 and CameraCapture validators retain veto authority.
 * No sensor init, DCMI config, Buffer acquire or default-limit copy. */
SysResult_t VCamera_Translate(const CoreMediaFormat *, CameraConfig_t *);
#endif
