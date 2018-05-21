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
#include <tchar.h>
#include <ras.h>
#include <rasdlg.h>
#include <raserror.h>

__declspec(dllexport)
DWORD WINAPI RasCustomDial(
    HINSTANCE hInstDll __attribute__((unused)),
    LPRASDIALEXTENSIONS lpRasDialExtensions __attribute__((unused)),
    LPWSTR lpszPhonebook __attribute__((unused)),
    LPRASDIALPARAMSW lpRasDialParams __attribute__((unused)),
    DWORD dwNotifierType __attribute__((unused)),
    LPVOID lpvNotifier __attribute__((unused)),
    LPHRASCONN lphRasConn __attribute__((unused)),
    DWORD dwFlags __attribute__((unused)))
{
    return ERROR_CALL_NOT_IMPLEMENTED;
}

/* Copied from OpenVPN-GUI main.c */
static const TCHAR OpenVPNGuiClassName[] = _T("OpenVPN-GUI");

/* Copied from OpenVPN-GUI tray.h */
#define IDM_CONNECTMENU         300

static const TCHAR WindowCaption[] = _T("Walrus VPN");

__declspec(dllexport)
BOOL WINAPI RasCustomDialDlg(
    HINSTANCE hInstDll __attribute__((unused)),
    DWORD dwFlags __attribute__((unused)),
    LPWSTR lpszwPhonebook,
    LPWSTR lpszwEntry,
    LPWSTR lpszwPhoneNumber __attribute__((unused)),
    LPRASDIALDLG lpInfo,
    PVOID pVoid __attribute__((unused)))
{
    HWND window = FindWindow(OpenVPNGuiClassName, NULL);
    if (NULL == window)
    {
        MessageBox(NULL, _T("OpenVPN-GUI is not running. Please launch the GUI."), WindowCaption, MB_OK);
        lpInfo->dwError = ERROR_UNKNOWN;
        return FALSE;
    }
    else
    {
        DWORD result;
        DWORD entrySize = 0;
        LPRASENTRYW entry = NULL;

        result = RasGetEntryPropertiesW(lpszwPhonebook, lpszwEntry, NULL, &entrySize, NULL, NULL);
        if (ERROR_SUCCESS != result && ERROR_BUFFER_TOO_SMALL != result)
        {
            MessageBox(NULL, _T("Could not get size of VPN entry"), WindowCaption, MB_OK);
            lpInfo->dwError = result;
            return FALSE;
        }

        entry = (LPRASENTRYW)malloc(entrySize);
        if (NULL == entry)
        {
            lpInfo->dwError = ERROR_OUTOFMEMORY;
            return FALSE;
        }

        memset(entry, 0, entrySize);
        entry->dwSize = sizeof(RASENTRYW);

        result = RasGetEntryPropertiesW(lpszwPhonebook, lpszwEntry, entry, &entrySize, NULL, NULL);
        if (ERROR_SUCCESS != result)
        {
            MessageBox(NULL, _T("Could not get VPN entry"), WindowCaption, MB_OK);
            lpInfo->dwError = result;
            free(entry);
            return FALSE;
        }

        /* Convert the number in the local phone number field to get the offset. */
        int offset = _wtoi(entry->szLocalPhoneNumber);

        /* Sanity check: There should not be more than 10 entries, and the value should never be negative.*/
        if (0 > offset || 10 < offset)
        {
            MessageBox(NULL, _T("Offset is invalid number"), WindowCaption, MB_OK);
            lpInfo->dwError = ERROR_UNKNOWN;
            free(entry);
            return FALSE;
        }

        if (0 == offset)
        {
            /* 0 is also used for an error condition. If that's the case, check that the string is actually
             * a zero.
             */
            if (entry->szLocalPhoneNumber[0] != L'0' && entry->szLocalPhoneNumber[1] != L'\0')
            {
                MessageBox(NULL, _T("Offset not recognized as a number"), WindowCaption, MB_OK);
                lpInfo->dwError = ERROR_UNKNOWN;
                free(entry);
                return FALSE;
            }
        }

        /* No longer need entry at this point. */
        free(entry);

        if (FALSE == PostMessage(window, WM_COMMAND, IDM_CONNECTMENU + offset, 0))
        {
            MessageBox(NULL, _T("Could not communicate with OpenVPN-GUI"), WindowCaption, MB_OK);
            lpInfo->dwError = GetLastError();
            return FALSE;
        }
        else
        {
            return TRUE;
        }
    }
}

__declspec(dllexport)
DWORD WINAPI RasCustomHangUp(
    HRASCONN hRasConn __attribute__((unused)))
{
    return ERROR_CALL_NOT_IMPLEMENTED;
}

__declspec(dllexport)
BOOL WINAPI RasCustomEntryDlg(HINSTANCE hInstDll __attribute__((unused)),
    LPWSTR lpszwPhonebook __attribute__((unused)),
    LPWSTR lpszwEntry __attribute__((unused)),
    LPRASENTRYDLG lpInfo,
    DWORD dwFlags __attribute__((unused)))
{
    lpInfo->dwError = ERROR_CALL_NOT_IMPLEMENTED;
    return FALSE;
}

__declspec(dllexport)
DWORD CALLBACK RasCustomDeleteEntryNotify(
    LPCTSTR lpszPhonebook __attribute__((unused)),
    LPCTSTR lpszEntry __attribute__((unused)),
    DWORD   dwFlags __attribute__((unused)))
{
    return ERROR_SUCCESS;
}
