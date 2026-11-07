/*---------------------------------------------------------*\
| RGBController_AsusSuperIOLED.cpp                          |
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

#include "RGBController_AsusSuperIOLED.h"

/**------------------------------------------------------------------*\
    @name ASUS SuperIO LED
    @category Motherboard
    @type SuperIO
    @save :x:
    @direct :x:
    @effects :warning:
    @detectors DetectAsusSuperIOLEDControllers
    @comment
        ASUS boards such as the PRIME H510M-E have one 12V 4-pin RGB header and no Aura
        controller. ASUS AI Suite 3 ("Lighting Control") drives it through the Nuvoton
        NCT6798D Super I/O (ports 0x2E/0x2F, logical devices 0x12, 0x13 and 0x15).

        Before assuming a board is supported, check that it has no USB device with VID 0B05
        and no ENE/Aura chip answering on the SMBus. OpenRGB's existing Aura detectors
        should find those boards.

        Colors have 4 bits per channel (4096 colors) and there is no brightness register.
        There is no direct mode: every update re-writes the full configuration, so only
        Static and Off are enabled by default. Blinking, Strobing and Fading (build option
        ASUS_SIO_LED_ENABLE_EFFECTS) were captured from AI Suite but not verified on hardware.

        The Super I/O is shared with fan control. The controller takes the same
        cross-process locks as ASUS software and does nothing if it cannot get them.
\*-------------------------------------------------------------------*/

RGBController_AsusSuperIOLED::RGBController_AsusSuperIOLED(AsusSuperIOLEDController * controller_ptr)
{
    controller              = controller_ptr;

    name                    = controller->GetDeviceName();
    vendor                  = "ASUS";
    type                    = DEVICE_TYPE_MOTHERBOARD;
    description             = "ASUS Super I/O 12V RGB Header";
    location                = controller->GetDeviceLocation();

    mode Static;
    Static.name             = "Static";
    Static.value            = ASUS_SIO_MODE_STATIC;
    Static.flags            = MODE_FLAG_HAS_PER_LED_COLOR;
    Static.color_mode       = MODE_COLORS_PER_LED;
    modes.push_back(Static);

#if ASUS_SIO_LED_ENABLE_EFFECTS
    mode Blinking;
    Blinking.name           = "Blinking";
    Blinking.value          = ASUS_SIO_MODE_BLINKING;
    Blinking.flags          = MODE_FLAG_HAS_PER_LED_COLOR | MODE_FLAG_HAS_SPEED;
    Blinking.speed_min      = 0;    /* Slow   */
    Blinking.speed_max      = 2;    /* Fast   */
    Blinking.speed          = 0;
    Blinking.color_mode     = MODE_COLORS_PER_LED;
    modes.push_back(Blinking);

    mode Strobing;
    Strobing.name           = "Strobing";
    Strobing.value          = ASUS_SIO_MODE_STROBING;
    Strobing.flags          = MODE_FLAG_HAS_PER_LED_COLOR | MODE_FLAG_HAS_SPEED;
    Strobing.speed_min      = 0;
    Strobing.speed_max      = 2;
    Strobing.speed          = 0;
    Strobing.color_mode     = MODE_COLORS_PER_LED;
    modes.push_back(Strobing);

    mode Fading;
    Fading.name             = "Fading";
    Fading.value            = ASUS_SIO_MODE_FADING;
    Fading.flags            = MODE_FLAG_HAS_PER_LED_COLOR;
    Fading.color_mode       = MODE_COLORS_PER_LED;
    modes.push_back(Fading);
#endif

    mode Off;
    Off.name                = "Off";
    Off.value               = ASUS_SIO_MODE_OFF;
    Off.flags               = 0;
    Off.color_mode          = MODE_COLORS_NONE;
    modes.push_back(Off);

    SetupZones();
}

RGBController_AsusSuperIOLED::~RGBController_AsusSuperIOLED()
{
    Shutdown();

    delete controller;
}

void RGBController_AsusSuperIOLED::SetupZones()
{
    zone asus_zone;
    asus_zone.name          = "12V RGB Header";
    asus_zone.type          = ZONE_TYPE_SINGLE;
    asus_zone.leds_min      = 1;
    asus_zone.leds_max      = 1;
    asus_zone.leds_count    = 1;
    zones.push_back(asus_zone);

    led asus_led;
    asus_led.name           = "RGB Header";
    leds.push_back(asus_led);

    SetupColors();
}

void RGBController_AsusSuperIOLED::ApplyActiveMode()
{
    unsigned int  speed = modes[active_mode].speed;
    RGBColor      color = colors[0];

    controller->Apply(modes[active_mode].value, RGBGetRValue(color), RGBGetGValue(color), RGBGetBValue(color), speed);
}

void RGBController_AsusSuperIOLED::DeviceUpdateLEDs()
{
    /*-----------------------------------------------------*\
    | Off has no color to send                              |
    \*-----------------------------------------------------*/
    if(modes[active_mode].value == ASUS_SIO_MODE_OFF)
    {
        return;
    }

    ApplyActiveMode();
}

void RGBController_AsusSuperIOLED::DeviceUpdateZoneLEDs(int /*zone*/)
{
    DeviceUpdateLEDs();
}

void RGBController_AsusSuperIOLED::DeviceUpdateSingleLED(int /*led*/)
{
    DeviceUpdateLEDs();
}

void RGBController_AsusSuperIOLED::DeviceUpdateMode()
{
    if(modes[active_mode].value == ASUS_SIO_MODE_OFF)
    {
        controller->SetOff();
    }
    else
    {
        ApplyActiveMode();
    }
}