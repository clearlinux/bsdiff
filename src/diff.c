/*-
 * Copyright 2003-2005 Colin Percival
 * All rights reserved
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
 */

#if 0
__FBSDID
    ("$FreeBSD: src/usr.bin/bsdiff/bsdiff/bsdiff.c,v 1.1 2005/08/06 01:59:05 cperciva Exp $");
#endif

#define _GNU_SOURCE
#include "config.h"

#ifdef BSDIFF_WITH_BZIP2
#include <bzlib.h>
#endif

#include <err.h>
#include <fcntl.h>

#ifdef BSDIFF_WITH_LZMA
#include <lzma.h>
#endif

#include <assert.h>
#include <endian.h>
#include <grp.h>
#include <pthread.h>
#include <pwd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <zlib.h>

#include "bsheader.h"

static int bsdiff_files;
static uint64_t bsdiff_newbytes;
static uint64_t bsdiff_outputbytes;
static int bsdiff_gzip;
static int bsdiff_bzip2;
static int bsdiff_xz;
static int bsdiff_none;
static int bsdiff_zeros;
static int bsdiff_fulldl;

/* TODO: oh dear, another MIN that multiple evaluates....  */
#undef MIN
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

static int64_t matchlen(u_char *old, int64_t oldsize, u_char *new,
			int64_t newsize)
{
	int64_t i;

	for (i = 0; (i < oldsize) && (i < newsize); i++) {
		if (old[i] != new[i]) {
			break;
		}
	}

	return i;
}

/**
 * Finds the longest matching array of bytes between the OLD and NEW file. The
 * old file is suffix-sorted; the suffix-sorted array is stored at I, and
 * indices to search between are indicated by ST (start) and EN (end). The
 * function does not return a value, but once a match is determined, POS is
 * updated to the position of the match within OLD, and MAX_LEN is set to the
 * match length.
 */
static void search(int64_t *I, u_char *old, int64_t oldsize,
		   u_char *new, int64_t newsize, int64_t st, int64_t en,
		   int64_t *pos, int64_t *max_len)
{
	int64_t x, y;

	/* Initialize max_len for the binary search */
	if (st == 0 && en == oldsize) {
		*max_len = matchlen(old, oldsize, new, newsize);
		*pos = I[st];
	}

	/* The binary search terminates here when "en" and "st" are adjacent
	 * indices in the suffix-sorted array. */
	if (en - st < 2) {
		x = matchlen(old + I[st], oldsize - I[st], new, newsize);
		if (x > *max_len) {
			*max_len = x;
			*pos = I[st];
		}
		y = matchlen(old + I[en], oldsize - I[en], new, newsize);
		if (y > *max_len) {
			*max_len = y;
			*pos = I[en];
		}

		return;
	}

	x = st + (en - st) / 2;

	int64_t length = MIN(oldsize - I[x], newsize);
	u_char *oldoffset = old + I[x];

	/* This match *could* be the longest one, so check for that here */
	int64_t tmp = matchlen(oldoffset, length, new, length);
	if (tmp > *max_len) {
		*max_len = tmp;
		*pos = I[x];
	}

	/* Determine how to continue the binary search */
	if (memcmp(oldoffset, new, length) < 0) {
		return search(I, old, oldsize, new, newsize, x, en, pos, max_len);
	} else {
		return search(I, old, oldsize, new, newsize, st, x, pos, max_len);
	}
}

static inline void offtout(int64_t x, u_char *buf)
{
	*((int64_t *)buf) = htole64(x);
}

/* zlib provides compress2, which deflates to deflate (zlib) format. This is
 * unfortunately distinct from gzip format in that the headers wrapping the
 * decompressed data are different. gbspatch reads gzip-compressed data using
 * the file-oriented gzread interface, which only supports gzip format.
 * compress2gzip is identical to zlib's compress2 except that it produces gzip
 * output compatible with gzread. This change is achieved by calling
 * deflateInit2 instead of deflateInit and specifying 31 for windowBits;
 * numbers greater than 15 cause the addition of a gzip wrapper. */

static int compress2gzip(Bytef *dest, size_t *destLen,
			 const Bytef *source, uLong sourceLen, int level)
{
	z_stream stream;
	int err;

	stream.next_in = (Bytef *)source;
	stream.avail_in = (uInt)sourceLen;

	stream.next_out = dest;
	stream.avail_out = (uInt)*destLen;
	if ((uLong)stream.avail_out != *destLen) {
		return Z_BUF_ERROR;
	}

	stream.zalloc = (alloc_func)0;
	stream.zfree = (free_func)0;
	stream.opaque = (voidpf)0;

	err = deflateInit2(&stream,
			   level, Z_DEFLATED, 31, 8, Z_DEFAULT_STRATEGY);
	if (err != Z_OK) {
		return err;
	}

	err = deflate(&stream, Z_FINISH);
	if (err != Z_STREAM_END) {
		deflateEnd(&stream);
		return err == Z_OK ? Z_BUF_ERROR : err;
	}
	*destLen = stream.total_out;

	err = deflateEnd(&stream);
	return err;
}

#ifdef BSDIFF_WITH_LZMA
static pthread_mutex_t lzma_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

static uint64_t count_nonzero(unsigned char *buf, uint64_t len)
{
	uint64_t count = 0;
	uint64_t i = 0;
	while (i < len) {
		if (buf[i]) {
			count++;
		}
		i++;
	}
	return count;
}

/* Recompress buf of size buf_len using a supported algorithm. The smallest version is
 * used. The original uncompressed variant may be the smallest. Returns a
 * number identifying the encoding according to enum BSDIFF_ENCODINGS.
 * If the original uncompressed variant is not smallest, it is freed. The caller
 * must free any buf after this function returns. */
static char make_small(u_char **buf,
		       uint64_t *buf_len,
		       int enc,
		       __attribute__((unused)) char *file,
		       char *blockname)
{
	u_char *source = *buf;
	uint64_t source_len = *buf_len;
#ifdef BSDIFF_WITH_BZIP2
	u_char *bz2;
	unsigned int bz2_len;
	int bz2_err;
	int bzip_penalty = 512;
#endif
#ifdef BSDIFF_WITH_LZMA
	u_char *lzma = NULL;
	size_t lzma_len, lzma_pos;
	lzma_ret lzma_err;
	lzma_check lzma_ck;
#endif
	u_char *gz;
	size_t gz_len;
	int gz_err;
	char smallest;

	__attribute__((unused)) uint64_t unc_size = 0, bzip_size = 0, gzip_size = 0, xz_size = 0, nonzero = 0;

	smallest = BSDIFF_ENC_NONE;

	if (enc == BSDIFF_ENC_NONE || source_len == 0) {
		return smallest;
	}
	unc_size = *buf_len;

	nonzero = count_nonzero(source, source_len);

	/* if it's an all-zeros block, we're done */
	if (!nonzero) {
		if ((enc == BSDIFF_ENC_ANY) &&
		    ((strncmp(blockname, "diff", 4) == 0) ||
		     (strncmp(blockname, "extra", 5) == 0))) {
			uint64_t *zeros;
			zeros = malloc(sizeof(uint64_t));
			assert(zeros);
			*zeros = source_len;
			free(source);
			*buf = (u_char *)zeros;
			*buf_len = sizeof(uint64_t);
			return BSDIFF_ENC_ZEROS;
		}
#ifdef BSDIFF_WITH_BZIP2
		bzip_penalty = 0;
#endif
	}

	/* we do gzip first. it's fast on decompression and does quite well on compression */
	gz_len = source_len + 1;
	gz = malloc(gz_len);
	gz_err = compress2gzip(gz, &gz_len, source, source_len, 9);
	if (gz_err == Z_OK) {
		gzip_size = gz_len;

		if (gz_len < (unsigned int)*buf_len &&
		    (enc == BSDIFF_ENC_ANY || enc == BSDIFF_ENC_GZIP)) {
			smallest = BSDIFF_ENC_GZIP;
			*buf = gz;
			*buf_len = gz_len;
		} else {
			free(gz);
			gz = NULL;
		}
	} else if (gz_err == Z_BUF_ERROR) {
		free(gz);
		gz = NULL;
	}

#ifdef BSDIFF_WITH_LZMA
	/* xz/lzma are slower on decompression, but esp for bigger files, compress better */
	pthread_mutex_lock(&lzma_mutex);
	lzma_len = source_len + 1000;
	lzma = malloc(lzma_len);
	lzma_pos = 0;

	/* Equivalent to the options used by xz -9 -e. */
	/*
	 * We'd like to set LZMA_CHECK_NONE, since we do our own sha based checksum at the end.
	 * However, that seems to generate undecodable compressed blocks, so we'll just do the
	 * smallest and cheapest alternative to _NONE, which is CRC32
	 */
	lzma_ck = LZMA_CHECK_CRC32;
	if (!lzma_check_is_supported(lzma_ck)) {
		lzma_ck = LZMA_CHECK_CRC32;
	}
	lzma_err = lzma_easy_buffer_encode(9 | LZMA_PRESET_EXTREME,
					   lzma_ck, NULL,
					   source, source_len,
					   lzma, &lzma_pos, lzma_len);
	if (lzma_err == LZMA_OK) {
		xz_size = lzma_pos;
		if (1.01 * lzma_pos + 64 < *buf_len &&
		    (enc == BSDIFF_ENC_ANY || enc == BSDIFF_ENC_XZ)) {
			smallest = BSDIFF_ENC_XZ;
			*buf = lzma;
			*buf_len = lzma_pos;
		} else {
			free(lzma);
			lzma = NULL;
		}
	} else if (lzma_err == LZMA_BUF_ERROR) {
		free(lzma);
		lzma = NULL;
	}

	pthread_mutex_unlock(&lzma_mutex);
#endif /* BSDIFF_WITH_LZMA */

#ifdef BSDIFF_WITH_BZIP2
	/* bzip2 is the slowed of the set on decompress, but for some times of inputs, does really really well */
	bz2_len = source_len + 1;
	bz2 = malloc(bz2_len);
	bz2_err =
	    BZ2_bzBuffToBuffCompress((char *)bz2, &bz2_len, (char *)source,
				     source_len, 9, 0, 0);
	if (bz2_err == BZ_OK) {
		bzip_size = bz2_len;

		/* we add a 5% + 1/2 Kb penalty to bzip2, due to the high cost on the client */
		if (1.05 * bz2_len + bzip_penalty < (unsigned int)*buf_len &&
		    (enc == BSDIFF_ENC_ANY || enc == BSDIFF_ENC_BZIP2)) {
			smallest = BSDIFF_ENC_BZIP2;
			*buf = bz2;
			*buf_len = bz2_len;
		} else {
			free(bz2);
			bz2 = NULL;
		}
	} else if (bz2_err == BZ_OUTBUFF_FULL) {
		free(bz2);
		bz2 = NULL;
	}
#endif

	if (smallest != BSDIFF_ENC_NONE) {
		free(source);
	}

#ifdef BSDIFF_WITH_BZIP2
	if (smallest != BSDIFF_ENC_BZIP2) {
		free(bz2);
	}
#endif
	if (smallest != BSDIFF_ENC_GZIP) {
		free(gz);
	}

#ifdef BSDIFF_WITH_LZMA
	if (smallest != BSDIFF_ENC_XZ) {
		free(lzma);
	}
#endif

	return smallest;
}

/* returns <0 on error, 0 on success, and 1 on "success" with a FULLDL header */
int make_bsdiff_delta(char *old_filename, char *new_filename, char *delta_filename, int enc)
{
	int fd, efd;
	u_char *old_data, *new_data;
	int64_t oldsize, newsize;
	int64_t *I, *V;
	int64_t scan;
	int64_t pos = 0;
	int64_t len;
	int64_t lastscan, lastpos, lastoffset;
	int64_t oldscore, scsc;
	int64_t s, Sf, lenf, Sb, lenb;
	int64_t overlap, Ss, lens;
	int64_t i;
	uint64_t cblen, dblen, eblen;
	u_char *cb, *db, *eb;
	struct stat new_stat;
	struct stat old_stat;
	int ret, smallfile;
	off_t first_block;
	int c_enc, d_enc, e_enc;
	enc_flags_t encodings;

	struct header_v20 large_header;
	struct header_v21 small_header;
	FILE *pf;

	ret = lstat(old_filename, &old_stat);
	if (ret < 0) {
		return -1;
	}

	ret = lstat(new_filename, &new_stat);
	if (ret < 0) {
		return -1;
	}

	if (S_ISDIR(new_stat.st_mode) || S_ISDIR(old_stat.st_mode)) {
		/* no delta on symlinks ! */
		return -1;
	}

	if ((new_stat.st_size < 65536) && (old_stat.st_size < 65536)) {
		smallfile = 1;
	} else {
		smallfile = 0;
	}

	fd = open(old_filename, O_RDONLY, 0);
	if (fd < 0) {
		return -1;
	}
	if (fstat(fd, &old_stat) != 0) {
		close(fd);
		return -1;
	}

	oldsize = old_stat.st_size;

	/* We may start with an empty file, if so, just mark it for full download
	 * to throw into the pack. In the case that newfile is <200, it will quit
	 * and ask for fulldownload, so we only need to check oldsize */
	if (oldsize == 0) {
		memset(&small_header, 0, sizeof(struct header_v21));
		memcpy(&small_header.magic, BSDIFF_HDR_FULLDL, 8);

		efd = open(delta_filename, O_CREAT | O_EXCL | O_WRONLY, 00644);
		if (efd < 0) {
			close(fd);
			return -1;
		}
		if ((pf = fdopen(efd, "w")) == NULL) {
			close(efd);
			close(fd);
			return -1;
		}
		if (fwrite(&small_header, 8, 1, pf) != 1) {
			fclose(pf);
			close(fd);
			return -1;
		}
		fclose(pf);
		close(fd);
		return 1;
	}

	/* TODO: investigate why this needs to be +1 to not overrun; coverity complains
	 * that we overrun old_data when we calculate differences otherwise. Tenatively,
	 * since this is used in qsufsort, it may need to be +1 like I and V because of
	 * a sentinel byte when sorting. However, newsize does not cause any overruns
	 * when created with the regular file size */
	old_data = mmap(NULL, oldsize + 1, PROT_READ, MAP_SHARED, fd, 0);
	close(fd);

	if (old_data == MAP_FAILED) {
		old_data = NULL;
		return -1;
	}

	/* These arrays are size + 1 because suffix sort needs space for the
	 * data + 1 sentinel element to actually do the sorting. Not because
	 * oldsize might be 0. */
	if ((I = malloc((oldsize + 1) * sizeof(int64_t))) == NULL) {
		munmap(old_data, oldsize);
		return -1;
	}
	if ((V = malloc((oldsize + 1) * sizeof(int64_t))) == NULL) {
		munmap(old_data, oldsize);
		free(I);
		return -1;
	}

	if (qsufsort(I, V, old_data, oldsize) != 0) {
		munmap(old_data, oldsize);
		free(I);
		free(V);
		return -1;
	}

	free(V);

	if ((fd = open(new_filename, O_RDONLY, 0)) < 0) {
		munmap(old_data, oldsize);
		free(I);
		return -1;
	}

	if (fstat(fd, &new_stat) != 0) {
		munmap(old_data, oldsize);
		free(I);
		close(fd);
		return -1;
	}

	newsize = new_stat.st_size;

	/* Note: testing this to see how diffs between small files affect
	 * updates. Small files seem to cause some problems between certain
	 * files (buffer overrun/underrun perhaps). We try to avoid this
	 * preemptively by marking the file as a FULLDL file and not create
	 * a bsdiff at all, which leaves us with small files that may fail
	 * the "is bsdiff < 90% of newfile size" check that would otherwise
	 * be performed later on.
	 */
	if (newsize < 200) {
		memset(&small_header, 0, sizeof(struct header_v21));
		memcpy(&small_header.magic, BSDIFF_HDR_FULLDL, 8);

		efd = open(delta_filename, O_CREAT | O_EXCL | O_WRONLY, 00644);
		if (efd < 0) {
			close(fd);
			munmap(old_data, oldsize);
			free(I);
			return -1;
		}
		if ((pf = fdopen(efd, "w")) == NULL) {
			close(efd);
			close(fd);
			munmap(old_data, oldsize);
			free(I);
			return -1;
		}
		if (fwrite(&small_header, 8, 1, pf) != 1) {
			fclose(pf);
			close(fd);
			munmap(old_data, oldsize);

			free(I);
			return -1;
		}
		fclose(pf);
		close(fd);
		munmap(old_data, oldsize);
		free(I);
		return 1;
	}

	if ((new_data = malloc(newsize)) == NULL) {
		close(fd);
		munmap(old_data, oldsize);
		free(I);
		return -1;
	}

	if (pread(fd, new_data, newsize, 0) != newsize) {
		close(fd);
		munmap(old_data, oldsize);
		free(new_data);
		free(I);
		return -1;
	}
	if (close(fd) == -1) {
		munmap(old_data, oldsize);
		free(new_data);
		free(I);
		return -1;
	}

	/* we can write 3 8 byte tupples extra, so allocate some headroom */
	if ((cb = malloc(newsize + 25)) == NULL) {
		munmap(old_data, oldsize);
		free(new_data);
		free(I);
		return -1;
	}
	if ((db = malloc(newsize + 25)) == NULL) {
		munmap(old_data, oldsize);
		free(new_data);
		free(cb);
		free(I);
		return -1;
	}
	if ((eb = malloc(newsize + 25)) == NULL) {
		munmap(old_data, oldsize);
		free(new_data);
		free(cb);
		free(db);
		free(I);
		return -1;
	}
	cblen = 0;
	dblen = 0;
	eblen = 0;

	/* Compute the differences */
	scan = 0;
	len = 0;
	lastscan = 0;
	lastpos = 0;
	lastoffset = 0;
	while (scan < newsize) {
		oldscore = 0;

		for (scsc = scan += len; scan < newsize; scan++) {
			search(I, old_data, oldsize, new_data + scan, newsize - scan,
			       0, oldsize, &pos, &len);

			for (; scsc < scan + len; scsc++) {
				if ((scsc + lastoffset < oldsize) &&
				    (old_data[scsc + lastoffset] == new_data[scsc])) {
					oldscore++;
				}
			}

			if (((len == oldscore) && (len != 0)) ||
			    (len > oldscore + 8)) {
				break;
			}

			if ((scan + lastoffset < oldsize) &&
			    (old_data[scan + lastoffset] == new_data[scan])) {
				oldscore--;
			}
		}

		if ((len != oldscore) || (scan == newsize)) {
			s = 0;
			Sf = 0;
			lenf = 0;
			for (i = 0;
			     (lastscan + i < scan) && (lastpos + i < oldsize);) {
				if (old_data[lastpos + i] == new_data[lastscan + i]) {
					s++;
				}
				i++;
				if (s * 2 - i > Sf * 2 - lenf) {
					Sf = s;
					lenf = i;
				}
			}

			lenb = 0;
			if (scan < newsize) {
				s = 0;
				Sb = 0;
				for (i = 1;
				     (scan >= lastscan + i) && (pos >= i);
				     i++) {
					if (old_data[pos - i] == new_data[scan - i]) {
						s++;
					}
					if (s * 2 - i > Sb * 2 - lenb) {
						Sb = s;
						lenb = i;
					}
				}
			}

			if (lastscan + lenf > scan - lenb) {
				overlap = (lastscan + lenf) - (scan - lenb);
				s = 0;
				Ss = 0;
				lens = 0;
				for (i = 0; i < overlap; i++) {
					if (new_data[lastscan + lenf - overlap + i] ==
					    old_data[lastpos + lenf - overlap + i]) {
						s++;
					}
					if (new_data[scan - lenb + i] ==
					    old_data[pos - lenb + i]) {
						s--;
					}
					if (s > Ss) {
						Ss = s;
						lens = i + 1;
					}
				}

				lenf += lens - overlap;
				lenb -= lens;
			}

			for (i = 0; i < lenf; i++) {
				db[dblen + i] =
				    new_data[lastscan + i] - old_data[lastpos + i];
			}
			for (i = 0; i < (scan - lenb) - (lastscan + lenf); i++) {
				eb[eblen + i] = new_data[lastscan + lenf + i];
			}

			dblen += lenf;
			eblen += (scan - lenb) - (lastscan + lenf);

			/* checking for control block overflow...
			 * See regression test #15 for an example */
			if ((int64_t)(cblen + 24) > (newsize + 25)) {
				munmap(old_data, oldsize);
				free(new_data);
				free(cb);
				free(db);
				free(eb);
				free(I);
				return -1;
			}

			offtout(lenf, cb + cblen);
			cblen += 8;

			offtout((scan - lenb) - (lastscan + lenf), cb + cblen);
			cblen += 8;

			offtout((pos - lenb) - (lastpos + lenf), cb + cblen);
			cblen += 8;

			lastscan = scan - lenb;
			lastpos = pos - lenb;
			lastoffset = pos - scan;
		}
	}
	free(I);

	c_enc = make_small(&cb, &cblen, enc, new_filename, "control");
	d_enc = make_small(&db, &dblen, enc, new_filename, "diff   ");
	e_enc = make_small(&eb, &eblen, enc, new_filename, "extra  ");

	if ((!cb) || (!db) || (!eb)) {
		ret = -1;
		goto fulldl_free;
	}

	/* Create the patch file */

	efd = open(delta_filename, O_CREAT | O_EXCL | O_WRONLY, 00644);
	if (efd < 0) {
		ret = -1;
		goto fulldl_free;
	}
	if ((pf = fdopen(efd, "w")) == NULL) {
		close(efd);
		ret = -1;
		goto fulldl_free;
	}

	if (smallfile && (cblen < 256) && (dblen < 65536) && (eblen < 65536)) {
		memset(&small_header, 0, sizeof(struct header_v21));
		memcpy(&small_header.magic, BSDIFF_HDR_MAGIC_V21, 8);

		/* in the future we may need to push first_block out further to squeeze
		 * something extra into the header */
		first_block = sizeof(struct header_v21);
		small_header.offset_to_first_block = first_block;
		small_header.control_length = cblen;
		small_header.diff_length = dblen;
		small_header.extra_length = eblen;
		small_header.old_file_length = oldsize;
		small_header.new_file_length = newsize;
		small_header.file_mode = new_stat.st_mode;
		small_header.file_owner = new_stat.st_uid;
		small_header.file_group = new_stat.st_gid;

		cblock_set_enc(&small_header.encoding, c_enc);
		dblock_set_enc(&small_header.encoding, d_enc);
		eblock_set_enc(&small_header.encoding, e_enc);
		encodings = small_header.encoding;

		if ((first_block + cblen + dblen + eblen > 0.90 * newsize) && (enc != BSDIFF_ENC_NONE)) { /* tune */
			memcpy(&small_header.magic, BSDIFF_HDR_FULLDL, 8);
			ret = 1;
			if (fwrite(&small_header, 8, 1, pf) != 1) {
				ret = -1;
				goto fulldl_close_free;
			}
			bsdiff_fulldl++;
			goto fulldl_close_free;
		}

		if (fwrite(&small_header, sizeof(struct header_v21), 1, pf) != 1) {
			ret = -1;
			goto fulldl_close_free;
		}
	} else {
		smallfile = 0;

		memset(&large_header, 0, sizeof(struct header_v20));
		memcpy(&large_header.magic, BSDIFF_HDR_MAGIC_V20, 8);

		/* in the future we may need to push this out further to squeeze
		 * something extra into the header */
		first_block = sizeof(struct header_v20);
		large_header.offset_to_first_block = first_block;
		large_header.control_length = cblen;
		large_header.diff_length = dblen;
		large_header.extra_length = eblen;
		large_header.old_file_length = oldsize;
		large_header.new_file_length = newsize;
		large_header.file_mode = new_stat.st_mode;
		large_header.file_owner = new_stat.st_uid;
		large_header.file_group = new_stat.st_gid;

		cblock_set_enc(&large_header.encoding, c_enc);
		dblock_set_enc(&large_header.encoding, d_enc);
		eblock_set_enc(&large_header.encoding, e_enc);
		encodings = large_header.encoding;

		if ((first_block + cblen + dblen + eblen > 0.90 * newsize) && (enc != BSDIFF_ENC_NONE)) { /* tune */
			memcpy(&large_header.magic, BSDIFF_HDR_FULLDL, 8);
			ret = 1;
			if (fwrite(&large_header, 8, 1, pf) != 1) {
				ret = -1;
				goto fulldl_close_free;
			}
			bsdiff_fulldl++;
			goto fulldl_close_free;
		}

		if (fwrite(&large_header, sizeof(struct header_v20), 1, pf) != 1) {
			ret = -1;
			goto fulldl_close_free;
		}
	}

	if (fwrite(cb, cblen, 1, pf) != 1) {
		ret = -1;
		goto fulldl_close_free;
	}
	if (dblen > 0 && fwrite(db, dblen, 1, pf) != 1) {
		ret = -1;
		goto fulldl_close_free;
	}
	if (eblen > 0 && fwrite(eb, eblen, 1, pf) != 1) {
		ret = -1;
		goto fulldl_close_free;
	}

	bsdiff_files++;
	bsdiff_newbytes += newsize;
	bsdiff_outputbytes += first_block + cblen + dblen + eblen;

	if (cblock_get_enc(encodings) == BSDIFF_ENC_NONE) {
		bsdiff_none++;
	}
	if (dblock_get_enc(encodings) == BSDIFF_ENC_NONE) {
		bsdiff_none++;
	}
	if (eblock_get_enc(encodings) == BSDIFF_ENC_NONE) {
		bsdiff_none++;
	}
	if (cblock_get_enc(encodings) == BSDIFF_ENC_GZIP) {
		bsdiff_gzip++;
	}
	if (dblock_get_enc(encodings) == BSDIFF_ENC_GZIP) {
		bsdiff_gzip++;
	}
	if (eblock_get_enc(encodings) == BSDIFF_ENC_GZIP) {
		bsdiff_gzip++;
	}
	if (cblock_get_enc(encodings) == BSDIFF_ENC_BZIP2) {
		bsdiff_bzip2++;
	}
	if (dblock_get_enc(encodings) == BSDIFF_ENC_BZIP2) {
		bsdiff_bzip2++;
	}
	if (eblock_get_enc(encodings) == BSDIFF_ENC_BZIP2) {
		bsdiff_bzip2++;
	}
	if (cblock_get_enc(encodings) == BSDIFF_ENC_XZ) {
		bsdiff_xz++;
	}
	if (dblock_get_enc(encodings) == BSDIFF_ENC_XZ) {
		bsdiff_xz++;
	}
	if (eblock_get_enc(encodings) == BSDIFF_ENC_XZ) {
		bsdiff_xz++;
	}
	if (dblock_get_enc(encodings) == BSDIFF_ENC_ZEROS) {
		bsdiff_zeros++;
	}
	if (eblock_get_enc(encodings) == BSDIFF_ENC_ZEROS) {
		bsdiff_zeros++;
	}

	ret = 0;

fulldl_close_free:
	if (fclose(pf)) {
		ret = -1;
	}
fulldl_free:
	/* Free the memory we used */
	munmap(old_data, oldsize);
	free(new_data);
	free(cb);
	free(db);
	free(eb);

	return ret;
}
