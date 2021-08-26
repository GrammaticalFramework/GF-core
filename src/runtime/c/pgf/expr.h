#ifndef EXPR_H_
#define EXPR_H_

/// An abstract syntax tree
typedef variant PgfExpr;

struct PgfHypo;
struct PgfType;

/// A literal for an abstract syntax tree
typedef variant PgfLiteral;

struct PGF_INTERNAL_DECL PgfLiteralStr {
    static const uint8_t tag = 0;

	PgfText val;
} ;

struct PGF_INTERNAL_DECL PgfLiteralInt {
    static const uint8_t tag = 1;

	int val;
} ;

struct PGF_INTERNAL_DECL PgfLiteralFlt {
    static const uint8_t tag = 2;

	double val;
};

struct PGF_INTERNAL_DECL PgfHypo {
	PgfBindType bind_type;
	ref<PgfText> cid;
	ref<PgfType> type;
};

struct PGF_INTERNAL_DECL PgfType {
	ref<PgfVector<PgfHypo>> hypos;
    ref<PgfVector<PgfExpr>> exprs;
	PgfText name;
};

struct PGF_INTERNAL_DECL PgfExprAbs {
    static const uint8_t tag = 0;
    
	PgfBindType bind_type;
	PgfExpr body;
	PgfText name;
};

struct PGF_INTERNAL_DECL PgfExprApp {
    static const uint8_t tag = 1;

	PgfExpr fun;
	PgfExpr arg;
};

struct PGF_INTERNAL_DECL PgfExprLit {
    static const uint8_t tag = 2;

	PgfLiteral lit;
};

struct PGF_INTERNAL_DECL PgfExprMeta {
    static const uint8_t tag = 3;

	PgfMetaId id;
};

struct PGF_INTERNAL_DECL PgfExprFun {
    static const uint8_t tag = 4;

	PgfText name;
};

struct PGF_INTERNAL_DECL PgfExprVar {
    static const uint8_t tag = 5;
    
	int var;
};

struct PGF_INTERNAL_DECL PgfExprTyped {
    static const uint8_t tag = 6;

	PgfExpr expr;
	ref<PgfType> type;
};

struct PGF_INTERNAL_DECL PgfExprImplArg {
    static const uint8_t tag = 7;

	PgfExpr expr;
};

typedef float prob_t;

typedef struct {
	prob_t prob;
	PgfExpr expr;
} PgfExprProb;

PGF_INTERNAL_DECL
uintptr_t pgf_unmarshall_literal(PgfUnmarshaller *u, PgfLiteral l);

PGF_INTERNAL_DECL
uintptr_t pgf_unmarshall_expr(PgfUnmarshaller *u, PgfExpr e);

PGF_INTERNAL_DECL
uintptr_t pgf_unmarshall_type(PgfUnmarshaller *u, PgfType *tp);

typedef struct PgfBind {
    PgfBindType bind_type;
    struct PgfBind *next;
    PgfText var;
} PgfBind;

class PGF_INTERNAL_DECL PgfExprParser {
    enum PGF_TOKEN_TAG {
        PGF_TOKEN_LPAR,
        PGF_TOKEN_RPAR,
        PGF_TOKEN_LCURLY,
        PGF_TOKEN_RCURLY,
        PGF_TOKEN_QUESTION,
        PGF_TOKEN_LAMBDA,
        PGF_TOKEN_RARROW,
        PGF_TOKEN_LTRIANGLE,
        PGF_TOKEN_RTRIANGLE,
        PGF_TOKEN_COMMA,
        PGF_TOKEN_COLON,
        PGF_TOKEN_SEMI,
        PGF_TOKEN_WILD,
        PGF_TOKEN_IDENT,
        PGF_TOKEN_INT,
        PGF_TOKEN_FLT,
        PGF_TOKEN_STR,
        PGF_TOKEN_UNKNOWN,
        PGF_TOKEN_EOF,
    };

    PgfUnmarshaller *u;
	PGF_TOKEN_TAG token_tag;
	PgfText *token_value;
    PgfText *inp;
    const char *pos;
    uint32_t ch;

    uint32_t getc();
    void putc(uint32_t ch);

public:
    PgfExprParser(PgfText* input, PgfUnmarshaller *unmarshaller);
    ~PgfExprParser();

    void str_char();
    void token();
    bool lookahead(int ch);

    PgfBind *parse_bind(PgfBind *next);
    PgfBind *parse_binds(PgfBind *next);


    uintptr_t parse_arg();
    uintptr_t parse_term();
    uintptr_t parse_expr();

    bool parse_hypos(size_t *n_hypos, PgfTypeHypo **hypos);
    uintptr_t parse_type();

    bool eof();
};

PGF_INTERNAL_DECL extern PgfText wildcard;

#endif /* EXPR_H_ */
