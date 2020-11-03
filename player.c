/*
 * player.c
 *
 *  Created on: Oct 6, 2020
 *      Author: ning
 */



/*
 * header
 */
//system header
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <stdio.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <rtscamkit.h>
#include <rtsavapi.h>
#include <rtsvideo.h>
#include <mp4v2/mp4v2.h>
#include <malloc.h>
#include <miss.h>
//program header
#include "../../manager/manager_interface.h"
#include "../../server/realtek/realtek_interface.h"
#include "../../tools/tools_interface.h"
#include "../../server/player/player_interface.h"
#include "../../server/miio/miio_interface.h"
#include "../../server/video/video_interface.h"
#include "../../server/audio/audio_interface.h"
#include "../../server/recorder/recorder_interface.h"
#include "../../server/miss/miss_interface.h"
//server header
#include "player.h"
#include "config.h"
#include "player_interface.h"

/*
 * static
 */
//variable
static 	message_buffer_t		message;
static 	server_info_t 			info;
static  player_config_t			config;
static	player_job_t			job;
static	player_file_list_t		flist;

//function
//common
static void *server_func(void);
static int server_message_proc(void);
static int server_release(void);
static void task_default(void);
static void task_error(void);
//specific
static int player_main(void);
static int player_interrupt(void);
/*
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 */

/*
 * helper
 */
static int send_message(int receiver, message_t *msg)
{
	int st = 0;
	switch(receiver) {
		case SERVER_DEVICE:
			st = server_device_message(msg);
			break;
		case SERVER_KERNEL:
	//		st = server_kernel_message(msg);
			break;
		case SERVER_REALTEK:
			st = server_realtek_message(msg);
			break;
		case SERVER_MIIO:
			st = server_miio_message(msg);
			break;
		case SERVER_MISS:
			st = server_miss_message(msg);
			break;
		case SERVER_MICLOUD:
	//		st = server_micloud_message(msg);
			break;
		case SERVER_VIDEO:
			st = server_video_message(msg);
			break;
		case SERVER_AUDIO:
			st = server_audio_message(msg);
			break;
		case SERVER_RECORDER:
			st = server_recorder_message(msg);
			break;
		case SERVER_PLAYER:
			st = server_player_message(msg);
			break;
		case SERVER_SPEAKER:
			st = server_speaker_message(msg);
			break;
		case SERVER_VIDEO2:
			st = server_video2_message(msg);
			break;
		case SERVER_SCANNER:
//			st = server_scanner_message(msg);
			break;
		case SERVER_MANAGER:
			st = manager_message(msg);
			break;
		default:
			log_qcy(DEBUG_SERIOUS, "unknown message target! %d", receiver);
			break;
	}
	return st;
}

int player_read_file_list(char *path)
{
	struct dirent **namelist;
	int n;
	char name[MAX_SYSTEM_STRING_SIZE*8];
	char *p = NULL;
	n = scandir(path, &namelist, 0, alphasort);
	if(n < 0) {
	    log_qcy(DEBUG_SERIOUS, "Open dir error");
	    return -1;
	}
	int index=0;
	while(index < n) {
        if(namelist[index]->d_type == 10) goto exit;
        else if(namelist[index]->d_type == 4) goto exit;
		if( strstr(namelist[index]->d_name,".mp4") == NULL ) {
			//remove file here.
			remove( namelist[index]->d_name );
			goto exit;
		}
//		else if(strcmp(namelist[index]->d_name,".")==0 || strcmp(namelist[index]->d_name,"..")==0) goto exit;
        else if(namelist[index]->d_type == 8) {
        	p = strstr( namelist[index]->d_name, "202");	//first
        	if( p == NULL) goto exit;
        	memset(name, 0, sizeof(name));
        	memcpy(name, p, 14);
        	long long int st,ed;
        	st = time_date_to_stamp( name );
        	p+=15;
        	memset(name, 0, sizeof(name));
        	memcpy(name, p, 14);
        	ed = time_date_to_stamp( name );
        	if( st >= ed ) goto exit;
        	flist.start[flist.num] = st;
        	flist.stop[flist.num] = ed;
        	flist.num ++;
        }
exit:
		free(namelist[index]);
	    index++;
	}
	free(namelist);
    return 0;
}

static int player_node_remove_last(player_list_node_t *head)
{
    int ret = 0;
    player_list_node_t *current = head;
    if (head->next == NULL) {
        ret = head->data;
        free(head);
        return 0;
    }
    while (current->next->next != NULL) {
        current = current->next;
    }
    free(current->next);
    current->next = NULL;
    return 0;
}

static int player_node_clear(void)
{
	int ret = 0;
	if( job.fhead == NULL) return ret;
    while( job.fhead->next != NULL)
        player_node_remove_last( job.fhead);
    player_node_remove_last( job.fhead );
    job.fhead = NULL;
    return ret;
}

static int player_node_insert(int val)
{
	player_list_node_t *current;
    player_list_node_t *node = malloc(sizeof(player_list_node_t));
    node->data = val;
    node->next = NULL;
    if( job.fhead == NULL ) job.fhead = node;
    else {
        for (current = job.fhead; current->next != NULL; current = current->next);
    	current->next = node;
    }
    return 0;
}

static int player_file_bisearch_start(long long key)
{
    int low = 0, mid, high = flist.num - 1;
    if( flist.start[0] > key) return -1;
    while (low <= high) {
        mid = (low + high) / 2;
        if (key < flist.start[mid]) {
            high = mid - 1;
        }
        else if (key > flist.start[mid]) {
            low = mid + 1;
        }
        else {
        	return mid;
        }
    }
    if( key > flist.start[low] )
    	return low;
    else
    	return low - 1;
}

static int player_file_bisearch_stop(long long key)
{
    int low = 0, mid, high = flist.num - 1;
    if( flist.stop[ flist.num - 1 ] < key) return flist.num -1 ;
    while (low <= high) {
        mid = (low + high) / 2;
        if (key < flist.stop[mid]) {
            high = mid - 1;
        }
        else if (key > flist.stop[mid]) {
            low = mid + 1;
        }
        else {
        	return mid;
        }
    }
    if( key < flist.stop[low] )
    	return low;
    else
    	return low + 1;
}

static int player_search_file_list(long long start, long long end)
{
	int ret = 0, start_index, stop_index;
	int i;
	player_node_clear();
	if( flist.num <= 0) return -1;
	start_index = player_file_bisearch_start(start);
	stop_index = player_file_bisearch_stop(end);
	if( start_index<0 || stop_index<0 ) return -1;
	if( (start_index > (flist.num-1)) || (stop_index > (flist.num-1)) ) return -1;
	if( start_index > stop_index ) return -1;
	for(i=start_index;i<=stop_index;i++) {
		player_node_insert(i);
	}
	return ret;
}

static int player_init( message_t* msg )
{
	message_t send_msg;
	int ret = 0;
	if( config.profile.enable == 0 ) goto error;
	player_interrupt();
	job.init.auto_exit = ((player_iot_config_t*)msg->arg)->want_to_stop;
	job.init.speed = ((player_iot_config_t*)msg->arg)->speed;
	job.init.start = ((player_iot_config_t*)msg->arg)->start;
	job.init.stop= ((player_iot_config_t*)msg->arg)->end;
	job.init.channel_merge = ((player_iot_config_t*)msg->arg)->channel_merge;
	job.init.offset = ((player_iot_config_t*)msg->arg)->offset;
	job.init.switch_to_live = ((player_iot_config_t*)msg->arg)->switch_to_live;
	if( job.init.stop <= job.init.start ) goto error;
	if( job.init.start == 0 ) goto error;
	if( player_search_file_list(job.init.start, job.init.stop) != 0) goto error;
	log_qcy(DEBUG_SERIOUS, "------------add new player setting----------------");
	log_qcy(DEBUG_SERIOUS, "now=%ld", time_get_now_stamp());
	log_qcy(DEBUG_SERIOUS, "start=%ld", job.init.start);
	log_qcy(DEBUG_SERIOUS, "end=%ld", job.init.stop);
	log_qcy(DEBUG_SERIOUS, "--------------------------------------------------");
	ret = 0;
	return ret;
error:
	ret = -1;
	return ret;
}

static int player_get_video_frame(void)
{
	char NAL[5] = {0x00,0x00,0x00,0x01};
    unsigned char *data = NULL;
    unsigned char *p = NULL;
    unsigned int size = 0, num = 0, framesize;
    MP4Timestamp start_time;
    MP4Duration duration;
    MP4Duration offset;
    char iframe = 0;
	int ret=0;
	int	flag=0;
	message_t msg;
	av_data_info_t	info;
	int sample_time;
    if( !job.run.mp4_file ) return -1;
    if( job.run.video_index >= job.run.video_frame_num) return -1;
    sample_time = MP4GetSampleTime( job.run.mp4_file, job.run.video_track, job.run.video_index );
	sample_time = sample_time * 1000 / job.run.video_timescale;
	iframe = 0;
    ret = MP4ReadSample( job.run.mp4_file, job.run.video_track, job.run.video_index,
    		&data,&size,&start_time,&duration,&offset,&iframe);
    if( !ret ) {
    	if(data) free(data);
    	job.run.video_index++;
    	return -1;
    }
    if( !job.run.i_frame_read && !iframe ) {
    	if(data) free(data);
    	job.run.video_index++;
    	return 0;
    }
	if( size > 0){
		start_time = start_time * 1000 / job.run.video_timescale;
		offset = offset * 1000 / job.run.video_timescale;
		if( job.run.stop ) {
			if( ( start_time / 1000 + job.run.start) > job.run.stop ) {
				return -1;
			}
		}
//		log_qcy(DEBUG_SERIOUS, "@@@@@reading video frame length=%d, duration = %d, start =%d@@@@@", size, sample_time, start_time);
		if( !job.run.i_frame_read && iframe ) {
		    /********message body********/
			unsigned char *mdata = NULL;
			mdata = calloc( (size + job.run.slen + job.run.plen + 8), 1);
			if( mdata == NULL) {
				log_qcy(DEBUG_SERIOUS, "calloc error, size = %d", (size + job.run.slen + job.run.plen + 8));
				return -1;
			}
			p = mdata;
			memcpy(p, NAL, 4); p+=4;
			memcpy(p, job.run.sps, job.run.slen); p+=job.run.slen;
			memcpy(p, NAL, 4); p+=4;
			memcpy(p, job.run.pps, job.run.plen); p+=job.run.plen;
			memcpy(p, data, size);
			memcpy(p, NAL, 4);
			log_qcy(DEBUG_SERIOUS, "first key frame.");
			/***************************/
			msg_init(&msg);
			msg.message = MSG_MISS_VIDEO_DATA;
			msg.extra = mdata;
			msg.extra_size = (size + job.run.slen + job.run.plen + 8);
			info.frame_index = job.run.video_index;
			info.timestamp = start_time;
		   	info.flag |= FLAG_STREAM_TYPE_PLAYBACK << 11;
	    	info.flag |= FLAG_FRAME_TYPE_IFRAME << 0;
		    info.flag |= FLAG_RESOLUTION_VIDEO_480P << 17;
			msg.arg = &info;
			msg.arg_size = sizeof(av_data_info_t);
			ret = server_miss_video_message(&msg);
			/****************************/
			job.run.i_frame_read = 1;
			free(mdata);
		}
		else {
			if( ((data[4] & 0x1f) == 0x07) || ((data[4] & 0x1f) == 0x08) ) { //not sending any psp pps
				free(data);
				job.run.video_index++;
				return 0;
			}
			data[0] = 0x00;
			data[1] = 0x00;
			data[2] = 0x00;
			data[3] = 0x01;
			/********message body********/
			msg_init(&msg);
			msg.message = MSG_MISS_VIDEO_DATA;
			msg.extra = data;
			msg.extra_size = size;
			info.frame_index = job.run.video_index;
			info.timestamp = start_time;
		   	info.flag |= FLAG_STREAM_TYPE_PLAYBACK << 11;
		    if( iframe )// I frame
		    	info.flag |= FLAG_FRAME_TYPE_IFRAME << 0;
		    else
		    	info.flag |= FLAG_FRAME_TYPE_PBFRAME << 0;
		    info.flag |= FLAG_RESOLUTION_VIDEO_480P << 17;
			msg.arg = &info;
			msg.arg_size = sizeof(av_data_info_t);
			ret = server_miss_video_message(&msg);
			/****************************/
			job.run.video_sync = start_time;
		}
	}
	job.run.video_index++;
	free(data);
	usleep(10000);
    return 0;
}

static int player_get_audio_frame( void )
{
    unsigned char *data = NULL;
    unsigned char *p = NULL;
    unsigned int size = 0, num = 0, framesize;
    MP4Timestamp start_time;
    MP4Duration duration;
    MP4Duration offset;
    char iframe = 0;
	int ret=0;
	int	flag=0;
	message_t msg;
	av_data_info_t	info;
	int sample_time;
    if( !job.run.mp4_file ) return -1;
    if( job.run.audio_index >= job.run.audio_frame_num) return -1;
    sample_time = MP4GetSampleTime( job.run.mp4_file, job.run.audio_track, job.run.audio_index );
	sample_time = sample_time * 1000 / job.run.audio_timescale;
	iframe = 0;
    ret = MP4ReadSample( job.run.mp4_file, job.run.audio_track, job.run.audio_index,
    		&data,&size,&start_time,&duration,&offset,&iframe);
    if( !ret ) {
    	if(data) free(data);
    	job.run.audio_index++;
    	return -1;
    }
    if( !job.run.i_frame_read ) {
    	if(data) free(data);
    	job.run.audio_index++;
    	return 0;
    }
	if( size > 0){
		start_time = start_time * 1000 / job.run.audio_timescale;
		offset = offset * 1000 / job.run.audio_timescale;
		if( job.run.stop ) {
			if( ( start_time / 1000 + job.run.start) > job.run.stop ) {
				return -1;
			}
		}
//		log_qcy(DEBUG_SERIOUS, "@@@@@reading audio frame length=%d, duration = %d, start =%d@@@@@", size, sample_time, start_time);
		/********message body********/
		msg_init(&msg);
		msg.message = MSG_MISS_AUDIO_DATA;
		msg.sender = msg.receiver = SERVER_PLAYER;
		msg.extra = data;
		msg.extra_size = size;
		info.frame_index = job.run.audio_index;
		info.timestamp = start_time;
		info.flag = FLAG_AUDIO_SAMPLE_8K << 3 | FLAG_AUDIO_DATABITS_8 << 7 | FLAG_AUDIO_CHANNEL_MONO << 9 |  FLAG_RESOLUTION_AUDIO_DEFAULT << 17;
		msg.arg = &info;
		msg.arg_size = sizeof(av_data_info_t);
		ret = server_miss_audio_message(&msg);
		/****************************/
		job.run.audio_sync = start_time;
	}
	job.run.audio_index++;
	free(data);
	usleep(10000);
    return 0;
}

static int player_open_mp4(void)
{
    int i, num_tracks;
    int diff, sample_time;
	unsigned char	**sps_header = NULL;
	unsigned char  	**pps_header = NULL;
	unsigned int	*sps_size = NULL;
	unsigned int	*pps_size = NULL;
	char start_str[MAX_SYSTEM_STRING_SIZE], stop_str[MAX_SYSTEM_STRING_SIZE];
	if( job.init.current == NULL)
		job.init.current = job.fhead;
	else
		job.init.current = job.init.current->next;
	if( job.run.mp4_file == NULL ) {
		memset( job.run.file_path, 0 ,sizeof(job.run.file_path));
		memset( start_str, 0, sizeof(start_str));
		memset( stop_str, 0, sizeof(stop_str));
		time_stamp_to_date( flist.start[job.init.current->data], start_str);
		time_stamp_to_date( flist.stop[job.init.current->data], stop_str);
		sprintf( job.run.file_path , "%s%s-%s_%s.mp4",config.profile.path, config.profile.prefix, start_str, stop_str);
	}
	else
		return 0;
    job.run.mp4_file = MP4Read( job.run.file_path );
    if ( !job.run.mp4_file ) {
        log_qcy(DEBUG_SERIOUS, "Read error....%s", job.run.file_path);
        return -1;
    }
    log_qcy(DEBUG_SERIOUS, "$%$%$%opened file %s$%$%$%", job.run.file_path);
    if( job.init.current == job.fhead ) {
    	job.run.start = job.init.start;
    }
    else {
    	job.run.start = flist.start[job.init.current->data];
    }
    if( job.init.current->next == NULL ) job.run.stop = job.init.stop;
    	else job.run.stop = 0;
    job.run.video_track = MP4_INVALID_TRACK_ID;
    num_tracks = MP4GetNumberOfTracks( job.run.mp4_file, NULL, 0);
    for (i = 0; i < num_tracks; i++) {
    	MP4TrackId id = MP4FindTrackId(job.run.mp4_file, i, NULL,0);
        char* type = MP4GetTrackType( job.run.mp4_file, id );
        if( MP4_IS_VIDEO_TRACK_TYPE( type ) ) {
        	job.run.video_track	= id;
            job.run.duration = MP4GetTrackDuration( job.run.mp4_file, id );
            job.run.video_frame_num = MP4GetTrackNumberOfSamples( job.run.mp4_file, id );
            job.run.video_timescale = MP4GetTrackTimeScale( job.run.mp4_file, id);
            job.run.fps = MP4GetTrackVideoFrameRate(job.run.mp4_file, id);
            job.run.width = MP4GetTrackVideoWidth(job.run.mp4_file, id);
            job.run.height = MP4GetTrackVideoHeight(job.run.mp4_file, id);
            log_qcy(DEBUG_SERIOUS, "video codec = %s", MP4GetTrackMediaDataName(job.run.mp4_file, id));
            job.run.video_index = 1;
            //sps pps
			char result = MP4GetTrackH264SeqPictHeaders(job.run.mp4_file, id, &sps_header, &sps_size,
					&pps_header, &pps_size);
			memset( job.run.sps, 0, sizeof(job.run.sps));
			job.run.slen = 0;
            for (i = 0; sps_size[i] != 0; i++) {
            	if( strlen(job.run.sps)<=0 && sps_size[i]>0 ) {
            		memcpy( job.run.sps, sps_header[i], sps_size[i]);
                	job.run.slen = sps_size[i];
            	}
                free(sps_header[i]);
            }
            free(sps_header);
            free(sps_size);
            memset( job.run.pps, 0, sizeof(job.run.pps));
            job.run.plen = 0;
            for (i = 0; pps_size[i] != 0; i++) {
            	if( strlen(job.run.pps)<=0 && pps_size[i]>0 ) {
            		memcpy( job.run.pps, pps_header[i], pps_size[i]);
                	job.run.plen = pps_size[i];
            	}
                free(pps_header[i]);
            }
			free(pps_header);
			free(pps_size);
        } else if (MP4_IS_AUDIO_TRACK_TYPE( type )) {
			job.run.audio_track = id;
			job.run.audio_frame_num = MP4GetTrackNumberOfSamples( job.run.mp4_file, id);
			job.run.audio_timescale = MP4GetTrackTimeScale( job.run.mp4_file, id);
//			job.run.audio_codec = MP4GetTrackAudioMpeg4Type( job.run.mp4_file, id);
			log_qcy(DEBUG_SERIOUS, "audio codec = %s", MP4GetTrackMediaDataName(job.run.mp4_file, id));
			job.run.audio_index = 1;
        }
    }
    //seek
    diff = (flist.start[job.init.current->data] - job.run.start) * 1000;
    for( i=1;i<=job.run.video_frame_num;i++) {
        sample_time = MP4GetSampleTime( job.run.mp4_file, job.run.video_track, i );
        sample_time = sample_time * 1000 / job.run.video_timescale;
    	if ( ( diff + sample_time ) >= 0 ) {
    		if( i>1 ) job.run.video_index = i;
    		else job.run.video_index = 1;
    		break;
    	}
    }
    for( i=1;i<=job.run.audio_frame_num;i++) {
        sample_time = MP4GetSampleTime( job.run.mp4_file, job.run.audio_track, i );
        sample_time = sample_time * 1000 / job.run.audio_timescale;
    	if ( ( diff + sample_time ) >= 0 ) {
    		if( i>1 ) job.run.audio_index = i;
    		else job.run.audio_index = 1;
    		break;
    	}
    }
    return 0;
}

static int player_close_mp4(void)
{
	int ret = 0;
	if( job.run.mp4_file ) {
		MP4Close( job.run.mp4_file, 0);
		log_qcy(DEBUG_SERIOUS, "$%$%$%closed file %s$%$%$%", job.run.file_path);
		memset( &job.run, 0, sizeof(player_run_t));
	}
	return ret;
}

static int player_start(void)
{
	int ret = 0;
	return ret;
}

static int player_main(void)
{
	int ret = 0;
	int ret_video, ret_audio;
	if( job.run.mp4_file == NULL ) {
		ret = player_open_mp4();
		if( ret )
			goto next_stream;
	}
	if( !job.run.vstream_wait )
		ret_video = player_get_video_frame();
	if( !job.run.astream_wait )
		ret_audio = player_get_audio_frame();
	if( job.run.i_frame_read ) {
		if( job.run.video_sync > job.run.audio_sync + DEFAULT_SYNC_DURATION ) {
			job.run.vstream_wait = 1;
			job.run.astream_wait = 0;
		}
		else if( job.run.audio_sync > job.run.video_sync + DEFAULT_SYNC_DURATION ) {
			job.run.astream_wait = 1;
			job.run.vstream_wait = 0;
		}
		else {
			job.run.astream_wait = 0;
			job.run.vstream_wait = 0;
		}
	}
	if( ret_video!=0 || ret_audio!=0 ) {
		job.run.astream_wait = 0;
		job.run.vstream_wait = 0;
	}
	if( ret_video && ret_audio ) {
		goto next_stream;
	}
	return 0;
next_stream:
	player_close_mp4();
//	job.run.start = flist.stop[job.init.current->data];
	if( job.init.current->next == NULL ) {
		ret = -1;
	}
/*	else {
		job.init.current = job.init.current->next;
		if( job.init.current->next == NULL)
			job.run.stop = job.init.stop;
		else
			job.run.stop = 0;
	}
*/
	return ret;
}

static int player_interrupt(void)
{
	int ret = 0;
	if( info.status == STATUS_RUN ) {
		player_close_mp4();
	}
	memset(&job, 0, sizeof(player_job_t));
	info.status = STATUS_IDLE;
	return ret;
}

static int player_stop(void)
{
	message_t msg;
	int ret;
    /********message body********/
	msg_init(&msg);
	memcpy(&msg.arg_pass,&info.task.msg.arg_pass, sizeof(message_arg_t));
	msg.arg_pass.cat = msg.arg_pass.cat + 1;	//trick here!!!
	msg.message = MSG_PLAYER_STOP_ACK;
	msg.sender = msg.receiver = SERVER_PLAYER;
	msg.arg_in.cat = job.init.switch_to_live;
	ret = server_miss_message(&msg);
	/****************************/
	memset(&job, 0, sizeof(player_job_t));
	info.status = STATUS_IDLE;
	return ret;
}

static void server_thread_termination(int sign)
{
    /********message body********/
	message_t msg;
	msg_init(&msg);
	msg.message = MSG_PLAYER_SIGINT;
	msg.sender = msg.receiver = SERVER_PLAYER;
	manager_message(&msg);
	/****************************/
}

static int server_release(void)
{
	int ret = 0;
	msg_buffer_release(&message);
	msg_free(&info.task.msg);
	memset(&info,0,sizeof(server_info_t));
	memset(&config,0,sizeof(player_config_t));
	memset(&job,0,sizeof(player_job_t));
	memset(&flist,0,sizeof(player_file_list_t));
	return ret;
}

static int server_message_proc(void)
{
	int ret = 0, ret1 = 0;
	message_t msg,send_msg;
	char	name[MAX_SYSTEM_STRING_SIZE];
	msg_init(&msg);
	ret = pthread_rwlock_wrlock(&message.lock);
	if(ret)	{
		log_qcy(DEBUG_SERIOUS, "add message lock fail, ret = %d\n", ret);
		return ret;
	}
	if( info.msg_lock ) {
		ret1 = pthread_rwlock_unlock(&message.lock);
		return 0;
	}
	ret = msg_buffer_pop(&message, &msg);
	ret1 = pthread_rwlock_unlock(&message.lock);
	if (ret1) {
		log_qcy(DEBUG_SERIOUS, "add message unlock fail, ret = %d\n", ret1);
	}
	if( ret == -1) {
		msg_free(&msg);
		return -1;
	}
	else if( ret == 1)
		return 0;
    /********message body********/
	msg_init(&send_msg);
	memcpy(&(send_msg.arg_pass), &(msg.arg_pass),sizeof(message_arg_t));
	memcpy(&(send_msg.arg_in), &(msg.arg_in),sizeof(message_arg_t));
	send_msg.message = msg.message | 0x1000;
	send_msg.sender = send_msg.receiver = SERVER_PLAYER;
	/***************************/
	switch(msg.message) {
		case MSG_PLAYER_START:
			if( (info.status != STATUS_IDLE) && (info.status != STATUS_RUN) ) ret = -1;
			else if( player_init(&msg) ) ret = -1;
			else ret = 0;
			send_msg.arg_in.cat = job.init.switch_to_live;
			send_msg.result = ret;
			ret = send_message(msg.receiver, &send_msg);
			if( !ret ) info.status = STATUS_START;
			memcpy(&info.task.msg, &msg,sizeof(message_t));
			break;
		case MSG_PLAYER_STOP:
			msg.arg_in.cat = job.init.switch_to_live;
			if( info.status != STATUS_RUN ) ret = 0;
			else {
				ret = player_interrupt();
			}
			send_msg.result = ret;
			ret = send_message(msg.receiver, &send_msg);
			break;
		case MSG_MANAGER_EXIT:
			info.exit = 1;
			break;
		case MSG_MANAGER_TIMER_ACK:
			((HANDLER)msg.arg_in.handler)();
			break;
		case MSG_MIIO_PROPERTY_NOTIFY:
		case MSG_MIIO_PROPERTY_GET_ACK:
			if( msg.arg_in.cat == MIIO_PROPERTY_TIME_SYNC ) {
				if( msg.arg_in.dog == 1 )
					misc_set_bit( &info.thread_exit, PLAYER_INIT_CONDITION_MIIO_TIME, 1);
			}
			break;
		case MSG_RECORDER_PROPERTY_GET_ACK:
			if( !msg.result ) {
				if( msg.arg_in.cat == RECORDER_PROPERTY_NORMAL_DIRECTORY ) {
					strcpy(config.profile.path, msg.arg);
					strcpy(config.profile.prefix, msg.extra);
					misc_set_bit( &info.thread_exit, PLAYER_INIT_CONDITION_RECORDER_CONFIG, 1);
					if( !player_read_file_list( config.profile.path) ) {
						misc_set_bit( &info.thread_exit, PLAYER_INIT_CONDITION_FILE_LIST, 1);
					}
				}
			}
			break;
		case MSG_RECORDER_ADD_FILE:
			if( info.status >= STATUS_SETUP ) {
	        	memset(name, 0, sizeof(name));
	        	memcpy(name, (char*)(msg.arg), 14);
	        	flist.start[flist.num] = time_date_to_stamp( name );
	        	memset(name, 0, sizeof(name));
	        	memcpy(name, &( ((char*)msg.arg)[14] ), 14);
	        	flist.stop[flist.num] = time_date_to_stamp( name );
			}
			break;
		default:
			log_qcy(DEBUG_SERIOUS, "not processed message = %d", msg.message);
			break;
	}
	msg_free(&msg);
	return ret;
}

static int heart_beat_proc(void)
{
	int ret = 0;
	message_t msg;
	long long int tick = 0;
	tick = time_get_now_stamp();
	if( (tick - info.tick) > SERVER_HEARTBEAT_INTERVAL ) {
		info.tick = tick;
	    /********message body********/
		msg_init(&msg);
		msg.message = MSG_MANAGER_HEARTBEAT;
		msg.sender = msg.receiver = SERVER_PLAYER;
		msg.arg_in.cat = info.status;
		msg.arg_in.dog = info.thread_start;
		msg.arg_in.duck = info.thread_exit;
		ret = manager_message(&msg);
		/***************************/
	}
	return ret;
}

/*
 * task
 */
/*
 * task error: error->5 seconds->shut down server->msg manager
 */
static void task_error(void)
{
	unsigned int tick=0;
	switch( info.status ) {
		case STATUS_ERROR:
			log_qcy(DEBUG_SERIOUS, "!!!!!!!!error in player, restart in 5 s!");
			info.tick3 = time_get_now_stamp();
			info.status = STATUS_NONE;
			break;
		case STATUS_NONE:
			tick = time_get_now_stamp();
			if( (tick - info.tick3) > SERVER_RESTART_PAUSE ) {
				info.exit = 1;
				info.tick3 = tick;
			}
			break;
		default:
			log_qcy(DEBUG_SERIOUS, "!!!!!!!unprocessed server status in task_error = %d", info.status);
			break;
	}
	usleep(1000);
	return;
}
/*
 * default task: none->run
 */
static void task_default(void)
{
	message_t msg;
	int ret = 0;
	player_iot_config_t player;
	switch( info.status){
		case STATUS_NONE:
			if( !misc_get_bit( info.thread_exit, PLAYER_INIT_CONDITION_CONFIG ) ) {
				ret = config_player_read(&config);
				if( !ret && misc_full_bit(config.status, CONFIG_PLAYER_MODULE_NUM) ) {
					misc_set_bit(&info.thread_exit, PLAYER_INIT_CONDITION_CONFIG, 1);
				}
				else {
					info.status = STATUS_ERROR;
					break;
				}
			}
			if( !misc_get_bit( info.thread_exit, PLAYER_INIT_CONDITION_MIIO_TIME )
					&& (info.tick2 - time_get_now_stamp()) > MESSAGE_RESENT ) {
				info.tick2 = time_get_now_stamp();
			    /********message body********/
				msg_init(&msg);
				msg.message = MSG_MIIO_PROPERTY_GET;
				msg.sender = msg.receiver = SERVER_RECORDER;
				msg.arg_in.cat = MIIO_PROPERTY_TIME_SYNC;
				server_miio_message(&msg);
				/****************************/
			}
			if( !misc_get_bit( info.thread_exit, PLAYER_INIT_CONDITION_RECORDER_CONFIG)
				&& (info.tick2 - time_get_now_stamp()) > MESSAGE_RESENT ) {
				info.tick2 = time_get_now_stamp();
			    /********message body********/
				msg_init(&msg);
				msg.message = MSG_RECORDER_PROPERTY_GET;
				msg.sender = msg.receiver = SERVER_PLAYER;
				msg.arg_in.cat = RECORDER_PROPERTY_NORMAL_DIRECTORY;
				ret = server_recorder_message(&msg);
				/***************************/
			}
			if( misc_full_bit( info.thread_exit, PLAYER_INIT_CONDITION_NUM ) )
				info.status = STATUS_WAIT;
			break;
		case STATUS_WAIT:
			info.status = STATUS_SETUP;
			break;
		case STATUS_SETUP:
			info.status = STATUS_IDLE;
			break;
		case STATUS_IDLE:
			break;
		case STATUS_START:
			player_start();
			info.status = STATUS_RUN;
			break;
		case STATUS_RUN:
			if( player_main() ) player_stop();
			break;
		case STATUS_STOP:
			info.status = STATUS_IDLE;
			break;
		case STATUS_ERROR:
			info.task.func = task_error;
			break;
		default:
			log_qcy(DEBUG_SERIOUS, "!!!!!!!unprocessed server status in task_default = %d", info.status);
			break;
		}
	usleep(1000);
	return;
}

/*
 * server entry point
 */
static void *server_func(void)
{
    signal(SIGINT, server_thread_termination);
    signal(SIGTERM, server_thread_termination);
	misc_set_thread_name("server_player");
	pthread_detach(pthread_self());
	if( !message.init ) {
		msg_buffer_init(&message, MSG_BUFFER_OVERFLOW_NO);
	}
	//default task
	info.task.func = task_default;
	info.task.start = STATUS_NONE;
	info.task.end = STATUS_RUN;
	while( !info.exit ) {
		info.task.func();
		server_message_proc();
		heart_beat_proc();
	}
	if( info.exit ) {
		while( info.thread_start ) {
		}
	    /********message body********/
		message_t msg;
		msg_init(&msg);
		msg.message = MSG_MANAGER_EXIT_ACK;
		msg.sender = SERVER_PLAYER;
		manager_message(&msg);
		/***************************/
	}
	server_release();
	log_qcy(DEBUG_SERIOUS, "-----------thread exit: server_player-----------");
	pthread_exit(0);
}

/*
 * internal interface
 */

/*
 * external interface
 */
int server_player_start(void)
{
	int ret=-1;
	ret = pthread_create(&info.id, NULL, server_func, NULL);
	if(ret != 0) {
		log_qcy(DEBUG_SERIOUS, "player server create error! ret = %d",ret);
		 return ret;
	 }
	else {
		log_qcy(DEBUG_SERIOUS, "player server create successful!");
		return 0;
	}
}

int server_player_message(message_t *msg)
{
	int ret=0,ret1;
	if( !message.init ) {
		log_qcy(DEBUG_SERIOUS, "player server is not ready for message processing!");
		return -1;
	}
	ret = pthread_rwlock_wrlock(&message.lock);
	if(ret)	{
		log_qcy(DEBUG_SERIOUS, "add message lock fail, ret = %d\n", ret);
		return ret;
	}
	ret = msg_buffer_push(&message, msg);
	log_qcy(DEBUG_SERIOUS, "push into the player message queue: sender=%d, message=%x, ret=%d, head=%d, tail=%d", msg->sender, msg->message, ret,
			message.head, message.tail);
	if( ret!=0 )
		log_qcy(DEBUG_SERIOUS, "message push in player error =%d", ret);
	ret1 = pthread_rwlock_unlock(&message.lock);
	if (ret1)
		log_qcy(DEBUG_SERIOUS, "add message unlock fail, ret = %d\n", ret1);
	return ret;
}
