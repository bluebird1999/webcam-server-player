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
#include <malloc.h>
#ifdef DMALLOC_ENABLE
#include <dmalloc.h>
#endif
//program header
#include "../../manager/manager_interface.h"
#include "../../server/realtek/realtek_interface.h"
#include "../../tools/tools_interface.h"
#include "../../server/player/player_interface.h"
#include "../../server/miio/miio_interface.h"
#include "../../server/audio/audio_interface.h"
#include "../../server/recorder/recorder_interface.h"
#include "../../server/miss/miss_interface.h"
#include "../../server/device/device_interface.h"
#include "../../server/video2/video2_interface.h"
#include "../../server/kernel/kernel_interface.h"
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
static	player_job_t			jobs[MAX_SESSION_NUMBER];
static	player_file_list_t		flist;
static 	av_buffer_t				pvbuffer[MAX_SESSION_NUMBER];
static 	av_buffer_t				pabuffer[MAX_SESSION_NUMBER];
static  pthread_rwlock_t		ilock = PTHREAD_RWLOCK_INITIALIZER;
static	pthread_rwlock_t		pvlock[MAX_SESSION_NUMBER] = {PTHREAD_RWLOCK_INITIALIZER,PTHREAD_RWLOCK_INITIALIZER,PTHREAD_RWLOCK_INITIALIZER};
static	pthread_rwlock_t		palock[MAX_SESSION_NUMBER] = {PTHREAD_RWLOCK_INITIALIZER,PTHREAD_RWLOCK_INITIALIZER,PTHREAD_RWLOCK_INITIALIZER};
static	pthread_mutex_t			mutex = PTHREAD_MUTEX_INITIALIZER;
static	pthread_cond_t			cond = PTHREAD_COND_INITIALIZER;
static	char					hotplug;

//function
//common
static void *server_func(void);
static int server_message_proc(void);
static void server_release_1(void);
static void server_release_2(void);
static void server_release_3(void);
static void task_default(void);
static void task_exit(void);
static void server_thread_termination(void);
//specific
static int player_main(player_init_t *init, player_run_t *run, av_buffer_t *vbuffer, av_buffer_t *abuffer, player_list_node_t *fhead, int speed);
static int *player_func(void* arg);
static int find_job_session(int session);
static void *player_picture_func(void *arg);

/*
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 */

/*
 * helper
 */
static int player_set_property(message_t *msg)
{
	int ret = 0, id = -1;
	message_t send_msg;
    /********message body********/
	msg_init(&send_msg);
	memcpy(&(send_msg.arg_pass), &(msg->arg_pass),sizeof(message_arg_t));
	send_msg.message = msg->message | 0x1000;
	send_msg.sender = send_msg.receiver = SERVER_PLAYER;
	send_msg.arg_in.cat = msg->arg_in.cat;
	/****************************/
	pthread_rwlock_rdlock(&ilock);
	id = find_job_session( msg->arg_in.wolf );
	if( id == -1) {
		ret = -1;
		goto send;
	}
	if( msg->arg_in.cat == PLAYER_PROPERTY_SPEED ) {
		if( info.status != STATUS_RUN ) ret = 0;
		else {
			int temp = *((int*)(msg->arg));
			jobs[id].init.speed = temp;
		}
	}
send:
	pthread_rwlock_unlock(&ilock);
	/***************************/
	send_msg.result = ret;
	ret = manager_common_send_message(msg->receiver, &send_msg);
	/***************************/
	return ret;
}

static int player_get_file_date(message_t *msg)
{
	int ret = 0, i;
	message_t send_msg;
	unsigned int empty[2]={0,0};
	unsigned int num = 0, len = 0, cmd, all = 0, temp;
	char *data = NULL;
	unsigned long long int init = 0, stamp;
	char newdate[MAX_SYSTEM_STRING_SIZE];
    /********message body********/
	msg_init(&send_msg);
	memcpy(&(send_msg.arg_pass), &(msg->arg_pass),sizeof(message_arg_t));
	send_msg.message = msg->message | 0x1000;
	send_msg.sender = send_msg.receiver = SERVER_PLAYER;
	/****************************/
	pthread_rwlock_rdlock(&ilock);
	if( misc_get_bit( info.init_status, PLAYER_INIT_CONDITION_FILE_LIST)==0 ) {
		ret = -1;
		goto send;
	}
	else {
		init = 0;
		for (i = 0; i < flist.num; i++) {
			if( init == 0) {
				memset(newdate, 0, sizeof(newdate));
				time_stamp_to_date( flist.start[i], newdate);
				strcpy(&newdate[8], "000000");
				init = time_date_to_stamp(newdate);
				num++;
				continue;
			}
			if( (flist.start[i] >= init) && (flist.start[i] < ( init + 86400 ) ) ) continue;
			else if( flist.start[i] >= (init+86400) ) {
				init = init + ( (int)( ( flist.start[i] - ( init + 86400 ) ) / 86400 ) + 1 ) * 86400;
				num++;
			}
		}
		if( num>0 ) {
			cmd = msg->arg_in.duck;
			len = sizeof(unsigned int) * num;
			all = sizeof(cmd) + sizeof(num) + len;
			data = malloc( all );
			if(!data) {
				log_qcy(DEBUG_WARNING, "Fail to calloc. size = %d", len);
				ret = -1;
				goto send;
			}
			memset(data, 0, all );
			memcpy(data, &cmd, sizeof(cmd));
			memcpy(data + sizeof(cmd), &num, sizeof(num));
			init = 0;
			num = 0;
			char *p = data + sizeof(cmd) + sizeof(num);
			for (i = 0; i < flist.num; i++) {
				if( init == 0) {
					memset(newdate, 0, sizeof(newdate));
					time_stamp_to_date( flist.start[i], newdate);
					strcpy(&newdate[8], "000000");
					init = time_date_to_stamp(newdate);
					newdate[8] = '\0';
					temp = atoi(newdate);
					memcpy( p, &temp, sizeof(temp) );
					p += sizeof(temp);
					continue;
				}
				if( (flist.start[i] >= init) && (flist.start[i] < ( init + 86400 ) ) ) continue;
				else if( flist.start[i] >= (init+86400) ) {
					init = init + ( (int)( ( flist.start[i] - ( init + 86400 ) ) / 86400 ) + 1 ) * 86400;
					memset(newdate, 0, sizeof(newdate));
					time_stamp_to_date( init, newdate);
					newdate[8] = '\0';
					temp = atoi(newdate);
					memcpy( p, &temp, sizeof(temp) );
					p += sizeof(temp);
				}
			}
		}
		else {
			goto send;
		}
		send_msg.arg = data;
		send_msg.arg_size = all;
		send_msg.result = 0;
		manager_common_send_message(msg->sender, &send_msg);
		pthread_rwlock_unlock(&ilock);
		free(data);
		return ret;
	}
send:
	pthread_rwlock_unlock(&ilock);
	cmd = msg->arg_in.duck;
	empty[0] = cmd;
	send_msg.arg = empty;
	send_msg.arg_size = sizeof(empty);
	send_msg.result = 0;
	manager_common_send_message(msg->sender, &send_msg);
	return ret;
}

static int player_get_file_list(message_t *msg)
{
	int ret = 0, i;
	miss_playlist_t list;
	unsigned int  empty[2]={0,0};
	unsigned char *data = NULL;
	unsigned char *p = NULL;
	unsigned int cmd, all = 0, num =0, len = 0;
	struct tm  ts={0};
	unsigned long long int start,end, temp, chn, stamp;
	message_t send_msg;
    /********message body********/
	msg_init(&send_msg);
	memcpy(&(send_msg.arg_pass), &(msg->arg_pass),sizeof(message_arg_t));
	send_msg.message = msg->message | 0x1000;
	send_msg.sender = send_msg.receiver = SERVER_PLAYER;
	/****************************/
	start = msg->arg_in.cat;
	end = msg->arg_in.dog;
	cmd = msg->arg_in.duck;
	chn = msg->arg_in.chick;
	num = 0;
	pthread_rwlock_rdlock(&ilock);
	if( misc_get_bit( info.init_status, PLAYER_INIT_CONDITION_FILE_LIST)==0 ) {
		ret = -1;
		goto send;
	}
	else {
		for (i = 0; i < flist.num; i++) {
			if( flist.start[i] < start ) continue;
			if( flist.stop[i] > end ) continue;
			num++;
		}
		if( num > 0) {
			if( cmd == GET_RECORD_FILE ) {
				len = sizeof(miss_playlist_t) * num;
				all = sizeof(cmd) + sizeof(num) + len;
				data = malloc( all );
				if( !data ) {
					log_qcy(DEBUG_SERIOUS, "Fail to calloc. size = %d", len);
					ret = -1;
					goto send;
				}
				memset(data, 0, all );
				memcpy(data, &cmd, sizeof(cmd));
				memcpy(data + sizeof(cmd), &num, sizeof(num));
				p = data + sizeof(cmd) + sizeof(num);
				for (i = 0; i < flist.num; i++) {
					if( flist.start[i] < start ) continue;
					if( flist.stop[i] > end ) continue;
					memset(&ts, 0, sizeof(ts));
					stamp = flist.start[i] /*- ( (_config_.timezone - 80) * 360)*/;
					localtime_r( &stamp, &ts );
					list.recordType 			= 0x04;
					list.channel    			= chn;
					list.deviceId   			= 0;
					list.startTime.dwYear   	= ts.tm_year + 1900;
					list.startTime.dwMonth  	= ts.tm_mon + 1;
					list.startTime.dwDay  		= ts.tm_mday;
					list.startTime.dwHour   	= ts.tm_hour;
					list.startTime.dwMinute 	= ts.tm_min;
					list.startTime.dwSecond 	= ts.tm_sec;
					memset(&ts, 0, sizeof(ts));
					stamp = flist.stop[i] /*- ( (_config_.timezone - 80) * 360)*/;
					localtime_r( &stamp, &ts );
					list.endTime.dwYear   		= ts.tm_year + 1900;
					list.endTime.dwMonth 		= ts.tm_mon + 1;
					list.endTime.dwDay  		= ts.tm_mday;
					list.endTime.dwHour   		= ts.tm_hour;
					list.endTime.dwMinute 		= ts.tm_min;
					list.endTime.dwSecond 		= ts.tm_sec;
					list.totalNum++;
					memcpy(p, &list, sizeof(miss_playlist_t) );
					p += sizeof(miss_playlist_t);
				}
			}
			else if( cmd == GET_RECORD_TIMESTAMP) {
				len = sizeof(num) + num * ( sizeof(unsigned long long int) * 3 );
				all = sizeof(cmd) + sizeof(len) + len;
				data = malloc( all );
				if( !data ) {
					log_qcy(DEBUG_SERIOUS, "Fail to calloc. size = %d", all );
					ret = -1;
					goto send;
				}
				memset(data, 0, all );
				memcpy(data, &cmd, sizeof(cmd));
				memcpy(data + sizeof(cmd), &len, sizeof(len));
				memcpy(data + sizeof(cmd) + sizeof(len), &num, sizeof(num));
				p = data + sizeof(cmd) + sizeof(len) + sizeof(num);
				for (i = 0; i < flist.num; i++) {
					if( flist.start[i] < start ) continue;
					if( flist.stop[i] > end ) continue;
					memcpy(p, &chn, sizeof(chn));
					p += sizeof(chn);
					memset(&ts, 0, sizeof(ts));
					stamp = flist.start[i] /*- ( (_config_.timezone - 80) * 360)*/;
					localtime_r( &stamp, &ts );
					temp		= 	((ts.tm_year + 1900) * 1e+10) +
										((ts.tm_mon + 1) * 1e+8) +
										( ts.tm_mday  * 1e+6 ) +
										( ts.tm_hour * 1e+4) +
										( ts.tm_min * 1e+2) +
										( ts.tm_sec );
					memcpy(p, &temp, sizeof(temp));
					p += sizeof(temp);
					memset(&ts, 0, sizeof(ts));
					stamp = flist.stop[i] /*- ( (_config_.timezone - 80) * 360)*/;
					localtime_r( &stamp, &ts );
					temp		= 	((ts.tm_year + 1900) * 1e+10) +
										((ts.tm_mon + 1) * 1e+8) +
										( ts.tm_mday  * 1e+6 ) +
										( ts.tm_hour * 1e+4) +
										( ts.tm_min * 1e+2) +
										( ts.tm_sec );
					memcpy(p, &temp, sizeof(temp));
					p += sizeof(temp);
				}
			}
			send_msg.arg = data;
			send_msg.arg_size = all;
			send_msg.result = 0;
			manager_common_send_message(msg->sender, &send_msg);
			free(data);
			pthread_rwlock_unlock(&ilock);
			return 0;
		}
		else {
			goto send;
		}
	}
send:
	pthread_rwlock_unlock(&ilock);
	cmd = msg->arg_in.duck;
	empty[0] = cmd;
	send_msg.arg = empty;
	send_msg.arg_size = sizeof(empty);
	send_msg.result = 0;
	manager_common_send_message(msg->sender, &send_msg);
	return ret;
}

static int player_get_picture_list(message_t *msg)
{
	int ret = 0;
	unsigned int  empty[2]={0,0};
	static message_t smsg;
	message_t send_msg;
	pthread_t pid;
	msg_init(&smsg);
	memcpy(&smsg, msg, sizeof(message_t));
	ret = pthread_create(&pid, NULL, player_picture_func, (void*)&smsg);
	if(ret != 0) {
		log_qcy(DEBUG_SERIOUS, "player picture reading thread create error! ret = %d",ret);
	    /********message body********/
		msg_init(&send_msg);
		memcpy(&(send_msg.arg_pass), &(msg->arg_pass),sizeof(message_arg_t));
		send_msg.message = msg->message | 0x1000;
		send_msg.sender = send_msg.receiver = SERVER_PLAYER;
		empty[0] = msg->arg_in.duck;
		send_msg.result = 0;
		send_msg.arg = empty;
		send_msg.arg_size = sizeof(empty);
		manager_common_send_message(msg->sender, &send_msg);
		/****************************/
	}
	return 0;
}

static void *player_picture_func(void *arg)
{
	int ret = 0;
	unsigned int  empty[2]={0,0};
	int i, num;
	FILE *fp = NULL;
	message_t send_msg;
	unsigned int len, offset, all, size;
	unsigned int start, fsize, cmd;
	char *data=NULL;
	char fname[2*MAX_SYSTEM_STRING_SIZE];
	char start_str[MAX_SYSTEM_STRING_SIZE];
	char stop_str[MAX_SYSTEM_STRING_SIZE];
	unsigned int begin[128],end[128];
	pthread_detach(pthread_self());
	log_qcy(DEBUG_INFO, "---------------------player picture listing thread started!---------------");
    signal(SIGINT, server_thread_termination);
    signal(SIGTERM, server_thread_termination);
    misc_set_thread_name("player_picture_list");
	message_t *msg = (message_t*)arg;
    /********message body********/
	msg_init(&send_msg);
	memcpy(&(send_msg.arg_pass), &(msg->arg_pass),sizeof(message_arg_t));
	send_msg.message = msg->message | 0x1000;
	send_msg.sender = send_msg.receiver = SERVER_PLAYER;
	/****************************/
    cmd = msg->arg_in.duck;
	if( misc_get_bit( info.init_status, PLAYER_INIT_CONDITION_FILE_LIST)==0 ) {
		ret = -1;
		goto send;
	}
	else {
		pthread_rwlock_rdlock(&ilock);
		size = 0;
		for (i = 0; (i < flist.num) && (!info.exit); i++) {
			if( flist.start[i] < msg->arg_in.cat ) continue;
			if( flist.stop[i] > msg->arg_in.dog + 59) continue;
			begin[size] = (unsigned int)flist.start[i];
			end[size] = (unsigned int)flist.stop[i];
			size++;
			if(size>128) {
				log_qcy(DEBUG_WARNING, "---exceed the 128 jpg file list limit, quit!---------");
				break;
			}
		}
		pthread_rwlock_unlock(&ilock);
		for( i = 0; i<size && (!info.exit); i++) {
			memset( fname, 0 ,sizeof(fname));
			memset( start_str, 0, sizeof(start_str));
			memset( stop_str, 0, sizeof(stop_str));
			time_stamp_to_date_with_zone( begin[i], start_str, 80, _config_.timezone);
			time_stamp_to_date_with_zone( end[i], stop_str, 80, _config_.timezone);
			sprintf( fname, "%s%s-%s_%s.jpg",config.profile.path, config.profile.prefix, start_str, stop_str);
	        fp = fopen(fname, "rb");
	        if (fp == NULL) {
	            log_qcy(DEBUG_SERIOUS, "fopen error %s not exist!\n", fname);
	            continue;
	        }
	        if (0 != fseek(fp, 0, SEEK_END)) {
	            fclose(fp);
	            continue;
	        }
	        fsize = ftell(fp);
	        start = (unsigned int)begin[i];
	        all = sizeof(cmd) + sizeof(start) + sizeof(fsize) + fsize;
	        data = malloc( all );
	        if( !data ) {
	            fclose(fp);
	            ret = -1;
	            goto send;
	        }
	        memset(data, 0, all );
	        if(0 != fseek(fp, 0, SEEK_SET)) {
	            free(data);
	            fclose(fp);
	            ret = -1;
	            goto send;
	        }
	        memcpy(data, &cmd, sizeof(cmd));
	        memcpy(data + sizeof(cmd), &start, sizeof(start));
	        memcpy(data + sizeof(cmd) + sizeof(start), &fsize, sizeof(fsize));
	        len = fsize;
	        offset = 0;
	        while (len > 0) {
	            ret = fread( data + sizeof(cmd) + sizeof(start) + sizeof(fsize) + offset, 1, len, fp);
	            if (ret >= 0) {
	                offset += ret;
	                len -= ret;
	            }
	            else {
	                log_qcy(DEBUG_WARNING, "offset:%d  len:%d  ret:%d\n", offset, len, ret);
	                break;
	            }
	        }
			/***************************************/
			send_msg.arg = data;
			send_msg.arg_size = all;
			send_msg.result = 0;
			manager_common_send_message(msg->sender, &send_msg);
			/***************************************/
	        offset = 0;
			num++;
			free(data);
	        fclose(fp);
	        usleep(10000);
		}
		ret = -2;
	}
send:
	empty[0] = cmd;
	send_msg.arg = empty;
	send_msg.arg_size = sizeof(empty);
	send_msg.result = 0;
	manager_common_send_message(msg->sender, &send_msg);
	manager_common_send_dummy(SERVER_PLAYER);
	log_qcy(DEBUG_INFO, "---------------------player picture listing thread exit---------------");
	pthread_exit(0);
}

int player_read_file_list(char *path)
{
	struct dirent **namelist;
	int n;
	char name[MAX_SYSTEM_STRING_SIZE*8];
	char *p = NULL;

	memset(&flist, 0, sizeof(player_file_list_t));
	pthread_rwlock_rdlock(&ilock);
	if(access(path, F_OK))
		mkdir("/mnt/media/normal",0777);
	n = scandir(path, &namelist, 0, alphasort);
	if(n < 0) {
	    log_qcy(DEBUG_SERIOUS, "Open dir error %s", path);
	    pthread_rwlock_unlock(&ilock);
	    return -1;
	}
	int index=0;
	while(index < n) {
        if(strcmp(namelist[index]->d_name,".") == 0 ||
           strcmp(namelist[index]->d_name,"..") == 0 )
        	goto exit;
        if(namelist[index]->d_type == 10) goto exit;
        if(namelist[index]->d_type == 4) goto exit;
		if( (strstr(namelist[index]->d_name,".mp4") == NULL) &&
				(strstr(namelist[index]->d_name,".jpg") == NULL)	) {
			//remove file here.
			memset(name, 0, sizeof(name));
			sprintf(name, "%s%s", path, namelist[index]->d_name);
			remove( name );
			goto exit;
		}
		if( strstr(namelist[index]->d_name,".jpg") ) goto exit;
		p = strstr( namelist[index]->d_name, "202");	//first
		if( p == NULL) goto exit;
		memset(name, 0, sizeof(name));
		memcpy(name, p, 14);
		long long int st,ed;
		st = time_date_to_stamp_with_zone( name, 80, _config_.timezone );
		p+=15;
		memset(name, 0, sizeof(name));
		memcpy(name, p, 14);
		ed = time_date_to_stamp_with_zone( name, 80, _config_.timezone);
		if( st >= ed ) {
			memset(name, 0, sizeof(name));
			sprintf(name, "%s%s", path, namelist[index]->d_name);
			remove(name );
			goto exit;
		}
		flist.start[flist.num] = st;
		flist.stop[flist.num] = ed;
		flist.num ++;
exit:
		free(namelist[index]);
	    index++;
		if( flist.num >= MAX_FILE_NUM) {
			flist.num = MAX_FILE_NUM;
			break;
		}
	}
	free(namelist);
	pthread_rwlock_unlock(&ilock);
    return 0;
}

static int player_node_remove_last(player_list_node_t **head)
{
    int ret = 0;
    player_list_node_t *current = *head;
    if ( current->next == NULL) {
        free(current);
        return 0;
    }
    while (current->next->next != NULL) {
        current = current->next;
    }
    free(current->next);
    current->next = NULL;
    return ret;
}

static int player_node_clear(player_list_node_t **fhead)
{
	if( *fhead == NULL) return 0;
    while( (*fhead)->next != NULL)
        player_node_remove_last(fhead);
    player_node_remove_last( fhead );
    *fhead = NULL;
    return 0;
}

static int player_node_insert(player_list_node_t **fhead, unsigned int start, unsigned int stop)
{
	player_list_node_t *current;
    player_list_node_t *node = malloc(sizeof(player_list_node_t));
    node->start = start;
    node->stop = stop;
    node->next = NULL;
    if( *fhead == NULL ) *fhead = node;
    else {
        for (current = *fhead; current->next != NULL; current = current->next);
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

static int player_check_file_list(long long start, long long end)
{
	int ret = 0, start_index, stop_index;
	int i, num;
	if( flist.num <= 0) return -1;
	start_index = player_file_bisearch_start(start);
	stop_index = player_file_bisearch_stop(end);
	if( start_index<0 || stop_index<0 ) return -1;
	if( (start_index > (flist.num-1)) || (stop_index > (flist.num-1)) ) return -1;
	if( start_index > stop_index ) return -1;
	num = 0;
	for(i=start_index;i<=stop_index;i++) {
		if( flist.stop[i] <= start) continue;
		if( flist.start[i] >= end) continue;;
		num++;
	}
	if(num==0) return -1;
	return ret;
}

static int player_search_file_list(long long start, long long end, player_list_node_t **fhead)
{
	int ret = 0, start_index, stop_index;
	int i, num;
	if( player_node_clear(fhead) ) return -1;
	pthread_rwlock_rdlock(&ilock);
	if( flist.num <= 0) {
		pthread_rwlock_unlock(&ilock);
		return -1;
	}
	start_index = player_file_bisearch_start(start);
	stop_index = player_file_bisearch_stop(end);
	if( start_index<0 || stop_index<0 ) {
		pthread_rwlock_unlock(&ilock);
		return -1;
	}
	if( (start_index > (flist.num-1)) || (stop_index > (flist.num-1)) ) {
		pthread_rwlock_unlock(&ilock);
		return -1;
	}
	if( start_index > stop_index ) {
		pthread_rwlock_unlock(&ilock);
		return -1;
	}
	num = 0;
	for(i=start_index;i<=stop_index;i++) {
		if( flist.stop[i] <= start) continue;
		if( flist.start[i] >= end) continue;
		num++;
		player_node_insert(fhead, flist.start[i], flist.stop[i]);
	}
	if( num==0 ) {
		pthread_rwlock_unlock(&ilock);
		return -1;
	}
	pthread_rwlock_unlock(&ilock);
	return ret;
}

static int player_quit_all(int id)
{
	int ret = 0;
	int i;
	pthread_rwlock_wrlock(&ilock);
	for( i=0; i<MAX_SESSION_NUMBER; i++ ) {
		if( id !=-1 && i!=id ) continue;
		misc_set_bit( &jobs[i].exit, i, 1);
	}
	pthread_rwlock_unlock(&ilock);
	return ret;
}

static int player_get_video_frame(player_init_t *init, player_run_t *run, av_buffer_t *buffer, int speed)
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
	av_packet_t *packet = NULL;
	//**************************************
    if( !run->mp4_file ) return -1;
    if( run->video_index >= run->video_frame_num) return -1;
    sample_time = MP4GetSampleTime( run->mp4_file, run->video_track, run->video_index );
	sample_time = sample_time * 1000 / run->video_timescale;
	iframe = 0;
    ret = MP4ReadSample( run->mp4_file, run->video_track, run->video_index,
    		&data,&size,&start_time,&duration,&offset,&iframe);
    if( !ret ) {
    	if(data)
    		free(data);
    	run->video_index++;
    	return -1;
    }
    if( !run->i_frame_read && !iframe ) {
    	if(data) free(data);
    	run->video_index++;
    	return 0;
    }
	if( data ){
		start_time = start_time * 1000 / run->video_timescale;
		offset = offset * 1000 / run->video_timescale;
		if( run->stop ) {
			if( ( start_time / 1000 + run->start) > run->stop ) {
				free(data);
				return -1;
			}
		}
		if( /*!run->i_frame_read && */ iframe ) {
		    /********message body********/
			unsigned char *mdata = NULL;
			mdata = calloc( (size + run->slen + run->plen + 8), 1);
			if( mdata == NULL) {
				log_qcy(DEBUG_SERIOUS, "calloc error, size = %d", (size + run->slen + run->plen + 8));
				free(data);
				return -1;
			}
			p = mdata;
			memcpy(p, NAL, 4); p+=4;
			memcpy(p, run->sps, run->slen); p+=run->slen;
			memcpy(p, NAL, 4); p+=4;
			memcpy(p, run->pps, run->plen); p+=run->plen;
			memcpy(p, data, size);
			memcpy(p, NAL, 4);
			log_qcy(DEBUG_SERIOUS, "first key frame.");
			if( _config_.memory_mode == MEMORY_MODE_SHARED ) {
				packet = av_buffer_get_empty(buffer, &run->qos.buffer_overrun, &run->qos.buffer_success);
				if( packet == NULL) {
					log_qcy(DEBUG_SERIOUS, "-------------PLAYER VIDEO buffer overrun!!!---");
					free(mdata);
					free(data);
					return -1;
				}
				packet->data = mdata;
			}
			else {
				packet = &(buffer->packet[0]);
				packet->data = mdata;
			}
			packet->info.frame_index = run->video_index;
			packet->info.timestamp = start_time + run->start * 1000 + 1000;
			packet->info.flag &= ~(0xF);
			packet->info.flag |= FLAG_STREAM_TYPE_PLAYBACK << 11;
			packet->info.flag |= FLAG_FRAME_TYPE_IFRAME << 0;
			packet->info.flag |= FLAG_RESOLUTION_VIDEO_1080P << 17;
			packet->info.size  = (size + run->slen + run->plen + 8);
			/***************************/
			msg_init(&msg);
			msg.message = MSG_MISS_VIDEO_DATA;
			msg.arg_in.wolf = init->session_id;
			msg.arg_in.handler = init->session;
			if( _config_.memory_mode == MEMORY_MODE_SHARED ) {
				msg.arg = packet;
				msg.arg_size = 0;	//make sure this is 0 for non-deep-copy
				msg.extra_size = 0;
			}
			else {
				msg.arg = packet->data;
				msg.arg_size = packet->info.size;
				msg.extra = &(packet->info);
				msg.extra_size = sizeof(packet->info);
			}
			ret = -1;
			ret = server_miss_video_message(&msg);
			if( (ret == MISS_LOCAL_ERR_MISS_GONE) || (ret == MISS_LOCAL_ERR_SESSION_GONE) ) {
	    		if( _config_.memory_mode == MEMORY_MODE_SHARED) {
	    			av_packet_check(packet);
	    		}
				log_qcy(DEBUG_WARNING, "Player video ring buffer send failed due to non-existing miss server or session");
				player_quit_all(init->tid);
				log_qcy(DEBUG_WARNING, "----shut down player video miss stream due to session lost!------");
			}
			else if( ret == MISS_LOCAL_ERR_AV_NOT_RUN) {
	    		if( _config_.memory_mode == MEMORY_MODE_SHARED) {
	    			av_packet_check(packet);
	    		}
				run->qos.failed_send[0]++;
				if( run->qos.failed_send[0] > PLAYER_MAX_FAILED_SEND) {
					run->qos.failed_send[0] = 0;
					player_quit_all(init->tid);
					log_qcy(DEBUG_WARNING, "----shut down player video miss stream due to long overrun!------");
				}
			}
			else if( ret == 0) {
				if( _config_.memory_mode == MEMORY_MODE_SHARED ) {
					av_packet_add(packet);
				}
				run->qos.failed_send[0] = 0;
			}
			run->i_frame_read = 1;
			if( _config_.memory_mode == MEMORY_MODE_DYNAMIC ) {
				free(mdata);
			}
			free(data);
		}
		else {
			if( ((data[4] & 0x1f) == 0x07) || ((data[4] & 0x1f) == 0x08) ) { //not sending any psp pps
				free(data);
				run->video_index++;
				return 0;
			}
			if( !iframe && speed == 16 ) {
				free(data);
				run->video_index++;
				return 0;
			}
			data[0] = 0x00;
			data[1] = 0x00;
			data[2] = 0x00;
			data[3] = 0x01;
			if( _config_.memory_mode == MEMORY_MODE_SHARED ) {
				packet = av_buffer_get_empty(buffer, &run->qos.buffer_overrun, &run->qos.buffer_success);
				if( packet == NULL) {
					log_qcy(DEBUG_SERIOUS, "-------------PLAYER VIDEO buffer overrun!!!---");
					free(data);
					return -1;
				}
				packet->data = data;
			}
			else {
				packet = &(buffer->packet[0]);
				packet->data = data;
			}
			packet->info.frame_index = run->video_index;
			packet->info.timestamp = start_time + run->start * 1000 + 1000;
			packet->info.flag |= FLAG_STREAM_TYPE_PLAYBACK << 11;
			packet->info.flag &= ~(0xF);
		    if( iframe )// I frame
		    	packet->info.flag |= FLAG_FRAME_TYPE_IFRAME << 0;
		    else
		    	packet->info.flag |= FLAG_FRAME_TYPE_PBFRAME << 0;
		    packet->info.flag |= FLAG_RESOLUTION_VIDEO_1080P << 17;
		    packet->info.size = size;
			/********message body********/
			msg_init(&msg);
			msg.message = MSG_MISS_VIDEO_DATA;
			msg.arg_in.wolf = init->session_id;
			msg.arg_in.handler = init->session;
			if( _config_.memory_mode == MEMORY_MODE_SHARED ) {
				msg.arg = packet;
				msg.arg_size = 0;	//make sure this is 0 for non-deep-copy
				msg.extra_size = 0;
			}
			else {
				msg.arg = packet->data;
				msg.arg_size = packet->info.size;
				msg.extra = &(packet->info);
				msg.extra_size = sizeof(packet->info);
			}
			ret = -1;
			ret = server_miss_video_message(&msg);
			if( (ret == MISS_LOCAL_ERR_MISS_GONE) || (ret == MISS_LOCAL_ERR_SESSION_GONE) ) {
	    		if( _config_.memory_mode == MEMORY_MODE_SHARED) {
	    			av_packet_check(packet);
	    		}
				log_qcy(DEBUG_WARNING, "Player video ring buffer send failed due to non-existing miss server or session");
				player_quit_all(init->tid);
				log_qcy(DEBUG_WARNING, "----shut down player video miss stream due to session lost!------");
			}
			else if( ret == MISS_LOCAL_ERR_AV_NOT_RUN) {
	    		if( _config_.memory_mode == MEMORY_MODE_SHARED) {
	    			av_packet_check(packet);
	    		}
				run->qos.failed_send[0]++;
				if( run->qos.failed_send[0] > PLAYER_MAX_FAILED_SEND) {
					run->qos.failed_send[0] = 0;
					player_quit_all(init->tid);
					log_qcy(DEBUG_WARNING, "----shut down player video miss stream due to long overrun!------");
				}
			}
			else if( ret == 0) {
	    		if( _config_.memory_mode == MEMORY_MODE_SHARED) {
	    			av_packet_add(packet);
	    		}
				run->qos.failed_send[0] = 0;
			}
			run->i_frame_read = 1;
			run->video_sync = start_time;
			if( _config_.memory_mode == MEMORY_MODE_DYNAMIC ) {
				free(data);
			}
		}
		log_qcy(DEBUG_VERBOSE," data = %p,size=%d", data, size);
	}
	if( (speed == 1) || (speed == 2) ||
			(speed == 4) || (speed == 0) ) {
		run->video_index++;
	}
	else if( speed==16) {
		run->video_index++;
	}
    return 0;
}

static int player_get_audio_frame( player_init_t *init, player_run_t *run, av_buffer_t *buffer
		, int speed)
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
	av_packet_t *packet = NULL;
	//*************************************
    if( !run->mp4_file ) return -1;
    if( run->audio_index >= run->audio_frame_num) return -1;
    sample_time = MP4GetSampleTime( run->mp4_file, run->audio_track, run->audio_index );
	sample_time = sample_time * 1000 / run->audio_timescale;
	iframe = 0;
    ret = MP4ReadSample( run->mp4_file, run->audio_track, run->audio_index,
    		&data,&size,&start_time,&duration,&offset,&iframe);
    if( !ret ) {
    	if(data) free(data);
    	run->audio_index++;
    	return -1;
    }
    if( !run->i_frame_read ) {
    	if(data) free(data);
    	run->audio_index++;
    	return 0;
    }
	if( data ){
		start_time = start_time * 1000 / run->audio_timescale;
		offset = offset * 1000 / run->audio_timescale;
		if( run->stop ) {
			if( ( start_time / 1000 + run->start) > run->stop ) {
				free(data);
				return -1;
			}
		}
		if( _config_.memory_mode == MEMORY_MODE_SHARED ) {
			packet = av_buffer_get_empty(buffer, &run->qos.buffer_overrun, &run->qos.buffer_success);
			if( packet == NULL) {
				log_qcy(DEBUG_INFO, "-------------PLAYER AUDIO buffer overrun!!!---");
				free(data);
				return -1;
			}
			packet->data = data;
		}
		else {
			packet = &(buffer->packet[0]);
			packet->data = data;
		}
		packet->info.frame_index = run->audio_index;
		packet->info.timestamp = start_time + run->start * 1000 + 1000;
		packet->info.flag = FLAG_AUDIO_SAMPLE_8K << 3 | FLAG_AUDIO_DATABITS_16 << 7 | FLAG_AUDIO_CHANNEL_MONO << 9 |  FLAG_RESOLUTION_AUDIO_DEFAULT << 17;
		packet->info.size = size;
		/********message body********/
		msg_init(&msg);
		msg.message = MSG_MISS_AUDIO_DATA;
		msg.sender = msg.receiver = SERVER_PLAYER;
		msg.arg_in.wolf = init->session_id;
		msg.arg_in.handler = init->session;
		if( _config_.memory_mode == MEMORY_MODE_SHARED ) {
			msg.arg = packet;
			msg.arg_size = 0;	//make sure this is 0 for non-deep-copy
			msg.extra_size = 0;
		}
		else {
			msg.arg = packet->data;
			msg.arg_size = packet->info.size;
			msg.extra = &(packet->info);
			msg.extra_size = sizeof(packet->info);
		}
		ret = -1;
	    if( jobs[init->tid].audio == 1 ) {
			ret = server_miss_audio_message(&msg);
	    }
		if( (ret == MISS_LOCAL_ERR_MISS_GONE) || (ret == MISS_LOCAL_ERR_SESSION_GONE) ) {
			if( _config_.memory_mode == MEMORY_MODE_SHARED ) {
				av_packet_check(packet);
			}
			log_qcy(DEBUG_WARNING, "Player audio ring buffer send failed due to non-existing miss server or session");
			log_qcy(DEBUG_WARNING, "----shut down player audio miss stream due to session lost!------");
			jobs[init->tid].audio = 0;
		}
		else if( ret==0 ) {
			if( _config_.memory_mode == MEMORY_MODE_SHARED ) {
				av_packet_add(packet);
			}
		}
		run->audio_sync = start_time;
		if( _config_.memory_mode == MEMORY_MODE_DYNAMIC ) {
			free(data);
		}
	}
	if( (speed == 1) || (speed == 2) ||
			(speed == 4) || (speed == 0) ) {
		run->audio_index++;
	}
	else if( speed==16) {
		run->audio_index+=30;	//2xfps for audio
	}
    return 0;
}

static int player_open_mp4(player_init_t *init, player_run_t *run, player_list_node_t *fhead)
{
    int i, num_tracks;
    int diff;
    unsigned long long int sample_time;
	unsigned char	**sps_header = NULL;
	unsigned char  	**pps_header = NULL;
	unsigned int	*sps_size = NULL;
	unsigned int	*pps_size = NULL;
	char start_str[MAX_SYSTEM_STRING_SIZE], stop_str[MAX_SYSTEM_STRING_SIZE];
	if( run->current == NULL)
		run->current = fhead;
	else
		run->current = run->current->next;
	if( run->mp4_file == NULL ) {
		memset( run->file_path, 0 ,sizeof(run->file_path));
		memset( start_str, 0, sizeof(start_str));
		memset( stop_str, 0, sizeof(stop_str));
		time_stamp_to_date_with_zone( run->current->start, start_str, 80, _config_.timezone);
		time_stamp_to_date_with_zone( run->current->stop, stop_str, 80, _config_.timezone);
		sprintf( run->file_path, "%s%s-%s_%s.mp4",config.profile.path, config.profile.prefix, start_str, stop_str);
	}
	else
		return 0;
    run->mp4_file = MP4Read( run->file_path );
    if ( !run->mp4_file ) {
        log_qcy(DEBUG_SERIOUS, "Read error....%s", run->file_path);
        return -1;
    }
    log_qcy(DEBUG_INFO, "opened file %s", run->file_path);
    if( run->current == fhead ) {
    	run->start = init->start;
    }
    else {
    	run->start = run->current->start;
    }
    if( run->current->next == NULL )
    	run->stop = init->stop;
    else
    	run->stop = 0;
    run->video_track = MP4_INVALID_TRACK_ID;
    num_tracks = MP4GetNumberOfTracks( run->mp4_file, NULL, 0);
    for (i = 0; i < num_tracks; i++) {
    	MP4TrackId id = MP4FindTrackId(run->mp4_file, i, NULL,0);
        char* type = MP4GetTrackType( run->mp4_file, id );
        if( MP4_IS_VIDEO_TRACK_TYPE( type ) ) {
        	run->video_track	= id;
            run->duration = MP4GetTrackDuration( run->mp4_file, id );
            run->video_frame_num = MP4GetTrackNumberOfSamples( run->mp4_file, id );
            run->video_timescale = MP4GetTrackTimeScale( run->mp4_file, id);
            run->fps = MP4GetTrackVideoFrameRate(run->mp4_file, id);
            run->width = MP4GetTrackVideoWidth(run->mp4_file, id);
            run->height = MP4GetTrackVideoHeight(run->mp4_file, id);
            log_qcy(DEBUG_INFO, "video codec = %s", MP4GetTrackMediaDataName(run->mp4_file, id));
            run->video_index = 1;
            //sps pps
			char result = MP4GetTrackH264SeqPictHeaders(run->mp4_file, id, &sps_header, &sps_size,
					&pps_header, &pps_size);
			memset( run->sps, 0, sizeof(run->sps));
			run->slen = 0;
            for (i = 0; sps_size[i] != 0; i++) {
            	if( strlen(run->sps)<=0 && sps_size[i]>0 ) {
            		memcpy( run->sps, sps_header[i], sps_size[i]);
                	run->slen = sps_size[i];
            	}
                free(sps_header[i]);
            }
            free(sps_header);
            free(sps_size);
            memset( run->pps, 0, sizeof(run->pps));
            run->plen = 0;
            for (i = 0; pps_size[i] != 0; i++) {
            	if( strlen(run->pps)<=0 && pps_size[i]>0 ) {
            		memcpy( run->pps, pps_header[i], pps_size[i]);
                	run->plen = pps_size[i];
            	}
                free(pps_header[i]);
            }
			free(pps_header);
			free(pps_size);
        } else if (MP4_IS_AUDIO_TRACK_TYPE( type )) {
			run->audio_track = id;
			run->audio_frame_num = MP4GetTrackNumberOfSamples( run->mp4_file, id);
			run->audio_timescale = MP4GetTrackTimeScale( run->mp4_file, id);
//			run->audio_codec = MP4GetTrackAudioMpeg4Type( run->mp4_file, id);
			log_qcy(DEBUG_INFO, "audio codec = %s", MP4GetTrackMediaDataName(run->mp4_file, id));
			run->audio_index = 1;
        }
    }
    //seek
//    diff = ( run->current->start - run->start ) * 1000;
    diff = ( run->current->start - (run->start + init->offset ) ) * 1000;
    for( i=1;i<=run->video_frame_num;i++) {
        sample_time = MP4GetSampleTime( run->mp4_file, run->video_track, i );
        sample_time = sample_time * 1000 / run->video_timescale;
    	if ( (int)( diff + sample_time ) >= 0 ) {
    		if( i>1 ) run->video_index = i;
    		else run->video_index = 1;
    		break;
    	}
    }
    for( i=1;i<=run->audio_frame_num;i++) {
        sample_time = MP4GetSampleTime( run->mp4_file, run->audio_track, i );
        sample_time = sample_time * 1000 / run->audio_timescale;
    	if ( (int)( diff + sample_time ) >= 0 ) {
    		if( i>1 ) run->audio_index = i;
    		else run->audio_index = 1;
    		break;
    	}
    }
    return 0;
}

static int player_close_mp4(player_run_t *run)
{
	int ret = 0;
	player_list_node_t	*current;
	if( run->mp4_file ) {
		MP4Close( run->mp4_file, 0);
		run->mp4_file = NULL;
		log_qcy(DEBUG_INFO, "------closed file=======%s", run->file_path);
		current = run->current;
		memset( run, 0, sizeof(player_run_t));
		run->current = current;
	}
	return ret;
}

static int player_main(player_init_t *init, player_run_t *run, av_buffer_t *vbuffer, av_buffer_t *abuffer,
		player_list_node_t *fhead, int speed)
{
	int ret = 0;
	int ret_video, ret_audio;
	if( run->mp4_file == NULL ) {
		ret = player_open_mp4(init, run, fhead);
		if( ret )
			goto next_stream;
	}
	if( !run->vstream_wait )
		ret_video = player_get_video_frame(init, run, vbuffer, speed);
	if( !run->astream_wait )
		ret_audio = player_get_audio_frame(init, run, abuffer, speed);
	if( run->i_frame_read ) {
		if( run->video_sync > run->audio_sync + DEFAULT_SYNC_DURATION ) {
			run->vstream_wait = 1;
			run->astream_wait = 0;
		}
		else if( run->audio_sync > run->video_sync + DEFAULT_SYNC_DURATION ) {
			run->astream_wait = 1;
			run->vstream_wait = 0;
		}
		else {
			run->astream_wait = 0;
			run->vstream_wait = 0;
		}
	}
	if( ret_video!=0 || ret_audio!=0 ) {
		run->astream_wait = 0;
		run->vstream_wait = 0;
	}
	if( ret_video && ret_audio ) {
		goto next_stream;
	}
	if( init->speed == 0 )
		usleep(60000);
	else if( init->speed == 1 )
		usleep(30000);
	else if( init->speed == 2 )
		usleep(10000);
	else if( init->speed == 4 )
		usleep(3000);
	else if( init->speed == 16)
		usleep(2000);
	return 0;
next_stream:
	player_close_mp4(run);
	if( run->current->next == NULL ) {
		ret = -1;
	}
	return ret;
}

static int find_job_session(int session)
{
	int i;
	for( i=0; i<MAX_SESSION_NUMBER; i++ ) {
		if( jobs[i].status > PLAYER_THREAD_NONE ) {
			if( jobs[i].init.session_id == session) {
				return i;
			}
		}
	}
	return -1;
}

static int player_add_job( message_t* msg )
{
	message_t send_msg;
	int i=-1, id = -1, num=0;
	int ret = 0, same = 0;
	pthread_t 		pid;
	player_init_t	*init = NULL;
	int file_ret = PLAYER_FILEFOUND;
    /********message body********/
	msg_init(&send_msg);
	memcpy(&(send_msg.arg_pass), &(msg->arg_pass),sizeof(message_arg_t));
	memcpy(&(send_msg.arg_in), &(msg->arg_in),sizeof(message_arg_t));
	send_msg.message = msg->message | 0x1000;
	send_msg.arg_in.tiger = file_ret;
	send_msg.sender = send_msg.receiver = SERVER_PLAYER;
	if( info.status != STATUS_RUN ) goto error;
	/***************************/
	pthread_rwlock_wrlock(&ilock);
	if( config.profile.enable == 0 ) goto error;
	init = ((player_init_t*)msg->arg);
	if( init == NULL) goto error;
	if( player_check_file_list(init->start, init->stop) ) {
		file_ret = PLAYER_FILENOTFOUND;
		goto error;
	}
	id = find_job_session( init->session_id );
	if( id != -1) {
		same = 1;
	}
	else {
		for(i = 0;i<MAX_SESSION_NUMBER;i++) {
			if( jobs[i].status == PLAYER_THREAD_NONE ) {
				id = i;
				break;
			}
		}
	}
	if( id==-1) goto error;
	if( same ) {
		memset( &(jobs[id].init), 0, sizeof(player_init_t));
		memcpy( &(jobs[id].init), init, sizeof(player_init_t));
		jobs[id].init.tid = id;
		misc_set_bit(&info.thread_start, id, 1);
		jobs[id].status = PLAYER_THREAD_INITED;
		jobs[id].audio = init->audio;
		jobs[id].run = 1;
		jobs[id].speed = init->speed;
		jobs[id].restart = 1;
		jobs[id].exit = 0;
		log_qcy(DEBUG_INFO, "player thread updated successful!");
		send_msg.arg_in.cat = 1;
	}
	else {
		//start the thread
		memset( &jobs[id], 0, sizeof(player_job_t));
		memcpy( &(jobs[id].init), init, sizeof(player_init_t));
		jobs[id].init.tid = id;
		ret = pthread_create(&pid, NULL, player_func, (void*)&(jobs[id].init));
		if(ret != 0) {
			log_qcy(DEBUG_SERIOUS, "player thread create error! ret = %d",ret);
			goto error;
		}
		misc_set_bit(&info.thread_start, id, 1);
		jobs[id].status = PLAYER_THREAD_INITED;
		jobs[id].session = init->session_id;
		jobs[id].sid = id;
		jobs[id].audio = init->audio;
		jobs[id].run = 0;
		jobs[id].speed = init->speed;
		jobs[id].restart = 0;
		jobs[id].exit = 0;
		log_qcy(DEBUG_INFO, "player thread create successful!");
		send_msg.arg_in.cat = 0;
	}
	pthread_rwlock_unlock(&ilock);
	send_msg.result = 0;
	ret = manager_common_send_message(msg->receiver, &send_msg);
	return ret;
error:
	pthread_rwlock_unlock(&ilock);
	send_msg.result = -1;
	send_msg.arg_in.tiger = file_ret;
	ret = manager_common_send_message(msg->receiver, &send_msg);
	return -1;
}

static int player_start_job( message_t* msg )
{
	message_t send_msg;
	int i=-1, id = -1;
	int ret = 0;
    /********message body********/
	msg_init(&send_msg);
	memcpy(&(send_msg.arg_pass), &(msg->arg_pass),sizeof(message_arg_t));
	memcpy(&(send_msg.arg_in), &(msg->arg_in),sizeof(message_arg_t));
	send_msg.message = msg->message | 0x1000;
	send_msg.sender = send_msg.receiver = SERVER_PLAYER;
	/***************************/
	if( config.profile.enable == 0 ) goto error;
	id = find_job_session( msg->arg_in.wolf );
	if( id==-1 ) goto error;
	pthread_rwlock_wrlock(&ilock);
	jobs[id].run = 1;
	pthread_rwlock_unlock(&ilock);
	send_msg.result = 0;
	ret = manager_common_send_message(msg->receiver, &send_msg);
	return ret;
error:
	send_msg.result = -1;
	ret = manager_common_send_message(msg->receiver, &send_msg);
	return -1;
}

static int player_stop_job( message_t* msg )
{
	message_t send_msg;
	int i=-1, id = -1;
	int ret = 0;
    /********message body********/
	msg_init(&send_msg);
	memcpy(&(send_msg.arg_pass), &(msg->arg_pass),sizeof(message_arg_t));
	memcpy(&(send_msg.arg_in), &(msg->arg_in),sizeof(message_arg_t));
	send_msg.message = msg->message | 0x1000;
	send_msg.sender = send_msg.receiver = SERVER_PLAYER;
	/***************************/
	if( config.profile.enable == 0 ) goto error;
	id = find_job_session( msg->arg_in.wolf );
	if( id==-1 ) goto error;
	pthread_rwlock_wrlock(&ilock);
	jobs[id].exit = 1;
	pthread_rwlock_unlock(&ilock);
	send_msg.result = 0;
	ret = manager_common_send_message(msg->receiver, &send_msg);
	return ret;
error:
	send_msg.result = -1;
	ret = manager_common_send_message(msg->receiver, &send_msg);
	return -1;
}

static int player_start_audio( message_t* msg )
{
	message_t send_msg;
	int i=-1, id = -1;
	int ret = 0;
    /********message body********/
	msg_init(&send_msg);
	memcpy(&(send_msg.arg_pass), &(msg->arg_pass),sizeof(message_arg_t));
	memcpy(&(send_msg.arg_in), &(msg->arg_in),sizeof(message_arg_t));
	send_msg.message = msg->message | 0x1000;
	send_msg.sender = send_msg.receiver = SERVER_PLAYER;
	/***************************/
	if( config.profile.enable == 0 ) goto error;
	id = find_job_session( msg->arg_in.wolf );
	if( id==-1 ) goto error;
	if( jobs[id].status== PLAYER_THREAD_NONE) goto error;
	pthread_rwlock_wrlock(&ilock);
	jobs[id].audio = 1;
	pthread_rwlock_unlock(&ilock);
	send_msg.result = 0;
	ret = manager_common_send_message(msg->receiver, &send_msg);
	return ret;
error:
	send_msg.result = -1;
	ret = manager_common_send_message(msg->receiver, &send_msg);
	return -1;
}

static int player_stop_audio( message_t* msg )
{
	message_t send_msg;
	int i=-1, id = -1;
	int ret = 0;
    /********message body********/
	msg_init(&send_msg);
	memcpy(&(send_msg.arg_pass), &(msg->arg_pass),sizeof(message_arg_t));
	memcpy(&(send_msg.arg_in), &(msg->arg_in),sizeof(message_arg_t));
	send_msg.message = msg->message | 0x1000;
	send_msg.sender = send_msg.receiver = SERVER_PLAYER;
	/***************************/
	if( config.profile.enable == 0 ) goto error;
	id = find_job_session( msg->arg_in.wolf );
	if( id==-1 ) goto error;
	if( jobs[id].status == PLAYER_THREAD_NONE) goto error;
	pthread_rwlock_wrlock(&ilock);
	jobs[id].audio = 0;
	pthread_rwlock_unlock(&ilock);
	send_msg.result = 0;
	ret = manager_common_send_message(msg->receiver, &send_msg);
	return ret;
error:
	send_msg.result = -1;
	ret = manager_common_send_message(msg->receiver, &send_msg);
	return -1;
}

static int player_thread_destroy( int tid, player_run_t *run )
{
	int ret=0, ret1;
	if( run->mp4_file ) {
		MP4Close( run->mp4_file, 0);
		run->mp4_file = NULL;
		log_qcy(DEBUG_INFO, "------closed file=======%s", run->file_path);
	}
	pthread_rwlock_wrlock(&ilock);
	misc_set_bit( &info.thread_start, tid, 0);
	memset(&jobs[tid], 0, sizeof(player_job_t));
	pthread_rwlock_unlock(&ilock);
	return ret;
}

static int *player_func(void* arg)
{
	int tid;
	player_run_t 	run;
	char fname[MAX_SYSTEM_STRING_SIZE];
	message_t msg;
	player_list_node_t	*fhead = NULL;
	player_init_t	init;
	int	status, send_finish = 0;
	int ret, st, play, restart, speed;
	pthread_detach(pthread_self());
	pthread_rwlock_wrlock(&ilock);
	memset(&init, 0, sizeof(player_init_t));
	memcpy(&init, (player_init_t*)(arg), sizeof(player_init_t));
	tid = init.tid;
    signal(SIGINT, server_thread_termination);
    signal(SIGTERM, server_thread_termination);
    sprintf(fname, "%d%d-%d",tid,init.session_id, time_get_now_stamp());
    misc_set_thread_name(fname);
	log_qcy(DEBUG_INFO, "------------add new player----------------");
	log_qcy(DEBUG_INFO, "now=%ld", time_get_now_stamp());
	log_qcy(DEBUG_INFO, "start=%ld", init.start);
	log_qcy(DEBUG_INFO, "end=%ld", init.stop);
	log_qcy(DEBUG_INFO, "--------------------------------------------------");
	memset(&run, 0, sizeof(player_run_t));
	av_buffer_init(&pvbuffer[tid], &pvlock[tid]);
	av_buffer_init(&pabuffer[tid], &palock[tid]);
	misc_set_bit( &info.thread_start, tid, 1);
	pthread_rwlock_unlock(&ilock);
	status = PLAYER_THREAD_INITED;
    while( 1 ) {
    	pthread_rwlock_wrlock(&ilock);
    	if( info.exit || jobs[tid].exit ) {
    		pthread_rwlock_unlock(&ilock);
    		break;
    	}
    	if( misc_get_bit(info.thread_exit, tid) ) {
    		pthread_rwlock_unlock(&ilock);
    		break;
    	}
    	play = jobs[tid].run;
    	restart = jobs[tid].restart;
    	speed = jobs[tid].speed;
    	if(restart) {
    		memcpy(&init, (player_init_t*)&(jobs[tid].init), sizeof(player_init_t));
    		if( status >= PLAYER_THREAD_RUN) {
    			player_close_mp4(&run);
    			memset(&run, 0 ,sizeof(run));
    			player_node_clear(&fhead);
    		    av_buffer_release(&pvbuffer[tid]);
    		    av_buffer_release(&pabuffer[tid]);
        		av_buffer_init(&pvbuffer[tid], &pvlock[tid]);
        		av_buffer_init(&pabuffer[tid], &palock[tid]);
    		}
    		status = PLAYER_THREAD_INITED;
    		jobs[tid].restart = 0;
    	}
    	pthread_rwlock_unlock(&ilock);
    	switch( status ) {
			case PLAYER_THREAD_INITED:
				if( player_search_file_list(init.start, init.stop, &fhead) != 0)
					status = PLAYER_THREAD_ERROR;
				else {
					status = PLAYER_THREAD_IDLE;
					log_qcy(DEBUG_INFO, "player initialized!");
				}
				break;
			case PLAYER_THREAD_IDLE:
				if(play) {
					status = PLAYER_THREAD_RUN;
					log_qcy(DEBUG_INFO, "player started!");
				}
				break;
    		case PLAYER_THREAD_RUN:
    			if( player_main(&init, &run, &pvbuffer[tid], &pabuffer[tid], fhead, speed) ) {
    				status = PLAYER_THREAD_FINISH;
    				log_qcy(DEBUG_INFO, "player finished!");
    			}
    			if( !play ) {
    				status = PLAYER_THREAD_STOP;
    				log_qcy(DEBUG_INFO, "player paused!");
    			}
    			break;
    		case PLAYER_THREAD_STOP:
				if(play)
					status = PLAYER_THREAD_RUN;
    			break;
    		case PLAYER_THREAD_PAUSE:
    			break;
    		case PLAYER_THREAD_FINISH:
    			log_qcy(DEBUG_INFO, "finished the playback for thread = %d", tid);
    			info.status = PLAYER_THREAD_INITED;
    			send_finish = 1;
    	    	pthread_rwlock_wrlock(&ilock);
    	    	jobs[tid].exit = 1;
   	    		pthread_rwlock_unlock(&ilock);
    			break;
    		case PLAYER_THREAD_ERROR:
    			log_qcy(DEBUG_SERIOUS, "error within thread = %d", tid);
    			info.status = PLAYER_THREAD_INITED;
    			send_finish = 1;
    	    	pthread_rwlock_wrlock(&ilock);
    	    	jobs[tid].exit = 1;
   	    		pthread_rwlock_unlock(&ilock);
    			break;
    	}
    }
    if( send_finish ) {
		/********message body********/
		msg_init(&msg);
		msg.message = MSG_PLAYER_FINISH;
		msg.sender = msg.receiver = SERVER_PLAYER;
		msg.arg_in.cat = init.switch_to_live;
		msg.arg_in.dog = init.switch_to_live_audio;
		msg.arg_pass.cat = MISS_ASYN_PLAYER_FINISH;
		msg.arg_pass.wolf = init.session_id;
		msg.arg_pass.handler = init.session;
		ret = manager_common_send_message(SERVER_MISS,   &msg);
		/****************************/
    }
    //release
    player_node_clear(&fhead);
    av_buffer_release(&pvbuffer[tid]);
    av_buffer_release(&pabuffer[tid]);
    player_thread_destroy(tid, &run);
    manager_common_send_dummy(SERVER_PLAYER);
    log_qcy(DEBUG_INFO, "-----------thread exit: player %s-----------", fname);
    pthread_exit(0);
}

static void server_thread_termination(void)
{
    /********message body********/
	message_t msg;
	msg_init(&msg);
	msg.message = MSG_PLAYER_SIGINT;
	msg.sender = msg.receiver = SERVER_PLAYER;
	manager_common_send_message(SERVER_MANAGER, &msg);
	/****************************/
}

static void player_broadcast_thread_exit(void)
{
}

static void server_release_1(void)
{
}

static void server_release_2(void)
{
	msg_buffer_release2(&message, &mutex);
	memset(&config,0,sizeof(player_config_t));
	memset(&jobs, 0, sizeof(jobs));
	memset(&flist,0,sizeof(player_file_list_t));
}

static void server_release_3(void)
{
	msg_free(&info.task.msg);
	memset(&info, 0, sizeof(server_info_t));
}

/*
 *
 */
static int player_message_filter(message_t  *msg)
{
	int ret = 0;
	if( info.task.func == task_exit) { //only system message
		if( !msg_is_system(msg->message) && !msg_is_response(msg->message) )
			return 1;
	}
	return ret;
}
static int server_message_proc(void)
{
	int ret = 0;
	message_t msg, send_msg;
	char	name[MAX_SYSTEM_STRING_SIZE];
	//condition
	pthread_mutex_lock(&mutex);
	if( message.head == message.tail ) {
		if( (info.status == info.old_status ) ) {
			pthread_cond_wait(&cond,&mutex);
		}
	}
	if( info.msg_lock ) {
		pthread_mutex_unlock(&mutex);
		return 0;
	}
	msg_init(&msg);
	ret = msg_buffer_pop(&message, &msg);
	pthread_mutex_unlock(&mutex);
	if( ret == 1)
		return 0;
	if( player_message_filter(&msg) ) {
		msg_free(&msg);
		log_qcy(DEBUG_VERBOSE, "PLAYER message--- sender=%d, message=%x, ret=%d, head=%d, tail=%d was screened, the current task is %p", msg.sender, msg.message,
				ret, message.head, message.tail, info.task.func);
		return -1;
	}
	msg_init(&send_msg);
	log_qcy(DEBUG_VERBOSE, "-----pop out from the PLAYER message queue: sender=%d, message=%x, ret=%d, head=%d, tail=%d", msg.sender, msg.message,
			ret, message.head, message.tail);
	switch(msg.message) {
		case MSG_PLAYER_REQUEST:
			msg_init(&info.task.msg);
			msg_deep_copy(&info.task.msg, &msg);
//			memcpy(&info.task.msg.arg_pass, &msg.arg_pass, sizeof(message_arg_t));
			player_add_job(&msg);
			break;
		case MSG_PLAYER_START:
			player_start_job(&msg);
			break;
		case MSG_PLAYER_STOP:
			player_stop_job(&msg);
			break;
		case MSG_PLAYER_AUDIO_START:
			player_start_audio(&msg);
			break;
		case MSG_PLAYER_AUDIO_STOP:
			player_stop_audio(&msg);
			break;
		case MSG_MANAGER_EXIT:
			msg_init(&info.task.msg);
			msg_copy(&info.task.msg, &msg);
			info.task.func = task_exit;
			info.status = EXIT_INIT;
			info.msg_lock = 0;
			break;
		case MSG_MANAGER_TIMER_ACK:
			((HANDLER)msg.arg_in.handler)();
			break;
		case MSG_MIIO_PROPERTY_NOTIFY:
		case MSG_MIIO_PROPERTY_GET_ACK:
			if( msg.arg_in.cat == MIIO_PROPERTY_TIME_SYNC ) {
				if( msg.arg_in.dog == 1 )
					misc_set_bit( &info.init_status, PLAYER_INIT_CONDITION_MIIO_TIME, 1);
			}
			break;
		case MSG_RECORDER_PROPERTY_GET_ACK:
			if( !msg.result ) {
				if( msg.arg_in.cat == RECORDER_PROPERTY_NORMAL_DIRECTORY ) {
					strcpy(config.profile.path, msg.arg);
					strcpy(config.profile.prefix, msg.extra);
					misc_set_bit( &info.init_status, PLAYER_INIT_CONDITION_RECORDER_CONFIG, 1);
				}
			}
			break;
		case MSG_RECORDER_ADD_FILE:
			if( misc_get_bit( info.init_status, PLAYER_INIT_CONDITION_FILE_LIST) ) {
				pthread_rwlock_wrlock(&ilock);
				if( flist.num >= MAX_FILE_NUM )
					break;
	        	memcpy( &flist.start[flist.num], (unsigned long long*)msg.arg, sizeof(flist.start[flist.num]));
	        	memcpy( &flist.stop[flist.num], (unsigned long long*)msg.extra, sizeof(flist.stop[flist.num]));
	        	flist.num++;
	        	pthread_rwlock_unlock(&ilock);
			}
			break;
		case MSG_PLAYER_GET_FILE_LIST:
			ret = player_get_file_list(&msg);
			break;
		case MSG_PLAYER_GET_FILE_DATE:
			ret = player_get_file_date(&msg);
			break;
		case MSG_PLAYER_GET_PICTURE_LIST:
			ret = player_get_picture_list(&msg);
			break;
		case MSG_PLAYER_PROPERTY_SET:
			ret = player_set_property(&msg);
			break;
		case MSG_MANAGER_EXIT_ACK:
			misc_set_bit(&info.error, msg.sender, 0);
			break;
		case MSG_PLAYER_RELAY:
			msg_init(&send_msg);
			send_msg.message = msg.message | 0x1000;
			send_msg.sender = send_msg.receiver = SERVER_PLAYER;
			memcpy(&send_msg.arg_pass, &msg.arg_pass, sizeof(message_arg_t));
			memcpy(&send_msg.arg_in, &msg.arg_in, sizeof(message_arg_t));
			manager_common_send_message(msg.receiver, &send_msg);
			break;
		case MSG_DEVICE_GET_PARA_ACK: {
			device_iot_config_t dev_iot;
			if( !msg.result ) {
				memcpy(&dev_iot, msg.arg, sizeof(device_iot_config_t));
				if( dev_iot.sd_iot_info.plug == SD_STATUS_PLUG ) {
					misc_set_bit( &info.init_status, PLAYER_INIT_CONDITION_DEVICE_SD, 1);
					hotplug = 0;
				}
			}
			break;
		}
		case MSG_MANAGER_DUMMY:
			break;
		case MSG_DEVICE_ACTION:
			if(msg.arg_in.cat == DEVICE_ACTION_SD_CAP_ALARM) {
				break;
			}
			else if(msg.arg_in.cat == DEVICE_ACTION_SD_EJECTED) {
				player_quit_all(-1);
				misc_set_bit( &info.init_status, PLAYER_INIT_CONDITION_DEVICE_SD, 0);
				misc_set_bit( &info.init_status, PLAYER_INIT_CONDITION_FILE_LIST, 0);
				info.status = STATUS_NONE;
				info.tick = 10;
				if(msg.arg_in.wolf) {
					send_msg.sender = send_msg.receiver = SERVER_PLAYER;
					send_msg.message = MSG_DEVICE_ACTION;
					send_msg.arg_in.cat = DEVICE_ACTION_SD_EJECTED_ACK;
					memcpy(&(send_msg.arg_pass), &(msg.arg_pass),sizeof(message_arg_t));
					server_device_message(&send_msg);
				}
			}
			else if(msg.arg_in.cat == DEVICE_ACTION_SD_INSERT) {
				info.tick = 0;	//restart the device check
				hotplug = 0;
			}
			break;
		case MSG_RECORDER_CLEAN_DISK_START:
			player_quit_all(-1);
			misc_set_bit( &info.init_status, PLAYER_INIT_CONDITION_DEVICE_SD, 0);
			misc_set_bit( &info.init_status, PLAYER_INIT_CONDITION_FILE_LIST, 0);
			info.status = STATUS_NONE;
			info.tick = 10;//no-request to device
			break;
		case MSG_RECORDER_CLEAN_DISK_STOP:
			info.tick = 0;	//restart the device check
			misc_set_bit( &info.init_status, PLAYER_INIT_CONDITION_DEVICE_SD, 1); //fake device response
			hotplug = 0;
			break;
		case MSG_KERNEL_TIMEZONE_CHANGE:
			player_quit_all(-1);
			misc_set_bit( &info.init_status, PLAYER_INIT_CONDITION_FILE_LIST, 0);
			info.status = STATUS_NONE;
			info.tick = 10;//no-request to device
			break;
		default:
			log_qcy(DEBUG_SERIOUS, "not processed message = %x", msg.message);
			break;
	}
	msg_free(&msg);
	return ret;
}

/*
 *
 */
static int server_none(void)
{
	int ret = 0;
	message_t msg;
	if( !misc_get_bit( info.init_status, PLAYER_INIT_CONDITION_CONFIG ) ) {
		ret = config_player_read(&config);
		if( !ret && misc_full_bit(config.status, CONFIG_PLAYER_MODULE_NUM) ) {
			misc_set_bit(&info.init_status, PLAYER_INIT_CONDITION_CONFIG, 1);
		}
		else {
			info.status = STATUS_ERROR;
			return -1;
		}
	}
	if( !misc_get_bit( info.init_status, PLAYER_INIT_CONDITION_RECORDER_CONFIG)) {
	    /********message body********/
		msg_init(&msg);
		msg.message = MSG_RECORDER_PROPERTY_GET;
		msg.sender = msg.receiver = SERVER_PLAYER;
		msg.arg_in.cat = RECORDER_PROPERTY_NORMAL_DIRECTORY;
		ret = manager_common_send_message(SERVER_RECORDER,    &msg);
		/***************************/
		usleep(MESSAGE_RESENT_SLEEP);
	}
	if( !misc_get_bit( info.init_status, PLAYER_INIT_CONDITION_DEVICE_SD)) {
		if( info.tick < MESSAGE_RESENT ) {
			/********message body********/
			msg_init(&msg);
			msg.message = MSG_DEVICE_GET_PARA;
			msg.sender = msg.receiver = SERVER_PLAYER;
			msg.arg_in.cat = DEVICE_CTRL_SD_INFO;
			ret = manager_common_send_message(SERVER_DEVICE, &msg);
			/***************************/
			info.tick++;
		}
		usleep(MESSAGE_RESENT_SLEEP);
	}
	if( !misc_get_bit( info.init_status, PLAYER_INIT_CONDITION_MIIO_TIME)) {
		/********message body********/
		msg_init(&msg);
		msg.message = MSG_MIIO_PROPERTY_GET;
		msg.sender = msg.receiver = SERVER_PLAYER;
		msg.arg_in.cat = MIIO_PROPERTY_TIME_SYNC;
		ret = manager_common_send_message(SERVER_MIIO, &msg);
		/***************************/
		usleep(MESSAGE_RESENT_SLEEP);
	}
	if( misc_get_bit( info.init_status, PLAYER_INIT_CONDITION_DEVICE_SD) &&
		misc_get_bit( info.init_status, PLAYER_INIT_CONDITION_RECORDER_CONFIG)	) {
		if(!misc_get_bit( info.init_status, PLAYER_INIT_CONDITION_FILE_LIST)) {
			if( !player_read_file_list( config.profile.path) ) {
				misc_set_bit( &info.init_status, PLAYER_INIT_CONDITION_FILE_LIST, 1);
			}
		}
	}
	if( misc_full_bit( info.init_status, PLAYER_INIT_CONDITION_NUM ) ) {
		info.status = STATUS_WAIT;
		info.tick = 0;
	}
	return ret;
}
/*
 * task
 */
/*
 * default exit: *->exit
 */
static void task_exit(void)
{
	switch( info.status ){
		case EXIT_INIT:
			log_qcy(DEBUG_INFO,"PLAYER: switch to exit task!");
			if( info.task.msg.sender == SERVER_MANAGER) {
				info.error = PLAYER_EXIT_CONDITION;
				info.error &= (info.task.msg.arg_in.cat);
			}
			else {
				info.error = 0;
			}
			info.status = EXIT_SERVER;
			break;
		case EXIT_SERVER:
			if( !info.error )
				info.status = EXIT_STAGE1;
			break;
		case EXIT_STAGE1:
			server_release_1();
			info.status = EXIT_THREAD;
			break;
		case EXIT_THREAD:
			info.thread_exit = info.thread_start;
			player_broadcast_thread_exit();
			if( !info.thread_start )
				info.status = EXIT_STAGE2;
			break;
			break;
		case EXIT_STAGE2:
			server_release_2();
			info.status = EXIT_FINISH;
			break;
		case EXIT_FINISH:
			info.exit = 1;
		    /********message body********/
			message_t msg;
			msg_init(&msg);
			msg.message = MSG_MANAGER_EXIT_ACK;
			msg.sender = SERVER_PLAYER;
			manager_common_send_message(SERVER_MANAGER, &msg);
			/***************************/
			info.status = STATUS_NONE;
			break;
		default:
			log_qcy(DEBUG_SERIOUS, "!!!!!!!unprocessed server status in task_exit = %d", info.status);
			break;
		}
	return;
}


/*
 * default task: none->run
 */
static void task_default(void)
{
	switch( info.status){
		case STATUS_NONE:
			server_none();
			break;
		case STATUS_WAIT:
			info.status = STATUS_SETUP;
			break;
		case STATUS_SETUP:
			info.status = STATUS_IDLE;
			break;
		case STATUS_IDLE:
			info.status = STATUS_START;
			break;
		case STATUS_START:
			info.status = STATUS_RUN;
			break;
		case STATUS_RUN:
			break;
		case STATUS_ERROR:
			info.task.func = task_exit;
			info.status = EXIT_INIT;
			info.msg_lock = 0;
			break;
		default:
			log_qcy(DEBUG_SERIOUS, "!!!!!!!unprocessed server status in task_default = %d", info.status);
			break;
		}
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
	msg_buffer_init2(&message, _config_.msg_overrun, &mutex);
	info.init = 1;
	//default task
	info.task.func = task_default;
	while( !info.exit ) {
		info.old_status = info.status;
		info.task.func();
		server_message_proc();
	}
	server_release_3();
	log_qcy(DEBUG_INFO, "-----------thread exit: server_player-----------");
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
		log_qcy(DEBUG_INFO, "player server create successful!");
		return 0;
	}
}

int server_player_message(message_t *msg)
{
	int ret=0;
	pthread_mutex_lock(&mutex);
	if( !message.init ) {
		log_qcy(DEBUG_SERIOUS, "player server is not ready for message processing!");
		pthread_mutex_unlock(&mutex);
		return -1;
	}
	ret = msg_buffer_push(&message, msg);
	log_qcy(DEBUG_VERBOSE, "push into the player message queue: sender=%d, message=%x, ret=%d, head=%d, tail=%d", msg->sender, msg->message, ret,
			message.head, message.tail);
	if( ret!=0 )
		log_qcy(DEBUG_WARNING, "message push in player error =%d", ret);
	else {
		pthread_cond_signal(&cond);
	}
	pthread_mutex_unlock(&mutex);
	return ret;
}

void server_player_interrupt_routine(int param)
{
	if( param == 1) {
		info.msg_lock = 0;
		hotplug = 1;
		info.tick = 0;
		misc_set_bit( &info.init_status, PLAYER_INIT_CONDITION_DEVICE_SD, 0);
		player_quit_all(-1);
		info.status = STATUS_NONE;
		pthread_mutex_lock(&mutex);
		pthread_cond_signal(&cond);
		pthread_mutex_unlock(&mutex);
		log_qcy(DEBUG_SERIOUS, "PLAYER: hotplug happened, player roll back to none state--------------");
	}
}
