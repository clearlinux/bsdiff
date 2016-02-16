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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsdiff.h"

/* parse encoding as string and return value as enum
 */

static int get_encoding(char *encoding)
{
	if (strcmp(encoding, "raw") == 0) {
		return BSDIFF_ENC_NONE;
	} else if (strcmp(encoding, "bzip2") == 0) {
		return BSDIFF_ENC_BZIP2;
	} else if (strcmp(encoding, "gzip") == 0) {
		return BSDIFF_ENC_GZIP;
	} else if (strcmp(encoding, "xz") == 0) {
		return BSDIFF_ENC_XZ;
	} else if (strcmp(encoding, "zeros") == 0) {
		return BSDIFF_ENC_ZEROS;
	} else if (strcmp(encoding, "any") == 0) {
		return BSDIFF_ENC_ANY;
	} else {
		return -1;
	}
}

int main(int argc, char **argv)
{
	int ret, enc = BSDIFF_ENC_ANY;

	if (argc < 4) {
		printf("Usage: %s oldfile newfile deltafile [encoding]\n\n", argv[0]);
		printf("Creates a binary diff DELTAFILE from OLDFILE to NEWFILE.");
		printf(" If ENCODING is specified, accepted values are 'raw', 'bzip2',");
		printf(" 'gzip', 'xz', 'zeros', or 'any'. The 'raw' value will force");
		printf(" no compression.\n");
		return -EXIT_FAILURE;
	}

	if (argc > 4) {
		if ((enc = get_encoding(argv[4])) < 0) {
			printf("Unknown encoding algorithm\n");
			return -EXIT_FAILURE;
		}
	}

	ret = make_bsdiff_delta(argv[1], argv[2], argv[3], enc);

	if (ret != 0) {
		printf("Failed to create delta (%d)\n", ret);
		return ret;
	}

	return EXIT_SUCCESS;
}
