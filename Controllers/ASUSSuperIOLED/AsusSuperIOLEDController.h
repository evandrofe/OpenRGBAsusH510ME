/*---------------------------------------------------------*\
| AsusSuperIOLEDController.h                                |
|                                                           |
|   Driver for the 12V RGB header of ASUS motherboards      |
|   whose LED engine is inside a Nuvoton NCT6798D Super I/O |
|   (no Aura chip), e.g. PRIME H510M-E                      |
|                                                           |
|   <Your Name>                                 <Date>      |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <chrono>
#include <string>

/*---------------------------------------------------------*\
| Set to 1 to also expose the Blinking, Strobing and Fading |
| modes. Their register values were captured from ASUS AI   |
| Suite but have NOT been replayed on hardware yet.         |
\*---------------------------------------------------------*/
#ifndef ASUS_SIO_LED_ENABLE_EFFECTS
#define ASUS_SIO_LED_ENABLE_EFFECTS 0
#endif

enum
{
    /*-----------------------------------------------------*\
    | Super I/O configuration port pair 0x2E / 0x2F         |
    \*-----------------------------------------------------*/
    ASUS_SIO_ADDRESS                = 0x2E,
    ASUS_SIO_EXIT_CONFIG            = 0xAA,     /* Written to the index port to leave config mode */

    /*-----------------------------------------------------*\
    | Logical device selection                              |
    \*-----------------------------------------------------*/
    ASUS_SIO_REG_LDN                = 0x07,
    ASUS_SIO_LDN_MISC               = 0x12,
    ASUS_SIO_LDN_LED                = 0x13,
    ASUS_SIO_LDN_PARAMS             = 0x15,

    /*-----------------------------------------------------*\
    | Registers                                             |
    \*-----------------------------------------------------*/
    ASUS_SIO_REG_MISC               = 0xED,     /* LDN 0x12: read and written back unchanged      */
    ASUS_SIO_REG_MODE               = 0xFF,     /* LDN 0x13: mode code                            */
    ASUS_SIO_REG_STAGE_FLAG         = 0xFC,     /* LDN 0x13: 0x01 in every captured mode but      */
                                                /*           multi-stage fading, where it is 0x00 */
    ASUS_SIO_REG_COLOR_BASE         = 0xF0,     /* LDN 0x13: F0-F3 red, F4-F7 green, F8-FB blue   */
                                                /*           one nibble per stage, 8 stages       */
    ASUS_SIO_REG_SPEED              = 0xF8,     /* LDN 0x15: period, lower value = faster         */
};

enum
{
    ASUS_SIO_MODE_OFF               = 0,
    ASUS_SIO_MODE_STATIC            = 1,
    ASUS_SIO_MODE_BLINKING          = 2,
    ASUS_SIO_MODE_STROBING          = 3,
    ASUS_SIO_MODE_FADING            = 4,
};

class AsusSuperIOLEDController
{
public:
    AsusSuperIOLEDController(int sioaddr, std::string dev_name);
    ~AsusSuperIOLEDController();

    /*-----------------------------------------------------*\
    | Reads the Super I/O chip ID (registers 0x20/0x21).    |
    | Returns false if the chip could not be read.          |
    \*-----------------------------------------------------*/
    static bool     ReadChipID(int sioaddr, unsigned int * chip_id);

    std::string     GetDeviceLocation();
    std::string     GetDeviceName();

    /*-----------------------------------------------------*\
    | red/green/blue are 8-bit, the hardware keeps 4 bits   |
    | per channel. speed is 0 (slow), 1 (middle) or 2 (fast)|
    \*-----------------------------------------------------*/
    bool            Apply(int mode, unsigned char red, unsigned char green, unsigned char blue, unsigned int speed);
    bool            SetOff();

private:
    int             sio_addr;
    std::string     name;

    bool            last_valid;
    int             last_key;
    std::chrono::steady_clock::time_point last_time;
};