// SPDX-FileCopyrightText: 2007-2020 pancake <pancake@nopcode.org>
// SPDX-FileCopyrightText: 2007-2020 Skia <skia@libskia.so>
// SPDX-License-Identifier: LGPL-3.0-only

#include <rz_util.h>
#include <rz_util/rz_print.h>
#include <rz_reg.h>
#include <rz_type.h>

#define NOPTR           0
#define PTRSEEK         1
#define PTRBACK         2
#define NULLPTR         3
#define STRUCTPTR       100
#define NESTEDSTRUCT    1
#define STRUCTFLAG      10000
#define NESTDEPTH       14
#define ARRAYINDEX_COEF 10000

#define MUSTSEE       (mode & RZ_PRINT_MUSTSEE && mode & RZ_PRINT_ISFIELD && !(mode & RZ_PRINT_JSON))
#define ISQUIET       (mode & RZ_PRINT_QUIET)
#define MUSTSET       (mode & RZ_PRINT_MUSTSET && mode & RZ_PRINT_ISFIELD && setval)
#define SEEVALUE      (mode & RZ_PRINT_VALUE)
#define MUSTSEEJSON   (mode & RZ_PRINT_JSON && mode & RZ_PRINT_ISFIELD)
#define MUSTSEESTRUCT (mode & RZ_PRINT_STRUCT)

//this define is used as a way to acknowledge when updateAddr should take len
//as real len of the buffer
#define THRESHOLD (-4444)

//TODO REWRITE THIS IS BECOMING A NIGHTMARE

static float updateAddr(const ut8 *buf, int len, int endian, ut64 *addr, ut64 *addr64) {
	float f = 0.0;
	// assert sizeof (float) == sizeof (ut32))
	// XXX 999 is used as an implicit buffer size, we should pass the buffer size to every function too, otherwise this code will give us some problems
	if (len >= THRESHOLD - 7 && len < THRESHOLD) {
		len = len + THRESHOLD; // get the real len to avoid oob
	} else {
		len = 999;
	}
	if (len < 1) {
		return 0;
	}
	if (len >= sizeof(float)) {
		rz_mem_swaporcopy((ut8 *)&f, buf, sizeof(float), endian);
	}
	if (addr && len > 3) {
		ut32 tmpaddr = rz_read_ble32(buf, endian);
		*addr = (ut64)tmpaddr;
	}
	if (addr64 && len > 7) {
		*addr64 = rz_read_ble64(buf, endian);
	}
	return f;
}

static int rz_get_size(RNum *num, ut8 *buf, int endian, const char *s) {
	int len = strlen(s);
	if (s[0] == '*' && len >= 4) { // value pointed by the address
		ut64 addr;
		int offset = (int)rz_num_math(num, s + 1);
		(void)updateAddr(buf + offset, 999, endian, &addr, NULL);
		return addr;
	}
	// flag handling doesnt seems to work here
	return rz_num_math(num, s);
}

static void rz_type_format_u128(RzStrBuf *outbuf, int endian, int mode,
	const char *setval, ut64 seeki, ut8 *buf, int i, int size) {
	ut64 low = rz_read_ble64(buf, endian);
	ut64 hig = rz_read_ble64(buf + 8, endian);
	if (MUSTSEEJSON) {
		rz_strbuf_append(outbuf, "\"");
	} else if (!SEEVALUE && !ISQUIET) {
		rz_strbuf_appendf(outbuf, "0x%08" PFMT64x " = (uint128_t)", seeki);
	}
	if (endian) {
		rz_strbuf_appendf(outbuf, "0x%016" PFMT64x "", low);
		rz_strbuf_appendf(outbuf, "%016" PFMT64x, hig);
	} else {
		rz_strbuf_appendf(outbuf, "0x%016" PFMT64x "", hig);
		rz_strbuf_appendf(outbuf, "%016" PFMT64x, low);
	}
	if (MUSTSEEJSON) {
		const char *end = endian ? "big" : "little";
		rz_strbuf_appendf(outbuf, "\",\"endian\":\"%s\",\"ctype\":\"uint128_t\"}", end);
	}
}

static void rz_type_format_quadword(RzStrBuf *outbuf, int endian, int mode,
	const char *setval, ut64 seeki, ut8 *buf, int i, int size) {
	ut64 addr64;
	int elem = -1;
	if (size >= ARRAYINDEX_COEF) {
		elem = size / ARRAYINDEX_COEF - 1;
		size %= ARRAYINDEX_COEF;
	}
	updateAddr(buf + i, size - i, endian, NULL, &addr64);
	if (MUSTSET) {
		rz_strbuf_appendf(outbuf, "wv8 %s @ 0x%08" PFMT64x "\n", setval, seeki + ((elem >= 0) ? elem * 8 : 0));
	} else if (MUSTSEE) {
		if (!SEEVALUE && !ISQUIET) {
			rz_strbuf_appendf(outbuf, "0x%08" PFMT64x " = (qword)",
				seeki + ((elem >= 0) ? elem * 8 : 0));
		}
		if (size == -1) {
			if (addr64 == UT32_MAX || ((st64)addr64 < 0 && (st64)addr64 > -4096)) {
				rz_strbuf_appendf(outbuf, "%d", (int)(addr64));
			} else {
				rz_strbuf_appendf(outbuf, "0x%016" PFMT64x, addr64);
			}
		} else {
			if (!SEEVALUE) {
				rz_strbuf_append(outbuf, "[ ");
			}
			while (size--) {
				updateAddr(buf + i, size - i, endian, NULL, &addr64);
				if (elem == -1 || elem == 0) {
					rz_strbuf_appendf(outbuf, "0x%016" PFMT64x, addr64);
					if (elem == 0) {
						elem = -2;
					}
				}
				if (size != 0 && elem == -1) {
					rz_strbuf_append(outbuf, ", ");
				}
				if (elem > -1) {
					elem--;
				}
				i += 8;
			}
			if (!SEEVALUE) {
				rz_strbuf_append(outbuf, " ]");
			}
		}
	} else if (MUSTSEEJSON || MUSTSEESTRUCT) {
		if (size == -1) {
			rz_strbuf_appendf(outbuf, "%" PFMT64d, addr64);
		} else {
			rz_strbuf_append(outbuf, "[ ");
			while (size--) {
				updateAddr(buf + i, size - i, endian, NULL, &addr64);
				if (elem == -1 || elem == 0) {
					rz_strbuf_appendf(outbuf, "%" PFMT64d, addr64);
					if (elem == 0) {
						elem = -2;
					}
				}
				if (size != 0 && elem == -1) {
					rz_strbuf_append(outbuf, ", ");
				}
				if (elem > -1) {
					elem--;
				}
				i += 8;
			}
			rz_strbuf_append(outbuf, " ]");
		}
		if (MUSTSEEJSON) {
			rz_strbuf_append(outbuf, "}");
		}
	}
}

static void rz_type_format_byte(RzStrBuf *outbuf, int endian, int mode,
	const char *setval, ut64 seeki, ut8 *buf, int i, int size) {
	int elem = -1;
	if (size >= ARRAYINDEX_COEF) {
		elem = size / ARRAYINDEX_COEF - 1;
		size %= ARRAYINDEX_COEF;
	}
	if (MUSTSET) {
		rz_strbuf_appendf(outbuf, "\"w %s\" @ 0x%08" PFMT64x "\n", setval, seeki + ((elem >= 0) ? elem : 0));
	} else if (MUSTSEE) {
		if (!SEEVALUE && !ISQUIET) {
			rz_strbuf_appendf(outbuf, "0x%08" PFMT64x " = ", seeki + ((elem >= 0) ? elem : 0));
		}
		if (size == -1) {
			rz_strbuf_appendf(outbuf, "0x%02x", buf[i]);
		} else {
			if (!SEEVALUE) {
				rz_strbuf_append(outbuf, "[ ");
			}
			while (size--) {
				if (elem == -1 || elem == 0) {
					rz_strbuf_appendf(outbuf, "0x%02x", buf[i]);
					if (elem == 0) {
						elem = -2;
					}
				}
				if (size != 0 && elem == -1) {
					rz_strbuf_append(outbuf, ", ");
				}
				if (elem > -1) {
					elem--;
				}
				i++;
			}
			if (!SEEVALUE) {
				rz_strbuf_append(outbuf, " ]");
			}
		}
	} else if (MUSTSEEJSON || MUSTSEESTRUCT) {
		if (size == -1) {
			rz_strbuf_appendf(outbuf, "%d", buf[i]);
		} else {
			rz_strbuf_append(outbuf, "[ ");
			const char *comma = "";
			while (size--) {
				if (elem == -1 || elem == 0) {
					rz_strbuf_appendf(outbuf, "%s%d", comma, buf[i]);
					comma = ",";
					if (elem == 0) {
						elem = -2;
					}
				}
				if (elem > -1) {
					elem--;
				}
				i++;
			}
			rz_strbuf_append(outbuf, " ]");
		}
		if (MUSTSEEJSON) {
			rz_strbuf_append(outbuf, "}");
		}
	}
}

// FIXME: Port to the PJ API
// Return number of consumed bytes
static int rz_type_format_uleb(RzStrBuf *outbuf, int endian, int mode,
	const char *setval, ut64 seeki, ut8 *buf, int i, int size) {
	int elem = -1;
	int s = 0, offset = 0;
	ut64 value = 0;
	if (size >= ARRAYINDEX_COEF) {
		elem = size / ARRAYINDEX_COEF - 1;
		size %= ARRAYINDEX_COEF;
	}
	if (MUSTSET) {
		ut8 *tmp;
		char *nbr;
		do {
			rz_uleb128_decode(buf + i, &s, &value);
			i += s;
			offset += s;
		} while (elem--);
		tmp = (ut8 *)rz_uleb128_encode(rz_num_math(NULL, setval), &s);
		nbr = rz_hex_bin2strdup(tmp, s);
		rz_strbuf_appendf(outbuf, "\"wx %s\" @ 0x%08" PFMT64x "\n", nbr, seeki + offset - s);
		free(tmp);
		free(nbr);
	} else if (MUSTSEE) {
		if (!SEEVALUE && !ISQUIET) {
			rz_strbuf_appendf(outbuf, "0x%08" PFMT64x " = ", seeki);
		}
		if (size == -1) {
			rz_uleb128_decode(buf + i, &offset, &value);
			rz_strbuf_appendf(outbuf, "%" PFMT64d, value);
		} else {
			if (!SEEVALUE) {
				rz_strbuf_append(outbuf, "[ ");
			}
			while (size--) {
				if (elem == -1 || elem == 0) {
					rz_uleb128_decode(buf + i, &s, &value);
					i += s;
					offset += s;
					rz_strbuf_appendf(outbuf, "%" PFMT64d, value);
					if (elem == 0) {
						elem = -2;
					}
				}
				if (size != 0 && elem == -1) {
					rz_strbuf_append(outbuf, ", ");
				}
				if (elem > -1) {
					elem--;
				}
			}
			if (!SEEVALUE) {
				rz_strbuf_append(outbuf, " ]");
			}
		}
	} else if (MUSTSEEJSON || MUSTSEESTRUCT) {
		if (size == -1) {
			rz_uleb128_decode(buf + i, &offset, &value);
			rz_strbuf_appendf(outbuf, "\"%" PFMT64d "\"", value);
		} else {
			rz_strbuf_append(outbuf, "[ ");
			while (size--) {
				if (elem == -1 || elem == 0) {
					rz_uleb128_decode(buf + i, &s, &value);
					i += s;
					offset += s;
					rz_strbuf_appendf(outbuf, "\"%" PFMT64d "\"", value);
					if (elem == 0) {
						elem = -2;
					}
				}
				if (size != 0 && elem == -1) {
					rz_strbuf_append(outbuf, ", ");
				}
				if (elem > -1) {
					elem--;
				}
			}
			rz_strbuf_append(outbuf, " ]");
		}
		if (MUSTSEEJSON) {
			rz_strbuf_append(outbuf, "}");
		}
	}
	return offset;
}

static void rz_type_format_char(RzStrBuf *outbuf, int endian, int mode,
	const char *setval, ut64 seeki, ut8 *buf, int i, int size) {
	int elem = -1;
	if (size >= ARRAYINDEX_COEF) {
		elem = size / ARRAYINDEX_COEF - 1;
		size %= ARRAYINDEX_COEF;
	}
	if (MUSTSET) {
		rz_strbuf_appendf(outbuf, "\"w %s\" @ 0x%08" PFMT64x "\n", setval, seeki + ((elem >= 0) ? elem : 0));
	} else if (MUSTSEE) {
		if (!SEEVALUE && !ISQUIET) {
			rz_strbuf_appendf(outbuf, "0x%08" PFMT64x " = ", seeki + ((elem >= 0) ? elem * 2 : 0)); //XXX:: shouldn't it be elem*1??
		}
		if (size == -1) {
			rz_strbuf_appendf(outbuf, "'%c'", IS_PRINTABLE(buf[i]) ? buf[i] : '.');
		} else {
			if (!SEEVALUE) {
				rz_strbuf_append(outbuf, "[ ");
			}
			while (size--) {
				if (elem == -1 || elem == 0) {
					rz_strbuf_appendf(outbuf, "'%c'", IS_PRINTABLE(buf[i]) ? buf[i] : '.');
					if (elem == 0) {
						elem = -2;
					}
				}
				if (size != 0 && elem == -1) {
					rz_strbuf_append(outbuf, ", ");
				}
				if (elem > -1) {
					elem--;
				}
				i++;
			}
			if (!SEEVALUE) {
				rz_strbuf_append(outbuf, " ]");
			}
		}
	} else if (MUSTSEEJSON || MUSTSEESTRUCT) {
		if (size == -1) {
			rz_strbuf_appendf(outbuf, "\"%c\"", IS_PRINTABLE(buf[i]) ? buf[i] : '.');
		} else {
			rz_strbuf_append(outbuf, "[ ");
			while (size--) {
				if (elem == -1 || elem == 0) {
					rz_strbuf_appendf(outbuf, "\"%c\"", IS_PRINTABLE(buf[i]) ? buf[i] : '.');
					if (elem == 0) {
						elem = -2;
					}
				}
				if (size != 0 && elem == -1) {
					rz_strbuf_append(outbuf, ", ");
				}
				if (elem > -1) {
					elem--;
				}
				i++;
			}
			rz_strbuf_append(outbuf, " ]");
		}
		if (MUSTSEEJSON) {
			rz_strbuf_append(outbuf, "}");
		}
	}
}

static void rz_type_format_decchar(RzStrBuf *outbuf, int endian, int mode,
	const char *setval, ut64 seeki, ut8 *buf, int i, int size) {
	int elem = -1;
	if (size >= ARRAYINDEX_COEF) {
		elem = size / ARRAYINDEX_COEF - 1;
		size %= ARRAYINDEX_COEF;
	}
	if (MUSTSET) {
		rz_strbuf_appendf(outbuf, "\"w %s\" @ 0x%08" PFMT64x "\n", setval, seeki + ((elem >= 0) ? elem : 0));
	} else if (MUSTSEE) {
		if (!SEEVALUE && !ISQUIET) {
			rz_strbuf_appendf(outbuf, "0x%08" PFMT64x " = ", seeki + ((elem >= 0) ? elem : 0));
		}
		if (size == -1) {
			rz_strbuf_appendf(outbuf, "%d", buf[i]);
		} else {
			if (!SEEVALUE) {
				rz_strbuf_append(outbuf, "[ ");
			}
			while (size--) {
				if (elem == -1 || elem == 0) {
					rz_strbuf_appendf(outbuf, "%d", buf[i]);
					if (elem == 0) {
						elem = -2;
					}
				}
				if (size != 0 && elem == -1) {
					rz_strbuf_appendf(outbuf, ", ");
				}
				if (elem > -1) {
					elem--;
				}
				i++;
			}
			if (!SEEVALUE) {
				rz_strbuf_append(outbuf, " ]");
			}
		}
	} else if (MUSTSEEJSON || MUSTSEESTRUCT) {
		if (size == -1) {
			rz_strbuf_appendf(outbuf, "\"%d\"", buf[i]);
		} else {
			rz_strbuf_append(outbuf, "[ ");
			while (size--) {
				if (elem == -1 || elem == 0) {
					rz_strbuf_appendf(outbuf, "\"%d\"", buf[i]);
					if (elem == 0) {
						elem = -2;
					}
				}
				if (size != 0 && elem == -1) {
					rz_strbuf_appendf(outbuf, ", ");
				}
				if (elem > -1) {
					elem--;
				}
				i++;
			}
			rz_strbuf_append(outbuf, " ]");
		}
		if (MUSTSEEJSON) {
			rz_strbuf_append(outbuf, "}");
		}
	}
}

static int rz_type_format_string(RzTypeDB *typedb, RzStrBuf *outbuf, ut64 seeki, ut64 addr64, ut64 addr, int is64, int mode) {
	ut8 buffer[255];
	buffer[0] = 0;
	const ut64 at = (is64 == 1) ? addr64 : (ut64)addr;
	int res = typedb->iob.read_at(typedb->iob.io, at, buffer, sizeof(buffer) - 8);
	if (MUSTSEEJSON) {
		char *encstr = rz_str_utf16_encode((const char *)buffer, -1);
		if (encstr) {
			rz_strbuf_appendf(outbuf, "%" PFMT64d ",\"string\":\"%s\"}", seeki, encstr);
			free(encstr);
		}
	} else if (MUSTSEESTRUCT) {
		char *encstr = rz_str_utf16_encode((const char *)buffer, -1);
		if (encstr) {
			rz_strbuf_appendf(outbuf, "\"%s\"", encstr);
			free(encstr);
		}
	} else if (MUSTSEE) {
		if (!SEEVALUE && !ISQUIET) {
			rz_strbuf_appendf(outbuf, "0x%08" PFMT64x " = ", seeki);
		}
		if (!SEEVALUE) {
			if (ISQUIET) {
				if (addr == 0LL) {
					rz_strbuf_append(outbuf, "NULL");
				} else if (addr == UT32_MAX || addr == UT64_MAX) {
					rz_strbuf_append(outbuf, "-1");
				} else {
					rz_strbuf_appendf(outbuf, "0x%08" PFMT64x " ", addr);
				}
			} else {
				rz_strbuf_appendf(outbuf, "0x%08" PFMT64x " -> 0x%08" PFMT64x " ", seeki, addr);
			}
		}
		if (res > 0 && buffer[0] != 0xff && buffer[1] != 0xff) {
			rz_strbuf_appendf(outbuf, "\"%s\"", buffer);
		}
	}
	return 0;
}

static void rz_type_format_time(RzStrBuf *outbuf, int endian, int mode,
	const char *setval, ut64 seeki, ut8 *buf, int i, int size) {
	ut64 addr;
	struct tm timestruct;
	int elem = -1;
	if (size >= ARRAYINDEX_COEF) {
		elem = size / ARRAYINDEX_COEF - 1;
		size %= ARRAYINDEX_COEF;
	}
	updateAddr(buf + i, size - i, endian, &addr, NULL);
	if (MUSTSET) {
		rz_strbuf_appendf(outbuf, "wv4 %s @ 0x%08" PFMT64x "\n", setval, seeki + ((elem >= 0) ? elem * 4 : 0));
	} else if (MUSTSEE) {
		char *timestr = malloc(ASCTIME_BUF_MINLEN);
		if (!timestr) {
			return;
		}
		rz_asctime_r(gmtime_r((time_t *)&addr, &timestruct), timestr);
		*(timestr + 24) = '\0';
		if (!SEEVALUE && !ISQUIET) {
			rz_strbuf_appendf(outbuf, "0x%08" PFMT64x " = ", seeki + ((elem >= 0) ? elem * 4 : 0));
		}
		if (size == -1) {
			rz_strbuf_appendf(outbuf, "%s", timestr);
		} else {
			if (!SEEVALUE) {
				rz_strbuf_appendf(outbuf, "[ ");
			}
			while (size--) {
				updateAddr(buf + i, size - i, endian, &addr, NULL);
				rz_asctime_r(gmtime_r((time_t *)&addr, &timestruct), timestr);
				*(timestr + 24) = '\0';
				if (elem == -1 || elem == 0) {
					rz_strbuf_appendf(outbuf, "%s", timestr);
					if (elem == 0) {
						elem = -2;
					}
				}
				if (size != 0 && elem == -1) {
					rz_strbuf_appendf(outbuf, ", ");
				}
				if (elem > -1) {
					elem--;
				}
				i += 4;
			}
			if (!SEEVALUE) {
				rz_strbuf_appendf(outbuf, " ]");
			}
		}
		free(timestr);
	} else if (MUSTSEEJSON || MUSTSEESTRUCT) {
		char *timestr = malloc(ASCTIME_BUF_MINLEN);
		if (!timestr) {
			return;
		}
		rz_asctime_r(gmtime_r((time_t *)&addr, &timestruct), timestr);
		*(timestr + 24) = '\0';
		if (size == -1) {
			rz_strbuf_appendf(outbuf, "\"%s\"", timestr);
		} else {
			rz_strbuf_append(outbuf, "[ ");
			while (size--) {
				updateAddr(buf + i, size - i, endian, &addr, NULL);
				rz_asctime_r(gmtime_r((time_t *)&addr, &timestruct), timestr);
				*(timestr + 24) = '\0';
				if (elem == -1 || elem == 0) {
					rz_strbuf_appendf(outbuf, "\"%s\"", timestr);
					if (elem == 0) {
						elem = -2;
					}
				}
				if (size != 0 && elem == -1) {
					rz_strbuf_append(outbuf, ", ");
				}
				if (elem > -1) {
					elem--;
				}
				i += 4;
			}
			rz_strbuf_append(outbuf, " ]");
		}
		free(timestr);
		if (MUSTSEEJSON) {
			rz_strbuf_append(outbuf, "}");
		}
	}
}

// TODO: support unsigned int?
static void rz_type_format_hex(RzStrBuf *outbuf, int endian, int mode,
	const char *setval, ut64 seeki, ut8 *buf, int i, int size) {
	ut64 addr;
	int elem = -1;
	if (size >= ARRAYINDEX_COEF) {
		elem = size / ARRAYINDEX_COEF - 1;
		size %= ARRAYINDEX_COEF;
	}
	updateAddr(buf + i, size - i, endian, &addr, NULL);
	if (MUSTSET) {
		rz_strbuf_appendf(outbuf, "wv4 %s @ 0x%08" PFMT64x "\n", setval, seeki + ((elem >= 0) ? elem * 4 : 0));
	} else if ((mode & RZ_PRINT_DOT) || MUSTSEESTRUCT) {
		rz_strbuf_appendf(outbuf, "%" PFMT64d, addr);
	} else if (MUSTSEE) {
		if (!SEEVALUE && !ISQUIET) {
			rz_strbuf_appendf(outbuf, "0x%08" PFMT64x " = ", seeki + ((elem >= 0) ? elem * 4 : 0));
		}
		if (size == -1) {
			if (addr == UT64_MAX || addr == UT32_MAX) {
				rz_strbuf_append(outbuf, "-1");
			} else {
				rz_strbuf_appendf(outbuf, "%" PFMT64d, addr);
			}
		} else {
			if (!SEEVALUE) {
				rz_strbuf_append(outbuf, "[ ");
			}
			while (size--) {
				updateAddr(buf + i, size - i, endian, &addr, NULL);
				if (elem == -1 || elem == 0) {
					if (ISQUIET) {
						if (addr == UT64_MAX || addr == UT32_MAX) {
							rz_strbuf_append(outbuf, "-1");
						} else {
							rz_strbuf_appendf(outbuf, "%" PFMT64d, addr);
						}
					} else {
						rz_strbuf_appendf(outbuf, "%" PFMT64d, addr);
					}
					if (elem == 0) {
						elem = -2;
					}
				}
				if (size != 0 && elem == -1) {
					rz_strbuf_append(outbuf, ", ");
				}
				if (elem > -1) {
					elem--;
				}
				i += 4;
			}
			if (!SEEVALUE) {
				rz_strbuf_append(outbuf, " ]");
			}
		}
	} else if (MUSTSEEJSON) {
		if (size == -1) {
			rz_strbuf_appendf(outbuf, "%" PFMT64d, addr);
		} else {
			rz_strbuf_append(outbuf, "[ ");
			while (size--) {
				updateAddr(buf + i, size - i, endian, &addr, NULL);
				if (elem == -1 || elem == 0) {
					rz_strbuf_appendf(outbuf, "%" PFMT64d, addr);
					if (elem == 0) {
						elem = -2;
					}
				}
				if (size != 0 && elem == -1) {
					rz_strbuf_append(outbuf, ", ");
				}
				if (elem > -1) {
					elem--;
				}
				i += 4;
			}
			rz_strbuf_append(outbuf, " ]");
		}
		rz_strbuf_append(outbuf, "}");
	}
}

static void rz_type_format_int(RzStrBuf *outbuf, int endian, int mode,
	const char *setval, ut64 seeki, ut8 *buf, int i, int size) {
	ut64 addr;
	int elem = -1;
	if (size >= ARRAYINDEX_COEF) {
		elem = size / ARRAYINDEX_COEF - 1;
		size %= ARRAYINDEX_COEF;
	}
	updateAddr(buf + i, size - i, endian, &addr, NULL);
	if (MUSTSET) {
		rz_strbuf_appendf(outbuf, "wv4 %s @ %" PFMT64d "\n", setval, seeki + ((elem >= 0) ? elem * 4 : 0));
	} else if ((mode & RZ_PRINT_DOT) || MUSTSEESTRUCT) {
		rz_strbuf_appendf(outbuf, "0x%08" PFMT64x, addr);
	} else if (MUSTSEE) {
		if (!SEEVALUE && !ISQUIET) {
			rz_strbuf_appendf(outbuf, "0x%08" PFMT64x " = ", seeki + ((elem >= 0) ? elem * 4 : 0));
		}
		if (size == -1) {
			rz_strbuf_appendf(outbuf, "%" PFMT64d, (st64)(st32)addr);
		} else {
			if (!SEEVALUE) {
				rz_strbuf_append(outbuf, "[ ");
			}
			while (size--) {
				updateAddr(buf + i, size - i, endian, &addr, NULL);
				if (elem == -1 || elem == 0) {
					rz_strbuf_appendf(outbuf, "%" PFMT64d, (st64)(st32)addr);
					if (elem == 0) {
						elem = -2;
					}
				}
				if (size != 0 && elem == -1) {
					rz_strbuf_append(outbuf, ", ");
				}
				if (elem > -1) {
					elem--;
				}
				i += 4;
			}
			if (!SEEVALUE) {
				rz_strbuf_append(outbuf, " ]");
			}
		}
	} else if (MUSTSEEJSON) {
		if (size == -1) {
			rz_strbuf_appendf(outbuf, "%" PFMT64d, addr);
		} else {
			rz_strbuf_append(outbuf, "[ ");
			while (size--) {
				updateAddr(buf + i, size - i, endian, &addr, NULL);
				if (elem == -1 || elem == 0) {
					rz_strbuf_appendf(outbuf, "%" PFMT64d, addr);
					if (elem == 0) {
						elem = -2;
					}
				}
				if (size != 0 && elem == -1) {
					rz_strbuf_append(outbuf, ", ");
				}
				if (elem > -1) {
					elem--;
				}
				i += 4;
			}
			rz_strbuf_append(outbuf, " ]");
		}
		rz_strbuf_append(outbuf, "}");
	}
}

/*
static int rz_type_format_disasm(const RzPrint *p, ut64 seeki, int size) {
	ut64 prevseeki = seeki;

	if (!p->disasm || !p->user) {
		return 0;
	}

	size = RZ_MAX(1, size);

	while (size-- > 0) {
		seeki += p->disasm(p->user, seeki);
	}

	return seeki - prevseeki;
}
*/

static void rz_type_format_octal(RzStrBuf *outbuf, int endian, int mode,
	const char *setval, ut64 seeki, ut8 *buf, int i, int size) {
	ut64 addr;
	int elem = -1;
	if (size >= ARRAYINDEX_COEF) {
		elem = size / ARRAYINDEX_COEF - 1;
		size %= ARRAYINDEX_COEF;
	}
	updateAddr(buf + i, size - i, endian, &addr, NULL);
	if (MUSTSET) {
		rz_strbuf_appendf(outbuf, "wv4 %s @ 0x%08" PFMT64x "\n", setval, seeki + ((elem >= 0) ? elem * 4 : 0));
	} else if ((mode & RZ_PRINT_DOT) || MUSTSEESTRUCT) {
		rz_strbuf_appendf(outbuf, "0%" PFMT64o, addr);
	} else if (MUSTSEE) {
		if (!SEEVALUE && !ISQUIET) {
			rz_strbuf_appendf(outbuf, "0x%08" PFMT64x " = ", seeki + ((elem >= 0) ? elem * 4 : 0));
		}
		if (!SEEVALUE) {
			rz_strbuf_append(outbuf, "(octal) ");
		}
		if (size == -1) {
			rz_strbuf_appendf(outbuf, " 0%08" PFMT64o, addr);
		} else {
			if (!SEEVALUE) {
				rz_strbuf_append(outbuf, "[ ");
			}
			while (size--) {
				updateAddr(buf + i, size - i, endian, &addr, NULL);
				if (elem == -1 || elem == 0) {
					rz_strbuf_appendf(outbuf, "0%08" PFMT64o, addr);
					if (elem == 0) {
						elem = -2;
					}
				}
				if (size != 0 && elem == -1) {
					rz_strbuf_append(outbuf, ", ");
				}
				if (elem > -1) {
					elem--;
				}
				i += 4;
			}
			if (!SEEVALUE) {
				rz_strbuf_append(outbuf, " ]");
			}
		}
	} else if (MUSTSEEJSON) {
		if (size == -1) {
			rz_strbuf_appendf(outbuf, "%" PFMT64d, addr);
		} else {
			rz_strbuf_append(outbuf, "[ ");
			while (size--) {
				updateAddr(buf, i, endian, &addr, NULL);
				if (elem == -1 || elem == 0) {
					rz_strbuf_appendf(outbuf, "%" PFMT64d, addr);
					if (elem == 0) {
						elem = -2;
					}
				}
				if (size != 0 && elem == -1) {
					rz_strbuf_append(outbuf, ", ");
				}
				if (elem > -1) {
					elem--;
				}
				i += 4;
			}
			rz_strbuf_append(outbuf, " ]");
		}
		rz_strbuf_append(outbuf, "}");
	}
}

static void rz_type_format_hexflag(RzStrBuf *outbuf, int endian, int mode,
	const char *setval, ut64 seeki, ut8 *buf, int i, int size) {
	ut64 addr = 0;
	int elem = -1;
	if (size >= ARRAYINDEX_COEF) {
		elem = size / ARRAYINDEX_COEF - 1;
		size %= ARRAYINDEX_COEF;
	}
	updateAddr(buf + i, size - i, endian, &addr, NULL);
	if (MUSTSET) {
		rz_strbuf_appendf(outbuf, "wv4 %s @ 0x%08" PFMT64x "\n", setval, seeki + ((elem >= 0) ? elem * 4 : 0));
	} else if ((mode & RZ_PRINT_DOT) || MUSTSEESTRUCT) {
		rz_strbuf_appendf(outbuf, "0x%08" PFMT64x, addr & UT32_MAX);
	} else if (MUSTSEE) {
		ut32 addr32 = (ut32)addr;
		if (!SEEVALUE && !ISQUIET) {
			rz_strbuf_appendf(outbuf, "0x%08" PFMT64x " = ", seeki + ((elem >= 0) ? elem * 4 : 0));
		}
		if (size == -1) {
			if (ISQUIET && (addr32 == UT32_MAX)) {
				rz_strbuf_append(outbuf, "-1");
			} else {
				rz_strbuf_appendf(outbuf, "0x%08" PFMT64x, (ut64)addr32);
			}
		} else {
			if (!SEEVALUE) {
				rz_strbuf_append(outbuf, "[ ");
			}
			while (size--) {
				updateAddr(buf + i, size - i, endian, &addr, NULL);
				if (elem == -1 || elem == 0) {
					rz_strbuf_appendf(outbuf, "0x%08" PFMT64x, addr);
					if (elem == 0) {
						elem = -2;
					}
				}
				if (size != 0 && elem == -1) {
					rz_strbuf_append(outbuf, ", ");
				}
				if (elem > -1) {
					elem--;
				}
				i += 4;
			}
			if (!SEEVALUE) {
				rz_strbuf_append(outbuf, " ]");
			}
		}
	} else if (MUSTSEEJSON) {
		if (size == -1) {
			rz_strbuf_appendf(outbuf, "%" PFMT64d, addr);
		} else {
			rz_strbuf_append(outbuf, "[ ");
			while (size--) {
				updateAddr(buf + i, size - i, endian, &addr, NULL);
				if (elem == -1 || elem == 0) {
					rz_strbuf_appendf(outbuf, "%" PFMT64d, addr);
					if (elem == 0) {
						elem = -2;
					}
				}
				if (size != 0 && elem == -1) {
					rz_strbuf_append(outbuf, ",");
				}
				if (elem > -1) {
					elem--;
				}
				i += 4;
			}
			rz_strbuf_append(outbuf, " ]");
		}
		rz_strbuf_append(outbuf, "}");
	}
}

static int rz_type_format_10bytes(RzTypeDB *typedb, RzStrBuf *outbuf, int mode, const char *setval,
	ut64 seeki, ut64 addr, ut8 *buf) {
	ut8 buffer[255];
	int j;
	if (MUSTSET) {
		rz_strbuf_append(outbuf, "?e pf B not yet implemented\n");
	} else if (mode & RZ_PRINT_DOT) {
		for (j = 0; j < 10; j++) {
			rz_strbuf_appendf(outbuf, "%02x ", buf[j]);
		}
	} else if (MUSTSEE) {
		typedb->iob.read_at(typedb->iob.io, (ut64)addr, buffer, 248);
		if (!SEEVALUE && !ISQUIET) {
			rz_strbuf_appendf(outbuf, "0x%08" PFMT64x " = ", seeki);
		}
		for (j = 0; j < 10; j++) {
			rz_strbuf_appendf(outbuf, "%02x ", buf[j]);
		}
		if (!SEEVALUE) {
			rz_strbuf_append(outbuf, " ... (");
		}
		for (j = 0; j < 10; j++) {
			if (!SEEVALUE) {
				if (IS_PRINTABLE(buf[j])) {
					rz_strbuf_appendf(outbuf, "%c", buf[j]);
				} else {
					rz_strbuf_append(outbuf, ".");
				}
			}
		}
		if (!SEEVALUE) {
			rz_strbuf_append(outbuf, ")");
		}
	} else if (MUSTSEEJSON) {
		typedb->iob.read_at(typedb->iob.io, (ut64)addr, buffer, 248);
		rz_strbuf_appendf(outbuf, "[ %d", buf[0]);
		j = 1;
		for (; j < 10; j++) {
			rz_strbuf_appendf(outbuf, ", %d", buf[j]);
		}
		rz_strbuf_append(outbuf, "]");
		return 0;
	}
	return 0;
}

static int rz_type_format_hexpairs(RzStrBuf *outbuf, int endian, int mode,
	const char *setval, ut64 seeki, ut8 *buf, int i, int size) {
	int j;
	size = (size == -1) ? 1 : size;
	if (MUSTSET) {
		rz_strbuf_append(outbuf, "?e pf X not yet implemented\n");
	} else if (mode & RZ_PRINT_DOT) {
		for (j = 0; j < size; j++) {
			rz_strbuf_appendf(outbuf, "%02x", buf[i + j]);
		}
	} else if (MUSTSEE) {
		size = (size < 1) ? 1 : size;
		if (!SEEVALUE && !ISQUIET) {
			rz_strbuf_appendf(outbuf, "0x%08" PFMT64x " = ", seeki);
		}
		for (j = 0; j < size; j++) {
			rz_strbuf_appendf(outbuf, "%02x ", buf[i + j]);
		}
		if (!SEEVALUE) {
			rz_strbuf_append(outbuf, " ... (");
		}
		for (j = 0; j < size; j++) {
			if (!SEEVALUE) {
				if (IS_PRINTABLE(buf[j])) {
					rz_strbuf_appendf(outbuf, "%c", buf[i + j]);
				} else {
					rz_strbuf_append(outbuf, ".");
				}
			}
		}
		rz_strbuf_append(outbuf, ")");
	} else if (MUSTSEEJSON || MUSTSEESTRUCT) {
		size = (size < 1) ? 1 : size;
		rz_strbuf_appendf(outbuf, "[ %d", buf[0]);
		j = 1;
		for (; j < 10; j++) {
			rz_strbuf_appendf(outbuf, ", %d", buf[j]);
		}
		rz_strbuf_append(outbuf, "]");
		if (MUSTSEEJSON) {
			rz_strbuf_append(outbuf, "}");
		}
		return size;
	}
	return size;
}

static void rz_type_format_float(RzStrBuf *outbuf, int endian, int mode,
	const char *setval, ut64 seeki, ut8 *buf, int i, int size) {
	float val_f = 0.0f;
	ut64 addr = 0;
	int elem = -1;
	if (size >= ARRAYINDEX_COEF) {
		elem = size / ARRAYINDEX_COEF - 1;
		size %= ARRAYINDEX_COEF;
	}
	val_f = updateAddr(buf + i, 999, endian, &addr, NULL);
	if (MUSTSET) {
		rz_strbuf_appendf(outbuf, "wv4 %s @ 0x%08" PFMT64x "\n", setval,
			seeki + ((elem >= 0) ? elem * 4 : 0));
	} else if ((mode & RZ_PRINT_DOT) || MUSTSEESTRUCT) {
		rz_strbuf_appendf(outbuf, "%.9g", val_f);
	} else {
		if (MUSTSEE) {
			if (!SEEVALUE && !ISQUIET) {
				rz_strbuf_appendf(outbuf, "0x%08" PFMT64x " = ",
					seeki + ((elem >= 0) ? elem * 4 : 0));
			}
		}
		if (size == -1) {
			rz_strbuf_appendf(outbuf, "%.9g", val_f);
		} else {
			if (!SEEVALUE) {
				rz_strbuf_append(outbuf, "[ ");
			}
			while (size--) {
				val_f = updateAddr(buf + i, 9999, endian, &addr, NULL);
				if (elem == -1 || elem == 0) {
					rz_strbuf_appendf(outbuf, "%.9g", val_f);
					if (elem == 0) {
						elem = -2;
					}
				}
				if (size != 0 && elem == -1) {
					rz_strbuf_append(outbuf, ", ");
				}
				if (elem > -1) {
					elem--;
				}
				i += 4;
			}
			if (!SEEVALUE) {
				rz_strbuf_append(outbuf, " ]");
			}
		}
		if (MUSTSEEJSON) {
			rz_strbuf_append(outbuf, "}");
		}
	}
}

static void rz_type_format_double(RzStrBuf *outbuf, int endian, int mode,
	const char *setval, ut64 seeki, ut8 *buf, int i, int size) {
	double val_f = 0.0;
	ut64 addr = 0;
	int elem = -1;
	if (size >= ARRAYINDEX_COEF) {
		elem = size / ARRAYINDEX_COEF - 1;
		size %= ARRAYINDEX_COEF;
	}
	updateAddr(buf + i, 999, endian, &addr, NULL);
	rz_mem_swaporcopy((ut8 *)&val_f, buf + i, sizeof(double), endian);
	if (MUSTSET) {
		rz_strbuf_appendf(outbuf, "wv8 %s @ 0x%08" PFMT64x "\n", setval,
			seeki + ((elem >= 0) ? elem * 8 : 0));
	} else if ((mode & RZ_PRINT_DOT) || MUSTSEESTRUCT) {
		rz_strbuf_appendf(outbuf, "%.17g", val_f);
	} else {
		if (MUSTSEE) {
			if (!SEEVALUE && !ISQUIET) {
				rz_strbuf_appendf(outbuf, "0x%08" PFMT64x " = ",
					seeki + ((elem >= 0) ? elem * 8 : 0));
			}
		}
		if (size == -1) {
			rz_strbuf_appendf(outbuf, "%.17g", val_f);
		} else {
			if (!SEEVALUE) {
				rz_strbuf_append(outbuf, "[ ");
			}
			while (size--) {
				// XXX this 999 is scary
				updateAddr(buf + i, 9999, endian, &addr, NULL);
				rz_mem_swaporcopy((ut8 *)&val_f, buf + i, sizeof(double), endian);
				if (elem == -1 || elem == 0) {
					rz_strbuf_appendf(outbuf, "%.17g", val_f);
					if (elem == 0) {
						elem = -2;
					}
				}
				if (size != 0 && elem == -1) {
					rz_strbuf_append(outbuf, ", ");
				}
				if (elem > -1) {
					elem--;
				}
				i += 8;
			}
			if (!SEEVALUE) {
				rz_strbuf_append(outbuf, " ]");
			}
		}
		if (MUSTSEEJSON) {
			rz_strbuf_appendf(outbuf, "}");
		}
	}
}

static void rz_type_format_word(RzStrBuf *outbuf, int endian, int mode,
	const char *setval, ut64 seeki, ut8 *buf, int i, int size) {
	ut64 addr;
	int elem = -1;
	if (size >= ARRAYINDEX_COEF) {
		elem = size / ARRAYINDEX_COEF - 1;
		size %= ARRAYINDEX_COEF;
	}
	addr = endian
		? (*(buf + i)) << 8 | (*(buf + i + 1))
		: (*(buf + i + 1)) << 8 | (*(buf + i));
	if (MUSTSET) {
		rz_strbuf_appendf(outbuf, "wv2 %s @ 0x%08" PFMT64x "\n", setval, seeki + ((elem >= 0) ? elem * 2 : 0));
	} else if ((mode & RZ_PRINT_DOT) || MUSTSEESTRUCT) {
		if (size == -1) {
			rz_strbuf_appendf(outbuf, "0x%04" PFMT64x, addr);
		}
		while ((size -= 2) > 0) {
			addr = endian
				? (*(buf + i)) << 8 | (*(buf + i + 1))
				: (*(buf + i + 1)) << 8 | (*(buf + i));
			if (elem == -1 || elem == 0) {
				rz_strbuf_appendf(outbuf, "%" PFMT64d, addr);
				if (elem == 0) {
					elem = -2;
				}
			}
			if (size != 0 && elem == -1) {
				rz_strbuf_append(outbuf, ",");
			}
			if (elem > -1) {
				elem--;
			}
			i += 2;
		}
	} else if (MUSTSEE) {
		if (!SEEVALUE && !ISQUIET) {
			rz_strbuf_appendf(outbuf, "0x%08" PFMT64x " = ", seeki + ((elem >= 0) ? elem * 2 : 0));
		}
		if (size == -1) {
			rz_strbuf_appendf(outbuf, "0x%04" PFMT64x, addr);
		} else {
			if (!SEEVALUE) {
				rz_strbuf_append(outbuf, "[ ");
			}
			while (size--) {
				addr = endian
					? (*(buf + i)) << 8 | (*(buf + i + 1))
					: (*(buf + i + 1)) << 8 | (*(buf + i));
				if (elem == -1 || elem == 0) {
					rz_strbuf_appendf(outbuf, "0x%04" PFMT64x, addr);
					if (elem == 0) {
						elem = -2;
					}
				}
				if (size != 0 && elem == -1) {
					rz_strbuf_append(outbuf, ", ");
				}
				if (elem > -1) {
					elem--;
				}
				i += 2;
			}
			if (!SEEVALUE) {
				rz_strbuf_append(outbuf, " ]");
			}
		}
	} else if (MUSTSEEJSON) {
		if (size == -1) {
			rz_strbuf_appendf(outbuf, "%" PFMT64d, addr);
		} else {
			rz_strbuf_append(outbuf, "[ ");
			while ((size -= 2) > 0) {
				addr = endian
					? (*(buf + i)) << 8 | (*(buf + i + 1))
					: (*(buf + i + 1)) << 8 | (*(buf + i));
				if (elem == -1 || elem == 0) {
					rz_strbuf_appendf(outbuf, "%" PFMT64d, addr);
					if (elem == 0) {
						elem = -2;
					}
				}
				if (size != 0 && elem == -1) {
					rz_strbuf_append(outbuf, ",");
				}
				if (elem > -1) {
					elem--;
				}
				i += 2;
			}
			rz_strbuf_append(outbuf, " ]");
		}
		rz_strbuf_append(outbuf, "}");
	}
}

static void rz_type_byte_escape(const RzPrint *p, const char *src, char **dst, int dot_nl) {
	rz_return_if_fail(p->strconv_mode);
	rz_str_byte_escape(src, dst, dot_nl, !strcmp(p->strconv_mode, "asciidot"), p->esc_bslash);
}

static void rz_type_format_nulltermstring(RzTypeDB *typedb, RzPrint *p, RzStrBuf *outbuf, int len, int endian, int mode,
	const char *setval, ut64 seeki, ut8 *buf, int i, int size) {
	if (!typedb->iob.is_valid_offset(typedb->iob.io, seeki, 1)) {
		ut8 ch = 0xff;
		// XXX there are some cases where the memory is there but is_valid_offset fails
		if (typedb->iob.read_at(typedb->iob.io, seeki, &ch, 1) != 1 && ch != 0xff) {
			rz_strbuf_append(outbuf, "-1");
			return;
		}
	}
	if (p->flags & RZ_PRINT_FLAGS_UNALLOC && !(typedb->iob.io->cached & RZ_PERM_R)) {
		ut64 total_map_left = 0;
		ut64 addr = seeki;
		RzIOMap *map;
		while (total_map_left < len && (map = typedb->iob.io->va ? typedb->iob.map_get(typedb->iob.io, addr) : typedb->iob.map_get_paddr(typedb->iob.io, addr)) && map->perm & RZ_PERM_R) {
			if (!map->itv.size) {
				total_map_left = addr == 0 ? UT64_MAX : UT64_MAX - addr + 1;
				break;
			}
			total_map_left += map->itv.size - (addr - (typedb->iob.io->va ? map->itv.addr : map->delta));
			addr += total_map_left;
		}
		if (total_map_left < len) {
			len = total_map_left;
		}
	}
	int str_len = rz_str_nlen((char *)buf + i, len - i);
	bool overflow = (size == -1 || size > len - i) && str_len == len - i;
	if (MUSTSET) {
		int buflen = strlen((const char *)buf + seeki);
		int vallen = strlen(setval);
		char *ons, *newstring = ons = strdup(setval);
		if ((newstring[0] == '\"' && newstring[vallen - 1] == '\"') || (newstring[0] == '\'' && newstring[vallen - 1] == '\'')) {
			newstring[vallen - 1] = '\0';
			newstring++;
			vallen -= 2;
		}
		if (vallen > buflen) {
			eprintf("Warning: new string is longer than previous one\n");
		}
		rz_strbuf_append(outbuf, "wx ");
		for (i = 0; i < vallen; i++) {
			if (i < vallen - 3 && newstring[i] == '\\' && newstring[i + 1] == 'x') {
				rz_strbuf_appendf(outbuf, "%c%c", newstring[i + 2], newstring[i + 3]);
				i += 3;
			} else {
				rz_strbuf_appendf(outbuf, "%2x", newstring[i]);
			}
		}
		rz_strbuf_appendf(outbuf, " @ 0x%08" PFMT64x "\n", seeki);
		free(ons);
	} else if ((mode & RZ_PRINT_DOT) || MUSTSEESTRUCT) {
		int j = i;
		(MUSTSEESTRUCT) ? rz_strbuf_append(outbuf, "\"") : rz_strbuf_append(outbuf, "\\\"");
		for (; j < len && ((size == -1 || size-- > 0) && buf[j]); j++) {
			char ch = buf[j];
			if (ch == '"') {
				rz_strbuf_append(outbuf, "\\\"");
			} else if (IS_PRINTABLE(ch)) {
				rz_strbuf_appendf(outbuf, "%c", ch);
			} else {
				rz_strbuf_append(outbuf, ".");
			}
		}
		(MUSTSEESTRUCT) ? rz_strbuf_append(outbuf, "\"") : rz_strbuf_append(outbuf, "\\\"");
	} else if (MUSTSEE) {
		int j = i;
		if (!SEEVALUE && !ISQUIET) {
			rz_strbuf_appendf(outbuf, "0x%08" PFMT64x " = %s", seeki, overflow ? "ovf " : "");
		}
		rz_strbuf_append(outbuf, "\"");
		for (; j < len && ((size == -1 || size-- > 0) && buf[j]); j++) {
			char esc_str[5] = { 0 };
			char *ptr = esc_str;
			rz_type_byte_escape(p, (char *)&buf[j], &ptr, false);
			rz_strbuf_appendf(outbuf, "%s", esc_str);
		}
		rz_strbuf_append(outbuf, "\"");
	} else if (MUSTSEEJSON) {
		char *utf_encoded_buf = NULL;
		rz_strbuf_append(outbuf, "\"");
		utf_encoded_buf = rz_str_escape_utf8_for_json(
			(char *)buf + i, size == -1 ? str_len : RZ_MIN(size, str_len));
		if (utf_encoded_buf) {
			rz_strbuf_appendf(outbuf, "%s", utf_encoded_buf);
			free(utf_encoded_buf);
		}
		rz_strbuf_append(outbuf, "\"");
		if (overflow) {
			rz_strbuf_append(outbuf, ",\"overflow\":true");
		}
		rz_strbuf_append(outbuf, "}");
	}
}

static void rz_type_format_nulltermwidestring(RzPrint *p, RzStrBuf *outbuf, const int len, int endian, int mode,
	const char *setval, ut64 seeki, ut8 *buf, int i, int size) {
	if (MUSTSET) {
		int vallen = strlen(setval);
		char *newstring, *ons;
		newstring = ons = strdup(setval);
		if ((newstring[0] == '\"' && newstring[vallen - 1] == '\"') || (newstring[0] == '\'' && newstring[vallen - 1] == '\'')) {
			newstring[vallen - 1] = '\0';
			newstring++;
			vallen -= 2;
		}
		if (vallen > rz_wstr_clen((char *)(buf + seeki))) {
			eprintf("Warning: new string is longer than previous one\n");
		}
		rz_strbuf_appendf(outbuf, "ww %s @ 0x%08" PFMT64x "\n", newstring, seeki);
		free(ons);
	} else if (MUSTSEE) {
		int j = i;
		if (!SEEVALUE && !ISQUIET) {
			rz_strbuf_appendf(outbuf, "0x%08" PFMT64x " = ", seeki);
		}
		for (; j < len && ((size == -1 || size-- > 0) && buf[j]); j += 2) {
			if (IS_PRINTABLE(buf[j])) {
				rz_strbuf_appendf(outbuf, "%c", buf[j]);
			} else {
				rz_strbuf_append(outbuf, ".");
			}
		}
	} else if (MUSTSEEJSON) {
		int j = i;
		rz_strbuf_appendf(outbuf, "%" PFMT64d ",\"string\":\"", seeki);
		for (; j < len && ((size == -1 || size-- > 0) && buf[j]); j += 2) {
			if (IS_PRINTABLE(buf[j])) {
				rz_strbuf_appendf(outbuf, "%c", buf[j]);
			} else {
				rz_strbuf_append(outbuf, ".");
			}
		}
		rz_strbuf_append(outbuf, "\"}");
	}
}

static void rz_type_format_bitfield(RzTypeDB *typedb, RzStrBuf *outbuf, ut64 seeki, char *fmtname,
	char *fieldname, ut64 addr, int mode, int size) {
	char *bitfield = NULL;
	addr &= (1ULL << (size * 8)) - 1;
	if (MUSTSEE && !SEEVALUE) {
		rz_strbuf_appendf(outbuf, "0x%08" PFMT64x " = ", seeki);
	}
	bitfield = rz_type_db_enum_get_bitfield(typedb, fmtname, addr);
	if (bitfield && *bitfield) {
		if (MUSTSEEJSON) {
			rz_strbuf_appendf(outbuf, "\"%s\"}", bitfield);
		} else if (MUSTSEE) {
			rz_strbuf_appendf(outbuf, "%s (bitfield) = %s\n", fieldname, bitfield);
		}
	} else {
		if (MUSTSEEJSON) {
			rz_strbuf_appendf(outbuf, "\"`tb %s 0x%" PFMT64x "`\"}", fmtname, addr);
		} else if (MUSTSEE) {
			rz_strbuf_appendf(outbuf, "%s (bitfield) = `tb %s 0x%" PFMT64x "`\n",
				fieldname, fmtname, addr);
		}
	}
	free(bitfield);
}

static void rz_type_format_enum(RzTypeDB *typedb, RzStrBuf *outbuf, ut64 seeki, char *fmtname,
	char *fieldname, ut64 addr, int mode, int size) {
	char *enumvalue = NULL;
	addr &= (1ULL << (size * 8)) - 1;
	if (MUSTSEE && !SEEVALUE) {
		rz_strbuf_appendf(outbuf, "0x%08" PFMT64x " = ", seeki);
	}
	enumvalue = rz_type_db_enum_member(typedb, fmtname, NULL, addr);
	if (enumvalue && *enumvalue) {
		if (mode & RZ_PRINT_DOT) {
			rz_strbuf_appendf(outbuf, "%s.%s", fmtname, enumvalue);
		} else if (MUSTSEEJSON) {
			rz_strbuf_appendf(outbuf, "%" PFMT64d ",\"label\":\"%s\",\"enum\":\"%s\"}",
				addr, enumvalue, fmtname);
		} else if (MUSTSEE) {
			rz_strbuf_appendf(outbuf, "%s (enum %s) = 0x%" PFMT64x " ; %s\n",
				fieldname, fmtname, addr, enumvalue);
		} else if (MUSTSEESTRUCT) {
			rz_strbuf_appendf(outbuf, "%s", enumvalue);
		}
	} else {
		if (MUSTSEEJSON) {
			rz_strbuf_appendf(outbuf, "%" PFMT64d ",\"enum\":\"%s\"}", addr, fmtname);
		} else if (MUSTSEE) {
			rz_strbuf_appendf(outbuf, "%s (enum %s) = 0x%" PFMT64x "\n", //`te %s 0x%x`\n",
				fieldname, fmtname, addr); //enumvalue); //fmtname, addr);
		}
	}
	free(enumvalue);
}

static void rz_print_format_register(RzStrBuf *outbuf, const RzPrint *p, int mode,
	const char *name, const char *setval) {
	if (!p || !p->get_register || !p->reg) {
		return;
	}
	RzRegItem *ri = p->get_register(p->reg, name, RZ_REG_TYPE_ALL);
	if (ri) {
		if (MUSTSET) {
			rz_strbuf_appendf(outbuf, "dr %s=%s\n", name, setval);
		} else if (MUSTSEE) {
			if (!SEEVALUE) {
				rz_strbuf_appendf(outbuf, "%s : 0x%08" PFMT64x "\n", ri->name, p->get_register_value(p->reg, ri));
			} else {
				rz_strbuf_appendf(outbuf, "0x%08" PFMT64x "\n", p->get_register_value(p->reg, ri));
			}
		} else if (MUSTSEEJSON) {
			rz_strbuf_appendf(outbuf, "%" PFMT64d "}", p->get_register_value(p->reg, ri));
		}
	} else {
		rz_strbuf_appendf(outbuf, "Register %s does not exists\n", name);
	}
}

static void rz_type_format_num_specifier(RzStrBuf *outbuf, ut64 addr, int bytes, int sign) {
#define EXT(T) (sign ? (signed T)(addr) : (unsigned T)(addr))
	const char *fs64 = sign ? "%" PFMT64d : "%" PFMT64u;
	const char *fs = sign ? "%d" : "%u";
	if (bytes == 1) {
		rz_strbuf_appendf(outbuf, fs, EXT(char));
	} else if (bytes == 2) {
		rz_strbuf_appendf(outbuf, fs, EXT(short));
	} else if (bytes == 4) {
		rz_strbuf_appendf(outbuf, fs, EXT(int)); //XXX: int is not necessarily 4 bytes I guess.
	} else if (bytes == 8) {
		rz_strbuf_appendf(outbuf, fs64, addr);
	}
#undef EXT
}

static void rz_type_format_num(RzStrBuf *outbuf, int endian, int mode, const char *setval, ut64 seeki, ut8 *buf, int i, int bytes, int sign, int size) {
	ut64 addr = 0LL;
	int elem = -1;
	if (size >= ARRAYINDEX_COEF) {
		elem = size / ARRAYINDEX_COEF - 1;
		size %= ARRAYINDEX_COEF;
	}
	if (bytes == 8) {
		updateAddr(buf + i, size - i, endian, NULL, &addr);
	} else {
		updateAddr(buf + i, size - i, endian, &addr, NULL);
	}
	if (MUSTSET) {
		rz_strbuf_appendf(outbuf, "wv%d %s @ 0x%08" PFMT64x "\n", bytes, setval, seeki + ((elem >= 0) ? elem * (bytes) : 0));
	} else if ((mode & RZ_PRINT_DOT) || MUSTSEESTRUCT) {
		rz_type_format_num_specifier(outbuf, addr, bytes, sign);
	} else if (MUSTSEE) {
		if (!SEEVALUE && !ISQUIET) {
			rz_strbuf_appendf(outbuf, "0x%08" PFMT64x " = ", seeki + ((elem >= 0) ? elem * bytes : 0));
		}
		if (size == -1) {
			rz_type_format_num_specifier(outbuf, addr, bytes, sign);
		} else {
			if (!SEEVALUE) {
				rz_strbuf_append(outbuf, "[ ");
			}
			while (size--) {
				if (bytes == 8) {
					updateAddr(buf + i, size - i, endian, NULL, &addr);
				} else {
					updateAddr(buf + i, size - i, endian, &addr, NULL);
				}
				if (elem == -1 || elem == 0) {
					rz_type_format_num_specifier(outbuf, addr, bytes, sign);
					if (elem == 0) {
						elem = -2;
					}
				}
				if (size != 0 && elem == -1) {
					rz_strbuf_append(outbuf, ", ");
				}
				if (elem > -1) {
					elem--;
				}
				i += bytes;
			}
			if (!SEEVALUE) {
				rz_strbuf_append(outbuf, " ]");
			}
		}
	} else if (MUSTSEEJSON) {
		if (size == -1) {
			rz_type_format_num_specifier(outbuf, addr, bytes, sign);
		} else {
			rz_strbuf_append(outbuf, "[ ");
			while (size--) {
				if (bytes == 8) {
					updateAddr(buf + i, size, endian, NULL, &addr);
				} else {
					updateAddr(buf + i, size, endian, &addr, NULL);
				}
				if (elem == -1 || elem == 0) {
					rz_type_format_num_specifier(outbuf, addr, bytes, sign);
					if (elem == 0) {
						elem = -2;
					}
				}
				if (size != 0 && elem == -1) {
					rz_strbuf_append(outbuf, ", ");
				}
				if (elem > -1) {
					elem--;
				}
				i += bytes;
			}
			rz_strbuf_append(outbuf, " ]");
		}
		rz_strbuf_append(outbuf, "}");
	}
}

RZ_API const char *rz_type_db_format_byname(RzTypeDB *typedb, const char *name) {
	return sdb_const_get(typedb->formats, name, NULL);
}

static char *fmt_struct_union(Sdb *TDB, char *var, bool is_typedef) {
	// assumes var list is sorted by offset.. should do more checks here
	char *p = NULL, *vars = NULL, var2[132], *fmt = NULL;
	size_t n;
	char *fields = rz_str_newf("%s.fields", var);
	char *nfields = (is_typedef) ? fields : var;
	for (n = 0; (p = sdb_array_get(TDB, nfields, n, NULL)); n++) {
		char *struct_name;
		const char *tfmt = NULL;
		bool isStruct = false;
		bool isEnum = false;
		bool isfp = false;
		snprintf(var2, sizeof(var2), "%s.%s", var, p);
		size_t alen = sdb_array_size(TDB, var2);
		int elements = sdb_array_get_num(TDB, var2, alen - 1, NULL);
		char *type = sdb_array_get(TDB, var2, 0, NULL);
		if (type) {
			char var3[128] = { 0 };
			// Handle general pointers except for char *
			if ((strstr(type, "*(") || strstr(type, " *")) && strncmp(type, "char *", 7)) {
				isfp = true;
			} else if (rz_str_startswith(type, "struct ")) {
				struct_name = type + 7;
				// TODO: iterate over all the struct fields, and format the format and vars
				snprintf(var3, sizeof(var3), "struct.%s", struct_name);
				tfmt = sdb_const_get(TDB, var3, NULL);
				isStruct = true;
			} else {
				// special case for char[]. Use char* format type without *
				if (!strcmp(type, "char") && elements > 0) {
					tfmt = sdb_const_get(TDB, "type.char *", NULL);
					if (tfmt && *tfmt == '*') {
						tfmt++;
					}
				} else {
					if (rz_str_startswith(type, "enum ")) {
						snprintf(var3, sizeof(var3), "%s", type + 5);
						isEnum = true;
					} else {
						snprintf(var3, sizeof(var3), "type.%s", type);
					}
					tfmt = sdb_const_get(TDB, var3, NULL);
				}
			}
			if (isfp) {
				// consider function pointer as void * for printing
				fmt = rz_str_append(fmt, "p");
				vars = rz_str_append(vars, p);
				vars = rz_str_append(vars, " ");
			} else if (tfmt) {
				(void)rz_str_replace_ch(type, ' ', '_', true);
				if (elements > 0) {
					fmt = rz_str_appendf(fmt, "[%d]", elements);
				}
				if (isStruct) {
					fmt = rz_str_append(fmt, "?");
					vars = rz_str_appendf(vars, "(%s)%s", struct_name, p);
					vars = rz_str_append(vars, " ");
				} else if (isEnum) {
					fmt = rz_str_append(fmt, "E");
					vars = rz_str_appendf(vars, "(%s)%s", type + 5, p);
					vars = rz_str_append(vars, " ");
				} else {
					fmt = rz_str_append(fmt, tfmt);
					vars = rz_str_append(vars, p);
					vars = rz_str_append(vars, " ");
				}
			} else {
				eprintf("Cannot resolve type '%s'\n", var3);
			}
			free(type);
		}
		free(p);
	}
	free(fields);
	fmt = rz_str_append(fmt, " ");
	fmt = rz_str_append(fmt, vars);
	free(vars);
	return fmt;
}

RZ_API char *rz_type_format(RzTypeDB *typedb, const char *t) {
	char var[130], var2[132];
	Sdb *TDB = typedb->sdb_types;
	const char *kind = sdb_const_get(TDB, t, NULL);
	if (!kind) {
		return NULL;
	}
	// only supports struct atm
	snprintf(var, sizeof(var), "%s.%s", kind, t);
	if (!strcmp(kind, "type")) {
		const char *fmt = sdb_const_get(TDB, var, NULL);
		if (fmt) {
			return strdup(fmt);
		}
	} else if (!strcmp(kind, "struct") || !strcmp(kind, "union")) {
		return fmt_struct_union(TDB, var, false);
	}
	if (!strcmp(kind, "typedef")) {
		snprintf(var2, sizeof(var2), "typedef.%s", t);
		const char *type = sdb_const_get(TDB, var2, NULL);
		// only supports struct atm
		if (type && !strcmp(type, "struct")) {
			return fmt_struct_union(TDB, var, true);
		}
	}
	return NULL;
}

// XXX: this is somewhat incomplete. must be updated to handle all format chars
RZ_API int rz_type_format_struct_size(RzTypeDB *typedb, const char *f, int mode, int n) {
	char *end, *args, *fmt;
	int size = 0, tabsize = 0, i, idx = 0, biggest = 0, fmt_len = 0, times = 1;
	bool tabsize_set = false;
	if (!f) {
		return -1;
	}
	if (n >= 5) { // This is the nesting level, is this not a bit arbitrary?!
		return 0;
	}
	const char *fmt2 = sdb_get(typedb->formats, f, NULL);
	if (!fmt2) {
		fmt2 = f;
	}
	char *o = strdup(fmt2);
	if (!o) {
		return -1;
	}
	end = strchr(o, ' ');
	fmt = o;
	if (!end && !(end = strchr(o, '\0'))) {
		free(o);
		return -1;
	}
	if (*end) {
		*end = 0;
		args = strdup(end + 1);
	} else {
		args = strdup("");
	}

	if (fmt[0] == '{') {
		char *end = strchr(fmt + 1, '}');
		if (!end) {
			eprintf("No end curly bracket.\n");
			free(o);
			free(args);
			return -1;
		}
		*end = '\0';
		times = rz_num_math(NULL, fmt + 1);
		fmt = end + 1;
	}
	if (fmt[0] == '0') {
		mode |= RZ_PRINT_UNIONMODE;
		fmt++;
	} else {
		mode &= ~RZ_PRINT_UNIONMODE;
	}

	int words = rz_str_word_set0_stack(args);
	fmt_len = strlen(fmt);
	for (i = 0; i < fmt_len; i++) {
		if (fmt[i] == '[') {
			char *end = strchr(fmt + i, ']');
			if (!end) {
				eprintf("No end bracket.\n");
				continue;
			}
			*end = '\0';
			tabsize_set = true;
			tabsize = rz_num_math(NULL, fmt + i + 1);
			*end = ']';
			while (fmt[i++] != ']') {
				;
			}
		} else {
			tabsize = 1;
		}

		switch (fmt[i]) {
		case '.':
			idx--;
		case 'c':
		case 'b':
		case 'X':
			size += tabsize * 1;
			break;
		case 'w':
			size += tabsize * 2;
			break;
		case ':':
			idx--;
		case 'd':
		case 'o':
		case 'i':
		case 'x':
		case 'f':
		case 's':
		case 't':
			size += tabsize * 4;
			break;
		case 'S':
		case 'q':
		case 'F':
			size += tabsize * 8;
			break;
		case 'Q': // uint128
			size += tabsize * 16;
			break;
		case 'z':
		case 'Z':
			size += tabsize;
			break;
		case '*':
			size += tabsize * (typedb->target->bits / 8);
			i++;
			idx--; //no need to go ahead for args
			break;
		case 'B':
		case 'E':
			if (tabsize_set) {
				if (tabsize < 1 || tabsize > 8) {
					eprintf("Unknown enum format size: %d\n", tabsize);
					break;
				}
				size += tabsize;
			} else {
				size += 4; // Assuming by default enum as int
			}
			break;
		case '?': {
			const char *wordAtIndex = NULL;
			const char *format = NULL;
			char *endname = NULL, *structname = NULL;
			char tmp = 0;
			if (words < idx) {
				eprintf("Index out of bounds\n");
			} else {
				wordAtIndex = rz_str_word_get0(args, idx);
			}
			if (!wordAtIndex) {
				break;
			}
			structname = strdup(wordAtIndex);
			if (*structname == '(') {
				endname = (char *)rz_str_rchr(structname, NULL, ')');
			} else {
				free(structname);
				break;
			}
			if (endname) {
				*endname = '\0';
			}
			format = strchr(structname, ' ');
			if (format) {
				tmp = *format;
				while (tmp == ' ') {
					format++;
					tmp = *format;
				}
			} else {
				format = sdb_get(typedb->formats, structname + 1, NULL);
				if (format && !strncmp(format, f, strlen(format) - 1)) { // Avoid recursion here
					free(o);
					free(structname);
					return -1;
				}
				if (!format) { // Fetch format from types db
					format = rz_type_format(typedb, structname + 1);
				}
			}
			if (!format) {
				eprintf("Cannot find format for struct `%s'\n", structname + 1);
				free(structname);
				free(o);
				return 0;
			}
			int newsize = rz_type_format_struct_size(typedb, format, mode, n + 1);
			if (newsize < 1) {
				eprintf("Cannot find size for `%s'\n", format);
				free(structname);
				free(o);
				return 0;
			}
			if (format) {
				size += tabsize * newsize;
			}
			free(structname);
		} break;
		case '{':
			while (fmt[i] != '}') {
				if (!fmt[i]) {
					free(o);
					free(args);
					return -1;
				}
				i++;
			}
			i++;
			idx--;
			break;
		case '}':
			free(o);
			free(args);
			return -1;
		case '+':
		case 'e':
			idx--;
			break;
		case 'p':
			if (fmt[i + 1] == '2') {
				size += tabsize * 2;
			} else if (fmt[i + 1] == '4') {
				size += tabsize * 4;
			} else if (fmt[i + 1] == '8') {
				size += tabsize * 8;
			} else {
				size += tabsize * (typedb->target->bits / 8);
				break;
			}
			i++;
			break;
		case 'r':
			break;
		case 'n':
		case 'N':
			if (fmt[i + 1] == '1') {
				size += tabsize * 1;
			} else if (fmt[i + 1] == '2') {
				size += tabsize * 2;
			} else if (fmt[i + 1] == '4') {
				size += tabsize * 4;
			} else if (fmt[i + 1] == '8') {
				size += tabsize * 8;
			} else {
				eprintf("Invalid n format in (%s)\n", fmt);
				free(o);
				free(args);
				return -2;
			}
			i++;
			break;
		case 'u':
		case 'D':
		case 'T':
			//TODO complete this.
		default:
			//idx--; //Does this makes sense?
			break;
		}
		idx++;
		if (mode & RZ_PRINT_UNIONMODE) {
			if (size > biggest) {
				biggest = size;
			}
			size = 0;
		}
	}
	size *= times;
	free(o);
	free(args);
	return (mode & RZ_PRINT_UNIONMODE) ? biggest : size;
}

static int rz_type_format_data_internal(RzTypeDB *typedb, RzPrint *p, RzStrBuf *outbuf, ut64 seek, const ut8 *b, const int len,
	const char *formatname, int mode, const char *setval, char *ofield);

static int rz_type_format_struct(RzTypeDB *typedb, RzPrint *p, RzStrBuf *outbuf, ut64 seek, const ut8 *b, int len, const char *name,
	int slide, int mode, const char *setval, char *field, int anon) {
	const char *fmt;
	char namefmt[128];
	slide++;
	if ((slide % STRUCTPTR) > NESTDEPTH || (slide % STRUCTFLAG) / STRUCTPTR > NESTDEPTH) {
		eprintf("Too much nested struct, recursion too deep...\n");
		return 0;
	}
	if (anon) {
		fmt = name;
	} else {
		fmt = sdb_get(typedb->formats, name, NULL);
		if (!fmt) { // Fetch struct info from types DB
			fmt = rz_type_format(typedb, name);
		}
	}
	if (!fmt || !*fmt) {
		eprintf("Undefined struct '%s'.\n", name);
		return 0;
	}
	if (MUSTSEE && !SEEVALUE) {
		snprintf(namefmt, sizeof(namefmt), "%%%ds", 10 + 6 * slide % STRUCTPTR);
		if (fmt[0] == '0') {
			rz_strbuf_appendf(outbuf, namefmt, "union");
		} else {
			rz_strbuf_appendf(outbuf, namefmt, "struct");
		}
		rz_strbuf_appendf(outbuf, "<%s>\n", name);
	}
	rz_type_format_data_internal(typedb, p, outbuf, seek, b, len, fmt, mode, setval, field);
	return rz_type_format_struct_size(typedb, fmt, mode, 0);
}

static char *get_args_offset(const char *arg) {
	char *args = strchr(arg, ' ');
	char *sq_bracket = strchr(arg, '[');
	int max = 30;
	if (args && sq_bracket) {
		char *csq_bracket = strchr(arg, ']');
		while (args && csq_bracket && csq_bracket > args && max--) {
			args = strchr(csq_bracket, ' ');
		}
	}
	return args;
}

static char *get_format_type(const char fmt, const char arg) {
	char *type = NULL;
	switch (fmt) {
	case 'b':
	case 'C':
		type = strdup("uint8_t");
		break;
	case 'c':
		type = strdup("int8_t");
		break;
	case 'd':
	case 'i':
	case 'o':
	case 'x':
		type = strdup("int32_t");
		break;
	case 'E':
		type = strdup("enum");
		break;
	case 'f':
		type = strdup("float");
		break;
	case 'F':
		type = strdup("double");
		break;
	case 'q':
		type = strdup("uint64_t");
		break;
	case 'u':
		type = strdup("uleb128_t");
		break;
	case 'Q':
		type = strdup("uint128_t");
		break;
	case 'w':
		type = strdup("uint16_t");
		break;
	case 'X':
		type = strdup("uint8_t[]");
		break;
	case 'D':
	case 's':
	case 'S':
	case 't':
	case 'z':
	case 'Z':
		type = strdup("char*");
		break;
	case 'n':
	case 'N':
		switch (arg) {
		case '1':
			type = strdup(fmt == 'n' ? "int8_t" : "uint8_t");
			break;
		case '2':
			type = strdup(fmt == 'n' ? "int16_t" : "uint16_t");
			break;
		case '4':
			type = strdup(fmt == 'n' ? "int32_t" : "uint32_t");
			break;
		case '8':
			type = strdup(fmt == 'n' ? "int64_t" : "uint64_t");
			break;
		}
		break;
	}
	return type;
}

#define MINUSONE ((void *)(size_t)-1)
#define ISSTRUCT (tmp == '?' || (tmp == '*' && *(arg + 1) == '?'))

RZ_API const char *rz_type_db_format_get(RzTypeDB *typedb, const char *name) {
	return sdb_get(typedb->formats, name, NULL);
}

RZ_API void rz_type_db_format_set(RzTypeDB *typedb, const char *name, const char *fmt) {
	sdb_set(typedb->formats, name, fmt, 0);
}

RZ_API RZ_OWN RzList *rz_type_db_format_all(RzTypeDB *typedb) {
	SdbListIter *iter;
	SdbKv *kv;
	RzList *fmtl = rz_list_newf(free);
	SdbList *sdbls = sdb_foreach_list(typedb->formats, true);
	ls_foreach (sdbls, iter, kv) {
		char *fmt = rz_str_newf("%s %s", sdbkv_key(kv), sdbkv_value(kv));
		rz_list_append(fmtl, fmt);
	}
	return fmtl;
}

RZ_API void rz_type_db_format_delete(RzTypeDB *typedb, const char *name) {
	sdb_unset(typedb->formats, name, 0);
}

RZ_API void rz_type_db_format_purge(RzTypeDB *typedb) {
	sdb_free(typedb->formats);
	typedb->formats = sdb_new0();
}

static int rz_type_format_data_internal(RzTypeDB *typedb, RzPrint *p, RzStrBuf *outbuf, ut64 seek, const ut8 *b, const int len,
	const char *formatname, int mode, const char *setval, char *ofield) {
	int nargs, i, invalid, nexti, idx, times, otimes, endian, isptr = 0;
	const int old_bits = typedb->target->bits;
	char *args = NULL, *bracket, tmp, last = 0;
	ut64 addr = 0, addr64 = 0, seeki = 0;
	static int slide = 0, oldslide = 0, ident = 4;
	char namefmt[32], *field = NULL;
	const char *arg = NULL;
	const char *fmt = NULL;
	const char *argend;
	int viewflags = 0;
	char *oarg = NULL;
	char *internal_format = NULL;

	/* Load format from name into fmt */
	if (!formatname) {
		return 0;
	}
	fmt = sdb_get(typedb->formats, formatname, NULL);
	if (!fmt) {
		fmt = formatname;
	}
	internal_format = strdup(fmt);
	fmt = internal_format;
	while (*fmt && IS_WHITECHAR(*fmt)) {
		fmt++;
	}
	argend = fmt + strlen(fmt);
	arg = fmt;

	nexti = nargs = i = 0;

	if (len < 1) {
		free(internal_format);
		return 0;
	}

	// len+2 to save space for the null termination in wide strings
	ut8 *buf = calloc(1, len + 2);
	if (!buf) {
		free(internal_format);
		return 0;
	}
	memcpy(buf, b, len);
	endian = typedb->target->big_endian;

	if (ofield && ofield != MINUSONE) {
		field = strdup(ofield);
	}
	/* get times */
	otimes = times = atoi(arg);
	if (times > 0) {
		while (IS_DIGIT(*arg)) {
			arg++;
		}
	}

	bracket = strchr(arg, '{');
	if (bracket) {
		char *end = strchr(arg, '}');
		if (!end) {
			eprintf("No end bracket. Try pf {ecx}b @ esi\n");
			goto beach;
		}
		*end = '\0';
		times = rz_num_math(NULL, bracket + 1);
		arg = end + 1;
	}

	if (*arg == '\0') {
		goto beach;
	}

	/* get args */
	args = get_args_offset(arg);
	if (args) {
		int l = 0, maxl = 0;
		argend = args;
		tmp = *args;
		while (tmp == ' ') {
			args++;
			tmp = *args;
		}
		args = strdup(args);
		nargs = rz_str_word_set0_stack(args);
		if (nargs == 0) {
			RZ_FREE(args);
		}
		for (i = 0; i < nargs; i++) {
			const char *tmp = rz_str_word_get0(args, i);
			const char *nm = rz_str_rchr(tmp, NULL, ')');
			int len = strlen(nm ? nm + 1 : tmp);
			if (len > maxl) {
				maxl = len;
			}
		}
		l++;
		const char *ends = " "; // XXX trailing space warning
		snprintf(namefmt, sizeof(namefmt), "%%%ds :%s",
			((maxl + 1) * (1 + slide)) % STRUCTPTR, ends);
	}
#define ISPOINTED ((slide % STRUCTFLAG) / STRUCTPTR <= (oldslide % STRUCTFLAG) / STRUCTPTR)
#define ISNESTED  ((slide % STRUCTPTR) <= (oldslide % STRUCTPTR))
	if (mode == RZ_PRINT_JSON && slide == 0) {
		rz_strbuf_append(outbuf, "[");
	}
	if (mode == RZ_PRINT_STRUCT) {
		if (formatname && *formatname) {
			if (strchr(formatname, ' ')) {
				rz_strbuf_append(outbuf, "struct {\n");
			} else {
				rz_strbuf_appendf(outbuf, "struct %s {\n", formatname);
			}
		} else {
			rz_strbuf_append(outbuf, "struct {\n");
		}
	}
	if (mode && arg[0] == '0') {
		mode |= RZ_PRINT_UNIONMODE;
		arg++;
	} else {
		mode &= ~RZ_PRINT_UNIONMODE;
	}
	if (mode & RZ_PRINT_DOT) {
		char *fmtname;
		if (formatname && *formatname) {
			if (strchr(formatname, ' ')) {
				fmtname = rz_str_newf("0x%" PFMT64x, seek);
			} else {
				fmtname = strdup(formatname);
			}
		} else {
			fmtname = rz_str_newf("0x%" PFMT64x, seek);
		}
		rz_strbuf_append(outbuf, "digraph g { graph [ rank=same; rankdir=LR; ];\n");
		rz_strbuf_appendf(outbuf, "root [ rank=1; shape=record\nlabel=\"%s", fmtname);
	}

	/* go format */
	i = 0;
	if (!times) {
		otimes = times = 1;
	}
	for (; times; times--) { // repeat N times
		const char *orig = arg;
		int first = 1;
		if (otimes > 1) {
			if (mode & RZ_PRINT_JSON) {
				if (otimes > times) {
					rz_strbuf_append(outbuf, ",");
				}
				rz_strbuf_appendf(outbuf, "[{\"index\":%d,\"offset\":%" PFMT64d "},", otimes - times, seek + i);
			} else if (mode) {
				rz_strbuf_appendf(outbuf, "0x%08" PFMT64x " [%d] {\n", seek + i, otimes - times);
			}
		}
		arg = orig;
		for (idx = 0; i < len && arg < argend && *arg; arg++) {
			int size = 0, elem = 0; /* size of the array, element of the array */
			char *fieldname = NULL, *fmtname = NULL;
			if (mode & RZ_PRINT_UNIONMODE) {
				i = 0;
			}
			seeki = seek + i;
			addr = 0LL;
			invalid = 0;
			typedb->target->bits = old_bits;
			if (arg[0] == '[') {
				char *end = strchr(arg, ']');
				if (!end) {
					eprintf("No end bracket.\n");
					goto beach;
				}
				*end = '\0';
				size = rz_get_size(typedb->num, buf, endian, arg + 1);
				arg = end + 1;
				*end = ']';
			} else {
				size = -1;
			}
			int fs = rz_type_format_struct_size(typedb, arg, 0, idx);
			if (fs == -2) {
				i = -1;
				goto beach;
			}
			if (fs < 1) {
				fs = 4;
			}
			if (i + fs - 1 < len) { // should be +7 to avoid oobread on 'q'
				// Max byte number where updateAddr will look into
				if (len - i < 7) {
					updateAddr(buf + i, THRESHOLD - (len - i), endian, &addr, &addr64);
				} else {
					updateAddr(buf + i, len - i, endian, &addr, &addr64);
				}
				if (typedb->target->bits == 64) {
					addr = addr64;
				}
			} else {
				// eprintf ("Format strings is too big for this buffer\n");
				goto beach;
			}

			tmp = *arg;

			if (mode && !args) {
				mode |= RZ_PRINT_ISFIELD;
			}
			if (!(mode & RZ_PRINT_QUIET)) {
				if (mode & RZ_PRINT_MUSTSEE && otimes > 1) {
					rz_strbuf_append(outbuf, "  ");
				}
			}
			if (idx < nargs && tmp != 'e' && isptr == 0) {
				char *dot = NULL, *bracket = NULL;
				if (field) {
					dot = strchr(field, '.');
				}
				if (dot) {
					*dot = '\0';
				}
				free(oarg);
				oarg = fieldname = strdup(rz_str_word_get0(args, idx));
				if (ISSTRUCT || tmp == 'E' || tmp == 'B' || tmp == 'r') {
					if (*fieldname == '(') {
						fmtname = fieldname + 1;
						fieldname = (char *)rz_str_rchr(fieldname, NULL, ')');
						if (fieldname) {
							*fieldname++ = '\0';
						} else {
							eprintf("Missing closing parenthesis in format ')'\n");
							goto beach;
						}
					} else {
						eprintf("Missing name (%s)\n", fieldname);
						goto beach;
					}
				}
				if (mode && (!args || (!field && ofield != MINUSONE) || (field && !strncmp(field, fieldname, strchr(field, '[') ? strchr(field, '[') - field : strlen(field) + 1)))) {
					mode |= RZ_PRINT_ISFIELD;
				} else {
					mode &= ~RZ_PRINT_ISFIELD;
				}

				/* There we handle specific element in array */
				if (field && (bracket = strchr(field, '[')) && mode & RZ_PRINT_ISFIELD) {
					char *end = strchr(field, ']');
					if (!end) {
						eprintf("Missing closing bracket\n");
						goto beach;
					}
					*end = '\0';
					elem = rz_num_math(NULL, bracket + 1) + 1; // +1 to handle 0 index easily
					for (; bracket < end; bracket++) {
						*bracket = '\0';
					}
					size += elem * ARRAYINDEX_COEF;
				} else {
					elem = -1;
				}
				if (tmp != '.' && tmp != ':') {
					idx++;
					if (MUSTSEE && !SEEVALUE) {
						if (!ISQUIET) {
							rz_strbuf_appendf(outbuf, namefmt, fieldname);
						}
					}
				}
			}
		feed_me_again:
			switch (isptr) {
			case PTRSEEK: {
				nexti = i + (typedb->target->bits / 8);
				i = 0;
				if (tmp == '?') {
					seeki = addr;
				}
				memset(buf, '\0', len);
				if (MUSTSEE && !ISQUIET) {
					rz_strbuf_appendf(outbuf, "(*0x%" PFMT64x ")", addr);
				}
				isptr = (addr) ? PTRBACK : NULLPTR;
				typedb->iob.read_at(typedb->iob.io, (ut64)addr, buf, len - 4);
				if (((i + 3) < len) || ((i + 7) < len)) {
					// XXX this breaks pf *D
					if (tmp != 'D') {
						updateAddr(buf + i, len - i, endian, &addr, &addr64);
					}
				} else {
					eprintf("(cannot read at 0x%08" PFMT64x ", block: %s, blocksize: 0x%x)\n",
						addr, b, len);
					rz_strbuf_append(outbuf, "\n");
					goto beach;
				}
			} break;
			case PTRBACK:
				// restore state after pointer seek
				i = nexti;
				memcpy(buf, b, len);
				isptr = NOPTR;
				arg--;
				continue;
			}
			if (tmp == 0 && last != '*') {
				break;
			}

			/* skip chars */
			switch (tmp) {
			case '*': // next char is a pointer
				isptr = PTRSEEK;
				arg++;
				tmp = *arg; //last;
				goto feed_me_again;
			case '+': // toggle view flags
				viewflags = !viewflags;
				continue;
			case 'e': // tmp swap endian
				endian ^= 1;
				continue;
			case ':': // skip 4 bytes
				if (size == -1) {
					i += 4;
				} else {
					while (size--) {
						i += 4;
					}
				}
				continue;
			case '.': // skip 1 byte
				i += (size == -1) ? 1 : size;
				continue;
			case 'p': // pointer reference
				if (*(arg + 1) == '2') {
					tmp = 'w';
					arg++;
				} else if (*(arg + 1) == '4') {
					tmp = 'x';
					arg++;
				} else if (*(arg + 1) == '8') {
					tmp = 'q';
					arg++;
				} else { //If pointer reference is not mentioned explicitly
					switch (typedb->target->bits) {
					case 16: tmp = 'w'; break;
					case 32: tmp = 'x'; break;
					default: tmp = 'q'; break;
					}
				}
				break;
			}

			/* flags */
			if (mode & RZ_PRINT_SEEFLAGS && isptr != NULLPTR) {
				char *newname = NULL;
				if (!fieldname) {
					newname = fieldname = rz_str_newf("pf.%" PFMT64u, seeki);
				}
				if (mode & RZ_PRINT_UNIONMODE) {
					rz_strbuf_appendf(outbuf, "f %s=0x%08" PFMT64x "\n", formatname, seeki);
					goto beach;
				} else if (tmp == '?') {
					rz_strbuf_appendf(outbuf, "f %s.%s_", fmtname, fieldname);
				} else if (tmp == 'E') {
					rz_strbuf_appendf(outbuf, "f %s=0x%08" PFMT64x "\n", fieldname, seeki);
				} else if (slide / STRUCTFLAG > 0 && idx == 1) {
					rz_strbuf_appendf(outbuf, "%s=0x%08" PFMT64x "\n", fieldname, seeki);
				} else {
					rz_strbuf_appendf(outbuf, "f %s=0x%08" PFMT64x "\n", fieldname, seeki);
				}
				if (newname) {
					RZ_FREE(newname);
					fieldname = NULL;
				}
			}

			/* dot */
			if (mode & RZ_PRINT_DOT) {
				if (fieldname) {
					rz_strbuf_appendf(outbuf, "|{0x%" PFMT64x "|%c|%s|<%s>",
						seeki, tmp, fieldname, fieldname);
				} else {
					rz_strbuf_appendf(outbuf, "|{0x%" PFMT64x "|%c|",
						seeki, tmp);
				}
			}

			/* json */
			if (MUSTSEEJSON && mode & RZ_PRINT_JSON) {
				if (oldslide <= slide) {
					if (first) {
						first = 0;
					} else {
						rz_strbuf_append(outbuf, ",");
					}
				} else if (oldslide) {
					rz_strbuf_append(outbuf, "]},");
					oldslide -= NESTEDSTRUCT;
				}
				if (fieldname) {
					rz_strbuf_appendf(outbuf, "{\"name\":\"%s\",\"type\":\"", fieldname);
				} else {
					rz_strbuf_append(outbuf, "{\"type\":\"");
				}
				if (ISSTRUCT) {
					rz_strbuf_appendf(outbuf, "%s", fmtname);
				} else {
					if (tmp == 'n' || tmp == 'N') {
						rz_strbuf_appendf(outbuf, "%c%c", tmp, *(arg + 1));
					} else {
						rz_strbuf_appendf(outbuf, "%c", tmp);
					}
				}
				if (isptr) {
					rz_strbuf_append(outbuf, "*");
				}
				rz_strbuf_appendf(outbuf, "\",\"offset\":%" PFMT64d ",\"value\":",
					isptr ? (seek + nexti - (typedb->target->bits / 8)) : seek + i);
			}

			/* c struct */
			if (MUSTSEESTRUCT) {
				char *type = get_format_type(tmp, (tmp == 'n' || tmp == 'N') ? arg[1] : 0);
				if (type) {
					rz_strbuf_appendf(outbuf, "%*c%s %s; // ", ident, ' ', type, fieldname);
				} else {
					rz_strbuf_appendf(outbuf, "%*cstruct %s {", ident, ' ', fieldname);
				}
				free(type);
			}
			bool noline = false;

			int oi = i;
			if (isptr == NULLPTR) {
				if (MUSTSEEJSON) {
					rz_strbuf_append(outbuf, "\"NULL\"}");
				} else if (MUSTSEE) {
					rz_strbuf_append(outbuf, " NULL\n");
				}
				isptr = PTRBACK;
			} else {
				/* format chars */
				// before to enter in the switch statement check buf boundaries due to  updateAddr
				// might go beyond its len and it's usually called in each of the following functions
				switch (tmp) {
				case 'u':
					i += rz_type_format_uleb(outbuf, endian, mode, setval, seeki, buf, i, size);
					break;
				case 't':
					rz_type_format_time(outbuf, endian, mode, setval, seeki, buf, i, size);
					i += (size == -1) ? 4 : 4 * size;
					break;
				case 'q':
					rz_type_format_quadword(outbuf, endian, mode, setval, seeki, buf, i, size);
					i += (size == -1) ? 8 : 8 * size;
					break;
				case 'Q':
					rz_type_format_u128(outbuf, endian, mode, setval, seeki, buf, i, size);
					i += (size == -1) ? 16 : 16 * size;
					break;
				case 'b':
					rz_type_format_byte(outbuf, endian, mode, setval, seeki, buf, i, size);
					i += (size == -1) ? 1 : size;
					break;
				case 'C':
					rz_type_format_decchar(outbuf, endian, mode, setval, seeki, buf, i, size);
					i += (size == -1) ? 1 : size;
					break;
				case 'c':
					rz_type_format_char(outbuf, endian, mode, setval, seeki, buf, i, size);
					i += (size == -1) ? 1 : size;
					break;
				case 'X':
					size = rz_type_format_hexpairs(outbuf, endian, mode, setval, seeki, buf, i, size);
					i += size;
					break;
				case 'T':
					if (rz_type_format_10bytes(typedb, outbuf, mode,
						    setval, seeki, addr, buf) == 0) {
						i += (size == -1) ? 4 : 4 * size;
					}
					break;
				case 'f':
					rz_type_format_float(outbuf, endian, mode, setval, seeki, buf, i, size);
					i += (size == -1) ? 4 : 4 * size;
					break;
				case 'F':
					rz_type_format_double(outbuf, endian, mode, setval, seeki, buf, i, size);
					i += (size == -1) ? 8 : 8 * size;
					break;
				case 'i':
					rz_type_format_int(outbuf, endian, mode, setval, seeki, buf, i, size);
					i += (size == -1) ? 4 : 4 * size;
					break;
				case 'd': //WHY?? help says: 0x%%08x hexadecimal value (4 bytes)
					rz_type_format_hex(outbuf, endian, mode, setval, seeki, buf, i, size);
					i += (size == -1) ? 4 : 4 * size;
					break;
				/*
				case 'D':
					if (isptr) {
						if (typedb->target->bits == 64) {
							i += rz_print_format_disasm(p, addr64, size);
						} else {
							i += rz_print_format_disasm(p, addr, size);
						}
					} else {
						i += rz_print_format_disasm(p, seeki, size);
					}
					break;
				*/
				case 'o':
					rz_type_format_octal(outbuf, endian, mode, setval, seeki, buf, i, size);
					i += (size == -1) ? 4 : 4 * size;
					break;
				case ';':
					noline = true;
					i -= (size == -1) ? 4 : 4 * size;
					if (i < 0) {
						i = 0;
					}
					break;
				case ',':
					noline = true;
					i -= (size == -1) ? 1 : size;
					if (i < 0) {
						i = 0;
					}
					break;
				case 'x':
					rz_type_format_hexflag(outbuf, endian, mode, setval, seeki, buf, i, size);
					i += (size == -1) ? 4 : 4 * size;
					break;
				case 'w':
					rz_type_format_word(outbuf, endian, mode, setval, seeki, buf, i, size);
					i += (size == -1) ? 2 : 2 * size;
					break;
				case 'z': // zero terminated string
					rz_type_format_nulltermstring(typedb, p, outbuf, len, endian, mode, setval, seeki, buf, i, size);
					if (size == -1) {
						i += strlen((char *)buf + i) + 1;
					} else {
						while (size--) {
							i++;
						}
					}
					break;
				case 'Z': // zero terminated wide string
					rz_type_format_nulltermwidestring(p, outbuf, len, endian, mode, setval, seeki, buf, i, size);
					if (size == -1) {
						i += rz_wstr_clen((char *)(buf + i)) * 2 + 2;
					} else {
						while (size--) {
							i += 2;
						}
					}
					break;
				case 's':
					if (rz_type_format_string(typedb, outbuf, seeki, addr64, addr, 0, mode) == 0) {
						i += (size == -1) ? 4 : 4 * size;
					}
					break;
				case 'S':
					if (rz_type_format_string(typedb, outbuf, seeki, addr64, addr, 1, mode) == 0) {
						i += (size == -1) ? 8 : 8 * size;
					}
					break;
				case 'B': // resolve bitfield
					if (size >= ARRAYINDEX_COEF) {
						size %= ARRAYINDEX_COEF;
					}
					rz_type_format_bitfield(typedb, outbuf, seeki, fmtname, fieldname, addr, mode, size);
					i += (size == -1) ? 1 : size;
					break;
				case 'E': // resolve enum
					if (size >= ARRAYINDEX_COEF) {
						size %= ARRAYINDEX_COEF;
					}
					rz_type_format_enum(typedb, outbuf, seeki, fmtname, fieldname, addr, mode, size);
					i += (size == -1) ? 1 : size;
					break;
				case 'r':
					if (fmtname) {
						rz_print_format_register(outbuf, p, mode, fmtname, setval);
					} else {
						eprintf("Unknown register\n");
					}
					break;
				case '?': {
					int s = 0;
					char *nxtfield = NULL;
					char *format = NULL;
					int anon = 0;
					if (size >= ARRAYINDEX_COEF) {
						elem = size / ARRAYINDEX_COEF - 1;
						size %= ARRAYINDEX_COEF;
					}
					if (!(mode & RZ_PRINT_ISFIELD)) {
						nxtfield = MINUSONE;
					} else if (field) {
						nxtfield = strchr(ofield, '.');
					}
					if (nxtfield != MINUSONE && nxtfield) {
						nxtfield++;
					}

					if (MUSTSEE) {
						if (!SEEVALUE) {
							rz_strbuf_append(outbuf, "\n");
						}
					}
					if (MUSTSEEJSON) {
						if (isptr) {
							rz_strbuf_appendf(outbuf, "%" PFMT64d "},", seeki);
						} else {
							rz_strbuf_append(outbuf, "[");
						}
					}
					if (MUSTSEESTRUCT) {
						if (isptr) {
							rz_strbuf_appendf(outbuf, "%" PFMT64d, seeki);
						} else {
							ident += 4;
							rz_strbuf_append(outbuf, "\n");
						}
					}
					if (mode & RZ_PRINT_SEEFLAGS) {
						slide += STRUCTFLAG;
					}
					if (!fmtname) {
						break;
					}
					format = strchr(fmtname, ' ');
					if (format) {
						anon = 1;
						fmtname = format;
						while (*fmtname == ' ') {
							fmtname++;
						}
					}
					oldslide = slide;
					//slide += (isptr) ? STRUCTPTR : NESTEDSTRUCT;
					slide += NESTEDSTRUCT;
					if (size == -1) {
						s = rz_type_format_struct(typedb, p, outbuf, seeki,
							buf + i, len - i, fmtname, slide,
							mode, setval, nxtfield, anon);
						i += (isptr) ? (typedb->target->bits / 8) : s;
						if (MUSTSEEJSON) {
							if (!isptr && (!arg[1] || arg[1] == ' ')) {
								rz_strbuf_append(outbuf, "]}");
							}
						}
					} else {
						if (mode & RZ_PRINT_ISFIELD) {
							if (!SEEVALUE) {
								rz_strbuf_append(outbuf, "[\n");
							}
						}
						while (size--) {
							if (mode && (elem == -1 || elem == 0)) {
								mode |= RZ_PRINT_MUSTSEE;
								if (elem == 0) {
									elem = -2;
								}
							} else {
								mode &= ~RZ_PRINT_MUSTSEE;
							}
							s = rz_type_format_struct(typedb, p, outbuf, seek + i,
								buf + i, len - i, fmtname, slide, mode, setval, nxtfield, anon);
							if ((MUSTSEE || MUSTSEEJSON || MUSTSEESTRUCT) && size != 0 && elem == -1) {
								if (MUSTSEEJSON) {
									rz_strbuf_append(outbuf, ",");
								} else if (MUSTSEE || MUSTSEESTRUCT) {
									rz_strbuf_append(outbuf, "\n");
								}
							}
							if (elem > -1) {
								elem--;
							}
							i += (isptr) ? (typedb->target->bits / 8) : s;
						}
						if (mode & RZ_PRINT_ISFIELD) {
							if (!SEEVALUE) {
								rz_strbuf_append(outbuf, "]\n");
							}
						}
						if (MUSTSEEJSON) {
							rz_strbuf_append(outbuf, "]}");
						}
					}
					oldslide = slide;
					//slide -= (isptr) ? STRUCTPTR : NESTEDSTRUCT;
					slide -= NESTEDSTRUCT;
					if (mode & RZ_PRINT_SEEFLAGS) {
						oldslide = slide;
						slide -= STRUCTFLAG;
					}
					break;
				}
				case 'n':
				case 'N': {
					int bytes = 0;
					int sign = (tmp == 'n') ? 1 : 0;
					if (arg[1] == '1') {
						bytes = 1;
					} else if (arg[1] == '2') {
						bytes = 2;
					} else if (arg[1] == '4') {
						bytes = 4;
					} else if (arg[1] == '8') {
						bytes = 8;
					} else {
						invalid = 1;
						break;
						//or goto beach;???
					}
					rz_type_format_num(outbuf, endian, mode, setval, seeki, buf, i, bytes, sign, size);
					i += (size == -1) ? bytes : size * bytes;
					arg++;
					break;
				}
				default:
					/* ignore unknown chars */
					invalid = 1;
					break;
				} //switch
			}
			if (MUSTSEESTRUCT) {
				if (oldslide) {
					ident -= 4;
					rz_strbuf_appendf(outbuf, "%*c}", ident, ' ');
					oldslide -= NESTEDSTRUCT;
				}
				rz_strbuf_append(outbuf, "\n");
			}
			if (mode & RZ_PRINT_DOT) {
				rz_strbuf_append(outbuf, "}");
			}
			if (mode & RZ_PRINT_SEEFLAGS && isptr != NULLPTR) {
				int sz = i - oi;
				if (sz > 1) {
					rz_strbuf_appendf(outbuf, "fl %d @ 0x%08" PFMT64x "\n", sz, seeki);
					rz_strbuf_appendf(outbuf, "Cd %d @ 0x%08" PFMT64x "\n", sz, seeki);
				}
			}
			if (viewflags && p->offname) {
				const char *s = p->offname(p->user, seeki);
				if (s) {
					rz_strbuf_appendf(outbuf, "@(%s)", s);
				}
				s = p->offname(p->user, addr);
				if (s) {
					rz_strbuf_appendf(outbuf, "*(%s)", s);
				}
			}
			if (!noline && tmp != 'D' && !invalid && !fmtname && MUSTSEE) {
				rz_strbuf_append(outbuf, "\n");
			}
			last = tmp;

			// XXX: Due to the already noted issues with the above, we need to strip
			// args from fmt:args the same way we strip fmt BUT only for enums as
			// nested structs seem to be handled correctly above!
			if (arg[0] == 'E') {
				char *end_fmt = strchr(arg, ' ');
				if (!end_fmt) {
					goto beach;
				}
				char *next_args = strchr(end_fmt + 1, ' ');
				if (next_args) {
					while (*next_args != '\0') {
						*end_fmt++ = *next_args++;
					}
				}
				*end_fmt = '\0';
			}
		}
		if (otimes > 1) {
			if (MUSTSEEJSON) {
				rz_strbuf_append(outbuf, "]");
			} else if (mode) {
				rz_strbuf_append(outbuf, "}\n");
			}
		}
		arg = orig;
		oldslide = 0;
	}
	if (mode & RZ_PRINT_JSON && slide == 0) {
		rz_strbuf_append(outbuf, "]\n");
	}
	if (MUSTSEESTRUCT && slide == 0) {
		rz_strbuf_append(outbuf, "}\n");
	}
	if (mode & RZ_PRINT_DOT) {
		rz_strbuf_append(outbuf, "\"];\n}\n");
		// TODO: show nested structs and field reference lines
	}
beach:
	if (slide == 0) {
		oldslide = 0;
	}
	free(internal_format);
	free(oarg);
	free(buf);
	free(field);
	free(args);
	return i;
}

RZ_API char *rz_type_format_data(RzTypeDB *typedb, RzPrint *p, ut64 seek, const ut8 *b, const int len,
	const char *formatname, int mode, const char *setval, char *ofield) {
	RzStrBuf *outbuf = rz_strbuf_new("");
	rz_type_format_data_internal(typedb, p, outbuf, seek, b, len, formatname, mode, setval, ofield);
	char *outstr = rz_strbuf_drain(outbuf);
	return outstr;
}
