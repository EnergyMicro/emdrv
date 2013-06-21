/***************************************************************************//**
 * @file
 * @brief Non-Volatile Memory Manager.
 * @author Energy Micro AS
 * @version 3.20.0
 * @details
 * This is a software manager module for non-volatile memory. It consists of
 * nvm.c, nvm.h and nvm_hal.h. In addition to this it requires a valid nvm_hal.c.
 * This lets the manager work with different types of memory. 
 * Files nvm_config_template.* shows possible configuration of objects and could
 * be used as template for customer application.
 *
 * The module has the following public interfaces:
 *
 * NVM_Init()
 * NVM_Erase()
 * NVM_Write()
 * NVM_Read()
 * NVM_WearLevel()
 *
 * Users have to be aware of the following limitations of the module:
 * - Maximum 254 objects in a page and 256 pages (limited by uint8_t).
 *
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
#ifndef __NVM_H
#define __NVM_H

#include <stdint.h>
#include <stdbool.h>

/* needed to get CPU flash size and calculate page size */
#include "em_device.h"

#ifdef __cplusplus
extern "C" {
#endif

/***************************************************************************//**
 * @addtogroup EM_Drivers
 * @{
 ******************************************************************************/

/***************************************************************************//**
 * @addtogroup NVM
 * @{
 ******************************************************************************/

/*******************************************************************************
 *******************************   DEFINES   ***********************************
 ******************************************************************************/

/** @cond DO_NOT_INCLUDE_WITH_DOXYGEN */

/** Certain features can be turned on and off on compile time to make the API
* faster, save RAM and flash space. Set it to TRUE to turn the feature on. */

/** Without this define the wear pages are no longer supported. */
#define NVM_FEATURE_WEAR_PAGES_ENABLED               true

/** Include and activate the static wear leveling functionality. */
#define NVM_FEATURE_STATIC_WEAR_ENABLED              true

/** The threshold used to decide when to do static wear leveling.*/
#define NVM_STATIC_WEAR_THRESHOLD                    100

/** Validate data against checksums on every read operation. */
#define NVM_FEATURE_READ_VALIDATION_ENABLED          true

/** Validate data against checksums after every write operation. */
#define NVM_FEATURE_WRITE_VALIDATION_ENABLED         true

/** Include the NVM_WearLevelGet function. */
#define NVM_FEATURE_WEARLEVELGET_ENABLED             true

/** Check if data has been updated before writing update to the NVM. */
#define NVM_FEATURE_WRITE_NECESSARY_CHECK_ENABLED    true

/** define maximum number of flash pages that can be used as NVM */
#define NVM_MAX_NUMBER_OF_PAGES                      32

/** Calculate page size. */
#if defined(_EFM32_GIANT_FAMILY) || defined(_EFM32_WONDER_FAMILY)
#define NVM_PAGE_SIZE           (FLASH_SIZE < (512 * 1024) ? 2048 : 4096)
#else
/* _EFM32_GECKO_FAMILY || _EFM32_TINY_FAMILY */
#define NVM_PAGE_SIZE           (512)
#endif
  
/** All objects are written from RAM. */
#define NVM_WRITE_ALL_CMD         0xff
/** All objects are copied from the old page. */
#define NVM_WRITE_NONE_CMD        0xfe
/** All objects are read to RAM. */
#define NVM_READ_ALL_CMD          0xff

/** Retains the registered erase count when eraseing a page. */
#define NVM_ERASE_RETAINCOUNT    0xffffffffUL

/** Structure defining end of pages table. */
#define NVM_PAGE_TERMINATION    { NULL, 0, (NVM_Object_Ids) 0 }

/** @endcond */

/*******************************************************************************
 ******************************   TYPEDEFS   ***********************************
 ******************************************************************************/

/** Enum describing the type of logical page we have; normal or wear. */
typedef enum
{
  nvmPageTypeNormal = 0, /**< Normal page, always rewrite. */
  nvmPageTypeWear   = 1  /**< Wear page. Can be used several times before rewrite. */
} NVM_Page_Type_t;

/** Describes the properties of an object in a page. */
typedef struct
{
  uint8_t  * location; /**< A pointer to the location of the object in RAM. */
  uint16_t size;       /**< The size of the object in bytes. */
  uint8_t  objectId;   /**< An object ID used to reference the object. Must be unique in the page. */
} NVM_Object_Descriptor_t;

/** A collection of object descriptors that make up a page. */
typedef NVM_Object_Descriptor_t   NVM_Page_t[];


/** Describes the properties of a page. */
typedef struct
{
  uint8_t          pageId;     /**< A page ID used when referring to the page. Must be unique. */
  NVM_Page_t const * page;    /**< A pointer to the list of all the objects in the page. */
  uint8_t          pageType;   /**< The type of page, normal or wear. */
} NVM_Page_Descriptor_t;

/** The list of pages registered for use. */
typedef NVM_Page_Descriptor_t   NVM_Page_Table_t[];

/** Configuration structure. */
typedef struct
{ NVM_Page_Table_t const *nvmPages;  /**< Pointer to table defining NVM pages. */
  uint8_t          const pages;      /**< Total number of physical pages. */
  uint8_t          const userPages;  /**< Number of defined (used) pages. */
  uint8_t          const *nvmArea;   /**< Pointer to nvm area in flash. */
} NVM_Config_t;

/** Result type for all the API functions. */
typedef enum
{
  nvmResultOk           = 0, /**< Flash read/write/erase successful. */
  nvmResultAddrInvalid  = 1, /**< Invalid address. Write to an address out of
                              *       bounds. */
  nvmResultInputInvalid = 2, /**< Invalid input data. */
  nvmResultDataInvalid  = 3, /**< Invalid data. */
  nvmResultWriteLock    = 4, /**< A write is currently in progress, and any
                             *        concurrent operations might cause problems. */
  nvmResultNoPages      = 5, /**< Initialization didn't find any pages. */
  nvmResultNoPage       = 6, /**< Could not find the specified page. It
                              *       might not have been saved yet. */
  nvmResultErrorInitial = 7, /**< Result not changed to OK. */
  nvmResultError        = 8  /**< General error. */
} NVM_Result_t;

/*******************************************************************************
 ***************************   PROTOTYPES   ************************************
 ******************************************************************************/

NVM_Result_t NVM_Init(NVM_Config_t const *nvmConfig);
NVM_Result_t NVM_Erase(uint32_t erasureCount);
NVM_Result_t NVM_Write(uint16_t pageId, uint8_t objectId);
NVM_Result_t NVM_Read(uint16_t pageId, uint8_t objectId);

#ifndef NVM_FEATURE_WEARLEVELGET_ENABLED
#define NVM_FEATURE_WEARLEVELGET_ENABLED    true
#endif
#if (NVM_FEATURE_WEARLEVELGET_ENABLED == true)
uint32_t NVM_WearLevelGet(void);
#endif

/** @} (end defgroup NVM) */
/** @} (end addtogroup EM_Drivers) */

#ifdef __cplusplus
}
#endif

#endif /* __NVM_H */
