/**
 *
 *  EGLPORT.C
 *  Copyright (C) 2011 Scott R. Smith
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *  THE SOFTWARE.
 *
 */

#include "eglport.h"

#include <stdio.h>
#include "EGL/egl.h"

#if defined(USE_EGL_SDL)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include "SDL_syswm.h"
#include "SDL.h"
#endif

#if defined(PANDORA)
/* Pandora VSync */
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fb.h>

#ifndef FBIO_WAITFORVSYNC
#define FBIO_WAITFORVSYNC _IOW('F', 0x20, __u32)
#endif
int fbdev = -1;
#endif

#if defined(PI)
#include "bcm_host.h"
#include "GLES2/gl2.h"
#include "EGL/egl.h"
#include "EGL/eglext.h"

#endif

int8_t VSync    = 0;
int8_t FSAA     = 0;

EGLDisplay          g_eglDisplay    = NULL;
EGLConfig           g_eglConfig     = NULL;
EGLContext          g_eglContext    = NULL;
EGLSurface          g_eglSurface    = NULL;
#if defined(USE_EGL_SDL)
Display*            g_Display       = NULL;
#else
NativeDisplayType   g_Display       = NULL;
#endif
NativeWindowType    g_Window        = NULL;

#define g_totalConfigsIn 20
EGLint      g_totalConfigsFound = 0;
EGLConfig   g_allConfigs[g_totalConfigsIn];


/*======================================================
 * Close EGL resources
  ====================================================*/
void EGL_Close()
{
    if (g_eglDisplay != NULL)
    {
        peglMakeCurrent( g_eglDisplay, NULL, NULL, EGL_NO_CONTEXT );
        if (g_eglContext != NULL) {
            peglDestroyContext( g_eglDisplay, g_eglContext );
        }
        if (g_eglSurface != NULL) {
            peglDestroySurface( g_eglDisplay, g_eglSurface );
        }
        peglTerminate( g_eglDisplay );
    }

    g_eglSurface = NULL;
    g_eglContext = NULL;
    g_eglDisplay = NULL;

#if defined(USE_EGL_RAW)
    if (g_Window != NULL) {
        free(g_Window);
    }
    g_Window = NULL;
#elif defined(USE_EGL_SDL)
    if (g_Display != NULL) {
        XCloseDisplay(g_Display);
    }
    g_Display = NULL;
#else
    #error Incorrect EGL Configuration
#endif /* defined(USE_EGL_RAW) */

    CheckEGLErrors( __FILE__, __LINE__ );

    printf( "EGL Closed\n" );

    Platform_Close();
}

/*===========================================================
Setup EGL context and surface
===========================================================*/
int8_t EGL_Init( void )
{
    int configIndex = 0;

    if (FindAppropriateEGLConfigs() != 0)
    {
        printf( "EGL ERROR: Unable to configure EGL. See previous error.\n" );
        return 1;
    }

    printf( "EGL Config %d\n", configIndex );

    if ( ConfigureEGL(g_allConfigs[configIndex]) != 0)
    {
        CheckEGLErrors( __FILE__, __LINE__ );
        printf( "EGL ERROR: Unable to configure EGL. See previous error.\n" );
        return 1;
    }

    return 0;
}

/*===========================================================
Swap EGL buffers and update the display
===========================================================*/
void EGL_SwapBuffers( void )
{
    if (VSync != 0) {
        Platform_VSync();
    }
    peglSwapBuffers( g_eglDisplay, g_eglSurface );
}


/*========================================================
 *  Init base EGL
 * ======================================================*/
int8_t EGL_Open( void )
{
    EGLBoolean result;
    const char* output;

    // Setup any platform specific bits
    Platform_Open();

#if defined(USE_EGL_SDL)
    printf( "EGL Opening X11 display\n" );
    g_Display = XOpenDisplay(NULL);

    if (g_Display == NULL)
    {
        printf( "EGL ERROR: unable to get display!\n" );
        return 1;
    }
#endif /* defined(USE_EGL_SDL) */


    printf( "EGL Getting EGL display\n" );
#if defined(PI)
    g_eglDisplay = peglGetDisplay(EGL_DEFAULT_DISPLAY);
#else
    g_eglDisplay = peglGetDisplay( (NativeDisplayType)g_Display );
#endif

    if (g_eglDisplay == EGL_NO_DISPLAY)
    {
        CheckEGLErrors( __FILE__, __LINE__ );
        printf( "EGL ERROR: Unable to create EGL display.\n" );
        return 1;
    }

    printf( "EGL Initializing\n" );
    result = peglInitialize( g_eglDisplay, NULL, NULL );

    if (result != EGL_TRUE )
    {
        CheckEGLErrors( __FILE__, __LINE__ );
        printf( "EGL ERROR: Unable to initialize EGL display.\n" );
        return 1;
    }

    // Get EGL Library Information
    output = peglQueryString( g_eglDisplay, EGL_VENDOR );
    printf( "EGL_VENDOR: %s\n", output );
    output = peglQueryString( g_eglDisplay, EGL_VERSION );
    printf( "EGL_VERSION: %s\n", output );
    output = peglQueryString( g_eglDisplay, EGL_EXTENSIONS );
    printf( "EGL_EXTENSIONS: %s\n", output );

    CheckEGLErrors( __FILE__, __LINE__ );

    return 0;
}

/*===========================================================
Initialise OpenGL settings
===========================================================*/
int8_t ConfigureEGL(EGLConfig config)
{
    EGLBoolean result;

#if defined(USE_GLES1)
    static const EGLint s_contextAttribs = NULL;
#elif defined(USE_GLES2)
    static const EGLint s_contextAttribs[] =
    {
          EGL_CONTEXT_CLIENT_VERSION,     2,
          EGL_NONE
    };
#else
    #error Incorrect Opengl-ES Configuration for s_contextAttribs
#endif /* defined(USE_GLES1) */

    // Cleanup in case of a reset
    if (g_eglDisplay != NULL)
    {
        peglMakeCurrent( g_eglDisplay, NULL, NULL, EGL_NO_CONTEXT );
        if (g_eglContext != NULL) {
            peglDestroyContext( g_eglDisplay, g_eglContext );
        }
        if (g_eglSurface != NULL) {
            peglDestroySurface( g_eglDisplay, g_eglSurface );
        }
    }

#if defined(EGL_VERSION_1_2)
    // Bind GLES and create the context
    printf( "EGL Binding API\n" );
    peglBindAPI( EGL_OPENGL_ES_API );

    if ( CheckEGLErrors( __FILE__, __LINE__ ) !=  0 )
    {
        printf( "EGL ERROR: Could not bind EGL API.\n" );
        return 1;
    }
#endif /* defined(USE_EGL_SDL) */

    printf( "EGL Creating Context\n" );
    g_eglContext = peglCreateContext( g_eglDisplay, config, NULL, s_contextAttribs );

    if (g_eglContext == EGL_NO_CONTEXT)
    {
        CheckEGLErrors( __FILE__, __LINE__ );
        printf( "EGL ERROR: Unable to create GLES context!\n");
        return 1;
    }

#if defined(USE_EGL_RAW)
    if (g_Window == NULL) {
        g_Window = (NativeWindowType)malloc(16*1024);

        if(g_Window == NULL) {
            printf( "EGL ERROR: Memory for window Failed\n" );
            return 1;
        }
    }
    else
    {
        printf( "EGL Info: Memory for window already allocated\n" );
    }
#elif defined(USE_EGL_SDL)
    // Get the SDL window handle
    SDL_SysWMinfo sysInfo; //Will hold our Window information
    SDL_VERSION(&sysInfo.version); //Set SDL version

    if(SDL_GetWMInfo(&sysInfo) <= 0)
    {
        printf( "EGL ERROR: Unable to get SDL window handle: %s\n", SDL_GetError() );
        return 1;
    }
    g_Window = (NativeWindowType)sysInfo.info.x11.window;
#else
    #error Incorrect EGL Configuration for g_Window
#endif /* defined(USE_EGL_RAW) */

#if defined(PI)
    static EGL_DISPMANX_WINDOW_T nativewindow;

    DISPMANX_ELEMENT_HANDLE_T dispman_element;
    DISPMANX_DISPLAY_HANDLE_T dispman_display;
    DISPMANX_UPDATE_HANDLE_T dispman_update;
    VC_RECT_T dst_rect;
    VC_RECT_T src_rect;

    uint32_t display_width;
    uint32_t display_height;
    uint32_t success = 0;

    // create an EGL window surface, passing context width/height
    success = graphics_get_display_size(0 /* LCD */, 
                        &display_width, &display_height);
    if ( success < 0 )
    {
      return EGL_FALSE;
    }

    // You can hardcode the resolution here:
    /* display_width = 1440; */
    /* display_height = 900; */

    dst_rect.x = 0;
    dst_rect.y = 0;
    dst_rect.width = display_width;
    dst_rect.height = display_height;

    src_rect.x = 0;
    src_rect.y = 0;
    src_rect.width = display_width << 16;
    src_rect.height = display_height << 16;

    dispman_display = vc_dispmanx_display_open( 0 /* LCD */);
    dispman_update = vc_dispmanx_update_start( 0 );

    dispman_element = vc_dispmanx_element_add ( dispman_update, 
      dispman_display, 0/*layer*/, &dst_rect, 0/*src*/,
      &src_rect, DISPMANX_PROTECTION_NONE, 0 /*alpha*/, 
      0/*clamp*/, 0/*transform*/);

    nativewindow.element = dispman_element;
    nativewindow.width = display_width;
    nativewindow.height = display_height;
    vc_dispmanx_update_submit_sync( dispman_update );

    /* g_eglContext->hWnd = &nativewindow; */
    printf( "EGL Creating window surface\n" );
    g_eglSurface = peglCreateWindowSurface(g_eglDisplay, config,
                         (EGLNativeWindowType)&nativewindow, NULL);
#else
    printf( "EGL Creating window surface\n" );
    g_eglSurface = peglCreateWindowSurface( g_eglDisplay, config, g_Window, 0 );
#endif /* defined(USE_EGL_RAW) */


    if (g_eglSurface == EGL_NO_SURFACE)
    {
        CheckEGLErrors( __FILE__, __LINE__ );
        printf( "EGL ERROR: Unable to create EGL surface!\n" );
        return 1;
    }

    printf( "EGL Making Current\n" );
    result = peglMakeCurrent( g_eglDisplay,  g_eglSurface,  g_eglSurface, g_eglContext );

    if (result != EGL_TRUE)
    {
        CheckEGLErrors( __FILE__, __LINE__ );
        printf( "EGL ERROR: Unable to make GLES context current\n" );
        return 1;
    }

    CheckEGLErrors( __FILE__, __LINE__ );
    printf( "EGL Complete\n" );
    glClear( GL_COLOR_BUFFER_BIT );

    return 0;
}

/*=======================================================
* Detect available video resolutions
=======================================================*/
int8_t FindAppropriateEGLConfigs( void )
{
    EGLBoolean result;
    int attrib = 0;
    EGLint ConfigAttribs[19];

#if defined(PI)

    ConfigAttribs[attrib++] = EGL_RED_SIZE;
    ConfigAttribs[attrib++] = 8;
    ConfigAttribs[attrib++] = EGL_GREEN_SIZE;
    ConfigAttribs[attrib++] = 8;
    ConfigAttribs[attrib++] = EGL_BLUE_SIZE;
    ConfigAttribs[attrib++] = 8;
    ConfigAttribs[attrib++] = EGL_ALPHA_SIZE;
    ConfigAttribs[attrib++] = 8;
    ConfigAttribs[attrib++] = EGL_SURFACE_TYPE;
    ConfigAttribs[attrib++] = EGL_WINDOW_BIT;
    ConfigAttribs[attrib++] = EGL_NONE;
#else
    ConfigAttribs[attrib++] = EGL_RED_SIZE;
    ConfigAttribs[attrib++] = 5;
    ConfigAttribs[attrib++] = EGL_GREEN_SIZE;
    ConfigAttribs[attrib++] = 6;
    ConfigAttribs[attrib++] = EGL_BLUE_SIZE;
    ConfigAttribs[attrib++] = 5;
    ConfigAttribs[attrib++] = EGL_DEPTH_SIZE;
    ConfigAttribs[attrib++] = 16;
#if !defined(CAANOO) && !defined(WIZ)
    ConfigAttribs[attrib++] = EGL_STENCIL_SIZE;
    ConfigAttribs[attrib++] = 8;
#endif
    ConfigAttribs[attrib++] = EGL_SURFACE_TYPE;
    ConfigAttribs[attrib++] = EGL_WINDOW_BIT;
#if defined(EGL_VERSION_1_2)
    ConfigAttribs[attrib++] = EGL_RENDERABLE_TYPE;
#if defined(USE_GLES1)
    ConfigAttribs[attrib++] = EGL_OPENGL_ES_BIT;
#elif defined(USE_GLES2)
    ConfigAttribs[attrib++] = EGL_OPENGL_ES2_BIT;
#endif /* defined(USE_GLES1) */
#endif /* defined(EGL_VERSION_1_2) */
    ConfigAttribs[attrib++] = EGL_SAMPLE_BUFFERS;
    ConfigAttribs[attrib++] = (FSAA > 0) ? 1 : 0;
    ConfigAttribs[attrib++] = EGL_SAMPLES;
    ConfigAttribs[attrib++] = FSAA;
    ConfigAttribs[attrib++] = EGL_NONE;
#endif /* defined(PI) */

    result = peglChooseConfig( g_eglDisplay, ConfigAttribs, g_allConfigs, g_totalConfigsIn, &g_totalConfigsFound );
    if (result != EGL_TRUE || g_totalConfigsFound == 0)
    {
        CheckEGLErrors( __FILE__, __LINE__ );
        printf( "EGL ERROR: Unable to query for available configs, found %d.\n", g_totalConfigsFound );
        return 1;
    }
    printf( "EGL Found %d available configs\n", g_totalConfigsFound );

    return 0;
}

int8_t CheckEGLErrors( const char* file, uint16_t line )
{
    EGLenum error;
    const char* errortext;

    error = eglGetError();

    if (error != EGL_SUCCESS && error != 0)
    {
        switch (error)
        {
            case EGL_NOT_INITIALIZED:           errortext = "EGL_NOT_INITIALIZED"; break;
            case EGL_BAD_ACCESS:                errortext = "EGL_BAD_ACCESS"; break;
            case EGL_BAD_ALLOC:                 errortext = "EGL_BAD_ALLOC"; break;
            case EGL_BAD_ATTRIBUTE:             errortext = "EGL_BAD_ATTRIBUTE"; break;
            case EGL_BAD_CONTEXT:               errortext = "EGL_BAD_CONTEXT"; break;
            case EGL_BAD_CONFIG:                errortext = "EGL_BAD_CONFIG"; break;
            case EGL_BAD_CURRENT_SURFACE:       errortext = "EGL_BAD_CURRENT_SURFACE"; break;
            case EGL_BAD_DISPLAY:               errortext = "EGL_BAD_DISPLAY"; break;
            case EGL_BAD_SURFACE:               errortext = "EGL_BAD_SURFACE"; break;
            case EGL_BAD_MATCH:                 errortext = "EGL_BAD_MATCH"; break;
            case EGL_BAD_PARAMETER:             errortext = "EGL_BAD_PARAMETER"; break;
            case EGL_BAD_NATIVE_PIXMAP:         errortext = "EGL_BAD_NATIVE_PIXMAP"; break;
            case EGL_BAD_NATIVE_WINDOW:         errortext = "EGL_BAD_NATIVE_WINDOW"; break;
            default:                            errortext = "Unknown EGL Error"; break;
        }

        printf( "ERROR: EGL Error detected in file %s at line %d: %s (0x%X)\n", file, line, errortext, error );
        return 1;
    }

    return 0;
}

void Platform_Open( void )
{
#if defined(PANDORA)
    /* Pandora VSync */
    fbdev = open ("/dev/fb0", O_RDONLY /* O_RDWR */ );
    if ( fbdev < 0 ) {
      printf( "EGL Couldn't open /dev/fb0 for vsync\n" );
    }
#endif /* defined(PANDORA) */
}

void Platform_Close( void )
{
#if defined(PANDORA)

    /* Pandora VSync */
    close(fbdev);
    fbdev = -1;
#endif /* defined(PANDORA) */
}

void Platform_VSync( void )
{
#if defined(PANDORA)

    /* Pandora VSync */
    if ( fbdev >= 0 ) {
        int arg = 0;
        ioctl( fbdev, FBIO_WAITFORVSYNC, &arg );
    }
#endif /* defined(PANDORA) */
}
