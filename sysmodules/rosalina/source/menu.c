/*
*   This file is part of Luma3DS
*   Copyright (C) 2016-2019 Aurora Wright, TuxSH
*
*   This program is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
*   Additional Terms 7.b and 7.c of GPLv3 apply to this file:
*       * Requiring preservation of specified reasonable legal notices or
*         author attributions in that material or in the Appropriate Legal
*         Notices displayed by works containing it.
*       * Prohibiting misrepresentation of the origin of that material,
*         or requiring that modified versions of such material be marked in
*         reasonable ways as different from the original version.
*/

#include <3ds.h>
#include "menu.h"
#include "draw.h"
#include "fmt.h"
#include "memory.h"
#include "ifile.h"
#include "menus.h"
#include "utils.h"
#include "plgloader.h"
#include "menus/n3ds.h"
#include "menus/cheats.h"
#include "minisoc.h"

u32 waitInputWithTimeout(u32 msec)
{
    bool pressedKey = false;
    u32 key = 0;
    u32 n = 0;

    //Wait for no keys to be pressed
    while(HID_PAD && !terminationRequest && (msec == 0 || n < msec))
    {
        svcSleepThread(1 * 1000 * 1000LL);
        n++;
    }

    if(terminationRequest || (msec != 0 && n >= msec))
        return 0;

    do
    {
        //Wait for a key to be pressed
        while(!HID_PAD && !terminationRequest && (msec == 0 || n < msec))
        {
            svcSleepThread(1 * 1000 * 1000LL);
            n++;
        }

        if(terminationRequest || (msec != 0 && n >= msec))
            return 0;

        key = HID_PAD;

        //Make sure it's pressed
        for(u32 i = 0x26000; i > 0; i --)
        {
            if(key != HID_PAD) break;
            if(i == 1) pressedKey = true;
        }
    }
    while(!pressedKey);

    return key;
}

u32 waitInput(void)
{
    return waitInputWithTimeout(0);
}

u32 waitComboWithTimeout(u32 msec)
{
    u32 key = 0;
    u32 n = 0;

    //Wait for no keys to be pressed
    while(HID_PAD && !terminationRequest && (msec == 0 || n < msec))
    {
        svcSleepThread(1 * 1000 * 1000LL);
        n++;
    }

    if(terminationRequest || (msec != 0 && n >= msec))
        return 0;

    do
    {
        svcSleepThread(1 * 1000 * 1000LL);
        n++;

        u32 tempKey = HID_PAD;

        for(u32 i = 0x26000; i > 0; i--)
        {
            if(tempKey != HID_PAD) break;
            if(i == 1) key = tempKey;
        }
    }
    while((!key || HID_PAD) && !terminationRequest && (msec == 0 || n < msec));

    if(terminationRequest || (msec != 0 && n >= msec))
        return 0;

    return key;
}

u32 waitCombo(void)
{
    return waitComboWithTimeout(0);
}

static MyThread menuThread;
static u8 ALIGN(8) menuThreadStack[0x3000];
static u8 batteryLevel = 255;
static u32 homeBtnPressed = 0;

MyThread *menuCreateThread(void)
{
    svcKernelSetState(0x10007, &homeBtnPressed);
    if(R_FAILED(MyThread_Create(&menuThread, menuThreadMain, menuThreadStack, 0x3000, 52, CORE_SYSTEM)))
        svcBreak(USERBREAK_PANIC);
    return &menuThread;
}

extern bool isN3DS;
u32 menuCombo;

u32     DispWarningOnHome(void);

void    menuThreadMain(void)
{
    if(!isN3DS)
    {
        rosalinaMenu.nbItems--;
        for(u32 i = 0; i <= rosalinaMenu.nbItems; i++)
            rosalinaMenu.items[i] = rosalinaMenu.items[i+1];
    }
    else
        N3DSMenu_UpdateStatus();

    bool isAcURegistered = false;

    while(!terminationRequest)
    {
        if((HID_PAD & menuCombo) == menuCombo)
        {
            if (!isAcURegistered)
                isAcURegistered = R_SUCCEEDED(srvIsServiceRegistered(&isAcURegistered, "ac:u"))
                    && isAcURegistered;

            else
            {
                menuEnter();
                if(isN3DS) N3DSMenu_UpdateStatus();
                PluginLoader__UpdateMenu();
                menuShow(&rosalinaMenu);
                menuLeave();
            }
        }
        else
        {
            Cheat_ApplyCheats();
        }

        // Check for home button on O3DS Mode3 with plugin loaded
        if (homeBtnPressed != 0)
        {
            if (DispWarningOnHome())
                svcKernelSetState(7); ///< reboot is fine since exiting a mode3 game reboot anyway

            homeBtnPressed = 0;
        }

        svcSleepThread(50 * 1000 * 1000LL);
    }
}

static s32 menuRefCount = 0;
void menuEnter(void)
{
    if(AtomicPostIncrement(&menuRefCount) == 0)
    {
        svcKernelSetState(0x10000, 1);
        svcSleepThread(5 * 1000 * 100LL);
        Draw_SetupFramebuffer();
        Draw_ClearFramebuffer();
    }
}

void menuLeave(void)
{
    svcSleepThread(50 * 1000 * 1000);

    if(AtomicDecrement(&menuRefCount) == 0)
    {
        Draw_Lock();
        Draw_FlushFramebuffer();
        Draw_RestoreFramebuffer();
        Draw_Unlock();
        svcKernelSetState(0x10000, 1);
    }
}

static void menuDraw(Menu *menu, u32 selected)
{
    char versionString[16];
    s64 out;
    u32 version, commitHash;
    bool isRelease;
    bool isMcuHwcRegistered;

    if(R_SUCCEEDED(srvIsServiceRegistered(&isMcuHwcRegistered, "mcu::HWC")) && isMcuHwcRegistered && R_SUCCEEDED(mcuHwcInit()))
    {
        if(R_FAILED(MCUHWC_GetBatteryLevel(&batteryLevel)))
            batteryLevel = 255;
        mcuHwcExit();
    }
    else
        batteryLevel = 255;

    svcGetSystemInfo(&out, 0x10000, 0);
    version = (u32)out;

    svcGetSystemInfo(&out, 0x10000, 1);
    commitHash = (u32)out;

    svcGetSystemInfo(&out, 0x10000, 0x200);
    isRelease = (bool)out;

    if(GET_VERSION_REVISION(version) == 0)
        sprintf(versionString, "v%lu.%lu", GET_VERSION_MAJOR(version), GET_VERSION_MINOR(version));
    else
        sprintf(versionString, "v%lu.%lu.%lu", GET_VERSION_MAJOR(version), GET_VERSION_MINOR(version), GET_VERSION_REVISION(version));

    Draw_DrawString(10, 10, COLOR_TITLE, menu->title);

    for(u32 i = 0; i < 15; i++)
    {
        if(i >= menu->nbItems)
            break;
        Draw_DrawString(30, 30 + i * SPACING_Y, COLOR_WHITE, menu->items[i].title);
        Draw_DrawCharacter(10, 30 + i * SPACING_Y, COLOR_TITLE, i == selected ? '>' : ' ');
    }

    if(miniSocEnabled)
    {
        char ipBuffer[17];
        u32 ip = gethostid();
        u8 *addr = (u8 *)&ip;
        int n = sprintf(ipBuffer, "%hhu.%hhu.%hhu.%hhu", addr[0], addr[1], addr[2], addr[3]);
        Draw_DrawString(SCREEN_BOT_WIDTH - 10 - SPACING_X * n, 10, COLOR_WHITE, ipBuffer);
    }

    Draw_DrawFormattedString(SCREEN_BOT_WIDTH - 10 - 4 * SPACING_X, SCREEN_BOT_HEIGHT - 20, COLOR_WHITE, "    ");

    if(batteryLevel != 255)
        Draw_DrawFormattedString(SCREEN_BOT_WIDTH - 10 - 4 * SPACING_X, SCREEN_BOT_HEIGHT - 20, COLOR_WHITE, "%02hhu%%", batteryLevel);
    else
        Draw_DrawString(SCREEN_BOT_WIDTH - 10 - 4 * SPACING_X, SCREEN_BOT_HEIGHT - 20, COLOR_WHITE, "    ");

    if(isRelease)
        Draw_DrawFormattedString(10, SCREEN_BOT_HEIGHT - 20, COLOR_TITLE, "Luma3DS %s", versionString);
    else
        Draw_DrawFormattedString(10, SCREEN_BOT_HEIGHT - 20, COLOR_TITLE, "Luma3DS %s-%08lx", versionString, commitHash);

    Draw_FlushFramebuffer();
}

void menuShow(Menu *root)
{
    u32 selectedItem = 0;
    Menu *currentMenu = root;
    u32 nbPreviousMenus = 0;
    Menu *previousMenus[0x80];
    u32 previousSelectedItems[0x80];

    Draw_Lock();
    Draw_ClearFramebuffer();
    Draw_FlushFramebuffer();
    menuDraw(currentMenu, selectedItem);
    Draw_Unlock();

    do
    {
        u32 pressed = waitInputWithTimeout(1000);

        if(pressed & BUTTON_A)
        {
            Draw_Lock();
            Draw_ClearFramebuffer();
            Draw_FlushFramebuffer();
            Draw_Unlock();

            switch(currentMenu->items[selectedItem].action_type)
            {
                case METHOD:
                    if(currentMenu->items[selectedItem].method != NULL)
                        currentMenu->items[selectedItem].method();
                    break;
                case MENU:
                    previousSelectedItems[nbPreviousMenus] = selectedItem;
                    previousMenus[nbPreviousMenus++] = currentMenu;
                    currentMenu = currentMenu->items[selectedItem].menu;
                    selectedItem = 0;
                    break;
            }

            Draw_Lock();
            Draw_ClearFramebuffer();
            Draw_FlushFramebuffer();
            Draw_Unlock();
        }
        else if(pressed & BUTTON_B)
        {
            Draw_Lock();
            Draw_ClearFramebuffer();
            Draw_FlushFramebuffer();
            Draw_Unlock();

            if(nbPreviousMenus > 0)
            {
                currentMenu = previousMenus[--nbPreviousMenus];
                selectedItem = previousSelectedItems[nbPreviousMenus];
            }
            else
                break;
        }
        else if(pressed & BUTTON_DOWN)
        {
            if(++selectedItem >= currentMenu->nbItems)
                selectedItem = 0;
        }
        else if(pressed & BUTTON_UP)
        {
            if(selectedItem-- <= 0)
                selectedItem = currentMenu->nbItems - 1;
        }

        Draw_Lock();
        menuDraw(currentMenu, selectedItem);
        Draw_Unlock();
    }
    while(!terminationRequest);
}

static const char *__press_b_to_close = "Press [B] to close";

void    DispMessage(const char *title, const char *message)
{
    menuEnter();

    Draw_Lock();
    Draw_ClearFramebuffer();
    Draw_FlushFramebuffer();

    Draw_DrawString(10, 10, COLOR_TITLE, title);

    Draw_DrawString(30, 30, COLOR_WHITE, message);
    Draw_DrawString(200, 220, COLOR_TITLE, __press_b_to_close);


    u32 keys = 0;

    do
    {
        keys = waitComboWithTimeout(1000);
    }while (!terminationRequest && !(keys & BUTTON_B));

    Draw_Unlock(); ///< Keep it locked until we exit the message
    menuLeave();
}

u32    DispErrMessage(const char *title, const char *message, const Result error)
{
    char buf[100];

    sprintf(buf, "Error code: 0x%08X", (unsigned int)error);
    menuEnter();

    Draw_Lock();
    Draw_ClearFramebuffer();
    Draw_FlushFramebuffer();

    Draw_DrawString(10, 10, COLOR_TITLE, title);

    u32 posY = Draw_DrawString(30, 30, COLOR_WHITE, message);
    Draw_DrawString(30, posY + 20, COLOR_RED, buf);
    Draw_DrawString(200, 220, COLOR_TITLE, __press_b_to_close);

    u32 keys = 0;

    do
    {
        keys = waitComboWithTimeout(1000);
    }while (!terminationRequest && !(keys & BUTTON_B));

    Draw_Unlock();  ///< Keep it locked until we exit the message
    menuLeave();
    return error;
}

u32     DispWarningOnHome(void)
{
    menuEnter();

    Draw_Lock();
    Draw_ClearFramebuffer();
    Draw_FlushFramebuffer();

    Draw_DrawString(10, 10, COLOR_TITLE, "Warning");

    u32 posY = Draw_DrawString(30, 40, COLOR_WHITE, "Due to memory shortage the home button\nis disabled.");
    Draw_DrawString(30, posY + 20, COLOR_WHITE, "Press [DPAD UP + B] to exit the application.");
    Draw_DrawString(200, 220, COLOR_TITLE, __press_b_to_close);


    u32 keys = 0;

    do
    {
        keys = waitComboWithTimeout(1000);
    }while (!terminationRequest && !(keys & BUTTON_B));

    Draw_Unlock(); ///< Keep it locked until we exit the message
    menuLeave();

    return (keys & BUTTON_UP) > 0;
}


typedef char string[50];
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

void    DisplayPluginMenu(u32   *cmdbuf)
{
    u32             cursor = 0;
    u32             nbItems = cmdbuf[1];
    u8              *states = (u8 *)cmdbuf[3];
    char            buffer[60];
    const char      *title = (const char *)cmdbuf[5];
    const string    *items = (const string *)cmdbuf[7];
    const string    *hints = (const string *)cmdbuf[9];

    menuEnter();
    Draw_Lock();

    do
    {
        // Draw the menu
        {
            // Clear screen
            Draw_ClearFramebuffer();
            Draw_FlushFramebuffer();

            // Draw title
            Draw_DrawString(10, 10, COLOR_TITLE, title);

            // Draw items
            u32 i = MAX(0, (int)cursor - 7);
            u32 end = MIN(nbItems, i + 16);
            u32 posY = 30;

            for (; i < end; ++i, posY += 10)
            {
                sprintf(buffer, "[ ] %s", items[i]);
                Draw_DrawString(30, posY, COLOR_WHITE, buffer);

                if (i == cursor) Draw_DrawCharacter(10, posY, COLOR_TITLE, '>');
                if (states[i]) Draw_DrawCharacter(36, posY, COLOR_LIME, 'x');
            }

            // Draw hint
            if (hints[cursor])
                Draw_DrawString(10, 200, COLOR_TITLE, hints[cursor]);
        }

        // Wait for input
        u32 pressed = waitInput();

        if (pressed & BUTTON_A)
            states[cursor] = !states[cursor];

        if (pressed & BUTTON_B)
            break;

        if (pressed & BUTTON_DOWN)
            if (++cursor >= nbItems)
                cursor = 0;

        if (pressed & BUTTON_UP)
            if (--cursor >= nbItems)
                cursor = nbItems - 1;

    } while (true);

    Draw_Unlock();
    menuLeave();
}