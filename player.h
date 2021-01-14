/*
 * player.h
 *
 *  Created on: Oct 6, 2020
 *      Author: ning
 */

#ifndef SERVER_PLAYER_PLAYER_H_
#define SERVER_PLAYER_PLAYER_H_

/*
 * header
 */

/*
 * define
 */
//#define PLAYBACK_RESP_FILEFOUND_TEMPLATE "{\"id\":%d, \"status\":\"filefound\", \"starttime\":%d, \"duration\":%d}"
//#define PLAYBACK_RESP_FILENOTFOUND_TEMPLATE "{\"id\":%d, \"status\":\"filenotfound\"}"
//#define PLAYBACK_RESP_ENDOFFILE_TEMPLATE "{\"id\":%d, \"status\":\"endoffile\"}"
//#define PLAYBACK_RESP_FILEREADERROR_TEMPLATE "{\"id\":%d, \"status\":\"readerror\", \"starttime\":%d}"
//#define PLAYBACK_RESP_ERRREQUEST_TEMPLATE "{\"id\":%d, \"status\":\"errorrequest\", \"starttime\":%d}"

#define		MAX_FILE_NUM							1440*7*2
#define		PLAYER_INIT_CONDITION_NUM				5
#define		PLAYER_INIT_CONDITION_CONFIG			0
#define		PLAYER_INIT_CONDITION_MIIO_TIME			1
#define		PLAYER_INIT_CONDITION_FILE_LIST			2
#define		PLAYER_INIT_CONDITION_RECORDER_CONFIG	3
#define		PLAYER_INIT_CONDITION_DEVICE_SD			4

#define		DEFAULT_SYNC_DURATION					67			//67ms

#define		PLAYER_EXIT_CONDITION					( (1 << SERVER_MISS) )

#define		PLAYER_MAX_FAILED_SEND					15

typedef enum {
	PLAYER_THREAD_NONE = 0,
	PLAYER_THREAD_INITED,
	PLAYER_THREAD_IDLE,
	PLAYER_THREAD_RUN,
	PLAYER_THREAD_STOP,
	PLAYER_THREAD_PAUSE,
	PLAYER_THREAD_FINISH,
	PLAYER_THREAD_ERROR,
};

/*
 * structure
 */
typedef struct player_list_node_t {
    unsigned int					start;
    unsigned int					stop;
    struct player_list_node_t*    	next;
} player_list_node_t;

typedef struct player_file_list_t {
	unsigned long long start[MAX_FILE_NUM];
	unsigned long long stop[MAX_FILE_NUM];
	unsigned int	num;
	unsigned int	begin;
	unsigned int	end;
} player_file_list_t;

typedef struct player_run_t {
	player_list_node_t	*current;
	char   				file_path[MAX_SYSTEM_STRING_SIZE*2];
	MP4FileHandle 		mp4_file;
	MP4TrackId 			video_track;
	MP4TrackId 			audio_track;
	int					video_frame_num;
	int					audio_frame_num;
	int					video_timescale;
	int					audio_timescale;
	int					video_index;
	int					audio_index;
	int					video_codec;
	int					audio_codec;
	int					duration;
    int 				slen;
    unsigned char 		sps[MAX_SYSTEM_STRING_SIZE*4];
    int 				plen;
    unsigned char 		pps[MAX_SYSTEM_STRING_SIZE*4];
    char				vstream_wait;
    char				astream_wait;
    unsigned long long	video_sync;
    unsigned long long	audio_sync;
	unsigned long long 	start;
	unsigned long long 	stop;
	char				i_frame_read;
    int					fps;
    int					width;
    int					height;
	av_qos_t 			qos;
} player_run_t;

typedef struct player_job_t {
	//shared
	char				status;
	char				run;
	char				speed;
	char				restart;
	char				exit;
	char				audio;
	//non-shared
	char				session;
	char				sid;
	player_init_t		init;
} player_job_t;
/*
 * function
 */

#endif /* SERVER_PLAYER_PLAYER_H_ */
