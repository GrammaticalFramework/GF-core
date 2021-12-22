#include "data.h"
#include "reader.h"
#include <math.h>
#include <string.h>

PgfReader::PgfReader(FILE *in)
{
    this->in = in;
    this->abstract = 0;
}

uint8_t PgfReader::read_uint8()
{
    uint8_t b;
    size_t n_bytes = fread((char*) &b, sizeof(b), 1, in);
    if (feof(in))
        throw pgf_error("reached end of file while reading the grammar");
    if (ferror(in))
        throw pgf_error("an error occured while reading the grammar");

	return b;
}

uint16_t PgfReader::read_u16be()
{
    uint8_t buf[2];
    size_t n_bytes = fread((char*) &buf, sizeof(buf), 1, in);
    if (feof(in))
        throw pgf_error("reached end of file while reading a grammar");
    if (ferror(in))
        throw pgf_error("an error occured while reading the grammar");

	return (((uint16_t) buf[0]) << 8 | buf[1]);
}

uint64_t PgfReader::read_u64be()
{
    uint8_t buf[8];
    size_t n_bytes = fread((char*) &buf, sizeof(buf), 1, in);
    if (feof(in))
        throw pgf_error("reached end of file while reading a grammar");
    if (ferror(in))
        throw pgf_error("an error occured while reading the grammar");

	return (((uint64_t) buf[0]) << 56 |
            ((uint64_t) buf[1]) << 48 |
            ((uint64_t) buf[2]) << 40 |
            ((uint64_t) buf[3]) << 32 |
            ((uint64_t) buf[4]) << 24 |
            ((uint64_t) buf[5]) << 16 |
            ((uint64_t) buf[6]) << 8  |
            ((uint64_t) buf[7]));
}

double PgfReader::read_double()
{
    uint64_t u = read_u64be();

    bool sign = u >> 63;
	unsigned rawexp = u >> 52 & 0x7ff;
	uint64_t mantissa = u & 0xfffffffffffff;
	double ret;

	if (rawexp == 0x7ff) {
		ret = (mantissa == 0) ? INFINITY : NAN;
	} else {
		uint64_t m = rawexp ? 1ULL << 52 | mantissa : mantissa << 1;
		ret = ldexp((double) m, rawexp - 1075);
	}
	return sign ? copysign(ret, -1.0) : ret;
}

uint64_t PgfReader::read_uint()
{
	uint64_t u = 0;
	int shift = 0;
	uint8_t b = 0;
	do {
		b = read_uint8();
		u |= (b & ~0x80) << shift;
		shift += 7;
	} while (b & 0x80);
	return u;
}

object PgfReader::read_name_internal(size_t struct_size)
{
    size_t size = read_len();
	object offs = current_db->malloc_internal(struct_size+sizeof(PgfText)+size+1);
    PgfText* ptext = (PgfText*) (current_base+offs+struct_size);
    ptext->size = size;

    // If reading the extra bytes causes EOF, it is an encoding
    // error, not a legitimate end of character stream.
    fread(ptext->text, size, 1, in);
    if (feof(in))
        throw pgf_error("utf8 decoding error");
    if (ferror(in))
        throw pgf_error("an error occured while reading the grammar");

    ptext->text[size] = 0;

	return offs;
}

object PgfReader::read_text_internal(size_t struct_size)
{
    size_t len  = read_len();

    char* buf = (char*) alloca(len*6+1);
	char* p   = buf;
	for (size_t i = 0; i < len; i++) {
        uint8_t c = read_uint8();
        *(p++) = (char) c;

        if (c < 0x80) {
            continue;
        }
        if (c < 0xc2) {
            throw pgf_error("utf8 decoding error");
        }

        int len = (c < 0xe0 ? 1 :
                   c < 0xf0 ? 2 :
                   c < 0xf8 ? 3 :
                   c < 0xfc ? 4 :
                              5
                   );
        // If reading the extra bytes causes EOF, it is an encoding
        // error, not a legitimate end of character stream.
        fread(p, len, 1, in);
        if (feof(in))
            throw pgf_error("utf8 decoding error");
        if (ferror(in))
            throw pgf_error("an error occured while reading the grammar");

        p += len;
	}

    size_t size = p-buf;
	*p++ = 0;

	object offs = current_db->malloc_internal(struct_size+sizeof(PgfText)+size+1);
    PgfText* ptext = (PgfText*) (current_base+offs+struct_size);
    ptext->size = size;
	memcpy(ptext->text, buf, size+1);

	return offs;
}

template<class V>
Namespace<V> PgfReader::read_namespace(ref<V> (PgfReader::*read_value)(), size_t len)
{
    if (len == 0)
        return 0;

    size_t half = len/2;
    Namespace<V> left  = read_namespace(read_value, half);
    ref<V> value = (this->*read_value)();
    Namespace<V> right = read_namespace(read_value, len-half-1);

    return Node<V>::new_node(value, left, right);
}

template<class V>
Namespace<V> PgfReader::read_namespace(ref<V> (PgfReader::*read_value)())
{
    size_t len = read_len();
    return read_namespace(read_value, len);
}

template<class V>
void PgfReader::merge_namespace(ref<V> (PgfReader::*read_value)())
{
    size_t len = read_len();
    for (size_t i = 0; i < len; i++) {
        ref<V> value = (this->*read_value)();
        V::release(value);
    }
}

template <class C, class V>
ref<C> PgfReader::read_vector(Vector<V> C::* field, void (PgfReader::*read_value)(ref<V> val))
{
    size_t len = read_len();
    ref<C> loc = vector_new<C,V>(field,len);
    for (size_t i = 0; i < len; i++) {
        (this->*read_value)(vector_elem(ref<Vector<V>>::from_ptr(&(loc->*field)),i));
    }
    return loc;
}

template <class V>
ref<Vector<V>> PgfReader::read_vector(void (PgfReader::*read_value)(ref<V> val))
{
    size_t len = read_len();
    ref<Vector<V>> vec = vector_new<V>(len);
    for (size_t i = 0; i < len; i++) {
        (this->*read_value)(vector_elem(vec,i));
    }
    return vec;
}

PgfLiteral PgfReader::read_literal()
{
    PgfLiteral lit = 0;

    uint8_t tag = read_tag();
    switch (tag) {
	case PgfLiteralStr::tag: {
		ref<PgfLiteralStr> lit_str =
            read_text<PgfLiteralStr>(&PgfLiteralStr::val);
        lit = ref<PgfLiteralStr>::tagged(lit_str);
		break;
	}
	case PgfLiteralInt::tag: {
        size_t size = read_len();
		ref<PgfLiteralInt> lit_int =
			PgfDB::malloc<PgfLiteralInt>(sizeof(uintmax_t)*size);
        lit_int->size   = size;
        for (size_t i = 0; i < size; i++) {
            lit_int->val[i] = (uintmax_t) read_uint();
        }
        lit = ref<PgfLiteralInt>::tagged(lit_int);
		break;
	}
	case PgfLiteralFlt::tag: {
		ref<PgfLiteralFlt> lit_flt =
			current_db->malloc<PgfLiteralFlt>();
		lit_flt->val = read_double();
        lit = ref<PgfLiteralFlt>::tagged(lit_flt);
		break;
	}
	default:
		throw pgf_error("Unknown literal tag");
	}
	return lit;
}

ref<PgfFlag> PgfReader::read_flag()
{
    ref<PgfFlag> flag = read_name(&PgfFlag::name);
    flag->ref_count = 1;
    flag->value = read_literal();
    return flag;
}

PgfExpr PgfReader::read_expr()
{
    PgfExpr expr = 0;
    uint8_t tag = read_tag();

    switch (tag) {
	case PgfExprAbs::tag:{
        PgfBindType bind_type = (PgfBindType) read_tag();
        ref<PgfExprAbs> eabs = read_name(&PgfExprAbs::name);
        eabs->bind_type = bind_type;
        eabs->body = read_expr();
        expr = ref<PgfExprAbs>::tagged(eabs);
		break;
	}
	case PgfExprApp::tag: {
        ref<PgfExprApp> eapp = PgfDB::malloc<PgfExprApp>();
		eapp->fun = read_expr();
		eapp->arg = read_expr();
        expr = ref<PgfExprApp>::tagged(eapp);
		break;
	}
	case PgfExprLit::tag: {
        ref<PgfExprLit> elit = PgfDB::malloc<PgfExprLit>();
		elit->lit = read_literal();
		expr = ref<PgfExprLit>::tagged(elit);
		break;
	}
	case PgfExprMeta::tag: {
		ref<PgfExprMeta> emeta = PgfDB::malloc<PgfExprMeta>();
		emeta->id = read_int();
		expr = ref<PgfExprMeta>::tagged(emeta);
		break;
	}
	case PgfExprFun::tag: {
		ref<PgfExprFun> efun = read_name(&PgfExprFun::name);
        expr = ref<PgfExprFun>::tagged(efun);
		break;
	}
	case PgfExprVar::tag: {
        ref<PgfExprVar> evar = PgfDB::malloc<PgfExprVar>();
		evar->var = read_int();
        expr = ref<PgfExprVar>::tagged(evar);
		break;
	}
	case PgfExprTyped::tag: {
        ref<PgfExprTyped> etyped = PgfDB::malloc<PgfExprTyped>();
		etyped->expr = read_expr();
		etyped->type = read_type();
        expr = ref<PgfExprTyped>::tagged(etyped);
		break;
	}
	case PgfExprImplArg::tag: {
        ref<PgfExprImplArg> eimpl = current_db->malloc<PgfExprImplArg>();
		eimpl->expr = read_expr();
        expr = ref<PgfExprImplArg>::tagged(eimpl);
		break;
	}
	default:
		throw pgf_error("Unknown expression tag");
	}

    return expr;
}

void PgfReader::read_hypo(ref<PgfHypo> hypo)
{
    hypo->bind_type = (PgfBindType) read_tag();
	hypo->cid = read_name();
	hypo->type = read_type();
}

ref<PgfDTyp> PgfReader::read_type()
{
    ref<Vector<PgfHypo>> hypos =
        read_vector<PgfHypo>(&PgfReader::read_hypo);
    ref<PgfDTyp> tp = read_name<PgfDTyp>(&PgfDTyp::name);
    tp->hypos = hypos;
    tp->exprs =
        read_vector<PgfExpr>(&PgfReader::read_expr);
    return tp;
}

ref<PgfAbsFun> PgfReader::read_absfun()
{
    ref<PgfAbsFun> absfun =
        read_name<PgfAbsFun>(&PgfAbsFun::name);
    absfun->ref_count = 1;
    ref<PgfExprFun> efun =
        ref<PgfExprFun>::from_ptr((PgfExprFun*) &absfun->name);
    absfun->type = read_type();
	absfun->arity = read_int();

    uint8_t tag = read_tag();
	switch (tag) {
	case 0:
        absfun->bytecode = 0;
        break;
    case 1: {
        read_len();
        absfun->bytecode = PgfDB::malloc<char>(0);
        break;
    }
    default:
        throw pgf_error("Unknown tag, 0 or 1 expected");
    }
    absfun->prob = - log(read_double());
    return absfun;
}

ref<PgfAbsCat> PgfReader::read_abscat()
{
    ref<PgfAbsCat> abscat = read_name<PgfAbsCat>(&PgfAbsCat::name);
    abscat->ref_count = 1;
    abscat->context = read_vector<PgfHypo>(&PgfReader::read_hypo);

    // for now we just read the set of functions per category and ignore them
    size_t n_funs = read_len();
    for (size_t i = 0; i < n_funs; i++) {
        read_double();
        read_name();
    }

    abscat->prob  = - log(read_double());
    return abscat;
}

void PgfReader::read_abstract(ref<PgfAbstr> abstract)
{
    this->abstract = abstract;

    abstract->name = read_name();
	abstract->aflags = read_namespace<PgfFlag>(&PgfReader::read_flag);
    abstract->funs = read_namespace<PgfAbsFun>(&PgfReader::read_absfun);
    abstract->cats = read_namespace<PgfAbsCat>(&PgfReader::read_abscat);
}

void PgfReader::merge_abstract(ref<PgfAbstr> abstract)
{
    this->abstract = abstract;

    read_name(); // ?
	merge_namespace<PgfFlag>(&PgfReader::read_flag); // ?
    merge_namespace<PgfAbsFun>(&PgfReader::read_absfun); // ?
    merge_namespace<PgfAbsCat>(&PgfReader::read_abscat); // ?
}

ref<PgfLParam> PgfReader::read_lparam()
{
    size_t i0 = read_int();
    size_t n_terms = read_len();
    ref<PgfLParam> lparam =
        PgfDB::malloc<PgfLParam>(n_terms*sizeof(PgfLParam::terms[0]));
    lparam->i0 = i0;
    lparam->n_terms = n_terms;

    for (size_t i = 0; i < n_terms; i++) {
        lparam->terms[i].factor = read_int();
        lparam->terms[i].var    = read_int();
    }

    return lparam;
}

void PgfReader::read_variable_range(ref<PgfVariableRange> var_info)
{
    var_info->var   = read_int();
    var_info->range = read_int();
}

void PgfReader::read_parg(ref<PgfPArg> parg)
{
    parg->param  = read_lparam();
}

ref<PgfPResult> PgfReader::read_presult()
{
    ref<Vector<PgfVariableRange>> vars = 0;
    size_t n_vars = read_len();
    if (n_vars > 0) {
        vars = vector_new<PgfVariableRange>(n_vars);
        for (size_t i = 0; i < n_vars; i++) {
            read_variable_range(vector_elem(vars,i));
        }
    }

    size_t i0 = read_int();
    size_t n_terms = read_len();
    ref<PgfPResult> res =
        PgfDB::malloc<PgfPResult>(n_terms*sizeof(PgfLParam::terms[0]));
    res->vars = vars;
    res->param.i0 = i0;
    res->param.n_terms = n_terms;

    for (size_t i = 0; i < n_terms; i++) {
        res->param.terms[i].factor = read_int();
        res->param.terms[i].var    = read_int();
    }

    return res;
}

template<class I>
ref<I> PgfReader::read_symbol_idx()
{
    size_t d = read_int();
    size_t i0 = read_int();
    size_t n_terms = read_len();
    ref<I> sym_idx =
        PgfDB::malloc<I>(n_terms*sizeof(PgfLParam::terms[0]));
    sym_idx->d = d;
    sym_idx->r.i0 = i0;
    sym_idx->r.n_terms = n_terms;

    for (size_t i = 0; i < n_terms; i++) {
        sym_idx->r.terms[i].factor = read_int();
        sym_idx->r.terms[i].var    = read_int();
    }

    return sym_idx;
}

PgfSymbol PgfReader::read_symbol()
{
    PgfSymbol sym = 0;

    uint8_t tag = read_tag();
    switch (tag) {
	case PgfSymbolCat::tag: {
        ref<PgfSymbolCat> sym_cat = read_symbol_idx<PgfSymbolCat>();
        sym = ref<PgfSymbolCat>::tagged(sym_cat);
		break;
    }
	case PgfSymbolLit::tag: {
        ref<PgfSymbolLit> sym_lit = read_symbol_idx<PgfSymbolLit>();
        sym = ref<PgfSymbolLit>::tagged(sym_lit);
		break;
    }
	case PgfSymbolVar::tag: {
        ref<PgfSymbolVar> sym_var = PgfDB::malloc<PgfSymbolVar>();
        sym_var->d = read_int();
        sym_var->r = read_int();
        sym = ref<PgfSymbolVar>::tagged(sym_var);
		break;
    }
	case PgfSymbolKS::tag: {
        ref<PgfSymbolKS> sym_ks = read_text(&PgfSymbolKS::token);
        sym = ref<PgfSymbolKS>::tagged(sym_ks);
		break;
    }
	case PgfSymbolKP::tag: {
        size_t n_alts = read_len();
        ref<PgfSymbolKP> sym_kp = PgfDB::malloc<PgfSymbolKP>(n_alts*sizeof(PgfAlternative));
        sym_kp->alts.len = n_alts;

        for (size_t i = 0; i < n_alts; i++) {
            auto form     = read_vector(&PgfReader::read_symbol2);
            auto prefixes = read_vector(&PgfReader::read_text2);

            sym_kp->alts.data[i].form     = form;
            sym_kp->alts.data[i].prefixes = prefixes;
        }

        auto default_form = read_vector(&PgfReader::read_symbol2);
        sym_kp->default_form = default_form;

        sym = ref<PgfSymbolKP>::tagged(sym_kp);
		break;
    }
	case PgfSymbolBIND::tag: {
        sym = ref<PgfSymbolBIND>::tagged(0);
		break;
    }
	case PgfSymbolSOFTBIND::tag: {
        sym = ref<PgfSymbolSOFTBIND>::tagged(0);
		break;
    }
	case PgfSymbolNE::tag: {
        sym = ref<PgfSymbolNE>::tagged(0);
		break;
    }
	case PgfSymbolSOFTSPACE::tag: {
        sym = ref<PgfSymbolSOFTSPACE>::tagged(0);
		break;
    }
	case PgfSymbolCAPIT::tag: {
        sym = ref<PgfSymbolCAPIT>::tagged(0);
		break;
    }
	case PgfSymbolALLCAPIT::tag: {
        sym = ref<PgfSymbolALLCAPIT>::tagged(0);
		break;
    }
	default:
		throw pgf_error("Unknown symbol tag");
    }

    return sym;
}

ref<PgfConcrLincat> PgfReader::read_lincat()
{
    ref<PgfConcrLincat> lincat = read_name(&PgfConcrLincat::name);
    lincat->ref_count = 1;
    lincat->abscat = namespace_lookup(abstract->cats, &lincat->name);
    lincat->fields = read_vector(&PgfReader::read_text2);
    lincat->n_lindefs = read_len();
    lincat->args = read_vector(&PgfReader::read_parg);
    lincat->res  = read_vector(&PgfReader::read_presult2);
    lincat->seqs = read_vector(&PgfReader::read_seq2);
    return lincat;
}

ref<PgfConcrLin> PgfReader::read_lin()
{
    ref<PgfConcrLin> lin = read_name(&PgfConcrLin::name);
    lin->ref_count = 1;
    lin->absfun = namespace_lookup(abstract->funs, &lin->name);
    lin->args = read_vector(&PgfReader::read_parg);
    lin->res  = read_vector(&PgfReader::read_presult2);
    lin->seqs = read_vector(&PgfReader::read_seq2);
    return lin;
}

ref<PgfConcrPrintname> PgfReader::read_printname()
{
    ref<PgfConcrPrintname> printname = read_name(&PgfConcrPrintname::name);
    printname->ref_count = 1;
    printname->printname = read_text();
    return printname;
}

ref<PgfConcr> PgfReader::read_concrete()
{
    ref<PgfConcr> concr = read_name(&PgfConcr::name);
    concr->ref_count    = 1;
    concr->ref_count_ex = 0;
	concr->cflags = read_namespace<PgfFlag>(&PgfReader::read_flag);
	concr->lincats = read_namespace<PgfConcrLincat>(&PgfReader::read_lincat);
	concr->lins = read_namespace<PgfConcrLin>(&PgfReader::read_lin);
	concr->printnames = read_namespace<PgfConcrPrintname>(&PgfReader::read_printname);
    concr->prev = 0;
    concr->next = 0;
    return concr;
}

ref<PgfPGF> PgfReader::read_pgf()
{
    ref<PgfPGF> pgf = PgfDB::malloc<PgfPGF>(master_size+1);

    pgf->ref_count = 1;
    pgf->major_version = read_u16be();
    pgf->minor_version = read_u16be();

    if (pgf->major_version != PGF_MAJOR_VERSION ||
        pgf->minor_version != PGF_MINOR_VERSION) {
        throw pgf_error("Unsupported format version");
    }

    pgf->gflags = read_namespace<PgfFlag>(&PgfReader::read_flag);

    read_abstract(ref<PgfAbstr>::from_ptr(&pgf->abstract));

    pgf->concretes = read_namespace<PgfConcr>(&PgfReader::read_concrete);

    pgf->prev = 0;
    pgf->next = 0;

    pgf->name.size = master_size;
    memcpy(&pgf->name.text, master_text, master_size+1);

    return pgf;
}

void PgfReader::merge_pgf(ref<PgfPGF> pgf)
{
    uint16_t major_version = read_u16be();
    uint16_t minor_version = read_u16be();

    if (pgf->major_version != PGF_MAJOR_VERSION ||
        pgf->minor_version != PGF_MINOR_VERSION) {
        throw pgf_error("Unsupported format version");
    }

    merge_namespace<PgfFlag>(&PgfReader::read_flag); // ??

    merge_abstract(ref<PgfAbstr>::from_ptr(&pgf->abstract));

    size_t len = read_len();
    for (size_t i = 0; i < len; i++) {
        ref<PgfConcr> concr = PgfReader::read_concrete();
        Namespace<PgfConcr> concrs =
            namespace_insert(pgf->concretes, concr);
        namespace_release(pgf->concretes);
        pgf->concretes = concrs;
    }
}
