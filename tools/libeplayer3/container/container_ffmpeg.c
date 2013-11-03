/*
 * Container handling for all stream's handled by ffmpeg
 * konfetti 2010; based on code from crow
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

/* ***************************** */
/* Includes                      */
/* ***************************** */

#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <memory.h>
#include <string.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/poll.h>
#include <pthread.h>

#include <libavutil/avutil.h>
#include <libavutil/time.h>
#include <libavformat/avformat.h>

#include "common.h"
#include "misc.h"
#include "debug.h"
#include "aac.h"
#include "pcm.h"
#include "ffmpeg_metadata.h"
#include "subtitle.h"

/* ***************************** */
/* Makros/Constants              */
/* ***************************** */

#define MAX_AUDIO_FRAME_SIZE 192000
#define FFMPEG_DEBUG

#ifdef FFMPEG_DEBUG

static short debug_level = 10;

#define ffmpeg_printf(level, fmt, x...) do { \
	if (debug_level >= level) printf("[%s:%s] " fmt, __FILE__, __FUNCTION__, ## x); } while (0)
#else
#define ffmpeg_printf(level, fmt, x...)
#endif

#ifndef FFMPEG_SILENT
#define ffmpeg_err(fmt, x...) do { printf("[%s:%s] " fmt, __FILE__, __FUNCTION__, ## x); } while (0)
#else
#define ffmpeg_err(fmt, x...)
#endif

/* Error Constants */
#define cERR_CONTAINER_FFMPEG_NO_ERROR        0
#define cERR_CONTAINER_FFMPEG_INIT           -1
#define cERR_CONTAINER_FFMPEG_NOT_SUPPORTED  -2
#define cERR_CONTAINER_FFMPEG_INVALID_FILE   -3
#define cERR_CONTAINER_FFMPEG_RUNNING        -4
#define cERR_CONTAINER_FFMPEG_NOMEM          -5
#define cERR_CONTAINER_FFMPEG_OPEN           -6
#define cERR_CONTAINER_FFMPEG_STREAM         -7
#define cERR_CONTAINER_FFMPEG_NULL           -8
#define cERR_CONTAINER_FFMPEG_ERR            -9
#define cERR_CONTAINER_FFMPEG_END_OF_FILE    -10

static const char* FILENAME = __FILE__;

/* ***************************** */
/* Types                         */
/* ***************************** */

/* ***************************** */
/* Varaibles                     */
/* ***************************** */
static pthread_mutex_t mutex;
volatile static int hasPlayThreadStarted = 0;
static pthread_t PlayThread;


static AVFormatContext*   avContext = NULL;
static AVDictionary *avio_opts = NULL;

volatile static unsigned int isContainerRunning = 0;

static long long int latestPts = 0;

/* ***************************** */
/* Prototypes                    */
/* ***************************** */
static int container_ffmpeg_seek_bytes(off_t pos);
static int container_ffmpeg_seek(Context_t *context, float sec);
static int container_ffmpeg_seek_rel(Context_t *context, off_t pos, long long int pts, float sec);
static int container_ffmpeg_seek_bytes_rel(off_t start, off_t bytes);

/* ***************************** */
/* MISC Functions                */
/* ***************************** */
volatile static int mutexInitialized = 0;

static void initMutex(void)
{
    mutexInitialized = 1;
    pthread_mutex_init(&mutex, NULL);
}

void getMutex(const char *filename, const char *function, int line) {
	ffmpeg_printf(100, "::%d requesting mutex\n", line);

        if (!mutexInitialized)
        {
            initMutex();
        }
	pthread_mutex_lock(&mutex);

	ffmpeg_printf(100, "::%d received mutex\n", line);
}

void releaseMutex(const char *filename, const const char *function, int line) {
	pthread_mutex_unlock(&mutex);

	ffmpeg_printf(100, "::%d released mutex\n", line);
}

static char* Codec2Encoding(AVCodecContext *codec, int* version)
{
	fprintf(stderr, "Codec ID: %ld (%.8lx)\n", (long)codec->codec_id, (long)codec->codec_id);
	switch (codec->codec_id)
	{
		case AV_CODEC_ID_MPEG1VIDEO:
			return "V_MPEG1";
		case AV_CODEC_ID_MPEG2VIDEO:
			return "V_MPEG1";
		case AV_CODEC_ID_H263:
		case AV_CODEC_ID_H263P:
		case AV_CODEC_ID_H263I:
			return "V_H263";
		case AV_CODEC_ID_FLV1:
			return "V_FLV";
		case AV_CODEC_ID_VP5:
		case AV_CODEC_ID_VP6:
		case AV_CODEC_ID_VP6F:
			return "V_VP6";
		case AV_CODEC_ID_RV10:
		case AV_CODEC_ID_RV20:
			return "V_RMV";
		case AV_CODEC_ID_MPEG4:
#if LIBAVCODEC_VERSION_MAJOR < 53
		case AV_CODEC_ID_XVID:
#endif
		case AV_CODEC_ID_MSMPEG4V1:
		case AV_CODEC_ID_MSMPEG4V2:
		case AV_CODEC_ID_MSMPEG4V3:
			return "V_MSCOMP";
		case AV_CODEC_ID_WMV1:
			*version = 1;
			return "V_WMV";
		case AV_CODEC_ID_WMV2:
			*version = 2;
			return "V_WMV";
		case AV_CODEC_ID_WMV3:
			*version = 3;
			return "V_WMV";
		case AV_CODEC_ID_VC1:
			return "V_VC1";
		case AV_CODEC_ID_H264:
#if LIBAVCODEC_VERSION_MAJOR < 54
		case AV_CODEC_ID_FFH264:
#endif
			return "V_MPEG4/ISO/AVC";
		case AV_CODEC_ID_AVS:
			return "V_AVS";
		case AV_CODEC_ID_MP2:
			return "A_MPEG/L3";
		case AV_CODEC_ID_MP3:
			return "A_MP3";
		case AV_CODEC_ID_AAC:
			return "A_AAC";
		case AV_CODEC_ID_AC3:
			return "A_AC3";
		case AV_CODEC_ID_DTS:
			return "A_DTS";
		case AV_CODEC_ID_WMAV1:
		case AV_CODEC_ID_WMAV2:
		case 86056:
		case AV_CODEC_ID_WMAPRO:
			return "A_WMA";
		case AV_CODEC_ID_MLP:
			return "A_MLP";
		case AV_CODEC_ID_RA_144:
			return "A_RMA";
		case AV_CODEC_ID_RA_288:
			return "A_RMA";
		case AV_CODEC_ID_VORBIS:
			return "A_IPCM";
		case AV_CODEC_ID_FLAC:
			return "A_IPCM";
		case AV_CODEC_ID_PCM_S16LE:
			return "A_PCM";
			/* subtitle */
		case AV_CODEC_ID_SSA:
			return "S_TEXT/ASS"; /* Hellmaster1024: seems to be ASS instead of SSA */
		case AV_CODEC_ID_TEXT: /* Hellmaster1024: i dont have most of this, but lets hope it is normal text :-) */
		case AV_CODEC_ID_DVD_SUBTITLE:
		case AV_CODEC_ID_DVB_SUBTITLE:
		case AV_CODEC_ID_XSUB:
		case AV_CODEC_ID_MOV_TEXT:
		case AV_CODEC_ID_HDMV_PGS_SUBTITLE:
		case AV_CODEC_ID_DVB_TELETEXT:
			//    case CODEC_ID_DVB_TELETEXT:
			//        return "S_TEXT/SRT"; /* fixme */
		case AV_CODEC_ID_SRT:
			return "S_TEXT/SRT"; /* fixme */
		default:
			// Default to injected-pcm for unhandled audio types.
			if (codec->codec_type == AVMEDIA_TYPE_AUDIO)
				return "A_IPCM";
			if (codec->codec_type == AVMEDIA_TYPE_SUBTITLE)
				return "S_TEXT/SRT";
			ffmpeg_err("Codec ID %ld (%.8lx) not found\n", (long)codec->codec_id, (long)codec->codec_id);
	}
	return NULL;
}

long long int calcPts(AVStream* stream, int64_t pts)
{
	if (!stream)
	{
		ffmpeg_err("stream / packet null\n");
		return INVALID_PTS_VALUE;
	}

	if(pts == AV_NOPTS_VALUE)
		pts = INVALID_PTS_VALUE;
	else if (avContext->start_time == AV_NOPTS_VALUE)
		pts = 90000.0 * (double)pts * av_q2d(stream->time_base);
	else
		pts = 90000.0 * (double)pts * av_q2d(stream->time_base) - 90000.0 * avContext->start_time / AV_TIME_BASE;

	if (pts & 0x8000000000000000ull)
		pts = INVALID_PTS_VALUE;

	return pts;
}

/*Hellmaster1024: get the Duration of the subtitle from the SSA line*/
float getDurationFromSSALine(unsigned char* line){
	int i,h,m,s,ms;
	char* Text = strdup((char*) line);
	char* ptr1;
	char* ptr[10];
	long int msec;

	ptr1 = Text;
	ptr[0]=Text;
	for (i=0; i < 3 && *ptr1 != '\0'; ptr1++) {
		if (*ptr1 == ',') {
			ptr[++i]=ptr1+1;
			*ptr1 = '\0';
		}
	}

	sscanf(ptr[2],"%d:%d:%d.%d",&h,&m,&s,&ms);
	msec = (ms*10) + (s*1000) + (m*60*1000) + (h*24*60*1000);
	sscanf(ptr[1],"%d:%d:%d.%d",&h,&m,&s,&ms);
	msec -= (ms*10) + (s*1000) + (m*60*1000) + (h*24*60*1000);

	ffmpeg_printf(10, "%s %s %f\n", ptr[2], ptr[1], (float) msec / 1000.0);

	free(Text);
	return (float)msec/1000.0;
}

/* search for metatdata in context and stream
 * and map it to our metadata.
 */

#if LIBAVCODEC_VERSION_MAJOR < 54
static char* searchMeta(AVMetadata *metadata, char* ourTag)
#else
static char* searchMeta(AVDictionary * metadata, char* ourTag)
#endif
{
#if LIBAVCODEC_VERSION_MAJOR < 54
	AVMetadataTag *tag = NULL;
#else
	AVDictionaryEntry *tag = NULL;
#endif
	int i = 0;

	while (metadata_map[i] != NULL)
	{
		if (strcmp(ourTag, metadata_map[i]) == 0)
		{
#if LIBAVCODEC_VERSION_MAJOR < 54
			while ((tag = av_metadata_get(metadata, "", tag, AV_METADATA_IGNORE_SUFFIX)))
#else
				while ((tag = av_dict_get(metadata, "", tag, AV_DICT_IGNORE_SUFFIX)))
#endif
				{
					if (strcmp(tag->key, metadata_map[ i + 1 ]) == 0)
						return tag->value;
				}
		}
		i++;
	}
	return NULL;
}

/* **************************** */
/* Worker Thread                */
/* **************************** */

static void FFMPEGThread(Context_t *context) {
	AVPacket   packet;
        AVFrame *decoded_frame = NULL;
	off_t lastSeek = -1;
	long long int lastPts = -1, currentVideoPts = -1, currentAudioPts = -1, showtime = 0, bofcount = 0;
	int           err = 0, audioMute = 0;
	AudioVideoOut_t avOut;

	/* Softdecoding buffer*/
	unsigned char *samples = NULL;


	ffmpeg_printf(10, "\n");

	while ( context->playback->isCreationPhase )
	{
		ffmpeg_err("Thread waiting for end of init phase...\n");
		usleep(1000);
	}
	ffmpeg_printf(10, "Running!\n");

	while ( context && context->playback && context->playback->isPlaying )
	{

		//IF MOVIE IS PAUSED, WAIT
		if (context->playback->isPaused) {
			ffmpeg_printf(20, "paused\n");
			usleep(100000);
			continue;
		}

		if (context->playback->isSeeking) {
			ffmpeg_printf(10, "seeking\n");
			usleep(100000);
			continue;
		}

		if (!context->playback || !context->playback->isPlaying) {
			continue;
		}	 

		if (context->playback->BackWard && av_gettime() >= showtime)
		{
			audioMute = 1;
			context->output->Command(context, OUTPUT_CLEAR, "video");

			if(bofcount == 1)
			{
				showtime = av_gettime();
				usleep(100000);
				continue;
			}

			if(lastPts == -1)
			{
				lastPts = (currentVideoPts != -1)? currentVideoPts:currentAudioPts;
			}


			if((err = container_ffmpeg_seek_rel(context, lastSeek, lastPts, (float) context->playback->Speed)) < 0)
			{
				ffmpeg_err( "Error seeking\n");

				if (err == cERR_CONTAINER_FFMPEG_END_OF_FILE)
				{
					bofcount = 1;
				}
			}

			lastPts = lastPts + (context->playback->Speed * 90000);
			showtime = av_gettime() + 300000; //jump back all 300ms
		}
		else if(!context->playback->BackWard && audioMute)
		{
			lastPts = -1;
			bofcount = 0;
			showtime = 0;
			audioMute = 0;
			context->output->Command(context, OUTPUT_AUDIOMUTE, "0");
		}

		getMutex(FILENAME, __FUNCTION__,__LINE__);

		if (av_read_frame(avContext, &packet) == 0 )
		{
			long long int pts;
			Track_t * videoTrack = NULL;
			Track_t * audioTrack = NULL;
			Track_t * subtitleTrack = NULL;
	                Track_t * dvbsubtitleTrack = NULL;
	                Track_t * teletextTrack = NULL;

			int pid = avContext->streams[packet.stream_index]->id;

			if (context->manager->video->Command(context, MANAGER_GET_TRACK, &videoTrack) < 0)
				ffmpeg_err("error getting video track\n");

			if (context->manager->audio->Command(context, MANAGER_GET_TRACK, &audioTrack) < 0)
				ffmpeg_err("error getting audio track\n");

			if (context->manager->subtitle->Command(context, MANAGER_GET_TRACK, &subtitleTrack) < 0)
				ffmpeg_err("error getting subtitle track\n");

			if (context->manager->dvbsubtitle->Command(context, MANAGER_GET_TRACK, &dvbsubtitleTrack) < 0)
				ffmpeg_err("error getting dvb subtitle track\n");

			if (context->manager->teletext->Command(context, MANAGER_GET_TRACK, &teletextTrack) < 0)
				ffmpeg_err("error getting teletext track\n");

			ffmpeg_printf(200, "packet.size %d - index %d\n", packet.size, pid);

			if (videoTrack != NULL && videoTrack->Id == pid)
			{
				currentVideoPts = videoTrack->pts = pts = calcPts(videoTrack->stream, packet.pts);

				if ((currentVideoPts > latestPts) && (currentVideoPts != INVALID_PTS_VALUE))
					latestPts = currentVideoPts;

				ffmpeg_printf(200, "VideoTrack index = %d %lld\n",pid, currentVideoPts);

				avOut.data       = packet.data;
				avOut.len        = packet.size;
				avOut.pts        = pts;
				avOut.extradata  = videoTrack->extraData;
				avOut.extralen   = videoTrack->extraSize;
				avOut.frameRate  = videoTrack->frame_rate;
				avOut.timeScale  = videoTrack->TimeScale;
				avOut.width      = videoTrack->width;
				avOut.height     = videoTrack->height;
				avOut.type       = OUTPUT_TYPE_VIDEO;

				if (context->output->video->Write(context, &avOut) < 0) {
					ffmpeg_err("writing data to video device failed\n");
				}
			}
			else if (audioTrack != NULL && audioTrack->Id == pid) 
			{
				currentAudioPts = audioTrack->pts = pts = calcPts(audioTrack->stream, packet.pts);

				if ((currentAudioPts > latestPts) && (!videoTrack))
					latestPts = currentAudioPts;

				ffmpeg_printf(200, "AudioTrack index = %d\n",pid);
				if (audioTrack->inject_raw_pcm == 1)
				{
					ffmpeg_printf(200,"write audio raw pcm\n");

					pcmPrivateData_t extradata;
					extradata.uNoOfChannels = ((AVStream*) audioTrack->stream)->codec->channels;
					extradata.uSampleRate = ((AVStream*) audioTrack->stream)->codec->sample_rate;
					extradata.uBitsPerSample = 16;
					extradata.bLittleEndian = 1;

					avOut.data       = packet.data;
					avOut.len        = packet.size;
					avOut.pts        = pts;
					avOut.extradata  = (unsigned char*)&extradata;
					avOut.extralen   = sizeof(extradata);
					avOut.frameRate  = 0;
					avOut.timeScale  = 0;
					avOut.width      = 0;
					avOut.height     = 0;
					avOut.type       = OUTPUT_TYPE_AUDIO;

					if (!context->playback->BackWard && context->output->audio->Write(context, &avOut) < 0)
					{
						ffmpeg_err("(raw pcm) writing data to audio device failed\n");
					}
				}
				else if (audioTrack->inject_as_pcm == 1)
				{
					int      bytesDone = 0;
					unsigned int samples_size = MAX_AUDIO_FRAME_SIZE;
					AVPacket avpkt;
					avpkt = packet;

					// This way the buffer is only allocated if we really need it
					if(samples == NULL)
						samples = (unsigned char *)malloc(samples_size);
                                        if (!decoded_frame) 
                                        {
                                            decoded_frame = avcodec_alloc_frame(); 
                                        } else
                                            avcodec_get_frame_defaults(decoded_frame);

					while(avpkt.size > 0)
					{
						int decoded_data_size = samples_size;
                                                int got_frame = 0;

                                                AVCodecContext* c = ((AVStream*) audioTrack->stream)->codec;
						bytesDone = avcodec_decode_audio4(c, decoded_frame, &got_frame, &avpkt);

						if(bytesDone < 0) // Error Happend
							break;

						avpkt.data += bytesDone;
						avpkt.size -= bytesDone;

						if(!got_frame)
							continue;

                                                decoded_data_size = av_samples_get_buffer_size(NULL, c->channels, decoded_frame->nb_samples, c->sample_fmt, 1);
						pcmPrivateData_t extradata;
						extradata.uNoOfChannels = ((AVStream*) audioTrack->stream)->codec->channels;
						extradata.uSampleRate = ((AVStream*) audioTrack->stream)->codec->sample_rate;
						extradata.uBitsPerSample = 16;
						extradata.bLittleEndian = 1;

						avOut.data       = decoded_frame->data[0];
						avOut.len        = decoded_data_size;

						avOut.pts        = pts;
						avOut.extradata  = (char*)&extradata;
						avOut.extralen   = sizeof(extradata);
						avOut.frameRate  = 0;
						avOut.timeScale  = 0;
						avOut.width      = 0;
						avOut.height     = 0;
						avOut.type       = OUTPUT_TYPE_AUDIO;

						if (!context->playback->BackWard && context->output->audio->Write(context, &avOut) < 0)
							ffmpeg_err("writing data to audio device failed\n");
					}
				}
				else
				{
					ffmpeg_printf(200,"write audio aac\n");

					avOut.data       = packet.data;
					avOut.len        = packet.size;
					avOut.pts        = pts;
					if (audioTrack->have_aacheader == 1)
					{
						avOut.extradata  = audioTrack->aacbuf;
						avOut.extralen   = audioTrack->aacbuflen;
					}
					else
					{
						avOut.extradata  = NULL;
						avOut.extralen   = 0;
					}
					avOut.frameRate  = 0;
					avOut.timeScale  = 0;
					avOut.width      = 0;
					avOut.height     = 0;
					avOut.type       = OUTPUT_TYPE_AUDIO;

					if (!context->playback->BackWard && context->output->audio->Write(context, &avOut) < 0)
					{
						ffmpeg_err("(aac) writing data to audio device failed\n");
					}
				}
			}
			else if (subtitleTrack != NULL && subtitleTrack->Id == pid) 
			{
				float duration=3.0;
				ffmpeg_printf(100, "subtitleTrack->stream %p \n", subtitleTrack->stream);

				pts = calcPts(subtitleTrack->stream, packet.pts);

				if ((pts > latestPts) && (!videoTrack) && (!audioTrack))
					latestPts = pts;

				/*Hellmaster1024: in mkv the duration for ID_TEXT is stored in convergence_duration */
				ffmpeg_printf(20, "Packet duration %d\n", packet.duration);
				ffmpeg_printf(20, "Packet convergence_duration %lld\n", packet.convergence_duration);

				if(packet.duration != 0 && packet.duration != AV_NOPTS_VALUE )
					duration=((float)packet.duration)/1000.0;
				else if(packet.convergence_duration != 0 && packet.convergence_duration != AV_NOPTS_VALUE )
					duration=((float)packet.convergence_duration)/1000.0;
				else if(((AVStream*)subtitleTrack->stream)->codec->codec_id == CODEC_ID_SSA)
				{
					/*Hellmaster1024 if the duration is not stored in packet.duration or
					  packet.convergence_duration we need to calculate it any other way, for SSA it is stored in
					  the Text line*/
					duration = getDurationFromSSALine(packet.data);
				} else {
					/* no clue yet */
				}

				/* konfetti: I've found cases where the duration from getDurationFromSSALine
				 * is zero (start end and are really the same in text). I think it make's
				 * no sense to pass those.
				 */
				if (duration > 0.0)
				{
					/* is there a decoder ? */
					if (avcodec_find_decoder(((AVStream*) subtitleTrack->stream)->codec->codec_id) != NULL)
					{
						AVSubtitle sub;
						int got_sub_ptr;

						if (avcodec_decode_subtitle2(((AVStream*) subtitleTrack->stream)->codec, &sub, &got_sub_ptr, &packet) < 0)
						{
							ffmpeg_err("error decoding subtitle\n");
						} else
						{
							int i;
							ffmpeg_printf(0, "format %d\n", sub.format);
							ffmpeg_printf(0, "start_display_time %d\n", sub.start_display_time);
							ffmpeg_printf(0, "end_display_time %d\n", sub.end_display_time);
							ffmpeg_printf(0, "num_rects %d\n", sub.num_rects);
							ffmpeg_printf(0, "pts %lld\n", sub.pts);

							for (i = 0; i < sub.num_rects; i++)
							{
								ffmpeg_printf(0, "x %d\n", sub.rects[i]->x);
								ffmpeg_printf(0, "y %d\n", sub.rects[i]->y);
								ffmpeg_printf(0, "w %d\n", sub.rects[i]->w);
								ffmpeg_printf(0, "h %d\n", sub.rects[i]->h);
								ffmpeg_printf(0, "nb_colors %d\n", sub.rects[i]->nb_colors);
								ffmpeg_printf(0, "type %d\n", sub.rects[i]->type);
								ffmpeg_printf(0, "text %s\n", sub.rects[i]->text);
								ffmpeg_printf(0, "ass %s\n", sub.rects[i]->ass);
								// pict ->AVPicture
							}
						}

						if(((AVStream*)subtitleTrack->stream)->codec->codec_id == CODEC_ID_SSA)
						{
							SubtitleData_t data;
							ffmpeg_printf(10, "videoPts %lld\n", currentVideoPts);

							data.data      = packet.data;
							data.len       = packet.size;
							data.extradata = subtitleTrack->extraData;
							data.extralen  = subtitleTrack->extraSize;
							data.pts       = pts;
							data.duration  = duration;

							context->container->assContainer->Command(context, CONTAINER_DATA, &data);
						}
						else
						{
							/* hopefully native text ;) */
							unsigned char* line = text_to_ass((char *)packet.data,pts/90,duration);
							ffmpeg_printf(50,"text line is %s\n",(char *)packet.data);
							ffmpeg_printf(50,"Sub line is %s\n",line);
							ffmpeg_printf(20, "videoPts %lld %f\n", currentVideoPts,currentVideoPts/90000.0);
							SubtitleData_t data;
							data.data      = line;
							data.len       = strlen((char*)line);
							data.extradata = DEFAULT_ASS_HEAD;
							data.extralen  = strlen(DEFAULT_ASS_HEAD);
							data.pts       = pts;
							data.duration  = duration;

							context->container->assContainer->Command(context, CONTAINER_DATA, &data);
							free(line);
						}
					}
				}

				if (packet.data)
					av_free_packet(&packet);
			}
			else if (dvbsubtitleTrack && dvbsubtitleTrack->Id == pid) 
			{
				dvbsubtitleTrack->pts = pts = calcPts(dvbsubtitleTrack->stream, packet.pts);

				ffmpeg_printf(200, "DvbSubTitle index = %d\n",pid);

				avOut.data       = packet.data;
				avOut.len        = packet.size;
				avOut.pts        = pts;
				avOut.extradata  = NULL;
				avOut.extralen   = 0;
				avOut.frameRate  = 0;
				avOut.timeScale  = 0;
				avOut.width      = 0;
				avOut.height     = 0;
				avOut.type       = OUTPUT_TYPE_DVBSUBTITLE;

				if (context->output->dvbsubtitle->Write(context, &avOut) < 0)
				{
					//ffmpeg_err("writing data to dvbsubtitle fifo failed\n");
				}
			}
			else if (teletextTrack && teletextTrack->Id == pid)
			{
				teletextTrack->pts = pts = calcPts(teletextTrack->stream, packet.pts);

				ffmpeg_printf(200, "TeleText index = %d\n",pid);

				avOut.data       = packet.data;
				avOut.len        = packet.size;
				avOut.pts        = pts;
				avOut.extradata  = NULL;
				avOut.extralen   = 0;
				avOut.frameRate  = 0;
				avOut.timeScale  = 0;
				avOut.width      = 0;
				avOut.height     = 0;
				avOut.type       = OUTPUT_TYPE_TELETEXT;

				if (context->output->teletext->Write(context, &avOut) < 0)
				{
					//ffmpeg_err("writing data to teletext fifo failed\n");
				}
			}
			else  
			{
				ffmpeg_err("no data ->end of file reached ? \n");
				av_free_packet(&packet);
				releaseMutex(FILENAME, __FUNCTION__,__LINE__);
				break;
			}
			av_free_packet(&packet);
			releaseMutex(FILENAME, __FUNCTION__,__LINE__);
		} 
	}/* while */
        // Freeing the allocated buffer for softdecoding
        if (decoded_frame != NULL) {
                av_free(samples);
                decoded_frame = NULL;
        }
        hasPlayThreadStarted = 0;
        ffmpeg_printf(10, "terminating\n");
}

/* **************************** */
/* Container part for ffmpeg    */
/* **************************** */

static int terminating = 0;
static int interrupt_cb(void *ctx)
{
	PlaybackHandler_t *p = (PlaybackHandler_t *)ctx;
	return p->abortRequested;
}


static int container_ffmpeg_init_unsafe(Context_t *context, char * filename)
{
	int err;

	ffmpeg_printf(10, ">\n");

	if (filename == NULL) {
		ffmpeg_err("filename NULL\n");
		return cERR_CONTAINER_FFMPEG_NULL;
	}

	if (context == NULL) {
		ffmpeg_err("context NULL\n");
		return cERR_CONTAINER_FFMPEG_NULL;
	}

	ffmpeg_printf(10, "filename %s\n", filename);

	if (isContainerRunning) {
		ffmpeg_err("ups already running?\n");
		return cERR_CONTAINER_FFMPEG_RUNNING;
	}

	/* initialize ffmpeg */
	avcodec_register_all();
	av_register_all();
	avformat_network_init();
	//    av_log_set_level( AV_LOG_DEBUG );

	context->playback->abortRequested = 0;
	avContext = avformat_alloc_context();
	avContext->interrupt_callback.callback = interrupt_cb;
	avContext->interrupt_callback.opaque = context->playback;

	if(strstr(filename, "http://") == filename)
	{
		//av_dict_set(&avio_opts, "timeout", "20000", 0); //20sec
		if ((err = avformat_open_input(&avContext, filename, NULL, &avio_opts)) != 0)
		{
			char error[512];

			ffmpeg_err("avformat_open_input failed %d (%s)\n", err, filename);
			av_strerror(err, error,  sizeof error);
			ffmpeg_err("Cause: %s\n", error);

			if(avio_opts != NULL) av_dict_free(&avio_opts);
			return cERR_CONTAINER_FFMPEG_OPEN;
		}
	}
	else
	{    
		if ((err = avformat_open_input(&avContext, filename, NULL, 0)) != 0)
		{
			char error[512];

			ffmpeg_err("avformat_open_input failed %d (%s)\n", err, filename);
			av_strerror(err, error, 512);
			ffmpeg_err("Cause: %s\n", error);

			if(avio_opts != NULL) av_dict_free(&avio_opts);
			return cERR_CONTAINER_FFMPEG_OPEN;
		}
	}

	avContext->iformat->flags |= AVFMT_SEEK_TO_PTS;
	avContext->flags |= AVFMT_FLAG_GENPTS;

	if (context->playback->noprobe)
		avContext->max_analyze_duration = 1;

	ffmpeg_printf(20, "find_streaminfo\n");

	if (avformat_find_stream_info(avContext, NULL) < 0) {
		ffmpeg_err("Error avformat_find_stream_info\n");
#ifdef this_is_ok
		/* crow reports that sometimes this returns an error
		 * but the file is played back well. so remove this
		 * until other works are done and we can prove this.
		 */
		avformat_close_input(&avContext);
		return cERR_CONTAINER_FFMPEG_STREAM;
#endif
	}

	terminating = 0;
	latestPts = 0;
	isContainerRunning = 1;
	int res = container_ffmpeg_update_tracks(context, filename, 1);
	return res;
}

static int container_ffmpeg_init(Context_t *context, char * filename)
{
	int ret = -1; 
	getMutex(FILENAME, __FUNCTION__,__LINE__);
	ret = container_ffmpeg_init_unsafe(context, filename);
	releaseMutex(FILENAME, __FUNCTION__,__LINE__);
	return ret;
}

int container_ffmpeg_update_tracks(Context_t *context, char *filename, int initial)
{
	if (terminating)
		return cERR_CONTAINER_FFMPEG_NO_ERROR;

	Track_t * audioTrack = NULL;
	Track_t * subtitleTrack = NULL;
	Track_t * dvbsubtitleTrack = NULL;
	Track_t * teletextTrack = NULL;

	context->manager->audio->Command(context, MANAGER_GET_TRACK, &audioTrack);
	if (initial)
		context->manager->subtitle->Command(context, MANAGER_GET_TRACK, &subtitleTrack);
	context->manager->dvbsubtitle->Command(context, MANAGER_GET_TRACK, &dvbsubtitleTrack);
	context->manager->teletext->Command(context, MANAGER_GET_TRACK, &teletextTrack);

	if (context->manager->video)
		context->manager->video->Command(context, MANAGER_INIT_UPDATE, NULL);
	if (context->manager->audio)
		context->manager->audio->Command(context, MANAGER_INIT_UPDATE, NULL);
#if 0
	if (context->manager->subtitle)
		context->manager->subtitle->Command(context, MANAGER_INIT_UPDATE, NULL);
#endif
	if (context->manager->dvbsubtitle)
		context->manager->dvbsubtitle->Command(context, MANAGER_INIT_UPDATE, NULL);
	if (context->manager->teletext)
		context->manager->teletext->Command(context, MANAGER_INIT_UPDATE, NULL);

	ffmpeg_printf(20, "dump format\n");
	av_dump_format(avContext, 0, filename, 0);

	ffmpeg_printf(1, "number streams %d\n", avContext->nb_streams);

	unsigned int n;

	for ( n = 0; n < avContext->nb_streams; n++) {
		Track_t track;
		AVStream *stream = avContext->streams[n];
		int version = 0;

		char* encoding = Codec2Encoding(stream->codec, &version);

		if (encoding != NULL)
			ffmpeg_printf(1, "%d. encoding = %s - version %d\n", n, encoding, version);

		if (!stream->id)
			stream->id = n;

		/* some values in track are unset and therefor copyTrack segfaults.
		 * so set it by default to NULL!
		 */
		memset(&track, 0, sizeof(track));

		switch (stream->codec->codec_type) {
			case AVMEDIA_TYPE_VIDEO:
				ffmpeg_printf(10, "CODEC_TYPE_VIDEO %d\n",stream->codec->codec_type);

				if (encoding != NULL) {
					track.type           = eTypeES;
					track.version        = version;

					track.width          = stream->codec->width;
					track.height         = stream->codec->height;

					track.extraData      = stream->codec->extradata;
					track.extraSize      = stream->codec->extradata_size;

					track.frame_rate     = stream->r_frame_rate.num;

					track.aacbuf         = 0;
					track.have_aacheader = -1;

					double frame_rate = av_q2d(stream->r_frame_rate); /* rational to double */

					ffmpeg_printf(10, "frame_rate = %f\n", frame_rate);

					track.frame_rate = frame_rate * 1000.0;

					/* fixme: revise this */

					if (track.frame_rate < 23970)
						track.TimeScale = 1001;
					else
						track.TimeScale = 1000;

					ffmpeg_printf(10, "bit_rate = %d\n",stream->codec->bit_rate);
					ffmpeg_printf(10, "flags = %d\n",stream->codec->flags);
					ffmpeg_printf(10, "frame_bits = %d\n",stream->codec->frame_bits);
					ffmpeg_printf(10, "time_base.den %d\n",stream->time_base.den);
					ffmpeg_printf(10, "time_base.num %d\n",stream->time_base.num);
					ffmpeg_printf(10, "frame_rate %d\n",stream->r_frame_rate.num);
					ffmpeg_printf(10, "TimeScale %d\n",stream->r_frame_rate.den);

					ffmpeg_printf(10, "frame_rate %d\n", track.frame_rate);
					ffmpeg_printf(10, "TimeScale %d\n", track.TimeScale);

					track.Name      = "und";
					track.Encoding  = encoding;
					track.stream    = stream;
					track.Id        = ((AVStream *) (track.stream))->id;

					if(stream->duration == AV_NOPTS_VALUE) {
						ffmpeg_printf(10, "Stream has no duration so we take the duration from context\n");
						track.duration = (double) avContext->duration / 1000.0;
					}
					else {
						track.duration = (double) stream->duration * av_q2d(stream->time_base) * 1000.0;
					}

					if (context->manager->video)
						if (context->manager->video->Command(context, MANAGER_ADD, &track) < 0) {
							/* konfetti: fixme: is this a reason to return with error? */
							ffmpeg_err("failed to add track %d\n", n);
						}

				}
				else {
					ffmpeg_err("codec type video but codec unknown %d\n", stream->codec->codec_id);
				}
				break;
			case AVMEDIA_TYPE_AUDIO:
				ffmpeg_printf(10, "CODEC_TYPE_AUDIO %d\n",stream->codec->codec_type);

				if (encoding != NULL) {
					AVDictionaryEntry *lang;
					track.type           = eTypeES;

					lang = av_dict_get(stream->metadata, "language", NULL, 0);

					track.Name = lang ? lang->value : "und";

					ffmpeg_printf(10, "Language %s\n", track.Name);

					track.Encoding       = encoding;
					track.stream         = stream;
					track.Id        = ((AVStream *) (track.stream))->id;
					track.duration       = (double)stream->duration * av_q2d(stream->time_base) * 1000.0;
					track.aacbuf         = 0;
					track.have_aacheader = -1;

					if(stream->duration == AV_NOPTS_VALUE) {
						ffmpeg_printf(10, "Stream has no duration so we take the duration from context\n");
						track.duration = (double) avContext->duration / 1000.0;
					}
					else {
						track.duration = (double) stream->duration * av_q2d(stream->time_base) * 1000.0;
					}

					if(!strncmp(encoding, "A_IPCM", 6))
					{
						track.inject_as_pcm = 1;
						ffmpeg_printf(10, " Handle inject_as_pcm = %d\n", track.inject_as_pcm);

						AVCodec *codec = avcodec_find_decoder(stream->codec->codec_id);

						if(codec != NULL && !avcodec_open2(stream->codec, codec, NULL))
							printf("AVCODEC__INIT__SUCCESS\n");
						else
							printf("AVCODEC__INIT__FAILED\n");
					}

					if (context->manager->audio)
					{
						if (context->manager->audio->Command(context, MANAGER_ADD, &track) < 0) {
							/* konfetti: fixme: is this a reason to return with error? */
							ffmpeg_err("failed to add track %d\n", n);
						}
					}

				}
				else {
					ffmpeg_err("codec type audio but codec unknown %d\n", stream->codec->codec_id);
				}
				break;
			case AVMEDIA_TYPE_SUBTITLE:
				{
					AVDictionaryEntry *lang;

					ffmpeg_printf(10, "CODEC_TYPE_SUBTITLE %d\n",stream->codec->codec_type);

					lang = av_dict_get(stream->metadata, "language", NULL, 0);

					track.Name = lang ? lang->value : "und";

					ffmpeg_printf(10, "Language %s\n", track.Name);

					track.Encoding       = encoding;
					track.stream         = stream;
					track.Id		 = ((AVStream *) (track.stream))->id;
					track.duration       = (double)stream->duration * av_q2d(stream->time_base) * 1000.0;

					track.aacbuf         = 0;
					track.have_aacheader = -1;

					track.width          = -1; /* will be filled online from videotrack */
					track.height         = -1; /* will be filled online from videotrack */

					track.extraData      = stream->codec->extradata;
					track.extraSize      = stream->codec->extradata_size;

					ffmpeg_printf(1, "subtitle codec %d\n", stream->codec->codec_id);
					ffmpeg_printf(1, "subtitle width %d\n", stream->codec->width);
					ffmpeg_printf(1, "subtitle height %d\n", stream->codec->height);
					ffmpeg_printf(1, "subtitle stream %p\n", stream);

					if(stream->duration == AV_NOPTS_VALUE) {
						ffmpeg_printf(10, "Stream has no duration so we take the duration from context\n");
						track.duration = (double) avContext->duration / 1000.0;
					}
					else {
						track.duration = (double) stream->duration * av_q2d(stream->time_base) * 1000.0;
					}

					ffmpeg_printf(10, "FOUND SUBTITLE %s\n", track.Name);

					if (stream->codec->codec_id == AV_CODEC_ID_DVB_TELETEXT && context->manager->teletext) {
						ffmpeg_printf(10, "dvb_teletext\n");
						int i = 0;
						AVDictionaryEntry *t = NULL;
						do {
							char tmp[30];
							snprintf(tmp, sizeof(tmp), "teletext_%d", i);
							t = av_dict_get(stream->metadata, tmp, NULL, 0);
							if (t) {
								track.Name = t->value;
								if (context->manager->teletext->Command(context, MANAGER_ADD, &track) < 0)
									ffmpeg_err("failed to add teletext track %d\n", n);
							}
							i++;
						} while (t);
					} else if (stream->codec->codec_id == AV_CODEC_ID_DVB_SUBTITLE && context->manager->dvbsubtitle) {
						ffmpeg_printf(10, "dvb_subtitle\n");
						lang = av_dict_get(stream->metadata, "language", NULL, 0);
						if (context->manager->dvbsubtitle->Command(context, MANAGER_ADD, &track) < 0) {
							ffmpeg_err("failed to add dvbsubtitle track %d\n", n);
						}
					} else if (initial && context->manager->subtitle) {
						if (!stream->codec->codec) {
							stream->codec->codec = avcodec_find_decoder(stream->codec->codec_id);
							if (!stream->codec->codec)
								ffmpeg_err("avcodec_find_decoder failed for subtitle track %d\n", n);
							else if (avcodec_open2(stream->codec, stream->codec->codec, NULL)) {
								ffmpeg_err("avcodec_open2 failed for subtitle track %d\n", n);
								stream->codec->codec = NULL;
							}
						}
						if (context->manager->subtitle->Command(context, MANAGER_ADD, &track) < 0) {
							/* konfetti: fixme: is this a reason to return with error? */
							ffmpeg_err("failed to add subtitle track %d\n", n);
						}
					}

					break;
				}
			case AVMEDIA_TYPE_UNKNOWN:
			case AVMEDIA_TYPE_DATA:
			case AVMEDIA_TYPE_ATTACHMENT:
			case AVMEDIA_TYPE_NB:
			default:
				ffmpeg_err("not handled or unknown codec_type %d\n", stream->codec->codec_type);
				break;
		}

	} /* for */

	return cERR_CONTAINER_FFMPEG_NO_ERROR;
}

static int container_ffmpeg_play(Context_t *context)
{
	int error;
	int ret = 0;
	pthread_attr_t attr;

	ffmpeg_printf(10, "\n");

	if ( context && context->playback && context->playback->isPlaying ) {
		ffmpeg_printf(10, "is Playing\n");
	}
	else {
		ffmpeg_printf(10, "is NOT Playing\n");
	}

	if (hasPlayThreadStarted == 0) {
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

		if((error = pthread_create(&PlayThread, &attr, (void *)&FFMPEGThread, context)) != 0) {
			ffmpeg_printf(10, "Error creating thread, error:%d:%s\n", error,strerror(error));

			hasPlayThreadStarted = 0;
			ret = cERR_CONTAINER_FFMPEG_ERR;
		}
		else {
			ffmpeg_printf(10, "Created thread\n");

			hasPlayThreadStarted = 1;
		}
	}
	else {
		ffmpeg_printf(10, "A thread already exists!\n");

		ret = cERR_CONTAINER_FFMPEG_ERR;
	}

	ffmpeg_printf(10, "exiting with value %d\n", ret);

	return ret;
}

static int container_ffmpeg_stop(Context_t *context) {
	int ret = cERR_CONTAINER_FFMPEG_NO_ERROR;
	int wait_time = 20;

	ffmpeg_printf(10, "\n");

	if (!isContainerRunning)
	{
		ffmpeg_err("Container not running\n");
		return cERR_CONTAINER_FFMPEG_ERR;
	}

	while ( (hasPlayThreadStarted != 0) && (--wait_time) > 0 ) {
		ffmpeg_printf(10, "Waiting for ffmpeg thread to terminate itself, will try another %d times\n", wait_time);

		usleep(100000);
	}

	if (wait_time == 0) {
		ffmpeg_err( "Timeout waiting for thread!\n");
		ret = cERR_CONTAINER_FFMPEG_ERR;
	}

	hasPlayThreadStarted = 0;

	getMutex(FILENAME, __FUNCTION__,__LINE__);

	if (avContext != NULL) {
		avformat_close_input(avContext);
		avContext = NULL;
	}

	avformat_network_deinit();
	isContainerRunning = 0;

	releaseMutex(FILENAME, __FUNCTION__,__LINE__);

	ffmpeg_printf(10, "ret %d\n", ret);
	return ret;
}

static int container_ffmpeg_seek_bytes(off_t pos) {
	int flag = AVSEEK_FLAG_BYTE;
	off_t current_pos = avio_tell(avContext->pb);

	ffmpeg_printf(20, "seeking to position %lld (bytes)\n", pos);

	if (current_pos > pos)
		flag |= AVSEEK_FLAG_BACKWARD;

	if (avformat_seek_file(avContext, -1, INT64_MIN, pos, INT64_MAX, flag) < 0)
	{
		ffmpeg_err( "Error seeking\n");
		return cERR_CONTAINER_FFMPEG_ERR;
	}

	ffmpeg_printf(30, "current_pos after seek %lld\n", avio_tell(avContext->pb));

	return cERR_CONTAINER_FFMPEG_NO_ERROR;
}


/* seeking relative to a given byteposition N seconds ->for reverse playback needed */
static int container_ffmpeg_seek_rel(Context_t *context, off_t pos, long long int pts, float sec) {
	Track_t * videoTrack = NULL;
	Track_t * audioTrack = NULL;
	Track_t * current = NULL;
	int flag = 0;

	ffmpeg_printf(10, "seeking %f sec relativ to %lld\n", sec, pos);

	context->manager->video->Command(context, MANAGER_GET_TRACK, &videoTrack);
	context->manager->audio->Command(context, MANAGER_GET_TRACK, &audioTrack);

	if (videoTrack != NULL)	     current = videoTrack;
	else if (audioTrack != NULL) current = audioTrack;
	else 
	{
		ffmpeg_err( "no track avaibale to seek\n");
		return cERR_CONTAINER_FFMPEG_ERR;
	}

	if (pos == -1)
	{
		pos = avio_tell(avContext->pb);
	}

	if (pts == -1)
		pts = current->pts;

	if (sec < 0)
		flag |= AVSEEK_FLAG_BACKWARD;

	getMutex(FILENAME, __FUNCTION__,__LINE__);

	ffmpeg_printf(10, "iformat->flags %d\n", avContext->iformat->flags);
	if (avContext->iformat->flags & AVFMT_TS_DISCONT)
	{
		if (avContext->bit_rate)
		{
			sec *= avContext->bit_rate / 8.0;
			ffmpeg_printf(10, "bit_rate %d\n", avContext->bit_rate);
		}
		else
		{
			sec *= 180000.0;
		}
		pos += sec;

		if (pos < 0)
		{
			ffmpeg_err("end of file reached\n");
			releaseMutex(FILENAME, __FUNCTION__,__LINE__);
			return cERR_CONTAINER_FFMPEG_END_OF_FILE;
		}

		ffmpeg_printf(10, "1. seeking to position %lld bytes ->sec %f\n", pos, sec);

		if (container_ffmpeg_seek_bytes(pos) < 0)
		{
			ffmpeg_err( "Error seeking\n");
			releaseMutex(FILENAME, __FUNCTION__,__LINE__);
			return cERR_CONTAINER_FFMPEG_ERR;
		}

		releaseMutex(FILENAME, __FUNCTION__,__LINE__);
		return pos;
	}
	else
	{
		sec += ((float) pts / 90000.0f);

		if (sec < 0) sec = 0;

		ffmpeg_printf(10, "2. seeking to position %f sec ->time base %f %d\n", sec, av_q2d(((AVStream*) current->stream)->time_base), AV_TIME_BASE);

		int64_t seek_target = av_rescale_q(sec * AV_TIME_BASE, AV_TIME_BASE_Q, ((AVStream*) current->stream)->time_base);

		if (av_seek_frame(avContext, current->Id, seek_target, flag) < 0) {
			ffmpeg_err( "Error seeking\n");
			releaseMutex(FILENAME, __FUNCTION__,__LINE__);
			return cERR_CONTAINER_FFMPEG_ERR;
		}

		if (sec <= 0)
		{
			ffmpeg_err("end of file reached\n");
			releaseMutex(FILENAME, __FUNCTION__,__LINE__);
			return cERR_CONTAINER_FFMPEG_END_OF_FILE;
		}
	}

	releaseMutex(FILENAME, __FUNCTION__,__LINE__);
	return cERR_CONTAINER_FFMPEG_NO_ERROR;
}

static int container_ffmpeg_seek(Context_t *context, float sec) {
	Track_t * videoTrack = NULL;
	Track_t * audioTrack = NULL;
	Track_t * current = NULL;
	int flag = 0;

#if !defined(VDR1722)
	ffmpeg_printf(10, "seeking %f sec\n", sec);

	if (sec == 0.0)
	{
		ffmpeg_err("sec = 0.0 ignoring\n");
		return cERR_CONTAINER_FFMPEG_ERR;
	}
#else
	ffmpeg_printf(10, "goto %f sec\n", sec);

	if (sec < 0.0)
	{
		ffmpeg_err("sec < 0.0 ignoring\n");
		return cERR_CONTAINER_FFMPEG_ERR;
	}
#endif
	context->manager->video->Command(context, MANAGER_GET_TRACK, &videoTrack);
	context->manager->audio->Command(context, MANAGER_GET_TRACK, &audioTrack);

	if (videoTrack != NULL)
		current = videoTrack;
	else if (audioTrack != NULL)
		current = audioTrack;

	if (current == NULL) {
		ffmpeg_err( "no track available to seek\n");
		return cERR_CONTAINER_FFMPEG_ERR;
	}

	if (sec < 0)
		flag |= AVSEEK_FLAG_BACKWARD;

	getMutex(FILENAME, __FUNCTION__,__LINE__);

	ffmpeg_printf(10, "iformat->flags %d\n", avContext->iformat->flags);

	if (avContext->iformat->flags & AVFMT_TS_DISCONT)
	{
		/* konfetti: for ts streams seeking frame per seconds does not work (why?).
		 * I take this algo partly from ffplay.c.
		 *
		 * seeking per HTTP does still not work very good. forward seeks everytime
		 * about 10 seconds, backward does not work.
		 */

#if LIBAVCODEC_VERSION_MAJOR < 54
		off_t pos = url_ftell(avContext->pb);
#else
		off_t pos = avio_tell(avContext->pb);
#endif

		ffmpeg_printf(10, "pos %lld %d\n", pos, avContext->bit_rate);

		if (avContext->bit_rate)
		{
			sec *= avContext->bit_rate / 8.0;
			ffmpeg_printf(10, "bit_rate %d\n", avContext->bit_rate);
		}
		else
		{
			sec *= 180000.0;
		}
#if !defined(VDR1722)
		pos += sec;
#else
		pos = sec;
#endif
		if (pos < 0)
		{
			ffmpeg_err("end of file reached\n");
			return cERR_CONTAINER_FFMPEG_END_OF_FILE;
		}

		ffmpeg_printf(10, "1. seeking to position %lld bytes ->sec %f\n", pos, sec);

		if (container_ffmpeg_seek_bytes(pos) < 0)
		{
			ffmpeg_err( "Error seeking\n");
			releaseMutex(FILENAME, __FUNCTION__,__LINE__);
			return cERR_CONTAINER_FFMPEG_ERR;
		}

	} else
	{
#if !defined(VDR1722)
		sec += ((float) current->pts / 90000.0f);
#endif
		ffmpeg_printf(10, "2. seeking to position %f sec ->time base %f %d\n", sec, av_q2d(((AVStream*) current->stream)->time_base), AV_TIME_BASE);
		int64_t seek_target = av_rescale_q(sec * AV_TIME_BASE, AV_TIME_BASE_Q, ((AVStream*) current->stream)->time_base);

		if (av_seek_frame(avContext, current->Id, seek_target, flag) < 0) {
			ffmpeg_err( "Error seeking\n");
			releaseMutex(FILENAME, __FUNCTION__,__LINE__);
			return cERR_CONTAINER_FFMPEG_ERR;
		}
	}

	releaseMutex(FILENAME, __FUNCTION__,__LINE__);
	return cERR_CONTAINER_FFMPEG_NO_ERROR;
}

static int container_ffmpeg_get_length(Context_t *context, double * length) {
	ffmpeg_printf(50, "\n");
	Track_t * videoTrack = NULL;
	Track_t * audioTrack = NULL;
	Track_t * subtitleTrack = NULL;
	Track_t * current = NULL;

	if (length == NULL) {
		ffmpeg_err( "null pointer passed\n");
		return cERR_CONTAINER_FFMPEG_ERR;
	}

	context->manager->video->Command(context, MANAGER_GET_TRACK, &videoTrack);
	context->manager->audio->Command(context, MANAGER_GET_TRACK, &audioTrack);
	context->manager->subtitle->Command(context, MANAGER_GET_TRACK, &subtitleTrack);

	if (videoTrack != NULL)
		current = videoTrack;
	else if (audioTrack != NULL)
		current = audioTrack;
	else if (subtitleTrack != NULL)
		current = subtitleTrack;

	*length = 0.0;

	if (current != NULL) {
		if (current->duration == 0)
			return cERR_CONTAINER_FFMPEG_ERR;
		else
			*length = (current->duration / 1000.0);
	}
	else {
		if (avContext != NULL)
		{
			*length = (avContext->duration / 1000.0);
		} else
		{
			ffmpeg_err( "no Track not context ->no problem :D\n");
			return cERR_CONTAINER_FFMPEG_ERR;
		}
	}

	return cERR_CONTAINER_FFMPEG_NO_ERROR;
}

static int container_ffmpeg_swich_audio(Context_t* context, int* arg)
{
	ffmpeg_printf(10, "track %d\n", *arg);
	/* Hellmaster1024: nothing to do here!*/
	float sec=-5.0;
	context->playback->Command(context, PLAYBACK_SEEK, (void*)&sec);
	return cERR_CONTAINER_FFMPEG_NO_ERROR;
}

static int container_ffmpeg_swich_subtitle(Context_t* context, int* arg)
{
	/* Hellmaster1024: nothing to do here!*/
	return cERR_CONTAINER_FFMPEG_NO_ERROR;
}

/* konfetti comment: I dont like the mechanism of overwriting
 * the pointer in infostring. This lead in most cases to
 * user errors, like it is in the current version (libeplayer2 <-->e2->servicemp3.cpp)
 * From e2 there is passed a tag=strdup here and we overwrite this
 * strdupped tag. This lead to dangling pointers which are never freed!
 * I do not free the string here because this is the wrong way. The mechanism
 * should be changed, or e2 should pass it in a different way...
 */
static int container_ffmpeg_get_info(Context_t* context, char ** infoString)
{
	Track_t * videoTrack = NULL;
	Track_t * audioTrack = NULL;
	char*     meta = NULL;

	ffmpeg_printf(20, ">\n");

	if (avContext != NULL)
	{
		if ((infoString == NULL) || (*infoString == NULL))
		{
			ffmpeg_err("infostring NULL\n");
			return cERR_CONTAINER_FFMPEG_ERR;
		}

		ffmpeg_printf(20, "%s\n", *infoString);

		context->manager->video->Command(context, MANAGER_GET_TRACK, &videoTrack);
		context->manager->audio->Command(context, MANAGER_GET_TRACK, &audioTrack);

		if ((meta = searchMeta(avContext->metadata, *infoString)) == NULL)
		{
			if (audioTrack != NULL)
			{
				AVStream* stream = audioTrack->stream;

				meta = searchMeta(stream->metadata, *infoString);
			}

			if ((meta == NULL) && (videoTrack != NULL))
			{
				AVStream* stream = videoTrack->stream;

				meta = searchMeta(stream->metadata, *infoString);
			}
		}

		if (meta != NULL)
		{
			*infoString = strdup(meta);
		}
		else
		{
			ffmpeg_printf(1, "no metadata found for \"%s\"\n", *infoString);
			*infoString = strdup("not found");
		}
	} else
	{
		ffmpeg_err("avContext NULL\n");
		return cERR_CONTAINER_FFMPEG_ERR;
	}

	return cERR_CONTAINER_FFMPEG_NO_ERROR;

}

static int Command(void  *_context, ContainerCmd_t command, void * argument)
{
	Context_t  *context = (Context_t*) _context;
	int ret = cERR_CONTAINER_FFMPEG_NO_ERROR;

	ffmpeg_printf(50, "Command %d\n", command);

	switch(command)
	{
		case CONTAINER_INIT:  {
					      char * FILENAME = (char *)argument;
					      ret = container_ffmpeg_init(context, FILENAME);
					      break;
				      }
		case CONTAINER_PLAY:  {
					      ret = container_ffmpeg_play(context);
					      break;
				      }
		case CONTAINER_STOP:  {
					      ret = container_ffmpeg_stop(context);
					      break;
				      }
		case CONTAINER_SEEK: {
					     ret = container_ffmpeg_seek(context, (float)*((float*)argument));
					     break;
				     }
		case CONTAINER_LENGTH: {
					       double length = 0;
					       ret = container_ffmpeg_get_length(context, &length);

					       *((double*)argument) = (double)length;
					       break;
				       }
		case CONTAINER_SWITCH_AUDIO: {
						     ret = container_ffmpeg_swich_audio(context, (int*) argument);
						     break;
					     }
		case CONTAINER_SWITCH_SUBTITLE: {
							ret = container_ffmpeg_swich_subtitle(context, (int*) argument);
							break;
						}
		case CONTAINER_INFO: {
					     ret = container_ffmpeg_get_info(context, (char **)argument);
					     break;
				     }
		case CONTAINER_STATUS: {
					       *((int*)argument) = hasPlayThreadStarted;
					       break;
				       }
		case CONTAINER_LAST_PTS: {
						 *((long long int*)argument) = latestPts;
						 break;
					 }
		default:
					 ffmpeg_err("ContainerCmd %d not supported!\n", command);
					 ret = cERR_CONTAINER_FFMPEG_ERR;
					 break;
	}

	ffmpeg_printf(50, "exiting with value %d\n", ret);

	return ret;
}


static char *FFMPEG_Capabilities[] = {"avi", "mkv", "mp4", "ts", "mov", "flv", "flac", "mp3", "mpg", "m2ts", "vob", "wmv","wma", "asf", "mp2", "m4v", "m4a", "divx", "dat", "mpeg", "trp", "mts", "vdr", "ogg", "wav", "wtv", "asx", "mvi", "png", "jpg", "m3u8", NULL };

Container_t FFMPEGContainer = {
	"FFMPEG",
	&Command,
	FFMPEG_Capabilities
};
