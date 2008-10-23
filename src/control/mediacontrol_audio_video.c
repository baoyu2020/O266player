/*****************************************************************************
 * audio_video.c: Audio/Video management : volume, snapshot, OSD
 *****************************************************************************
 * Copyright (C) 2005 the VideoLAN team
 * $Id$
 *
 * Authors: Olivier Aubert <olivier.aubert@liris.univ-lyon1.fr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include "mediacontrol_internal.h"
#include "libvlc_internal.h"

#include <vlc/mediacontrol.h>
#include <vlc/libvlc.h>

#include <vlc_vout.h>
#include <vlc_osd.h>

#include <stdlib.h>                                      /* malloc(), free() */
#include <string.h>

#include <errno.h>                                                 /* ENOMEM */
#include <stdio.h>
#include <ctype.h>

#ifdef HAVE_UNISTD_H
#    include <unistd.h>
#endif
#ifdef HAVE_SYS_TIME_H
#    include <sys/time.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#    include <sys/types.h>
#endif

mediacontrol_RGBPicture *
mediacontrol_snapshot( mediacontrol_Instance *self,
                       const mediacontrol_Position * a_position,
                       mediacontrol_Exception *exception )
{
    (void)a_position;
    vlc_object_t* p_cache;
    vout_thread_t* p_vout;
    input_thread_t *p_input;
    mediacontrol_RGBPicture *p_pic = NULL;
    char path[256];
    snapshot_t *p_snapshot;
    libvlc_exception_t ex;

    libvlc_exception_init( &ex );
    mediacontrol_exception_init( exception );

    p_input = libvlc_get_input_thread( self->p_media_player, &ex );
    if( ! p_input )
    {
        RAISE_NULL( mediacontrol_InternalException, "No input" );
    }
    p_vout = vlc_object_find( p_input, VLC_OBJECT_VOUT, FIND_CHILD );
    if( ! p_vout )
    {
        RAISE_NULL( mediacontrol_InternalException, "No video output" );
    }
    p_cache = vlc_object_create( p_input, sizeof( vlc_object_t ) );
    if( p_cache == NULL )
    {
        vlc_object_release( p_vout );
        vlc_object_release( p_input );
        RAISE_NULL( mediacontrol_InternalException, "Out of memory" );
    }
    snprintf( path, 255, "object:%ju", (uintmax_t)(uintptr_t)p_cache );
    var_SetString( p_vout, "snapshot-path", path );
    var_SetString( p_vout, "snapshot-format", "png" );

    vlc_object_lock( p_cache );
    vout_Control( p_vout, VOUT_SNAPSHOT );
    vlc_object_wait( p_cache );
    vlc_object_release( p_vout );

    p_snapshot = ( snapshot_t* ) p_cache->p_private;
    vlc_object_release( p_cache );
    vlc_object_release( p_input );

    if( p_snapshot )
    {
        /* Note: p_snapshot->p_data is directly used, not copied. Thus
           do not free it here. */
        p_pic = private_mediacontrol_createRGBPicture( p_snapshot->i_width,
                                                       p_snapshot->i_height,
                                                       VLC_FOURCC( 'p','n','g',' ' ),
                                                       p_snapshot->date,
                                                       p_snapshot->p_data,
                                                       p_snapshot->i_datasize );
        if( !p_pic )
        {
            free( p_snapshot );
            RAISE_NULL( mediacontrol_InternalException, "Out of memory" );
        }
    }
    else
    {
        RAISE_NULL( mediacontrol_InternalException, "Snapshot exception" );
    }
    return p_pic;
}

static
int mediacontrol_showtext( vout_thread_t *p_vout, int i_channel,
                           char *psz_string, text_style_t *p_style,
                           int i_flags, int i_hmargin, int i_vmargin,
                           mtime_t i_start, mtime_t i_stop )
{
    return osd_ShowTextAbsolute( p_vout->p_spu, i_channel,
                                 psz_string, p_style,
                                 i_flags, i_hmargin, i_vmargin,
                                 i_start, i_stop );
}


void
mediacontrol_display_text( mediacontrol_Instance *self,
                           const char * message,
                           const mediacontrol_Position * begin,
                           const mediacontrol_Position * end,
                           mediacontrol_Exception *exception )
{
    vout_thread_t *p_vout = NULL;
    char* psz_message;
    input_thread_t *p_input;
    libvlc_exception_t ex;

    libvlc_exception_init( &ex );
    mediacontrol_exception_init( exception );

    p_input = libvlc_get_input_thread( self->p_media_player, &ex );
    if( ! p_input )
    {
        RAISE_VOID( mediacontrol_InternalException, "No input" );
    }
    p_vout = vlc_object_find( p_input, VLC_OBJECT_VOUT, FIND_CHILD );
    if( ! p_vout )
    {
        RAISE_VOID( mediacontrol_InternalException, "No video output" );
    }

    psz_message = strdup( message );
    if( !psz_message )
    {
        RAISE_VOID( mediacontrol_InternalException, "no more memory" );
    }

    if( begin->origin == mediacontrol_RelativePosition &&
        begin->value == 0 &&
        end->origin == mediacontrol_RelativePosition )
    {
        mtime_t i_duration = 0;
        mtime_t i_now = mdate();

        i_duration = 1000 * private_mediacontrol_unit_convert(
                                                              self->p_media_player,
                                                              end->key,
                                                              mediacontrol_MediaTime,
                                                              end->value );

        mediacontrol_showtext( p_vout, DEFAULT_CHAN, psz_message, NULL,
                               OSD_ALIGN_BOTTOM | OSD_ALIGN_LEFT, 0, 0,
                               i_now, i_now + i_duration );
    }
    else
    {
        mtime_t i_debut, i_fin, i_now;

        /* FIXME */
        /* i_now = input_ClockGetTS( p_input, NULL, 0 ); */
        i_now = mdate();

        i_debut = private_mediacontrol_position2microsecond( self->p_media_player,
                                            ( mediacontrol_Position* ) begin );
        i_debut += i_now;

        i_fin = private_mediacontrol_position2microsecond( self->p_media_player,
                                          ( mediacontrol_Position * ) end );
        i_fin += i_now;

        vout_ShowTextAbsolute( p_vout, DEFAULT_CHAN, psz_message, NULL,
                               OSD_ALIGN_BOTTOM | OSD_ALIGN_LEFT, 0, 0,
                               i_debut, i_fin );
    }

    vlc_object_release( p_vout );
}

unsigned short
mediacontrol_sound_get_volume( mediacontrol_Instance *self,
                               mediacontrol_Exception *exception )
{
    libvlc_exception_t ex;
    int i_ret = 0;

    mediacontrol_exception_init( exception );
    libvlc_exception_init( &ex );

    i_ret = libvlc_audio_get_volume( self->p_instance, &ex );
    HANDLE_LIBVLC_EXCEPTION_ZERO( &ex );
    /* FIXME: Normalize in [0..100] */
    return (unsigned short)i_ret;
}

void
mediacontrol_sound_set_volume( mediacontrol_Instance *self,
                               const unsigned short volume,
                               mediacontrol_Exception *exception )
{
    /* FIXME: Normalize in [0..100] */
    libvlc_exception_t ex;

    mediacontrol_exception_init( exception );
    libvlc_exception_init( &ex );

    libvlc_audio_set_volume( self->p_instance, volume, &ex );
    HANDLE_LIBVLC_EXCEPTION_VOID( &ex );
}

int mediacontrol_set_visual( mediacontrol_Instance *self,
                                    WINDOWHANDLE visual_id,
                                    mediacontrol_Exception *exception )
{
    libvlc_exception_t ex;

    mediacontrol_exception_init( exception );
    libvlc_exception_init( &ex );

    libvlc_media_player_set_drawable( self->p_media_player, (libvlc_drawable_t)visual_id, &ex );
    HANDLE_LIBVLC_EXCEPTION_ZERO( &ex );
    return true;
}

int
mediacontrol_get_rate( mediacontrol_Instance *self,
               mediacontrol_Exception *exception )
{
    libvlc_exception_t ex;
    int i_ret;

    mediacontrol_exception_init( exception );
    libvlc_exception_init( &ex );

    i_ret = libvlc_media_player_get_rate( self->p_media_player, &ex );
    HANDLE_LIBVLC_EXCEPTION_ZERO( &ex );

    return i_ret / 10;
}

void
mediacontrol_set_rate( mediacontrol_Instance *self,
               const int rate,
               mediacontrol_Exception *exception )
{
    libvlc_exception_t ex;

    mediacontrol_exception_init( exception );
    libvlc_exception_init( &ex );

    libvlc_media_player_set_rate( self->p_media_player, rate * 10, &ex );
    HANDLE_LIBVLC_EXCEPTION_VOID( &ex );
}

int
mediacontrol_get_fullscreen( mediacontrol_Instance *self,
                 mediacontrol_Exception *exception )
{
    libvlc_exception_t ex;
    int i_ret;

    mediacontrol_exception_init( exception );
    libvlc_exception_init( &ex );

    i_ret = libvlc_get_fullscreen( self->p_media_player, &ex );
    HANDLE_LIBVLC_EXCEPTION_ZERO( &ex );

    return i_ret;
}

void
mediacontrol_set_fullscreen( mediacontrol_Instance *self,
                 const int b_fullscreen,
                 mediacontrol_Exception *exception )
{
    libvlc_exception_t ex;

    mediacontrol_exception_init( exception );
    libvlc_exception_init( &ex );

    libvlc_set_fullscreen( self->p_media_player, b_fullscreen, &ex );
    HANDLE_LIBVLC_EXCEPTION_VOID( &ex );
}
