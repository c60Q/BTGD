#ifndef BOOT_TRANSPORT_PARAMETER_H
#define BOOT_TRANSPORT_PARAMETER_H

/* Full Protocol layout:
 * [0..3]  : index (filled by caller)
 * [4..5]  : data length (big-endian), counts CMD + PKG state + DATA bytes
 * [6]	   : CMD
 * [7]     : PKG state
 * [8..]   : DATA
 * [..]    : CRC32 (big-endian), computed over CMD + PKG state + DATA
 * '\n'    : data end
 * 
 * Net layer prepend index(net.c,  uip.c)，Rx side is strip index， transport handler's data[0] is length
 * transport handler Protocol layout:
 * [0..1]	: data length (big-endian), counts CMD + PKG state + DATA bytes
 * [2]		: CMD
 * [3]		: PKG state
 * [4..]	: DATA
 * [..]		: CRC32 (big-endian), computed over CMD + PKG state + DATA
 * '\n'		: data end
 */

#define DL_TIMEOUT_MS						(5000u)
#define FIXED_INDEX_COUNT_SIZE				(4u)
#define FIXED_LENGTH_SIZE					(2u)
#define FIXED_CMD_SIZE						(1u)
#define FIXED_PKG_STATE_SIZE				(1u)
#define FIXED_CRC_SIZE						(4u)
#define FIXED_END_DELIM_SIZE    			(1u)
#define IMAGE_UPDATE_DATA_SIZE				(512u) //3MB need 6144 times
#define MINIMUM_EFFERCTIVE_PKG_SIZE			(FIXED_INDEX_COUNT_SIZE + FIXED_LENGTH_SIZE	+ FIXED_CMD_SIZE + FIXED_PKG_STATE_SIZE + FIXED_CRC_SIZE)
#define TCP_BUF_SIZE						(MINIMUM_EFFERCTIVE_PKG_SIZE + IMAGE_UPDATE_DATA_SIZE + FIXED_END_DELIM_SIZE)

#define RETRY_ETH_CREATE_MAX 3u
#define RETRY_ETH_PKT_MAX 3u

#endif /* BOOT_TRANSPORT_PARAMETER_H */

