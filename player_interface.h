/*
 * player_interface.h
 *
 *  Created on: Oct 6, 2020
 *      Author: ning
 */

#ifndef SERVER_PLAYER_PLAYER_INTERFACE_H_
#define SERVER_PLAYER_PLAYER_INTERFACE_H_

/*
 * header
 */
#include <mp4v2/mp4v2.h>
#include <pthread.h>
#include "../../manager/global_interface.h"
#include "../../manager/manager_interface.h"
#include "config.h"

/*
 * define
 */
#define		SERVER_PLAYER_VERSION_STRING		"alpha-3.2"

#define		MSG_PLAYER_BASE						(SERVER_PLAYER<<16)
#define		MSG_PLAYER_SIGINT						MSG_PLAYER_BASE | 0x0000
#define		MSG_PLAYER_SIGINT_ACK					MSG_PLAYER_BASE | 0x1000
#define		MSG_PLAYER_START						MSG_PLAYER_BASE | 0x0010
#define		MSG_PLAYER_START_ACK					MSG_PLAYER_BASE | 0x1010
#define		MSG_PLAYER_STOP						MSG_PLAYER_BASE | 0x0011
#define		MSG_PLAYER_STOP_ACK					MSG_PLAYER_BASE | 0x1011
#define		MSG_PLAYER_GET_PARA					MSG_PLAYER_BASE | 0x0012
#define		MSG_PLAYER_GET_PARA_ACK				MSG_PLAYER_BASE | 0x1012
#define		MSG_PLAYER_SET_PARA					MSG_PLAYER_BASE | 0x0013
#define		MSG_PLAYER_SET_PARA_ACK				MSG_PLAYER_BASE | 0x1013
#define		MSG_PLAYER_CTRL_DIRECT				MSG_PLAYER_BASE | 0x0014
#define		MSG_PLAYER_CTRL_DIRECT_ACK			MSG_PLAYER_BASE | 0x1014
#define		MSG_PLAYER_ADD						MSG_PLAYER_BASE | 0x0015
#define		MSG_PLAYER_ADD_ACK					MSG_PLAYER_BASE | 0x1015
#define		MSG_PLAYER_VIDEO_DATA					MSG_PLAYER_BASE | 0x0100
#define		MSG_PLAYER_AUDIO_DATA					MSG_PLAYER_BASE | 0x0101

#define		PLAYER_AUDIO_YES						0x00
#define		PLAYER_AUDIO_NO						0x01

#define		PLAYER_QUALITY_LOW					0x00
#define		PLAYER_QUALITY_MEDIUM					0x01
#define		PLAYER_QUALITY_HIGH					0x02

#define		PLAYER_TYPE_NORMAL					0x00
#define		PLAYER_TYPE_MOTION_DETECTION			0x01
#define		PLAYER_TYPE_ALARM						0x02

#define		PLAYER_MODE_BY_TIME					0x00
#define		PLAYER_MODE_BY_SIZE					0x01

#define		MAX_PLAYER_JOB						3

//control command
#define		PLAYER_CTRL_LOCAL_SAVE				0x0000
#define		PLAYER_CTRL_RECORDING_MODE			0x0001
/*
 * structure
 */
typedef enum {
	PLAYER_THREAD_NONE = 0,
	PLAYER_THREAD_INITED,
	PLAYER_THREAD_STARTED,
	PLAYER_THREAD_RUN,
	PLAYER_THREAD_PAUSE,
	PLAYER_THREAD_ERROR,
};

typedef struct player_init_t {
	int		type;
	int		mode;
	int		repeat;
	int		repeat_interval;
    int		audio;
    int		quality;
    char   	start[MAX_SYSTEM_STRING_SIZE];
    char   	stop[MAX_SYSTEM_STRING_SIZE];
    HANDLER	func;
} player_init_t;

typedef struct player_run_t {
	char   				file_path[MAX_SYSTEM_STRING_SIZE*2];
	pthread_rwlock_t 	lock;
	pthread_t 			pid;
	MP4FileHandle 		mp4_file;
	MP4TrackId 			video_track;
	MP4TrackId 			audio_track;
    FILE    			*file;
	unsigned long long 	start;
	unsigned long long 	stop;
	unsigned long long 	real_start;
	unsigned long long 	real_stop;
	unsigned long long	last_write;
	char				i_frame_read;
    int					fps;
    int					width;
    int					height;
    char				exit;
} player_run_t;

typedef struct player_job_t {
	char				status;
	char				t_id;
	player_init_t		init;
	player_run_t		run;
	player_config_t 	config;
} player_job_t;

typedef struct player_iot_config_t {
	int 	local_save;
	int		recording_mode;
} player_iot_config_t;

/*
 * function
 */
int server_player_start(void);
int server_player_message(message_t *msg);
int server_player_video_message(message_t *msg);
int server_player_audio_message(message_t *msg);

#endif /* SERVER_PLAYER_PLAYER_INTERFACE_H_ */
