/* SPDX-License-Identifier: LGPL-2.1-or-later */

#ifndef __BITMAP_H__
#define __BITMAP_H__

#include <stdint.h>
#include <stdlib.h>

static inline int test_bit(uint64_t block_offset, uint64_t *bitmap, uint64_t total)
{
	if (!bitmap)
		return -1;
	if (block_offset >= total)
		return -1;
	uint64_t bitmap_offset = block_offset / 64;
	uint64_t bit = block_offset & (64 - 1);
	return (bitmap[bitmap_offset] >> bit) & 1;
	
}

#endif
