/*
 * manager handling.
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

/* ***************************** */
/* Includes                      */
/* ***************************** */

#include <stdlib.h>
#include <string.h>
#include "manager.h"

/* ***************************** */
/* Makros/Constants              */
/* ***************************** */

/* ***************************** */
/* Types                         */
/* ***************************** */

/* ***************************** */
/* Varaibles                     */
/* ***************************** */

extern Manager_t AudioManager;
extern Manager_t VideoManager;
extern Manager_t SubtitleManager;
extern Manager_t DvbSubtitleManager;
extern Manager_t TeletextManager;

ManagerHandler_t ManagerHandler = {
    "ManagerHandler",
    &AudioManager,
    &VideoManager,
    &SubtitleManager
  , &DvbSubtitleManager
  , &TeletextManager
};

/* ***************************** */
/* Prototypes                    */
/* ***************************** */

/* ***************************** */
/* Functions                     */
/* ***************************** */
void copyTrack(Track_t* to, Track_t* from)
{
    *to = *from;
    to->Name     = (from->Name     != NULL)? strdup(from->Name)    : strdup("Unknown");
    to->Encoding = (from->Encoding != NULL)? strdup(from->Encoding): strdup("Unknown");
    to->language = (from->language != NULL)? strdup(from->language): strdup("Unknown");
    to->aacbuf   = (from->aacbuf   != NULL)? strdup(from->aacbuf)  : strdup("Unknown");
}

void freeTrack(Track_t* track)
{
    if (track->Name != NULL){
        free(track->Name);
        track->Name = NULL;
    }

    if (track->Encoding != NULL){
        free(track->Encoding);
        track->Encoding = NULL;
    }

    if (track->language != NULL) {
        free(track->language);
        track->language = NULL;
    }

    if (track->aacbuf != NULL) {
        free(track->aacbuf);
        track->aacbuf = NULL;
    }
}
