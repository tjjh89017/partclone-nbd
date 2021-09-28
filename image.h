/* SPDX-License-Identifier: LGPL-2.1-or-later */

#ifndef __IMAGE_H__
#define __IMAGE_H__

#include <stdint.h>

#pragma pack(push, 1)

/* Image Header Format 0002
 * Refer: https://github.com/Thomas-Tsai/partclone/blob/master/IMAGE_FORMATS.md
 */
struct image_header {
	/* image_head_v2 */
	char magic[16];
	char partclone_version[14];
	char image_version[4];
	uint16_t endianess;

	/* file_system_info_v2 */
	char filesystem_type[16];
	uint64_t device_size;
	uint64_t total_block;
	uint64_t used_block;
	uint64_t bitmap_size;
	uint32_t block_size;

	/* image_options_v2 */
	uint32_t feature_size;
	uint16_t image_version_binary;
	uint16_t cpu_bits;
	uint16_t checksum_mode;
	uint16_t checksum_size;
	uint32_t blocks_per_checksum;
	uint8_t reseed_checksum;
	uint8_t bitmap_mode;

	/* crc32 */
	uint32_t crc;
};

/* Bitmap
 * ceil(total_block/8) bytes
 * crc32 4 bytes
 */

/* Blocks
 * block_size * blocks_per_checksum
 */

#pragma pack(pop)
#endif
