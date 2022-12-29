#pragma once

#include "device.h"
#include "linalg.h"

#ifdef __cplusplus
extern "C"{
#endif

VkResult lida_ForwardPassCreate(uint32_t width, uint32_t height);
void lida_ForwardPassDestroy();

#ifdef __cplusplus
}
#endif
