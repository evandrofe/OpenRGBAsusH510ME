/*---------------------------------------------------------*\
| AsusSuperIOLEDControllerDetect.cpp                        |
|                                                           |
|   Detector for the 12V RGB header of ASUS motherboards    |
|   whose LED engine is inside a Nuvoton NCT6798D Super I/O |
|                                                           |
|   <Your Name>                                 <Date>      |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "AsusSuperIOLEDController.h"
#include "DetectionManager.h"
#include "dmiinfo.h"
#include "LogManager.h"
#include "RGBController_AsusSuperIOLED.h"
#include "super_io.h"

/*---------------------------------------------------------*\
| Boards known to have this LED engine. Match is a          |
| substring of the DMI board name. Add new boards only after|
| their register behavior has been checked on hardware.     |
\*---------------------------------------------------------*/
typedef struct
{
    const char *    name;           /* Substring of the DMI board name  */
    const char *    excluded;       /* If found in the DMI name, skip   */
} asus_sio_led_board;

static const asus_sio_led_board compatible_boards[] =
{
    /*-----------------------------------------------------*\
    | PRIME H510M-E R2.0 is a different board with Aura     |
    | headers, it must not match                            |
    \*-----------------------------------------------------*/
    { "PRIME H510M-E",  "R2.0" },
};

#define ASUS_SIO_LED_NUM_BOARDS (sizeof(compatible_boards) / sizeof(compatible_boards[0]))

DetectedControllers DetectAsusSuperIOLEDControllers()
{
    DetectedControllers detected_controllers;

#ifdef _WIN32
    /*-----------------------------------------------------*\
    | Only Windows is supported for now: the register       |
    | sequences were captured from ASUS AI Suite and tested |
    | through PawnIO only                                   |
    \*-----------------------------------------------------*/
    DMIInfo         board;
    std::string     board_dmi       = board.getMainboard();
    std::string     manufacturer    = board.getManufacturer();

    if(manufacturer.find("ASUS") == std::string::npos)
    {
        return(detected_controllers);
    }

    for(unsigned int i = 0; i < ASUS_SIO_LED_NUM_BOARDS; i++)
    {
        if(board_dmi.find(compatible_boards[i].name) == std::string::npos)
        {
            continue;
        }

        if(compatible_boards[i].excluded != NULL && board_dmi.find(compatible_boards[i].excluded) != std::string::npos)
        {
            continue;
        }

        /*-------------------------------------------------*\
        | The board matches, now check that the Super I/O   |
        | really is a Nuvoton NCT6798D                      |
        \*-------------------------------------------------*/
        unsigned int chip_id = 0;

        if(!AsusSuperIOLEDController::ReadChipID(ASUS_SIO_ADDRESS, &chip_id))
        {
            LOG_INFO("[ASUS SuperIO LED] %s matched but the Super I/O could not be read", board_dmi.c_str());
            break;
        }

        if((chip_id & SIO_ID_MASK) != SIO_NCT6798_ID)
        {
            LOG_INFO("[ASUS SuperIO LED] %s matched but Super I/O ID 0x%04X is not an NCT6798D", board_dmi.c_str(), chip_id);
            break;
        }

        AsusSuperIOLEDController *      controller      = new AsusSuperIOLEDController(ASUS_SIO_ADDRESS, "ASUS " + board_dmi);
        RGBController_AsusSuperIOLED *  rgb_controller  = new RGBController_AsusSuperIOLED(controller);

        detected_controllers.push_back(rgb_controller);
        break;
    }
#endif

    return(detected_controllers);
}

REGISTER_DETECTOR("ASUS SuperIO LED", DetectAsusSuperIOLEDControllers);