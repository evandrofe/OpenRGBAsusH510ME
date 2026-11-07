/*---------------------------------------------------------*\
| AsusSuperIOLEDController.cpp                              |
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

#include <cstdio>
#include <mutex>
#include "AsusSuperIOLEDController.h"
#include "LogManager.h"
#include "super_io.h"

#ifdef _WIN32
#include <windows.h>
#endif

#define ASUS_SIO_LOG_TAG            "[ASUS SuperIO LED]"
#define ASUS_SIO_LOCK_TIMEOUT_MS    2000
#define ASUS_SIO_MIN_REPEAT_MS      250

/*---------------------------------------------------------*\
| Serializes every access to the Super I/O inside OpenRGB   |
\*---------------------------------------------------------*/
static std::mutex sio_access_mutex;

/*---------------------------------------------------------*\
| Cross-process locks.                                      |
|                                                           |
| The Super I/O is shared with fan control and monitoring,  |
| so a configuration sequence must never be interleaved     |
| with another program's. ASUS software takes these two     |
| objects around every access:                              |
|   Global\ASUS_SIO_MUTEX     auto-reset EVENT (set = free) |
|   Global\ASUS_ISABUS_MUTEX  MUTEX                         |
| The PawnIO LpcIO module also asks callers to hold         |
|   Global\Access_ISABUS.HTP.Method                         |
| If a lock cannot be taken, nothing is written.            |
\*---------------------------------------------------------*/
#ifdef _WIN32

static SECURITY_ATTRIBUTES * MakeOpenSecurityAttributes(SECURITY_DESCRIPTOR * sd, SECURITY_ATTRIBUTES * sa)
{
    /*-----------------------------------------------------*\
    | NULL DACL so that other processes (services, AI Suite)|
    | can open objects created here                         |
    \*-----------------------------------------------------*/
    if(!InitializeSecurityDescriptor(sd, SECURITY_DESCRIPTOR_REVISION))
    {
        return(NULL);
    }

    if(!SetSecurityDescriptorDacl(sd, TRUE, NULL, FALSE))
    {
        return(NULL);
    }

    sa->nLength                 = sizeof(SECURITY_ATTRIBUTES);
    sa->lpSecurityDescriptor    = sd;
    sa->bInheritHandle          = FALSE;

    return(sa);
}

static HANDLE OpenOrCreateEvent(const wchar_t * name)
{
    HANDLE h = OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, name);

    if(h == NULL)
    {
        SECURITY_DESCRIPTOR sd;
        SECURITY_ATTRIBUTES sa;

        /*-------------------------------------------------*\
        | Same as the ASUS software: auto-reset, initially  |
        | signalled (= free)                                |
        \*-------------------------------------------------*/
        h = CreateEventW(MakeOpenSecurityAttributes(&sd, &sa), FALSE, TRUE, name);
    }

    return(h);
}

static HANDLE OpenOrCreateMutex(const wchar_t * name)
{
    HANDLE h = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, name);

    if(h == NULL)
    {
        SECURITY_DESCRIPTOR sd;
        SECURITY_ATTRIBUTES sa;

        h = CreateMutexW(MakeOpenSecurityAttributes(&sd, &sa), FALSE, name);
    }

    return(h);
}

class AsusSIOBusLock
{
public:
    AsusSIOBusLock()
    {
        sio_event   = NULL;
        isa_mutex   = NULL;
        htp_mutex   = NULL;
        have_event  = false;
        have_isa    = false;
        have_htp    = false;

        sio_event = OpenOrCreateEvent(L"Global\\ASUS_SIO_MUTEX");

        if(sio_event == NULL)
        {
            LOG_WARNING("%s could not open Global\\ASUS_SIO_MUTEX (error %lu)", ASUS_SIO_LOG_TAG, GetLastError());
            return;
        }

        if(WaitForSingleObject(sio_event, ASUS_SIO_LOCK_TIMEOUT_MS) != WAIT_OBJECT_0)
        {
            LOG_WARNING("%s Global\\ASUS_SIO_MUTEX is busy", ASUS_SIO_LOG_TAG);
            return;
        }

        have_event = true;

        isa_mutex = OpenOrCreateMutex(L"Global\\ASUS_ISABUS_MUTEX");

        if(isa_mutex == NULL)
        {
            LOG_WARNING("%s could not open Global\\ASUS_ISABUS_MUTEX (error %lu)", ASUS_SIO_LOG_TAG, GetLastError());
            return;
        }

        DWORD result = WaitForSingleObject(isa_mutex, ASUS_SIO_LOCK_TIMEOUT_MS);

        if(result != WAIT_OBJECT_0 && result != WAIT_ABANDONED)
        {
            LOG_WARNING("%s Global\\ASUS_ISABUS_MUTEX is busy", ASUS_SIO_LOG_TAG);
            return;
        }

        have_isa = true;

        htp_mutex = OpenOrCreateMutex(L"Global\\Access_ISABUS.HTP.Method");

        if(htp_mutex == NULL)
        {
            LOG_WARNING("%s could not open Global\\Access_ISABUS.HTP.Method (error %lu)", ASUS_SIO_LOG_TAG, GetLastError());
            return;
        }

        result = WaitForSingleObject(htp_mutex, ASUS_SIO_LOCK_TIMEOUT_MS);

        if(result != WAIT_OBJECT_0 && result != WAIT_ABANDONED)
        {
            LOG_WARNING("%s Global\\Access_ISABUS.HTP.Method is busy", ASUS_SIO_LOG_TAG);
            return;
        }

        have_htp = true;
    }

    ~AsusSIOBusLock()
    {
        if(have_htp)    ReleaseMutex(htp_mutex);
        if(htp_mutex)   CloseHandle(htp_mutex);
        if(have_isa)    ReleaseMutex(isa_mutex);
        if(isa_mutex)   CloseHandle(isa_mutex);
        if(have_event)  SetEvent(sio_event);
        if(sio_event)   CloseHandle(sio_event);
    }

    bool Acquired() const
    {
        return(have_event && have_isa && have_htp);
    }

    AsusSIOBusLock(const AsusSIOBusLock&)               = delete;
    AsusSIOBusLock& operator=(const AsusSIOBusLock&)    = delete;

private:
    HANDLE  sio_event;
    HANDLE  isa_mutex;
    HANDLE  htp_mutex;
    bool    have_event;
    bool    have_isa;
    bool    have_htp;
};

#else

/*---------------------------------------------------------*\
| The ASUS software and the locks above only exist on       |
| Windows. This controller is only registered on Windows,   |
| the stub keeps the file compiling elsewhere.              |
\*---------------------------------------------------------*/
class AsusSIOBusLock
{
public:
    bool Acquired() const
    {
        return(true);
    }
};

#endif

/*---------------------------------------------------------*\
| Mode presets, taken from AI Suite 3 "Lighting Control"    |
| captures on a PRIME H510M-E. Every value below is exactly |
| what AI Suite wrote.                                      |
\*---------------------------------------------------------*/
typedef struct
{
    int             mode;
    unsigned char   mode_reg;           /* LDN 0x13 reg 0xFF                    */
    unsigned char   stage_flag;         /* LDN 0x13 reg 0xFC                    */
    unsigned char   p15_f3;             /* LDN 0x15 reg 0xF3                    */
    unsigned char   p15_f4;             /* LDN 0x15 reg 0xF4                    */
    unsigned char   p15_f9;             /* LDN 0x15 reg 0xF9                    */
    unsigned char   p15_fa;             /* LDN 0x15 reg 0xFA                    */
    unsigned char   speed[3];           /* LDN 0x15 reg 0xF8: slow/middle/fast  */
} asus_sio_preset;

static const asus_sio_preset presets[] =
{
    /* mode                       FF    FC    F3    F4    F9    FA    F8 slow/middle/fast */
    { ASUS_SIO_MODE_STATIC,       0x01, 0x01, 0x06, 0x05, 0x06, 0x05, { 0x10, 0x10, 0x10 } },
    { ASUS_SIO_MODE_BLINKING,     0x05, 0x01, 0x06, 0x05, 0x06, 0x05, { 0x10, 0x08, 0x01 } },
    { ASUS_SIO_MODE_STROBING,     0x07, 0x01, 0x02, 0x0F, 0xFF, 0xFF, { 0x10, 0x0A, 0x07 } },
    { ASUS_SIO_MODE_FADING,       0x07, 0x01, 0x03, 0x03, 0x06, 0x05, { 0x10, 0x10, 0x10 } },
};

#define ASUS_SIO_NUM_PRESETS    (sizeof(presets) / sizeof(presets[0]))

/*---------------------------------------------------------*\
| AI Suite's default palette for stages S1..S8, in 4-bit    |
| R, G, B. Static mode keeps it so that the sequence is     |
| byte-for-byte what AI Suite sends. Stage 1 is replaced by |
| the chosen color.                                         |
\*---------------------------------------------------------*/
static const unsigned char default_palette[8][3] =
{
    { 15,  0,  0 },     /* S1 red     */
    { 15,  6,  0 },     /* S2 orange  */
    { 15, 15,  0 },     /* S3 yellow  */
    {  0, 15,  0 },     /* S4 green   */
    {  0, 15, 15 },     /* S5 cyan    */
    {  0,  0, 15 },     /* S6 blue    */
    { 15,  0, 15 },     /* S7 magenta */
    { 15, 15, 15 },     /* S8 white   */
};

/*---------------------------------------------------------*\
| Fixed values AI Suite writes to LDN 0x15 in every mode    |
\*---------------------------------------------------------*/
static const unsigned char p15_order[12]   = { 0xF8, 0xF3, 0xF4, 0xF9, 0xFA, 0xF0, 0xF1, 0xF2, 0xF5, 0xF6, 0xF7, 0xFB };

static unsigned char To4Bit(unsigned char value)
{
    return((unsigned char)(((unsigned int)value * 15 + 127) / 255));
}

static const asus_sio_preset * FindPreset(int mode)
{
    for(unsigned int i = 0; i < ASUS_SIO_NUM_PRESETS; i++)
    {
        if(presets[i].mode == mode)
        {
            return(&presets[i]);
        }
    }

    return(NULL);
}

static void ChipEntry(int sioaddr)
{
    superio_enter(sioaddr);
}

static void ChipExit(int sioaddr)
{
    /*-----------------------------------------------------*\
    | Leave configuration mode by writing 0xAA to the index |
    | port. superio_inb writes its register argument to the |
    | index port and then reads the data port, the same     |
    | trick superio_enter uses with 0x87.                   |
    \*-----------------------------------------------------*/
    (void)superio_inb(sioaddr, ASUS_SIO_EXIT_CONFIG);
}

static void SelectLogicalDevice(int sioaddr, int ldn)
{
    superio_outb(sioaddr, ASUS_SIO_REG_LDN, ldn);
}

AsusSuperIOLEDController::AsusSuperIOLEDController(int sioaddr, std::string dev_name)
{
    sio_addr    = sioaddr;
    name        = dev_name;
    last_valid  = false;
    last_key    = 0;
}

AsusSuperIOLEDController::~AsusSuperIOLEDController()
{

}

bool AsusSuperIOLEDController::ReadChipID(int sioaddr, unsigned int * chip_id)
{
    std::lock_guard<std::mutex> guard(sio_access_mutex);
    AsusSIOBusLock              bus_lock;

    if(!bus_lock.Acquired())
    {
        return(false);
    }

    ChipEntry(sioaddr);

    int high = superio_inb(sioaddr, SIO_REG_DEVID);
    int low  = superio_inb(sioaddr, SIO_REG_DEVID + 1);

    ChipExit(sioaddr);

    if(high < 0 || low < 0)
    {
        return(false);
    }

    *chip_id = ((unsigned int)high << 8) | (unsigned int)low;

    return(true);
}

std::string AsusSuperIOLEDController::GetDeviceLocation()
{
    char hex[12];
    snprintf(hex, sizeof(hex), "0x%X", sio_addr);
    return("SIO: " + std::string(hex));
}

std::string AsusSuperIOLEDController::GetDeviceName()
{
    return(name);
}

bool AsusSuperIOLEDController::Apply(int mode, unsigned char red, unsigned char green, unsigned char blue, unsigned int speed)
{
    const asus_sio_preset * preset = FindPreset(mode);

    if(preset == NULL)
    {
        return(false);
    }

    if(speed > 2)
    {
        speed = 2;
    }

    /*-----------------------------------------------------*\
    | The hardware keeps 4 bits per channel                 |
    \*-----------------------------------------------------*/
    unsigned char chosen[3] = { To4Bit(red), To4Bit(green), To4Bit(blue) };

    /*-----------------------------------------------------*\
    | Stage colors. Static keeps AI Suite's default palette |
    | for S2..S8. The effect modes use the chosen color in  |
    | every stage so that the effect has a single color.    |
    \*-----------------------------------------------------*/
    unsigned char stage[8][3];

    for(int s = 0; s < 8; s++)
    {
        for(int c = 0; c < 3; c++)
        {
            stage[s][c] = (mode == ASUS_SIO_MODE_STATIC) ? default_palette[s][c] : chosen[c];
        }
    }

    for(int c = 0; c < 3; c++)
    {
        stage[0][c] = chosen[c];
    }

    /*-----------------------------------------------------*\
    | Two stages per byte: low nibble = odd stage (S1, S3,  |
    | S5, S7), high nibble = even stage (S2, S4, S6, S8).   |
    | Registers F0-F3 hold red, F4-F7 green, F8-FB blue.    |
    \*-----------------------------------------------------*/
    unsigned char color_reg[3][4];

    for(int c = 0; c < 3; c++)
    {
        for(int k = 0; k < 4; k++)
        {
            color_reg[c][k] = (unsigned char)((stage[2 * k + 1][c] << 4) | stage[2 * k][c]);
        }
    }

    /*-----------------------------------------------------*\
    | LDN 0x15 values                                       |
    \*-----------------------------------------------------*/
    unsigned char p15_value[12] =
    {
        preset->speed[speed],       /* F8 */
        preset->p15_f3,             /* F3 */
        preset->p15_f4,             /* F4 */
        preset->p15_f9,             /* F9 */
        preset->p15_fa,             /* FA */
        0xFF,                       /* F0 */
        0x7F,                       /* F1 */
        0x00,                       /* F2 */
        0x17,                       /* F5 */
        0x00,                       /* F6 */
        0x0F,                       /* F7 */
        0xFF,                       /* FB */
    };

    std::lock_guard<std::mutex> guard(sio_access_mutex);

    /*-----------------------------------------------------*\
    | Skip identical updates that arrive in quick succession|
    \*-----------------------------------------------------*/
    int key = (mode << 24) | (chosen[0] << 16) | (chosen[1] << 12) | (chosen[2] << 8) | (int)speed;
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

    if(last_valid && key == last_key && std::chrono::duration_cast<std::chrono::milliseconds>(now - last_time).count() < ASUS_SIO_MIN_REPEAT_MS)
    {
        return(true);
    }

    AsusSIOBusLock bus_lock;

    if(!bus_lock.Acquired())
    {
        return(false);
    }

    /*-----------------------------------------------------*\
    | Same order as AI Suite                                |
    \*-----------------------------------------------------*/
    ChipEntry(sio_addr);

    SelectLogicalDevice(sio_addr, ASUS_SIO_LDN_MISC);
    int misc = superio_inb(sio_addr, ASUS_SIO_REG_MISC);

    if(misc < 0)
    {
        ChipExit(sio_addr);
        LOG_ERROR("%s could not read LDN 0x12 reg 0xED, aborting", ASUS_SIO_LOG_TAG);
        return(false);
    }

    superio_outb(sio_addr, ASUS_SIO_REG_MISC, misc);

    SelectLogicalDevice(sio_addr, ASUS_SIO_LDN_LED);
    (void)superio_inb(sio_addr, ASUS_SIO_REG_MODE);
    superio_outb(sio_addr, ASUS_SIO_REG_MODE, preset->mode_reg);

    for(int k = 0; k < 4; k++)
    {
        for(int c = 0; c < 3; c++)
        {
            superio_outb(sio_addr, ASUS_SIO_REG_COLOR_BASE + c * 4 + k, color_reg[c][k]);
        }
    }

    superio_outb(sio_addr, ASUS_SIO_REG_STAGE_FLAG, preset->stage_flag);

    SelectLogicalDevice(sio_addr, ASUS_SIO_LDN_PARAMS);

    for(int i = 0; i < 12; i++)
    {
        superio_outb(sio_addr, p15_order[i], p15_value[i]);
    }

    /*-----------------------------------------------------*\
    | Read the colors back to make sure they were accepted  |
    \*-----------------------------------------------------*/
    bool ok = true;

    SelectLogicalDevice(sio_addr, ASUS_SIO_LDN_LED);

    for(int c = 0; c < 3 && ok; c++)
    {
        for(int k = 0; k < 4; k++)
        {
            int readback = superio_inb(sio_addr, ASUS_SIO_REG_COLOR_BASE + c * 4 + k);

            if(readback != color_reg[c][k])
            {
                LOG_WARNING("%s color register 0x%02X read back 0x%02X, expected 0x%02X", ASUS_SIO_LOG_TAG, ASUS_SIO_REG_COLOR_BASE + c * 4 + k, readback, color_reg[c][k]);
                ok = false;
                break;
            }
        }
    }

    ChipExit(sio_addr);

    last_valid  = ok;
    last_key    = key;
    last_time   = now;

    return(ok);
}

bool AsusSuperIOLEDController::SetOff()
{
    std::lock_guard<std::mutex> guard(sio_access_mutex);
    AsusSIOBusLock              bus_lock;

    if(!bus_lock.Acquired())
    {
        return(false);
    }

    ChipEntry(sio_addr);

    SelectLogicalDevice(sio_addr, ASUS_SIO_LDN_MISC);
    int misc = superio_inb(sio_addr, ASUS_SIO_REG_MISC);

    if(misc < 0)
    {
        ChipExit(sio_addr);
        LOG_ERROR("%s could not read LDN 0x12 reg 0xED, aborting", ASUS_SIO_LOG_TAG);
        return(false);
    }

    superio_outb(sio_addr, ASUS_SIO_REG_MISC, misc);

    /*-----------------------------------------------------*\
    | OFF only writes the mode register                     |
    \*-----------------------------------------------------*/
    SelectLogicalDevice(sio_addr, ASUS_SIO_LDN_LED);
    (void)superio_inb(sio_addr, ASUS_SIO_REG_MODE);
    superio_outb(sio_addr, ASUS_SIO_REG_MODE, 0x03);

    ChipExit(sio_addr);

    last_valid = false;

    return(true);
}