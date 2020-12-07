/*
 * config_player.c
 *
 *  Created on: Aug 16, 2020
 *      Author: ning
 */

/*
 * header
 */
//system header
#include <pthread.h>
#include <stdio.h>
#include <malloc.h>
//program header
#include "../../tools/tools_interface.h"
#include "../../manager/manager_interface.h"
//server header
#include "config.h"

/*
 * static
 */
//variable
static int						dirty;
static player_config_t		player_config;
static config_map_t player_config_profile_map[] = {
	{"enable",     			&(player_config.profile.enable),      			cfg_u32, 	0,0,0,1,  	},
	{"switch_to_live",     	&(player_config.profile.switch_to_live),      	cfg_u32, 	0,0,0,1,  	},
	{"auto_exit",      		&(player_config.profile.auto_exit),       		cfg_u32, 	0,0,0,1,},
	{"offset",      		&(player_config.profile.offset),   				cfg_u32, 	0,0,0,100000,},
	{"speed",     			&(player_config.profile.speed),      			cfg_u32, 	0,0,0,100,  	},
	{"channel_merge",  		&(player_config.profile.channel_merge),      	cfg_u32, 	0,0,0,1,  	},
    {NULL,},
};
//function
static int player_config_save(void);

/*
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 */

/*
 * interface
 */
static int player_config_save(void)
{
	int ret = 0;
	message_t msg;
	char fname[MAX_SYSTEM_STRING_SIZE*2];
	memset(fname,0,sizeof(fname));
	sprintf(fname,"%s%s",_config_.qcy_path, CONFIG_PLAYER_PROFILE_PATH);
	if( misc_get_bit(dirty, CONFIG_PLAYER_PROFILE) ) {
		ret = write_config_file(&player_config_profile_map, fname);
		if(!ret)
			misc_set_bit(&dirty, CONFIG_PLAYER_PROFILE, 0);
	}
	if( !dirty ) {
		/********message body********/
		msg_init(&msg);
		msg.message = MSG_MANAGER_TIMER_REMOVE;
		msg.arg_in.handler = player_config_save;
		/****************************/
		manager_common_send_message(SERVER_MANAGER, &msg);
	}
	return ret;
}

int config_player_read(player_config_t *rconfig)
{
	int ret,ret1=0;
	char fname[MAX_SYSTEM_STRING_SIZE*2];
	memset(fname,0,sizeof(fname));
	sprintf(fname,"%s%s",_config_.qcy_path, CONFIG_PLAYER_PROFILE_PATH);
	ret = read_config_file(&player_config_profile_map, fname);
	if(!ret)
		misc_set_bit(&player_config.status, CONFIG_PLAYER_PROFILE,1);
	else
		misc_set_bit(&player_config.status, CONFIG_PLAYER_PROFILE,0);
	ret1 |= ret;
	memcpy(rconfig,&player_config,sizeof(player_config_t));
	return ret1;
}

int config_player_set(int module, void *arg)
{
	int ret = 0;
	if(dirty==0) {
		message_t msg;
	    /********message body********/
		msg_init(&msg);
		msg.message = MSG_MANAGER_TIMER_ADD;
		msg.sender = SERVER_CONFIG;
		msg.arg_in.cat = FILE_FLUSH_TIME;	//1min
		msg.arg_in.dog = 0;
		msg.arg_in.duck = 0;
		msg.arg_in.handler = &player_config_save;
		/****************************/
		manager_common_send_message(SERVER_MANAGER, &msg);
	}
	misc_set_bit(&dirty, module, 1);
	if( module == CONFIG_PLAYER_PROFILE) {
		memcpy( (player_profile_config_t*)(&player_config.profile), arg, sizeof(player_profile_config_t));
	}
	return ret;
}
