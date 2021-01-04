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
#define		SERVER_PLAYER_VERSION_STRING		"alpha-5.3"

#define		MSG_PLAYER_BASE						(SERVER_PLAYER<<16)
#define		MSG_PLAYER_SIGINT					(MSG_PLAYER_BASE | 0x0000)
#define		MSG_PLAYER_SIGINT_ACK				(MSG_PLAYER_BASE | 0x1000)
#define		MSG_PLAYER_START					(MSG_PLAYER_BASE | 0x0010)
#define		MSG_PLAYER_START_ACK				(MSG_PLAYER_BASE | 0x1010)
#define		MSG_PLAYER_STOP						(MSG_PLAYER_BASE | 0x0011)
#define		MSG_PLAYER_STOP_ACK					(MSG_PLAYER_BASE | 0x1011)
#define		MSG_PLAYER_GET_FILE_LIST			(MSG_PLAYER_BASE | 0x0012)
#define		MSG_PLAYER_GET_FILE_LIST_ACK		(MSG_PLAYER_BASE | 0x1012)
#define		MSG_PLAYER_GET_FILE_DATE			(MSG_PLAYER_BASE | 0x0013)
#define		MSG_PLAYER_GET_FILE_DATE_ACK		(MSG_PLAYER_BASE | 0x1013)
#define		MSG_PLAYER_PROPERTY_SET				(MSG_PLAYER_BASE | 0x0014)
#define		MSG_PLAYER_PROPERTY_SET_ACK			(MSG_PLAYER_BASE | 0x1014)
#define		MSG_PLAYER_AUDIO_START				(MSG_PLAYER_BASE | 0x0015)
#define		MSG_PLAYER_AUDIO_START_ACK			(MSG_PLAYER_BASE | 0x1015)
#define		MSG_PLAYER_AUDIO_STOP				(MSG_PLAYER_BASE | 0x0016)
#define		MSG_PLAYER_AUDIO_STOP_ACK			(MSG_PLAYER_BASE | 0x1016)
#define		MSG_PLAYER_REQUEST					(MSG_PLAYER_BASE | 0x0017)
#define		MSG_PLAYER_REQUEST_ACK				(MSG_PLAYER_BASE | 0x1017)
#define		MSG_PLAYER_RELAY					(MSG_PLAYER_BASE | 0x0018)
#define		MSG_PLAYER_RELAY_ACK				(MSG_PLAYER_BASE | 0x1018)
#define		MSG_PLAYER_FINISH					(MSG_PLAYER_BASE | 0x0019)
#define		MSG_PLAYER_GET_PICTURE_LIST			(MSG_PLAYER_BASE | 0x0020)
#define		MSG_PLAYER_GET_PICTURE_LIST_ACK		(MSG_PLAYER_BASE | 0x1020)
#define		MSG_PLAYER_GET_INFOMATION			(MSG_PLAYER_BASE | 0x0021)
#define		MSG_PLAYER_GET_INFOMATION_ACK		(MSG_PLAYER_BASE | 0x0021)

//control command
#define		PLAYER_PROPERTY_SPEED				(0x0000 | PROPERTY_TYPE_GET | PROPERTY_TYPE_SET)

/*
 * structure
 */
typedef enum {
   PLAYER_FILEFOUND = 0x10,
   PLAYER_FILENOTFOUND,
   PLAYER_FINISH,
};


typedef struct player_init_t {
	char				switch_to_live;
	char				switch_to_live_audio;
	char				auto_exit;
	char				offset;
	char				speed;
	char				channel_merge;
	char				audio;
	unsigned long long 	start;
	unsigned long long 	stop;
	void				*session;
	int					session_id;
	int					tid;
} player_init_t;

/*
 * function
 */
int server_player_start(void);
int server_player_message(message_t *msg);
int server_player_video_message(message_t *msg);
int server_player_audio_message(message_t *msg);
void server_player_interrupt_routine(int param);

#endif /* SERVER_PLAYER_PLAYER_INTERFACE_H_ */
