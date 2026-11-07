/*---------------------------------------------------------*\
| RGBController_AsusSuperIOLED.h                            |
|                                                           |
|   RGBController for the 12V RGB header of ASUS            |
|   motherboards controlled through a Nuvoton NCT6798D      |
|   Super I/O                                               |
|                                                           |
|   <Your Name>                                 <Date>      |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include "RGBController.h"
#include "AsusSuperIOLEDController.h"

class RGBController_AsusSuperIOLED : public RGBController
{
public:
    RGBController_AsusSuperIOLED(AsusSuperIOLEDController * controller_ptr);
    ~RGBController_AsusSuperIOLED();

    void        SetupZones();

    void        DeviceUpdateLEDs();
    void        DeviceUpdateZoneLEDs(int zone);
    void        DeviceUpdateSingleLED(int led);

    void        DeviceUpdateMode();

private:
    AsusSuperIOLEDController *  controller;

    void        ApplyActiveMode();
};