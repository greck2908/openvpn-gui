/*
 *  OpenVPN-GUI -- A Windows GUI for OpenVPN.
 *
 *  Copyright (C) 2004 Mathias Sundman <mathias@nilings.se>
 *                2010 Heiko Hund <heikoh@users.sf.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program (see the file COPYING included with this
 *  distribution); if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <windows.h>
#include <shellapi.h>
#include <tchar.h>
#include <time.h>
#include <ras.h>
#include <raserror.h>

#include "tray.h"
#include "service.h"
#include "main.h"
#include "options.h"
#include "openvpn.h"
#include "openvpn_config.h"
#include "openvpn-gui-res.h"
#include "localization.h"

/* Popup Menus */
HMENU hMenu;
HMENU hMenuConn[MAX_CONFIGS];
HMENU hMenuService;

NOTIFYICONDATA ni;
extern options_t o;

static const TCHAR VpnEntryPrefix[] = _T("OpenVPN ");
static const TCHAR WindowCaption[] = _T("OpenVPN-GUI");

/* Delete OpenVPN entries from Windows networking/VPN flyout */
void
ClearNetworkFlyout()
{
    DWORD result = ERROR_SUCCESS;
    LPRASENTRYNAME entryName = NULL;
    DWORD entryNameSize = 0;
    DWORD entryCount = 0;

    /* Determine memory required */
    result = RasEnumEntries(NULL, NULL, NULL, &entryNameSize, &entryCount);
    if (ERROR_BUFFER_TOO_SMALL != result)
    {
        /* Anything else is unexpected. */
        MessageBox(NULL, _T("ClearNetworkFlyout: Could not get memory needed for RAS entries"), WindowCaption, MB_OK);
        return;
    }

    entryName = (LPRASENTRYNAME)malloc(entryNameSize);
    if (NULL == entryName)
    {
        /* No memory! */
        MessageBox(NULL, _T("ClearNetworkFlyout: No memory allocating entry name"), WindowCaption, MB_OK);
        return;
    }

    entryName[0].dwSize = sizeof(RASENTRYNAME);

    result = RasEnumEntries(NULL, NULL, entryName, &entryNameSize, &entryCount);
    if (ERROR_SUCCESS != result)
    {
        MessageBox(NULL, _T("ClearNetworkFlyout: Could not enumerate RAS entries"), WindowCaption, MB_OK);
        free(entryName);
        return;
    }

    for (DWORD i = 0; i < entryCount; i++)
    {
        if ((entryName[i].dwFlags == REN_User) &&
            (0 == _tcsncmp(VpnEntryPrefix, entryName[i].szEntryName, _countof(VpnEntryPrefix) - 1)))
        {
            /* TODO: Retrieve the entry and verify that it references our custom dialer DLL? RasEnumEntries doesn't
             * give us this information but we could retrieve the entry and check. This would keep us from purging any
             * entries which for some inexplicable reason started with the VpnEntryPrefix string.
             */
            result = RasDeleteEntry(NULL, entryName[i].szEntryName);
            if (ERROR_SUCCESS != result)
            {
                MessageBox(NULL, _T("ClearNetworkFlyout: Failed to delete entry"), WindowCaption, MB_OK);
                /* Continue processing; don't return. */
            }
        }
    }

    free(entryName);

    return;
}

static const TCHAR DialerFileName[] = _T("libopenvpndialer-0.dll");
static const TCHAR DeviceName[] = _T("TAP-Windows Adapter V9");

/* Create VPN flyout entry for a new connection */
static void
CreateNetworkFlyoutEntry(const connection_t *c, int number)
{
    DWORD result = ERROR_SUCCESS;
    RASENTRY entry;
    TCHAR entryName[RAS_MaxEntryName + 1];

    memset(&entry, 0, sizeof(entry));
    entry.dwSize = sizeof(entry);
    _tcsncpy(entry.szDeviceType, RASDT_Vpn, _countof(RASDT_Vpn));
    _tcsncpy(entry.szDeviceName, DeviceName, _countof(DeviceName));

    entry.dwfOptions = RASEO_Custom;
    /* Take the exe_path, strip off everything after the last backslash, and append DialerFileName */
    TCHAR* lastBackslash = _tcsrchr(o.exe_path, _T('\\'));
    if (NULL == lastBackslash)
    {
        MessageBox(NULL, _T("CreateNetworkFlyoutEntry: No last backslash found"), WindowCaption, MB_OK);
        return;
    }
    _tcsncpy(entry.szCustomDialDll, o.exe_path, (lastBackslash - o.exe_path + 1));
    /* Check for possible overrun */
    if (_tcslen(entry.szCustomDialDll) + _countof(DialerFileName) >= _countof(entry.szCustomDialDll))
    {
        MessageBox(NULL, _T("CreateNetworkFlyoutEntry: Not enough space for custom dialer DLL filename"), WindowCaption, MB_OK);
        return;
    }
    _tcsncat(entry.szCustomDialDll, DialerFileName, _countof(DialerFileName));

    /* Use the local phone number space to store the entry number for the custom dialer to use. */
    _sntprintf_0(entry.szLocalPhoneNumber, _T("%d"), number);
    entry.dwType = RASET_Vpn;
    entry.dwEncryptionType = ET_Optional; 

    _sntprintf_0(entryName, _T("%s%s"), VpnEntryPrefix, c->config_name);
    result = RasSetEntryProperties(NULL, entryName, &entry, sizeof(entry), NULL, 0);
    if (ERROR_SUCCESS != result)
    {
        TCHAR message[512];
        _sntprintf_0(message, _T("CreateNetworkFlyoutEntry: Could not RasSetEntryProperties: %lu"), result);
        MessageBox(NULL, message, WindowCaption, MB_OK);
        return;
    }
}

/* Create popup menus */
void
CreatePopupMenus()
{
    int i;
    for (i = 0; i < o.num_configs; i++)
        hMenuConn[i] = CreatePopupMenu();

    hMenuService = CreatePopupMenu();
    hMenu = CreatePopupMenu();

    if (o.num_configs == 1) {
        /* Create Main menu with actions */
        if (o.service_only == 0) {
            AppendMenu(hMenu, MF_STRING, IDM_CONNECTMENU, LoadLocalizedString(IDS_MENU_CONNECT));
            AppendMenu(hMenu, MF_STRING, IDM_DISCONNECTMENU, LoadLocalizedString(IDS_MENU_DISCONNECT));
            AppendMenu(hMenu, MF_STRING, IDM_STATUSMENU, LoadLocalizedString(IDS_MENU_STATUS));
            AppendMenu(hMenu, MF_SEPARATOR, 0, 0);
        }
        else {
            AppendMenu(hMenu, MF_STRING, IDM_SERVICE_START, LoadLocalizedString(IDS_MENU_SERVICEONLY_START));
            AppendMenu(hMenu, MF_STRING, IDM_SERVICE_STOP, LoadLocalizedString(IDS_MENU_SERVICEONLY_STOP));
            AppendMenu(hMenu, MF_STRING, IDM_SERVICE_RESTART, LoadLocalizedString(IDS_MENU_SERVICEONLY_RESTART));
            AppendMenu(hMenu, MF_SEPARATOR, 0, 0);
        }

        AppendMenu(hMenu, MF_STRING, IDM_VIEWLOGMENU, LoadLocalizedString(IDS_MENU_VIEWLOG));

        AppendMenu(hMenu, MF_STRING, IDM_EDITMENU, LoadLocalizedString(IDS_MENU_EDITCONFIG));
        AppendMenu(hMenu, MF_STRING, IDM_CLEARPASSMENU, LoadLocalizedString(IDS_MENU_CLEARPASS));

#ifndef DISABLE_CHANGE_PASSWORD
        if (o.conn[0].flags & FLAG_ALLOW_CHANGE_PASSPHRASE)
            AppendMenu(hMenu, MF_STRING, IDM_PASSPHRASEMENU, LoadLocalizedString(IDS_MENU_PASSPHRASE));
#endif

        AppendMenu(hMenu, MF_SEPARATOR, 0, 0);

        AppendMenu(hMenu, MF_STRING, IDM_IMPORT, LoadLocalizedString(IDS_MENU_IMPORT));
        AppendMenu(hMenu, MF_STRING ,IDM_SETTINGS, LoadLocalizedString(IDS_MENU_SETTINGS));
        AppendMenu(hMenu, MF_STRING ,IDM_CLOSE, LoadLocalizedString(IDS_MENU_CLOSE));

        SetMenuStatus(&o.conn[0],  o.conn[0].state);
        CreateNetworkFlyoutEntry(&o.conn[0], 0);
    }
    else {
        /* Create Main menu with all connections */
        int i;
        for (i = 0; i < o.num_configs; i++)
            AppendMenu(hMenu, MF_POPUP, (UINT_PTR) hMenuConn[i], o.conn[i].config_name);

        if (o.num_configs > 0)
            AppendMenu(hMenu, MF_SEPARATOR, 0, 0);

        if (o.service_only) {
            AppendMenu(hMenu, MF_STRING, IDM_SERVICE_START, LoadLocalizedString(IDS_MENU_SERVICEONLY_START));
            AppendMenu(hMenu, MF_STRING, IDM_SERVICE_STOP, LoadLocalizedString(IDS_MENU_SERVICEONLY_STOP));
            AppendMenu(hMenu, MF_STRING, IDM_SERVICE_RESTART, LoadLocalizedString(IDS_MENU_SERVICEONLY_RESTART));
            AppendMenu(hMenu, MF_SEPARATOR, 0, 0);
        }

        AppendMenu(hMenu, MF_STRING, IDM_IMPORT, LoadLocalizedString(IDS_MENU_IMPORT));
        AppendMenu(hMenu, MF_STRING, IDM_SETTINGS, LoadLocalizedString(IDS_MENU_SETTINGS));
        AppendMenu(hMenu, MF_STRING, IDM_CLOSE, LoadLocalizedString(IDS_MENU_CLOSE));


        /* Create popup menus for every connection */
        for (i=0; i < o.num_configs; i++) {
            if (o.service_only == 0) {
                AppendMenu(hMenuConn[i], MF_STRING, IDM_CONNECTMENU + i, LoadLocalizedString(IDS_MENU_CONNECT));
                AppendMenu(hMenuConn[i], MF_STRING, IDM_DISCONNECTMENU + i, LoadLocalizedString(IDS_MENU_DISCONNECT));
                AppendMenu(hMenuConn[i], MF_STRING, IDM_STATUSMENU + i, LoadLocalizedString(IDS_MENU_STATUS));
                AppendMenu(hMenuConn[i], MF_SEPARATOR, 0, 0);
            }

            AppendMenu(hMenuConn[i], MF_STRING, IDM_VIEWLOGMENU + i, LoadLocalizedString(IDS_MENU_VIEWLOG));

            AppendMenu(hMenuConn[i], MF_STRING, IDM_EDITMENU + i, LoadLocalizedString(IDS_MENU_EDITCONFIG));
            AppendMenu(hMenuConn[i], MF_STRING, IDM_CLEARPASSMENU + i, LoadLocalizedString(IDS_MENU_CLEARPASS));

#ifndef DISABLE_CHANGE_PASSWORD
            if (o.conn[i].flags & FLAG_ALLOW_CHANGE_PASSPHRASE)
                AppendMenu(hMenuConn[i], MF_STRING, IDM_PASSPHRASEMENU + i, LoadLocalizedString(IDS_MENU_PASSPHRASE));
#endif

            SetMenuStatus(&o.conn[i], o.conn[i].state);
            CreateNetworkFlyoutEntry(&o.conn[i], i);
        }
    }

    SetServiceMenuStatus();
}


/* Destroy popup menus */
static void
DestroyPopupMenus()
{
    int i;
    for (i = 0; i < o.num_configs; i++)
        DestroyMenu(hMenuConn[i]);

    DestroyMenu(hMenuService);
    DestroyMenu(hMenu);
    ClearNetworkFlyout();
}


/*
 * Handle mouse clicks on tray icon
 */
void
OnNotifyTray(LPARAM lParam)
{
    POINT pt;

    switch (lParam) {
    case WM_RBUTTONUP:
        /* Recreate popup menus */
        DestroyPopupMenus();
        BuildFileList();
        CreatePopupMenus();

        GetCursorPos(&pt);
        SetForegroundWindow(o.hWnd);
        TrackPopupMenu(hMenu, TPM_RIGHTALIGN, pt.x, pt.y, 0, o.hWnd, NULL);
        PostMessage(o.hWnd, WM_NULL, 0, 0);
        break;

    case WM_LBUTTONDBLCLK:
        if (o.service_only) {
            /* Start or stop OpenVPN service */
            if (o.service_state == service_disconnected) {
                MyStartService();
            }
            else if (o.service_state == service_connected
            && ShowLocalizedMsgEx(MB_YESNO, _T(PACKAGE_NAME), IDS_MENU_ASK_STOP_SERVICE) == IDYES) {
                MyStopService();
            }
        }
        else {
            int disconnected_conns = CountConnState(disconnected);

            DestroyPopupMenus();
            BuildFileList();
            CreatePopupMenus();

            /* Start connection if only one config exist */
            if (o.num_configs == 1 && o.conn[0].state == disconnected)
                    StartOpenVPN(&o.conn[0]);
            else if (disconnected_conns == o.num_configs - 1) {
                /* Show status window if only one connection is running */
                int i;
                for (i = 0; i < o.num_configs; i++) {
                    if (o.conn[i].state != disconnected) {
                        ShowWindow(o.conn[i].hwndStatus, SW_SHOW);
                        SetForegroundWindow(o.conn[i].hwndStatus);
                        break;
                    }
                }
            }
        }
        break;
    }
}


void
OnDestroyTray()
{
    DestroyMenu(hMenu);
    ClearNetworkFlyout();
    Shell_NotifyIcon(NIM_DELETE, &ni);
}


void
ShowTrayIcon()
{
  ni.cbSize = sizeof(ni);
  ni.uID = 0;
  ni.hWnd = o.hWnd;
  ni.uFlags = NIF_MESSAGE | NIF_TIP | NIF_ICON;
  ni.uCallbackMessage = WM_NOTIFYICONTRAY;
  ni.hIcon = LoadLocalizedSmallIcon(ID_ICO_DISCONNECTED);
  _tcsncpy(ni.szTip, LoadLocalizedString(IDS_TIP_DEFAULT), _countof(ni.szTip));

  Shell_NotifyIcon(NIM_ADD, &ni);
}

void
SetTrayIcon(conn_state_t state)
{
    TCHAR msg[500];
    TCHAR msg_connected[100];
    TCHAR msg_connecting[100];
    int i, config = 0;
    BOOL first_conn;
    UINT icon_id;

    _tcsncpy(msg, LoadLocalizedString(IDS_TIP_DEFAULT), _countof(ni.szTip));
    _tcsncpy(msg_connected, LoadLocalizedString(IDS_TIP_CONNECTED), _countof(msg_connected));
    _tcsncpy(msg_connecting, LoadLocalizedString(IDS_TIP_CONNECTING), _countof(msg_connecting));

    first_conn = TRUE;
    for (i = 0; i < o.num_configs; i++) {
        if (o.conn[i].state == connected) {
            /* Append connection name to Icon Tip Msg */
            _tcsncat(msg, (first_conn ? msg_connected : _T(", ")), _countof(msg) - _tcslen(msg) - 1);
            _tcsncat(msg, o.conn[i].config_name, _countof(msg) - _tcslen(msg) - 1);
            first_conn = FALSE;
            config = i;
        }
    }

    first_conn = TRUE;
    for (i = 0; i < o.num_configs; i++) {
        if (o.conn[i].state == connecting || o.conn[i].state == resuming || o.conn[i].state == reconnecting) {
            /* Append connection name to Icon Tip Msg */
            _tcsncat(msg, (first_conn ? msg_connecting : _T(", ")), _countof(msg) - _tcslen(msg) - 1);
            _tcsncat(msg, o.conn[i].config_name, _countof(msg) - _tcslen(msg) - 1);
            first_conn = FALSE;
        }
    }

    if (CountConnState(connected) == 1) {
        /* Append "Connected since and assigned IP" to message */
        TCHAR time[50];

        LocalizedTime(o.conn[config].connected_since, time, _countof(time));
        _tcsncat(msg, LoadLocalizedString(IDS_TIP_CONNECTED_SINCE), _countof(msg) - _tcslen(msg) - 1);
        _tcsncat(msg, time, _countof(msg) - _tcslen(msg) - 1);

        if (_tcslen(o.conn[config].ip) > 0) {
            TCHAR *assigned_ip = LoadLocalizedString(IDS_TIP_ASSIGNED_IP, o.conn[config].ip);
            _tcsncat(msg, assigned_ip, _countof(msg) - _tcslen(msg) - 1);
        }
    }

    icon_id = ID_ICO_CONNECTING;
    if (state == connected)
        icon_id = ID_ICO_CONNECTED;
    else if (state == disconnected)
        icon_id = ID_ICO_DISCONNECTED;

    ni.cbSize = sizeof(ni);
    ni.uID = 0;
    ni.hWnd = o.hWnd;
    ni.hIcon = LoadLocalizedSmallIcon(icon_id);
    ni.uFlags = NIF_MESSAGE | NIF_TIP | NIF_ICON;
    ni.uCallbackMessage = WM_NOTIFYICONTRAY;
    _tcsncpy(ni.szTip, msg, _countof(ni.szTip));

    Shell_NotifyIcon(NIM_MODIFY, &ni);
}


void
CheckAndSetTrayIcon()
{
    if (o.service_state == service_connected)
    {
        SetTrayIcon(connected);
        return;
    }

    if (CountConnState(connected) != 0)
    {
        SetTrayIcon(connected);
    }
    else
    {
        if (CountConnState(connecting) != 0 || CountConnState(reconnecting) != 0
        ||  CountConnState(resuming) != 0 || o.service_state == service_connecting)
            SetTrayIcon(connecting);
        else
            SetTrayIcon(disconnected);
    }
}


void
ShowTrayBalloon(TCHAR *infotitle_msg, TCHAR *info_msg)
{
    ni.cbSize = sizeof(ni);
    ni.uID = 0;
    ni.hWnd = o.hWnd;
    ni.uFlags = NIF_INFO;
    ni.uTimeout = 5000;
    ni.dwInfoFlags = NIIF_INFO;
    _tcsncpy(ni.szInfo, info_msg, _countof(ni.szInfo));
    _tcsncpy(ni.szInfoTitle, infotitle_msg, _countof(ni.szInfoTitle));

    Shell_NotifyIcon(NIM_MODIFY, &ni);
}


void
SetMenuStatus(connection_t *c, conn_state_t state)
{
    if (o.num_configs == 1)
    {
        if (state == disconnected)
        {
            EnableMenuItem(hMenu, IDM_CONNECTMENU, MF_ENABLED);
            EnableMenuItem(hMenu, IDM_DISCONNECTMENU, MF_GRAYED);
            EnableMenuItem(hMenu, IDM_STATUSMENU, MF_GRAYED);
        }
        else if (state == connecting || state == resuming || state == connected)
        {
            EnableMenuItem(hMenu, IDM_CONNECTMENU, MF_GRAYED);
            EnableMenuItem(hMenu, IDM_DISCONNECTMENU, MF_ENABLED);
            EnableMenuItem(hMenu, IDM_STATUSMENU, MF_ENABLED);
        }
        else if (state == disconnecting)
        {
            EnableMenuItem(hMenu, IDM_CONNECTMENU, MF_GRAYED);
            EnableMenuItem(hMenu, IDM_DISCONNECTMENU, MF_GRAYED);
            EnableMenuItem(hMenu, IDM_STATUSMENU, MF_ENABLED);
        }
        if (c->flags & (FLAG_SAVE_AUTH_PASS | FLAG_SAVE_KEY_PASS))
            EnableMenuItem(hMenu, IDM_CLEARPASSMENU, MF_ENABLED);
        else
            EnableMenuItem(hMenu, IDM_CLEARPASSMENU, MF_GRAYED);
    }
    else
    {
        int i;
        for (i = 0; i < o.num_configs; ++i)
        {
            if (c == &o.conn[i])
                break;
        }

        BOOL checked = (state == connected || state == disconnecting);
        CheckMenuItem(hMenu, i, MF_BYPOSITION | (checked ? MF_CHECKED : MF_UNCHECKED));

        if (state == disconnected)
        {
            EnableMenuItem(hMenuConn[i], IDM_CONNECTMENU + i, MF_ENABLED);
            EnableMenuItem(hMenuConn[i], IDM_DISCONNECTMENU + i, MF_GRAYED);
            EnableMenuItem(hMenuConn[i], IDM_STATUSMENU + i, MF_GRAYED);
        }
        else if (state == connecting || state == resuming || state == connected)
        {
            EnableMenuItem(hMenuConn[i], IDM_CONNECTMENU + i, MF_GRAYED);
            EnableMenuItem(hMenuConn[i], IDM_DISCONNECTMENU + i, MF_ENABLED);
            EnableMenuItem(hMenuConn[i], IDM_STATUSMENU + i, MF_ENABLED);
        }
        else if (state == disconnecting)
        {
            EnableMenuItem(hMenuConn[i], IDM_CONNECTMENU + i, MF_GRAYED);
            EnableMenuItem(hMenuConn[i], IDM_DISCONNECTMENU + i, MF_GRAYED);
            EnableMenuItem(hMenuConn[i], IDM_STATUSMENU + i, MF_ENABLED);
        }
        if (c->flags & (FLAG_SAVE_AUTH_PASS | FLAG_SAVE_KEY_PASS))
            EnableMenuItem(hMenuConn[i], IDM_CLEARPASSMENU + i, MF_ENABLED);
        else
            EnableMenuItem(hMenuConn[i], IDM_CLEARPASSMENU + i, MF_GRAYED);
    }
}


void
SetServiceMenuStatus()
{
    HMENU hMenuHandle;

    if (o.service_only == 0)
        return;

    if (o.service_only)
        hMenuHandle = hMenu;
    else
        hMenuHandle = hMenuService;

    if (o.service_state == service_noaccess
    ||  o.service_state == service_connecting) {
        EnableMenuItem(hMenuHandle, IDM_SERVICE_START, MF_GRAYED);
        EnableMenuItem(hMenuHandle, IDM_SERVICE_STOP, MF_GRAYED);
        EnableMenuItem(hMenuHandle, IDM_SERVICE_RESTART, MF_GRAYED);
    }
    else if (o.service_state == service_connected) {
        EnableMenuItem(hMenuHandle, IDM_SERVICE_START, MF_GRAYED);
        EnableMenuItem(hMenuHandle, IDM_SERVICE_STOP, MF_ENABLED);
        EnableMenuItem(hMenuHandle, IDM_SERVICE_RESTART, MF_ENABLED);
    }
    else {
        EnableMenuItem(hMenuHandle, IDM_SERVICE_START, MF_ENABLED);
        EnableMenuItem(hMenuHandle, IDM_SERVICE_STOP, MF_GRAYED);
        EnableMenuItem(hMenuHandle, IDM_SERVICE_RESTART, MF_GRAYED);
    }
}
