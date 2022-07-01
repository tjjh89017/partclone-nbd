/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "image.h"
#include "bitmap.h"
#include <pthread.h>
#include <nbdkit-filter.h>
#include <vector>

struct continuous_block {
	uint64_t index;
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
	std::vector<continuous_block*> block;
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

	r = next->pread(next, (void*)h->bitmap, bitmap_size, sizeof(struct image_header), 0, &err);
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
	nbdkit_debug("checksum_size=%lu", h->image_header->checksum_size);

	/* TODO prepare a list of range to search true offset */
	uint64_t total_block = h->image_header->total_block;
	uint64_t block_size = h->image_header->block_size;
	uint64_t checksum_size = h->image_header->checksum_size;
	uint64_t blocks_per_checksum = h->image_header->blocks_per_checksum;
	uint64_t image_offset = h->block_start;
	uint64_t offset = 0;
	uint64_t last_used_block = -100;
	uint64_t used_block = 0;
	continuous_block *b = nullptr;

	for (uint64_t i = 0; i < total_block; i++) {
		int used = test_bit(i, h->bitmap, total_block);
		
		if (used) {
			if (used_block % blocks_per_checksum == 0) {
				// hit checksum
				image_offset += checksum_size;
				last_used_block = -100;
			}

			if (last_used_block + 1 != i) {
				b = new continuous_block;
				b->index = i;
				b->offset = offset;
				b->image_offset = image_offset;
				b->length = block_size;

				h->block.push_back(b);
			} else {
				// continue
				// use previous block
				b = h->block.back();
				b->length += block_size;
			}
			
			used_block++;
			last_used_block = i;
			image_offset += block_size;
		}

		offset += block_size;
	}

	// debug
	/*
	for (std::vector<continuous_block*>::iterator it = h->block.begin(); it != h->block.end(); it++) {
		b = *it;
		nbdkit_debug("---");
		nbdkit_debug("index=%lu", b->index);
		nbdkit_debug("offset=%lu", b->offset);
		nbdkit_debug("image_offset=%lu", b->image_offset);
		nbdkit_debug("length=%lu", b->length);
	}
	*/
	initialized = 1;
	nbdkit_debug("prepare 3");


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
	continuous_block *b = nullptr;
	void *buf_ptr = buf;
	uint64_t offset = offs;
	uint64_t image_offset = 0;
	uint32_t counts = count;
	uint64_t remain_len = 0;
	int r = 0;
	
	/* TODO search offset in list and split by blocksize */
	for (std::vector<continuous_block*>::iterator it = h->block.begin(); it != h->block.end(); it++) {
		b = *it;
		if (counts <= 0) {
			break;
		}
		if (offset >= b->offset && offset < b->offset + b->length) {
			nbdkit_debug("block_offset=%lu", b->offset);
			nbdkit_debug("block_image_offset=%lu", b->image_offset);
			nbdkit_debug("block_length=%lu", b->length);
			nbdkit_debug("counts=%u", counts);
			nbdkit_debug("offset=%lu", offset);

			image_offset = b->image_offset + (b->offset - offset);
			remain_len = b->length - (b->offset - offset);
			if (counts > remain_len) {
				nbdkit_debug("pread remain_len");
				int tmp = next->pread(next, buf_ptr, remain_len, image_offset, flags, err);
				if (tmp > 0) {
					r += tmp;
					offset += remain_len;
					counts -= remain_len;
					buf_ptr += remain_len;
				}

			} else {
				nbdkit_debug("pread counts");
				int tmp = next->pread(next, buf_ptr, counts, image_offset, flags, err);	
				return tmp;
			}
		}
	}

	nbdkit_debug("pread end");
	return r;
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
