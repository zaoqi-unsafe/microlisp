/* Single file scheme interpreter
MIT License

Copyright Michael Lazear (c) 2016 

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>


#define null(x) ((x) == NULL || (x) == NIL)
#define EOL(x) 	(null((x)) || (x) == EMPTY_LIST)
#define error(x) do { fprintf(stderr, "%s\n", x); exit(1); }while(0)
#define caar(x) (car(car((x))))
#define cdar(x) (cdr(car((x))))
#define cadr(x) (car(cdr((x))))
#define caddr(x) (car(cdr(cdr((x)))))
#define cadddr(x) (car(cdr(cdr(cdr((x))))))
#define cadar(x) (car(cdr(car((x)))))
#define cddr(x) (cdr(cdr((x))))
#define atom(x) (!null(x) && (x)->type != LIST)

typedef enum { INTEGER, SYMBOL, STRING, LIST, PRIMITIVE } type_t;
typedef struct object* (*primitive_t)(struct object*);

/* Lisp object. We want to mimic the homoiconicity of LISP, so we will not be
providing separate "types" for procedures, etc. Everything is represented as
atoms (integers, strings, booleans) or a list of atoms */
typedef struct object object_t;
struct object {
	type_t type;
	union {
		int integer;
		char* string;
		struct {
			struct object* car;
			struct object* cdr;
		};
		primitive_t primitive;
	};
};

/* We declare a couple of global variables as keywords */
static struct object* ENV;
static struct object* NIL;
static struct object* EMPTY_LIST;
static struct object* TRUE;
static struct object* QUOTE;
static struct object* DEFINE;
static struct object* SET;
static struct object* LET;
static struct object* LAMBDA;
static struct object* BEGIN;
static struct object* PROCEDURE;

struct object* read_exp(FILE* in);
struct object* eval(struct object* exp, struct object* env);
struct object* cons(struct object* x, struct object* y);

/*==============================================================================
  Memory management
==============================================================================*/

void* HEAP;
int FREE_SPACE;

void alloc_heap(size_t sz) {
	HEAP = malloc(sz);
	if (HEAP == NULL) 
		error("Out of memory!");
}

/*============================================================================
Constructors and etc
==============================================================================*/

struct object* make_symbol(char* s) {
	struct object* ret = malloc(sizeof(struct object));
	ret->type = SYMBOL;
	ret->string = strdup(s);
	return ret;
}

struct object* make_integer(int x) {
	struct object* ret = malloc(sizeof(struct object));
	ret->type = INTEGER;
	ret->integer = x;
	return ret;	
}

struct object* make_primitive(primitive_t x) {
	struct object* ret = malloc(sizeof(struct object));
	ret->type = PRIMITIVE;
	ret->primitive = x;
	return ret;	
}

struct object* make_lambda(struct object* params, struct object* body) {
	return cons(LAMBDA, cons(params, body));
}

struct object* make_procedure(struct object* params, struct object* body, 
	struct object* env) {
	return cons(PROCEDURE, cons(params, cons(body, cons(env, EMPTY_LIST))));
}

struct object* cons(struct object* x, struct object* y) {
	struct object* ret = malloc(sizeof(struct object));
	ret->type = LIST;
	ret->car = x;
	ret->cdr = y;
	return ret;
}

struct object* car(struct object* cell) {
	if (null(cell) || cell->type != LIST)
		return NIL;
	return cell->car;
}

struct object* cdr(struct object* cell) {
	if (null(cell) || cell->type != LIST)
		return NIL;
	return cell->cdr;
}

struct object* append(struct object* l1, struct object* l2) {
	if (null(l1))
		return l2;
	return cons(car(l1), append(cdr(l1), l2));
}

struct object* reverse(struct object* list, struct object* first) {
	if (null(list))
		return first;
	return reverse(cdr(list), cons(car(list), first));

}

bool is_equal(struct object* x, struct object* y) {

	if (x == y)
		return true;
	if (null(x) || null(y))
		return false;
	if (x->type != y->type)
		return false;
	switch(x->type) {
		case INTEGER:
			return x->integer == y->integer;
		case SYMBOL: case STRING:
			return !strcmp(x->string, y->string);
	}
}

bool is_tagged(struct object* cell, struct object* tag) {
	if (null(cell) || cell->type != LIST)
		return false;
	return is_equal(car(cell), tag);
}

struct object* prim_cons(struct object* args) {
	return cons(car(args), cadr(args));
}

struct object* prim_car(struct object* args) {
	return caar(args);
}

struct object* prim_cdr(struct object* args) {
	return cdar(args);
}


/*==============================================================================
Environment handling
==============================================================================*/

object_t* extend_env(object_t* var, object_t* val, object_t* env) {
	return cons(cons(var, val), env);
}

object_t* lookup_variable(object_t* var, object_t* env) {
	while (!null(env)) {
		object_t* frame = car(env);
		object_t* vars 	= car(frame);
		object_t* vals 	= cdr(frame);
		while(!null(vars)) {
			if (is_equal(car(vars), var))
				return car(vals);
			vars = cdr(vars);
			vals = cdr(vals);
		}
		env = cdr(env);
	}
	return NIL;
}

/* set_variable binds var to val in the first frame in which var occurs */
void set_variable(object_t* var, object_t* val, object_t* env) {
	while (!null(env)) {
		object_t* frame = car(env);
		object_t* vars 	= car(frame);
		object_t* vals 	= cdr(frame);
		while(!null(vars)) {
			if (is_equal(car(vars), var)) {
				vals->car = val;
				return;
			}
			vars = cdr(vars);
			vals = cdr(vals);
		}
		env = cdr(env);
	}
}

/* define_variable binds var to val in the *current* frame */
void define_variable(object_t* var, object_t* val, object_t* env) {
	object_t* frame = car(env);
	object_t* vars = car(frame);
	object_t* vals = cdr(frame);

	while(!null(vars)) {
		if (is_equal(var, car(vars))) {
			vals->car = val;
			return;
		}
		vars = cdr(vars);
		vals = cdr(vals);
	}
	frame->car = cons(var, car(frame));
	frame->cdr = cons(val, cdr(frame));
//	add_binding(var, val, frame);
}


/*==============================================================================
Recursive descent parser 
==============================================================================*/

char SYMBOLS[] = "~!@#$%^&*_-+\\:,.<>|{}[]";

int peek(FILE* in) {
	int c = getc(in);
	ungetc(c, in);
	return c;
}

/* skip characters until end of line */
void skip(FILE* in) {
	int c;
	for(;;) {
		c = getc(in);
		if (c == '\n' || c == EOF)
			return;
	}
}

struct object* read_string(FILE* in) {
	char buf[256];
    int i = 0;
    int c;
    while ((c = getc(in)) != '\"') {
    	if (c == EOF)
    		return NIL;
        if (i >= 256)
            error("String too long");
        buf[i++] = (char)c;
    }
    buf[i] = '\0';
    struct object* s = make_symbol(buf);
    s->type = STRING;
    return s;
}


struct object* read_symbol(FILE* in, char start) {
	char buf[128];
	buf[0] = start;
    int i = 1;
    while (isalnum(peek(in)) || strchr(SYMBOLS, peek(in))) {
        if (i >= 128)
            error("Symbol name too long");
        buf[i++] = getc(in);
    }
    buf[i] = '\0';
    return make_symbol(buf);
}

int read_int(FILE* in, int start) {
	while(isdigit(peek(in))) 
		start = start * 10 + (getc(in) - '0');
	return start;
}

struct object* read_list(FILE* in) {
	struct object* obj;
	struct object* cell = EMPTY_LIST;
	for (;;) {
		obj = read_exp(in);
		
		if (obj == EMPTY_LIST)
			return reverse(cell, EMPTY_LIST);
		cell = cons(obj, cell);
	}
	return EMPTY_LIST;
}

struct object* read_quote(FILE* in) {
	return cons(QUOTE, cons(read_exp(in), NIL));
}

int depth = 0;
struct object* read_exp(FILE* in) {
	int c;
	struct object* root = NIL;

	for(;;) {
		c = getc(in);
		if (c == '\n' || c == ' ' || c == '\t') {
			int i;
			for (i = 1; i < depth; i++)
				printf("..");
			continue;
		}
		if (c == ';') {
			skip(in);
			continue;
		}
		if (c == EOF)
			return NULL;
		if (c == '\"')
			return read_string(in);
		if (c == '\'')
			return read_quote(in);
		if (c == '(') {
			depth++;
			return read_list(in);
		}
		if (c == ')') {
			depth--;
			return EMPTY_LIST;
		}
		if (isdigit(c))
			return make_integer(read_int(in, c - '0'));
		if (c == '-' && isdigit(peek(in))) 
			return make_integer(-1 * read_int(in, getc(in) - '0'));
		if (isalpha(c) || strchr(SYMBOLS, c)) 
			return read_symbol(in, c);
	}
	return root;
}

void print_exp(struct object* e) {
	if (e == EMPTY_LIST || null(e)) {
		printf("'()");
		return;
	}
	switch(e->type) {
		case STRING:
			printf("\"%s\"", e->string);
			break;
		case SYMBOL:
			printf("%s", e->string);
			break;
		case INTEGER:
			printf("%d", e->integer);
			break;
		case LIST:
			if (is_tagged(e, PROCEDURE)) {
				printf("<closure>");
				return;
			}
			printf("(");
			struct object** t = &e;
			while(!null(*t)) {
				print_exp((*t)->car);
				if (!null((*t)->cdr)) {
					printf(" ");
					if ((*t)->cdr->type == LIST) {
						t = &(*t)->cdr;						
					}
					else{
						printf(". ");
						print_exp((*t)->cdr);
						break;
					}
				} else
					break;
			}				
			printf(")");
	}
}

/*==============================================================================
LISP evaluator 
==============================================================================*/

struct object* evlis(struct object* exp, struct object* env) {
	if ( null(exp))
		return NIL;
	return cons(eval(car(exp), env), evlis(cdr(exp), env));
}

struct object* eval_sequence(struct object* exps, struct object* env) {
	if (null(cdr(exps))) 
		return eval(car(exps), env);
	eval(car(exps), env);
	return eval_sequence(cdr(exps), env);
}

struct object* eval(struct object* exp, struct object* env) {

tail:
	if (null(exp) || exp == EMPTY_LIST) {
		return NIL;
	} else if (exp->type == INTEGER || exp->type == STRING) {
		return exp;
	} else if (exp->type == SYMBOL){
		return lookup_variable(exp, env);
	} else if (is_tagged(exp, QUOTE)) {
		return cadr(exp);
	} else if (is_tagged(exp, LAMBDA)) {
		return make_procedure(cadr(exp), cddr(exp), env);
	} else if (is_tagged(exp, DEFINE)) {
		if (atom(cadr(exp))) 
			define_variable(cadr(exp), eval(caddr(exp), env), env);
		else {
			struct object* closure = eval(make_lambda(cdr(cadr(exp)), cddr(exp)), env);
			define_variable(car(cadr(exp)), closure, env);
		}
		return make_symbol("ok");
	} else if (is_tagged(exp, BEGIN)) {
 		object_t* args = cdr(exp);
 		for (; !null(cdr(args)); args = cdr(args))
 			eval(car(args), env);
 		exp = car(args);
 		goto tail;
  	} else if (is_tagged(exp, SET)) {
		if (atom(cadr(exp))) 
			set_variable(cadr(exp), eval(caddr(exp), env), env);
		else {
			struct object* closure = eval(make_lambda(cdr(cadr(exp)), cddr(exp)), env);
			set_variable(car(cadr(exp)), closure, env);
		}
		return make_symbol("ok");
	} else if (is_tagged(exp, LET)) {
		/* We go with the strategy of transforming let into a lambda function*/
		struct object** tmp;
		struct object* vars = NIL;
		struct object* vals = NIL;
		for (tmp =&exp->cdr->car; !null(*tmp); tmp=&(*tmp)->cdr) {
			vars = cons(caar(*tmp), vars);
			vals = cons(cadar(*tmp), vals);
		}
		exp = cons(make_lambda(vars, cddr(exp)), vals);
		goto tail;
	} else {
		/* procedure structure is as follows:
		('procedure, (parameters), (body), (env)) */
		struct object* proc = eval(car(exp), env);
		struct object* args = evlis(cdr(exp), env);
		if (proc->type == PRIMITIVE) 
			return proc->primitive(args);
		if (is_tagged(proc, PROCEDURE)) {
			env = extend_env(cadr(proc), args, cadddr(proc));
			exp = cons(BEGIN, caddr(proc));	/* procedure body */
			goto tail;
		}		
		error("Invalid arguments to eval()");
	}
	return NIL;
}

int main(int argc, char** argv) {
	ENV = extend_env(NIL, NIL, NIL);
	TRUE 		= make_symbol("#t");
	QUOTE 		= make_symbol("quote");
	LAMBDA 		= make_symbol("lambda");
	PROCEDURE 	= make_symbol("procedure");
	DEFINE 		= make_symbol("define");
	SET 		= make_symbol("set!");
	LET 		= make_symbol("let");
	BEGIN 		= make_symbol("begin");
	define_variable(make_symbol("true"), TRUE, ENV);
	define_variable(make_symbol("false"), NIL, ENV);
	define_variable(make_symbol("cons"), make_primitive(prim_cons), ENV);
	define_variable(make_symbol("car"), make_primitive(prim_car), ENV);
	define_variable(make_symbol("cdr"), make_primitive(prim_cdr), ENV);	
	for(;;) {
		printf("user> ");
		struct object* exp = read_exp(stdin);
		printf("====> ");
		print_exp(eval(exp, ENV));
		printf("\n");
	}
}