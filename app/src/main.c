
#include <stdio.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>

#define LOG_LEVEL CONFIG_LOG_DBG_LEVEL
#include <zephyr/logging/log.h>

# include "ubxlib.h"

// Bring in the application settings
# include "u_cfg_app_platform_specific.h"

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

// Optionally, define a user context which you can use to pass
// information to a callback that does not have a dedicated
// void * callback parameter (this is the case with
// uGnssPosGetStreamedStart()).
typedef struct {
    char *pSomething;
    int32_t somethingElse;
} MyContext_t;

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

// ZEPHYR USERS may prefer to set the device and network
// configuration from their device tree, rather than in this C
// code: see /port/platform/zephyr/README.md for instructions on
// how to do that.

// GNSS configuration.
// Set U_CFG_TEST_GNSS_MODULE_TYPE to your module type,
// chosen from the values in gnss/api/u_gnss_module_type.h
//
// Note that the pin numbers are those of the MCU: if you
// are using an MCU inside a u-blox module the IO pin numbering
// for the module is likely different to that of the MCU: check
// the data sheet for the module to determine the mapping.

// Count of the number of position fixes received
static size_t gPositionCount = 0;

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS
 * -------------------------------------------------------------- */

// Convert a lat/long into a whole number and a bit-after-the-decimal-point
// that can be printed by a version of printf() that does not support
// floating point operations, returning the prefix (either "+" or "-").
// The result should be printed with printf() format specifiers
// %c%d.%07d, e.g. something like:
//
// int32_t whole;
// int32_t fraction;
//
// printf("%c%d.%07d/%c%d.%07d", latLongToBits(latitudeX1e7, &whole, &fraction),
//                               whole, fraction,
//                               latLongToBits(longitudeX1e7, &whole, &fraction),
//                               whole, fraction);
static char latLongToBits(int32_t thingX1e7,
                          int32_t *pWhole,
                          int32_t *pFraction)
{
    char prefix = '+';

    // Deal with the sign
    if (thingX1e7 < 0) {
        thingX1e7 = -thingX1e7;
        prefix = '-';
    }
    *pWhole = thingX1e7 / 10000000;
    *pFraction = thingX1e7 % 10000000;

    return prefix;
}

// Callback for position reception.
static void callback(uDeviceHandle_t gnssHandle,
                     int32_t errorCode,
                     int32_t latitudeX1e7,
                     int32_t longitudeX1e7,
                     int32_t altitudeMillimetres,
                     int32_t radiusMillimetres,
                     int32_t speedMillimetresPerSecond,
                     int32_t svs,
                     int64_t timeUtc)
{
    char prefix[2] = {0};
    int32_t whole[2] = {0};
    int32_t fraction[2] = {0};

    // Pick up our user context
    MyContext_t *pContext = (MyContext_t *) pUDeviceGetUserContext(gnssHandle);

    // Not using these, just keep the compiler happy
    (void) gnssHandle;
    (void) altitudeMillimetres;
    (void) radiusMillimetres;
    (void) speedMillimetresPerSecond;
    (void) svs;
    (void) timeUtc;
    // We don't actually use the user context here, but
    // of course _you_ could
    (void) pContext;

    if (errorCode == 0) {
        prefix[0] = latLongToBits(longitudeX1e7, &(whole[0]), &(fraction[0]));
        prefix[1] = latLongToBits(latitudeX1e7, &(whole[1]), &(fraction[1]));
        uPortLog("I am here: https://maps.google.com/?q=%c%d.%07d,%c%d.%07d\n",
                 prefix[1], whole[1], fraction[1], prefix[0], whole[0], fraction[0]);
        gPositionCount++;
    }
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS: THE EXAMPLE
 * -------------------------------------------------------------- */

// The entry point, main(): before this is called the system
// clocks must have been started and the RTOS must be running;
// we are in task space.
int main(void)
{
    uDeviceHandle_t devHandle = NULL;
    int32_t returnCode;
    int32_t guardCount = 0;
    MyContext_t context = {0};

    // Initialise the APIs we will need
    uPortInit();
    uPortI2cInit(); // You only need this if an I2C interface is used
    uPortSpiInit(); // You only need this if an SPI interface is used
    uDeviceInit();

    // Open the device
    returnCode = uDeviceOpen(NULL, &devHandle);
    uPortLog("Opened device with return code %d.\n", returnCode);

    if (returnCode == 0) {
        // Since we are not using the common APIs we do not need
        // to call uNetworkInteraceUp()/uNetworkInteraceDown().

        // If you need to pass context data to
        // uGnssPosGetStreamedStart(), which does not include
        // a user parameter in its function signature, set
        // a user context for ourselves in the device, which
        // we can pick up in callback()
        uDeviceSetUserContext(devHandle, (void *) &context);

        // Start to get position
        uPortLog("Starting position stream.\n");
        returnCode = uGnssPosGetStreamedStart(devHandle,
                                              U_GNSS_POS_STREAMED_PERIOD_DEFAULT_MS,
                                              callback);
        if (returnCode == 0) {

            uPortLog("Waiting up to 60 seconds for 5 position fixes.\n");
            while ((gPositionCount < 5) && (guardCount < 60)) {
                uPortTaskBlock(1000);
                guardCount++;
            }
            // Stop getting position
            uGnssPosGetStreamedStop(devHandle);

        } else {
            uPortLog("Unable to start position stream!\n");
        }

        // Close the device
        // Note: we don't power the device down here in order
        // to speed up testing; you may prefer to power it off
        // by setting the second parameter to true.
        uDeviceClose(devHandle, false);

    } else {
        uPortLog("Unable to open GNSS!\n");
    }

    // Tidy up
    uDeviceDeinit();
    uPortSpiDeinit(); // You only need this if an SPI interface is used
    uPortI2cDeinit(); // You only need this if an I2C interface is used
    uPortDeinit();

    uPortLog("Done.\n");

#if ((U_CFG_APP_GNSS_UART >= 0) || (U_CFG_APP_GNSS_I2C >= 0) || (U_CFG_APP_GNSS_SPI >= 0))
    // For u-blox internal testing only
    EXAMPLE_FINAL_STATE(((gPositionCount > 0) && (returnCode == 0)) ||
                        (returnCode == U_ERROR_COMMON_NOT_SUPPORTED));
# endif
