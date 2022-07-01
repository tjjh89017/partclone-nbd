/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "image.h"
#include "bitmap.h"
#include <pthread.h>
#include <nbdkit-filter.h>

struct continuous_block {
	/* disk offset */
	uint64_t offset;
	/* image offset (without block_start) */
	uint64_t image_offset;
	uint64_t length;
};

/* readonly struct, once finished construct */
struct partclone_handle {
	struct image_header *image_header;
	uint64_t *bitmap;
	uint64_t block_start;
	uint64_t bitmap_size;
	uint64_t size;
	struct continuous_block *block;
	uint64_t block_length;
};

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int initialized = 0;

static int partclone_thread_model(void)
{
	return NBDKIT_THREAD_MODEL_PARALLEL;
}

static void* partclone_open(nbdkit_next_open *next, nbdkit_context *context, int readonly, const char *exportname, int is_tls)
{
	nbdkit_debug("open");
	struct partclone_handle *h = (struct partclone_handle*)calloc(1, sizeof(struct partclone_handle));
	if (next(context, 1, exportname) == -1)
		return NULL;

	return h;
}

static void partclone_close(void *handle)
{
	nbdkit_debug("close");
	struct partclone_handle *h = (struct partclone_handle*)handle;
	free(h->image_header);
	free(h->bitmap);
}

static int partclone_prepare(nbdkit_next *next, void *handle, int readonly)
{
	nbdkit_debug("prepare");
	struct partclone_handle *h = (struct partclone_handle*)handle;

	if (initialized) {
		nbdkit_debug("already construct global_handle\n");
		return 0;
	}

	/* call get_size first */
	h->size = next->get_size(next);
	
	/* construct image_header */
	h->image_header = (struct image_header*)malloc(sizeof(struct image_header));

	int err, r;
	r = next->pread(next, (void*)h->image_header, sizeof(struct image_header), 0, 0, &err);
	nbdkit_debug("prepare 0");
	if (r == -1) {
		errno = err;
		nbdkit_error("pread: %m");
		return -1;
	}
	nbdkit_debug("prepare 1");

	/* construct bitmap */
	/* TODO replace 63 64 with MACRO*/
	uint64_t bitmap_size = (h->image_header->total_block + 63) / 64 * 8;
	h->bitmap = (uint64_t*)malloc(bitmap_size);
	h->bitmap_size = bitmap_size;

	r = next->pread(nxdata, (void*)h->bitmap, bitmap_size, sizeof(struct image_header), 0, &err);
	if (r == -1) {
		errno = err;
		nbdkit_error("pread: %m");
		return -1;
	}
	nbdkit_debug("prepare 2");

	/* get block_start offset, skip bitmap crc32 */
	h->block_start = sizeof(struct image_header) + bitmap_size + 4;
	nbdkit_debug("block_start=%lu", h->block_start);
	nbdkit_debug("bitmap_size=%lu", bitmap_size);
	nbdkit_debug("blocks_per_checksum=%lu", h->image_header->blocks_per_checksum);

	initialized = 1;
	nbdkit_debug("prepare 3");

	/* TODO prepare a list of range to search true offset */

	return 0;
}

static int64_t partclone_get_size(nbdkit_next *next, void *handle)
{
	nbdkit_debug("get_size");
	struct partclone_handle *h = (struct partclone_handle*)handle;
	return h->image_header->device_size;
}

static int partclone_pread(nbdkit_next *next, void *handle, void *buf, uint32_t count, uint64_t offs, uint32_t flags, int *err)
{
	nbdkit_debug("pread");
	struct partclone_handle *h = (struct partclone_handle*)handle;
	
	/* TODO search offset in list and split by blocksize */

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
