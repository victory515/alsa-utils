/* AlsaMixer - Commandline mixer for the ALSA project
 * Copyright (C) 1998, 1999 Tim Janik <timj@gtk.org> and Jaroslav Kysela <perex@jcu.cz>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 *
 *
 * ChangeLog:
 *
 * Sun Feb 21 19:55:01 1999  Tim Janik  <timj@gtk.org>
 *
 *	* bumped version to 0.10.
 *
 *	* added scrollable text views.
 *	we now feature an F1 Help screen and an F2 /proc info screen.
 *	the help screen does still require lots of work though.
 *
 *	* keys are evaluated view specific now.
 *
 *	* we feature meta-keys now, e.g. M-Tab as back-tab.
 *
 *	* if we are already in channel view and the user still hits Return,
 *	we do a refresh nonetheless, since 'r'/'R' got removed as a redraw
 *	key (reserved for record volumes). 'l'/'L' is still preserved though,
 *	and actually needs to be to e.g. get around the xterm bold-artefacts.
 *
 *	* support terminals that can't write into lower right corner.
 *
 *	* undocumented '-s' option that will keep the screen to its
 *	minimum size, usefull for debugging only.
 *
 * Sun Feb 21 02:23:52 1999  Tim Janik  <timj@gtk.org>
 *
 *	* don't abort if snd_mixer_* functions failed due to EINTR,
 *	we simply retry on the next cycle. hopefully asoundlib preserves
 *	errno states correctly (Jaroslav can you asure that?).
 *
 *	* feature WINCH correctly, so we make a complete relayout on
 *	screen resizes. don't abort on too-small screen sizes anymore,
 *	but simply beep.
 *
 *	* redid the layout algorithm to fix some bugs and to preserve
 *	space for a flag indication line. the channels are
 *	nicer spread horizontally now (i.e. we also pad on the left and
 *	right screen bounds now).
 *
 *	* various other minor fixes.
 *
 *	* indicate whether ExactMode is active or not.
 *
 *	* fixed coding style to follow the GNU coding conventions.
 *
 *	* reverted record volume changes since they broke ExactMode display.
 *
 *	* composed ChangeLog entries.
 *
 * 1998/11/04 19:43:45  perex
 *
 *	* Stereo record source and route selection...
 *	provided by Carl van Schaik <carl@dreamcoat.che.uct.ac.za>.
 *
 * 1998/09/20 08:05:24  perex
 *
 *	* Fixed -m option...
 *
 * 1998/10/29 22:50:10
 *
 *	* initial checkin of alsamixer.c, written by Tim Janik, modified by
 *	Jaroslav Kysela to feature asoundlib.h instead of plain ioctl()s and
 *	automated updates after select() (i always missed that with OSS!).
 */

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <errno.h>

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/signal.h>

#ifndef CURSESINC
#include <ncurses.h>
#else
#include CURSESINC
#endif
#include <time.h>

#include <sys/asoundlib.h>

/* example compilation commandline:
 * clear; gcc -Wall -pipe -O2 alsamixer.c -o alsamixer -lasound -lncurses
 */

/* --- defines --- */
#define	PRGNAME		 "alsamixer"
#define	PRGNAME_UPPER	 "AlsaMixer"
#define	VERSION		 "v0.10"
#define	CHECK_ABORT(e,s) ({ if (errno != EINTR) mixer_abort ((e), (s)); })
#define GETCH_BLOCK(w)	 ({ timeout ((w) ? -1 : 0); })

#undef MAX
#define MAX(a, b)  (((a) > (b)) ? (a) : (b))
#undef MIN
#define MIN(a, b)  (((a) < (b)) ? (a) : (b))
#undef ABS
#define ABS(a)     (((a) < 0) ? -(a) : (a))
#undef CLAMP
#define CLAMP(x, low, high)  (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))

#define MIXER_MIN_X	(18)			/* abs minimum: 18 */
#define	MIXER_TEXT_Y	(10)
#define	MIXER_MIN_Y	(MIXER_TEXT_Y + 3)	/* abs minimum: 11 */

#define MIXER_BLACK	(COLOR_BLACK)
#define MIXER_DARK_RED  (COLOR_RED)
#define MIXER_RED       (COLOR_RED | A_BOLD)
#define MIXER_GREEN     (COLOR_GREEN | A_BOLD)
#define MIXER_ORANGE    (COLOR_YELLOW)
#define MIXER_YELLOW    (COLOR_YELLOW | A_BOLD)
#define MIXER_MARIN     (COLOR_BLUE)
#define MIXER_BLUE      (COLOR_BLUE | A_BOLD)
#define MIXER_MAGENTA   (COLOR_MAGENTA)
#define MIXER_DARK_CYAN (COLOR_CYAN)
#define MIXER_CYAN      (COLOR_CYAN | A_BOLD)
#define MIXER_GREY      (COLOR_WHITE)
#define MIXER_GRAY      (MIXER_GREY)
#define MIXER_WHITE     (COLOR_WHITE | A_BOLD)


/* --- views --- */
enum {
  VIEW_CHANNELS,
  VIEW_HELP,
  VIEW_PROCINFO
};


/* --- variables --- */
static WINDOW	*mixer_window = NULL;
static int	 mixer_needs_resize = 0;
static int	 mixer_minimize = 0;
static int	 mixer_no_lrcorner = 0;
static int	 mixer_view = VIEW_CHANNELS;
static int	 mixer_max_x = 0;
static int	 mixer_max_y = 0;
static int	 mixer_ofs_x = 0;
static float	 mixer_extra_space = 0;
static int	 mixer_cbar_height = 0;

static int	 card_id = 0;
static int	 mixer_id = 0;
static void	*mixer_handle;
static char	*mixer_card_name = NULL;
static char	*mixer_device_name = NULL;

static int	 mixer_n_channels = 0;
static int	 mixer_n_vis_channels = 0;
static int	 mixer_first_vis_channel = 0;
static int	 mixer_focus_channel = 0;
static int	 mixer_have_old_focus = 0;
static int	 mixer_exact = 0;

static int	 mixer_lvolume_delta = 0;
static int	 mixer_rvolume_delta = 0;
static int	 mixer_balance_volumes = 0;
static int	 mixer_toggle_mute_left = 0;
static int	 mixer_toggle_mute_right = 0;
static int	 mixer_toggle_record = 0;

static int	 mixer_hscroll_delta = 0;
static int	 mixer_vscroll_delta = 0;

/* By Carl */
static int	 mixer_toggle_rec_left = 0;
static int	 mixer_toggle_rec_right = 0;
static int	 mixer_route_ltor_in = 0;
static int	 mixer_route_rtol_in = 0;


/* --- text --- */
static int	 mixer_procinfo_xoffs = 0;
static int	 mixer_procinfo_yoffs = 0;
static int	 mixer_help_xoffs = 0;
static int	 mixer_help_yoffs = 0;
static char     *mixer_help_text =
(
 "\n"
 " Esc     exit alsamixer\n"
 " F1      show Help screen\n"
 " F2      show /proc info screen\n"
 " Return  return to main screen\n"
 " Space   toggle Record facility\n"
 " Tab     toggle ExactMode\n"
 " m M     mute both channels\n"
 " < >     mute left/right channel\n"
 " Up      increase left and right volume\n"
 " Down    decrease left and right volume\n"
 " Right   move (scroll) to the right next channel\n"
 " Left    move (scroll) to the left next channel\n"
 "\n"
 "Alsamixer has been written and is Copyrighted in 1998, 1999 by\n"
 "Tim Janik <timj@gtk.org> and Jaroslav Kysela <perex@jcu.cz>.\n"
 );


/* --- draw contexts --- */
enum {
  DC_DEFAULT,
  DC_BACK,
  DC_TEXT,
  DC_PROMPT,
  DC_CBAR_MUTE,
  DC_CBAR_NOMUTE,
  DC_CBAR_RECORD,
  DC_CBAR_NORECORD,
  DC_CBAR_EMPTY,
  DC_CBAR_LABEL,
  DC_CBAR_FOCUS_LABEL,
  DC_FOCUS,
  DC_ANY_1,
  DC_ANY_2,
  DC_ANY_3,
  DC_ANY_4,
  DC_LAST
};

static int dc_fg[DC_LAST] = { 0 };
static int dc_attrib[DC_LAST] = { 0 };
static int dc_char[DC_LAST] = { 0 };
static int mixer_do_color = 1;

static void
mixer_init_dc (int c,
	       int n,
	       int f,
	       int b,
	       int a)
{
  dc_fg[n] = f;
  dc_attrib[n] = a;
  dc_char[n] = c;
  if (n > 0)
    init_pair (n, dc_fg[n] & 0xf, b & 0x0f);
}

static int
mixer_dc (int n)
{
  if (mixer_do_color)
    attrset (COLOR_PAIR (n) | (dc_fg[n] & 0xfffffff0));
  else
    attrset (dc_attrib[n]);
  
  return dc_char[n];
}

static void
mixer_init_draw_contexts (void)
{
  start_color ();
  
  mixer_init_dc ('.', DC_BACK, MIXER_WHITE, MIXER_BLACK, A_NORMAL);
  mixer_init_dc ('.', DC_TEXT, MIXER_YELLOW, MIXER_BLACK, A_BOLD);
  mixer_init_dc ('.', DC_PROMPT, MIXER_DARK_CYAN, MIXER_BLACK, A_NORMAL);
  mixer_init_dc ('M', DC_CBAR_MUTE, MIXER_CYAN, MIXER_BLACK, A_BOLD);
  mixer_init_dc ('-', DC_CBAR_NOMUTE, MIXER_CYAN, MIXER_BLACK, A_NORMAL);
  mixer_init_dc ('x', DC_CBAR_RECORD, MIXER_DARK_RED, MIXER_BLACK, A_BOLD);
  mixer_init_dc ('-', DC_CBAR_NORECORD, MIXER_GRAY, MIXER_BLACK, A_NORMAL);
  mixer_init_dc (' ', DC_CBAR_EMPTY, MIXER_GRAY, MIXER_BLACK, A_DIM);
  mixer_init_dc ('.', DC_CBAR_LABEL, MIXER_WHITE, MIXER_BLUE, A_REVERSE | A_BOLD);
  mixer_init_dc ('.', DC_CBAR_FOCUS_LABEL, MIXER_RED, MIXER_BLUE, A_REVERSE | A_BOLD);
  mixer_init_dc ('.', DC_FOCUS, MIXER_RED, MIXER_BLACK, A_BOLD);
  mixer_init_dc ('#', DC_ANY_1, MIXER_WHITE, MIXER_BLACK, A_BOLD);
  mixer_init_dc ('#', DC_ANY_2, MIXER_GREEN, MIXER_BLACK, A_BOLD);
  mixer_init_dc ('#', DC_ANY_3, MIXER_RED, MIXER_BLACK, A_BOLD);
  mixer_init_dc ('.', DC_ANY_4, MIXER_WHITE, MIXER_GREEN, A_BOLD);
  mixer_init_dc ('.', DC_ANY_4, MIXER_WHITE, MIXER_BLUE, A_BOLD);
}

#define	DC_CBAR_FRAME	(DC_CBAR_MUTE)
#define	DC_FRAME	(DC_PROMPT)


/* --- error types --- */
typedef enum
{
  ERR_NONE,
  ERR_OPEN,
  ERR_FCN,
  ERR_SIGNAL,
  ERR_WINSIZE,
} ErrType;


/* --- prototypes --- */
static void
mixer_abort (ErrType error,
	     const char *err_string)
     __attribute__
((noreturn));


/* --- functions --- */
static void
mixer_clear (int full_redraw)
{
  int x, y;
  int f = full_redraw ? 0 : 1;

  mixer_dc (DC_BACK);

  if (full_redraw)
    clearok (mixer_window, TRUE);

  /* buggy ncurses doesn't really write spaces with the specified
   * color into the screen on clear () or erase ()
   */
  for (x = f; x < mixer_max_x - f; x++)
    for (y = f; y < mixer_max_y - f; y++)
      mvaddch (y, x, ' ');
}

static void
mixer_abort (ErrType     error,
	     const char *err_string)
{
  if (mixer_window)
    {
      mixer_clear (TRUE);
      refresh ();
      keypad (mixer_window, FALSE);
      leaveok (mixer_window, FALSE);
      endwin ();
      mixer_window = NULL;
    }
  printf ("\n");
  
  switch (error)
    {
    case ERR_OPEN:
      fprintf (stderr,
	       PRGNAME ": failed to open mixer #%i/#%i: %s\n",
	       card_id,
	       mixer_id,
	       snd_strerror (errno));
      break;
    case ERR_FCN:
      fprintf (stderr,
	       PRGNAME ": function %s failed: %s\n",
	       err_string,
	       snd_strerror (errno));
      break;
    case ERR_SIGNAL:
      fprintf (stderr,
	       PRGNAME ": aborting due to signal `%s'\n",
	       err_string);
      break;
    case ERR_WINSIZE:
      fprintf (stderr,
	       PRGNAME ": screen size too small (%dx%d)\n",
	       mixer_max_x,
	       mixer_max_y);
      break;
    default:
      break;
    }
  
  exit (error);
}

static int
mixer_cbar_get_pos (int  channel_index,
		    int *x_p,
		    int *y_p)
{
  int x;
  int y;
  
  if (channel_index < mixer_first_vis_channel ||
      channel_index - mixer_first_vis_channel >= mixer_n_vis_channels)
    return FALSE;
  
  channel_index -= mixer_first_vis_channel;
  
  x = mixer_ofs_x;
  x += (3 + 2 + 3 + 1) * channel_index + mixer_extra_space * (channel_index + 1);

  if (MIXER_TEXT_Y + 10 < mixer_max_y)
    y = mixer_max_y / 2 + 3;
  else
    y = (mixer_max_y + 1) / 2 + 3;
  y += mixer_cbar_height / 2;
  
  if (x_p)
    *x_p = x;
  if (y_p)
    *y_p = y;
  
  return TRUE;
}

static void
mixer_update_cbar (int channel_index)
{
  char string[64];
  char c;
  snd_mixer_channel_info_t cinfo = { 0, };
  snd_mixer_channel_t cdata = { 0, };
  int vleft, vright;
  int x, y, i;
  
  
  /* set specified EXACT mode
   */
  if (snd_mixer_exact_mode (mixer_handle, mixer_exact) < 0)
    CHECK_ABORT (ERR_FCN, "snd_mixer_exact_mode()");
  
  /* set new channel indices and read info
   */
  if (snd_mixer_channel_info (mixer_handle, channel_index, &cinfo) < 0)
    CHECK_ABORT (ERR_FCN, "snd_mixer_channel_info()");
  
  /* set new channel values
   */
  if (channel_index == mixer_focus_channel &&
      (mixer_lvolume_delta || mixer_rvolume_delta ||
       mixer_toggle_mute_left || mixer_toggle_mute_right ||
       mixer_balance_volumes ||
       mixer_toggle_record || mixer_toggle_rec_left ||
       mixer_toggle_rec_right ||
       mixer_route_rtol_in || mixer_route_ltor_in))
    {
      if (snd_mixer_channel_read (mixer_handle, channel_index, &cdata) < 0)
	CHECK_ABORT (ERR_FCN, "snd_mixer_channel_read()");
      
      cdata.flags &= ~SND_MIXER_FLG_DECIBEL;
      cdata.left = CLAMP (cdata.left + mixer_lvolume_delta, cinfo.min, cinfo.max);
      cdata.right = CLAMP (cdata.right + mixer_rvolume_delta, cinfo.min, cinfo.max);
      mixer_lvolume_delta = mixer_rvolume_delta = 0;
      if (mixer_balance_volumes)
	{
	  cdata.left = (cdata.left + cdata.right) / 2;
	  cdata.right = cdata.left;
	  mixer_balance_volumes = 0;
	}
      if (mixer_toggle_mute_left)
	{
	  if (cdata.flags & SND_MIXER_FLG_MUTE_LEFT)
	    cdata.flags &= ~SND_MIXER_FLG_MUTE_LEFT;
	  else
	    cdata.flags |= SND_MIXER_FLG_MUTE_LEFT;
	}
      if (mixer_toggle_mute_right)
	{
	  if (cdata.flags & SND_MIXER_FLG_MUTE_RIGHT)
	    cdata.flags &= ~SND_MIXER_FLG_MUTE_RIGHT;
	  else
	    cdata.flags |= SND_MIXER_FLG_MUTE_RIGHT;
	}
      mixer_toggle_mute_left = mixer_toggle_mute_right = 0;
      if (mixer_toggle_record)
	{
	  if (cdata.flags & SND_MIXER_FLG_RECORD)
	    cdata.flags &= ~SND_MIXER_FLG_RECORD;
	  else
	    cdata.flags |= SND_MIXER_FLG_RECORD;
	}
      mixer_toggle_record = 0;
      
      if (mixer_toggle_rec_left)
	{
	  if (cdata.flags & SND_MIXER_FLG_RECORD_LEFT)
	    cdata.flags &= ~SND_MIXER_FLG_RECORD_LEFT;
	  else
	    cdata.flags |= SND_MIXER_FLG_RECORD_LEFT;
	}
      mixer_toggle_rec_left = 0;
      
      if (mixer_toggle_rec_right)
	{
	  if (cdata.flags & SND_MIXER_FLG_RECORD_RIGHT)
	    cdata.flags &= ~SND_MIXER_FLG_RECORD_RIGHT;
	  else
	    cdata.flags |= SND_MIXER_FLG_RECORD_RIGHT;
	}
      mixer_toggle_rec_right = 0;
      
      if (mixer_route_ltor_in)
	{
	  if (cdata.flags & SND_MIXER_FLG_LTOR_IN)
	    cdata.flags &= ~SND_MIXER_FLG_LTOR_IN;
	  else
	    cdata.flags |= SND_MIXER_FLG_LTOR_IN;
	  /*      printf ("state : \n %d \n",cdata.flags & SND_MIXER_FLG_LTOR_IN);
	   */ 
	}
      mixer_route_ltor_in = 0;
      
      if (mixer_route_rtol_in)
	{
	  if (cdata.flags & SND_MIXER_FLG_RTOL_IN)
	    cdata.flags &= ~SND_MIXER_FLG_RTOL_IN;
	  else
	    cdata.flags |= SND_MIXER_FLG_RTOL_IN;
	}
      mixer_route_rtol_in = 0;
      
      if (snd_mixer_channel_write (mixer_handle, channel_index, &cdata) < 0)
	CHECK_ABORT (ERR_FCN, "snd_mixer_channel_write()");
    }
  /* first, read values for the numbers to be displayed in
   * specified EXACT mode
   */
  if (snd_mixer_channel_read (mixer_handle, channel_index, &cdata) < 0)
    CHECK_ABORT (ERR_FCN, "snd_mixer_channel_read()");
  vleft = cdata.left;
  vright = cdata.right;
  
  /* then, always use percentage values for the bars. if we don't do
   * this, we will see aliasing effects on specific circumstances.
   * (actually they don't really dissapear, but they are transfered
   *  to bar<->smaller-scale ambiguities).
   */
  if (mixer_exact)
    {
      i = 0;
      if (snd_mixer_exact_mode (mixer_handle, 0) < 0)
	CHECK_ABORT (ERR_FCN, "snd_mixer_exact_mode()");
      if (snd_mixer_channel_read (mixer_handle, channel_index, &cdata) < 0)
	CHECK_ABORT (ERR_FCN, "snd_mixer_channel_read()");
    }
  /* get channel bar position
   */
  if (!mixer_cbar_get_pos (channel_index, &x, &y))
    return;

  /* setup colors
   */
  mixer_init_dc ('#', DC_ANY_1, MIXER_WHITE, MIXER_BLACK, A_BOLD);
  mixer_init_dc ('#', DC_ANY_2, MIXER_GREEN, MIXER_BLACK, A_BOLD);
  mixer_init_dc ('#', DC_ANY_3, MIXER_RED, MIXER_BLACK, A_BOLD);
  
  /* channel bar name
   */
  mixer_dc (channel_index == mixer_focus_channel ? DC_CBAR_FOCUS_LABEL : DC_CBAR_LABEL);
  cinfo.name[8] = 0;
  for (i = 0; i < 8; i++)
    {
      string[i] = ' ';
    }
  sprintf (string + (8 - strlen (cinfo.name)) / 2, "%s          ", cinfo.name);
  string[8] = 0;
  mvaddstr (y, x, string);
  y--;
  
  /* current channel values
   */
  mixer_dc (DC_BACK);
  mvaddstr (y, x, "         ");
  mixer_dc (DC_TEXT);
  sprintf (string, "%d", vleft);
  mvaddstr (y, x + 3 - strlen (string), string);
  mixer_dc (DC_CBAR_FRAME);
  mvaddch (y, x + 3, '<');
  mvaddch (y, x + 4, '>');
  mixer_dc (DC_TEXT);
  sprintf (string, "%d", vright);
  mvaddstr (y, x + 5, string);
  y--;
  
  /* left/right bar
   */
  mixer_dc (DC_CBAR_FRAME);
  mvaddstr (y, x, "         ");
  mvaddch (y, x + 2, ACS_LLCORNER);
  mvaddch (y, x + 3, ACS_HLINE);
  mvaddch (y, x + 4, ACS_HLINE);
  mvaddch (y, x + 5, ACS_LRCORNER);
  y--;
  for (i = 0; i < mixer_cbar_height; i++)
    {
      mvaddstr (y - i, x, "         ");
      mvaddch (y - i, x + 2, ACS_VLINE);
      mvaddch (y - i, x + 5, ACS_VLINE);
    }
  string[2] = 0;
  for (i = 0; i < mixer_cbar_height; i++)
    {
      int dc;
      
      if (i + 1 >= 0.8 * mixer_cbar_height)
	dc = DC_ANY_3;
      else if (i + 1 >= 0.4 * mixer_cbar_height)
	dc = DC_ANY_2;
      else
	dc = DC_ANY_1;
      mvaddch (y, x + 3, mixer_dc (cdata.left > i * 100 / mixer_cbar_height ? dc : DC_CBAR_EMPTY));
      mvaddch (y, x + 4, mixer_dc (cdata.right > i * 100 / mixer_cbar_height ? dc : DC_CBAR_EMPTY));
      y--;
    }
  
  /* muted?
   */
  mixer_dc (DC_BACK);
  mvaddstr (y, x, "         ");
  c = cinfo.caps & SND_MIXER_CINFO_CAP_MUTE ? '-' : ' ';
  mixer_dc (DC_CBAR_FRAME);
  mvaddch (y, x + 2, ACS_ULCORNER);
  mvaddch (y, x + 3, mixer_dc (cdata.flags & SND_MIXER_FLG_MUTE_LEFT ?
			       DC_CBAR_MUTE : DC_CBAR_NOMUTE));
  mvaddch (y, x + 4, mixer_dc (cdata.flags & SND_MIXER_FLG_MUTE_RIGHT ?
			       DC_CBAR_MUTE : DC_CBAR_NOMUTE));
  mixer_dc (DC_CBAR_FRAME);
  mvaddch (y, x + 5, ACS_URCORNER);
  y--;
  
  /* record input?
   */
  if (cdata.flags & SND_MIXER_FLG_RECORD)
    {
      mixer_dc (DC_CBAR_RECORD);
      mvaddstr (y, x + 1, "RECORD");
      if (cdata.flags & SND_MIXER_FLG_RECORD_LEFT)
	{
	  if (cdata.flags & SND_MIXER_FLG_LTOR_IN)
	    mvaddstr (y + 2, x + 6, "L");
	  else
	    mvaddstr (y + 1, x + 1, "L");
	}
      if (cdata.flags & SND_MIXER_FLG_RECORD_RIGHT)
	{
	  if (cdata.flags & SND_MIXER_FLG_RTOL_IN)
	    mvaddstr (y + 2, x + 1, "R");
	  else
	    mvaddstr (y + 1, x + 6, "R");
	}
    }
  else if (cinfo.caps & SND_MIXER_CINFO_CAP_RECORD)
    for (i = 0; i < 6; i++)
      mvaddch (y, x + 1 + i, mixer_dc (DC_CBAR_NORECORD));
  else
    {
      mixer_dc (DC_BACK);
      mvaddstr (y, x, "         ");
    }
  y--;
}

static void
mixer_update_cbars (void)
{
  static int o_x = 0;
  static int o_y = 0;
  int i, x, y;
  
  if (!mixer_cbar_get_pos (mixer_focus_channel, &x, &y))
    {
      if (mixer_focus_channel < mixer_first_vis_channel)
	mixer_first_vis_channel = mixer_focus_channel;
      else if (mixer_focus_channel >= mixer_first_vis_channel + mixer_n_vis_channels)
	mixer_first_vis_channel = mixer_focus_channel - mixer_n_vis_channels + 1;
      mixer_cbar_get_pos (mixer_focus_channel, &x, &y);
    }
  for (i = 0; i < mixer_n_vis_channels; i++)
    mixer_update_cbar (i + mixer_first_vis_channel);
  
  /* draw focused cbar
   */
  if (mixer_have_old_focus)
    {
      mixer_dc (DC_BACK);
      mvaddstr (o_y, o_x, " ");
      mvaddstr (o_y, o_x + 9, " ");
    }
  o_x = x - 1;
  o_y = y;
  mixer_dc (DC_FOCUS);
  mvaddstr (o_y, o_x, "<");
  mvaddstr (o_y, o_x + 9, ">");
  mixer_have_old_focus = 1;
}

static void
mixer_draw_frame (void)
{
  char string[128];
  int i;
  int max_len;
  
  mixer_dc (DC_FRAME);
  
  /* card name
   */
  mixer_dc (DC_PROMPT);
  mvaddstr (1, 2, "Card: ");
  mixer_dc (DC_TEXT);
  sprintf (string, "%s", mixer_card_name);
  max_len = mixer_max_x - 2 - 6 - 2;
  if (strlen (string) > max_len)
    string[max_len] = 0;
  addstr (string);
  
  /* device name
   */
  mixer_dc (DC_PROMPT);
  mvaddstr (2, 2, "Chip: ");
  mixer_dc (DC_TEXT);
  sprintf (string, "%s", mixer_device_name);
  max_len = mixer_max_x - 2 - 6 - 2;
  if (strlen (string) > max_len)
    string[max_len] = 0;
  addstr (string);

  /* indicate exact mode
   */
  if (mixer_exact)
    {
      mixer_dc (DC_PROMPT);
      mvaddstr (3, 2, "[");
      mixer_dc (DC_TEXT);
      addstr ("ExactMode");
      mixer_dc (DC_PROMPT);
      addstr ("]");
    }
  else
    mvaddstr (3, 2, "           ");

  /* lines
   */
  mixer_dc (DC_PROMPT);
  for (i = 1; i < mixer_max_y - 1; i++)
    {
      mvaddch (i, 0, ACS_VLINE);
      mvaddch (i, mixer_max_x - 1, ACS_VLINE);
    }
  for (i = 1; i < mixer_max_x - 1; i++)
    {
      mvaddch (0, i, ACS_HLINE);
      mvaddch (mixer_max_y - 1, i, ACS_HLINE);
    }
  
  /* corners
   */
  mixer_dc (DC_PROMPT);
  mvaddch (0, 0, ACS_ULCORNER);
  mvaddch (0, mixer_max_x - 1, ACS_URCORNER);
  mvaddch (mixer_max_y - 1, 0, ACS_LLCORNER);
  if (!mixer_no_lrcorner)
    mvaddch (mixer_max_y - 1, mixer_max_x - 1, ACS_LRCORNER);
  else
    {
      mvaddch (mixer_max_y - 2, mixer_max_x - 1, ACS_LRCORNER);
      mvaddch (mixer_max_y - 2, mixer_max_x - 2, ACS_ULCORNER);
      mvaddch (mixer_max_y - 1, mixer_max_x - 2, ACS_LRCORNER);
    }

  /* program title
   */
  sprintf (string, "%s %s", PRGNAME_UPPER, VERSION);
  max_len = strlen (string);
  if (mixer_max_x >= max_len + 4)
    {
      mixer_dc (DC_PROMPT);
      mvaddch (0, mixer_max_x / 2 - max_len / 2 - 1, '[');
      mvaddch (0, mixer_max_x / 2 - max_len / 2 + max_len, ']');
    }
  if (mixer_max_x >= max_len + 2)
    {
      mixer_dc (DC_TEXT);
      mvaddstr (0, mixer_max_x / 2 - max_len / 2, string);
    }
}

static char*
mixer_offset_text (char **t,
		   int	  col,
		   int	 *length)
{
  char *p = *t;
  char *r;

  while (*p && *p != '\n' && col--)
    p++;
  if (*p == '\n' || !*p)
    {
      if (*p == '\n')
	p++;
      *length = 0;
      *t = p;
      return p;
    }

  r = p;
  while (*r && *r != '\n' && (*length)--)
    r++;

  *length = r - p;
  while (*r && *r != '\n')
    r++;
  if (*r == '\n')
    r++;
  *t = r;

  return p;
}

static void
mixer_show_text (char *title,
		 char *text,
		 int  *xoffs,
		 int  *yoffs)
{
  int tlines = 0, tcols = 0;
  float hscroll, vscroll;
  float hoffs, voffs;
  char *p, *text_offs = text;
  int x1, x2, y1, y2;
  int i, n, l, r, block, stipple;

  /* coords
   */
  x1 = 2;
  x2 = mixer_max_x - 3;
  y1 = 4;
  y2 = mixer_max_y - 2;

  if ((y2 - y1) < 3 || (x2 - x1) < 3)
    return;

  /* text dimensions
   */
  l = 0;
  for (p = text; *p; p++)
    if (*p == '\n')
      {
	tlines++;
	tcols = MAX (l, tcols);
	l = 0;
      }
    else
      l++;
  tcols = MAX (l, tcols);
  if (p > text && *(p - 1) != '\n')
    tlines++;

  /* scroll areas / offsets
   */
  l = x2 - x1 - 2;
  if (l > tcols)
    {
      x1 += (l - tcols) / 2;
      x2 = x1 + tcols + 1;
    }
  if (mixer_hscroll_delta)
    {
      *xoffs += mixer_hscroll_delta;
      mixer_hscroll_delta = 0;
      if (*xoffs < 0)
	{
	  *xoffs = 0;
	  beep ();
	}
      else if (*xoffs > tcols - l - 1)
	{
	  *xoffs = MAX (0, tcols - l - 1);
	  beep ();
	}
    }
  if (tcols - l - 1 <= 0)
    {
      hscroll = 1;
      hoffs = 0;
    }
  else
    {
      hscroll = ((float) l) / tcols;
      hoffs = ((float) *xoffs) / (tcols - l - 1);
    }

  l = y2 - y1 - 2;
  if (l > tlines)
    {
      y1 += (l - tlines) / 2;
      y2 = y1 + tlines + 1;
    }
  if (mixer_vscroll_delta)
    {
      *yoffs += mixer_vscroll_delta;
      mixer_vscroll_delta = 0;
      if (*yoffs < 0)
	{
	  *yoffs = 0;
	  beep ();
	}
      else if (*yoffs > tlines - l - 1)
	{
	  *yoffs = MAX (0, tlines - l - 1);
	  beep ();
	}
    }
  if (tlines - l - 1 <= 0)
    {
      voffs = 0;
      vscroll = 1;
    }
  else
    {
      vscroll = ((float) l) / tlines;
      voffs = ((float) *yoffs) / (tlines - l - 1);
    }

  /* colors
   */
  mixer_init_dc ('.', DC_ANY_3, MIXER_YELLOW, MIXER_BLUE, A_BOLD);
  mixer_init_dc ('.', DC_ANY_4, MIXER_WHITE, MIXER_BLUE, A_BOLD);
  mixer_dc (DC_ANY_4);

  /* corners
   */
  mvaddch (y2, x2, ACS_LRCORNER);
  mvaddch (y2, x1, ACS_LLCORNER);
  mvaddch (y1, x1, ACS_ULCORNER);
  mvaddch (y1, x2, ACS_URCORNER);

  /* left + upper border
   */
  for (i = y1 + 1; i < y2; i++)
    mvaddch (i, x1, ACS_VLINE);
  for (i = x1 + 1; i < x2; i++)
    mvaddch (y1, i, ACS_HLINE);
  if (title)
    {
      l = strlen (title);
      if (l <= x2 - x1 - 3)
	{
	  mvaddch (y1, x1 + 1 + (x2 - x1 - l) / 2 - 1, '[');
	  mvaddch (y1, x1 + 1 + (x2 - x1 - l) / 2 + l, ']');
	}
      if (l <= x2 - x1 - 1)
	{
	  mixer_dc (DC_ANY_3);
	  mvaddstr (y1, x1 + 1 + (x2 - x1 - l) / 2, title);
	}
      mixer_dc (DC_ANY_4);
    }

  stipple = ACS_CKBOARD;
  block = ACS_BLOCK;
  if (block == '#' && ACS_BOARD == '#')
    {
      block = stipple;
      stipple = ACS_BLOCK;
    }

  /* lower scroll border
   */
  l = x2 - x1 - 1;
  n = hscroll * l;
  r = (hoffs + 1.0 / (2 * (l - n - 1))) * (l - n - 1);
  for (i = 0; i < l; i++)
    mvaddch (y2, i + x1 + 1, hscroll >= 1 ? ACS_HLINE :
	     i >= r && i <= r + n ? block : stipple);

  /* right scroll border
   */
  l = y2 - y1 - 1;
  n = vscroll * l;
  r = (voffs + 1.0 / (2 * (l - n - 1))) * (l - n - 1);
  for (i = 0; i < l; i++)
    mvaddch (i + y1 + 1, x2, vscroll >= 1 ? ACS_VLINE :
	     i >= r && i <= r + n ? block : stipple);

  /* show text
   */
  x1++; y1++;
  for (i = 0; i < *yoffs; i++)
    {
      l = 0;
      mixer_offset_text (&text_offs, 0, &l);
    }
  for (i = y1; i < y2; i++)
    {
      l = x2 - x1;
      p = mixer_offset_text (&text_offs, *xoffs, &l);
      n = x1;
      while (l--)
	mvaddch (i, n++, *p++);
      while (n < x2)
	mvaddch (i, n++, ' ');
    }
}

struct vbuffer
{
  char *buffer;
  int size;
  int len;
};

static void
vbuffer_kill (struct vbuffer *vbuf)
{
  if (vbuf->size)
    free (vbuf->buffer);
  vbuf->buffer = NULL;
  vbuf->size = 0;
  vbuf->len = 0;
}

#define vbuffer_append_string(vb,str)	vbuffer_append (vb, str, strlen (str))
static void
vbuffer_append (struct vbuffer *vbuf,
		char           *text,
		int             len)
{
  if (vbuf->size - vbuf->len <= len)
    {
      vbuf->size += len + 1;
      vbuf->buffer = realloc (vbuf->buffer, vbuf->size);
    }
  memcpy (vbuf->buffer + vbuf->len, text, len);
  vbuf->len += len;
  vbuf->buffer[vbuf->len] = 0;
}

static int
vbuffer_append_file (struct vbuffer *vbuf,
		     char	    *name)
{
  int fd;

  fd = open (name, O_RDONLY);
  if (fd >= 0)
    {
      char buffer[1025];
      int l;

      do
	{
	  l = read (fd, buffer, 1024);
	  
	  vbuffer_append (vbuf, buffer, MAX (0, l));
	}
      while (l > 0 || (l < 0 && (errno == EAGAIN || errno == EINTR)));

      close (fd);

      return 0;
    }
  else
    return 1;
}

static void
mixer_show_procinfo (void)
{
  struct vbuffer vbuf = { NULL, 0, 0 };

  vbuffer_append_string (&vbuf, "\n");
  vbuffer_append_string (&vbuf, "/proc/asound/version:\n");
  vbuffer_append_string (&vbuf, "====================\n");
  if (vbuffer_append_file (&vbuf, "/proc/asound/version"))
    {
      vbuffer_kill (&vbuf);
      mixer_procinfo_xoffs = mixer_procinfo_yoffs = 0;
      mixer_show_text ("/proc",
		       " No /proc information available. ",
		       &mixer_procinfo_xoffs, &mixer_procinfo_yoffs);
      return;
    }
  else
    vbuffer_append_file (&vbuf, "/proc/asound/meminfo");

  vbuffer_append_string (&vbuf, "\n");
  vbuffer_append_string (&vbuf, "/proc/asound/cards:\n");
  vbuffer_append_string (&vbuf, "===================\n");
  if (vbuffer_append_file (&vbuf, "/proc/asound/cards"))
    vbuffer_append_string (&vbuf, "No information available.\n");

  vbuffer_append_string (&vbuf, "\n");
  vbuffer_append_string (&vbuf, "/proc/asound/devices:\n");
  vbuffer_append_string (&vbuf, "=====================\n");
  if (vbuffer_append_file (&vbuf, "/proc/asound/devices"))
    vbuffer_append_string (&vbuf, "No information available.\n");

  vbuffer_append_string (&vbuf, "\n");
  vbuffer_append_string (&vbuf, "/proc/asound/oss-devices:\n");
  vbuffer_append_string (&vbuf, "=========================\n");
  if (vbuffer_append_file (&vbuf, "/proc/asound/oss-devices"))
    vbuffer_append_string (&vbuf, "No information available.\n");

  vbuffer_append_string (&vbuf, "\n");
  vbuffer_append_string (&vbuf, "/proc/asound/timers:\n");
  vbuffer_append_string (&vbuf, "====================\n");
  if (vbuffer_append_file (&vbuf, "/proc/asound/timers"))
    vbuffer_append_string (&vbuf, "No information available.\n");

  vbuffer_append_string (&vbuf, "\n");
  vbuffer_append_string (&vbuf, "/proc/asound/pcm:\n");
  vbuffer_append_string (&vbuf, "=================\n");
  if (vbuffer_append_file (&vbuf, "/proc/asound/pcm"))
    vbuffer_append_string (&vbuf, "No information available.\n");

  mixer_show_text ("/proc", vbuf.buffer,
		   &mixer_procinfo_xoffs, &mixer_procinfo_yoffs);
  vbuffer_kill (&vbuf);
}

static void
mixer_init (void)
{
  static snd_mixer_info_t mixer_info = { 0, };
  static struct snd_ctl_hw_info hw_info;
  void *ctl_handle;
  
  if (snd_ctl_open (&ctl_handle, card_id) < 0)
    mixer_abort (ERR_OPEN, "snd_ctl_open");
  if (snd_ctl_hw_info (ctl_handle, &hw_info) < 0)
    mixer_abort (ERR_FCN, "snd_ctl_hw_info");
  snd_ctl_close (ctl_handle);
  /* open mixer device
   */
  if (snd_mixer_open (&mixer_handle, card_id, mixer_id) < 0)
    mixer_abort (ERR_OPEN, "snd_mixer_open");
  
  /* setup global variables
   */
  if (snd_mixer_info (mixer_handle, &mixer_info) < 0)
    mixer_abort (ERR_FCN, "snd_mixer_info");
  mixer_n_channels = mixer_info.channels;
  mixer_card_name = hw_info.name;
  mixer_device_name = mixer_info.name;
}

static void
mixer_init_window (void)
{
  /* initialize ncurses
   */
  mixer_window = initscr ();

  mixer_no_lrcorner = tigetflag ("xenl") != 1 && tigetflag ("am") != 1;

  if (mixer_do_color)
    mixer_do_color = has_colors ();
  mixer_init_draw_contexts ();

  /* react on key presses
   */
  cbreak ();
  noecho ();
  leaveok (mixer_window, TRUE);
  keypad (mixer_window, TRUE);
  GETCH_BLOCK (1);

  /* init mixer screen
   */
  getmaxyx (mixer_window, mixer_max_y, mixer_max_x);
  if (mixer_minimize)
    {
      mixer_max_x = MIXER_MIN_X;
      mixer_max_y = MIXER_MIN_Y;
    }
  mixer_ofs_x = 2 /* extra begin padding: */ + 1;

  /* required allocations */
  mixer_n_vis_channels = (mixer_max_x - mixer_ofs_x * 2 + 1) / 9;
  mixer_n_vis_channels = CLAMP (mixer_n_vis_channels, 1, mixer_n_channels);
  mixer_extra_space = mixer_max_x - mixer_ofs_x * 2 + 1 - mixer_n_vis_channels * 9;
  mixer_extra_space = MAX (0, mixer_extra_space / (mixer_n_vis_channels + 1));
  if (MIXER_TEXT_Y + 10 < mixer_max_y)
    mixer_cbar_height = 10 + MAX (0, mixer_max_y - MIXER_TEXT_Y - 10 ) / 2;
  else
    mixer_cbar_height = MAX (1, mixer_max_y - MIXER_TEXT_Y);

  mixer_clear (TRUE);
}

static void
mixer_resize (void)
{
  struct winsize winsz = { 0, };

  mixer_needs_resize = 0;
  
  if (ioctl (fileno (stdout), TIOCGWINSZ, &winsz) >= 0 &&
      winsz.ws_row && winsz.ws_col)
    {
      keypad (mixer_window, FALSE);
      leaveok (mixer_window, FALSE);

      endwin ();
      
      mixer_max_x = MAX (2, winsz.ws_col);
      mixer_max_y = MAX (2, winsz.ws_row);
      
      /* humpf, i don't get it, if only the number of rows change,
       * ncurses will segfault shortly after (could trigger that with mc as well).
       */
      resizeterm (mixer_max_y + 1, mixer_max_x + 1);
      resizeterm (mixer_max_y, mixer_max_x);
      
      mixer_init_window ();
      
      if (mixer_max_x < MIXER_MIN_X ||
	  mixer_max_y < MIXER_MIN_Y)
	beep (); // mixer_abort (ERR_WINSIZE, "");

      mixer_have_old_focus = 0;
    }
}

static void
mixer_channel_changed (void *private_data,
		       int   channel)
{
  /* we don't actually need to update the individual channels because
   * we redraw the whole screen upon every main iteration anyways.
   */
#if 0
  fprintf (stderr, "*** channel = %i\n", channel);
  mixer_update_cbar (channel);
#endif
}

static int
mixer_iteration (void)
{
  struct timeval delay = { 0, };
  snd_mixer_callbacks_t callbacks = { 0, };
  int mixer_fd;
  fd_set rfds;
  int finished = 0;
  int key = 0;
  int old_view;
  
  callbacks.channel_was_changed = mixer_channel_changed;

  /* setup for select on stdin and the mixer fd */
  mixer_fd = snd_mixer_file_descriptor (mixer_handle);
  FD_ZERO (&rfds);
  FD_SET (fileno (stdin), &rfds);
  FD_SET (mixer_fd, &rfds);

  delay.tv_sec = 0;
  delay.tv_usec = 0 * 100 * 1000;

  finished = select (mixer_fd + 1, &rfds, NULL, NULL, mixer_needs_resize ? &delay : NULL) < 0;

  /* don't abort on handled signals */
  if (finished && errno == EINTR)
    {
      FD_ZERO (&rfds);
      finished = 0;
    }
  else if (mixer_needs_resize)
    mixer_resize ();

  if (FD_ISSET (mixer_fd, &rfds))
    snd_mixer_read (mixer_handle, &callbacks);

  if (FD_ISSET (fileno (stdin), &rfds))
    key = getch ();

  old_view = mixer_view;
  
  /* feature Escape prefixing for some keys */
  if (key == 27)
    {
      GETCH_BLOCK (0);
      key = getch ();
      GETCH_BLOCK (1);
      switch (key)
	{
	case 9:	/* Tab */
	  key = KEY_BTAB;
	  break;
	default:
	  key = 27;
	  break;
	}
    }
  
  /* general keys */
  switch (key)
    {
    case 0:
      /* ignore */
      break;
    case 27:	/* Escape */
      finished = 1;
      key = 0;
      break;
    case 13:	/* Return */
    case 10:	/* NewLine */
      if (mixer_view == VIEW_CHANNELS)
	mixer_clear (FALSE);
      mixer_view = VIEW_CHANNELS;
      key = 0;
      break;
    case 'h':
    case 'H':
    case KEY_F (1):
      mixer_view = VIEW_HELP;
      key = 0;
      break;
    case '/':
    case KEY_F (2):
      mixer_view = VIEW_PROCINFO;
      key = 0;
      break;
    case 'L':
    case 'l':
      mixer_clear (TRUE);
      break;
    }
  
  if (key && (mixer_view == VIEW_HELP ||
	      mixer_view == VIEW_PROCINFO))
    switch (key)
      {
      case 9:		/* Tab */
	mixer_hscroll_delta += 8;
	break;
      case KEY_BTAB:
	mixer_hscroll_delta -= 8;
	break;
      case KEY_A1:
	mixer_hscroll_delta -= 1;
	mixer_vscroll_delta -= 1;
	break;
      case KEY_A3:
	mixer_hscroll_delta += 1;
	mixer_vscroll_delta -= 1;
	break;
      case KEY_C1:
	mixer_hscroll_delta -= 1;
	mixer_vscroll_delta += 1;
	break;
      case KEY_C3:
	mixer_hscroll_delta += 1;
	mixer_vscroll_delta += 1;
	break;
      case KEY_RIGHT:
      case 'n':
	mixer_hscroll_delta += 1;
	break;
      case KEY_LEFT:
      case 'p':
	mixer_hscroll_delta -= 1;
	break;
      case KEY_UP:
      case 'w':
      case 'W':
	mixer_vscroll_delta -= 1;
	break;
      case KEY_DOWN:
      case 'x':
      case 'X':
	mixer_vscroll_delta += 1;
	break;
      case KEY_PPAGE:
      case 'B':
      case 'b':
	mixer_vscroll_delta -= (mixer_max_y - 5) / 2;
	break;
      case KEY_NPAGE:
      case ' ':
	mixer_vscroll_delta += (mixer_max_y - 5) / 2;
	break;
      case KEY_BEG:
      case KEY_HOME:
	mixer_hscroll_delta -= 0xffffff;
	break;
      case KEY_LL:
      case KEY_END:
	mixer_hscroll_delta += 0xffffff;
	break;
      }
  
  if (key && mixer_view == VIEW_CHANNELS)
    switch (key)
      {
      case 9:		/* Tab */
	mixer_exact = !mixer_exact;
	break;
      case KEY_RIGHT:
      case 'n':
	mixer_focus_channel += 1;
	break;
      case KEY_LEFT:
      case 'p':
	mixer_focus_channel -= 1;
	break;
      case KEY_PPAGE:
	if (mixer_exact)
	  {
	    mixer_lvolume_delta = 8;
	    mixer_rvolume_delta = 8;
	  }
	else
	  {
	    mixer_lvolume_delta = 10;
	    mixer_rvolume_delta = 10;
	  }
	break;
      case KEY_NPAGE:
	if (mixer_exact)
	  {
	    mixer_lvolume_delta = -8;
	    mixer_rvolume_delta = -8;
	  }
	else
	  {
	    mixer_lvolume_delta = -10;
	    mixer_rvolume_delta = -10;
	  }
	break;
      case KEY_BEG:
      case KEY_HOME:
	mixer_lvolume_delta = 512;
	mixer_rvolume_delta = 512;
	break;
      case KEY_LL:
      case KEY_END:
	mixer_lvolume_delta = -512;
	mixer_rvolume_delta = -512;
	break;
      case '+':
	mixer_lvolume_delta = 1;
	mixer_rvolume_delta = 1;
	break;
      case '-':
	mixer_lvolume_delta = -1;
	mixer_rvolume_delta = -1;
	break;
      case 'w':
      case KEY_UP:
	mixer_lvolume_delta = 1;
	mixer_rvolume_delta = 1;
      case 'W':
	mixer_lvolume_delta += 1;
	mixer_rvolume_delta += 1;
	break;
      case 'x':
      case KEY_DOWN:
	mixer_lvolume_delta = -1;
	mixer_rvolume_delta = -1;
      case 'X':
	mixer_lvolume_delta += -1;
	mixer_rvolume_delta += -1;
	break;
      case 'q':
	mixer_lvolume_delta = 1;
      case 'Q':
	mixer_lvolume_delta += 1;
	break;
      case 'y':
      case 'z':
	mixer_lvolume_delta = -1;
      case 'Y':
      case 'Z':
	mixer_lvolume_delta += -1;
	break;
      case 'e':
	mixer_rvolume_delta = 1;
      case 'E':
	mixer_rvolume_delta += 1;
	break;
      case 'c':
	mixer_rvolume_delta = -1;
      case 'C':
	mixer_rvolume_delta += -1;
	break;
      case 'm':
      case 'M':
	mixer_toggle_mute_left = 1;
	mixer_toggle_mute_right = 1;
	break;
      case 'b':
      case 'B':
      case '=':
	mixer_balance_volumes = 1;
	break;
      case '<':
      case ',':
	mixer_toggle_mute_left = 1;
	break;
      case '>':
      case '.':
	mixer_toggle_mute_right = 1;
	break;
      case ' ':
	mixer_toggle_record = 1;
	break;
      case KEY_IC:
      case ';':
	mixer_toggle_rec_left = 1;
	break;
      case '\'':
      case KEY_DC:
	mixer_toggle_rec_right = 1;
	break;
      case '1':
	mixer_route_rtol_in = 1;
	break;
      case '2':
	mixer_route_ltor_in = 1;
	break;
      }
  
  if (old_view != mixer_view)
    mixer_clear (FALSE);
  
  mixer_focus_channel = CLAMP (mixer_focus_channel, 0, mixer_n_channels - 1);
  
  return finished;
}

static void
mixer_winch (void)
{
  signal (SIGWINCH, (void*) mixer_winch);

  mixer_needs_resize++;
}

static void
mixer_signal_handler (int signal)
{
  if (signal != SIGSEGV)
    mixer_abort (ERR_SIGNAL, sys_siglist[signal]);
  else
    {
      fprintf (stderr, "\nSegmentation fault.\n");
      _exit (11);
    }
}

int
main (int    argc,
      char **argv)
{
  int opt;
  
  /* parse args
   */
  do
    {
      opt = getopt (argc, argv, "c:m:eshg");
      switch (opt)
	{
	case '?':
	case 'h':
	  fprintf (stderr, "%s %s\n", PRGNAME_UPPER, VERSION);
	  fprintf (stderr, "Usage: %s [-e] [-c <card: 1..%i>] [-m <mixer: 0..1>] [-z]\n", PRGNAME, snd_cards ());
	  mixer_abort (ERR_NONE, "");
	case 'c':
	  card_id = snd_card_name (optarg);
	  break;
	case 'e':
	  mixer_exact = !mixer_exact;
	  break;
	case 'g':
	  mixer_do_color = !mixer_do_color;
	  break;
	case 'm':
	  mixer_id = CLAMP (optarg[0], '0', '1') - '0';
	  break;
	case 's':
	  mixer_minimize = 1;
	  break;
	}
    }
  while (opt > 0);
  
  /* initialize mixer
   */
  mixer_init ();
  
  /* setup signal handlers
   */
  signal (SIGINT, mixer_signal_handler);
  signal (SIGTRAP, mixer_signal_handler);
  signal (SIGABRT, mixer_signal_handler);
  signal (SIGQUIT, mixer_signal_handler);
  signal (SIGBUS, mixer_signal_handler);
  signal (SIGSEGV, mixer_signal_handler);
  signal (SIGPIPE, mixer_signal_handler);
  signal (SIGTERM, mixer_signal_handler);
  
  /* initialize ncurses
   */
  mixer_init_window ();
  if (mixer_max_x < MIXER_MIN_X ||
      mixer_max_y < MIXER_MIN_Y)
    beep (); // mixer_abort (ERR_WINSIZE, "");
  
  signal (SIGWINCH, (void*) mixer_winch);

  do
    {
      /* draw window upon every iteration */
      if (!mixer_needs_resize)
	{
	  switch (mixer_view)
	    {
	    case VIEW_CHANNELS:
	      mixer_update_cbars ();
	      break;
	    case VIEW_HELP:
	      mixer_show_text ("Help", mixer_help_text, &mixer_help_xoffs, &mixer_help_yoffs);
	      break;
	    case VIEW_PROCINFO:
	      mixer_show_procinfo ();
	      break;
	    }
	  mixer_draw_frame ();
	  refresh ();
	}
    }
  while (!mixer_iteration ());
  
  mixer_abort (ERR_NONE, "");
};