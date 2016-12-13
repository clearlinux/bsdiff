/*
 *   This file is part of bsdiff.
 *
 *      Copyright © 2012-2016 Intel Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted providing that the following conditions 
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 *   Authors:
 *         Tim Pepper <timothy.c.pepper@linux.intel.com>
 *
 */

#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "bsdiff.h"
#include "bsheader.h"

static char *algos[BSDIFF_ENC_LAST] = {"invalid", "none", "bzip2", "gzip", "xz", "zeros"};

static void banner(char **argv)
{
	printf("Usage: %s FILE\n\n", argv[0]);
	printf("Dumps header information from the bsdiff FILE.\n");
	exit(-EXIT_FAILURE);
}

static int read_v20_header(struct header_v20 *h, FILE *f, char *filename)
{
	rewind(f);
	if (fread(h, sizeof(struct header_v20), 1, f) < 1) {
		printf("v2 magic, but short header (%s)\n", filename);
		return -1;
	}
	return 0;
}

static void print_v20_header(struct header_v20 *h, FILE *f)
{
	int ret;
	uint64_t *zeros;

	printf("First block offset:\t%3u\n", h->offset_to_first_block);
	printf("Cblock length:   %10u\n", h->control_length);
	printf("     encoding:   %10s\n", algos[cblock_get_enc(h->encoding)]);
	printf("Dblock length:   %10llu\n", (long long unsigned int)(h->diff_length));
	printf("     encoding:   %10s\n", algos[dblock_get_enc(h->encoding)]);
	if (dblock_get_enc(h->encoding) == BSDIFF_ENC_ZEROS) {
		zeros = malloc(sizeof(uint64_t));
		assert(zeros);

		ret = fseek(f, h->offset_to_first_block + h->control_length, SEEK_SET);
		if (ret != 0) {
			printf("     numzeros:   error seeking\n");
			exit(-1);
		}

		if (fread(zeros, sizeof(uint64_t), 1, f) != 1) {
			printf("     numzeros:   error reading\n");
		} else {
			printf("     numzeros:   %10llu\n", (long long unsigned int)(*zeros));
		}
		free(zeros);
	}
	printf("Eblock length:   %10llu\n", (long long unsigned int)(h->extra_length));
	printf("     encoding:   %10s\n", algos[eblock_get_enc(h->encoding)]);
	if (eblock_get_enc(h->encoding) == BSDIFF_ENC_ZEROS) {
		zeros = malloc(sizeof(uint64_t));
		assert(zeros);

		ret = fseek(f, h->offset_to_first_block + h->control_length + h->diff_length, SEEK_SET);
		if (ret != 0) {
			printf("     numzeros:   error seeking\n");
			exit(-1);
		}

		if (fread(zeros, sizeof(uint64_t), 1, f) != 1) {
			printf("     numzeros:   error reading\n");
		} else {
			printf("     numzeros:   %10llu\n", (long long unsigned int)(*zeros));
		}
		free(zeros);
	}
	printf("Old file length: %10llu\n", (long long unsigned int)(h->old_file_length));
	printf("New file length: %10llu\n", (long long unsigned int)(h->new_file_length));
	if (h->mtime == 0) {
		printf("Mtime:\t(not set, as expected)\n");
	} else {
		printf("Mtime:\t%s (probably means there is a bug)\n", ctime((const time_t *)&h->mtime));
	}
	printf("Mode:\t%4o\n", h->file_mode);
	printf("Uid:\t%d\n", h->file_owner);
	printf("Gid:\t%d\n", h->file_group);
}

static int read_v21_header(struct header_v21 *h, FILE *f, char *filename)
{
	rewind(f);
	if (fread(h, sizeof(struct header_v21), 1, f) < 1) {
		printf("v3 magic, but short header (%s)\n", filename);
		return -1;
	}
	return 0;
}

static void print_v21_header(struct header_v21 *h, FILE *f)
{
	int ret;
	uint64_t *zeros;

	printf("First block offset:\t%3u\n", h->offset_to_first_block);
	printf("Cblock length:   %10u\n", h->control_length);
	printf("     encoding:   %10s\n", algos[cblock_get_enc(h->encoding)]);
	printf("Dblock length:   %10u\n", h->diff_length);
	printf("     encoding:   %10s\n", algos[dblock_get_enc(h->encoding)]);
	if (dblock_get_enc(h->encoding) == BSDIFF_ENC_ZEROS) {
		zeros = malloc(sizeof(uint64_t));
		assert(zeros);

		ret = fseek(f, h->offset_to_first_block + h->control_length, SEEK_SET);
		if (ret != 0) {
			printf("     numzeros:   error seeking\n");
			exit(-1);
		}

		if (fread(zeros, sizeof(uint64_t), 1, f) != 1) {
			printf("     numzeros:   error reading\n");
		} else {
			printf("     numzeros:   %10llu\n", (long long unsigned int)(*zeros));
		}
		free(zeros);
	}
	printf("Eblock length:   %10u\n", h->extra_length);
	printf("     encoding:   %10s\n", algos[eblock_get_enc(h->encoding)]);
	if (eblock_get_enc(h->encoding) == BSDIFF_ENC_ZEROS) {
		zeros = malloc(sizeof(uint64_t));
		assert(zeros);

		ret = fseek(f, h->offset_to_first_block + h->control_length + h->diff_length, SEEK_SET);
		if (ret != 0) {
			printf("     numzeros:   error seeking\n");
			exit(-1);
		}

		if (fread(zeros, sizeof(uint64_t), 1, f) != 1) {
			printf("     numzeros:   error reading\n");
		} else {
			printf("     numzeros:   %10llu\n", (long long unsigned int)(*zeros));
		}
		free(zeros);
	}
	printf("Old file length: %10u\n", h->old_file_length);
	printf("New file length: %10u\n", h->new_file_length);
	printf("Mode:\t%4o\n", h->file_mode);
	printf("Uid:\t%d\n", h->file_owner);
	printf("Gid:\t%d\n", h->file_group);
}

int main(int argc, char **argv)
{
	FILE *infile;
	unsigned char magic[8];
	int ret = 0;

	if (argc < 2) {
		banner(argv);
	}

	infile = fopen(argv[1], "r");

	if (infile == NULL) {
		printf("Error opening file %s\n", argv[1]);
		banner(argv);
	}

	if (fread(&magic, 8, 1, infile) < 1) {
		printf("Magic: unknown (short)\n");
		ret = -1;
		goto out;
	}

	if (memcmp(&magic, BSDIFF_HDR_MAGIC_V20, 8) == 0) {
		/* bsdiff v20: */
		struct header_v20 h;
		memset(&h, 0, sizeof(struct header_v20));

		ret = read_v20_header(&h, infile, argv[1]);
		if (ret != 0) {
			goto out;
		}

		printf("Magic: %s (v2.0)\n", BSDIFF_HDR_MAGIC_V20);
		print_v20_header(&h, infile);

	} else if (memcmp(&magic, BSDIFF_HDR_MAGIC_V21, 8) == 0) {
		/* bsdiff v21: */
		struct header_v21 h;
		memset(&h, 0, sizeof(struct header_v21));

		ret = read_v21_header(&h, infile, argv[1]);
		if (ret != 0) {
			goto out;
		}

		printf("Magic: %s (v2.1)\n", BSDIFF_HDR_MAGIC_V21);
		print_v21_header(&h, infile);

	} else if (memcmp(&magic, BSDIFF_HDR_DIR_V20, 8) == 0) {
		/* directory: anything interesting to print? */
		struct header_v20 h;
		memset(&h, 0, sizeof(struct header_v20));

		ret = read_v20_header(&h, infile, argv[1]);
		if (ret != 0) {
			goto out;
		}

		printf("Magic:\t%s\n", BSDIFF_HDR_DIR_V20);
		printf("Mode:\t%4o\n", h.file_mode);
		printf("Uid:\t%d\n", h.file_owner);
		printf("Gid:\t%d\n", h.file_group);

	} else if (memcmp(&magic, BSDIFF_HDR_FULLDL, 8) == 0) {
		/* full download req'd: possibly nothing more than the 8bytes
		   magic, or maybe interesting info still */
		uint8_t len;
		printf("Magic: %s\n", BSDIFF_HDR_FULLDL);
		if (fread(&len, 1, 1, infile) == 1) {
			if (len == sizeof(struct header_v20)) {
				struct header_v20 h;
				ret = read_v20_header(&h, infile, argv[1]);
				if (ret != 0) {
					goto out;
				}
				print_v20_header(&h, infile);
			} else if (len == sizeof(struct header_v21)) {
				struct header_v21 h;
				ret = read_v21_header(&h, infile, argv[1]);
				if (ret != 0) {
					goto out;
				}
				print_v21_header(&h, infile);
			}
		}
	} else { // unknown
		printf("Magic: unknown\n");
		ret = -1;
	}
out:
	fclose(infile);
	return ret;
}
