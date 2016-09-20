/*
 * Subtitle output to one registered client.
 *
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <memory.h>
#include <asm/types.h>
#include <errno.h>

#include "common.h"

#define SUBTITLE_DEBUG
#ifdef SUBTITLE_DEBUG

static short debug_level = 0;

#define subtitle_printf(level, fmt, x...) do { \
if (debug_level >= level) printf("[%s:%s] " fmt, __FILE__, __FUNCTION__, ## x); } while (0)
#else
#define subtitle_printf(level, fmt, x...)
#endif

#ifndef SUBTITLE_SILENT
#define subtitle_err(fmt, x...) do { printf("[%s:%s] " fmt, __FILE__, __FUNCTION__, ## x); } while (0)
#else
#define subtitle_err(fmt, x...)
#endif

/* Error Constants */
#define cERR_SUBTITLE_NO_ERROR         0
#define cERR_SUBTITLE_ERROR            -1

static const char FILENAME[] = "output_subtitle.c";

struct sub_t
{
     uint8_t *data;
     int64_t  pts;
     int64_t  duration;
};

#define PUFFERSIZE 20

static struct sub_t subtitleData[PUFFERSIZE];

static int writePointer = 0;
static int readPointer = 0;
static int isSubtitleOpened = 0;

static char *ass_get_text(char *str)
{
	/*
	ReadOrder, Layer, Style, Name, MarginL, MarginR, MarginV, Effect, Text
	91,0,Default,,0,0,0,,maar hij smaakt vast tof.
	*/

	int i = 0;
	char *p_str = str;
	while(i < 8 && *p_str != '\0')
	{
		if (*p_str == ',')
			i++;
		p_str++;
	}

	/* standardize hard break: '\N' -> '\n' http://docs.aegisub.org/3.2/ASS_Tags/ */
	char *p_newline = NULL;
	while((p_newline = strstr(p_str, "\\N")) != NULL)
		*(p_newline + 1) = 'n';
	return p_str;
}

static int Write(Context_t *context, void *data)
{
	subtitle_printf(10, "\n");
	char *Encoding = NULL;
	int msg = 0;

	if (data == NULL)
	{
		subtitle_err("null pointer passed\n");
		return cERR_SUBTITLE_ERROR;
	}

	context->manager->subtitle->Command(context, MANAGER_GETENCODING, &Encoding);

	if (Encoding == NULL)
	{
		subtitle_err("encoding unknown\n");
		return cERR_SUBTITLE_ERROR;
	}

	Subtitle_Out_t* out = (Subtitle_Out_t*) data;

	if (subtitleData[writePointer].data != NULL)
	{
		subtitle_err("subtitle list is full, dlete %d\n", writePointer);
		free(subtitleData[writePointer].data);
		subtitleData[writePointer].data     = NULL;
		subtitleData[writePointer].pts	    = 0;
		subtitleData[writePointer].duration = 0;
	}

	if (!strncmp("S_TEXT/ASS", Encoding, 10))
	{
		subtitleData[writePointer].data = (uint8_t *)strdup(ass_get_text((char *)out->data));
	}
	else if (!strncmp("S_TEXT/SUBRIP", Encoding, 13) || !strncmp("S_TEXT/SRT", Encoding, 10) || !strncmp("S_TEXT/SSA", Encoding, 10))
	{
		subtitleData[writePointer].data = (uint8_t *)strdup((const char *)out->data);
	}
	else
	{
		subtitle_err("unknown encoding %s\n", Encoding);
		return cERR_SUBTITLE_ERROR;
	}

	subtitleData[writePointer].pts	    = out->pts;
	subtitleData[writePointer].duration = out->duration;

	subtitle_printf(10, "Encoding:%s Text:%s [%lld] -> [%lld]\n",
		Encoding, (const char *)subtitleData[writePointer].data, subtitleData[writePointer].pts,
		subtitleData[writePointer].pts + subtitleData[writePointer].duration);

	writePointer++;

	if (writePointer == PUFFERSIZE)
	{
		writePointer = 0;
	}

	/* Tell enigma2 that we have subtitles */
	context->playback->Command(context, PLAYBACK_SEND_MESSAGE, (void *) &msg);

	subtitle_printf(10, "<\n");
	return cERR_SUBTITLE_NO_ERROR;
}

static void subtitle_Reset()
{
	subtitle_printf(10, "\n");
	int i;

	writePointer = 0;
	readPointer = 0;

	for (i = 0; i < PUFFERSIZE; i++)
	{
		if (isSubtitleOpened && subtitleData[i].data != NULL)
		{
			free(subtitleData[i].data);
			subtitleData[i].data = NULL;
		}
		subtitleData[i].pts	     = 0;
		subtitleData[i].duration = 0;
	}

	subtitle_printf(10, "<\n");
}

static int subtitle_DelData()
{
	subtitle_printf(10, " %d\n", readPointer);

	if (subtitleData[readPointer].data == NULL)
	{
		subtitle_err("null pointer in data\n");
	}
	else
	{
		free(subtitleData[readPointer].data);
		subtitleData[readPointer].data = NULL;
	}
	subtitleData[readPointer].pts	   = 0;
	subtitleData[readPointer].duration = 0;

	readPointer++;

	if (readPointer == PUFFERSIZE)
	{
		readPointer = 0;
	}

	return cERR_SUBTITLE_NO_ERROR;
}

static int subtitle_Open()
{
	subtitle_printf(10, "\n");

	subtitle_Reset();
	isSubtitleOpened = 1;

	return cERR_SUBTITLE_NO_ERROR;
}

static int subtitle_Close()
{
	subtitle_printf(10, "\n");

	subtitle_Reset();
	isSubtitleOpened = 0;

	return cERR_SUBTITLE_NO_ERROR;
}

static int Command(Context_t *context, OutputCmd_t command, void *argument)
{
	int ret = cERR_SUBTITLE_NO_ERROR;

	subtitle_printf(50, "%d\n", command);

	switch(command)
	{
		case OUTPUT_GET_SUBTITLE_DATA:
			*((Subtitle_Out_t **)argument) = (Subtitle_Out_t *) &subtitleData[readPointer];
			subtitle_printf(50, "Get data %d Text:%s [%lld] -> [%lld]\n",
				readPointer, (const char *)subtitleData[readPointer].data, subtitleData[readPointer].pts,
				subtitleData[readPointer].pts + subtitleData[readPointer].duration);
			break;
		case OUTPUT_DEL_SUBTITLE_DATA:
			ret = subtitle_DelData();
			break;
		case OUTPUT_OPEN:
			ret = subtitle_Open();
			break;
		case OUTPUT_CLOSE:
			ret = subtitle_Close();
			break;
		default:
			subtitle_err("OutputCmd %d not supported!\n", command);
			ret = cERR_SUBTITLE_ERROR;
			break;
	}

	subtitle_printf(50, "exiting with value %d\n", ret);

	return ret;
}


static char *SubtitleCapabilitis[] = { "subtitle", NULL };

struct Output_s SubtitleOutput = {
	"Subtitle",
	&Command,
	&Write,
	SubtitleCapabilitis
};
