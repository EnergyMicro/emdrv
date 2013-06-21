/***************************************************************************//**
 * @file
 * @brief Non-Volatile Memory driver configuration template.
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
#ifndef __NVM_CONFIG_H__
#define __NVM_CONFIG_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/***************************************************************************//**
 * @addtogroup EM_Drivers
 * @{
 ******************************************************************************/

/***************************************************************************//**
 * @defgroup NVM
 * @{
 ******************************************************************************/

/*******************************************************************************
 ****************************   CONFIGURATION   ********************************
 ******************************************************************************/

/** Configure maximum amount of pages for use. */
#define NVM_PAGES            3

/** Configure extra pages to allocate for data security and wear leveling.
 * Minimum 1, but the more you add the better lifetime your system will have. */
#define NVM_PAGES_SCRATCH    3


/** Configure where in memory to start storing data. This area should be
 *  reserved using the linker and needs to be aligned with the physical page
 *  grouping of the device.
 *
 *  For the internal flash in the Gecko and Tiny Gecko MCUs, the flash pages are
 *  512 bytes long. This means that the start location must be a multiple of
 *  512 bytes, and that an area equal to 512 bytes * the number of pages and
 *  scratch page must be reserved here. */
#define NVM_START_LOCATION    (FLASH_SIZE - ((NVM_PAGES + NVM_PAGES_SCRATCH) * NVM_PAGE_SIZE))

/** Certain features can be turned on and off on compile time to make the API
* faster, save RAM and flash space. Set it to TRUE to turn the feature on. */

/* Without this define the wear pages are no longer supported. */
#define NVM_FEATURE_WEAR_PAGES_ENABLED               true

/* Include and activate the static wear leveling functionality. */
#define NVM_FEATURE_STATIC_WEAR_ENABLED              true

/* The threshold used to decide when to do static wear leveling.*/
#define NVM_STATIC_WEAR_THRESHOLD                    100

/* Validate data against checksums on every read operation. */
#define NVM_FEATURE_READ_VALIDATION_ENABLED          true

/* Validate data against checksums after every write operation. */
#define NVM_FEATURE_WRITE_VALIDATION_ENABLED         true

/* Include the NVM_WearLevelGet function. */
#define NVM_FEATURE_WEARLEVELGET_ENABLED             true

/* Check if data has been updated before writing update to the NVM. */
#define NVM_FEATURE_WRITE_NECESSARY_CHECK_ENABLED    true

/*******************************************************************************
 ******************************   TYPEDEFS   ***********************************
 ******************************************************************************/
/* Object IDs.
 * Enum used to store IDs in a readable format. */
typedef enum
{
  FIRST_TABL_ID,
  SINGL_VAR_ID,
  SECOND_TABL_ID,
  WEAR_TABL_ID
} NVM_Object_Ids;

/* Page IDs.
 * Enum used to store IDs in a readable format. */
typedef enum
{
  FIRST_PAGE_ID,
  SECOND_PAGE_ID,
  WEAR_PAGE_ID
} NVM_Page_Ids;

/*******************************************************************************
 **************************   GLOBAL VARIABLES   *******************************
 ******************************************************************************/
/* Make the variables accessible for the entire project. */
extern uint32_t nvmFirstTable[];
extern uint32_t nvmSingleVariable;

extern uint32_t nvmSecondTable[];

extern uint8_t  nvmWearTable[];

/** @} (end defgroup NVM) */
/** @} (end addtogroup EM_Drivers) */

#ifdef __cplusplus
}
#endif

#endif /* __NVM_CONFIG_H__ */
