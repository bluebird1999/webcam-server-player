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
#define		MAX_BETWEEN_RECODER_PAUSE		5		//5s
#define		TIMEOUT							3		//3s
#define		MIN_SD_SIZE_IN_MB				64		//64M

#define		ERR_NONE						0
#define		ERR_NO_DATA						-1
#define		ERR_TIME_OUT					-2
#define		ERR_LOCK						-3
#define		ERR_ERROR						-4

#define		PLAYER_INIT_CONDITION_NUM				3
#define		PLAYER_INIT_CONDITION_CONFIG			0
#define		PLAYER_INIT_CONDITION_DEVICE_CONFIG	1
#define		PLAYER_INIT_CONDITION_MIIO_TIME		2

/*
 * structure
 */

/*
 * function
 */

#endif /* SERVER_PLAYER_PLAYER_H_ */
