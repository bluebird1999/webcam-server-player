/*
 * config_player.h
 *
 *  Created on: Aug 16, 2020
 *      Author: ning
 */

#ifndef SERVER_PLAYER_CONFIG_H_
#define SERVER_PLAYER_CONFIG_H_

/*
 * header
 */

/*
 * define
 */
#define		CONFIG_PLAYER_MODULE_NUM			1
#define		CONFIG_PLAYER_PROFILE				0

#define 	CONFIG_PLAYER_PROFILE_PATH			"config/player_profile.config"

/*
 * structure
 */
typedef struct player_profile_config_t {
	char				enable;
	char				switch_to_live;
	char				auto_exit;
	char				offset;
	char				speed;
	char				channel_merge;
	char   				path[MAX_SYSTEM_STRING_SIZE*2];
	char   				prefix[MAX_SYSTEM_STRING_SIZE];
} player_profile_config_t;

typedef struct player_config_t {
	int							status;
	player_profile_config_t		profile;
} player_config_t;

/*
 * function
 */
int config_player_read(player_config_t*);
int config_player_set(int module, void *arg);

#endif /* SERVER_PLAYER_CONFIG_H_ */
