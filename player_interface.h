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
#define		SERVER_PLAYER_VERSION_STRING		"alpha-3.8"

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
#define		MSG_PLAYER_PROPERTY_SET				(MSG_PLAYER_BASE | 0x1014)
#define		MSG_PLAYER_PROPERTY_SET_ACK			(MSG_PLAYER_BASE | 0x1014)

//control command
#define		PLAYER_PROPERTY_SPEED				(0x0000 | PROPERTY_TYPE_GET | PROPERTY_TYPE_SET)

/*
 * structure
 */

typedef struct player_iot_config_t {
	unsigned long long	start;
	unsigned long long	end;
	int					switch_to_live;
	int					offset;
	int					speed;
	int					want_to_stop;
	int					channel_merge;
} player_iot_config_t;

typedef struct player_file_item_t {
	unsigned long long start;
	unsigned long long stop;
} player_file_item_t;

typedef struct player_file_item_ext_t {
	unsigned int		chn;
	player_file_item_t 	node;
} player_file_item_ext_t;

/*
 * function
 */
int server_player_start(void);
int server_player_message(message_t *msg);
int server_player_video_message(message_t *msg);
int server_player_audio_message(message_t *msg);

#endif /* SERVER_PLAYER_PLAYER_INTERFACE_H_ */
