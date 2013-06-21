/***************************************************************************//**
 * @file
 * @brief Non-Volatile Memory HAL.
 * @author Energy Micro AS
 * @version 3.20.0
 *******************************************************************************
 * @section License
 * <b>(C) Copyright 2013 Energy Micro AS, http://www.energymicro.com</b>
 *******************************************************************************
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 * 4. The source and compiled code may only be used on Energy Micro "EFM32"
 *    microcontrollers and "EFR4" radios.
 *
 * DISCLAIMER OF WARRANTY/LIMITATION OF REMEDIES: Energy Micro AS has no
 * obligation to support this Software. Energy Micro AS is providing the
 * Software "AS IS", with no express or implied warranties of any kind,
 * including, but not limited to, any implied warranties of merchantability
 * or fitness for any particular purpose or warranties against infringement
 * of any proprietary rights of a third party.
 *
 * Energy Micro AS will not be liable for any consequential, incidental, or
 * special damages, or any other relief, or for any claim by any third party,
 * arising from your use of this Software.
 *
 *****************************************************************************/

#ifndef __NVMHAL_H
#define __NVMHAL_H

#include <stdbool.h>

#include "nvm.h"

/* Defines for changing HAL functionality. These are both a bit experimental,
 * but should work properly. */

/* Custom write and format methods based on the emlib are used in place of
* the originals. These methods put the CPU to sleep by going to EM1 while the
* operation progresses.
*
* NVMHAL_SLEEP_FORMAT and NVMHAL_SLEEP_WRITE is only used for toggling
* which function is called, and includes about the same amount of code. */

/** Use energy saving version of format function */
#ifndef NVMHAL_SLEEP_FORMAT
#define NVMHAL_SLEEP_FORMAT    false
#endif

/** Use energy saving version of write function */
#ifndef NVMHAL_SLEEP_WRITE
#define NVMHAL_SLEEP_WRITE     false
#endif

/** DMA read uses the DMA to read data from flash. This also works, but takes a
 * bit more time than the usual reading operations, while not providing a high
 * amount of power saving since read operations are normally very fast. */
#ifndef NVMHAL_DMAREAD
#define NVMHAL_DMAREAD    false
#endif

/** @cond DO_NOT_INCLUDE_WITH_DOXYGEN */
#define NVMHAL_SLEEP           (NVMHAL_SLEEP_FORMAT | NVMHAL_SLEEP_WRITE)
/** @endcond */

#include "em_device.h"

#if (NVMHAL_SLEEP == true)
#include "em_msc.h"
#include "em_dma.h"
#include "em_cmu.h"
#include "em_emu.h"
#include "em_int.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 ******************************   CONSTANTS   **********************************
 ******************************************************************************/

/*******************************************************************************
 *****************************   PROTOTYPES   **********************************
 ******************************************************************************/

void NVMHAL_Init(void);
void NVMHAL_DeInit(void);
void NVMHAL_Read(uint8_t *pAddress, void *pObject, uint16_t len);
NVM_Result_t NVMHAL_Write(uint8_t *pAddress, void const *pObject, uint16_t len);
NVM_Result_t NVMHAL_PageErase(uint8_t *pAddress);
void NVMHAL_Checksum(uint16_t *checksum, void *pMemory, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* __NVMHAL_H */
