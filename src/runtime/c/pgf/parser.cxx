#include "data.h"
#include "printer.h"
#include "parser.h"
#include <algorithm>

//#define DEBUG_STATE_CREATION
//#define DEBUG_AUTOMATON
//#define DEBUG_PARSER
//#define DEBUG_GENERATOR

struct PgfLRTableMaker::CCat {
    CCat *parent;
    size_t lin_idx;
    ref<PgfConcrLincat> lincat;

    size_t id;
    bool productive;                 // true if it has non epsilon rules
    std::vector<Item*> items;        // productive items
	std::vector<Item*> suspended;    // items that can progress on epsilon
    std::vector<Production*> prods;  // epsilon productions

    ref<PgfLRReduceArg> persistant;

    CCat(size_t id, CCat *parent, size_t lin_idx) {
        this->parent     = parent;
        this->lin_idx    = lin_idx;
        this->lincat     = (parent != NULL) ? parent->lincat : 0;
        this->id         = id;
        this->productive = false;
        this->persistant = 0;
    }

    ref<PgfLRReduceArg> persist();

    void suspend_item(Item *item);
    void register_item(Item *item);

    ~CCat();
};

#define container(T,field,p) ((T*) (((char*) p) - offsetof(T, field)))

struct PgfLRTableMaker::Production {
    ref<PgfConcrLin> lin;
    size_t index;

    struct {
        // After the Production there is an array of arguments
        size_t count;
        CCat *&operator [](int i) {
            return ((CCat **) (container(Production,args,this)+1))[i];
        }
    } args;

    struct {
        // After the array of arguments there is an array of variables
        size_t count;
        size_t &operator [](int i) {
            Production *prod = container(Production,vals,this);
            return ((size_t *) (((CCat**) (prod+1)) + prod->args.count))[i];
        }
    } vals;

    void *operator new(size_t size, Item *item);

    Production() {
        // If there is no constructor, GCC will zero the object,
        // while it has already been initialized in the new operator.
    }

    void operator delete(void *p) {
        free(p);
    }
};

struct PgfLRTableMaker::Item {
    size_t ref_cnt;   // how many CCat:s contain the item?
    CCat* ccat;
    object lin_obj;
    ref<PgfSequence> seq;
    size_t seq_idx;
    size_t sym_idx;
    size_t stk_size;

    struct Arg {
        CCat *ccat;
        size_t stk_idx;
    };

    struct {
        // After the Item there is an array of arguments
        size_t count;
        Arg &operator [](int i) const {
            return ((Arg*) (container(Item,args,this)+1))[i];
        }
    } args;

    struct {
        // After the array of arguments there is an array of variables
        size_t count;
        size_t &operator [](int i) const {
            Item *item = container(Item,vals,this);
            return ((size_t *) (((Arg*) (item+1)) + item->args.count))[i];
        }
    } vals;

    void *operator new(size_t size, CCat* ccat, ref<PgfConcrLin> lin, size_t seq_idx);
    void *operator new(size_t size, ref<PgfConcrLincat> lincat, size_t index);
    void *operator new(size_t size, CCat* ccat, Production *prod, size_t lin_idx);
    void *operator new(size_t size, Item *item, CCat *ccat);
    void *operator new(size_t size, Item *item, size_t lin_idx);
    void *operator new(size_t size, Item *item);

    Item() {
        // If there is no constructor, GCC will zero the object,
        // while it has already been initialized in the new operator.
    }

    void operator delete(void *p) {
        if (((Item *) p)->ref_cnt == 0)
            free(p);
    }
};

struct PgfLRTableMaker::CompareItem : std::less<Item*> {
    bool operator() (const Item *item1, const Item *item2) const {
        if (item1->lin_obj < item2->lin_obj)
            return true;
        else if (item1->lin_obj > item2->lin_obj)
            return false;

        if (item1->seq_idx < item2->seq_idx)
            return true;
        else if (item1->seq_idx > item2->seq_idx)
            return false;

        if (item1->sym_idx < item2->sym_idx)
            return true;
        else if (item1->sym_idx > item2->sym_idx)
            return false;

        for (size_t i = 0; i < item1->args.count; i++) {
            if (item1->args[i].ccat < item2->args[i].ccat)
                return true;
            else if (item1->args[i].ccat > item2->args[i].ccat)
                return false;
            if (item1->args[i].stk_idx < item2->args[i].stk_idx)
                return true;
            else if (item1->args[i].stk_idx > item2->args[i].stk_idx)
                return false;
        }

        return false;
    }
};

const PgfLRTableMaker::CompareItem PgfLRTableMaker::compare_item;

ref<PgfLRReduceArg> PgfLRTableMaker::CCat::persist() {
    if (persistant != 0)
        return persistant;

    size_t n_prods = prods.size();
    persistant = PgfDB::malloc<PgfLRReduceArg>(n_prods*sizeof(PgfLRReduce));
    persistant->n_prods = n_prods;
    for (size_t i = 0; i < n_prods; i++) {
        Production *prod = prods[i];
        persistant->prods[i].lin   = prod->lin;
        persistant->prods[i].index = prod->index;
        auto children = vector<ref<PgfLRReduceArg>>::alloc(prod->args.count);
        for (size_t j = 0; j < prod->args.count; j++) {
            if (prod->args[j] == NULL) {
                children[j] = 0;
            } else {
                ref<PgfLRReduceArg> child_arg = prod->args[j]->persist();
                children[j] = child_arg;
            }
        }
        persistant->prods[i].args = children;
    }

    return persistant;
}

void PgfLRTableMaker::CCat::suspend_item(Item *item) {
    suspended.push_back(item);
    if (item != NULL)
        item->ref_cnt++;
}

void PgfLRTableMaker::CCat::register_item(Item *item) {
    items.push_back(item); item->ref_cnt++;
}

PgfLRTableMaker::CCat::~CCat() {
    for (Item *item : items) {
        item->ref_cnt--;
        delete item;
    }
    for (Item *item : suspended) {
        item->ref_cnt--;
        delete item;
    }
    for (Production *prod : prods) {
        delete prod;
    }
}

void *PgfLRTableMaker::Production::operator new(size_t size, Item *item) {
    ref<PgfConcrLin> lin = ref<PgfConcrLin>::untagged(item->lin_obj);

    size_t n_fields = lin->seqs.size() / lin->res.size();
    size_t ex_size = sizeof(CCat*)*item->args.count+sizeof(size_t)*item->vals.count;

    Production *prod = (Production *) malloc(size+ex_size);
    prod->lin = lin;
    prod->index = item->seq_idx / n_fields;
    prod->args.count = item->args.count;
    prod->vals.count = item->vals.count;

    for (size_t i = 0; i < item->args.count; i++) {
        prod->args[i] = item->args[i].ccat;
    }
    for (size_t i = 0; i < item->vals.count; i++) {
        prod->vals[i] = item->vals[i];
    }

    return prod;
}

void *PgfLRTableMaker::Item::operator new(size_t size, CCat* ccat, ref<PgfConcrLin> lin, size_t seq_idx) {
    size_t n_args = lin->absfun->type->hypos.size();
    size_t n_fields = lin->seqs.size() / lin->res.size();
    ref<PgfPResult> res = lin->res[seq_idx / n_fields];
    size_t n_vars = res->vars.size();
    size_t ex_size = sizeof(Arg)*n_args+sizeof(size_t)*n_vars;

    Item *item = (Item *) malloc(size+ex_size);
    item->ref_cnt = 0;
    item->ccat = ccat;
    item->lin_obj = lin.tagged();
    item->seq = lin->seqs[seq_idx];
    item->seq_idx = seq_idx;
    item->sym_idx = 0;
    item->stk_size = 0;
    item->args.count = n_args;
    item->vals.count = n_vars;
    memset(item+1, 0, ex_size);

    return item;
}

void *PgfLRTableMaker::Item::operator new(size_t size, ref<PgfConcrLincat> lincat, size_t index) {
    size_t n_args = 1;
    ref<PgfPResult> res = lincat->res[lincat->n_lindefs+index];
    size_t n_vars = res->vars.size();
    size_t ex_size = sizeof(Arg)*n_args+sizeof(size_t)*n_vars;

    size_t seq_idx =
        lincat->n_lindefs*lincat->fields.size() + index;

    Item *item = (Item *) malloc(size+ex_size);
    item->ref_cnt = 0;
    item->ccat = NULL;
    item->lin_obj = lincat.tagged();
    item->seq = lincat->seqs[seq_idx];
    item->seq_idx = seq_idx;
    item->sym_idx = 0;
    item->stk_size = 0;
    item->args.count = n_args;
    item->vals.count = n_vars;
    memset(item+1, 0, ex_size);

    return item;
}

void *PgfLRTableMaker::Item::operator new(size_t size, CCat* ccat, Production *prod, size_t lin_idx) {
    size_t n_fields = prod->lin->seqs.size() / prod->lin->res.size();
    ref<PgfPResult> res = prod->lin->res[prod->index];
    size_t ex_size = sizeof(Arg)*prod->args.count+sizeof(size_t)*prod->vals.count;

    Item *item = (Item *) malloc(size+ex_size);
    item->ref_cnt = 0;
    item->ccat = ccat;
    item->lin_obj = prod->lin.tagged();
    item->seq_idx = prod->index*n_fields+lin_idx;
    item->seq = prod->lin->seqs[item->seq_idx];
    item->sym_idx = 0;
    item->stk_size = 0;
    item->args.count = prod->args.count;
    item->vals.count = prod->vals.count;

    for (size_t i = 0; i < item->args.count; i++) {
        item->args[i].ccat    = prod->args[i];
        item->args[i].stk_idx = 0;
    }
    for (size_t i = 0; i < item->vals.count; i++) {
        item->vals[i] = prod->vals[i];
    }

    return item;
}

void *PgfLRTableMaker::Item::operator new(size_t size, Item *item, CCat *ccat) {
    size_t ex_size = sizeof(Arg)*item->args.count+sizeof(size_t)*item->vals.count;

    Item *new_item = (Item *) malloc(size+ex_size);
    new_item->ref_cnt = 0;
    new_item->ccat = item->ccat;
    new_item->lin_obj = item->lin_obj;
    new_item->seq = item->seq;
    new_item->seq_idx = item->seq_idx;
    new_item->sym_idx = item->sym_idx+1;
    new_item->stk_size = item->stk_size;
    new_item->args.count = item->args.count;
    new_item->vals.count = item->vals.count;
    memcpy(new_item+1,item+1,ex_size);

    ref<PgfSymbolCat> scat =
        ref<PgfSymbolCat>::untagged(item->seq->syms[item->sym_idx]);
    new_item->args[scat->d].ccat = ccat;

    return new_item;
}

void *PgfLRTableMaker::Item::operator new(size_t size, Item *item, size_t lin_idx) {
    size_t ex_size = sizeof(Arg)*item->args.count+sizeof(size_t)*item->vals.count;

    Item *new_item = (Item *) malloc(size+ex_size);
    new_item->ref_cnt = 0;
    new_item->ccat = item->ccat;
    new_item->lin_obj = item->lin_obj;
    new_item->seq = item->seq;
    new_item->seq_idx = item->seq_idx;
    new_item->sym_idx = item->sym_idx+1;
    new_item->stk_size = item->stk_size;
    new_item->args.count = item->args.count;
    new_item->vals.count = item->vals.count;
    memcpy(new_item+1,item+1,ex_size);

    ref<PgfSymbolCat> scat =
        ref<PgfSymbolCat>::untagged(item->seq->syms[item->sym_idx]);
    new_item->args[scat->d].stk_idx = ++new_item->stk_size;

    return new_item;
}

void *PgfLRTableMaker::Item::operator new(size_t size, Item *item) {
    size_t ex_size = sizeof(Arg)*item->args.count+sizeof(size_t)*item->vals.count;

    Item *new_item = (Item *) malloc(size+ex_size);
    memcpy(new_item,item,size+ex_size);
    new_item->ref_cnt = 0;

    return new_item;
}

bool PgfLRTableMaker::CompareKey3::operator() (const Key3& k1, const Key3& k2) const {
    size_t i = k1.second;
    size_t j = k2.second;
    for (;;) {
        if (i >= k1.first->syms.size() || ref<PgfSymbol>::get_tag(k1.first->syms[i]) != PgfSymbolKS::tag)
            return (j < k2.first->syms.size() && ref<PgfSymbol>::get_tag(k2.first->syms[j]) == PgfSymbolKS::tag);

        if (j >= k2.first->syms.size() || ref<PgfSymbol>::get_tag(k2.first->syms[j]) != PgfSymbolKS::tag)
            return false;

        auto symks1 = ref<PgfSymbolKS>::untagged(k1.first->syms[i]);
        auto symks2 = ref<PgfSymbolKS>::untagged(k2.first->syms[j]);

        int res[2] = {0,0};
        texticmp(&symks1->token, &symks2->token, res);
        if (res[0] < 0)
            return true;
        if (res[0] > 0)
            return false;

        i++; j++;
    }

    return false;
}

struct PgfLRTableMaker::State {
    size_t id;
    std::vector<Item*> items;       // The seed items for this state
    std::vector<Item*> completed;   // Completed items that will become reductions
    std::map<Key1,State*,CompareKey1> ccats1;
    std::map<Key2,State*,CompareKey2> ccats2;
    std::map<Key3,State*,CompareKey3> tokens;
    State *bind_state;

    State() {
        this->id = 0;
        this->bind_state = NULL;
    }

    ~State() {
        for (Item *item : items) {
            item->ref_cnt--;
            delete item;
        }

        for (Item *item : completed) {
            item->ref_cnt--;
            delete item;
        }
    }

    void push_item(Item *item) {
        items.push_back(item); item->ref_cnt++;
        push_heap(items.begin(), items.end(), compare_item);
    }

    Item *pop_item() {
        Item *item = items.back(); items.pop_back();
        item->ref_cnt--;
        return item;
    }
};

PgfLRTableMaker::PgfLRTableMaker(ref<PgfAbstr> abstr, ref<PgfConcr> concr)
{
    this->abstr = abstr;
    this->concr = concr;
    this->ccat_id = 0;
    this->state_id = 0;

    PgfText *startcat = (PgfText *)
        alloca(sizeof(PgfText)+9);
    startcat->size = 8;
    strcpy(startcat->text, "startcat");

    ref<PgfFlag> flag =
        namespace_lookup(abstr->aflags, startcat);

    ref<PgfConcrLincat> lincat = 0;
    if (flag != 0) {
        switch (ref<PgfLiteral>::get_tag(flag->value)) {
        case PgfLiteralStr::tag: {
            auto lstr = ref<PgfLiteralStr>::untagged(flag->value);

            State *state = new State();

            lincat =
                namespace_lookup(concr->lincats, &lstr->val);

            MD5Context ctxt;

            for (size_t i = 0; i < lincat->res.size()-lincat->n_lindefs; i++) {
                Item *item = new(lincat, i) Item;

                ctxt.update(item->lin_obj);
                ctxt.update(item->seq_idx);
                ctxt.update(item->sym_idx);
                ctxt.update(item->args[0].ccat);
                ctxt.update(item->args[0].stk_idx);
                for (size_t i = 0; i < item->vals.count; i++) {
                    ctxt.update(item->vals[i]);
                }

                state->push_item(item);
            }

            MD5Digest digest;
            ctxt.finalize(&digest);

            states[digest] = state;
            todo.push(state);
        }
        }
    }
}

PgfLRTableMaker::~PgfLRTableMaker()
{
    for (auto p : states) {
        delete p.second;
    }

    for (auto p : ccats1) {
        delete p.second;
    }

    for (auto p : ccats2) {
        delete p.second;
    }
}

#if defined(DEBUG_STATE_CREATION) || defined(DEBUG_AUTOMATON)
void PgfLRTableMaker::print_production(CCat *ccat, Production *prod)
{
    PgfPrinter printer(NULL, 0, NULL);

    ref<PgfPResult> res = *vector_elem(prod->lin->res, prod->index);
    if (res->vars != 0) {
        printer.lvar_ranges(res->vars, &prod->vals[0]);
        printer.puts(" ");
    }

    ref<PgfDTyp> type = prod->lin->absfun->type;
    printer.nprintf(37, "?%zu -> ", ccat->id);
    printer.puts(&prod->lin->name);
    printer.nprintf(37, "/%zu[", prod->index);
    PgfDBMarshaller m;
    size_t args_start = type->hypos->len * prod->index;
    for (size_t i = 0; i < type->hypos->len; i++) {
        if (i > 0)
            printer.puts(",");

        if (prod->args[i] == NULL) {
            ref<PgfPArg> arg = vector_elem(prod->lin->args, args_start + i);
            m.match_type(&printer, vector_elem(type->hypos, i)->type.as_object());
            printer.puts("(");
            printer.lparam(arg->param);
            printer.puts(")");
        } else {
            printer.nprintf(32, "?%zu", prod->args[i]->id);
        }
    }
    printer.puts("]\n");

    PgfText *text = printer.get_text();
    fputs(text->text, stderr);
    free(text);
}

void PgfLRTableMaker::print_item(Item *item)
{
    PgfPrinter printer(NULL, 0, NULL);

    switch (ref<PgfConcrLin>::get_tag(item->lin_obj)) {
    case PgfConcrLin::tag: {
        auto lin =
            ref<PgfConcrLin>::untagged(item->lin_obj);

        size_t index = item->seq_idx / lin->lincat->fields->len;
        size_t r     = item->seq_idx % lin->lincat->fields->len;
        ref<PgfPResult> res = *vector_elem(lin->res, index);
        if (res->vars != 0) {
            printer.lvar_ranges(res->vars, &item->vals[0]);
            printer.puts(" ");
        }

        if (item->ccat->parent == NULL) {
            printer.puts(&item->ccat->lincat->name);
            printer.puts("(");
            printer.lparam(ref<PgfLParam>::from_ptr(&res->param));
            printer.puts(") -> ");
        } else {
            printer.nprintf(32,"?%zu -> ",item->ccat->parent->id);
        }

        printer.puts(&lin->name);
        printer.nprintf(32, "/%zd[", index);
        PgfDBMarshaller m;
        ref<PgfDTyp> type = lin->absfun->type;
        size_t args_start = type->hypos->len * index;
        for (size_t i = 0; i < type->hypos->len; i++) {
            if (i > 0)
                printer.puts(",");

            if (item->args[i].ccat == NULL) {
                ref<PgfPArg> arg = vector_elem(lin->args, args_start + i);
                m.match_type(&printer, vector_elem(type->hypos, i)->type.as_object());
                printer.puts("(");
                printer.lparam(arg->param);
                printer.puts(")");
            } else {
                printer.nprintf(32, "?%zu", item->args[i].ccat->id);
            }
            if (item->args[i].stk_idx > 0)
                printer.nprintf(32, "$%zd", item->args[i].stk_idx);
        }
        printer.nprintf(32, "]; %zu : ", r);
        break;
    }
    case PgfConcrLincat::tag: {
        auto lincat =
            ref<PgfConcrLincat>::untagged(item->lin_obj);

        size_t index = item->seq_idx - lincat->n_lindefs*lincat->fields->len;
        ref<PgfPResult> res = *vector_elem(lincat->res, lincat->n_lindefs+index);
        if (res->vars != 0) {
            printer.lvar_ranges(res->vars, &item->vals[0]);
            printer.puts(" ");
        }

        printer.puts("linref ");
        printer.puts(&lincat->name);
        printer.nprintf(32, "/%zd[", index);
        if (item->args[0].ccat == NULL) {
            printer.puts(&lincat->name);
            printer.puts("(");
            printer.lparam(vector_elem(lincat->args, lincat->n_lindefs+index)->param);
            printer.puts(")");
        } else {
            printer.nprintf(32, "?%zu", item->args[0].ccat->id);
        }
        if (item->args[0].stk_idx > 0)
            printer.nprintf(32, "$%zd", item->args[0].stk_idx);
        printer.puts("]; 0 : ");
        break;
    }
    }

    if (item->sym_idx == 0)
        printer.puts(". ");

    for (size_t i = 0; i < item->seq->syms.len; i++) {
        PgfSymbol sym = item->seq->syms.data[i];
        printer.symbol(sym);

        if (i+1 == item->sym_idx)
            printer.puts(" . ");
    }
    printer.puts("\n");

    PgfText *text = printer.get_text();
    fputs(text->text, stderr);
    free(text);
}
#endif

void PgfLRTableMaker::process(State *state, Fold fold, Item *item)
{
#if defined(DEBUG_STATE_CREATION)
    if (fold == PROBE)
        fprintf(stderr, "PROBE  ");
    else if (fold == INIT)
        fprintf(stderr, "INIT   ");
    else if (fold == REPEAT)
        fprintf(stderr, "REPEAT ");
    print_item(item);
#endif

    if (item->sym_idx < item->seq->syms.size()) {
        PgfSymbol sym = item->seq->syms[item->sym_idx];
        symbol(state, fold, item, sym);
    } else {
        complete(state, fold, item);
    }
}

void PgfLRTableMaker::symbol(State *state, Fold fold, Item *item, PgfSymbol sym)
{
    switch (ref<PgfSymbol>::get_tag(sym)) {
    case PgfSymbolCat::tag: {
        auto symcat = ref<PgfSymbolCat>::untagged(sym);

        switch (ref<PgfConcrLin>::get_tag(item->lin_obj)) {
        case PgfConcrLin::tag: {
            auto lin =
                ref<PgfConcrLin>::untagged(item->lin_obj);
            ref<PgfPResult> res = lin->res[item->seq_idx / lin->lincat->fields.size()];
            auto arg = item->args[symcat->d];
            if (arg.ccat != NULL) {
                predict(state, fold, item, arg.ccat, res->vars, &symcat->r);
            } else {
                ref<PgfHypo> hypo = lin->absfun->type->hypos.elem(symcat->d);
                predict(state, fold, item, ref<PgfText>::from_ptr(&hypo->type->name), res->vars, &symcat->r);
            }
            break;
        }
        case PgfConcrLincat::tag: {
            auto lincat =
                ref<PgfConcrLincat>::untagged(item->lin_obj);
            ref<PgfPResult> res =
                lincat->res[lincat->n_lindefs + item->seq_idx - lincat->n_lindefs*lincat->fields.size()];
            auto arg = item->args[symcat->d];
            if (arg.ccat != NULL) {
                predict(state, fold, item, arg.ccat, res->vars, &symcat->r);
            } else {
                predict(state, fold, item, ref<PgfText>::from_ptr(&lincat->name), res->vars, &symcat->r);
            }
            break;
        }
        }
        break;
    }
    case PgfSymbolKS::tag: {
        auto symks = ref<PgfSymbolKS>::untagged(sym);

        size_t sym_idx_2 = item->sym_idx+1;
        while (sym_idx_2 < item->seq->syms.size()) {
            if (ref<PgfSymbol>::get_tag(item->seq->syms[sym_idx_2]) != PgfSymbolKS::tag)
                break;
            sym_idx_2++;
        }

        if (fold == PROBE) {
            item->ccat->productive = true;
            if (item->sym_idx > 0 || sym_idx_2 < item->seq->syms.size()) {
                item->ccat->register_item(item);
            }
        } else {
            auto &next_state = state->tokens[Key3(item->seq,item->sym_idx)];
            if (next_state == NULL) {
                next_state = new State;
            }
            item = new (item) Item;
            item->sym_idx = sym_idx_2;
            item->stk_size++;
            next_state->push_item(item);
        }
        break;
    }
    case PgfSymbolKP::tag: {
        if (fold == PROBE) {
            item->ccat->productive = true;
            item->ccat->register_item(item);
        } else {
            auto symkp = ref<PgfSymbolKP>::untagged(sym);
            Item *new_item1 = NULL;
            Item *new_item2 = NULL;
            for (size_t i = 0; i < symkp->alts.size(); i++) {
                ref<PgfSequence> form = symkp->alts[i].form;
                if (form->syms.size() == 0) {
                    if (!new_item1) {
                        new_item1 = new (item) Item;
                        new_item1->sym_idx++;
                    }
                    process(state, fold, new_item1);
                } else {
                    auto &next_state = state->tokens[Key3(form,0)];
                    if (next_state == NULL) {
                        next_state = new State;
                    }
                    if (!new_item2) {
                        new_item2 = new (item) Item;
                        new_item2->sym_idx++;
                        new_item2->stk_size++;
                    }
                    next_state->push_item(new_item2);
                }
            }

            ref<PgfSequence> form = symkp->default_form;
            if (form->syms.size() == 0) {
                if (!new_item1) {
                    new_item1 = new (item) Item;
                    new_item1->sym_idx++;
                }
                process(state, fold, new_item1);
            } else {
                auto &next_state = state->tokens[Key3(form,0)];
                if (next_state == NULL) {
                    next_state = new State;
                }
                if (!new_item2) {
                    new_item2 = new (item) Item;
                    new_item2->sym_idx++;
                    new_item2->stk_size++;
                }
                next_state->push_item(new_item2);
            }

            // If the items are not owned by anyone, we must delete them
            if (new_item1 != NULL)
                delete new_item1;
            if (new_item2 != NULL)
                delete new_item2;
        }
    }
    case PgfSymbolBIND::tag: {
        if (fold == PROBE) {
            item->ccat->productive = true;
            item->ccat->register_item(item);
        } else {
            if (state->bind_state == NULL) {
                state->bind_state = new State;
            }
            item = new (item) Item;
            item->sym_idx++;
            item->stk_size++;
            state->bind_state->push_item(item);
        }
        break;
    }
    case PgfSymbolSOFTBIND::tag:
    case PgfSymbolSOFTSPACE::tag: {
        if (fold == PROBE) {
            item->ccat->productive = true;
            item->ccat->register_item(item);
        } else {
            // SOFT_BIND && SOFT_SPACE also allow a space
            Item *new_item = new (item) Item();
            new_item->sym_idx++;
            process(state,fold,new_item);
            delete new_item;

            // Now we handle the case where there is no space.
            if (state->bind_state == NULL) {
                state->bind_state = new State;
            }
            new_item = new (item) Item();
            new_item->stk_size++;
            state->bind_state->push_item(new_item);
            delete new_item;
        }
        break;
    }
    case PgfSymbolCAPIT::tag:
    case PgfSymbolALLCAPIT::tag: {
        // We just ignore CAPIT && ALLCAPIT during parsing
        item = new (item) Item(); item->sym_idx++;
        process(state,fold,item);
        delete item;
        break;
    }
    }
}

struct PGF_INTERNAL_DECL PgfVariableValue {
    size_t range;
    size_t factor;
    size_t value;
    size_t j;
};

template<class T>
void PgfLRTableMaker::predict(State *state, Fold fold, Item *item, T cat,
                              vector<PgfVariableRange> vars, PgfLParam *r)
{
    size_t index   = r->i0;
    size_t n_terms = 0;

    PgfVariableValue *values = (PgfVariableValue *)
        alloca(sizeof(PgfVariableValue)*r->n_terms);
    for (size_t i = 0; i < r->n_terms; i++)
    {
        size_t var = r->terms[i].var;
        for (size_t j = 0; j < vars.size(); j++)
        {
            ref<PgfVariableRange> range = vars.elem(j);
            if (range->var == var) {
                if (item->vals[j] == 0) {
                    values[n_terms].range  = range->range;
                    values[n_terms].factor = r->terms[i].factor;
                    values[n_terms].value  = 0;
                    values[n_terms].j      = j;
                    n_terms++;
                } else {
                    index += (item->vals[j]-1) * r->terms[i].factor;
                }
                break;
            }
        }
    }

    for (;;) {
        Item *new_item = new (item) Item();
        for (size_t i = 0; i < n_terms; i++) {
            new_item->vals[values[i].j] = values[i].value+1;
        }

        predict(state, fold, new_item, cat, index);

        delete new_item;

        size_t i = n_terms;
        while (i > 0) {
            i--;
            values[i].value++;
            if (values[i].value < values[i].range) {
                index += values[i].factor;
                i++;
                break;
            }

            index -= (values[i].value-1) * values[i].factor;
            values[i].value = 0;
        }

        if (i == 0) {
            break;
        }
    }
}

void PgfLRTableMaker::predict(State *state, Fold fold, Item *item, ref<PgfText> cat, size_t lin_idx)
{
    CCat *&ccat = ccats1[Key0(cat,lin_idx)];
    CCat *tmp = ccat;
    if (tmp == NULL) {
        ccat = new CCat(++ccat_id, NULL, lin_idx);
    }
    if (fold == PROBE) {
        ccat->suspend_item(item);
    }
    if (tmp == NULL) {
        std::function<bool(ref<PgfAbsFun>)> f =
            [this,ccat](ref<PgfAbsFun> fun) {
                predict(fun, ccat);
                return true;
            };
        probspace_iter(abstr->funs_by_cat, cat, f, false);
    } else if (fold == PROBE && ccat->prods.size() > 0) {
        Item *new_item = new(item,ccat) Item;
        process(state,fold,new_item);
        delete new_item;
    }

    if (fold == PROBE) {
        if (item->ccat != NULL && ccat->productive) {
            item->ccat->productive = true;
            item->ccat->register_item(item);
        }
    } else {
        if (ccat->productive) {
            auto &next_state = state->ccats1[Key1(ccat->lincat,lin_idx)];
            if (next_state == NULL) {
                next_state = new State;
            }
            next_state->push_item(new(item,lin_idx) Item);

            if (next_state->items.size() == 1) {
                for (size_t i = 0; i < ccat->items.size(); i++) {
                    process(state, REPEAT, ccat->items[i]);
                }
            }
        }

        if (fold == INIT && ccat->prods.size() > 0) {
            Item *new_item = new (item, ccat) Item;
            process(state, fold, new_item);
            delete new_item;
        }
    }
}

void PgfLRTableMaker::predict(State *state, Fold fold, Item *item, CCat *ccat, size_t lin_idx)
{
    CCat *&new_ccat = ccats2[Key2(ccat,lin_idx)];
    CCat *tmp = new_ccat;
    if (new_ccat == NULL) {
        new_ccat = new CCat(++ccat_id, ccat, lin_idx);
    }
    if (fold == PROBE) {
        new_ccat->suspend_item(item);
    }
    if (tmp == NULL) {
        size_t n_prods = ccat->prods.size();
        for (size_t i = 0; i < n_prods; i++) {
            Production *prod = ccat->prods[i];
            Item *item = new(new_ccat, prod, lin_idx) Item;
            process(NULL, PROBE, item);
            delete item;
        }
    }

    if (fold == PROBE) {
        if (item->ccat != NULL && new_ccat->productive) {
            item->ccat->productive = true;
            item->ccat->register_item(item);
        }
    } else {
        if (new_ccat->productive) {
            auto &next_state = state->ccats2[Key2(new_ccat,lin_idx)];
            if (next_state == NULL) {
                next_state = new State;
            }
            next_state->push_item(new(item,lin_idx) Item);

            if (next_state->items.size() == 1) {
                for (size_t i = 0; i < new_ccat->items.size(); i++) {
                    process(state, REPEAT, new_ccat->items[i]);
                }
            }
        }
        if (fold == INIT && new_ccat->prods.size() > 0) {
            Item *new_item = new (item, new_ccat) Item;
            process(state, fold, new_item);
            delete new_item;
        }
    }
}

void PgfLRTableMaker::predict(ref<PgfAbsFun> absfun, CCat *ccat)
{
    ref<PgfConcrLin> lin =
        namespace_lookup(concr->lins, &absfun->name);

    if (lin != 0) {
        ccat->lincat = lin->lincat;

        size_t n_fields = lin->seqs.size() / lin->res.size();
        for (size_t i = 0; i < lin->res.size(); i++) {
            size_t seq_idx = n_fields * i + ccat->lin_idx;
            Item *item = new(ccat, lin, seq_idx) Item;
            process(NULL, PROBE, item);
            delete item;
        }
    }
}

void PgfLRTableMaker::complete(State *state, Fold fold, Item *item)
{
    if (fold == PROBE) {
        Production *prod = new(item) Production;
        item->ccat->prods.push_back(prod);

#if defined(DEBUG_STATE_CREATION) || defined(DEBUG_AUTOMATON)
        print_production(item->ccat, prod);
#endif

        if (item->ccat->prods.size() == 1) {
            // If this is the first epsilon production,
            // resume the suspended items.

            // We don't use an iterator here since the vector suspended,
            // may get updated in the recursion.
            size_t n_susp = item->ccat->suspended.size();
            for (size_t i = 0; i < n_susp; i++) {
                Item *susp = item->ccat->suspended[i];
                if (susp != NULL) {
                    Item *new_item = new (susp, item->ccat) Item;
                    process(state, PROBE, new_item);
                    delete new_item;
                }
            }
        }
    } else {
        state->completed.push_back(item); item->ref_cnt++;

#if defined(DEBUG_AUTOMATON)
        fprintf(stderr, "reduce ");
        print_item(item);
#endif
    }
}

void PgfLRTableMaker::internalize_state(State *&state)
{
    MD5Context ctxt;
    auto begin = state->items.begin();
    auto end   = state->items.end();
    while (begin != end) {
        Item *item = *(--end);
        ctxt.update(item->lin_obj);
        ctxt.update(item->seq_idx);
        ctxt.update(item->sym_idx);
        for (size_t i = 0; i < item->args.count; i++) {
            ctxt.update(item->args[i].ccat);
            ctxt.update(item->args[i].stk_idx);
        }
        for (size_t i = 0; i < item->vals.count; i++) {
            ctxt.update(item->vals[i]);
        }

        pop_heap(begin,end,compare_item);
    }

    MD5Digest digest;
    ctxt.finalize(&digest);

    State *&next_state = states[digest];
    if (next_state == NULL) {
        next_state = state;
        next_state->id = ++state_id;
        todo.push(next_state);
    } else {
        delete state;
        state = next_state;
    }
}

vector<PgfLRState> PgfLRTableMaker::make()
{
    while (!todo.empty()) {
        State *state = todo.front(); todo.pop();

#if defined(DEBUG_AUTOMATON) || defined(DEBUG_STATE_CREATION)
        fprintf(stderr, "--------------- state %ld ---------------\n", state->id);
#endif

        while (!state->items.empty()) {
            Item *item = state->pop_item();

#if defined(DEBUG_AUTOMATON) && !defined(DEBUG_STATE_CREATION)
            // The order in which we process the items should not matter,
            // For debugging however it is useful to see them in the same order.
            pop_heap(state->items.begin(),state->items.end(),compare_item);
            print_item(item);
#endif

            process(state, INIT, item);

            delete item;
        }

        for (auto &i : state->ccats1) {
            internalize_state(i.second);
#if defined(DEBUG_AUTOMATON)
            fprintf(stderr, "%s.%zu: state %ld\n",
                    i.first.first->name.text, i.first.second, i.second->id);
#endif
        }
        for (auto &i : state->ccats2) {
            internalize_state(i.second);
#if defined(DEBUG_AUTOMATON)
            fprintf(stderr, "%s.%zu: state %ld\n",
                    i.first.first->lincat->name.text, i.first.second, i.second->id);
#endif
        }
        for (auto &i : state->tokens) {
            internalize_state(i.second);
#if defined(DEBUG_AUTOMATON)
            PgfPrinter printer(NULL, 0, NULL);
            size_t sym_idx = i.first.second;
            ref<PgfSequence> seq = i.first.first;
            while (sym_idx < seq->syms.len) {
                PgfSymbol sym = seq->syms.data[sym_idx];
                if (ref<PgfSymbol>::get_tag(sym) != PgfSymbolKS::tag)
                    break;
                printer.symbol(sym);
                sym_idx++;
            }
            printer.nprintf(64, ": state %ld\n", i.second->id);

            PgfText *text = printer.get_text();
            fputs(text->text, stderr);
            free(text);
#endif
        }
        if (state->bind_state != NULL) {
            internalize_state(state->bind_state);
#if defined(DEBUG_AUTOMATON)
            fprintf(stderr, "BIND: state %ld\n", state->bind_state->id);
#endif
        }

    }

    vector<PgfLRState> lrtable = vector<PgfLRState>::alloc(states.size());
    for (auto v : states) {
        State *state = v.second;

        size_t index = 0;
        auto shifts = vector<PgfLRShift>::alloc(state->ccats1.size()+state->ccats2.size());
        for (auto i : state->ccats1) {
            ref<PgfLRShift> shift = shifts.elem(index++);
            shift->lincat = i.first.first;
            shift->r = i.first.second;
            shift->next_state = i.second->id;
        }
        for (auto i : state->ccats2) {
            ref<PgfLRShift> shift = shifts.elem(index++);
            shift->lincat = i.first.first->lincat;
            shift->r = i.first.second;
            shift->next_state = i.second->id;
        }

        vector<PgfLRShiftKS> tokens = 0;
        if (state->tokens.size() > 0) {
            size_t index = 0;
            tokens = vector<PgfLRShiftKS>::alloc(state->tokens.size());
            for (auto i : state->tokens) {
                ref<PgfLRShiftKS> shift = tokens.elem(index++);
                shift->seq = i.first.first;
                shift->sym_idx = i.first.second;
                shift->next_state = i.second->id;
            }
        }

        size_t next_bind_state = 0;
        if (state->bind_state != NULL) {
            next_bind_state = state->bind_state->id;
        }

        auto reductions = vector<PgfLRReduce>::alloc(state->completed.size());
        for (size_t i = 0; i < state->completed.size(); i++) {
            Item *item = state->completed[i];
            ref<PgfLRReduce> reduction = reductions.elem(i);
            reduction->lin_obj = item->lin_obj;
            reduction->seq_idx = item->seq_idx;
            reduction->depth = item->stk_size;

            auto args = vector<PgfLRReduce::Arg>::alloc(item->args.count);
            for (size_t j = 0; j < item->args.count; j++) {
                ref<PgfLRReduceArg> arg = 0;
                if (item->args[j].ccat != NULL) {
                    arg = item->args[j].ccat->persist();
                }
                args[j].arg     = arg;
                args[j].stk_idx = item->args[j].stk_idx;
            }
            reduction->args = args;
        }

        ref<PgfLRState> lrstate = lrtable.elem(state->id);
        lrstate->shifts = shifts;
        lrstate->tokens = tokens;
        lrstate->next_bind_state = next_bind_state;
        lrstate->reductions = reductions;
    }
    return lrtable;
}

PgfLCTableMaker::PgfLCTableMaker(ref<PgfAbstr> abstr, ref<PgfConcr> concr)
{
    this->abstr = abstr;
    this->concr = concr;
}

PgfLCTableMaker::~PgfLCTableMaker()
{
}

static bool edge_match(ref<PgfLCEdge> edge1, ref<PgfLCEdge> edge2)
{
    size_t sz1 = sizeof(PgfLCEdge) + sizeof(term)*edge1->n_terms + sizeof(PgfVariableRange)*edge1->vars.size();
    size_t sz2 = sizeof(PgfLCEdge) + sizeof(term)*edge2->n_terms + sizeof(PgfVariableRange)*edge2->vars.size();
    
    if (sz1 != sz2)
        return false;
    return (memcmp(&*edge1,&*edge2,sz1) == 0);
}

int comp (const void * elem1, const void * elem2) 
{
    int f = *((int*)elem1);
    int s = *((int*)elem2);
    if (f > s) return  1;
    if (f < s) return -1;
    return 0;
}

void PgfLCTableMaker::rename(ref<PgfLCEdge> edge)
{
    size_t next_var = 0;
    std::map<size_t,size_t> subst;
    for (size_t i = 0; i < edge->n_terms; i++) {
        auto it = subst.find(edge->terms[i].var);
        if (it == subst.end()) {
            subst[edge->terms[i].var] = next_var;
            edge->terms[i].var = next_var++;
        } else {
            edge->terms[i].var = it->second;
        }
    }

    for (size_t i = 0; i < edge->vars.size(); i++) {
        edge->vars[i].var = subst[edge->vars[i].var];
    }
    qsort (&edge->vars[0], edge->vars.size(), sizeof(PgfVariableRange), comp);
}

void PgfLCTableMaker::add_edge(ref<PgfLCEdge> edge)
{
    bool found = false;
    for (ref<PgfLCEdge> xedge : forwards[edge->from.lincat]) {
        if (edge_match(edge,xedge)) {
            found = true;
            break;
        }
    }

    if (!found) {
        print_edge(edge);
        forwards[edge->from.lincat].push_back(edge);
        backwards[edge->to.lincat].push_back(edge);
        update_closure(edge);
    }
}

void PgfLCTableMaker::update_closure(ref<PgfLCEdge> edge)
{
    auto &incoming = backwards[edge->from.lincat];
    size_t n_incoming = incoming.size();
    for (size_t i = 0; i < n_incoming; i++) {
        ref<PgfLCEdge> xedge = compute_unifier(incoming[i],edge);
        if (xedge != 0) {
            rename(xedge);
            add_edge(xedge);
        }
    }

    auto &outgoing = forwards[edge->to.lincat];
    size_t n_outgoing = outgoing.size();
    for (size_t i = 0; i < n_outgoing; i++) {
        ref<PgfLCEdge> xedge = compute_unifier(edge,outgoing[i]);
        if (xedge != 0) {
            rename(xedge);
            add_edge(xedge);
        }
    }
}

typedef std::pair<size_t,std::vector<term>> Param;
typedef std::map<size_t,Param> Subst;

template<class V, class T, class F>
bool unifier_helper1(Subst &subst1, V &vars1, T &to,
                     Subst &subst2, V &vars2, F &from)
{
    size_t i01t = to.i0;
    size_t i02f = from.i0;

    size_t i = 0, j = 0;
    while (i < to.size() && j < from.size()) {
        size_t factor1 = to[i].factor;
        size_t range1  = 0;
        for (size_t k = 0; k < vars1.size(); k++) {
            if (vars1[k].var == to[i].var) {
                range1 = vars1[k].range;
                break;
            }
        }
        size_t value1 = factor1*range1;

        size_t factor2 = from[j].factor;
        size_t range2  = 0;
        for (size_t k = 0; k < vars2.size(); k++) {
            if (vars2[k].var == from[j].var) {
                range2 = vars2[k].range;
                break;
            }
        }
        size_t value2 = factor2*range2;

        if (value1 > value2) {
            size_t x = i02f / factor1;
            if (x >= range1)
                return false;
            auto &s = subst1[to[i].var];
            s.first = i02f / factor1;
            s.second.clear();
            i02f %= factor1;
            while (j < from.size() && factor2 % factor1 == 0) {
                size_t factor = factor2 / factor1;
                s.second.emplace_back();
                s.second.back().factor=factor;
                s.second.back().var=subst2[from[j].var].second[0].var;
                j++;
                factor2 = from[j].factor;
            }
            i++;
        } else {
            size_t x = i01t / factor2;
            if (x >= range2)
                return false;
            auto &s = subst2[from[j].var];
            s.first = i01t / factor2;
            s.second.clear();
            i01t %= factor2;
            while (i < to.size() && factor1 % factor2 == 0) {
                size_t factor = factor1 / factor2;
                s.second.emplace_back();
                s.second.back().factor=factor;
                s.second.back().var=subst1[to[i].var].second[0].var;
                i++;
                factor1 = to[i].factor;
            }
            j++;
        }
    }

    while (i < to.size()) {
        auto &s = subst1[to[i].var];
        size_t factor1 = to[i].factor;
        s.first = i02f / factor1;
        s.second.clear();
        i02f %= factor1;
        i++;
    }

    while (j < from.size()) {
        auto &s = subst2[from[j].var];
        size_t factor2 = from[j].factor;
        s.first = i01t / factor2;
        s.second.clear();
        i01t %= factor2;
        j++;
    }

    return (i01t == i02f);
}

template<class A>
void unifier_helper2(Subst &subst, std::map<size_t,size_t> &vars, std::map<size_t,size_t> &ranges, A &v, Param &p)
{
    for (size_t i = 0; i < v.size(); i++) {
        auto &s = subst[v[i].var];
        size_t factor = v[i].factor;
        p.first += factor * s.first;
        for (term &t : s.second) {
            p.second.emplace_back();
            p.second.back().factor = factor * t.factor;
            p.second.back().var = t.var;
            vars[t.var] = ranges[t.var];
        }
    }
}

ref<PgfLCEdge> PgfLCTableMaker::compute_unifier(ref<PgfLCEdge> edge1, ref<PgfLCEdge> edge2)
{
    std::map<size_t,std::pair<size_t,std::vector<term>>> subst1, subst2;
    std::map<size_t,size_t> vars, ranges;

    size_t next_var = 0;
    for (size_t i = 0; i < edge1->vars.size(); i++) {
        ranges[next_var] = edge1->vars[i].range;

        auto &s = subst1[edge1->vars[i].var];
        s.second.emplace_back();
        s.second.back().factor = 1;
        s.second.back().var = next_var++;        
    }
    for (size_t i = 0; i < edge2->vars.size(); i++) {
        ranges[next_var] = edge2->vars[i].range;

        auto &s = subst2[edge2->vars[i].var];
        s.second.emplace_back();
        s.second.back().factor = 1;
        s.second.back().var = next_var++;
    }

    if (!unifier_helper1(subst1, edge1->vars, edge1->to.value,
                         subst2, edge2->vars, edge2->from.value))
        return 0;
    if (!unifier_helper1(subst1, edge1->vars, edge1->to.lin_idx,
                         subst2, edge2->vars, edge2->from.lin_idx))
        return 0;

    Param p1fv,p1fi,p2tv,p2ti;
    p1fv.first = edge1->from.value.i0;
    p1fi.first = edge1->from.lin_idx.i0;
    p2tv.first = edge2->to.value.i0;
    p2ti.first = edge2->to.lin_idx.i0;

    unifier_helper2(subst1, vars, ranges, edge1->from.value, p1fv);
    unifier_helper2(subst1, vars, ranges, edge1->from.lin_idx, p1fi);
    unifier_helper2(subst2, vars, ranges, edge2->to.value, p2tv);
    unifier_helper2(subst2, vars, ranges, edge2->to.lin_idx, p2ti);

    ref<PgfLCEdge> edge = PgfLCEdge::alloc(p1fv.second.size(),p1fi.second.size(),p2tv.second.size(),p2ti.second.size(),vars.size());
    edge->from.lincat = edge1->from.lincat;
    edge->from.value.i0 = p1fv.first;
    for (size_t i = 0; i < p1fv.second.size(); i++) {
        edge->from.value[i] = p1fv.second[i];
    }
    edge->from.lin_idx.i0 = p1fi.first;
    for (size_t i = 0; i < p1fi.second.size(); i++) {
        edge->from.lin_idx[i] = p1fi.second[i];
    }
    edge->to.lincat = edge2->to.lincat;
    edge->to.value.i0 = p2tv.first;
    for (size_t i = 0; i < p2tv.second.size(); i++) {
        edge->to.value[i] = p2tv.second[i];
    }
    edge->to.lin_idx.i0 = p2ti.first;
    for (size_t i = 0; i < p2ti.second.size(); i++) {
        edge->to.lin_idx[i] = p2ti.second[i];
    }
    size_t i = 0;
    for (auto it : vars) {
        edge->vars[i].var   = it.first;
        edge->vars[i].range = it.second;
        i++;
    }
/*
    if (strcmp(edge->to.lincat->name.text, "VP") == 0 && edge->to.value.i0 == 2 && edge->to.value.size() == 2) {
        print_edge(edge1);
        print_edge(edge2);
        fprintf(stderr,"------------------\n");
        print_edge(edge);
        fprintf(stderr,"\n");
    }
*/
    return edge;
}

void PgfLCTableMaker::print_edge(ref<PgfLCEdge> edge)
{
    PgfPrinter printer(NULL, 0, NULL);

    if (edge->vars.size() > 0) {
        printer.puts("{");
        for (size_t i = 0; i < edge->vars.size(); i++) {
            if (i > 0)
                printer.puts(",");
            printer.lvar(edge->vars[i].var);
            printer.nprintf(32,"<%zu",edge->vars[i].range);
        }
        printer.puts("} ");
    }

    printer.efun(&edge->from.lincat->name);
    printer.puts("(");
    if (edge->from.value.i0 != 0 || edge->from.value.size() == 0)
        printer.nprintf(32,"%ld",edge->from.value.i0);
    for (size_t i = 0; i < edge->from.value.size(); i++) {
        if (edge->from.value.i0 != 0 || i > 0)
            printer.puts("+");
        if (edge->from.value[i].factor != 1) {
            printer.nprintf(32,"%ld",edge->from.value[i].factor);
            printer.puts("*");
        }
        printer.lvar(edge->from.value[i].var);
    }
    printer.puts(",");
    if (edge->from.lin_idx.i0 != 0 || edge->from.lin_idx.size() == 0)
        printer.nprintf(32,"%ld",edge->from.lin_idx.i0);
    for (size_t i = 0; i < edge->from.lin_idx.size(); i++) {
        if (edge->from.lin_idx.i0 != 0 || i > 0)
            printer.puts("+");
        if (edge->from.lin_idx[i].factor != 1) {
            printer.nprintf(32,"%ld",edge->from.lin_idx[i].factor);
            printer.puts("*");
        }
        printer.lvar(edge->from.lin_idx[i].var);
    }
    printer.puts(") -> ");

    printer.efun(&edge->to.lincat->name);
    printer.puts("(");
    if (edge->to.value.i0 != 0 || edge->to.value.size() == 0)
        printer.nprintf(32,"%ld",edge->to.value.i0);
    for (size_t i = 0; i < edge->to.value.size(); i++) {
        if (edge->to.value.i0 != 0 || i > 0)
            printer.puts("+");
        if (edge->to.value[i].factor != 1) {
            printer.nprintf(32,"%ld",edge->to.value[i].factor);
            printer.puts("*");
        }
        printer.lvar(edge->to.value[i].var);
    }
    printer.puts(",");
    if (edge->to.lin_idx.i0 != 0 || edge->to.lin_idx.size() == 0)
        printer.nprintf(32,"%ld",edge->to.lin_idx.i0);
    for (size_t i = 0; i < edge->to.lin_idx.size(); i++) {
        if (edge->to.lin_idx.i0 != 0 || i > 0)
            printer.puts("+");
        if (edge->to.lin_idx[i].factor != 1) {
            printer.nprintf(32,"%ld",edge->to.lin_idx[i].factor);
            printer.puts("*");
        }
        printer.lvar(edge->to.lin_idx[i].var);
    }
    printer.puts(")\n");

    PgfText *text = printer.get_text();
    fputs(text->text, stderr);
    free(text);
}

vector<PgfLRState> PgfLCTableMaker::make()
{
    std::function<bool(ref<PgfConcrLin>)> f =
        [this](ref<PgfConcrLin> lin) {
            for (size_t seq_idx = 0; seq_idx < lin->seqs.size(); seq_idx++) {
                size_t index = seq_idx / (lin->seqs.size() / lin->res.size());
                size_t n_args = (lin->args.size() / lin->res.size());
                ref<PgfPResult> res = lin->res[index];
                ref<PgfSequence> seq = lin->seqs[seq_idx];

                if (seq->syms.size() > 0) {
                    PgfSymbol sym = seq->syms[0];
                    switch (ref<PgfSymbol>::get_tag(sym)) {
                    case PgfSymbolCat::tag: {
                        auto sym_cat = ref<PgfSymbolCat>::untagged(sym);
                        size_t arg_idx = n_args * index + sym_cat->d;
                        ref<PgfPArg> arg = ref<PgfPArg>::from_ptr(&lin->args[arg_idx]);

                        std::set<size_t> vars;
                        for (size_t i = 0; i < res->param.n_terms; i++) {
                            vars.insert(res->param.terms[i].var);
                        }
                        for (size_t i = 0; i < arg->param->n_terms; i++) {
                            vars.insert(arg->param->terms[i].var);
                        }
                        for (size_t i = 0; i < sym_cat->r.n_terms; i++) {
                            vars.insert(sym_cat->r.terms[i].var);
                        }

                        ref<PgfLCEdge> edge =
                            PgfLCEdge::alloc(res->param.n_terms,0,arg->param->n_terms,sym_cat->r.n_terms,vars.size());
                        edge->from.lincat = lin->lincat;
                        edge->from.value.i0 = res->param.i0;
                        for (size_t i = 0; i < res->param.n_terms; i++) {
                            edge->from.value[i] = res->param.terms[i];
                        }
                        edge->from.lin_idx.i0 = seq_idx % (lin->seqs.size() / lin->res.size());
                        edge->to.lincat =
                            namespace_lookup(concr->lincats, &lin->absfun->type->hypos[sym_cat->d].type->name);
                        edge->to.value.i0 = arg->param->i0;
                        for (size_t i = 0; i < arg->param->n_terms; i++) {
                            edge->to.value[i] = arg->param->terms[i];
                        }
                        edge->to.lin_idx.i0 = sym_cat->r.i0;
                        for (size_t i = 0; i < sym_cat->r.n_terms; i++) {
                            edge->to.lin_idx[i] = sym_cat->r.terms[i];
                        }
                        size_t i = 0;
                        for (size_t var : vars) {
                            edge->vars[i].var = var;
                            for (size_t k = 0; k < res->vars.size(); k++) {
                                if (res->vars[k].var == var) {
                                    edge->vars[i].range = res->vars[k].range;
                                    break;
                                }
                            }
                            i++;
                        }

                        rename(edge);
                        add_edge(edge);
                    }
                    break;
                    }
                }
            }
            return true;
        };
    namespace_iter(concr->lins, f);

/*    for (auto it : forwards) {
        for (ref<PgfLCEdge> edge : it.second) {
            print_edge(edge);
        }
    }
*/
    return 0;
}

struct PgfParser::Choice {
    int fid;
    std::vector<Production*> prods;
    std::vector<ExprState*> states;
    std::vector<ExprInstance> exprs;

    Choice(int fid) {
        this->fid = fid;
    }

    ~Choice();
};

struct PgfParser::Production {
    ref<PgfConcrLin> lin;
    size_t index;
    size_t n_args;
    Choice *args[];

    void *operator new(size_t size, ref<PgfConcrLin> lin, size_t index) {
        size_t n_args = lin->args.size() / lin->res.size();
        Production *prod = (Production *)
            malloc(size+sizeof(Choice*)*n_args);
        prod->lin    = lin;
        prod->index  = index;
        prod->n_args = n_args;
        for (size_t i = 0; i < n_args; i++) {
            prod->args[i] = NULL;
        }
        return prod;
    }

    Production() {
        // If there is no constructor, GCC will zero the object,
        // while it has already been initialized in the new operator.
    }

    bool operator==(const Production& other) const {
        if (lin != other.lin || index != other.index)
            return false;

        for (size_t i = 0; i < n_args; i++) {
            if (args[i] != other.args[i])
                return false;
        }

        return true;
    }

    void operator delete(void *p) {
        free(p);
    }
};

struct PgfParser::StackNode {
    Stage *stage;
    size_t state_id;
    Choice *choice;
    std::vector<StackNode*> parents;

    StackNode(Stage *stage, size_t state_id) {
        this->stage    = stage;
        this->state_id = state_id;
        this->choice   = NULL;
    }
};

struct PgfParser::Stage {
    Stage *next;
    PgfTextSpot start;
    PgfTextSpot end;
    std::vector<StackNode*> nodes;

    Stage(PgfTextSpot spot) {
        next = NULL;
        start = spot;
        end   = spot;
    }

    ~Stage() {
        for (StackNode *node : nodes) {
            delete node;
        }
    }
};

struct PgfParser::ExprState {
    prob_t prob;

    Choice *choice;
    Production *prod;
    size_t n_args;
    PgfExpr expr;
};

struct PgfParser::ExprInstance {
    PgfExpr expr;
    prob_t prob;

    ExprInstance(PgfExpr expr, prob_t prob) {
        this->expr = expr;
        this->prob = prob;
    }
};

PgfParser::Choice::~Choice() {
    while (states.size() > 0) {
        ExprState *state = states.back(); states.pop_back();
        delete state;
    }

    for (Production *prod : prods) {
        delete prod;
    }
}

#if defined(DEBUG_STATE_CREATION) || defined(DEBUG_AUTOMATON) || defined(DEBUG_PARSER)
void PgfParser::print_prod(Choice *choice, Production *prod)
{
    PgfPrinter printer(NULL, 0, m);

    printer.nprintf(32, "?%d -> ", choice->fid);

    ref<PgfDTyp> type = prod->lin->absfun->type;
    printer.puts(&prod->lin->name);
    printer.nprintf(32,"/%zd[", prod->index);
    PgfDBMarshaller m;
    for (size_t i = 0; i < prod->n_args; i++) {
        Choice *choice = prod->args[i];
        if (i > 0)
            printer.puts(",");
        if (choice == NULL) {
            m.match_type(&printer, vector_elem(type->hypos, i)->type.as_object());
        } else {
            printer.nprintf(32, "?%d", choice->fid);
        }
    }
    printer.puts("]\n");

    PgfText *text = printer.get_text();
    fputs(text->text, stderr);
    free(text);
}

void PgfParser::print_transition(StackNode *source, StackNode *target, Stage *stage, ref<PgfLRShiftKS> shift)
{
    PgfPrinter printer(NULL, 0, m);
    printer.nprintf(64, "state %ld --- ", source->state_id);
    if (target->choice != 0) {
        printer.nprintf(32, "?%d", target->choice->fid);
    } else if (shift != 0) {
        size_t sym_idx = shift->sym_idx;
        ref<PgfSequence> seq = shift->seq;
        while (sym_idx < seq->syms.len) {
            PgfSymbol sym = seq->syms.data[sym_idx];
            if (ref<PgfSymbol>::get_tag(sym) != PgfSymbolKS::tag)
                break;
            printer.symbol(sym);
            sym_idx++;
        }
    } else {
        printer.puts("BIND");
    }
    printer.nprintf(80, " ---> state %ld (position %zu-%zu, nodes %zu)\n",
                    target->state_id,
                    stage->start.pos, stage->end.pos, stage->nodes.size());
    PgfText *text = printer.get_text();
    fputs(text->text, stderr);
    free(text);
}
#endif

PgfParser::PgfParser(ref<PgfConcr> concr, ref<PgfConcrLincat> start, PgfText *sentence, bool case_sensitive, PgfMarshaller *m, PgfUnmarshaller *u)
{
    this->concr = concr;
    this->sentence = sentence;
    this->case_sensitive = case_sensitive;
    this->m = m;
    this->u = u;
    this->last_fid = 0;
    this->top_choice = NULL;
    this->top_choice_index = 0;

    PgfTextSpot spot;
    spot.pos = 0;
    spot.ptr = (uint8_t*) sentence->text;

    this->before = new Stage(spot);
    this->after  = NULL;
    this->ahead  = NULL;

    StackNode *node = new StackNode(before, 0);
    this->before->nodes.push_back(node);
}

bool PgfParser::shift(StackNode *parent, ref<PgfConcrLincat> lincat, size_t r, Production *prod,
                      Stage *before, Stage *after)
{
    vector<PgfLRShift> shifts = concr->lrtable[parent->state_id].shifts;
    for (size_t i = 0; i < shifts.size(); i++) {
        ref<PgfLRShift> shift = shifts.elem(i);
        if (lincat == shift->lincat && r == shift->r) {
            StackNode *node = NULL;
            for (StackNode *n : after->nodes) {
                if (n->stage == before && n->state_id == shift->next_state) {
                    node = n;
                    break;
                }
            }
            if (node == NULL) {
                node = new StackNode(before, shift->next_state);
                node->choice = new Choice(++last_fid);
                dynamic.push_back(node->choice);
                after->nodes.push_back(node);
            }

            bool added = true;
            for (Production *other : node->choice->prods) {
                if (*prod == *other) {
                    added = false;
                    break;
                }
            }

            if (added) {
                node->choice->prods.push_back(prod);
#ifdef DEBUG_PARSER
                print_prod(node->choice, prod);
#endif
            }

            if (std::find(node->parents.begin(), node->parents.end(), parent) == node->parents.end()) {
                node->parents.push_back(parent);
#ifdef DEBUG_PARSER
                print_transition(parent,node,after,0);
#endif
            }

            return added;
        }
    }

    return false;
}

void PgfParser::shift(StackNode *parent, Stage *before)
{
    vector<PgfLRShiftKS> shifts = concr->lrtable[parent->state_id].tokens;
    if (shifts != 0) {
        const uint8_t *sent_end = (const uint8_t *) &sentence->text[sentence->size];
        for (size_t i = 0; i < shifts.size(); i++) {
            ref<PgfLRShiftKS> shift = shifts.elem(i);
            PgfTextSpot spot = before->end;
            size_t sym_idx = shift->sym_idx;
            int cmp =
                text_sequence_cmp(&spot, sent_end,
                                  shift->seq, &sym_idx,
                                  case_sensitive, SM_PARTIAL);
            if (cmp == 0) {
                start_matches(&spot, NULL);

                StackNode *node = NULL;
                for (StackNode *n : after->nodes) {
                    if (n->stage == before && n->state_id == shift->next_state) {
                        node = n;
                        break;
                    }
                }
                if (node == NULL) {
                    node = new StackNode(before, shift->next_state);
                    node->choice = NULL;
                    after->nodes.push_back(node);
                }

                if (std::find(node->parents.begin(), node->parents.end(), parent) == node->parents.end()) {
                    node->parents.push_back(parent);
#ifdef DEBUG_PARSER
                    print_transition(parent,node,after,shift);
#endif
                }

                end_matches(&spot, NULL);
            }
        }
    }
}

void PgfParser::shift(StackNode *parent, Stage *before, Stage *after)
{
    size_t next_bind_state = concr->lrtable[parent->state_id].next_bind_state;
    if (next_bind_state != 0) {
        StackNode *node = NULL;
        for (StackNode *n : after->nodes) {
            if (n->stage == before && n->state_id == next_bind_state) {
                node = n;
                break;
            }
        }
        if (node == NULL) {
            node = new StackNode(before, next_bind_state);
            node->choice = NULL;
            after->nodes.push_back(node);
        }

        if (std::find(node->parents.begin(), node->parents.end(), parent) == node->parents.end()) {
            node->parents.push_back(parent);
#ifdef DEBUG_PARSER
            print_transition(parent,node,after,0);
#endif
        }
    }
}

PgfParser::Choice *PgfParser::intersect_choice(Choice *choice1, Choice *choice2, intersection_map &im)
{
    if (choice1 == NULL)
        return choice2;
    if (choice2 == NULL)
        return choice1;
    if (choice1 == choice2)
        return choice1;

    std::pair<Choice*,Choice*> key(choice1,choice2);
    auto it = im.find(key);
    if (it != im.end()) {
        return it->second;
    }

    Choice *choice = new Choice(++last_fid);
    dynamic.push_back(choice);
    im[key] = choice;
    for (Production *prod1 : choice1->prods) {
        for (Production *prod2 : choice2->prods) {
            if (prod1->lin == prod2->lin && prod1->index == prod2->index) {
                Production *prod = new(prod1->lin,prod1->index) Production();
                choice->prods.push_back(prod);

                for (size_t i = 0; i < prod->n_args; i++) {
                    Choice *arg = intersect_choice(prod1->args[i],prod2->args[i],im);
                    if (arg == NULL) {
                        //delete choice;
                        return NULL;
                    }
                    prod->args[i] = arg;
                }

#ifdef DEBUG_PARSER
                print_prod(choice, prod);
#endif
            }
        }
    }

    return choice;
}

void PgfParser::reduce(StackNode *parent, ref<PgfConcrLin> lin, ref<PgfLRReduce> red,
                       size_t n, std::vector<Choice*> &args,
                       Stage *before, Stage *after)
{
    if (n == 0) {
        ref<PgfConcrLincat> lincat = lin->lincat;

        size_t index = red->seq_idx / lincat->fields.size();
        size_t r     = red->seq_idx % lincat->fields.size();
        Production *prod = new(lin,index) Production();

        for (size_t i = 0; i < prod->n_args; i++) {
            auto arg = red->args.elem(i);

            if (arg->stk_idx > 0) {
                Choice *choice = args[red->depth-arg->stk_idx];
                if (choice != NULL) {
                    intersection_map im;
                    choice = intersect_choice(choice, prod->args[i], im);
                    if (choice == NULL) {
                        //delete prod;
                        return;
                    }
                }
                prod->args[i] = choice;
            }
            if (arg->arg != 0) {
                Choice *choice = retrieve_choice(arg->arg);
                if (choice != NULL) {
                    intersection_map im;
                    choice = intersect_choice(choice, prod->args[i], im);
                    if (choice == NULL) {
                        //delete prod;
                        return;
                    }
                }
                prod->args[i] = choice;
            }
        }

        if (!shift(parent, lincat, r, prod, before, after)) {
            delete prod;
        }
        return;
    }

    args.push_back(parent->choice);
    for (auto node : parent->parents) {
        reduce(node, lin, red, n-1, args, parent->stage, after);
    }
    args.pop_back();
}

PgfParser::Choice *PgfParser::retrieve_choice(ref<PgfLRReduceArg> arg)
{
    if (arg == 0)
        return NULL;

    Choice *&tmp   = persistant[arg.tagged()];
    Choice *choice = tmp;
    if (choice == NULL) {
        tmp = new Choice(++last_fid); choice = tmp;
        for (size_t i = 0; i < arg->n_prods; i++) {
            Production *prod = new(arg->prods[i].lin, arg->prods[i].index) Production();
            for (size_t j = 0; j < prod->n_args; j++) {
                auto child = arg->prods[i].args[j];
                prod->args[j] = retrieve_choice(child);
            }
            choice->prods.push_back(prod);
#ifdef DEBUG_PARSER
            print_prod(choice, prod);
#endif
        }
    }

    return choice;
}

void PgfParser::complete(StackNode *parent, ref<PgfConcrLincat> lincat, size_t r,
                         size_t n, std::vector<Choice*> &args)
{
    if (n == 0) {
        top_choice = args[0];
        return;
    }

    args.push_back(parent->choice);
    for (auto node : parent->parents) {
        complete(node, lincat, r, n-1, args);
    }
    args.pop_back();
}

void PgfParser::reduce_all(StackNode *node)
{
    vector<PgfLRReduce> reductions = concr->lrtable[node->state_id].reductions;
    for (size_t j = 0; j < reductions.size(); j++) {
        ref<PgfLRReduce> red = reductions.elem(j);
        switch (ref<PgfConcrLin>::get_tag(red->lin_obj)) {
        case PgfConcrLin::tag: {
            auto lin =
                ref<PgfConcrLin>::untagged(red->lin_obj);
            std::vector<Choice*> args;
            reduce(node, lin, red, red->depth, args, before, before);
            break;
        }
        case PgfConcrLincat::tag: {
            auto lincat =
                ref<PgfConcrLincat>::untagged(red->lin_obj);
            std::vector<Choice*> args;
            if (before->end.pos == sentence->size) {
                complete(node, lincat, red->seq_idx % lincat->fields.size(), red->depth, args);
            }
        }
        }
    }
}

void PgfParser::space(PgfTextSpot *start, PgfTextSpot *end, PgfExn* err)
{
#ifdef DEBUG_PARSER
    fprintf(stderr, "------------------ position %zu-%zu ------------------\n",
                    start->pos, end->pos);
#endif

    while (ahead != NULL && ahead->start.pos <= start->pos) {
        Stage *tmp = ahead->next;
        ahead->next = before;
        before = ahead;
        ahead = tmp;
    }

    before->end = *end;

    if (before->next != NULL && before->start.pos==before->end.pos) {
        after = new Stage(*end);
        after->next = before;
        size_t i = 0;
        while (i < before->nodes.size()) {
            StackNode *node = before->nodes[i++];
            reduce_all(node);
            shift(node, before, after);
        }
        before = after;
    }

    size_t i = 0;
    while (i < before->nodes.size()) {
        StackNode *node = before->nodes[i++];
        reduce_all(node);
        shift(node, before);
    }
}

void PgfParser::start_matches(PgfTextSpot *end, PgfExn* err)
{
    Stage **last = &ahead; after = *last;
    while (after != NULL && after->start.pos < end->pos) {
        last = &after->next; after = *last;
    }

    if (after == NULL) {
        *last = new Stage(*end);
        after = *last;
    }
}

void PgfParser::match(ref<PgfConcrLin> lin, size_t seq_index, PgfExn* err)
{
    size_t index = seq_index / lin->lincat->fields.size();
    size_t r     = seq_index % lin->lincat->fields.size();

    for (StackNode *parent : before->nodes) {
        Production *prod = new(lin,index) Production();
        if (!shift(parent, lin->lincat, r, prod, before, after)) {
            delete prod;
        }
    }
}

void PgfParser::end_matches(PgfTextSpot *end, PgfExn* err)
{
}

bool PgfParser::CompareExprState::operator() (const ExprState *state1, const ExprState *state2) const {
    return state1->prob > state2->prob;
}

void PgfParser::prepare()
{
    if (top_choice != NULL)
        predict_expr_states(top_choice, 0);
}

void PgfParser::predict_expr_states(Choice *choice, prob_t outside_prob)
{
    for (Production *prod : choice->prods) {
        ExprState *state = new ExprState;
        state->choice = choice;
        state->prod = prod;
        state->n_args = 0;
        state->expr = u->efun(&prod->lin->name);
        state->prob = outside_prob+prod->lin->absfun->prob;
        exprs.push_back(state->expr);
        queue.push(state);
    }
}

#ifdef DEBUG_GENERATOR
void PgfParser::print_expr_state_before(PgfPrinter *printer, ExprState *state)
{
    if (state->choice->states.size() > 0) {
        ExprState *parent = state->choice->states[0];
        print_expr_state_before(printer, parent);
        printer->puts(" [");
    }
    m->match_expr(printer, state->expr);
}

void PgfParser::print_expr_state_after(PgfPrinter *printer, ExprState *state)
{
    for (size_t i = state->n_args+1; i < state->prod->n_args; i++) {
        if (state->prod->args[i] == NULL)
            printer->puts(" ?");
        else
            printer->nprintf(32, " ?%d", state->prod->args[i]->fid);
    }

    if (state->choice->states.size() > 0) {
        printer->puts("]");
        ExprState *parent = state->choice->states[0];
        print_expr_state_after(printer, parent);
    }
}

void PgfParser::print_expr_state(ExprState *state)
{
    PgfPrinter printer(NULL, 0, m);

    printer.nprintf(16, "[%f] ", state->prob);
    print_expr_state_before(&printer, state);
    if (state->n_args < state->prod->n_args) {
        Choice *choice = state->prod->args[state->n_args];
        if (choice == NULL)
            printer.puts(" ?");
        else
            printer.nprintf(32, " ?%d", state->prod->args[state->n_args]->fid);
    }
    print_expr_state_after(&printer, state);
    printer.puts("\n");

    PgfText *text = printer.get_text();
    fputs(text->text, stderr);
    free(text);
}
#endif

bool PgfParser::process_expr_state(ExprState *state)
{
    if (state->n_args >= state->prod->n_args) {
        complete_expr_state(state);
        return true;
    }

    Choice *choice = state->prod->args[state->n_args];
    if (choice == NULL) {
        PgfExpr meta = u->emeta(0);
        PgfExpr app = u->eapp(state->expr, meta);
        exprs.push_back(app);
        u->free_ref(meta);
        state->expr = app;
        state->n_args++;
        queue.push(state);
    } else {
        choice->states.push_back(state);

        if (choice->states.size() == 1) {
            predict_expr_states(choice, state->prob);
        } else {
            for (ExprInstance p : choice->exprs) {
                combine_expr_state(state,p);
            }
        }
    }

    return false;
}

void PgfParser::complete_expr_state(ExprState *state)
{
    Choice *choice = state->choice;

    prob_t outside_prob;
    if (choice == top_choice)
        outside_prob = 0;
    else
        outside_prob = choice->states[0]->prob;

    prob_t inside_prob = state->prob-outside_prob;
    choice->exprs.emplace_back(state->expr,inside_prob);
    for (ExprState *state : choice->states) {
        combine_expr_state(state,choice->exprs.back());
    }
}

void PgfParser::combine_expr_state(ExprState *state, ExprInstance &inst)
{
    PgfExpr app = u->eapp(state->expr, inst.expr);
    exprs.push_back(app);

    ExprState *app_state = new ExprState();
    app_state->prob   = state->prob + inst.prob;
    app_state->choice = state->choice;
    app_state->prod   = state->prod;
    app_state->n_args = state->n_args+1;
    app_state->expr   = app;
    queue.push(app_state);
}

PgfExpr PgfParser::fetch(PgfDB *db, prob_t *prob)
{
    DB_scope scope(db, READER_SCOPE);

    if (top_choice == NULL)
        return 0;

    for (;;) {
        if (top_choice_index < top_choice->exprs.size()) {
            auto inst = top_choice->exprs[top_choice_index++];
            *prob = inst.prob;
            return inst.expr;
        }

        if (queue.empty())
            return 0;

        ExprState *state = queue.top(); queue.pop();
#ifdef DEBUG_GENERATOR
        print_expr_state(state);
#endif

        if (process_expr_state(state)) {
            delete state;
        }
    }

    return 0;
}

PgfParser::~PgfParser()
{
    while (before != NULL) {
        Stage *tmp = before;
        before = before->next;
        delete tmp;
    }

    while (ahead != NULL) {
        Stage *tmp = ahead;
        ahead = ahead->next;
        delete tmp;
    }

    for (auto it : persistant) {
        delete it.second;
    }

    for (Choice *choice : dynamic) {
        delete choice;
    }

    for (PgfExpr expr : exprs) {
        u->free_ref(expr);
    }

    while (!queue.empty()) {
        ExprState *state = queue.top(); queue.pop();
        delete state;
    }
}
