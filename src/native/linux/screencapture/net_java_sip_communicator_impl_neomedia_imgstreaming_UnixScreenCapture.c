/*
 * SIP Communicator, the OpenSource Java VoIP and Instant Messaging client.
 *
 * Distributable under LGPL license.
 * See terms of license at gnu.org.
 */

/**
 * \file net_java_sip_communicator_impl_neomedia_imgstreaming_UnixScreenCapture.c
 * \brief X11 screen capture.
 * \author Sebastien Vincent
 * \date 2009
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>

#include "net_java_sip_communicator_impl_neomedia_imgstreaming_UnixScreenCapture.h" 

/**
 * \brief Grab X11 screen.
 * \param x11display display string (i.e. :0.0), if NULL getenv("DISPLAY") is used
 * \param x x position to start capture
 * \param y y position to start capture
 * \param w capture width
 * \param h capture height 
 * \return array of integers which will contains screen capture
 * (ARGB format) or NULL if failure. It needs to be freed by caller
 * \note Return pointer if not NULL needs to be freed by caller
 */
static int32_t* x11_grab_screen(const char* x11display, int32_t x, int32_t y, int32_t w, int32_t h)
{
  const char* display_str; /* display string */
  Display* display = NULL; /* X11 display */
  Visual* visual = NULL;
  int screen = 0; /* X11 screen */
  Window root_window = 0; /* X11 root window of a screen */
  int width = 0;
  int height = 0;
  int depth = 0;
  int shm_support = 0;
  XImage* img = NULL;
  XShmSegmentInfo shm_info;
  int32_t* data = NULL;
  size_t off = 0;
  int i = 0;
  int j = 0;
  size_t size = 0;

  display_str = x11display ? x11display : getenv("DISPLAY");

  if(!display_str)
  {
    /* fprintf(stderr, "No display!\n"); */
    return NULL;
  }

  /* open current X11 display */
  display = XOpenDisplay(display_str);

  if(!display)
  {
    /* fprintf(stderr, "Cannot open X11 display!\n"); */
    return NULL;
  }
  
  screen = DefaultScreen(display);
  root_window = RootWindow(display, screen);
  visual = DefaultVisual(display, screen);
  width = DisplayWidth(display, screen);
  height = DisplayHeight(display, screen);
  depth = DefaultDepth(display, screen);

  /* check that user-defined parameters are in image */
  if((w + x) > width || (h + y) > height)
  {
    XCloseDisplay(display);
    return NULL;
  }

  size = w * h;

  /* test is XServer support SHM */
  shm_support = XShmQueryExtension(display);

  /* fprintf(stdout, "Display=%s width=%d height=%d depth=%d SHM=%s\n", display_str, width, height, depth, shm_support ? "true" : "false"); */

  if(shm_support)
  {
    /* fprintf(stdout, "Use XShmGetImage\n"); */

    /* create image for SHM use */
    img = XShmCreateImage(display, visual, depth, ZPixmap, NULL, &shm_info, w, h);

    if(!img)
    {
      /* fprintf(stderr, "Image cannot be created!\n"); */
      XCloseDisplay(display);
      return NULL;
    }

    /* setup SHM stuff */
    shm_info.shmid = shmget(IPC_PRIVATE, img->bytes_per_line * img->height, IPC_CREAT | 0777);
    shm_info.shmaddr = (char*)shmat(shm_info.shmid, NULL, 0);
    img->data = shm_info.shmaddr;
    shmctl(shm_info.shmid, IPC_RMID, NULL);
    shm_info.readOnly = 0;

    if((shm_info.shmaddr == (void*)-1) || !XShmAttach(display, &shm_info))
    {
      /* fprintf(stderr, "Cannot use shared memory!\n"); */
      XCloseDisplay(display);
      return NULL;
    }
    
    /* grab screen */
    if(!XShmGetImage(display, root_window, img, x, y, 0xffffffff))
    {
      /* fprintf(stderr, "Cannot grab image!\n"); */
      XShmDetach(display, &shm_info);
      shmdt(shm_info.shmaddr);
      XCloseDisplay(display);
      return NULL;
    }
  }
  else
  {
    /* fprintf(stdout, "Use XGetImage\n"); */
    /* no SHM */
    img = XGetImage(display, root_window, x, y, w, h, 0xffffffff, ZPixmap);

    if(!img)
    {
      /* fprintf(stderr, "Cannot grab image!\n"); */
      XCloseDisplay(display);
      return NULL;
    }
  }

  data = malloc(sizeof(int32_t) * size);

  if(!data)
  {
    XDestroyImage(img);

    if(shm_support)
    {
      XShmDetach(display, &shm_info);
      shmdt(shm_info.shmaddr);
    }

    XCloseDisplay(display);
    return NULL;
  }

  /* convert to Java ARGB */
  for(j = 0 ; j < h ; j++)
  {
    for(i = 0 ; i < w ; i++)
    {
      /* do not care about hight 32-bit for Linux 64 bit 
       * machine (sizeof(unsigned long) = 8)
       */
      uint32_t pixel = (uint32_t)XGetPixel(img, i, j);

      pixel |= 0xff000000; /* ARGB */
      data[off++] = pixel;
    }
  }

  /* free X11 resources and close display */
  XDestroyImage(img);
  
  if(shm_support)
  {
    XShmDetach(display, &shm_info);
    shmdt(shm_info.shmaddr);
  }

  XCloseDisplay(display);

  /* return array */
  return data;
}

/**
 * \brief JNI native method to grab desktop screen and retrieve ARGB pixels.
 * \param env JVM environment
 * \param obj UnixScreenCapture Java object
 * \param x x position to start capture
 * \param y y position to start capture
 * \param width capture width
 * \param height capture height
 * \return array of ARGB pixels (jint)
 */
JNIEXPORT jintArray JNICALL Java_net_java_sip_communicator_impl_neomedia_imgstreaming_UnixScreenCapture_grabScreen
  (JNIEnv* env, jobject obj, jint x, jint y, jint width, jint height)
{
  int32_t* data = NULL; /* jint is always four-bytes signed integer */
  size_t size = width * height;
  jintArray ret = NULL;

  obj = obj; /* not used */

  data = x11_grab_screen(NULL, x, y, width, height);

  if(!data)
  {
    return 0;
  }

  ret = (*env)->NewIntArray(env, size);

  /* updates array with data's content */
  (*env)->SetIntArrayRegion(env, ret, 0, size, data);
  free(data);

  return ret;
}

