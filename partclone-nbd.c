/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "image.h"
#include "bitmap.h"
#include <pthread.h>
#include <nbdkit-filter.h>

/* readonly struct, once finished construct */
struct partclone_handle {
	struct image_header *image_header;
	uint64_t *bitmap;
	uint64_t block_start;
	uint64_t bitmap_size;
	uint64_t size;
};

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int initialized = 0;

static int partclone_thread_model(void)
{
	return NBDKIT_THREAD_MODEL_PARALLEL;
}

static void* partclone_open(nbdkit_next_open *next, nbdkit_backend *nxdata, int readonly, const char *exportname, int is_tls)
{
	nbdkit_debug("open");
	struct partclone_handle *h = calloc(1, sizeof(struct partclone_handle));
	if(next(nxdata, 1, exportname) == -1)
		return NULL;

	return h;
}

static void partclone_close(void *handle)
{
	nbdkit_debug("close");
	struct partclone_handle *h = handle;
	free(h->image_header);
	free(h->bitmap);
}

static int partclone_prepare(struct nbdkit_next_ops *next, void *nxdata, void *handle, int readonly)
{
	nbdkit_debug("prepare");
	struct partclone_handle *h = handle;

	/*if(initialized) {
		nbdkit_debug("already construct global_handle\n");
		return 0;
	}*/

	/* call get_size first */
	h->size = next->get_size(nxdata);
	
	/* construct image_header */
	h->image_header = malloc(sizeof(struct image_header));

	int err, r;
	r = next->pread(nxdata, (void*)h->image_header, sizeof(struct image_header), 0, 0, &err);
	nbdkit_debug("prepare 0");
	if(r == -1) {
		errno = err;
		nbdkit_error("pread: %m");
		return -1;
	}
	nbdkit_debug("prepare 1");

	/* construct bitmap */
	uint64_t bitmap_size = (h->image_header->total_block + 63) / 64 * 8;
	h->bitmap = malloc(bitmap_size);
	h->bitmap_size = bitmap_size;

	r = next->pread(nxdata, (void*)h->bitmap, bitmap_size, sizeof(struct image_header), 0, &err);
	if(r == -1) {
		errno = err;
		nbdkit_error("pread: %m");
		return -1;
	}
	nbdkit_debug("prepare 2");

	/* get block_start offset, skip bitmap crc32 */
	h->block_start = sizeof(struct image_header) + bitmap_size + 4;
	nbdkit_debug("block_start=%llu", h->block_start);
	nbdkit_debug("bitmap_size=%llu", bitmap_size);
	nbdkit_debug("blocks_per_checksum=%llu", h->image_header->blocks_per_checksum);

	initialized = 1;
	nbdkit_debug("prepare 3");

	return 0;
}

static int64_t partclone_get_size(struct nbdkit_next_ops *next, void *nxdata, void *handle)
{
	nbdkit_debug("get_size");
	struct partclone_handle *h = handle;
	return h->image_header->device_size;
}

static int partclone_pread(struct nbdkit_next_ops *next, void *nxdata, void *handle, void *buf, uint32_t count, uint64_t offs, uint32_t flags, int *err)
{
	nbdkit_debug("pread");
	struct partclone_handle *h = handle;
	uint64_t total_block = h->image_header->total_block;
	uint64_t block_size = h->image_header->block_size;
	uint64_t block_start = h->block_start;
	uint32_t blocks_per_checksum = h->image_header->blocks_per_checksum;
	int r = 0;
	uint64_t i = 0, j = 0;
	uint64_t blocks_before_offs = 0;

	/* search how many blocks from block_start to offs */
	nbdkit_debug("total block: %llu", total_block);
	nbdkit_debug("before search blocks");
	for(i = block_start, j = 0; i < offs; i += block_size, j++) {
		if(j >= total_block || j >= h->bitmap_size * 64) {
			/* excess total blocks return err */
			nbdkit_error("pread: %m");
			return -1;
		}
		blocks_before_offs += test_bit(j, h->bitmap, total_block) > 0 ? 1 : 0;
	}
	nbdkit_debug("after search blocks");
	nbdkit_debug("blocks_before_offs: %llu", blocks_before_offs);
	nbdkit_debug("blocks_per_checksum: %llu", blocks_per_checksum);

	uint64_t block_offset = offs / block_size;
	for(i = 0; i < count; i += block_size) {
		uint32_t read_count = block_size;
		if(count < block_size) {
			read_count = count;
		}
		if(test_bit(block_offset, h->bitmap, total_block) > 0) {
			/* read from image */
			uint64_t offset = offs + i + block_start + (blocks_before_offs / blocks_per_checksum) * 4;
			if(offset > h->size) {
				memset(buf + i, 0, read_count);
				block_offset += 1;
				nbdkit_debug("offset %llu is empty", offs + i);
				continue;
			}
			r = next->pread(nxdata, buf + i, read_count, offset, flags, err);
			if(r == -1) {
				errno = *err;
				nbdkit_error("pread: %m");
				return -1;
			}
			blocks_before_offs += 1;
		} else {
			/* memset to zero */
			memset(buf + i, 0, read_count);
			nbdkit_debug("offset %llu is empty", offs + i);
		}
		block_offset += 1;
	}

	return 0;
}

static struct nbdkit_filter filter = {
	.name = "partclone",
	.longname = "nbdkit partclone filter",
	//.config = partclone_config,
	//.config_complete = partclone_config_complete,
	.thread_model = partclone_thread_model,
	.open = partclone_open,
	.close = partclone_close,
	.prepare = partclone_prepare,
	.get_size = partclone_get_size,
	.pread = partclone_pread,
};

NBDKIT_REGISTER_FILTER(filter)
