/*
 * Copyright (C) 1997-2001 Id Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * =======================================================================
 *
 * This is the "heart" of the id Tech 2 refresh engine. This file
 * implements the main window in which Quake II is running. The window
 * itself is created by the SDL backend, but here the refresh module is
 * loaded, initialized and it's interaction with the operating system
 * implemented. This code is also the interconnect between the input
 * system (the mouse) and the keyboard system, both are here tied
 * together with the refresher. The direct interaction between the
 * refresher and those subsystems are the main cause for the very
 * acurate and precise input controls of the id Tech 2.
 *
 * =======================================================================
 */

#include <assert.h>
#include <float.h>

#include "../common/header/common.h"
#include "header/winquake.h"
#include "../client/header/client.h"

/* Structure containing functions
 * exported from refresh DLL */
refexport_t re;

cvar_t *win_noalttab;

/* Console variables that we need to access from this module */
cvar_t *vid_gamma;
cvar_t *vid_ref;                /* Name of Refresh DLL loaded */
cvar_t *vid_xpos;               /* X coordinate of window position */
cvar_t *vid_ypos;               /* Y coordinate of window position */
cvar_t *vid_fullscreen;

/* Global variables used internally by this module */
viddef_t viddef;                   /* global video state; used by other modules */
HINSTANCE reflib_library;          /* Handle to refresh DLL */
qboolean reflib_active = 0;
HWND cl_hwnd;                      /* Main window handle for life of program */
static qboolean s_alttab_disabled;
extern unsigned sys_msg_time;
in_state_t in_state;

typedef struct vidmode_s
{
	const char *description;
	int width, height;
	int mode;
} vidmode_t;

void (*IN_Update_fp)(void);
void (*IN_KeyboardInit_fp)(Key_Event_fp_t fp);
void (*IN_Close_fp)(void);
void (*IN_BackendInit_fp)(in_state_t *in_state_p);
void (*IN_BackendShutdown_fp)(void);
void (*IN_BackendMouseButtons_fp)(void);
void (*IN_BackendMove_fp)(usercmd_t *cmd);

#define VID_NUM_MODES (sizeof(vid_modes) / sizeof(vid_modes[0]))
#define MAXPRINTMSG 4096

/* ========================================================================== */

void
Do_Key_Event(int key, qboolean down)
{
	Key_Event(key, down, Sys_Milliseconds());
}

static void
WIN_DisableAltTab(void)
{
	if (s_alttab_disabled)
	{
		return;
	}

	s_alttab_disabled = true;
}

static void
WIN_EnableAltTab(void)
{
	if (s_alttab_disabled)
	{
		UnregisterHotKey(0, 0);
		UnregisterHotKey(0, 1);
		s_alttab_disabled = false;
	}
}

/* ========================================================================== */

void
VID_Printf(int print_level, char *fmt, ...)
{
	va_list argptr;
	char msg[MAXPRINTMSG];

	va_start(argptr, fmt);
	vsprintf(msg, fmt, argptr);
	va_end(argptr);

	if (print_level == PRINT_ALL)
	{
		Com_Printf("%s", msg);
	}
	else if (print_level == PRINT_DEVELOPER)
	{
		Com_DPrintf("%s", msg);
	}
	else if (print_level == PRINT_ALERT)
	{
		MessageBox(0, msg, "PRINT_ALERT", MB_ICONWARNING);
		OutputDebugString(msg);
	}
}

void
VID_Error(int err_level, char *fmt, ...)
{
	va_list argptr;
	char msg[MAXPRINTMSG];

	va_start(argptr, fmt);
	vsprintf(msg, fmt, argptr);
	va_end(argptr);

	Com_Error(err_level, "%s", msg);
}

/* ========================================================================== */

void
AppActivate(BOOL fActive, BOOL minimize)
{
	Minimized = minimize;

	Key_ClearStates();

	/* we don't want to act like we're active if we're minimized */
	if (fActive && !Minimized)
	{
		ActiveApp = true;
	}
	else
	{
		ActiveApp = false;
	}

	/* minimize/restore mouse-capture on demand */
	if (!ActiveApp)
	{
		CDAudio_Activate(false);

		if (win_noalttab->value)
		{
			WIN_EnableAltTab();
		}
	}
	else
	{
		CDAudio_Activate(true);

		if (win_noalttab->value)
		{
			WIN_DisableAltTab();
		}
	}
}

/*
 * Console command to re-start the video mode and refresh DLL. We do this
 * simply by setting the modified flag for the vid_ref variable, which will
 * cause the entire video mode and refresh DLL to be reset on the next frame.
 */
void
VID_Restart_f(void)
{
	vid_ref->modified = true;
}

void
VID_Front_f(void)
{
	SetWindowLong(cl_hwnd, GWL_EXSTYLE, WS_EX_TOPMOST);
	SetForegroundWindow(cl_hwnd);
}

/* This must be the same as in menu.c! */
vidmode_t vid_modes[] = {
	{"Mode 0: 320x240", 320, 240, 0},
	{"Mode 1: 400x300", 400, 300, 1},
	{"Mode 2: 512x384", 512, 384, 2},
	{"Mode 3: 640x400", 640, 400, 3},
	{"Mode 4: 640x480", 640, 480, 4},
	{"Mode 5: 800x500", 800, 500, 5},
	{"Mode 6: 800x600", 800, 600, 6},
	{"Mode 7: 960x720", 960, 720, 7},
	{"Mode 8: 1024x480", 1024, 480, 8},
	{"Mode 9: 1024x640", 1024, 640, 9},
	{"Mode 10: 1024x768", 1024, 768, 10},
	{"Mode 11: 1152x768", 1152, 768, 11},
	{"Mode 12: 1152x864", 1152, 864, 12},
	{"Mode 13: 1280x800", 1280, 800, 13},
	{"Mode 14: 1280x854", 1280, 854, 14},
	{"Mode 15: 1280x960", 1280, 860, 15},
	{"Mode 16: 1280x1024", 1280, 1024, 16},
	{"Mode 17: 1440x900", 1440, 900, 17},
	{"Mode 18: 1600x1200", 1600, 1200, 18},
	{"Mode 19: 1680x1050", 1680, 1050, 19},
	{"Mode 20: 1920x1080", 1920, 1080, 20},
	{"Mode 21: 1920x1200", 1920, 1200, 21},
	{"Mode 22: 2048x1536", 2048, 1536, 22},
};

qboolean
VID_GetModeInfo(int *width, int *height, int mode)
{
	if ((mode < 0) || (mode >= VID_NUM_MODES))
	{
		return false;
	}

	*width = vid_modes[mode].width;
	*height = vid_modes[mode].height;

	return true;
}

void
VID_UpdateWindowPosAndSize(int x, int y)
{
	RECT r;
	int style;
	int w, h;

	r.left = 0;
	r.top = 0;
	r.right = viddef.width;
	r.bottom = viddef.height;

	style = GetWindowLong(cl_hwnd, GWL_STYLE);
	AdjustWindowRect(&r, style, FALSE);

	w = r.right - r.left;
	h = r.bottom - r.top;

	MoveWindow(cl_hwnd, vid_xpos->value, vid_ypos->value, w, h, TRUE);
}

void
VID_NewWindow(int width, int height)
{
	viddef.width = width;
	viddef.height = height;

	cl.force_refdef = true; /* can't use a paused refdef */
}

void
VID_FreeReflib(void)
{
	if (reflib_library)
	{
		if (IN_Close_fp)
		{
			IN_Close_fp();
		}

		if (IN_BackendShutdown_fp)
		{
			IN_BackendShutdown_fp();
		}
	}

	if (!FreeLibrary(reflib_library))
	{
		Com_Error(ERR_FATAL, "Reflib FreeLibrary failed");
	}

	IN_KeyboardInit_fp = NULL;
	IN_Update_fp = NULL;
	IN_Close_fp = NULL;
	IN_BackendInit_fp = NULL;
	IN_BackendShutdown_fp = NULL;
	IN_BackendMouseButtons_fp = NULL;
	IN_BackendMove_fp = NULL;

	memset(&re, 0, sizeof(re));
	reflib_library = NULL;
	reflib_active = false;
}

qboolean
VID_LoadRefresh(char *name)
{
	refimport_t ri;
	R_GetRefAPI_t GetRefAPI;

	if (reflib_active)
	{
		if (IN_Close_fp)
		{
			IN_Close_fp();
		}

		if (IN_BackendShutdown_fp)
		{
			IN_BackendShutdown_fp();
		}

		IN_Close_fp = NULL;
		IN_BackendShutdown_fp = NULL;

		re.Shutdown();
		VID_FreeReflib();
	}

	Com_Printf("------- Loading %s -------\n", name);

	if ((reflib_library = LoadLibrary(name)) == 0)
	{
		Com_Printf("LoadLibrary(\"%s\") failed\n", name);

		return false;
	}

	ri.Cmd_AddCommand = Cmd_AddCommand;
	ri.Cmd_RemoveCommand = Cmd_RemoveCommand;
	ri.Cmd_Argc = Cmd_Argc;
	ri.Cmd_Argv = Cmd_Argv;
	ri.Cmd_ExecuteText = Cbuf_ExecuteText;
	ri.Con_Printf = VID_Printf;
	ri.Sys_Error = VID_Error;
	ri.FS_LoadFile = FS_LoadFile;
	ri.FS_FreeFile = FS_FreeFile;
	ri.FS_Gamedir = FS_Gamedir;
	ri.Cvar_Get = Cvar_Get;
	ri.Cvar_Set = Cvar_Set;
	ri.Cvar_SetValue = Cvar_SetValue;
	ri.Vid_GetModeInfo = VID_GetModeInfo;
	ri.Vid_MenuInit = VID_MenuInit;
	ri.Vid_NewWindow = VID_NewWindow;

	if ((GetRefAPI = (void *)GetProcAddress(reflib_library, "R_GetRefAPI")) == 0)
	{
		Com_Error(ERR_FATAL, "GetProcAddress failed on %s", name);
	}

	re = GetRefAPI(ri);

	if (re.api_version != API_VERSION)
	{
		VID_FreeReflib();
		Com_Error(ERR_FATAL, "%s has incompatible api_version", name);
	}

	/* Init IN (Mouse) */
	in_state.IN_CenterView_fp = IN_CenterView;
	in_state.Key_Event_fp = Do_Key_Event;
	in_state.viewangles = cl.viewangles;
	in_state.in_strafe_state = &in_strafe.state;
	in_state.in_speed_state = &in_speed.state;

	if (((IN_BackendInit_fp = (void *)GetProcAddress(reflib_library, "IN_BackendInit")) == NULL) ||
		((IN_BackendShutdown_fp = (void *)GetProcAddress(reflib_library, "IN_BackendShutdown")) == NULL) ||
		((IN_BackendMouseButtons_fp = (void *)GetProcAddress(reflib_library, "IN_BackendMouseButtons")) == NULL) ||
		((IN_BackendMove_fp = (void *)GetProcAddress(reflib_library, "IN_BackendMove")) == NULL))
	{
		Com_Error(ERR_FATAL, "No input backend init functions in REF.\n");
	}

	if (IN_BackendInit_fp)
	{
		IN_BackendInit_fp( &in_state );
	}

	if (re.Init(global_hInstance, 0) == -1)
	{
		re.Shutdown();
		Com_Error(ERR_FATAL, "re.Init() failed");
		VID_FreeReflib();
		return false;
	}

	/* Init IN (Keyboard) */
	if (((IN_KeyboardInit_fp = (void *)GetProcAddress(reflib_library, "IN_KeyboardInit")) == NULL) ||
		((IN_Update_fp = (void *)GetProcAddress(reflib_library, "IN_Update")) == NULL) || 
		((IN_Close_fp = (void *)GetProcAddress(reflib_library, "IN_Close")) == NULL))
	{
		Com_Error(ERR_FATAL, "No keyboard input functions in REF.\n");
	}

	IN_KeyboardInit_fp(Do_Key_Event);
	Key_ClearStates();

	Com_Printf("------------------------------------\n");
	reflib_active = true;

	return true;
}

/*
 * This function gets called once just before drawing each frame, and it's sole purpose in life
 * is to check to see if any of the video mode parameters have changed, and if they have to
 * update the rendering DLL and/or video mode to match.
 */
void
VID_CheckChanges(void)
{
	char name[100];

	if (win_noalttab->modified)
	{
		if (win_noalttab->value)
		{
			WIN_DisableAltTab();
		}
		else
		{
			WIN_EnableAltTab();
		}

		win_noalttab->modified = false;
	}

	if (vid_ref->modified)
	{
		cl.force_refdef = true; /* can't use a paused refdef */
		S_StopAllSounds();
	}

	while (vid_ref->modified)
	{
		/* refresh has changed */
		vid_ref->modified = false;
		vid_fullscreen->modified = true;
		cl.refresh_prepped = false;
		cls.disable_screen = true;

		Com_sprintf(name, sizeof(name), "ref_%s.dll", vid_ref->string);

		if (!VID_LoadRefresh(name))
		{
			Cvar_Set("vid_ref", "gl");
		}

		cls.disable_screen = false;
	}
}

void
VID_Init(void)
{
	/* Create the video variables so we know how to start the graphics drivers */
	vid_ref = Cvar_Get("vid_ref", "soft", CVAR_ARCHIVE);
	vid_xpos = Cvar_Get("vid_xpos", "3", CVAR_ARCHIVE);
	vid_ypos = Cvar_Get("vid_ypos", "22", CVAR_ARCHIVE);
	vid_fullscreen = Cvar_Get("vid_fullscreen", "0", CVAR_ARCHIVE);
	vid_gamma = Cvar_Get("vid_gamma", "1", CVAR_ARCHIVE);
	win_noalttab = Cvar_Get("win_noalttab", "0", CVAR_ARCHIVE);

	/* Add some console commands that we want to handle */
	Cmd_AddCommand("vid_restart", VID_Restart_f);
	Cmd_AddCommand("vid_front", VID_Front_f);

	/* Start the graphics mode and load refresh DLL */
	VID_CheckChanges();
}

void
VID_Shutdown(void)
{
	if (reflib_active)
	{
		re.Shutdown();
		VID_FreeReflib();
	}
}

qboolean
VID_CheckRefExists(const char *ref)
{
	return true;
}

/* ========================================================================== */

void
IN_Move(usercmd_t *cmd)
{
	if (IN_BackendMove_fp)
	{
		IN_BackendMove_fp(cmd);
	}
}

void
IN_Shutdown(void)
{
	if (IN_BackendShutdown_fp)
	{
		IN_BackendShutdown_fp();
	}
}

void
IN_Commands(void)
{
	if (IN_BackendMouseButtons_fp)
	{
		IN_BackendMouseButtons_fp();
	}
}
