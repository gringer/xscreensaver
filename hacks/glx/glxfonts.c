/* glxfonts, Copyright (c) 2001-2011 Jamie Zawinski <jwz@jwz.org>
 * Loads X11 fonts for use with OpenGL.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  No representations are made about the suitability of this
 * software for any purpose.  It is provided "as is" without express or 
 * implied warranty.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#ifdef HAVE_COCOA
# include "jwxyz.h"
# include <OpenGL/gl.h>
# include <OpenGL/glu.h>
# include <AGL/agl.h>
#else
# include <GL/glx.h>
# include <GL/glu.h>
#endif

#include "resources.h"
#include "glxfonts.h"

/* These are in xlock-gl.c */
extern void clear_gl_error (void);
extern void check_gl_error (const char *type);

/* screenhack.h */
extern char *progname;


#undef DEBUG  /* Defining this causes check_gl_error() to be called inside
                 time-critical sections, which could slow things down (since
                 it might result in a round-trip, and stall of the pipeline.)
               */


/* Mostly lifted from the Mesa implementation of glXUseXFont(), since
   Mac OS 10.6 no longer supports aglUseFont() which was their analog
   of that.  This code could be in libjwxyz instead, but we might as
   well use the same text-drawing code on both X11 and Cocoa.
 */
static void
fill_bitmap (Display *dpy, Window win, GC gc,
	     unsigned int width, unsigned int height,
	     int x0, int y0, char c, GLubyte *bitmap)
{
  XImage *image;
  int x, y;
  Pixmap pixmap;

  pixmap = XCreatePixmap (dpy, win, 8*width, height, 1);
  XSetForeground(dpy, gc, 0);
  XFillRectangle (dpy, pixmap, gc, 0, 0, 8*width, height);
  XSetForeground(dpy, gc, 1);
  XDrawString (dpy, pixmap, gc, x0, y0, &c, 1);

  image = XGetImage (dpy, pixmap, 0, 0, 8*width, height, 1, XYPixmap);

  /* Fill the bitmap (X11 and OpenGL are upside down wrt each other).  */
  for (y = 0; y < height; y++)
    for (x = 0; x < 8*width; x++)
      if (XGetPixel (image, x, y))
	bitmap[width*(height - y - 1) + x/8] |= (1 << (7 - (x % 8)));
  
  XFreePixmap (dpy, pixmap);
  XDestroyImage (image);
}


#if 0
static void
dump_bitmap (unsigned int width, unsigned int height, GLubyte *bitmap)
{
  int x, y;

  printf ("    ");
  for (x = 0; x < 8*width; x++)
    printf ("%o", 7 - (x % 8));
  putchar ('\n');
  for (y = 0; y < height; y++)
    {
      printf ("%3o:", y);
      for (x = 0; x < 8*width; x++)
        putchar ((bitmap[width*(height - y - 1) + x/8] & (1 << (7 - (x % 8))))
		 ? '#' : '.');
      printf ("   ");
      for (x = 0; x < width; x++)
	printf ("0x%02x, ", bitmap[width*(height - y - 1) + x]);
      putchar ('\n');
    }
}
#endif


void
xscreensaver_glXUseXFont (Display *dpy, Font font, 
                          int first, int count, int listbase)
{
  Window win = RootWindowOfScreen (DefaultScreenOfDisplay (dpy));
  Pixmap pixmap;
  GC gc;
  XGCValues values;
  unsigned long valuemask;

  XFontStruct *fs;

  GLint swapbytes, lsbfirst, rowlength;
  GLint skiprows, skippixels, alignment;

  unsigned int max_width, max_height, max_bm_width, max_bm_height;
  GLubyte *bm;

  int i;

  clear_gl_error ();

  fs = XQueryFont (dpy, font);  
  if (!fs)
    {
      /*gl_error (CC->gl_ctx, GL_INVALID_VALUE,
		"Couldn't get font structure information");*/
      abort();
      return;
    }

  /* Allocate a bitmap that can fit all characters.  */
  max_width = fs->max_bounds.rbearing - fs->min_bounds.lbearing;
  max_height = fs->max_bounds.ascent + fs->max_bounds.descent;
  max_bm_width = (max_width + 7) / 8;
  max_bm_height = max_height;

  bm = (GLubyte *) malloc ((max_bm_width * max_bm_height) * sizeof (GLubyte));
  if (!bm)
    {
      /*gl_error (CC->gl_ctx, GL_OUT_OF_MEMORY,
		"Couldn't allocate bitmap in glXUseXFont()");*/
      abort();
      return;
    }

  /* Save the current packing mode for bitmaps.  */
  glGetIntegerv	(GL_UNPACK_SWAP_BYTES, &swapbytes);
  glGetIntegerv	(GL_UNPACK_LSB_FIRST, &lsbfirst);
  glGetIntegerv	(GL_UNPACK_ROW_LENGTH, &rowlength);
  glGetIntegerv	(GL_UNPACK_SKIP_ROWS, &skiprows);
  glGetIntegerv	(GL_UNPACK_SKIP_PIXELS, &skippixels);
  glGetIntegerv	(GL_UNPACK_ALIGNMENT, &alignment);

  /* Enforce a standard packing mode which is compatible with
     fill_bitmap() from above.  This is actually the default mode,
     except for the (non)alignment.  */
  glPixelStorei	(GL_UNPACK_SWAP_BYTES, GL_FALSE);
  glPixelStorei	(GL_UNPACK_LSB_FIRST, GL_FALSE);
  glPixelStorei	(GL_UNPACK_ROW_LENGTH, 0);
  glPixelStorei	(GL_UNPACK_SKIP_ROWS, 0);
  glPixelStorei	(GL_UNPACK_SKIP_PIXELS, 0);
  glPixelStorei	(GL_UNPACK_ALIGNMENT, 1);

  clear_gl_error(); /* WTF? sometimes "invalid op" from glPixelStorei! */


  pixmap = XCreatePixmap (dpy, win, 10, 10, 1);
  values.foreground = 0;
  values.background = 1;
  values.font = fs->fid;
  valuemask = GCForeground | GCBackground | GCFont;
  gc = XCreateGC (dpy, pixmap, valuemask, &values);
  XFreePixmap (dpy, pixmap);

# ifdef HAVE_COCOA
  /* Anti-aliasing of fonts looks like crap with 1-bit bitmaps.
     It would be nice if we were using full-depth bitmaps, so
     that the text showed up anti-aliased on screen, but
     glBitmap() doesn't work that way. */
  jwxyz_XSetAntiAliasing (dpy, gc, False);
# endif

  for (i = 0; i < count; i++)
    {
      unsigned int width, height, bm_width, bm_height;
      GLfloat x0, y0, dx, dy;
      XCharStruct *ch;
      int x, y;
      int c = first + i;
      int list = listbase + i;

      if (fs->per_char
	  && (c >= fs->min_char_or_byte2) && (c <= fs->max_char_or_byte2))
	ch = &fs->per_char[c-fs->min_char_or_byte2];
      else
	ch = &fs->max_bounds;

      /* I'm not entirely clear on why this is necessary on OSX, but
         without it, the characters are clipped.  And it does not hurt
         under real X11.  -- jwz. */
      ch->lbearing--;
      ch->ascent++;

      /* glBitmap()'s parameters:
         "Bitmap parameters xorig, yorig, width, and height are
         computed from font metrics as descent-1, -lbearing,
         rbearing-lbearing, and ascent+descent, respectively. 
         xmove is taken from the glyph's width metric, and 
         ymove is set to zero. Finally, the glyph's image is 
         converted to the appropriate format for glBitmap."
      */
      width = ch->rbearing - ch->lbearing;
      height = ch->ascent + ch->descent;
      x0 = - ch->lbearing;
      y0 = ch->descent - 1;
      dx = ch->width;
      dy = 0;

      /* X11's starting point.  */
      x = - ch->lbearing;
      y = ch->ascent;
      
      /* Round the width to a multiple of eight.  We will use this also
	 for the pixmap for capturing the X11 font.  This is slightly
	 inefficient, but it makes the OpenGL part real easy.  */
      bm_width = (width + 7) / 8;
      bm_height = height;

      glNewList (list, GL_COMPILE);
        if ((c >= fs->min_char_or_byte2) && (c <= fs->max_char_or_byte2)
	    && (bm_width > 0) && (bm_height > 0))
	  {
	    memset (bm, '\0', bm_width * bm_height);
	    fill_bitmap (dpy, win, gc, bm_width, bm_height, x, y, c, bm);
	    glBitmap (width, height, x0, y0, dx, dy, bm);
#if 0
            printf ("width/height = %d/%d\n", width, height);
            printf ("bm_width/bm_height = %d/%d\n", bm_width, bm_height);
            dump_bitmap (bm_width, bm_height, bm);
#endif
	  }
	else
	  glBitmap (0, 0, 0.0, 0.0, dx, dy, NULL);
      glEndList ();
    }

  free (bm);
  XFreeFontInfo( NULL, fs, 0 );
  XFreeGC (dpy, gc);

  /* Restore saved packing modes.  */    
  glPixelStorei(GL_UNPACK_SWAP_BYTES, swapbytes);
  glPixelStorei(GL_UNPACK_LSB_FIRST, lsbfirst);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, rowlength);
  glPixelStorei(GL_UNPACK_SKIP_ROWS, skiprows);
  glPixelStorei(GL_UNPACK_SKIP_PIXELS, skippixels);
  glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);

  check_gl_error ("xscreensaver_glXUseXFont");
}



/* Loads the font named by the X resource "res".
   Returns an XFontStruct.
   Also converts the font to a set of GL lists and returns the first list.
*/
void
load_font (Display *dpy, char *res, XFontStruct **font_ret, GLuint *dlist_ret)
{
  XFontStruct *f;

  const char *font = get_string_resource (dpy, res, "Font");
  const char *def1 = "-*-helvetica-medium-r-normal-*-180-*";
  const char *def2 = "fixed";
  Font id;
  int first, last;

  if (!res || !*res) abort();
  if (!font_ret && !dlist_ret) abort();

  if (!font) font = def1;

  f = XLoadQueryFont(dpy, font);
  if (!f && !!strcmp (font, def1))
    {
      fprintf (stderr, "%s: unable to load font \"%s\", using \"%s\"\n",
               progname, font, def1);
      font = def1;
      f = XLoadQueryFont(dpy, font);
    }

  if (!f && !!strcmp (font, def2))
    {
      fprintf (stderr, "%s: unable to load font \"%s\", using \"%s\"\n",
               progname, font, def2);
      font = def2;
      f = XLoadQueryFont(dpy, font);
    }

  if (!f)
    {
      fprintf (stderr, "%s: unable to load fallback font \"%s\" either!\n",
               progname, font);
      exit (1);
    }

  id = f->fid;
  first = f->min_char_or_byte2;
  last = f->max_char_or_byte2;
  

  if (dlist_ret)
    {
      clear_gl_error ();
      *dlist_ret = glGenLists ((GLuint) last+1);
      check_gl_error ("glGenLists");
      xscreensaver_glXUseXFont (dpy, id, first, last-first+1,
                                *dlist_ret + first);
    }

  if (font_ret)
    *font_ret = f;
}


/* Width (and optionally height) of the string in pixels.
 */
int
string_width (XFontStruct *f, const char *c, int *height_ret)
{
  int x = 0;
  int max_w = 0;
  int h = f->ascent + f->descent;
  while (*c)
    {
      int cc = *((unsigned char *) c);
      if (*c == '\n')
        {
          if (x > max_w) max_w = x;
          x = 0;
          h += f->ascent + f->descent;
        }
      else
        x += (f->per_char
              ? f->per_char[cc-f->min_char_or_byte2].width
              : f->min_bounds.rbearing);
      c++;
    }
  if (x > max_w) max_w = x;
  if (height_ret) *height_ret = h;

  return max_w;
}


/* Draws the string on the window at the given pixel position.
   Newlines and tab stops are honored.
   Any text inside [] will be rendered as a subscript.
   Assumes the font has been loaded as with load_font().
 */
void
print_gl_string (Display *dpy,
                 XFontStruct *font,
                 GLuint font_dlist,
                 int window_width, int window_height,
                 GLfloat x, GLfloat y,
                 const char *string,
                 Bool clear_background_p)
{
  GLfloat line_height = font->ascent + font->descent;
  GLfloat sub_shift = (line_height * 0.3);
  int cw = string_width (font, "m", 0);
  int tabs = cw * 7;

  y -= line_height;

  /* Sadly, this causes a stall of the graphics pipeline (as would the
     equivalent calls to glGet*.)  But there's no way around this, short
     of having each caller set up the specific display matrix we need
     here, which would kind of defeat the purpose of centralizing this
     code in one file.
   */
  glPushAttrib (GL_TRANSFORM_BIT |  /* for matrix contents */
                GL_ENABLE_BIT |     /* for various glDisable calls */
                GL_CURRENT_BIT |    /* for glColor3f() */
                GL_LIST_BIT);       /* for glListBase() */
  {
# ifdef DEBUG
    check_gl_error ("glPushAttrib");
# endif

    /* disable lighting and texturing when drawing bitmaps!
       (glPopAttrib() restores these.)
     */
    glDisable (GL_TEXTURE_2D);
    glDisable (GL_LIGHTING);
    glDisable (GL_BLEND);
    glDisable (GL_DEPTH_TEST);
    glDisable (GL_CULL_FACE);

    /* glPopAttrib() does not restore matrix changes, so we must
       push/pop the matrix stacks to be non-intrusive there.
     */
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    {
# ifdef DEBUG
      check_gl_error ("glPushMatrix");
# endif
      glLoadIdentity();

      /* Each matrix mode has its own stack, so we need to push/pop
         them separately. */
      glMatrixMode(GL_MODELVIEW);
      glPushMatrix();
      {
        unsigned int i;
        int x2 = x;
        Bool sub_p = False;

# ifdef DEBUG
        check_gl_error ("glPushMatrix");
# endif

        glLoadIdentity();
        gluOrtho2D (0, window_width, 0, window_height);
# ifdef DEBUG
        check_gl_error ("gluOrtho2D");
# endif

        if (clear_background_p)
          {
            int w, h;
            int lh = font->ascent + font->descent;
            w = string_width (font, string, &h);
            glColor3f (0, 0, 0);
            glRecti (x - font->descent,
                     y + lh, 
                     x + w + 2*font->descent,
                     y + lh - h - font->descent);
            glColor3f (1, 1, 1);
          }

        /* draw the text */
        glRasterPos2f (x, y);
/*        glListBase (font_dlist);*/
        for (i = 0; i < strlen(string); i++)
          {
            unsigned char c = (unsigned char) string[i];
            if (c == '\n')
              {
                glRasterPos2f (x, (y -= line_height));
                x2 = x;
              }
            else if (c == '\t')
              {
                x2 -= x;
                x2 = ((x2 + tabs) / tabs) * tabs;  /* tab to tab stop */
                x2 += x;
                glRasterPos2f (x2, y);
              }
            else if (c == '[' && (isdigit (string[i+1])))
              {
                sub_p = True;
                glRasterPos2f (x2, (y -= sub_shift));
              }
            else if (c == ']' && sub_p)
              {
                sub_p = False;
                glRasterPos2f (x2, (y += sub_shift));
              }
            else
              {
/*            glCallLists (s - string, GL_UNSIGNED_BYTE, string);*/
                glCallList (font_dlist + (int)(c));
                x2 += (font->per_char
                       ? font->per_char[c - font->min_char_or_byte2].width
                       : font->min_bounds.width);
              }
          }
# ifdef DEBUG
        check_gl_error ("print_gl_string");
# endif
      }
      glPopMatrix();
    }
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
  }
  glPopAttrib();
# ifdef DEBUG
  check_gl_error ("glPopAttrib");
# endif

  glMatrixMode(GL_MODELVIEW);
}
