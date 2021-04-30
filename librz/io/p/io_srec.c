// SPDX-FileCopyrightText: 2021 Manolache183 <manolache.alexandru8@gmail.com>
// SPDX-License-Identifier: LGPL-3.0-only

/*
*** SREC format description : every line follows this pattern
	S / Type / Bytecount / Address / Data / Checksum
<<<<<<< HEAD
	There are a bunch of types, from which one (S4) is reserved
// source : https://en.wikipedia.org/wiki/SREC_(file_format)
**** example records
=======

	There are a bunch of types, from which one (S4) is reserved

// source : https://en.wikipedia.org/wiki/SREC_(file_format)

**** example records

>>>>>>> b73a89fe448a0024c8e21dac562927aa01619d84
S00F000068656C6C6F202020202000003C
S11F00007C0802A6900100049421FFF07C6C1B787C8C23783C6000003863000026
S11F001C4BFFFFE5398000007D83637880010014382100107C0803A64E800020E9
S5030003F9
S9030000FC
*/

#include <rz_io.h>
#include <rz_lib.h>

#define NMAX             100
#define SREC_PATH_PREFIX "srec://"

// struct RzSREC : holds sparse buffer + its own fd, for internal management
typedef struct {
	st32 fd;
	RzBuffer *rbuf;
} RzSREC;

<<<<<<< HEAD
static st32 write_record_S3(FILE *fd, ut32 start_addr, ut8 *b, ut8 size) {
	ut8 recordSize = 4 + size + 1;

	ut8 cks = recordSize;
	cks += start_addr & 0xff;
	cks += (start_addr >> 8) & 0xff;
	cks += (start_addr >> 16) & 0xff;
	cks += start_addr >> 24;
	
	fprintf(fd, "S3%02x%08x", recordSize, start_addr);
	
	for (int j = 0; j < size; j++) {
		cks += b[j];
		fprintf(fd, "%02x", b[j]);
	}

	fprintf(fd, "%02x\n", cks);

	return 1;
}

static st32 __write(RzIO *io, RzIODesc *fd, const ut8 *buf, st32 count) {
	
=======
static st32 fw04b(FILE *fd, ut16 eaddr);
static st32 fwblock(FILE *fd, ut8 *b, ut32 start_addr, ut16 size);

static st32 __write(RzIO *io, RzIODesc *fd, const ut8 *buf, st32 count) {
>>>>>>> b73a89fe448a0024c8e21dac562927aa01619d84
	const char *pathname;
	FILE *out;
	RzSREC *rih;
	RzBufferSparse *rbs;
	RzListIter *iter;

	if (!fd || !fd->data || (fd->perm & RZ_PERM_W) == 0 || count <= 0) {
		return -1;
	}
<<<<<<< HEAD
	
	rih = fd->data;
	pathname = fd->name + 7;
	out = rz_sys_fopen(pathname, "w");

	// random starting record
	fprintf(out, "S00F000068656C6C6F202020202000003C\n");

	
=======
	rih = fd->data;
	pathname = fd->name + 7;
	out = rz_sys_fopen(pathname, "w");
>>>>>>> b73a89fe448a0024c8e21dac562927aa01619d84
	if (!out) {
		eprintf("Cannot open '%s' for writing\n", pathname);
		return -1;
	}
<<<<<<< HEAD
	
=======
>>>>>>> b73a89fe448a0024c8e21dac562927aa01619d84
	/* mem write */
	if (rz_buf_write_at(rih->rbuf, io->off, buf, count) != count) {
		eprintf("srec:write(): sparse write failed\n");
		fclose(out);
		return -1;
	}
<<<<<<< HEAD
	
	rz_buf_seek(rih->rbuf, count, RZ_BUF_CUR);

	RzList *nonempty = rz_buf_nonempty_list(rih->rbuf);
	
	rz_list_foreach (nonempty, iter, rbs) {
		const ut8 RecordSize = 64; //bytes per record

		for (ut32 rbsOffset = 0; rbsOffset < rbs->size; rbsOffset += RecordSize) {
			const ut32 address = rbs->from + rbsOffset;
			ut8 bytesInRecord = RecordSize;
			if (rbsOffset + RecordSize > rbs->size) {
				bytesInRecord = rbs->size - rbsOffset;
			}
			write_record_S3(out, address, rbs->data + rbsOffset, bytesInRecord);
=======
	rz_buf_seek(rih->rbuf, count, RZ_BUF_CUR);

	/* disk write : process each sparse chunk */
	// TODO : sort addresses + check overlap?
	RzList *nonempty = rz_buf_nonempty_list(rih->rbuf);
	rz_list_foreach (nonempty, iter, rbs) {
		ut16 addl0 = rbs->from & 0xffff;
		ut16 addh0 = rbs->from >> 16;
		ut16 addh1 = rbs->to >> 16;
		ut16 tsiz = 0;
		if (rbs->size == 0) {
			continue;
		}

		if (addh0 != addh1) {
			// we cross a 64k boundary, so write in two steps
			if (fw04b(out, addh0) < 0) {
				eprintf("srec:write: file error\n");
				rz_list_free(nonempty);
				fclose(out);
				return -1;
			}
			tsiz = -addl0;
			addl0 = 0;
			if (fwblock(out, rbs->data, rbs->from, tsiz)) {
				eprintf("srec:fwblock error\n");
				rz_list_free(nonempty);
				fclose(out);
				return -1;
			}
		}
		if (fw04b(out, addh1) < 0) {
			eprintf("srec:write: file error\n");
			rz_list_free(nonempty);
			fclose(out);
			return -1;
		}
		if (fwblock(out, rbs->data + tsiz, (addh1 << 16) | addl0, rbs->size - tsiz)) {
			eprintf("srec:fwblock error\n");
			rz_list_free(nonempty);
			fclose(out);
			return -1;
>>>>>>> b73a89fe448a0024c8e21dac562927aa01619d84
		}
	} // list_foreach

	rz_list_free(nonempty);
<<<<<<< HEAD
	fprintf(out, "S9030000FC\n");
=======
	fprintf(out, ":00000001FF\n");
>>>>>>> b73a89fe448a0024c8e21dac562927aa01619d84
	fclose(out);
	out = NULL;
	return 0;
}

<<<<<<< HEAD
=======
// write contiguous block of data to file; ret 0 if ok
// max 65535 bytes;
static st32 fwblock(FILE *fd, ut8 *b, ut32 start_addr, ut16 size) {
	ut8 cks;
	char linebuf[80];
	ut16 last_addr;
	st32 j;
	ut32 i; // has to be bigger than size !

	if (size < 1 || !fd || !b) {
		return -1;
	}

	for (i = 0; (i + 0x10) < size; i += 0x10) {
		cks = 0x10;
		cks += (i + start_addr) >> 8;
		cks += (i + start_addr);
		for (j = 0; j < 0x10; j++) {
			cks += b[j];
		}
		cks = 0 - cks;
		if (fprintf(fd, ":10%04x00%02x%02x%02x%02x%02x%02x%02x"
				"%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
			    (i + start_addr) & 0xffff, b[0], b[1], b[2], b[3], b[4], b[5], b[6],
			    b[7], b[8], b[9], b[10], b[11], b[12], b[13],
			    b[14], b[15], cks) < 0) {
			return -1;
		}
		start_addr += 0x10;
		b += 0x10;
		if ((start_addr & 0xffff) < 0x10) {
			// addr rollover: write ext address record
			if (fw04b(fd, start_addr >> 16) < 0) {
				return -1;
			}
		}
	}
	if (i == size) {
		return 0;
	}
	// write crumbs
	last_addr = i + start_addr;
	cks = -last_addr;
	cks -= last_addr >> 8;
	for (j = 0; i < size; i++, j++) {
		cks -= b[j];
		sprintf(linebuf + (2 * j), "%02X", b[j]);
	}
	cks -= j;

	if (fprintf(fd, ":%02X%04X00%.*s%02X\n", j, last_addr, 2 * j, linebuf, cks) < 0) {
		return -1;
	}
	return 0;
}

static st32 fw04b(FILE *fd, ut16 eaddr) {
	ut8 cks = 0 - (6 + (eaddr >> 8) + (eaddr & 0xff));
	return fprintf(fd, ":02000004%04X%02X\n", eaddr, cks);
}

>>>>>>> b73a89fe448a0024c8e21dac562927aa01619d84
static st32 __read(RzIO *io, RzIODesc *fd, ut8 *buf, st32 count) {
	if (!fd || !fd->data || (count <= 0)) {
		return -1;
	}
	RzSREC *rih = fd->data;
	memset(buf, io->Oxff, count);
	st32 r = rz_buf_read_at(rih->rbuf, io->off, buf, count);
	if (r >= 0) {
		rz_buf_seek(rih->rbuf, r, RZ_BUF_CUR);
	}
<<<<<<< HEAD

=======
>>>>>>> b73a89fe448a0024c8e21dac562927aa01619d84
	return r;
}

static st32 __close(RzIODesc *fd) {
	if (!fd || !fd->data) {
		return -1;
	}
	RzSREC *rih = fd->data;
	rz_buf_free(rih->rbuf);
	free(rih);
	fd->data = NULL;
	return 0;
}

static ut64 __lseek(struct rz_io_t *io, RzIODesc *fd, ut64 offset, st32 whence) {
	RzSREC *rih;
	if (!fd || !fd->data) {
		return -1;
	}
	rih = fd->data;
	io->off = rz_buf_seek(rih->rbuf, offset, whence);
	return io->off;
}

static bool __plugin_open(RzIO *io, const char *pathname, bool many) {
	return (!strncmp(pathname, SREC_PATH_PREFIX, strlen(SREC_PATH_PREFIX)));
}

// parsing function
static bool SREC_parse(RzBuffer *rbuf, char *str) {
	ut8 *sec_tmp;
	ut32 sec_start = 0; // addr for next section write
	ut32 segreg = 0; // basis for addr fields
	ut32 addr_tmp = 0; // addr for record
	ut16 next_addr = 0; // for checking if records are sequential
	char *eol;
	ut8 cksum;
	st32 bc = 0, byte, i, l;
	char type; // 1 digit
	// ugly macro to prevent an overflow of rz_buf_write_at() len
#define SEC_MAX (sec_size < ST32_MAX) ? sec_size : ST32_MAX
	ut32 sec_size = 0;
	const st32 sec_count = UT16_MAX;
	sec_tmp = calloc(1, sec_count);
	if (!sec_tmp) {
		goto fail;
	}
	do {

		l = sscanf(str, "S%c%02x", &type, &bc);
		if (l != 2) {
			eprintf("Invalid data in SREC file (%.*s)\n", 80, str);
			goto fail;
		}

		bc &= 0xff;
		type &= 0xff;

		// format: S / Type / Byte_Count / Adress / Data / Checksum
		// first we tackle the 16bit adress cases
		// then the 24 bit
		// then the 32 bit

		switch (type) {
<<<<<<< HEAD
		case '0': // 16 bit adress, with header instead of data field
=======
		case 0: // 16 bit adress, with header instead of data field
>>>>>>> b73a89fe448a0024c8e21dac562927aa01619d84

			// S / 0 / (addres+data+checksum) bytes / 0000 / header / checksum

			l = sscanf(str + 4, "%04x", &addr_tmp);
			addr_tmp &= 0xffff;

			eol = strchr(str + 1, 'S');
			if (eol) {
				*eol = 0;
			}

			cksum = bc;
			cksum += addr_tmp & 0xff;
			cksum += addr_tmp >> 8;

			if ((next_addr != addr_tmp) || ((sec_size + bc) > SEC_MAX)) {
				// previous block is not contiguous, or
				// section buffer is full => write a sparse chunk
				if (sec_size && sec_size < UT16_MAX) {
					if (rz_buf_write_at(rbuf, sec_start, sec_tmp, (st32)sec_size) != sec_size) {
						eprintf("sparse buffer problem, giving up\n");
						goto fail;
					}
				}
				// advance cursor, reset section
				sec_start = segreg + addr_tmp;
				next_addr = addr_tmp;
				sec_size = 0;
			}

			for (i = 0; i < bc - 3; i++) {
				if (sscanf(str + 8 + (i * 2), "%02x", &byte) != 1) {
					eprintf("unparsable data !\n");
					goto fail;
				}
				if (sec_size + i < sec_count) {
					sec_tmp[sec_size + i] = (ut8)byte & 0xff;
				}
				cksum += byte;
			}
			sec_size += bc;
			next_addr += bc;
			if (eol) {
				// checksum
				if (sscanf(str + 8 + (i * 2), "%02x", &byte) != 1) {
					eprintf("unparsable data !\n");
					goto fail;
				}
				cksum = ~cksum; //this might be useless cause cksum is ut8
				if (cksum != byte) {
					eprintf("Checksum failed (got %02x expected %02x)\n", byte, cksum);
					goto fail;
				}
				*eol = 'S';
			}
			str = eol;
			break;

<<<<<<< HEAD
		case '1': // 16 bit adress, with data field!, kinda same with S0
=======
		case 1: // 16 bit adress, with data field!, kinda same with S0
>>>>>>> b73a89fe448a0024c8e21dac562927aa01619d84

			// S / 1 / (addres+data+checksum) bytes / 0000 / data / checksum
			l = sscanf(str + 4, "%04x", &addr_tmp);
			addr_tmp &= 0xffff;

			eol = strchr(str + 1, 'S');
			if (eol) {
				*eol = 0;
			}

			cksum = bc;
			cksum += addr_tmp & 0xff;
			cksum += addr_tmp >> 8;

			if ((next_addr != addr_tmp) || ((sec_size + bc) > SEC_MAX)) {
				// previous block is not contiguous, or
				// section buffer is full => write a sparse chunk
				if (sec_size && sec_size < UT16_MAX) {
					if (rz_buf_write_at(rbuf, sec_start, sec_tmp, (st32)sec_size) != sec_size) {
						eprintf("sparse buffer problem, giving up\n");
						goto fail;
					}
				}
				// advance cursor, reset section
				sec_start = segreg + addr_tmp;
				next_addr = addr_tmp;
				sec_size = 0;
			}

			for (i = 0; i < bc - 3; i++) {
				if (sscanf(str + 8 + (i * 2), "%02x", &byte) != 1) {
					eprintf("unparsable data !\n");
					goto fail;
				}
				if (sec_size + i < sec_count) {
					sec_tmp[sec_size + i] = (ut8)byte & 0xff;
				}
				cksum += byte;
			}
			sec_size += bc;
			next_addr += bc;
			if (eol) {
				// checksum
				if (sscanf(str + 8 + (i * 2), "%02x", &byte) != 1) {
					eprintf("unparsable data !\n");
					goto fail;
				}
				cksum = ~cksum;
				if (cksum != byte) {
					eprintf("Checksum failed (got %02x expected %02x)\n", byte, cksum);
					goto fail;
				}
				*eol = 'S';
			}
			str = eol;
			break;

<<<<<<< HEAD
		case '5': // optional, the adress field is a 16bit counter of S1/S2/S3 records
=======
		case 5: // optional, the adress field is a 16bit counter of S1/S2/S3 records
>>>>>>> b73a89fe448a0024c8e21dac562927aa01619d84

			// S / 5 / (counter+checksum) bytes (3)/ counter / - / checksum

			l = sscanf(str + 4, "%04x", &addr_tmp);
			addr_tmp &= 0xffff;

			eol = strchr(str + 1, 'S');
			if (eol) {
				*eol = 0;
			}

			cksum = bc;
			cksum += addr_tmp & 0xff;
			cksum += addr_tmp >> 8;

			if ((next_addr != addr_tmp) || ((sec_size + bc) > SEC_MAX)) {
				// previous block is not contiguous, or
				// section buffer is full => write a sparse chunk
				if (sec_size && sec_size < UT16_MAX) {
					if (rz_buf_write_at(rbuf, sec_start, sec_tmp, (st32)sec_size) != sec_size) {
						eprintf("sparse buffer problem, giving up\n");
						goto fail;
					}
				}
				// advance cursor, reset section
				sec_start = segreg + addr_tmp;
				next_addr = addr_tmp;
				sec_size = 0;
			}

			sec_size += bc;
			next_addr += bc;
			if (eol) {
				// checksum
				if (sscanf(str + 8, "%02x", &byte) != 1) {
					eprintf("unparsable data !\n");
					goto fail;
				}
				cksum = ~cksum;
				if (cksum != byte) {
					eprintf("Checksum failed (got %02x expected %02x)\n", byte, cksum);
					goto fail;
				}
				*eol = 'S';
			}
			str = eol;
			break;

<<<<<<< HEAD
		case '9': // a lot like S5
=======
		case 9: // a lot like S5
>>>>>>> b73a89fe448a0024c8e21dac562927aa01619d84
			// S / 9 / (counter+checksum) bytes (3)/ adress / - / checksum

			l = sscanf(str + 4, "%04x", &addr_tmp);
			addr_tmp &= 0xffff;

			eol = strchr(str + 1, 'S');
			if (eol) {
				*eol = 0;
			}

			cksum = bc;
			cksum += addr_tmp & 0xff;
			cksum += addr_tmp >> 8;

			if ((next_addr != addr_tmp) || ((sec_size + bc) > SEC_MAX)) {
				// previous block is not contiguous, or
				// section buffer is full => write a sparse chunk
				if (sec_size && sec_size < UT16_MAX) {
					if (rz_buf_write_at(rbuf, sec_start, sec_tmp, (st32)sec_size) != sec_size) {
						eprintf("sparse buffer problem, giving up\n");
						goto fail;
					}
				}
				// advance cursor, reset section
				sec_start = segreg + addr_tmp;
				next_addr = addr_tmp;
				sec_size = 0;
			}

			sec_size += bc;
			next_addr += bc;
			if (eol) {
				// checksum
				if (sscanf(str + 8, "%02x", &byte) != 1) {
					eprintf("unparsable data !\n");
					goto fail;
				}
				cksum = ~cksum;
				if (cksum != byte) {
					eprintf("Checksum failed (got %02x expected %02x)\n", byte, cksum);
					goto fail;
				}
				*eol = 'S';
			}
			str = eol;
			break;
<<<<<<< HEAD
		case '2': // 24 bit adress, with data field!, kinda same with S1
=======
		case 2: // 24 bit adress, with data field!, kinda same with S1
>>>>>>> b73a89fe448a0024c8e21dac562927aa01619d84

			// S / 2 / (addres+data+checksum) bytes / 0000 / data / checksum
			l = sscanf(str + 4, "%06x", &addr_tmp);
			addr_tmp &= 0xffffff;

			eol = strchr(str + 1, 'S');
			if (eol) {
				*eol = 0;
			}

			cksum = bc;
			cksum += addr_tmp & 0xff;
			cksum += (addr_tmp >> 8) & 0xff;
			cksum += addr_tmp >> 16;

			if ((next_addr != addr_tmp) || ((sec_size + bc) > SEC_MAX)) {
				// previous block is not contiguous, or
				// section buffer is full => write a sparse chunk
				if (sec_size && sec_size < UT16_MAX) {
					if (rz_buf_write_at(rbuf, sec_start, sec_tmp, (st32)sec_size) != sec_size) {
						eprintf("sparse buffer problem, giving up\n");
						goto fail;
					}
				}
				// advance cursor, reset section
				sec_start = segreg + addr_tmp;
				next_addr = addr_tmp;
				sec_size = 0;
			}

			for (i = 0; i < bc - 4; i++) {
				if (sscanf(str + 10 + (i * 2), "%02x", &byte) != 1) {
					eprintf("unparsable data !\n");
					goto fail;
				}
				if (sec_size + i < sec_count) {
					sec_tmp[sec_size + i] = (ut8)byte & 0xff;
				}
				cksum += byte;
			}
			sec_size += bc;
			next_addr += bc;
			if (eol) {
				// checksum
				if (sscanf(str + 10 + (i * 2), "%02x", &byte) != 1) {
					eprintf("unparsable data !\n");
					goto fail;
				}
				cksum = ~cksum; // this might be useless cause cksum is ut8
				if (cksum != byte) {
					eprintf("Checksum failed (got %02x expected %02x)\n", byte, cksum);
					goto fail;
				}
				*eol = 'S';
			}
			str = eol;
			break;

<<<<<<< HEAD
		case '6': // same with S5 but on 24 bits instead of 16
=======
		case 6: // same with S5 but on 24 bits instead of 16
>>>>>>> b73a89fe448a0024c8e21dac562927aa01619d84

			// S / 6 / (addres+data+checksum) bytes / counter / - / checksum
			l = sscanf(str + 4, "%06x", &addr_tmp);
			addr_tmp &= 0xffffff;

			eol = strchr(str + 1, 'S');
			if (eol) {
				*eol = 0;
			}

			cksum = bc;
			cksum += addr_tmp & 0xff;
			cksum += (addr_tmp >> 8) & 0xff;
			cksum += addr_tmp >> 16;

			if ((next_addr != addr_tmp) || ((sec_size + bc) > SEC_MAX)) {
				// previous block is not contiguous, or
				// section buffer is full => write a sparse chunk
				if (sec_size && sec_size < UT16_MAX) {
					if (rz_buf_write_at(rbuf, sec_start, sec_tmp, (st32)sec_size) != sec_size) {
						eprintf("sparse buffer problem, giving up\n");
						goto fail;
					}
				}
				// advance cursor, reset section
				sec_start = segreg + addr_tmp;
				next_addr = addr_tmp;
				sec_size = 0;
			}
			sec_size += bc;
			next_addr += bc;
			if (eol) {
				// checksum
				if (sscanf(str + 10, "%02x", &byte) != 1) {
					eprintf("unparsable data !\n");
					goto fail;
				}
				cksum = ~cksum; // this might be useless cause cksum is ut8
				if (cksum != byte) {
					eprintf("Checksum failed (got %02x expected %02x)\n", byte, cksum);
					goto fail;
				}
				*eol = 'S';
			}
			str = eol;
			break;

<<<<<<< HEAD
		case '8': // same with S9 but on 24 bits instead of 16
=======
		case 8: // same with S9 but on 24 bits instead of 16
>>>>>>> b73a89fe448a0024c8e21dac562927aa01619d84

			// S / 8 / (addres+data+checksum) bytes / adress / - / checksum
			l = sscanf(str + 4, "%06x", &addr_tmp);
			addr_tmp &= 0xffffff;

			eol = strchr(str + 1, 'S');
			if (eol) {
				*eol = 0;
			}

			cksum = bc;
			cksum += addr_tmp & 0xff;
			cksum += (addr_tmp >> 8) & 0xff;
			cksum += addr_tmp >> 16;

			if ((next_addr != addr_tmp) || ((sec_size + bc) > SEC_MAX)) {
				// previous block is not contiguous, or
				// section buffer is full => write a sparse chunk
				if (sec_size && sec_size < UT16_MAX) {
					if (rz_buf_write_at(rbuf, sec_start, sec_tmp, (st32)sec_size) != sec_size) {
						eprintf("sparse buffer problem, giving up\n");
						goto fail;
					}
				}
				// advance cursor, reset section
				sec_start = segreg + addr_tmp;
				next_addr = addr_tmp;
				sec_size = 0;
			}
			sec_size += bc;
			next_addr += bc;
			if (eol) {
				// checksum
				if (sscanf(str + 10, "%02x", &byte) != 1) {
					eprintf("unparsable data !\n");
					goto fail;
				}
				cksum = ~cksum;
				if (cksum != byte) {
					eprintf("Checksum failed (got %02x expected %02x)\n", byte, cksum);
					goto fail;
				}
				*eol = 'S';
			}
			str = eol;
			break;

<<<<<<< HEAD
		case '3': // 32 bit adress, with data field!, kinda same with S1
=======
		case 3: // 32 bit adress, with data field!, kinda same with S1
>>>>>>> b73a89fe448a0024c8e21dac562927aa01619d84

			// S / 3 / (addres+data+checksum) bytes / address / data / checksum
			l = sscanf(str + 4, "%08x", &addr_tmp);
			addr_tmp &= 0xffffffff;

			eol = strchr(str + 1, 'S');
			if (eol) {
				*eol = 0;
			}

			cksum = bc;
			cksum += addr_tmp & 0xff;
			cksum += (addr_tmp >> 8) & 0xff;
			cksum += (addr_tmp >> 16) & 0xff;
			cksum += addr_tmp >> 24;

			if ((next_addr != addr_tmp) || ((sec_size + bc) > SEC_MAX)) {
				// previous block is not contiguous, or
				// section buffer is full => write a sparse chunk
				if (sec_size && sec_size < UT16_MAX) {
					if (rz_buf_write_at(rbuf, sec_start, sec_tmp, (st32)sec_size) != sec_size) {
						eprintf("sparse buffer problem, giving up\n");
						goto fail;
					}
				}
				// advance cursor, reset section
				sec_start = segreg + addr_tmp;
				next_addr = addr_tmp;
				sec_size = 0;
			}

			for (i = 0; i < bc - 5; i++) {
				if (sscanf(str + 12 + (i * 2), "%02x", &byte) != 1) {
					eprintf("unparsable data !\n");
					goto fail;
				}
				if (sec_size + i < sec_count) {
					sec_tmp[sec_size + i] = (ut8)byte & 0xff;
				}
				cksum += byte;
			}
			sec_size += bc;
			next_addr += bc;
			if (eol) {
				// checksum
				if (sscanf(str + 12 + (i * 2), "%02x", &byte) != 1) {
					eprintf("unparsable data !\n");
					goto fail;
				}
				cksum = ~cksum;
				if (cksum != byte) {
					eprintf("Checksum failed (got %02x expected %02x)\n", byte, cksum);
					goto fail;
				}
				*eol = 'S';
			}
			str = eol;
			break;

<<<<<<< HEAD
		case '7': // same with S9 but on 32 bits
=======
		case 7: // same with S9 but on 32 bits
>>>>>>> b73a89fe448a0024c8e21dac562927aa01619d84

			// S / 7 / (addres+data+checksum) bytes / address / - / checksum
			l = sscanf(str + 4, "%08x", &addr_tmp);
			addr_tmp &= 0xffffffff;

			eol = strchr(str + 1, 'S');
			if (eol) {
				*eol = 0;
			}

			cksum = bc;
			cksum += addr_tmp & 0xff;
			cksum += (addr_tmp >> 8) & 0xff;
			cksum += (addr_tmp >> 16) & 0xff;
			cksum += addr_tmp >> 24;

			if ((next_addr != addr_tmp) || ((sec_size + bc) > SEC_MAX)) {
				// previous block is not contiguous, or
				// section buffer is full => write a sparse chunk
				if (sec_size && sec_size < UT16_MAX) {
					if (rz_buf_write_at(rbuf, sec_start, sec_tmp, (st32)sec_size) != sec_size) {
						eprintf("sparse buffer problem, giving up\n");
						goto fail;
					}
				}
				// advance cursor, reset section
				sec_start = segreg + addr_tmp;
				next_addr = addr_tmp;
				sec_size = 0;
			}
			sec_size += bc;
			next_addr += bc;
			if (eol) {
				// checksum
				if (sscanf(str + 12, "%02x", &byte) != 1) {
					eprintf("unparsable data !\n");
					goto fail;
				}
				cksum = ~cksum;
				if (cksum != byte) {
					eprintf("Checksum failed (got %02x expected %02x)\n", byte, cksum);
					goto fail;
				}
				*eol = 'S';
			}
			str = eol;
			break;

<<<<<<< HEAD
		case '4': break; // reserved
=======
		case 4: break; // reserved
>>>>>>> b73a89fe448a0024c8e21dac562927aa01619d84
		}
	} while (str);
	free(sec_tmp);
	return true;
fail:
	free(sec_tmp);
	return false;
}

static RzIODesc *__open(RzIO *io, const char *pathname, st32 rw, st32 mode) {
	RzSREC *mal = NULL;
	char *str = NULL;
	if (__plugin_open(io, pathname, 0)) {
		str = rz_file_slurp(pathname + 7, NULL);
		if (!str) {
			return NULL;
		}
		mal = RZ_NEW0(RzSREC);
		if (!mal) {
			free(str);
			return NULL;
		}
		mal->rbuf = rz_buf_new_sparse(io->Oxff);
		if (!mal->rbuf) {
			free(str);
			free(mal);
			return NULL;
		}
		if (!SREC_parse(mal->rbuf, str)) {
			eprintf("srec: failed to parse file\n");
			free(str);
			rz_buf_free(mal->rbuf);
			free(mal);
			return NULL;
		}
		free(str);
<<<<<<< HEAD
		return rz_io_desc_new(io, &rz_io_plugin_srec,
=======
		return rz_io_desc_new(io, &rz_io_plugin_ihex,
>>>>>>> b73a89fe448a0024c8e21dac562927aa01619d84
			pathname, rw, mode, mal);
	}
	return NULL;
}

static bool __resize(RzIO *io, RzIODesc *fd, ut64 size) {
	if (!fd) {
		return false;
	}
	RzSREC *rih = fd->data;
	if (rih) {
		return rz_buf_resize(rih->rbuf, size);
	}
	return false;
}

RzIOPlugin rz_io_plugin_srec = {
	.name = "srec",
	.desc = "Motorola S-record file format",
	.uris = SREC_PATH_PREFIX,
	.license = "LGPL",
	.open = __open,
	.close = __close,
	.read = __read,
	.check = __plugin_open,
	.lseek = __lseek,
	.write = __write,
	.resize = __resize
};

#ifndef RZ_PLUGIN_INCORE
RZ_API RzLibStruct rizin_plugin = {
	.type = RZ_LIB_TYPE_IO,
	.data = &rz_io_plugin_srec,
	.version = RZ_VERSION
};
#endif
