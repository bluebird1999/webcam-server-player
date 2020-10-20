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

#define 	CONFIG_PLAYER_PROFILE_PATH				"/opt/qcy/config/player_profile.config"

/*
 * structure
 */
typedef struct player_quality_t {
	int	bitrate;
	int audio_sample;
} player_quality_t;

typedef struct player_profile_config_t {
	unsigned int		enable;
	unsigned int		mode;
	char				normal_start[MAX_SYSTEM_STRING_SIZE];
	char				normal_end[MAX_SYSTEM_STRING_SIZE];
	int					normal_repeat;
	int					normal_repeat_interval;
	int					normal_audio;
	int					normal_quality;
	player_quality_t	quality[3];
	unsigned int		max_length;		//in seconds
	unsigned int		min_length;
	char				directory[MAX_SYSTEM_STRING_SIZE];
	char				normal_prefix[MAX_SYSTEM_STRING_SIZE];
	char				motion_prefix[MAX_SYSTEM_STRING_SIZE];
	char				alarm_prefix[MAX_SYSTEM_STRING_SIZE];
} player_profile_config_t;

typedef struct player_config_t {
	int							status;
	player_profile_config_t	profile;
} player_config_t;

/*
 * function
 */
int config_player_read(player_config_t*);
int config_player_set(int module, void *arg);
int config_player_get_config_status(int module);

#endif /* CONFIG_PLAYER_CONFIG_H_ */
