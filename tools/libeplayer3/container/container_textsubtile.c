/*
 * subtitle handling for text files.
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/types.h>
#include <dirent.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>

#include "common.h"
#include "misc.h"

#define SUBTITLE_DEBUG

#ifdef SUBTITLE_DEBUG

static short debug_level = 10;

#define sub_printf(level, fmt, x...) do { \
if (debug_level >= level) printf("[%s:%s] " fmt, FILENAME, __FUNCTION__, ## x); } while (0)
#else
#define sub_printf(level, fmt, x...)
#endif

#ifndef SUBTITLE_SILENT
#define sub_err(fmt, x...) do { printf("[%s:%s] " fmt, FILENAME, __FUNCTION__, ## x); } while (0)
#else
#define sub_err(fmt, x...)
#endif

#define cERR_SUBTITLE_NO_ERROR        0
#define cERR_SUBTITLE_ERROR          -1

#define TRACKWRAP 20
#define MAXLINELENGTH 250

static const char FILENAME[] = "container_textsubtile.c";

typedef struct {
	char *File;
	int   Id;
} SubTrack_t;

static pthread_t thread_sub;

static SubTrack_t *Tracks;
static int TrackCount = 0;
static int hasThreadStarted = 0;
static int SrtSubtitles = 0;

FILE *fsub = NULL;


void data_to_manager(Context_t *context, char* Text, int64_t Pts, int64_t Duration)
{
	sub_printf(20, "start:%lld end:%lld Text: %s",Pts, Pts + Duration, Text);

	if(context && context->playback && context->playback->isPlaying && Text)
	{
		int sl = strlen(Text)-1;
		Text[sl]='\0'; /* Set last to \0, to replace \n or \r if exist */

		Subtitle_Out_t subOut;
		memset(&subOut, 0, sizeof(subOut));

		subOut.data	= (uint8_t *)Text;
		subOut.pts	= Pts;
		subOut.duration	= Duration;

		if (context->output->subtitle->Write(context, &subOut) < 0)
		{
			sub_err("writing data to subtitle device failed\n");
		}
	}
}

static void *SubtitleThread(void *data)
{
	int horIni, minIni, secIni, milIni, horFim, minFim, secFim, milFim;
	int pos = 0;
	int ret = 0;
	char  Data[MAXLINELENGTH];
	char *Text = NULL;
	int64_t Pts = 0;
	int64_t Duration = 0;
	unsigned long long int playPts = 0;

	Context_t *context = (Context_t *)data;

	sub_printf(10, "\n");

	while(context && context->playback && context->playback->isPlaying && fsub && fgets(Data, MAXLINELENGTH, fsub))
	{
		if (SrtSubtitles)
		{
			/*
			00:02:17,440 --> 00:02:20,375
			Senator, we're making
			our final approach into Coruscant.
			*/

			sub_printf(20, "pos=%d\n", pos);

			if(pos == 0)
			{
				if(Data[0] == '\n' || Data[0] == '\0' || Data[0] == 13 /* ^M */)
					continue; /* Empty line not allowed here */
				pos++;
			}
			else if(pos == 1)
			{
				ret = sscanf(Data, "%d:%d:%d,%d --> %d:%d:%d,%d", &horIni, &minIni, &secIni, &milIni, &horFim, &minFim, &secFim, &milFim);
				if (ret != 8)
				{
					continue; /* Data is not in correct format */
				}

				Pts = (int64_t)(horIni * 3600 + minIni * 60 + secIni) * 1000 + milIni;
				Duration = (int64_t)(horFim * 3600 + minFim * 60 + secFim) * 1000  + milFim - Pts;

				if (context->playback->Command(context, PLAYBACK_PTS, &playPts) < 0)
				{
					sub_err("Error in get playback pts!\n");
				}

				unsigned long long int curPts = playPts / 90.0;
				if (Pts + Duration < curPts)
				{
					sub_printf(20, "current sub has already ended, skip\n");
					continue;
				}

				while (context && context->playback && context->playback->isPlaying && fsub && Pts > curPts + 2000.0)
				{
					sub_printf(20, "current sub in the future, waiting\n");
					usleep(900000);
					context->playback->Command(context, PLAYBACK_PTS, &playPts);
					if (playPts >= curPts * 90.0)
					{
						curPts = playPts / 90.0;
					}
					else
					{
						sub_printf(20, "seek back, restart read\n");
						pos = -1;
						rewind(fsub);
						break;
					}
				}
			pos++;
			}
			else if(pos == 2)
			{
				sub_printf(50, "Data[0] = %d \'%c\'\n", Data[0], Data[0]);

				if(Data[0] == '\n' || Data[0] == '\0' || Data[0] == 13 /* ^M */) {
					if(Text != NULL)
					{
						data_to_manager(context, Text, Pts, Duration);
						free(Text);
						Text = NULL;
					}
					pos = 0;
					continue;
				}

				if(!Text)
				{
					Text = strdup(Data);
				}
				else
				{
					int length = strlen(Text) /* \0 -> \n */ + strlen(Data) + 2 /* \0 */;
					char *tmpText = Text;
					Text = (char *)malloc(length);

					strcpy(Text, tmpText);
					strcat(Text, Data);
					free(tmpText);
				}
			}
		}
		else /* ssa or ass subtitles */
		{
			/*
			Format: Marked, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
			Dialogue: Marked=0,0:02:40.65,0:02:41.79,Wolf main,Cher,0000,0000,0000,,Hello world!
			*/

			if(Data[0] == '\n' || Data[0] == '\0' || Data[0] != 'D')
				continue; /* Skip empty line and line without Dialogue */

			int i = 0;
			char *p_newline = NULL;
			char *ptr = Data;

			while(i < 10 && *ptr != '\0')
			{
				if (*ptr == ',')
				{
					i++;
				}
				ptr++;
				if (i == 1)
				{
					ret = sscanf(ptr, "%d:%d:%d.%d,%d:%d:%d.%d,", &horIni, &minIni, &secIni, &milIni, &horFim, &minFim, &secFim, &milFim);
					i++;
				}
			}

			if (ret != 8)
			{
				continue; /* Data is not in correct format */
			}

			Pts = (int64_t)(horIni * 3600 + minIni * 60 + secIni) * 1000 + milIni;
			Duration = (int64_t)(horFim * 3600 + minFim * 60 + secFim) * 1000  + milFim - Pts; 

			/* standardize hard break: '\N'->'\n' http://docs.aegisub.org/3.2/ASS_Tags/ */
			while((p_newline = strstr(ptr, "\\N")) != NULL)
			{
				*(p_newline + 1) = 'n';
			}

			if (context->playback->Command(context, PLAYBACK_PTS, &playPts) < 0)
			{
				sub_err("Error in get playback pts!\n");
			}

			unsigned long long int curPts = playPts / 90.0;
			if (Pts + Duration < curPts)
			{
				sub_printf(20, "current sub has already ended, skip\n");
				continue;
			}

			while (context && context->playback && context->playback->isPlaying && fsub && Pts > curPts + 2000.0)
			{
				sub_printf(20, "current sub in the future, waiting\n");
				usleep(900000);
				context->playback->Command(context, PLAYBACK_PTS, &playPts);
				if (playPts >= curPts * 90.0)
				{
					curPts = playPts / 90.0;
				}
				else
				{
					sub_printf(20, "seek back, restart read\n");
					pos = -1;
					rewind(fsub);
					break;
				}
			}

			Text = strdup(ptr);

			if(Text != NULL)
			{
				data_to_manager(context, Text, Pts, Duration);
				free(Text);
				Text = NULL;
			}
		}
	} /* while */

	hasThreadStarted = 0;

	if(Text)
	{
		data_to_manager(context, Text, Pts, Duration);
		free(Text);
		Text = NULL;
	}

	sub_printf(20, "thread has ended\n");
	return NULL;
}

static void SubtitleManagerAdd(Context_t  *context __attribute__((unused)), SubTrack_t track)
{
	sub_printf(20, "%s %d\n", track.File, track.Id);

	if (Tracks == NULL) {
		Tracks = malloc(sizeof(SubTrack_t) *TRACKWRAP);
	}

	if (TrackCount < TRACKWRAP) {
		Tracks[TrackCount].File = strdup(track.File);
		Tracks[TrackCount].Id = track.Id;
		TrackCount++;
	}
}

static void SubtitleManagerDel(Context_t *context __attribute__((unused)))
{
	int i = 0;

	sub_printf(20, "\n");

	if(Tracks != NULL) {
		for (i = 0; i < TrackCount; i++) {
			if (Tracks[i].File != NULL)
				free(Tracks[i].File);
			Tracks[i].File = NULL;
		}
		free(Tracks);
		Tracks = NULL;
	}

	TrackCount = 0;
}


static int GetSubtitle(Context_t  *context, char * Filename)
{
	struct dirent *dirzeiger;
	DIR  *  dir;
	int     i                    = 0;
	char *  copyFilename         = NULL;
	char *  FilenameExtension    = NULL;
	char *  FilenameFolder       = NULL;
	char *  FilenameShort        = NULL;

	sub_printf(20, "\n");

	if (Filename == NULL)
	{
		sub_err("Filename NULL\n");
		return cERR_SUBTITLE_ERROR;
	}

	sub_printf(20, "file: %s\n", Filename);

	copyFilename = strdup(Filename);

	if (copyFilename == NULL)
	{
		sub_err("copyFilename NULL\n");
		return cERR_SUBTITLE_ERROR;
	}

	FilenameFolder = dirname(copyFilename);

	sub_printf(20, "folder: %s\n", FilenameFolder);

	FilenameExtension = getExtension(copyFilename);

	if (FilenameExtension == NULL)
	{
		sub_err("FilenameExtension NULL\n");
		free(copyFilename);
		return cERR_SUBTITLE_ERROR;
	}

	sub_printf(20, "ext: %s\n", FilenameExtension);

	FilenameShort = basename(copyFilename);

	/* cut extension */
	FilenameShort[strlen(FilenameShort) - strlen(FilenameExtension) - 1] = '\0';

	sub_printf(20, "basename: %s\n", FilenameShort);
	sub_printf(20, "%s\n%s | %s | %s\n", copyFilename, FilenameFolder, FilenameShort, FilenameExtension);

	if((dir = opendir(FilenameFolder)) != NULL)
	{
		while((dirzeiger = readdir(dir)) != NULL)
		{
			char subtitleFilename[PATH_MAX];
			char *subtitleExtension = NULL;

			sub_printf(20, "%s\n",(*dirzeiger).d_name);

			strcpy(subtitleFilename, (*dirzeiger).d_name);

			// Extension of Relativ Subtitle File Name
			subtitleExtension = getExtension(subtitleFilename);

			if (subtitleExtension == NULL)
				continue;

			if (strcmp(subtitleExtension, "srt") != 0 && strcmp(subtitleExtension, "ssa") != 0 && strcmp(subtitleExtension, "ass") != 0)
				continue;

			/* cut extension */
			subtitleFilename[strlen(subtitleFilename) - strlen(subtitleExtension) - 1] = '\0';

			sub_printf(20, "%s %s\n", FilenameShort, subtitleFilename);

			if (strncmp(FilenameShort, subtitleFilename,strlen(FilenameShort)) == 0)
			{
				char absSubtitleFileName[PATH_MAX];
				int ioff = 0;

				/* found something of interest, so now make an absolut path name */
				sprintf(absSubtitleFileName, "%s/%s.%s", FilenameFolder, subtitleFilename, subtitleExtension);

				sub_printf(10, "Add subtitle %s\n", absSubtitleFileName);

				Track_t Subtitle;
				memset(&Subtitle, 0, sizeof(Subtitle));
				if (strcmp(subtitleExtension, "srt") != 0)
				{
					Subtitle.Encoding = "S_TEXT/SSA";
					ioff = i + TEXTSSAOFFSET;
				}
				else
				{
					Subtitle.Encoding = "S_TEXT/SRT";
					ioff = i + TEXTSRTOFFSET;
				}
				Subtitle.Name = subtitleExtension;
				Subtitle.Id = ioff;

				SubTrack_t TextSubtitle = {
						absSubtitleFileName,
						ioff,
				};

				SubtitleManagerAdd(context, TextSubtitle);

				i++;

				context->manager->subtitle->Command(context, MANAGER_ADD, &Subtitle);
			}
		} /* while */
	closedir(dir);
	} /* if dir */

	free(copyFilename);

	sub_printf(20, "<\n");
	return cERR_SUBTITLE_NO_ERROR;
}

static int OpenSubtitle(Context_t *context __attribute__((unused)), int pid)
{
	sub_printf(20, "\n");

	if(pid < TEXTSRTOFFSET)
	{
		sub_err("trackid not for text subtitles\n");
		return cERR_SUBTITLE_ERROR;
	}

	int trackid;
	for (trackid = 0; trackid < TrackCount; trackid++)
	if (Tracks[trackid].Id == pid)
		break;

	if(trackid == TrackCount)
	{
		sub_err("trackid not for us\n");
		return cERR_SUBTITLE_ERROR;
	}

	sub_printf(20, "%s\n", Tracks[trackid].File);

	fsub = fopen(Tracks[trackid].File, "rb");

	sub_printf(20, "%s\n", fsub ? "fsub!=NULL" : "fsub==NULL");

	if(!fsub)
	{
		sub_err("cannot open file %s\n", Tracks[trackid].File);
		return cERR_SUBTITLE_ERROR;
	}

	if (pid < TEXTSSAOFFSET)
	{
		SrtSubtitles = 1;
	}
	else
	{
		SrtSubtitles = 0;
	}

	return cERR_SUBTITLE_NO_ERROR;
}

static int CloseSubtitle(Context_t *context __attribute__((unused)))
{
	sub_printf(20, "\n");

	if(fsub)
		fclose(fsub);

	/* this closes the thread! */
	fsub = NULL;

	hasThreadStarted = 0;

	return cERR_SUBTITLE_NO_ERROR;
}

static int SwitchSubtitle(Context_t *context, int* arg)
{
	sub_printf(20, "arg:%d\n", *arg);

	int ret = cERR_SUBTITLE_NO_ERROR;

	ret = CloseSubtitle(context);

	if (( (ret |= OpenSubtitle(context, *arg)) == cERR_SUBTITLE_NO_ERROR) && (!hasThreadStarted))
	{
		pthread_attr_t attr;
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		pthread_create (&thread_sub, &attr, &SubtitleThread, context);

		hasThreadStarted = 1;
	}

	return ret;
}

static int SubtitleDel(Context_t *context)
{
	sub_printf(20, "\n");

	int ret = cERR_SUBTITLE_NO_ERROR;

	ret = CloseSubtitle(context);
	SubtitleManagerDel(context);

	return ret;
}

static int Command(Context_t *context, ContainerCmd_t command, void * argument)
{
	int ret = cERR_SUBTITLE_NO_ERROR;

	sub_printf(20, "\n");

	switch(command)
	{
		case CONTAINER_INIT:
		{
			char * filename = (char *)argument;
			ret = GetSubtitle(context, filename);
			break;
		}
		case CONTAINER_DEL:
			ret = SubtitleDel(context);
			break;
		case CONTAINER_SWITCH_SUBTITLE:
			ret = SwitchSubtitle(context, (int *)argument);
			break;
		default:
			sub_err("ConatinerCmd not supported! %d\n", command);
			break;
	}

	sub_printf(20, "ret = %d\n", ret);
	return 0;
}

static char *SubtitleCapabilities[] = { "srt", "ssa", "ass", NULL };

Container_t TextSubtitleContainer = {
	"SRT",
	&Command,
	SubtitleCapabilities
};
