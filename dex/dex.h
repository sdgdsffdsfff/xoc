/*@
XOC Release License

Copyright (c) 2013-2014, Alibaba Group, All rights reserved.

    compiler@aliexpress.com

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the Su Zhenyu nor the names of its contributors
      may be used to endorse or promote products derived from this software
      without specific prior written permission.

THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS
BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

author: Su Zhenyu
@*/
#ifndef _DEX_H_
#define _DEX_H_

#ifdef _ENABLE_LOG_
#define LOG ALOGE
#else
#define LOG
#endif

typedef enum _INVOKE_KIND {
	INVOKE_UNDEF = 0,
	INVOKE_VIRTUAL,
	INVOKE_SUPER,
	INVOKE_DIRECT,
	INVOKE_STATIC,
	INVOKE_INTERFACE,
	INVOKE_VIRTUAL_RANGE,
	INVOKE_SUPER_RANGE,
	INVOKE_DIRECT_RANGE,
	INVOKE_STATIC_RANGE,
	INVOKE_INTERFACE_RANGE,
} INVOKE_KIND;

typedef enum _BLTIN_TYPE {
	BLTIN_UNDEF = 0,
	BLTIN_INVOKE,
	BLTIN_NEW,
	BLTIN_NEW_ARRAY,
	BLTIN_MOVE_EXP,
	BLTIN_MOVE_RES,
	BLTIN_THROW,
	BLTIN_CHECK_CAST,
	BLTIN_FILLED_NEW_ARRAY,
	BLTIN_FILL_ARRAY_DATA,
	BLTIN_CONST_CLASS,
	BLTIN_ARRAY_LENGTH,
	BLTIN_MONITOR_ENTER,
	BLTIN_MONITOR_EXIT,
	BLTIN_INSTANCE_OF,
	BLTIN_CMP_BIAS
} BLTIN_TYPE;


#define BLTIN_type(t)			(g_builtin_info[(t)].ty)
#define BLTIN_name(t)			(g_builtin_info[(t)].name)
class BLTIN_INFO {
public:
	BLTIN_TYPE ty;
	CHAR const* name;
};


extern UINT g_builtin_num;
extern BLTIN_INFO g_builtin_info[];


class VAR2UINT : public TMap<VAR const*, UINT> {
public:
	VAR2UINT() {}
	virtual ~VAR2UINT() {}

	UINT mget(VAR const* v)
	{
		bool find;
		UINT i = TMap<VAR const*, UINT>::get(v, &find);
		ASSERT0(find);
		return i;
	}
};


class Prno2UINT : public HMap<UINT, UINT, HashFuncBase2<UINT> > {
public:
	INT maxreg; //record the max vreg used.
	UINT paramnum; //record the number of parameter.

	Prno2UINT(UINT sz = 0) : HMap<UINT, UINT, HashFuncBase2<UINT> >(sz)
	{
		maxreg = -1;
		paramnum = 0;
	}
	virtual ~Prno2UINT() {}

	UINT get(UINT prno)
	{
		bool find = false;
		UINT x = HMap<UINT, UINT, HashFuncBase2<UINT> >::get(prno, &find);
		ASSERT0(find);
		return x;
	}

	UINT get_mapped_vreg(UINT prno)
	{
		UINT mapped = 0;
		bool f = HMap<UINT, UINT, HashFuncBase2<UINT> >::find(prno, &mapped);
		UNUSED(f);
		ASSERT(f, ("prno should be mapped with vreg in dex2ir"));
		//return HMap<UINT, UINT, HashFuncBase2<UINT> >::get(prno);
		return mapped;
	}

	UINT get(UINT prno, bool * find)
	{ return HMap<UINT, UINT, HashFuncBase2<UINT> >::get(prno, find); }

	void set(UINT prno, UINT v)
	{
		ASSERT0(prno > 0);
		HMap<UINT, UINT, HashFuncBase2<UINT> >::set(prno, v);
	}

	void copy(Prno2UINT & src)
	{
		INT cur;
		for (UINT prno = src.get_first(cur); cur >= 0; prno = src.get_next(cur)) {
			UINT v = src.get(prno);
			set(prno, v);
		}
		maxreg = src.maxreg;
		paramnum = src.paramnum;
	}

	void dump()
	{
		if (g_tfile == NULL) { return; }
		fprintf(g_tfile, "\n==---- DUMP Prno2UINT ----==");
		g_indent = 4;

		if (maxreg < 0) {
			fprintf(g_tfile, "\n==------ PR to Vreg is unordered ------==");
			INT cur;
			for (UINT prno = get_first(cur); cur >= 0; prno = get_next(cur)) {
				UINT v = get(prno);
				fprintf(g_tfile, "\nPR%d->v%d", prno, v);
			}
		} else {
			INT cur;
			for (INT i = 0; i <= maxreg; i++) {
				bool find = false;
				for (UINT prno = get_first(cur); cur >= 0; prno = get_next(cur)) {
					UINT v = get(prno);
					if (v == (UINT)i) {
						fprintf(g_tfile, "\nPR%d -> v%d", prno, v);
						find = true;
						break;
					}
				}

				if (!find) {
					fprintf(g_tfile, "\n-- -> v%d", i);
				}
			}
		}
		fflush(g_tfile);
	}
};


class UINT2PR : public Vector<IR*> {
public:
	void dump()
	{
		if (g_tfile == NULL) { return; }

		fprintf(g_tfile, "\n==---- DUMP Prno2UINT ----==");

		for (INT i = 0; i <= get_last_idx(); i++) {
			IR * pr = get(i);
			if (pr == NULL) {
				fprintf(g_tfile, "\nv%d -> --", i);
			}
			fprintf(g_tfile, "\nv%d -> PR%u", i, pr->get_prno());
		}
		fflush(g_tfile);
	}
};


class STR2INTRI : public HMap<CHAR const*, BLTIN_TYPE, HashFuncString> {
public:
	STR2INTRI(UINT sz = 13) : HMap<CHAR const*, BLTIN_TYPE, HashFuncString>(sz)
	{
		for (UINT i = BLTIN_UNDEF + 1; i < g_builtin_num; i++) {
			set(BLTIN_name((BLTIN_TYPE)i), (BLTIN_TYPE)i);
		}
	}
	virtual ~STR2INTRI() {}
};


//Map from typeIdx of type-table to positionIdx in file-class-def-table.
class FieldTypeIdx2PosIdx : public TMap<UINT, UINT> {
public:
};


inline bool is_us4(UINT value)
{ return value == (value & 0xf); }

inline bool is_s4(INT value)
{ return (value >= -8) && (value <= 7); }

inline bool is_us8(UINT value)
{ return value == (value & 0xff); }

inline bool is_s8(INT value)
{ return (CHAR)value == value; }

inline bool is_us16(UINT value)
{ return value == (value & 0xffff); }

inline bool is_s16(INT value)
{ return (SHORT) value == value; }

inline bool is_swide16(LONGLONG value)
{ return (LONGLONG)((SHORT)value) == value; }

inline bool is_swide32(LONGLONG value)
{ return (LONGLONG)((INT)value) == value; }

//Record type to facilitate type comparation.
class TypeIndexRep {
public:
	Type const* i8;
	Type const* u8;
	Type const* i16;
	Type const* u16;
	Type const* i32;
	Type const* u32;
	Type const* i64;
	Type const* u64;
	Type const* f32;
	Type const* f64;
	Type const* b;
	Type const* ptr;
	Type const* array;
	Type const* obj_lda_base_type;
	Type const* uint16x4;
	Type const* int16x4;
	Type const* uint32x4;
	Type const* int32x4;
	Type const* uint64x2;
	Type const* int64x2;

public:
	TypeIndexRep() { memset(this, 0, sizeof(TypeIndexRep)); }
};


//Perform Dex register allocation.
extern bool g_do_dex_ra;
#endif
