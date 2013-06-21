/***************************************************************************//**
 * @file
 * @brief Non-Volatile Memory Driver configuration.
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
#include "nvm.h"

/***************************************************************************//**
 * @addtogroup EM_Drivers
 * @{
 ******************************************************************************/

/***************************************************************************//**
 * @addtogroup NVM
 * @{
 ******************************************************************************/

/*******************************************************************************
 *******************************   CONFIG   ************************************
 ******************************************************************************/

/* Objects. */
uint32_t nvmFirstTable[20] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20 };
uint32_t nvmSingleVariable = 32;

/* Page definition.
 * Combine objects with their id, and put them in a page. */
NVM_Page_t const nvmFirstPage =
{
/*{Pointer to object,          Size of object,         Object ID}, */
  { (uint8_t *) nvmFirstTable,      sizeof(nvmFirstTable),     FIRST_TABL_ID },
  { (uint8_t *) &nvmSingleVariable, sizeof(nvmSingleVariable), SINGL_VAR_ID  },
  NVM_PAGE_TERMINATION /* Null termination of table. Do not remove! */
};

/* Objects. */
uint32_t nvmSecondTable[20] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20 };

/* Page definition.
 * Combine objects with their id, and put them in a page. */
NVM_Page_t const nvmSecondPage =
{
/*{Pointer to object,          Size of object,         Object ID}, */
  { (uint8_t *) nvmSecondTable, sizeof(nvmSecondTable), SECOND_TABL_ID },
  NVM_PAGE_TERMINATION /* Null termination of table. Do not remove! */
};

/* Objects. */
uint8_t nvmWearTable[] = { 1, 2, 3, 4, 5 };

/* Page definition.
 * Combine objects with their id, and put them in a page.
 * This page contains only one object, since it is going to be
 * used as a wear page. */
NVM_Page_t const nvmWearPage =
{
/*{Pointer to object,    Size of object,    Object ID}, */
  { (uint8_t *) nvmWearTable, sizeof(nvmWearTable), WEAR_TABL_ID },
  NVM_PAGE_TERMINATION /* Null termination of table. Do not remove! */
};


/* Register pages.
 * Connect pages to page IDs, and define the type of page. */
NVM_Page_Table_t const nvmPages =
{
/*{Page ID,        Page pointer, Page type}, */
  { FIRST_PAGE_ID,  &nvmFirstPage,  nvmPageTypeNormal },
  { SECOND_PAGE_ID, &nvmSecondPage, nvmPageTypeNormal },
  { WEAR_PAGE_ID,   &nvmWearPage,   nvmPageTypeWear   }
};

/** @} (end addtogroup NVM */
/** @} (end addtogroup EM_Drivers) */
