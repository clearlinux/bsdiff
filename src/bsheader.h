#ifndef __INCLUDE_GUARD_BSHEADER_H
#define __INCLUDE_GUARD_BSHEADER_H

#include <stdint.h>

#include "bsdiff.h"

/* process no more than 512MB sized files */
#define BSDIFF_MAX_FILESZ 512 * 1024 * 1024

/* used for suffix sort in bsdiff */
#define QSUF_BUCKET_SIZE 256

enum BSDIFF_BLOCKS {
	BSDIFF_BLOCK_CONTROL,
	BSDIFF_BLOCK_DIFF,
	BSDIFF_BLOCK_EXTRA,
};

/* encodings bitfield */
typedef union {
	struct {
		uint16_t cblk_none : 1; /* control block enc's*/
		uint16_t cblk_bzip2 : 1;
		uint16_t cblk_gzip : 1;
		uint16_t cblk_xz : 1;
		uint16_t : 1;		/* unused */
		uint16_t dblk_none : 1; /* diff block enc's */
		uint16_t dblk_bzip2 : 1;
		uint16_t dblk_gzip : 1;
		uint16_t dblk_xz : 1;
		uint16_t dblk_zeros : 1;
		uint16_t eblk_none : 1; /* extra block enc's */
		uint16_t eblk_bzip2 : 1;
		uint16_t eblk_gzip : 1;
		uint16_t eblk_xz : 1;
		uint16_t eblk_zeros : 1;
		uint16_t : 1; /* unused */
	} ordered;
	uint16_t raw;
} enc_flags_t;

/***************************************************************
 * v2.x headers
 ***************************************************************/

/* cleaned up typing and optimized fields */
#define BSDIFF_HDR_MAGIC_V20 "BSDIFF4U"
/* directory header: uses only file_{mode|owner|group} */
#define BSDIFF_HDR_DIR_V20 "DIR_V20U"
/* do a full download instead of reading a bsdiff, only magic field used */
#define BSDIFF_HDR_FULLDL "FULLV20U"
struct header_v20 {
	unsigned char magic[8];
	uint8_t offset_to_first_block; /* ~= header length */
	uint32_t control_length;
	uint64_t diff_length;
	uint64_t extra_length;
	uint64_t old_file_length;
	uint64_t new_file_length;
	uint64_t mtime; /* unused */
	uint32_t file_mode;
	uint32_t file_owner;
	uint32_t file_group;

	/*	Supported encodings: uncompressed, bzip2, gzip, xz, zeros */
	enc_flags_t encoding;
} __attribute__((__packed__));

/* optimized for small files */
#define BSDIFF_HDR_MAGIC_V21 "BSDIFF4V"
struct header_v21 {
	unsigned char magic[8];
	uint8_t offset_to_first_block; /* ~= header length */
	uint8_t control_length;
	uint16_t diff_length;
	uint16_t extra_length;
	uint16_t old_file_length;
	uint16_t new_file_length;
	uint32_t file_mode;
	uint32_t file_owner;
	uint32_t file_group;

	/*	Supported encodings: uncompressed, bzip2, gzip, xz, zeros */
	enc_flags_t encoding;
} __attribute__((__packed__));

static inline void cblock_set_enc(enc_flags_t *enc, int method)
{
	if (method == BSDIFF_ENC_NONE) {
		enc->ordered.cblk_none = 1;
	} else if (method == BSDIFF_ENC_BZIP2) {
		enc->ordered.cblk_bzip2 = 1;
	} else if (method == BSDIFF_ENC_GZIP) {
		enc->ordered.cblk_gzip = 1;
	} else if (method == BSDIFF_ENC_XZ) {
		enc->ordered.cblk_xz = 1;
	}
}

static inline void dblock_set_enc(enc_flags_t *enc, int method)
{
	if (method == BSDIFF_ENC_NONE) {
		enc->ordered.dblk_none = 1;
	} else if (method == BSDIFF_ENC_BZIP2) {
		enc->ordered.dblk_bzip2 = 1;
	} else if (method == BSDIFF_ENC_GZIP) {
		enc->ordered.dblk_gzip = 1;
	} else if (method == BSDIFF_ENC_XZ) {
		enc->ordered.dblk_xz = 1;
	} else if (method == BSDIFF_ENC_ZEROS) {
		enc->ordered.dblk_zeros = 1;
	}
}

static inline void eblock_set_enc(enc_flags_t *enc, int method)
{
	if (method == BSDIFF_ENC_NONE) {
		enc->ordered.eblk_none = 1;
	} else if (method == BSDIFF_ENC_BZIP2) {
		enc->ordered.eblk_bzip2 = 1;
	} else if (method == BSDIFF_ENC_GZIP) {
		enc->ordered.eblk_gzip = 1;
	} else if (method == BSDIFF_ENC_XZ) {
		enc->ordered.eblk_xz = 1;
	} else if (method == BSDIFF_ENC_ZEROS) {
		enc->ordered.eblk_zeros = 1;
	}
}

static inline int cblock_get_enc(enc_flags_t enc)
{
	if (enc.ordered.cblk_none) {
		return BSDIFF_ENC_NONE;
	} else if (enc.ordered.cblk_bzip2) {
		return BSDIFF_ENC_BZIP2;
	} else if (enc.ordered.cblk_gzip) {
		return BSDIFF_ENC_GZIP;
	} else if (enc.ordered.cblk_xz) {
		return BSDIFF_ENC_XZ;
	} else {
		return BSDIFF_ENC_ANY;
	}
}

static inline int dblock_get_enc(enc_flags_t enc)
{
	if (enc.ordered.dblk_none) {
		return BSDIFF_ENC_NONE;
	} else if (enc.ordered.dblk_bzip2) {
		return BSDIFF_ENC_BZIP2;
	} else if (enc.ordered.dblk_gzip) {
		return BSDIFF_ENC_GZIP;
	} else if (enc.ordered.dblk_xz) {
		return BSDIFF_ENC_XZ;
	} else if (enc.ordered.dblk_zeros) {
		return BSDIFF_ENC_ZEROS;
	} else {
		return BSDIFF_ENC_ANY;
	}
}

static inline int eblock_get_enc(enc_flags_t enc)
{
	if (enc.ordered.eblk_none) {
		return BSDIFF_ENC_NONE;
	} else if (enc.ordered.eblk_bzip2) {
		return BSDIFF_ENC_BZIP2;
	} else if (enc.ordered.eblk_gzip) {
		return BSDIFF_ENC_GZIP;
	} else if (enc.ordered.eblk_xz) {
		return BSDIFF_ENC_XZ;
	} else if (enc.ordered.eblk_zeros) {
		return BSDIFF_ENC_ZEROS;
	} else {
		return BSDIFF_ENC_ANY;
	}
}

#endif
