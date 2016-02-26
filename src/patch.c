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
    ("$FreeBSD: src/usr.bin/bsdiff/bspatch/bspatch.c,v 1.1 2005/08/06 01:59:06 cperciva Exp $");
#endif

#define _GNU_SOURCE
#include "config.h"

#include <sys/types.h>

#ifdef BSDIFF_WITH_BZIP2
#include <bzlib.h>
#endif

#include <err.h>

#ifdef BSDIFF_WITH_LZMA
#include <lzma.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/fs.h>
#include <assert.h>
#include <endian.h>
#include <errno.h>
#include <sys/mman.h>

#include "bsheader.h"

static inline int64_t offtin(u_char *buf)
{
	return le64toh(*((int64_t *)buf));
}

#ifdef BSDIFF_WITH_LZMA
/* xzfile is a provisional stdio-like interface to xz/lzma2-compressed data.
 * liblzma does not currently include this functionality. The interface is
 * read-only and only supports sequential access. */

typedef struct {
	/* in and out are the underlying buffers to be used with lzma_stream. */
	u_char in[BUFSIZ];
	u_char out[BUFSIZ];

	lzma_stream ls;
	FILE *f;

	/* read_out points to the first byte in out not yet consumed by an
	 * xzread call. read_out_len tracks the amount of data available in
	 * out beginning at read_out. */
	u_char *read_out;
	size_t read_out_len;

	/* Error and end-of-file indicators. */
	lzma_ret err;
	int eof;
} xzfile;

/* Initializes and returns a new xzfile pointer that will read from f. On
 * failure, returns NULL. If err is non-NULL, it will be set to indicate any
 * error that may have occurred. */
static xzfile *xzdopen(FILE *f, lzma_ret *err)
{
	xzfile *xzf;
	lzma_stream ls = LZMA_STREAM_INIT;
	uint64_t memlimit;

	if (!(xzf = malloc(sizeof(xzfile)))) {
		if (err) {
			*err = LZMA_MEM_ERROR;
		}
		return NULL;
	}

	xzf->ls = ls;
	xzf->f = f;

	xzf->read_out = xzf->out;
	xzf->read_out_len = 0;

	xzf->err = LZMA_OK;
	xzf->eof = 0;

	/* Use the same memory limits used by xzdec and xz. Use 40% of
	 * physical memory if 80MB or more, otherwise use 80% of physical
	 * memory if 80MB or less, otherwise use 80MB. If physical memory
	 * can't be determined, use 128MB. These limits should be sufficient
	 * for any decompression on any general-purpose system. */
	memlimit = 80 * 1024 * 1024;

	xzf->err = lzma_stream_decoder(&xzf->ls, memlimit,
				       LZMA_TELL_NO_CHECK |
					   LZMA_TELL_UNSUPPORTED_CHECK);
	if (xzf->err != LZMA_OK) {
		if (err) {
			*err = xzf->err;
		}
		free(xzf);
		return NULL;
	}

	if (err) {
		*err = xzf->err;
	}
	return xzf;
}

/* Closes an xzfile opened by xzopen, freeing all memory and closing all
 * files. Returns LZMA_OK normally, or LZMA_STREAM_END if fclose fails. */
static lzma_ret xzclose(xzfile *xzf)
{
	lzma_ret lzma_err = LZMA_OK;

	lzma_end(&xzf->ls);
	if (fclose(xzf->f) != 0) {
		lzma_err = LZMA_STREAM_END;
	}
	free(xzf);

	return lzma_err;
}

/* Reads len uncompressed bytes from xzf into buf. Returns the number of bytes
 * read, which may be less than len at the end of the file. Upon error, if
 * err is non-NULL, it will be set to an appropriate value, which will either
 * be a return value from lzma_code (with the exception of LZMA_STREAM_END,
 * which is remapped to LZMA_OK), or LZMA_STREAM_END to indicate an I/O error.
 */
static size_t xzread(xzfile *xzf, u_char *buf, size_t len, lzma_ret *err)
{
	lzma_action action = LZMA_RUN;
	size_t copylen;
	size_t nread = 0;

	*err = LZMA_OK;

	while (xzf->err == LZMA_OK && len > 0) {
		if (xzf->read_out_len == 0) {
			/* No unconsumed data is available, need to run
			 * lzma_code to decompress. */

			if (xzf->ls.avail_in == 0 && xzf->eof) {
				return 0;
			}
			if (xzf->ls.avail_in == 0 && !xzf->eof) {
				/* No input data available, need to read. */
				xzf->ls.next_in = xzf->in;
				xzf->ls.avail_in = fread(xzf->in, 1, BUFSIZ,
							 xzf->f);
				if (ferror(xzf->f)) {
					/* Map I/O errors to LZMA_STREAM_END. */
					xzf->err = LZMA_STREAM_END;
					*err = xzf->err;
					return 0;
				} else if (feof(xzf->f)) {
					xzf->eof = 1;
				}
			}

			/* Use the full output buffer. */
			xzf->ls.next_out = xzf->out;
			xzf->ls.avail_out = BUFSIZ;

			/* There must be something to decode. */
			if (xzf->ls.avail_in == 0) {
				xzf->err = LZMA_BUF_ERROR;
				*err = xzf->err;
				return 0;
			}

			/* LZMA_FINISH is not critical because
			 * LZMA_CONCATENATED is not in use. */
			if (xzf->eof) {
				action = LZMA_FINISH;
			}

			/* Run the decoder. */
			xzf->err = lzma_code(&xzf->ls, action);
			if (xzf->err == LZMA_STREAM_END) {
				xzf->eof = 1;
				xzf->err = LZMA_OK;
				/* if the stream ended, but no bytes were outputed.. we're at the end */
				if (xzf->ls.avail_out == BUFSIZ) {
					len = 0;
				}
			} else if (xzf->err != LZMA_OK) {
				*err = xzf->err;
				return 0;
			}

			/* Everything that was decoded is now available for
			 * reading into buf. */
			xzf->read_out = xzf->out;
			xzf->read_out_len = BUFSIZ - xzf->ls.avail_out;
		}

		/* Copy everything available up to len, and push some
		 * pointers. */
		copylen = xzf->read_out_len;
		if (copylen > len) {
			copylen = len;
		}
		memcpy(buf, xzf->read_out, copylen);
		nread += copylen;
		buf += copylen;
		len -= copylen;
		xzf->read_out += copylen;
		xzf->read_out_len -= copylen;
	}

	*err = xzf->err;
	return nread;
}
#endif /* BSDIFF_WITH_LZMA */

/* cfile is a uniform interface to read from maybe-compressed files. */

typedef struct {
	FILE *f; /* method = NONE, BZIP2, ZEROS */
	int fd;  /* method = BZIP2 */
	union {
#ifdef BSDIFF_WITH_BZIP2
		BZFILE *bz2; /* method = BZIP2 */
#endif
		gzFile gz;  /* method = GZIP */
#ifdef BSDIFF_WITH_LZMA
		xzfile *xz; /* method = XZ */
#endif
	} u;
	const char *tag;
	unsigned char method;
} cfile;

/* Opens a file at path, seeks to offset off, and prepares for reading using
 * the specified method in enum BSDIFF_ENCODINGS.  The tag is an identifier
 * for error reporting. */
static int cfopen(cfile *cf, const char *path, int64_t off,
		  const char *tag, unsigned char method)
{
#ifdef BSDIFF_WITH_BZIP2
	int bz2_err;
#endif
#ifdef BSDIFF_WITH_LZMA
	lzma_ret lzma_err;
#endif

	if (method == BSDIFF_ENC_NONE ||
	    method == BSDIFF_ENC_BZIP2 ||
	    method == BSDIFF_ENC_XZ ||
	    method == BSDIFF_ENC_ZEROS) {
		/* Use stdio for uncompressed files. The bzip interface also
		 * sits on top of a stdio FILE* but does not take "ownership"
		 * of the FILE*. The xz/lzma2 interface sits on top of a FILE*
		 * and does take ownership of the FILE*. */
		if ((cf->f = fopen(path, "rb")) == NULL) {
			return -1;
		}
		if ((fseeko(cf->f, off, SEEK_SET)) != 0) {
			fclose(cf->f);
			return -1;
		}
		if (method == BSDIFF_ENC_BZIP2) {
#ifdef BSDIFF_WITH_BZIP2
			if ((cf->u.bz2 = BZ2_bzReadOpen(&bz2_err, cf->f, 0, 0,
							NULL, 0)) == NULL) {
				fclose(cf->f);
				return -1;
			}
#else /*BSDIFF_WITHOUT_BZIP2*/
			fclose(cf->f);
			return -1;
#endif
		} else if (method == BSDIFF_ENC_XZ) {
#ifdef BSDIFF_WITH_LZMA
			if ((cf->u.xz = xzdopen(cf->f, &lzma_err)) == NULL) {
				fclose(cf->f);
				return -1;
			}
			/* cf->f belongs to the xzfile now, don't access it
			 * from here. */
			cf->f = NULL;
#else /* BSDIFF_WITHOUT_LZMA */
			fclose(cf->f);
			return -1;
#endif
		}
	} else if (method == BSDIFF_ENC_GZIP) {
		if ((cf->fd = open(path, O_RDONLY)) < 0) {
			return -1;
		}
		if (lseek(cf->fd, off, SEEK_SET) != off) {
			close(cf->fd);
			return -1;
		}
		if ((cf->u.gz = gzdopen(cf->fd, "rb")) == NULL) {
			close(cf->fd);
			return -1;
		}
	} else {
		return -1;
	}

	cf->tag = tag;
	cf->method = method;

	return 0;
}

static void cfclose(cfile *cf)
{
#ifdef BSDIFF_WITH_BZIP2
	int bz2_err;
#endif
	int gz_err;
#ifdef BSDIFF_WITH_LZMA
	lzma_ret lzma_err;
#endif

	if (cf->method == BSDIFF_ENC_NONE ||
	    cf->method == BSDIFF_ENC_BZIP2 ||
	    cf->method == BSDIFF_ENC_ZEROS) {
		if (cf->method == BSDIFF_ENC_BZIP2) {
#ifdef BSDIFF_WITH_BZIP2
			bz2_err = BZ_OK;
			BZ2_bzReadClose(&bz2_err, cf->u.bz2);
#else /*BSDIFF_WITHOUT_BZIP2*/
			return;
#endif
		}
		if (fclose(cf->f) != 0) {
			return;
		}
	} else if (cf->method == BSDIFF_ENC_GZIP) {
		if ((gz_err = gzclose(cf->u.gz)) != Z_OK) {
			return;
		}
	} else if (cf->method == BSDIFF_ENC_XZ) {
#ifdef BSDIFF_WITH_LZMA
		if ((lzma_err = xzclose(cf->u.xz)) != LZMA_OK) {
			return;
		}
#else /* BSDIFF_WITHOUT_LZMA */
		return;
#endif
	}
}

static int cfread(cfile *cf, u_char *buf, size_t len, int block, uint64_t *zeros)
{
	size_t nread;
#ifdef BSDIFF_WITH_BZIP2
	int bz2_err;
#endif
	int gz_err;
#ifdef BSDIFF_WITH_LZMA
	lzma_ret lzma_err;
#endif

	if (len <= 0) {
		return 0;
	}

	if (cf->method == BSDIFF_ENC_NONE) {
		nread = fread(buf, 1, len, cf->f);
		if (nread != len) {
			return -1;
		}
	} else if (cf->method == BSDIFF_ENC_BZIP2) {
#ifdef BSDIFF_WITH_BZIP2
		bz2_err = BZ_OK;
		if ((nread = BZ2_bzRead(&bz2_err, cf->u.bz2, buf, len)) != len) {
			return -1;
		}
#else /*BSDIFF_WITHOUT_BZIP2*/
		return -1;
#endif
	} else if (cf->method == BSDIFF_ENC_GZIP) {
		if ((nread = gzread(cf->u.gz, buf, len)) != len) {
			gz_err = Z_OK;
			gzerror(cf->u.gz, &gz_err);
			return -1;
		}
	} else if (cf->method == BSDIFF_ENC_XZ) {
#ifdef BSDIFF_WITH_LZMA
		if ((nread = xzread(cf->u.xz, buf, len, &lzma_err)) != len) {
			return -1;
		}
#else /* BSDIFF_WITH_LZMA */
		return -1;
#endif
	} else if ((cf->method == BSDIFF_ENC_ZEROS) &&
		   ((block == BSDIFF_BLOCK_DIFF) || (block == BSDIFF_BLOCK_EXTRA))) {
		if (*zeros == ULONG_MAX) {
			uint64_t tmp;
			nread = fread(&tmp, sizeof(uint64_t), 1, cf->f);
			if (nread != 1) {
				return -1;
			}
			*zeros = tmp;
		}
		if (*zeros < len) {
			return -1;
		}
		if (*zeros >= len) {
			memset(buf, 0, len);
			*zeros -= len;
		}
	} else {
		return -1;
	}

	return 0;
}

static int check_header(FILE *f, enc_flags_t encoding,
			off_t control_length, off_t diff_length, off_t extra_length,
			off_t old_file_length, off_t new_file_length, off_t offset_to_first_block)
{
	off_t patchsize;

	/* Read lengths from header */
	if (control_length < 0 || diff_length < 0 || extra_length < 0) {
		return -1;
	}
	if (old_file_length < 0 || new_file_length < 0) {
		return -1;
	}
	if (fseeko(f, 0, SEEK_END) != 0 || (patchsize = ftello(f)) < 0) {
		return -1;
	}
	if (patchsize != offset_to_first_block + control_length + diff_length + extra_length) {
		return -1;
	}

	if (cblock_get_enc(encoding) == BSDIFF_ENC_ZEROS) {
		return -1;
	}
	return 0;
}

static int open_bsdiff_blocks(cfile *cf, cfile *df, cfile *ef, char *deltafile,
			      off_t control_length, off_t diff_length,
			      off_t offset_to_first_block, enc_flags_t encoding)
{
	int ret;

	ret = cfopen(cf, deltafile, offset_to_first_block,
		     "control", cblock_get_enc(encoding));
	if (ret < 0) {
		return -1;
	}
	ret = cfopen(df, deltafile, offset_to_first_block + control_length,
		     "diff", dblock_get_enc(encoding));
	if (ret < 0) {
		cfclose(cf);
		return -1;
	}
	ret = cfopen(ef, deltafile, offset_to_first_block + control_length + diff_length,
		     "extra", eblock_get_enc(encoding));
	if (ret < 0) {
		cfclose(cf);
		cfclose(df);
		return -1;
	}
	return 0;
}

static int read_file(char *filename, unsigned char **data, off_t len)
{
	int fd;
	struct stat sb;

	fd = open(filename, O_RDONLY, 0);
	if (fd < 0) {
		return -1;
	}

	if (fstat(fd, &sb) != 0) {
		close(fd);
		return -1;
	}

	if (len != sb.st_size) {
		close(fd);
		return -1;
	}

	*data = mmap(NULL, sb.st_size, PROT_READ, MAP_SHARED, fd, 0);
	close(fd);

	if (*data == MAP_FAILED) {
		*data = NULL;
		return -1;
	}

	return 0;
}

static int apply_delta_v2(int subver, FILE *f,
			  char *old_filename, char *new_filename, char *delta_filename)
{
	cfile cf, df, ef;
	unsigned char *old_data = NULL, *new_data;
	unsigned char buf[8];
	off_t oldpos, newpos;
	int64_t ctrl[3];
	int i, ret, fd;
	off_t data_offset;
	off_t ctrllen, difflen, extralen;
	off_t oldsize, newsize;
	mode_t mode;
	uid_t uid;
	gid_t gid;
	enc_flags_t encoding;
	uint64_t d_zeros = ULONG_MAX;
	uint64_t e_zeros = ULONG_MAX;

	if (subver == 0) {
		struct header_v20 header;
		if (fread(&header, sizeof(struct header_v20), 1, f) < 1) {
			return -1;
		}
		data_offset = header.offset_to_first_block;
		ctrllen = header.control_length;
		difflen = header.diff_length;
		extralen = header.extra_length;
		oldsize = header.old_file_length;
		newsize = header.new_file_length;
		mode = header.file_mode;
		uid = header.file_owner;
		gid = header.file_group;
		encoding = header.encoding;
	} else if (subver == 1) {
		struct header_v21 header;
		if (fread(&header, sizeof(struct header_v21), 1, f) < 1) {
			return -1;
		}
		data_offset = header.offset_to_first_block;
		ctrllen = header.control_length;
		difflen = header.diff_length;
		extralen = header.extra_length;
		oldsize = header.old_file_length;
		newsize = header.new_file_length;
		mode = header.file_mode;
		uid = header.file_owner;
		gid = header.file_group;
		encoding = header.encoding;
	} else {
		return -1;
	}

	if ((ret = check_header(f, encoding,
				ctrllen, difflen, extralen,
				oldsize, newsize, data_offset)) < 0) {
		return ret;
	}

	if ((ret = open_bsdiff_blocks(&cf, &df, &ef, delta_filename,
				      ctrllen, difflen, data_offset, encoding)) < 0) {
		return ret;
	}

	ret = read_file(old_filename, &old_data, oldsize);
	if (ret < 0) {
		goto preperror;
	}

	if (newsize > BSDIFF_MAX_FILESZ) {
		munmap(old_data, oldsize);
		ret = -1;
		goto preperror;
	}

	/* Allocate newsize+1 bytes instead of newsize bytes to ensure
	   that we never try to malloc(0) and get a NULL pointer */
	if ((new_data = malloc(newsize + 1)) == NULL) {
		munmap(old_data, oldsize);
		ret = -1;
		goto preperror;
	}
	memset(new_data, 0, newsize + 1);

	oldpos = 0;
	newpos = 0;
	while (newpos < newsize) {
		/* Read control data:
		 *   ctrl[0] == offset into diff block
		 *   ctrl[1] == offset into extra block
		 *   ctrl[2] == adjustment factor for offset into old_data
		 *
		 * The three control block words manage reads of the diff,
		 * extra and old_data so that those three sources can be
		 * combined into new_data.  ctrl[2] in particular may cause
		 * oldpos to jump forward AND backward in order to allow
		 * copies of the original file content rather than using
		 * diff or extra content.
		 */
		for (i = 0; i <= 2; i++) {
			ret = cfread(&cf, buf, 8, BSDIFF_BLOCK_CONTROL, NULL);
			if (ret < 0) {
				goto readerror;
			}
			ctrl[i] = offtin(buf);
		}

		/* Sanity-check */
		if (newpos + ctrl[0] > newsize || ctrl[0] < 0 || newpos + ctrl[0] < 0) {
			ret = -1;
			goto readerror;
		}

		/* Read diff string */
		ret = cfread(&df, new_data + newpos, ctrl[0], BSDIFF_BLOCK_DIFF, &d_zeros);
		if (ret < 0) {
			goto readerror;
		}

		/* Add old data to diff string */
		for (i = 0; i < ctrl[0]; i++) {
			if ((oldpos + i >= 0) && (oldpos + i < oldsize)) {
				new_data[newpos + i] += old_data[oldpos + i];
			}
		}

		/* Adjust pointers */
		newpos += ctrl[0];
		oldpos += ctrl[0];

		/* Sanity-check */
		if (newpos + ctrl[1] > newsize || ctrl[1] < 0 || newpos + ctrl[1] < 0) {
			ret = -1;
			goto readerror;
		}
		if (oldpos + ctrl[2] > oldsize || oldpos + ctrl[2] < 0) {
			ret = -1;
			goto readerror;
		}

		/* Read extra string */
		ret = cfread(&ef, new_data + newpos, ctrl[1], BSDIFF_BLOCK_EXTRA, &e_zeros);
		if (ret < 0) {
			goto readerror;
		}

		/* Adjust pointers */
		newpos += ctrl[1];
		oldpos += ctrl[2];
	}

	/* Clean up the readers */
	cfclose(&cf);
	cfclose(&df);
	cfclose(&ef);

	/* Write the new file */
	fd = open(new_filename, O_CREAT | O_EXCL | O_WRONLY, 00644);
	if (fd < 0) {
		ret = -1;
		goto writeerror;
	}

	if (write(fd, new_data, newsize) != newsize) {
		unlink(new_filename);
		close(fd);
		ret = -1;
		goto writeerror;
	}

	close(fd);

	ret = chown(new_filename, uid, gid);
	if (ret < 0) {
		free(new_data);
		return ret;
	}

	ret = chmod(new_filename, mode);
	if (ret < 0) {
		free(new_data);
		return ret;
	}

writeerror:
	free(new_data);
	munmap(old_data, oldsize);
	return ret;

readerror:
	free(new_data);
	munmap(old_data, oldsize);
preperror:
	cfclose(&cf);
	cfclose(&df);
	cfclose(&ef);
	return ret;
}

int apply_bsdiff_delta(char *oldfile, char *newfile, char *deltafile)
{
	FILE *f;
	unsigned char magic[8];
	struct stat sb;
	int ret;

	/* Open patch file */
	f = fopen(deltafile, "rb");
	if (!f) {
		return -1;
	}

	if (stat(deltafile, &sb) == -1) {
		ret = -1;
		goto error;
	}
	/* Make sure delta file is at least big enough to have a header */
	if (sb.st_size < 8) {
		ret = -2;
		goto error;
	}

	/* Read header magic */
	if (fread(&magic, 8, 1, f) < 1) {
		ret = -1;
		goto error;
	}

	/* Deal with different header types */
	if (memcmp(&magic, BSDIFF_HDR_MAGIC_V20, 8) == 0) {
		rewind(f);
		ret = apply_delta_v2(0, f, oldfile, newfile, deltafile);
		if (ret != 0) {
			goto error;
		}
	} else if (memcmp(&magic, BSDIFF_HDR_MAGIC_V21, 8) == 0) {
		rewind(f);
		ret = apply_delta_v2(1, f, oldfile, newfile, deltafile);
		if (ret != 0) {
			goto error;
		}
	} else if (memcmp(&magic, BSDIFF_HDR_DIR_V20, 8) == 0) {
		ret = -1;
	} else if (memcmp(&magic, BSDIFF_HDR_FULLDL, 8) == 0) {
		ret = -2;
	} else {
		ret = -1;
	}
error:
	fclose(f);
	return ret;
}
