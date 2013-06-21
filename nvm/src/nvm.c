/***************************************************************************//**
 * @file
 * @brief Non-Volatile Memory Driver.
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
 * NVM_Erase()
 * NVM_Init()
 * NVM_Read()
 * NVM_WearLevel()
 * NVM_Write()
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
#include <stdbool.h>
#include "nvm.h"
#include "nvm_hal.h"

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

/** Version constant of the NVM system. Is stored together with the datablocks
 * so that it is easy to upgrade between different versions of the system. */
#define NVM_VERSION              0x2U

/* Sizes. Internal sizes of different objects */
#define NVM_CONTENT_SIZE         (NVM_PAGE_SIZE - (NVM_HEADER_SIZE + NVM_FOOTER_SIZE))
#define NVM_WEAR_CONTENT_SIZE    (NVM_PAGE_SIZE - NVM_HEADER_SIZE)

/* Generic constants. Clears up the code and make it more readable. */
#define NVM_PAGE_EMPTY_VALUE                   0xffffU
#define NVM_NO_PAGE_RETURNED                   0xffffffffUL
#define NVM_NO_WRITE_16BIT                     0xffffU
#define NVM_NO_WRITE_32BIT                     0xffffffffUL
#define NVM_HIGHEST_32BIT                      0xffffffffUL
#define NVM_FLIP_FIRST_BIT_OF_32_WHEN_WRITE    0xffff7fffUL
#define NVM_FIRST_BIT_ONE                      0x8000U
#define NVM_FIRST_BIT_ZERO                     0x7fffU
#define NVM_LAST_BIT_ZERO                      0xfffeU

#define NVM_CHECKSUM_INITIAL                   0xffffU
#define NVM_CHECKSUM_LENGTH                    2U

#define NVM_PAGES_PER_WEAR_HISTORY             8U

/* Macros for acquiring and releasing write lock. Currently empty but could be redefined */
/* in RTOSes to add resources protection. Without it, it is not smart to call NVM module */
/* from interrupts or other tasks without ensuring that it is not used by main thread.   */
#ifndef NVM_ACQUIRE_WRITE_LOCK
#define NVM_ACQUIRE_WRITE_LOCK
#endif

#ifndef NVM_RELEASE_WRITE_LOCK
#define NVM_RELEASE_WRITE_LOCK
#endif

/** @endcond */

/*******************************************************************************
 ******************************   TYPEDEFS   ***********************************
 ******************************************************************************/

/** @cond DO_NOT_INCLUDE_WITH_DOXYGEN */
/** Result types for the internal page validation function. */
typedef enum
{
  nvmValidateResultOk       = 0, /**< Page validates. */
  nvmValidateResultOkMarked = 1, /**< Page validates, but is marked for copying. */
  nvmValidateResultOld      = 2, /**< Page has got an older version number. */
  nvmValidateResultError    = 3  /**< Page does not validate. */
} NVM_ValidateResult_t;

/** A struct representing the header of each page stored in NVM. This is a
 *  packed struct that is stored and retrieved from NVM directly. */
typedef struct
{
  uint16_t watermark; /**< The watermark is made up of a update flag (1st bit) and the logical address of the page.  */
  uint32_t updateId;  /**< The update id is the update id of this physical location in memory and sticks with it. */
  uint16_t version;   /**< The version number of the API is also stored in the page for any future updates. */
} NVM_Page_Header_t;

/** size of page header on flash (not in RAM) */
#define NVM_HEADER_SIZE          (2*sizeof(uint16_t)+sizeof(uint32_t))

/** A struct representing the footer of each page stored in NVM. This is a
 *  packed struct that is stored and retrieved from NVM directly. */
typedef struct
{
  uint16_t checksum;  /**< Contains a 16 bit XOR checksum of the content of the page. */
  uint16_t watermark; /**< Contains the same watermark as the opening of the page, but with the update bit always set to high.*/
} NVM_Page_Footer_t;

/** size of page footer on flash (not in RAM) */
#define NVM_FOOTER_SIZE          (2*sizeof(uint16_t))

/** @endcond */

/*******************************************************************************
 *******************************   STATICS   ***********************************
 ******************************************************************************/

/** @cond DO_NOT_INCLUDE_WITH_DOXYGEN */
/** Result types for the internal page validation function. */

static NVM_Config_t const *nvmConfig;

#if (NVM_FEATURE_STATIC_WEAR_ENABLED == true)
/* Static wear leveling */

/* Bit list that records which pages have been rewritten.
 * The array contains number of pages divided by 8 bits in a byte. */
static uint8_t nvmStaticWearWriteHistory[(NVM_MAX_NUMBER_OF_PAGES+(NVM_PAGES_PER_WEAR_HISTORY-1))/NVM_PAGES_PER_WEAR_HISTORY];

/* Number of different page writes recorded in history. */
static uint16_t nvmStaticWearWritesInHistory;

/* Number of page erases performed since last rest. */
static uint16_t nvmStaticWearErasesSinceReset;

/* Stop recurring calls from causing mayhem. */
static bool nvmStaticWearWorking = false;
#endif

/** @endcond */

/*******************************************************************************
 ******************************   PROTOTYPES   *********************************
 ******************************************************************************/

/** @cond DO_NOT_INCLUDE_WITH_DOXYGEN */

static uint8_t* NVM_PageFind(uint16_t pageId);
static uint8_t* NVM_ScratchPageFindBest(void);
static NVM_Result_t NVM_PageErase(uint8_t *pPhysicalAddress);
static NVM_Page_Descriptor_t NVM_PageGet(uint16_t pageId);
static NVM_ValidateResult_t NVM_PageValidate(uint8_t *pPhysicalAddress);

#if (NVM_FEATURE_WEAR_PAGES_ENABLED == true)
static uint16_t NVM_WearIndex(uint8_t *pPhysicalAddress, NVM_Page_Descriptor_t *pPageDesc);
static bool NVM_WearReadIndex(uint8_t *pPhysicalAddress, NVM_Page_Descriptor_t *pPageDesc, uint16_t *pIndex);
#endif

static void NVM_ChecksumAdditive(uint16_t *pChecksum, void *pBuffer, uint16_t len);

#if (NVM_FEATURE_STATIC_WEAR_ENABLED == true)
static void NVM_StaticWearReset(void);
static void NVM_StaticWearUpdate(uint16_t address);
static NVM_Result_t NVM_StaticWearCheck(void);
#endif
/** @endcond */

/*******************************************************************************
 ***************************   GLOBAL FUNCTIONS   ******************************
 ******************************************************************************/

/***************************************************************************//**
 * @brief
 *   Initialize the NVM manager.
 *
 * @details
 *   Use this function to initialize and validate the NVM. Should be run on
 *   startup. The result of this process is then returned in the form of a
 *   NVM_Result_t.
 *
 *   If nvmResultOk is returned, everything went according to plan and you
 *   can use the API right away. If nvmResultNoPages is returned this is a
 *   device that validates, but is empty. The proper way to handle this is to
 *   first reset the memory using NVM_Erase, and then write any initial
 *   data.
 *
 *   If a nvmResultError, or anything more specific, is returned something
 *   irreparable happened, and the system cannot be used reliably. A simple
 *   solution to this would be to erase and reinitialize, but this will then
 *   cause data loss.
 *
 * @param[in] config
 *   Pointer to structure defining NVM area.
 *
 * @return
 *   Returns the result of the initialization using a
 *   NVM_InitResult_TypeDef.
 ******************************************************************************/
NVM_Result_t NVM_Init(NVM_Config_t const *config)
{
  uint16_t     page;
  /* Variable to store the result returned at the end. */
  NVM_Result_t result = nvmResultErrorInitial;

  /* Physical address of the current page. */
  uint8_t *pPhysicalAddress = (uint8_t *)(config->nvmArea);
  /* Physical address of a suspected duplicate page under observation. */
  uint8_t *pDuplicatePhysicalAddress;

  /* Logical address of the current page. */
  uint16_t logicalAddress;
  /* Logical address of a duplicate page. */
  uint16_t duplicateLogicalAddress;

  /* Temporary variable to store results of a validation operation. */
  NVM_ValidateResult_t validationResult;
  /* Temporary variable to store results of a erase operation. */
  NVM_Result_t         eraseResult;

  /* if there is no spare page, return error */
  if( (config->pages <= config->userPages) || (config->pages > NVM_MAX_NUMBER_OF_PAGES) )
    return nvmResultError;

  /* now check that page structures fits to physical page size */
  { 
    uint16_t pageIdx = 0, obj = 0, sum = 0;
    const NVM_Page_Descriptor_t *current_page;
    
    for(pageIdx=0;pageIdx < config->userPages; pageIdx++)
    { 
      sum = 0;
      obj = 0;
      current_page = &((*(config->nvmPages))[pageIdx]);

      while( (*(current_page->page))[obj].location != 0)
        sum += (*(current_page->page))[obj++].size;

      if(current_page->pageType == nvmPageTypeNormal)
      {
        if( sum > NVM_CONTENT_SIZE )
        {
          return nvmResultError; /* objects bigger than page size */
        }
      } 
      else
      {
        if(current_page->pageType == nvmPageTypeWear)
        {
          if( (sum+NVM_CHECKSUM_LENGTH) > NVM_WEAR_CONTENT_SIZE )
          {
            return nvmResultError; /* objects bigger than page size */
          }
        } else 
          {
            return nvmResultError; /* unknown page type */
          }
      }
    }
  }

  nvmConfig = config;

  /* Require write lock to continue. */
  NVM_ACQUIRE_WRITE_LOCK

  /* Initialize the NVM. */
  NVMHAL_Init();

#if (NVM_FEATURE_STATIC_WEAR_ENABLED == true)
  /* Initialize the static wear leveling functionality. */
  NVM_StaticWearReset();
#endif

  /* Run through all pages and see if they validate if they contain content. */
  for (page = 0; page < nvmConfig->pages; ++page)
  {
    /* Read the logical address of the page stored at the current physical
     * address, and compare it to the value of an empty page. */
    NVMHAL_Read(pPhysicalAddress, &logicalAddress, sizeof(logicalAddress));
    if (NVM_PAGE_EMPTY_VALUE != logicalAddress)
    {
      /* Not an empty page. Check if it validates. */
      validationResult = NVM_PageValidate(pPhysicalAddress);

      /* Three different kinds of pages. */
      if (nvmValidateResultOk == validationResult)
      {
        /* We have found a valid page, so the initial error can be changed to an
         * OK result. */
        if (nvmResultErrorInitial == result)
        {
          result = nvmResultOk;
        }
      }
      else if (nvmValidateResultOkMarked == validationResult)
      {
        /* Page validates, but is marked for write.
         * There might exist a newer version. */

        /* Walk through all the possible pages looking for a page with
         * matching watermark. */
        pDuplicatePhysicalAddress = (uint8_t *)(nvmConfig->nvmArea);
        for (page = 0; (NVM_PAGE_EMPTY_VALUE != logicalAddress) && (page < nvmConfig->pages);
             ++page)
        {
          NVMHAL_Read(pDuplicatePhysicalAddress, &duplicateLogicalAddress, sizeof(duplicateLogicalAddress));

          if ((pDuplicatePhysicalAddress != pPhysicalAddress) && ((logicalAddress | NVM_FIRST_BIT_ONE) == duplicateLogicalAddress))
          {
            /* Duplicate page has got the same logical address. Check if it
             * validates. */
            validationResult = NVM_PageValidate(pDuplicatePhysicalAddress);

            if (nvmValidateResultOk == validationResult)
            {
              /* The new one validates, delete the old one. */
              eraseResult = NVM_PageErase(pPhysicalAddress);
            }
            else
            {
              /* The new one is broken, delete the new one. */
              eraseResult = NVM_PageErase(pDuplicatePhysicalAddress);
            }

            /* Something went wrong */
            if (nvmResultOk != eraseResult)
            {
              result = nvmResultError;
            }
          }

          /* Go to the next physical page. */
          pDuplicatePhysicalAddress += NVM_PAGE_SIZE;
        } /* End duplicate search loop. */

        /* If everything went OK and this is the first page we found, then
         * we can change the status from initial error to OK. */
        if (nvmResultErrorInitial == result)
        {
          result = nvmResultOk;
        }
      }
      else
      {
        /* Page does not validate */
        result = nvmResultError;
      }
    } /* End - not empty if. */

    /* Go to the next physical page. */
    pPhysicalAddress += NVM_PAGE_SIZE;
  } /* End pages loop. */

  /* If no pages was found, the system is not in use and should be reset. */
  if (nvmResultErrorInitial == result)
  {
    result = nvmResultNoPages;
  }

  /* Give up write lock and open for other API operations. */
  NVM_RELEASE_WRITE_LOCK

  return result;
}

/***************************************************************************//**
 * @brief
 *   Erase the entire NVM.
 *
 * @details
 *   Use this function to erase the entire non-volatile memory area allocated to
 *   the NVM system. It is possible to set a fixed erasure count for all the
 *   pages, or retain the existing one. To retain the erasure count might not be
 *   advisable if an error has occurred since this data may also have been
 *   damaged.
 *
 * @param[in] erasureCount
 *   Specifies which erasure count to set for the blank pages. Pass
 *   NVM_ERASE_RETAINCOUNT to retain the erasure count.
 *
 * @return
 *   Returns the result of the erase operation using a NVM_Result_t.
 ******************************************************************************/
NVM_Result_t NVM_Erase(uint32_t erasureCount)
{
  uint16_t     page;
  /* Result used when returning from the function. */
  NVM_Result_t result = nvmResultErrorInitial;

  /* Location of physical page. */
  uint8_t *pPhysicalAddress = (uint8_t *)(nvmConfig->nvmArea);

  /* Container for moving old erasure count, or set to new. */
  uint32_t tempErasureCount = erasureCount;


  /* Require write lock to continue. */
  NVM_ACQUIRE_WRITE_LOCK


  /* Loop over all the pages, as long as everything is OK. */
  for (page = 0;
       (page < nvmConfig->pages) && ((nvmResultOk == result) || (nvmResultErrorInitial == result));
       ++page)
  {
    /* If erasureCount input is set to the retain constant, we need to get the
     * old erasure count before we erase the page. */
    if (NVM_ERASE_RETAINCOUNT == erasureCount)
    {
      /* Read old erasure count. */
      NVMHAL_Read(pPhysicalAddress + 2, &tempErasureCount, sizeof(tempErasureCount));
    }

    /* Erase page. */
    result = NVMHAL_PageErase(pPhysicalAddress);

    /* If still OK, write erasure count to page. */
    if (nvmResultOk == result)
    {
      result = NVMHAL_Write(pPhysicalAddress + 2, &tempErasureCount, sizeof(tempErasureCount));
    }

    /* Go to the next physical page. */
    pPhysicalAddress += NVM_PAGE_SIZE;
  }

  /* Give up write lock and open for other API operations. */
  NVM_RELEASE_WRITE_LOCK

  return result;
}

/***************************************************************************//**
 * @brief
 *   Write an object or a page.
 *
 * @details
 *   Use this function to write an object or an entire page to NVM. It takes a
 *   page and an object and updates this object with the data pointed to by the
 *   corresponding page entry. All the objects in a page can be written
 *   simultaneously by using NVM_WRITE_ALL instead of an object ID. For "normal"
 *   pages it simply finds not used page in flash (with lowest erase counter) and
 *   copies all objects belonging to this page updating objects defined by 
 *   objectId argument. For "wear" pages function tries to find spare place in 
 *   already used page and write object here - if there is no free space it uses
 *   new page invalidating previously used one.
 *
 * @param[in] pageId
 *   Identifier of the page you want to write to NVM.
 *
 * @param[in] objectId
 *   Identifier of the object you want to write. May be set to NVM_WRITE_ALL
 *   to write the entire page to memory.
 *
 * @return
 *   Returns the result of the write operation using a NVM_Result_t.
 ******************************************************************************/
NVM_Result_t NVM_Write(uint16_t pageId, uint8_t objectId)
{
  /* Result variable used as return value from the function. */
  NVM_Result_t result = nvmResultErrorInitial;

  /* Watermark to look for when finding page. First bit true. */
  uint16_t              watermark = pageId | NVM_FIRST_BIT_ONE;
  /* Watermark used when flipping the duplication bit of a page. */
  const uint32_t flipWatermark = NVM_FLIP_FIRST_BIT_OF_32_WHEN_WRITE;

  /* Page descriptor used for accessing page type and page objects. */
  NVM_Page_Descriptor_t pageDesc;

  /* Page header and footer. Used to store old version and to easily update and
   * write new version. */
  NVM_Page_Header_t header;

  /* Variable used for checksum calculation. Starts at defined initial value. */
  uint16_t checksum = NVM_CHECKSUM_INITIAL;

  /* Physical addresses in memory for the old and new version of the page. */
  uint8_t *pOldPhysicalAddress = (uint8_t *) NVM_NO_PAGE_RETURNED;
  uint8_t *pNewPhysicalAddress = (uint8_t *) NVM_NO_PAGE_RETURNED;

  /* Offset address within page. */
  uint16_t offsetAddress;
  /* Single byte buffer used when copying data from an old page.*/
  uint8_t  copyBuffer;
  /* Object in page counter. */
  uint8_t  objectIndex;
  /* Amount of bytes to copy. */
  uint16_t copyLength;

  /* Handle wear pages. Should we handle this as an extra write to an existing
   * page or create a new one. */
  bool wearWrite = false;

#if (NVM_FEATURE_WRITE_NECESSARY_CHECK_ENABLED == true)
  /* Bool used when checking if a write operation is needed. */
  bool rewriteNeeded;
#endif

#if (NVM_FEATURE_WEAR_PAGES_ENABLED == true)
  /* Used to hold the checksum of the wear object. */
  uint16_t wearChecksum;
  /* Byte size of the wear object. Includes checksum length. */
  uint16_t wearObjectSize;
  /* Used to specify the internal index of the wear object in a page. */
  uint16_t wearIndex;

  #if (NVM_FEATURE_WRITE_VALIDATION_ENABLED == true)
  /* The new wear index the object will be written to. */
  uint16_t wearIndexNew;
  #endif
#endif

  /* Require write lock to continue. */
  NVM_ACQUIRE_WRITE_LOCK

  /* Find old physical address. */
  pOldPhysicalAddress = NVM_PageFind(pageId);

  /* Get the page configuration. */
  pageDesc = NVM_PageGet(pageId);

#if (NVM_FEATURE_WRITE_NECESSARY_CHECK_ENABLED == true)
  /* If there is an old version of the page, it might not be necessary to update
   * the data. Also check that this is a normal page and that the static wear
   * leveling system is not working (this system might want to rewrite pages
   * even if the data is similar to the old version). */
  if (((uint8_t *) NVM_NO_PAGE_RETURNED != pOldPhysicalAddress)
      && (nvmPageTypeNormal == pageDesc.pageType)
#if (NVM_FEATURE_STATIC_WEAR_ENABLED == true)
      && !nvmStaticWearWorking
#endif

      )
  {
    rewriteNeeded = false;
    objectIndex   = 0;
    offsetAddress = 0;

    /* Loop over items as long as no rewrite is needed and the current item has
    * got a size other than 0. Size 0 is used as a marker for a NULL object. */
    while (((*pageDesc.page)[objectIndex].size != 0) && !rewriteNeeded)
    {
      /* Check if every object should be written or if this is the object to
       * write. */
      if ((NVM_WRITE_ALL_CMD == objectId) ||
          ((*pageDesc.page)[objectIndex].objectId == objectId))
      {
        /* Compare object to RAM. */

        copyLength = (*pageDesc.page)[objectIndex].size;

        /* Loop over each byte of the object and compare with RAM. */
        while (copyLength != 0)
        {
          /* Get byte from NVM. */
          NVMHAL_Read(pOldPhysicalAddress + offsetAddress + NVM_HEADER_SIZE,
                      &copyBuffer,
                      sizeof(copyBuffer));

          /* Check byte in NVM with the corresponding byte in RAM. */
          if (*(uint8_t *)((*pageDesc.page)[objectIndex].location + offsetAddress) != copyBuffer)
          {
            rewriteNeeded = true;
            break;
          }

          /* Move offset. */
          offsetAddress += sizeof(copyBuffer);
          copyLength    -= sizeof(copyBuffer);
        }
      }
      else
      {
        /* Move offset past the object. */
        offsetAddress += (*pageDesc.page)[objectIndex].size;
      }

      /* Check next object. */
      objectIndex++;
    }

    if (!rewriteNeeded)
    {
      /* Release write lock before return. */
      NVM_RELEASE_WRITE_LOCK

      return nvmResultOk;
    }
  }
#endif



#if (NVM_FEATURE_WEAR_PAGES_ENABLED == true)
  /* If this is a wear page then we can check if we can possibly squeeze another
   * version of the object inside the already existing page. If this is possible
   * we set the wearWrite boolean. This will then make us ignore the normal
   * write operation. */
  if (nvmPageTypeWear == pageDesc.pageType)
  {
    /* Calculate checksum. The wear page checksum is only stored in 15 bits,
     * because we need one bit to mark that the object is written. This bit is
     * always set to 0. */

    wearChecksum = NVM_CHECKSUM_INITIAL;
    NVM_ChecksumAdditive(&wearChecksum, (*pageDesc.page)[0].location, (*pageDesc.page)[0].size);
    wearChecksum &= NVM_LAST_BIT_ZERO;

    /* If there was an old page. */
    if ((uint8_t *) NVM_NO_PAGE_RETURNED != pOldPhysicalAddress)
    {
      /* Find location in old page. */
      wearIndex      = NVM_WearIndex(pOldPhysicalAddress, &pageDesc);
      wearObjectSize = (*pageDesc.page)[0].size + NVM_CHECKSUM_LENGTH;

      /* Check that the wearIndex returned is within the length of the page. */
      if (wearIndex < ((uint16_t) NVM_WEAR_CONTENT_SIZE) / wearObjectSize)
      {
        result = NVMHAL_Write(pOldPhysicalAddress + NVM_HEADER_SIZE + wearIndex * wearObjectSize,
                              (*pageDesc.page)[0].location,
                              (*pageDesc.page)[0].size);
        result = NVMHAL_Write(pOldPhysicalAddress + NVM_HEADER_SIZE + wearIndex * wearObjectSize + (*pageDesc.page)[0].size,
                              &wearChecksum,
                              sizeof(wearChecksum));

        /* Register that we have now written to the old page. */
        wearWrite = true;

#if (NVM_FEATURE_WRITE_VALIDATION_ENABLED == true)
        /* Check if the newest one that is valid is the same as the one we just
         * wrote to the NVM. */
        if ((!NVM_WearReadIndex(pOldPhysicalAddress, &pageDesc, &wearIndexNew)) ||
            (wearIndexNew != wearIndex))
        {
          result = nvmResultError;
        }
#endif
      }
    } /* End of old page if. */
  }   /* End of wear page if. */
#endif

#if (NVM_FEATURE_WEAR_PAGES_ENABLED == true)
  /* Do not create a new page if we have already done an in-page wear write. */
  if (!wearWrite)
  {
#endif
  /* Mark any old page before creating a new one. */
  if ((uint8_t *) NVM_NO_PAGE_RETURNED != pOldPhysicalAddress)
  {
    result = NVMHAL_Write(pOldPhysicalAddress, &flipWatermark, 4);

    if (nvmResultOk != result)
    {
      /* Give up write lock and open for other API operations. */
      NVM_RELEASE_WRITE_LOCK
      return result;
    }
  }

  /* Find new physical address to write to. */
  pNewPhysicalAddress = NVM_ScratchPageFindBest();

  if ((uint8_t*) NVM_NO_PAGE_RETURNED == pNewPhysicalAddress)
  {
    /* Give up write lock and open for other API operations. */
    NVM_RELEASE_WRITE_LOCK
    return nvmResultError;
  }

  /* Generate and write header */
  header.watermark = watermark;
  header.updateId  = NVM_NO_WRITE_32BIT;
  header.version   = NVM_VERSION;

  /* store header at beginning of page */
  result = NVMHAL_Write(pNewPhysicalAddress, &header.watermark, sizeof(header.watermark));
  result = NVMHAL_Write(pNewPhysicalAddress + sizeof(header.watermark), &header.updateId, sizeof(header.updateId));
  result = NVMHAL_Write(pNewPhysicalAddress + sizeof(header.watermark)+ + sizeof(header.updateId), &header.version, sizeof(header.version));

  /* Reset address index within page. */
  offsetAddress = 0;
  /* Reset object in page counter. */
  objectIndex = 0;

  /* Loop over items as long as everything is OK, and the current item has got
   * a size other than 0. Size 0 is used as a marker for a NULL object. */
  while (((*pageDesc.page)[objectIndex].size != 0) && (nvmResultOk == result))
  {
    /* Check if every object should be written or if this is the object to
     * write. */
    if ((NVM_WRITE_ALL_CMD == objectId) ||
        ((*pageDesc.page)[objectIndex].objectId == objectId))
    {
      /* Write object from RAM. */
      result = NVMHAL_Write(pNewPhysicalAddress + offsetAddress + NVM_HEADER_SIZE,
                            (*pageDesc.page)[objectIndex].location,
                            (*pageDesc.page)[objectIndex].size);
      offsetAddress += (*pageDesc.page)[objectIndex].size;

      NVM_ChecksumAdditive(&checksum, (*pageDesc.page)[objectIndex].location, (*pageDesc.page)[objectIndex].size);
    }
    else
    {
      /* Get version from old page. */
      if ((uint8_t *) NVM_NO_PAGE_RETURNED != pOldPhysicalAddress)
      {
        NVM_ChecksumAdditive(&checksum, pOldPhysicalAddress + offsetAddress + NVM_HEADER_SIZE, (*pageDesc.page)[objectIndex].size);

        copyLength = (*pageDesc.page)[objectIndex].size;

        while ((copyLength != 0) && (nvmResultOk == result))
        {
          /* Copies using an 1 byte buffer. Might be better to dynamically use larger if possible. */
          NVMHAL_Read(pOldPhysicalAddress + offsetAddress + NVM_HEADER_SIZE,
                      &copyBuffer,
                      sizeof(copyBuffer));
          result = NVMHAL_Write(pNewPhysicalAddress + offsetAddress + NVM_HEADER_SIZE,
                                &copyBuffer,
                                sizeof(copyBuffer));

          offsetAddress += sizeof(copyBuffer);
          copyLength    -= sizeof(copyBuffer);
        }
      }  /* End if old page. */
    }   /* Else-end of NVM_WRITE_ALL if-statement. */

    objectIndex++;
  }

  /* If we are creating a wear page, add the checksum directly after the data. */
#if (NVM_FEATURE_WEAR_PAGES_ENABLED == true)
  if (nvmPageTypeWear == pageDesc.pageType)
  {
    result = NVMHAL_Write(pNewPhysicalAddress + offsetAddress + NVM_HEADER_SIZE,
                          &wearChecksum,
                          sizeof(wearChecksum));
  }
  /* Generate and write footer on normal pages. */
  else
  {
#endif
  if (nvmResultOk == result)
  {
    /* write checksum at end of page */
    result = NVMHAL_Write(pNewPhysicalAddress + (NVM_PAGE_SIZE - NVM_FOOTER_SIZE), &checksum, sizeof(checksum));
    /* write watermark just after checksum at end of page */
    result = NVMHAL_Write(pNewPhysicalAddress + (NVM_PAGE_SIZE + sizeof(checksum) - NVM_FOOTER_SIZE), &watermark, sizeof(watermark));
  }

#if (NVM_FEATURE_WEAR_PAGES_ENABLED == true)
}     /* End of else from page type. */
#endif

#if (NVM_FEATURE_WRITE_VALIDATION_ENABLED == true)
  /* Validate that the correct data was written. */
  if (nvmValidateResultOk != NVM_PageValidate(pNewPhysicalAddress))
  {
    result = nvmResultError;
  }
#endif

#if (NVM_FEATURE_WEAR_PAGES_ENABLED == true)
}   /* End of if for normal write (!wearWrite). */
#endif

  /* Erase old if there was an old one and everything else have gone OK. */
  if ((!wearWrite) &&
      ((uint8_t *) NVM_NO_PAGE_RETURNED != pOldPhysicalAddress))
  {
    if (nvmResultOk == result)
    {
      result = NVM_PageErase(pOldPhysicalAddress);
    }
    else
    {
      NVM_PageErase(pNewPhysicalAddress);
    }
  }

  /* Give up write lock and open for other API operations. */
  NVM_RELEASE_WRITE_LOCK

  return result;
}

/***************************************************************************//**
 * @brief
 *   Read an object or an entire page.
 *
 * @details
 *   Use this function to read an object or an entire page from memory. It takes
 *   a page id and an object id (or the NVM_READ_ALL constant to read
 *   everything) and reads data from flash and puts it in the memory locations
 *   given in the page specification.
 *
 * @param[in] pageId
 *   Identifier of the page to read from.
 *
 * @param[in] objectId
 *   Identifier of the object to read. Can be set to NVM_READ_ALL to read
 *   an entire page.
 *
 * @return
 *   Returns the result of the read operation using a NVM_Result_t.
 ******************************************************************************/
NVM_Result_t NVM_Read(uint16_t pageId, uint8_t objectId)
{
#if (NVM_FEATURE_WEAR_PAGES_ENABLED == true)
  /* Variable used to fetch read index. */
  uint16_t wearIndex;
#endif

  /* Physical address of the page to read from. */
  uint8_t *pPhysicalAddress;

  /* Description of the page, used to find page type and objects. */
  NVM_Page_Descriptor_t pageDesc;

  /* Index of object in page. */
  uint8_t  objectIndex;
  /* Address of read location within a page. */
  uint16_t offsetAddress;


  /* Require write lock to continue. */
  NVM_ACQUIRE_WRITE_LOCK

  /* Find physical page. */
  pPhysicalAddress = NVM_PageFind(pageId);

  /* If no page was found, we cannot read anything. */
  if ((uint8_t*) NVM_NO_PAGE_RETURNED == pPhysicalAddress)
  {
    /* Give up write lock and open for other API operations. */
    NVM_RELEASE_WRITE_LOCK
    return nvmResultNoPage;
  }

  /* Get page description. */
  pageDesc = NVM_PageGet(pageId);

#if (NVM_FEATURE_WEAR_PAGES_ENABLED == true)
  /* If this is a wear page, we must find out which object in the page should be
   * read. */
  if (nvmPageTypeWear == pageDesc.pageType)
  {
    /* Find valid object in wear page and read it. */
    if (NVM_WearReadIndex(pPhysicalAddress, &pageDesc, &wearIndex))
    {
      NVMHAL_Read(pPhysicalAddress + NVM_HEADER_SIZE +
                  wearIndex * ((*pageDesc.page)[0].size + NVM_CHECKSUM_LENGTH),
                  (*pageDesc.page)[0].location,
                  (*pageDesc.page)[0].size);
    }
    else
    {
      /* No valid object was found in the page. */
      /* Give up write lock and open for other API operations. */
      NVM_RELEASE_WRITE_LOCK
      return nvmResultDataInvalid;
    }
  }
  else
#endif
  {
    /* Read normal page. */
    objectIndex   = 0;
    offsetAddress = 0;

#if (NVM_FEATURE_READ_VALIDATION_ENABLED == true)
    if (nvmValidateResultError == NVM_PageValidate(pPhysicalAddress))
    {
      /* Give up write lock and open for other API operations. */
      NVM_RELEASE_WRITE_LOCK
      return nvmResultDataInvalid;
    }
#endif

    /* Loop through and read the objects of a page, as long as the current item
     * has got a size other than 0. Size 0 is a marker for the NULL object. */
    while ((*pageDesc.page)[objectIndex].size != 0)
    {
      /* Check if every object should be read or if this is the object to read. */
      if ((NVM_READ_ALL_CMD == objectId) || ((*pageDesc.page)[objectIndex].objectId == objectId))
      {
        NVMHAL_Read(pPhysicalAddress + offsetAddress + NVM_HEADER_SIZE,
                    (*pageDesc.page)[objectIndex].location,
                    (*pageDesc.page)[objectIndex].size);
      }

      offsetAddress += (*pageDesc.page)[objectIndex].size;
      objectIndex++;
    }
  }

  /* Give up write lock and open for other API operations. */
  NVM_RELEASE_WRITE_LOCK

  return nvmResultOk;
}

/***************************************************************************//**
 * @brief
 *   Get maximum wear level.
 *
 * @details
 *   This function returns the amount of erase cycles for the most erased page
 *   in memory. This can be used as a rough measure of health for the device.
 *
 * @return
 *   Returns the wear level as a uint32_t.
 ******************************************************************************/
#if (NVM_FEATURE_WEARLEVELGET_ENABLED == true)
uint32_t NVM_WearLevelGet(void)
{
  uint16_t page;
  /* Used to temporarily store the update id of the current page. */
  uint32_t updateId;
  /* Worst (highest) update id. Used as return value. */
  uint32_t worstUpdateId = 0;

  /* Address of physical page. */
  uint8_t *pPhysicalAddress = (uint8_t *)(nvmConfig->nvmArea);

  /* Loop through all pages in memory. */
  for (page = 0; page < nvmConfig->pages; ++page)
  {
    /* Find and compare erasure count. */
    NVMHAL_Read(pPhysicalAddress + 2, &updateId, sizeof(updateId));
    if (updateId > worstUpdateId)
    {
      worstUpdateId = updateId;
    }

    /* Go to the next physical page. */
    pPhysicalAddress += NVM_PAGE_SIZE;
  }

  return worstUpdateId;
}
#endif

/*******************************************************************************
 ***************************   LOCAL FUNCTIONS   *******************************
 ******************************************************************************/

/** @cond DO_NOT_INCLUDE_WITH_DOXYGEN */

/***************************************************************************//**
 * @brief
 *   Find the physical address of a page.
 *
 * @details
 *   This function finds the physical address of a page given a page id by
 *   traversing the flash memory.
 *
 * @param[in] pageId
 *   NVM_Page_Ids that identifies the page.
 *
 * @return
 *   Returns the resulting address as a uint8_t*. If no page was found
 *   (uint8_t*)NVM_NO_PAGE_RETURNED is returned.
 ******************************************************************************/
static uint8_t* NVM_PageFind(uint16_t pageId)
{
  uint16_t page;
  /* Physical address to return. */
  uint8_t  *pPhysicalAddress = (uint8_t *)(nvmConfig->nvmArea);
  /* Temporary variable used to read and compare logical page address. */
  uint16_t logicalAddress;

  /* Loop through memory looking for a matching watermark. */
  for (page = 0; page < nvmConfig->pages; ++page)
  {
    /* Allow both versions of writing mark, invalid duplicates should already
     * have been deleted. */
    NVMHAL_Read(pPhysicalAddress, &logicalAddress, sizeof(logicalAddress));
    if (((pageId | NVM_FIRST_BIT_ONE) == logicalAddress) || (pageId == logicalAddress))
    {
      return pPhysicalAddress;
    }

    /* Move lookup point to the next page. */
    pPhysicalAddress += NVM_PAGE_SIZE;
  }

  /* No page found. */
  return (uint8_t *) NVM_NO_PAGE_RETURNED;
}

/***************************************************************************//**
 * @brief
 *   Finds the best scratch page.
 *
 * @details
 *   This function returns the least used of all the currently empty pages. This
 *   can be thought of as the best page to use if one wants the system to
 *   perform dynamic wear leveling.
 *
 * @return
 *   Address of the page is returned as a uint8_t*.
 ******************************************************************************/
static uint8_t* NVM_ScratchPageFindBest(void)
{
  uint16_t page;
  /* Address for physical page to return. */
  uint8_t  *pPhysicalPage = (uint8_t *) NVM_NO_PAGE_RETURNED;

  /* Variable used to read and compare update id of physical pages. */
  uint32_t updateId = NVM_HIGHEST_32BIT;
  /* The best update id found. */
  uint32_t bestUpdateId = NVM_HIGHEST_32BIT;

  /* Pointer to the current physical page. */
  uint8_t  *pPhysicalAddress = (uint8_t *)(nvmConfig->nvmArea);
  /* Logical address that identifies the page. */
  uint16_t logicalAddress;

  /* Loop through all pages in memory. */
  for (page = 0; page < nvmConfig->pages; ++page)
  {
    /* Read and check logical address. */
    NVMHAL_Read(pPhysicalAddress, &logicalAddress, sizeof(logicalAddress));
    if ((uint16_t) NVM_PAGE_EMPTY_VALUE == logicalAddress)
    {
      /* Find and compare erasure count. */
      NVMHAL_Read(pPhysicalAddress + 2, &updateId, sizeof(updateId));
      if (updateId < bestUpdateId)
      {
        bestUpdateId  = updateId;
        pPhysicalPage = pPhysicalAddress;
      }
    }

    /* Move lookup point to the next page. */
    pPhysicalAddress += NVM_PAGE_SIZE;
  }

  /* Return a pointer to the best/least used page. */
  return pPhysicalPage;
}

/***************************************************************************//**
 * @brief
 *   Erases a page.
 *
 * @details
 *   This function erases the page at a certain address. All the data is erased
 *   while the erasure count of the page is retained and updated.
 *
 * @param[in] address
 *   Pointer to the location you want to erase.
 *
 * @return
 *   Returns the result of the operation as a NVM_Result_t.
 ******************************************************************************/
static NVM_Result_t NVM_PageErase(uint8_t *pPhysicalAddress)
{
#if (NVM_FEATURE_STATIC_WEAR_ENABLED == true)
  /* Logical page address. */
  uint16_t logicalAddress;
#endif

  /* Read out the old page update id. */
  uint32_t updateId;
  NVMHAL_Read(pPhysicalAddress + 2, &updateId, sizeof(updateId));

#if (NVM_FEATURE_STATIC_WEAR_ENABLED == true)
  /* Get logical page address. */
  NVMHAL_Read(pPhysicalAddress, &logicalAddress, sizeof(logicalAddress));

  /* If not empty: mark as erased and check against threshold. */
  if (logicalAddress != NVM_PAGE_EMPTY_VALUE)
  {
    /* Set first bit low. */
    logicalAddress = logicalAddress & NVM_FIRST_BIT_ZERO;
    NVM_StaticWearUpdate(logicalAddress);
  }
#endif

  /* Erase the page. */
  NVMHAL_PageErase(pPhysicalAddress);

  /* Update erasure count. */
  updateId++;

  /* Write increased erasure count. */
  return NVMHAL_Write(pPhysicalAddress + 2, &updateId, sizeof(updateId));
}

/***************************************************************************//**
 * @brief
 *   Get the description of a page.
 *
 * @details
 *   This function returns the NVM_Page_Descriptor_t corresponding to a
 *   given NVM_Page_Ids.
 *
 * @note
 *   This function returns a struct instead of a pointer since the packed struct
 *   is as small as a pointer, and using a pointer might add more instruction
 *   overhead.
 *
 * @param[in] pageId
 *   Identifier of the page.
 *
 * @return
 *   Returns the page description as a NVM_Page_Descriptor_t.
 ******************************************************************************/
static NVM_Page_Descriptor_t NVM_PageGet(uint16_t pageId)
{
  uint8_t                            pageIndex;
  static const NVM_Page_Descriptor_t nullPage = { (uint8_t) 0, 0, (NVM_Page_Type_t) 0 };

  /* Step through all configured pages. */
  for (pageIndex = 0; pageIndex < nvmConfig->userPages; ++pageIndex)
  {
    /* If this is the page we want, return it. */
    if ( (*(nvmConfig->nvmPages))[pageIndex].pageId == pageId)
    {
      return (*(nvmConfig->nvmPages))[pageIndex];
    }
  }

  /* No page matched the ID, return a NULL page to mark the error. */
  return nullPage;
}

/***************************************************************************//**
 * @brief
 *   Validate a certain address.
 *
 * @details
 *   This function checks if there is a valid page at the supplied address.
 *
 *   For normal pages the checksum is calculated and compared against the one
 *   stored in NVM, and the watermark in the header and footer are compared.
 *   Results are returned based on the write mark of the page.
 *
 *   For wear pages we check that there are any readable objects in the page
 *   using the lookup function used by the read command. Here we are dependent
 *   on user settings to control the checksum.
 *
 * @param[in] pPhysicalAddress
 *   Pointer to the location you want to check.
 *
 * @return
 *   Returns the validation status of the address as a NVM_ValidateResult_t.
 ******************************************************************************/
static NVM_ValidateResult_t NVM_PageValidate(uint8_t *pPhysicalAddress)
{
  /* Result used as return value from the function. */
  NVM_ValidateResult_t result;

  /* Objects used to read out the page header and footer. */
  NVM_Page_Header_t header;
  NVM_Page_Footer_t footer;

  /* Descriptor for the current page. */
  NVM_Page_Descriptor_t pageDesc;

  /* Variable used for calculating checksums. */
  uint16_t checksum;

  /* Offset of object in page. */
  uint8_t  objectIndex;
  /* Address of read location within a page. */
  uint16_t offsetAddress;

#if (NVM_FEATURE_WEAR_PAGES_ENABLED == true)
  /* Variable used to fetch read index. */
  uint16_t index;
#endif

  /* Read page header data */
  NVMHAL_Read(pPhysicalAddress, &header.watermark, sizeof(header.watermark));
  NVMHAL_Read(pPhysicalAddress + sizeof(header.watermark), &header.updateId, sizeof(header.updateId));
  NVMHAL_Read(pPhysicalAddress + sizeof(header.watermark) + sizeof(header.updateId), &header.version, sizeof(header.version));

  /* Stop immediately if data is from another version of the API. */
  if (NVM_VERSION != header.version)
  {
    return nvmValidateResultOld;
  }

  /* Get the page configuration. */
  pageDesc = NVM_PageGet((header.watermark & NVM_FIRST_BIT_ZERO));


#if (NVM_FEATURE_WEAR_PAGES_ENABLED == true)
  if (nvmPageTypeWear == pageDesc.pageType)
  {
    /* Wear page. */

    /* If first bit is already zero the page is marked as a duplicate. */
    if ((header.watermark & NVM_FIRST_BIT_ZERO) == header.watermark)
    {
      result = nvmValidateResultOkMarked;
    }
    else
    {
      /* Page is not marked as a duplicate. */
      result = nvmValidateResultOk;
    }

    /* If we do not have any valid objects in the page it is invalid. */
    if (!NVM_WearReadIndex(pPhysicalAddress, &pageDesc, &index))
    {
      result = nvmValidateResultError;
    }
  }
  else
#endif
  {
    /* Normal page. */
    NVMHAL_Read(pPhysicalAddress + (NVM_PAGE_SIZE - NVM_FOOTER_SIZE), &footer.checksum, sizeof(footer.checksum));
    NVMHAL_Read(pPhysicalAddress + (NVM_PAGE_SIZE + sizeof(checksum) - NVM_FOOTER_SIZE), &footer.watermark, sizeof(footer.watermark));
    /* Check if watermark or watermark with flipped write bit matches. */
    if (header.watermark == footer.watermark)
    {
      result = nvmValidateResultOk;
    }
    else if ((header.watermark | NVM_FIRST_BIT_ONE) == footer.watermark)
    {
      result = nvmValidateResultOkMarked;
    }
    else
    {
      result = nvmValidateResultError;
    }

    /* Calculate checksum and compare with the one stored. */
    objectIndex   = 0;
    offsetAddress = 0;
    checksum      = NVM_CHECKSUM_INITIAL;

    /* Calculate per object using the HAL. Loop over items as long as the
     * current object has got a size other than 0. Size 0 is used as a marker
     * for a NULL object. */
    while ((*pageDesc.page)[objectIndex].size != 0)
    {
      NVMHAL_Checksum(&checksum, (uint8_t *) pPhysicalAddress + NVM_HEADER_SIZE + offsetAddress, (*pageDesc.page)[objectIndex].size);
      offsetAddress += (*pageDesc.page)[objectIndex].size;
      objectIndex++;
    }

    if (checksum != footer.checksum)
    {
      result = nvmValidateResultError;
    }
  }

  return result;
}

#if (NVM_FEATURE_WEAR_PAGES_ENABLED == true)
/***************************************************************************//**
 * @brief
 *   Find used wear slots.
 *
 * @details
 *   This function returns the index of the first unused space in a wear page,
 *   might be outside of the page. The index is based on using the size of the
 *   object and checksum as the step size when traversing the memory.
 *
 * @param[in] *pPhysicalAddress
 *   Pointer to the start of the page you want to check.
 *
 * @param[in] pageDesc
 *   The page descriptor for the page.
 *
 * @return
 *   Returns the index as a uint16_t.
 ******************************************************************************/
static uint16_t NVM_WearIndex(uint8_t *pPhysicalAddress, NVM_Page_Descriptor_t *pPageDesc)
{
  /* Index to return. */
  uint16_t wearIndex = 0;

  /* Temporary variable used when calculating and comparing checksums. */
  uint16_t checksum;

  /* Size of the object in the wear page, including a 16 bit checksum. */
  uint16_t wearObjectSize = ((*pPageDesc->page)[0].size + NVM_CHECKSUM_LENGTH);


  /* Loop over possible pages. Stop when empty page was found. */
  while (wearIndex < NVM_WEAR_CONTENT_SIZE / wearObjectSize)
  {
    NVMHAL_Read((uint8_t *)(pPhysicalAddress + NVM_HEADER_SIZE +
                            wearIndex * wearObjectSize +
                            (*pPageDesc->page)[0].size
                            ),
                &checksum,
                sizeof(checksum));

    /* Flip the last bit of the checksum to zero. This is a mark used to
     * determine that an object is written to this location. */
    if ((checksum & NVM_LAST_BIT_ZERO) != checksum)
    {
      /* Break the loop and accept this location. */
      break;
    }

    /* Not a valid position for a new write.
    * Increase the index and check again. */
    wearIndex++;
  }

  return wearIndex;
}
#endif

/***************************************************************************//**
 * @brief
 *   Find newest wear index in a page.
 *
 * @details
 *   This function finds the index of the newest valid instance of the wear
 *   object in a given page and assigns it to the given index variable. The
 *   function returns false if there are no valid instances.
 *
 * @param[in] *pPhysicalAddress
 *   Pointer to the start of the page you want to check.
 *
 * @param[in] *pageDesc
 *   The page descriptor for the page.
 *
 * @param[in] *index
 *   Pointer to where to store the index found.
 *
 * @return
 *   Returns the result of the operation as a boolean.
 ******************************************************************************/
#if (NVM_FEATURE_WEAR_PAGES_ENABLED == true)
static bool NVM_WearReadIndex(uint8_t *pPhysicalAddress, NVM_Page_Descriptor_t *pPageDesc, uint16_t *pIndex)
{
#if (NVM_FEATURE_READ_VALIDATION_ENABLED == true)
  /* Variable used for calculating checksum when validating. */
  uint16_t checksum = NVM_CHECKSUM_INITIAL;
#endif

  /* Length of wear object plus checksum. */
  const uint16_t wearObjectSize = ((*pPageDesc->page)[0].size + NVM_CHECKSUM_LENGTH);

  /* Return value. */
  bool validObjectFound = false;

  /* Buffer used when reading checksum. */
  uint16_t readBuffer;

  /* Initialize index at max plus one object. */
  *pIndex = (((uint16_t) NVM_WEAR_CONTENT_SIZE) / wearObjectSize);

  /* Loop over possible pages. Stop when first OK page is found. */
  while ((*pIndex > 0) && (!validObjectFound))
  {
    (*pIndex)--;

    /* Initialize checksum, and then calculate it from the HAL.*/
    uint8_t *temp = pPhysicalAddress + NVM_HEADER_SIZE + (*pIndex) * wearObjectSize + (*pPageDesc->page)[0].size;
    NVMHAL_Read((uint8_t *) temp, &readBuffer, sizeof(readBuffer));

#if (NVM_FEATURE_READ_VALIDATION_ENABLED == true)
    /* Calculate the checksum before accepting the object. */
    checksum = NVM_CHECKSUM_INITIAL;
    NVMHAL_Checksum(&checksum, pPhysicalAddress + NVM_HEADER_SIZE + (*pIndex) * wearObjectSize, (*pPageDesc->page)[0].size);
    /* Flips the last bit of the checksum to zero. This is a mark used to
     * determine whether we have written anything to the page. */
    if ((uint16_t)(checksum & NVM_LAST_BIT_ZERO) == readBuffer)
#else
    if (NVM_NO_WRITE_16BIT != readBuffer)
#endif
    {
      validObjectFound = true;
    }
  }

  return validObjectFound;
}
#endif

/***************************************************************************//**
 * @brief
 *   Calculate checksum according to CCITT CRC16.
 *
 * @details
 *   This function calculates a checksum of the supplied buffer.
 *   The checksum is calculated using CCITT CRC16 plynomial x^16+x^12+x^5+1.
 *
 * @param[in] pChecksum
 *   Pointer to where the checksum should be calculated and stored. This buffer
 *   should be initialized. A good consistent starting point would be
 *   NVM_CHECKSUM_INITIAL.
 *
 * @param[in] pBuffer
 *   Pointer to the data you want to calculate a checksum for.
 *
 * @param[in] len
 *   The length of the data.
 ******************************************************************************/
static void NVM_ChecksumAdditive(uint16_t *pChecksum, void *pBuffer, uint16_t len)
{
  uint8_t *pointer = (uint8_t *) pBuffer;
  uint16_t crc = *pChecksum;

  while(len--)
  {
    crc = (crc >> 8) | (crc << 8);
    crc ^= *pointer++;
    crc ^= (crc & 0xf0) >> 4;
    crc ^= (crc & 0x0f) << 12;
    crc ^= (crc & 0xff) << 5;
  }

  *pChecksum = crc;
}

#if (NVM_FEATURE_STATIC_WEAR_ENABLED == true)
/***************************************************************************//**
 * @brief
 *   Resets the static wear leveling system.
 *
 * @details
 *   This function resets the history of the static wear leveling system. This
 *   is done at startup and whenever all the pages have been updated at least
 *   once and the threshold for rewrites have been reached.
 ******************************************************************************/
static void NVM_StaticWearReset(void)
{
  uint16_t i;
  nvmStaticWearErasesSinceReset = 0;
  nvmStaticWearWritesInHistory  = 0;

  for (i = 0; (NVM_PAGES_PER_WEAR_HISTORY * i) < nvmConfig->userPages; i += 1)
  {
    nvmStaticWearWriteHistory[i] = 0;
  }
}

/***************************************************************************//**
 * @brief
 *   Mark a page as updated.
 *
 * @details
 *   This function marks the given page as updated, and updates the update
 *   count. It then executes the StaticWearCheck function.
 *
 * @param[in] address
 *   Logical address of the page that was updated.
 ******************************************************************************/
static void NVM_StaticWearUpdate(uint16_t address)
{
  if (address < nvmConfig->userPages)
  {
    /* Mark page with logical address as written. */

    /* Bitmask to check and change the desired bit. */
    uint8_t mask = 1U << (address % NVM_PAGES_PER_WEAR_HISTORY);

    if ((nvmStaticWearWriteHistory[address / NVM_PAGES_PER_WEAR_HISTORY] & mask) == 0)
    {
      /* Flip bit. */
      nvmStaticWearWriteHistory[address / NVM_PAGES_PER_WEAR_HISTORY] |= mask;
      /* Record flip. */
      nvmStaticWearWritesInHistory++;
    }

    /* Record erase operation. */
    nvmStaticWearErasesSinceReset++;

    /* Call the static wear leveler. */
    NVM_StaticWearCheck();
  }
}

/***************************************************************************//**
 * @brief
 *   Run the static wear leveling check.
 *
 * @details
 *   The static wear leveling check is executed in this function. It uses the
 *   give threshold value to decide whether it is time to walk through the pages
 *   and move non-updated ones.
 ******************************************************************************/
static NVM_Result_t NVM_StaticWearCheck(void)
{
  /* Check if there is a check already running. We do not need more of these. */
  if (!nvmStaticWearWorking)
  {
    nvmStaticWearWorking = true;
    while (nvmStaticWearErasesSinceReset / nvmStaticWearWritesInHistory > NVM_STATIC_WEAR_THRESHOLD)
    {
      /* If all the pages have been moved in this cycle: reset. */
      if (nvmStaticWearWritesInHistory >= nvmConfig->userPages)
      {
        NVM_StaticWearReset();
        break;
      }

      /* Find an address for a page that has not been rewritten. */
      uint16_t address = 0;
      uint8_t  mask    = 1U << (address % NVM_PAGES_PER_WEAR_HISTORY);
      while ((nvmStaticWearWriteHistory[address / NVM_PAGES_PER_WEAR_HISTORY] & mask) != 0)
      {
        address++;
        mask = 1U << (address % NVM_PAGES_PER_WEAR_HISTORY);
      }

      /* Check for wear page. */
      if (nvmPageTypeWear == NVM_PageGet(address).pageType)
      {
        /* Flip bit. */
        nvmStaticWearWriteHistory[address / NVM_PAGES_PER_WEAR_HISTORY] |= mask;
        /* Record flip. */
        nvmStaticWearWritesInHistory++;
      }
      else
      {
        /* Must release write lock, run the write function to move the data,
         * then acquire lock again. */

        /* Give up write lock and open for other API operations. */
        NVM_RELEASE_WRITE_LOCK

        NVM_Write(address, NVM_WRITE_NONE_CMD);

        /* Require write lock to continue. */
        NVM_ACQUIRE_WRITE_LOCK
      }
    }
    nvmStaticWearWorking = false;
  }

  return nvmResultOk;
}

#endif

/** @endcond */

/** @} (end addtogroup NVM */
/** @} (end addtogroup EM_Drivers) */
