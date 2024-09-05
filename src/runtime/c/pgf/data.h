#ifndef PGF_DATA_H_
#define PGF_DATA_H_

#include <string.h>
#include <assert.h>
#include <exception>
#include <stdexcept>
#include <functional>
#include <queue>
#include <map>
#include <set>

#include "pgf.h"

#ifdef _WIN32
#include <windows.h>
typedef SSIZE_T ssize_t;
#endif

#ifdef __APPLE__
#include <sys/errno.h>
#endif

#ifdef EMSCRIPTEN
#include <errno.h>
#include <stdio.h>
#endif

#ifdef _MSC_VER
#include <malloc.h>
#define alloca _alloca
#define strdup _strdup
#pragma warning(disable : 4996)
#endif


class PGF_INTERNAL_DECL pgf_error : public std::runtime_error {
public:
    pgf_error(const char *msg) : std::runtime_error(msg)
    {
        this->msg = msg;
    }

    virtual const char *what() const throw ()
    {
    	return msg;
    }

private:
    const char *msg;
};

class PGF_INTERNAL_DECL pgf_systemerror : public std::runtime_error {
public:
    pgf_systemerror(int code) : std::runtime_error("pgf_systemerror")
    {
        this->m_code     = code;
        this->m_filepath = NULL;
    }

    pgf_systemerror(int code, const char *filepath) : std::runtime_error("pgf_systemerror")
    {
        this->m_code     = code;
        this->m_filepath = filepath;
    }

    virtual int code() const
    {
        return m_code;
    }

    const char *filepath() const
    {
        return m_filepath;
    }

private:
    int m_code;
    const char *m_filepath;
};

struct PgfPGF;
struct PgfAbsFun;
struct PgfConcr;

#include "db.h"
#include "text.h"
#include "vector.h"
#include "namespace.h"
#include "phrasetable.h"
#include "probspace.h"
#include "expr.h"

struct PGF_INTERNAL_DECL PgfFlag {
    PgfLiteral value;
    PgfText name;

    static void release(ref<PgfFlag> pgf);
};

struct PGF_INTERNAL_DECL PgfAbsFun {
    ref<PgfDTyp> type;
	int arity;
    ref<char> bytecode;
    prob_t prob;
    PgfText name;

    static void release(ref<PgfAbsFun> cat);
};

struct PGF_INTERNAL_DECL PgfAbsCat {
	vector<PgfHypo> context;
	prob_t prob;
    PgfText name;

    static void release(ref<PgfAbsCat> cat);
};

typedef struct {
	ref<PgfText> name;
    Namespace<PgfFlag> aflags;
    Namespace<PgfAbsFun> funs;
    Namespace<PgfAbsCat> cats;
    PgfProbspace funs_by_cat;
} PgfAbstr;

typedef struct {
    size_t factor;
    size_t var;
} term;

struct PGF_INTERNAL_DECL PgfLParam {
    size_t i0;
    size_t n_terms;
    term terms[];

    static void release(ref<PgfLParam> param);
};

struct PGF_INTERNAL_DECL PgfVariableRange {
    size_t var;
    size_t range;
};

struct PGF_INTERNAL_DECL PgfPArg {
    ref<PgfLParam> param;
};

struct PGF_INTERNAL_DECL PgfPResult {
    vector<PgfVariableRange> vars; 
    PgfLParam param;

    static void release(ref<PgfPResult> res);
};

typedef object PgfSymbol;

struct PGF_INTERNAL_DECL PgfSequence {
	inline_vector<PgfSymbol> syms;

    static void release(ref<PgfSequence> seq);
};

struct PGF_INTERNAL_DECL PgfSequenceBackref {
    object container;
    size_t seq_index;
};

struct PGF_INTERNAL_DECL PgfSymbolCat {
    static const uint8_t tag = 0;
    size_t d;
    PgfLParam r;
};

struct PGF_INTERNAL_DECL PgfSymbolLit {
    static const uint8_t tag = 1;
    size_t d;
    PgfLParam r;
};

struct PGF_INTERNAL_DECL PgfSymbolVar {
    static const uint8_t tag = 2;
    size_t d, r;
};

struct PGF_INTERNAL_DECL PgfSymbolKS {
    static const uint8_t tag = 3;
    PgfText token;
};

struct PGF_INTERNAL_DECL PgfAlternative {
	ref<PgfSequence> form;
	/**< The form of this variant as a list of tokens. */

	vector<ref<PgfText>> prefixes;
	/**< The prefixes of the following symbol that trigger this
	 * form. */
};

struct PGF_INTERNAL_DECL PgfSymbolKP {
    static const uint8_t tag = 4;
    ref<PgfSequence> default_form;
    inline_vector<PgfAlternative> alts;
};

struct PGF_INTERNAL_DECL PgfSymbolBIND {
    static const uint8_t tag = 5;
};

struct PGF_INTERNAL_DECL PgfSymbolSOFTBIND {
    static const uint8_t tag = 6;
};

struct PGF_INTERNAL_DECL PgfSymbolNE {
    static const uint8_t tag = 7;
};

struct PGF_INTERNAL_DECL PgfSymbolSOFTSPACE {
    static const uint8_t tag = 8;
};

struct PGF_INTERNAL_DECL PgfSymbolCAPIT {
    static const uint8_t tag = 9;
};

struct PGF_INTERNAL_DECL PgfSymbolALLCAPIT {
    static const uint8_t tag = 10;
};

struct PGF_INTERNAL_DECL PgfConcrLincat {
    static const uint8_t tag = 0;

    ref<PgfAbsCat> abscat;

    size_t n_lindefs;
    vector<PgfPArg> args;
    vector<ref<PgfPResult>> res;
    vector<ref<PgfSequence>> seqs;
    vector<ref<PgfText>> fields;

    PgfText name;

    static void release(ref<PgfConcrLincat> lincat);
};

struct PGF_INTERNAL_DECL PgfConcrLin {
    static const uint8_t tag = 1;

    ref<PgfAbsFun> absfun;
    ref<PgfConcrLincat> lincat;

    vector<PgfPArg> args;
    vector<ref<PgfPResult>> res;
    vector<ref<PgfSequence>> seqs;

    PgfText name;

    static void release(ref<PgfConcrLin> lin);
};

struct PGF_INTERNAL_DECL PgfConcrPrintname {
    ref<PgfText> printname;
    PgfText name;

    static void release(ref<PgfConcrPrintname> printname);
};

#define containerof(T,field,p) (T*) (((char*) p)-offsetof(T,field))

struct PGF_INTERNAL_DECL PgfLCEdge {
    struct {
        ref<PgfConcrLincat> lincat;
        struct {
            size_t i0;
            term& operator[](int i) {
                PgfLCEdge *edge = containerof(PgfLCEdge,from.value,this);
                return edge->terms[i];
            }
            size_t size() {
                PgfLCEdge *edge = containerof(PgfLCEdge,from.value,this);
                return edge->from.lin_idx.n_offset;
            }
        } value;
        struct {
            size_t i0;
            size_t n_offset;
            term& operator[](int i) {
                PgfLCEdge *edge = containerof(PgfLCEdge,from.lin_idx,this);
                return edge->terms[n_offset+i];
            }
            size_t size() {
                PgfLCEdge *edge = containerof(PgfLCEdge,from.lin_idx,this);
                return edge->to.value.n_offset-n_offset;
            }
        } lin_idx;
    } from;

    struct {
        ref<PgfConcrLincat> lincat;
        struct {
            size_t i0;
            size_t n_offset;
            term& operator[](int i) {
                PgfLCEdge *edge = containerof(PgfLCEdge,to.value,this);
                return edge->terms[n_offset+i];
            }
            size_t size() {
                PgfLCEdge *edge = containerof(PgfLCEdge,to.value,this);
                return edge->to.lin_idx.n_offset-n_offset;
            }
        } value;
        struct {
            size_t i0;
            size_t n_offset;
            term& operator[](int i) {
                PgfLCEdge *edge = containerof(PgfLCEdge,to.lin_idx,this);
                return edge->terms[n_offset+i];
            }
            size_t size() {
                PgfLCEdge *edge = containerof(PgfLCEdge,to.lin_idx,this);
                return edge->n_terms-n_offset;
            }
        } lin_idx;
    } to;

    struct {
        size_t n_vars;
        PgfVariableRange& operator[](int i) {
            PgfLCEdge *edge = containerof(PgfLCEdge,vars,this);
            return ((PgfVariableRange*)(((term*) (edge+1))+edge->n_terms))[i];
        }
        size_t size() {
            return n_vars;
        }
    } vars;

    size_t n_terms;
    term terms[];

    static ref<PgfLCEdge> alloc(size_t n_terms1, size_t n_terms2, size_t n_terms3, size_t n_terms4, size_t n_vars) {
        auto edge = PgfDB::malloc<PgfLCEdge>((n_terms1+n_terms2+n_terms3+n_terms4)*sizeof(term)+n_vars*sizeof(PgfVariableRange));
        edge->from.lin_idx.n_offset = n_terms1;
        edge->to.value.n_offset = n_terms1+n_terms2;
        edge->to.lin_idx.n_offset = n_terms1+n_terms2+n_terms3;
        edge->n_terms = n_terms1+n_terms2+n_terms3+n_terms4;
        edge->vars.n_vars = n_vars;
        return edge;
    }
};

struct PGF_INTERNAL_DECL PgfLRShift {
    size_t next_state;
    ref<PgfConcrLincat> lincat;
    size_t r;
};

struct PGF_INTERNAL_DECL PgfLRShiftKS {
    size_t next_state;
    ref<PgfSequence> seq;
    size_t sym_idx;
};

struct PgfLRReduceArg;

struct PGF_INTERNAL_DECL PgfLRProduction {
    ref<PgfConcrLin> lin;
    size_t index;
    vector<ref<PgfLRReduceArg>> args;
};

struct PGF_INTERNAL_DECL PgfLRReduceArg {
    static const uint8_t tag = 2;

    size_t id;
    size_t n_prods;
    PgfLRProduction prods[];
};

struct PGF_INTERNAL_DECL PgfLRReduce {
    object lin_obj;
    size_t seq_idx;
    size_t depth;

    struct Arg {
        ref<PgfLRReduceArg> arg;
        size_t stk_idx;
    };

    vector<Arg> args;
};

struct PGF_INTERNAL_DECL PgfLRState {
    vector<PgfLRShift> shifts;
    vector<PgfLRShiftKS> tokens;
    size_t next_bind_state;
    vector<PgfLRReduce> reductions;
};

struct PGF_INTERNAL_DECL PgfConcr {
    static const uint8_t tag = 1;

    Namespace<PgfFlag> cflags;
    Namespace<PgfConcrLin> lins;
    Namespace<PgfConcrLincat> lincats;
    PgfPhrasetable phrasetable;
    Namespace<PgfConcrPrintname> printnames;

    vector<PgfLRState> lrtable;

    PgfText name;

    static void release(ref<PgfConcr> pgf);
};

struct PGF_INTERNAL_DECL PgfPGF {
    static const uint8_t tag = 0;

	uint16_t major_version;
	uint16_t minor_version;
	Namespace<PgfFlag> gflags;
	PgfAbstr abstract;
    Namespace<PgfConcr> concretes;

    static void release(ref<PgfPGF> pgf);
};

#endif
