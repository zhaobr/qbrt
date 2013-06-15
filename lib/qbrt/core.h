#ifndef QBRT_CORE_H
#define QBRT_CORE_H

#include <stdint.h>
#include <map>
#include <string>
#include <vector>


// TYPE DECLARATIONS

typedef uint16_t reg_t;
typedef uint64_t TaskID;
struct qbrt_value;
struct qbrt_value_index;
struct function_value;
struct cfunction_value;
struct Worker;
struct CContext;
struct Type;
struct List;
struct Map;
struct Vector;
struct Stream;
struct Tuple;
struct Promise;
struct Failure;
typedef void (*c_function)(CContext &, qbrt_value &out);


// VALUE TYPES

#define VT_VOID		0x00
#define VT_KIND		0x01
#define VT_BSTRING	0x02
#define VT_FUNCTION	0x03
#define VT_BOOL		0x04
#define VT_FLOAT	0x05
#define VT_REF		0x06
#define VT_TUPLE	0x07
#define VT_CFUNCTION	0x08
#define VT_LIST		0x09
#define VT_MAP		0x0a
#define VT_VECTOR	0x0b
#define VT_STRUCT	0x0c
#define VT_INT		0x0d
#define VT_STREAM	0x0e
#define VT_HASHTAG	0x0f
#define VT_PROMISE	0x10
#define VT_FAILURE	0xff

extern Type TYPE_VOID;
extern Type TYPE_INT;
extern Type TYPE_BSTRING;
extern Type TYPE_BOOL;
extern Type TYPE_FLOAT;
extern Type TYPE_HASHTAG;
extern Type TYPE_REF;
extern Type TYPE_TUPLE;
extern Type TYPE_FUNCTION;
extern Type TYPE_CFUNCTION;
extern Type TYPE_LIST;
extern Type TYPE_MAP;
extern Type TYPE_VECTOR;
extern Type TYPE_STREAM;
extern Type TYPE_KIND;
extern Type TYPE_PROMISE;
extern Type TYPE_FAILURE;

struct qbrt_value
{
	union {
		bool b;
		int64_t i;
		std::string *str;
		std::string *hashtag;
		function_value *f;
		cfunction_value *cfunc;
		// binary_value *bin;
		// uint8_t *bin;
		double fp;
		qbrt_value *ref;
		qbrt_value_index *reg;
		const Type *type;
		Tuple *tuple;
		List *list;
		Map *map;
		Vector *vect;
		Stream *stream;
		Promise *promise;
		Failure *failure;
	} data;
	const Type *type;

	operator List & ()
	{
		return *data.list;
	}
	operator const List & () const
	{
		return *data.list;
	}
	operator Vector * ()
	{
		return data.vect;
	}
	operator const Vector * () const
	{
		return data.vect;
	}

	qbrt_value()
	: type(&TYPE_VOID)
	{
		data.str = NULL;
	}
	static void set_void(qbrt_value &);
	static void b(qbrt_value &v, bool b)
	{
		v.type = &TYPE_BOOL;
		v.data.b = b;
	}
	static void i(qbrt_value &v, int64_t i)
	{
		v.type = &TYPE_INT;
		v.data.i = i;
	}
	static void fp(qbrt_value &v, double f)
	{
		v.type = &TYPE_FLOAT;
		v.data.fp = f;
	}
	static void str(qbrt_value &v, const std::string &s)
	{
		v.type = &TYPE_BSTRING;
		v.data.str = new std::string(s);
	}
	static void hashtag(qbrt_value &v, const std::string &h)
	{
		v.type = &TYPE_HASHTAG;
		v.data.hashtag = new std::string(h);
	}
	static void f(qbrt_value &v, function_value *f)
	{
		v.type = &TYPE_FUNCTION;
		v.data.f = f;
	}
	static void cfunc(qbrt_value &v, cfunction_value *f)
	{
		v.type = &TYPE_CFUNCTION;
		v.data.cfunc = f;
	}
	static void ref(qbrt_value &, qbrt_value &ref);
	static void typ(qbrt_value &v, const Type *t)
	{
		set_void(v);
		v.type = &TYPE_KIND;
		v.data.type = t;
	}
	static void list(qbrt_value &v, List *l)
	{
		set_void(v);
		v.type = &TYPE_LIST;
		v.data.list = l;
	}
	static void promise(qbrt_value &v, Promise *p)
	{
		v.type = &TYPE_PROMISE;
		v.data.promise = p;
	}
	static void map(qbrt_value &v, Map *m)
	{
		set_void(v);
		v.type = &TYPE_MAP;
		v.data.map = m;
	}
	static void vect(qbrt_value &dst, Vector *v)
	{
		set_void(dst);
		dst.type = &TYPE_VECTOR;
		dst.data.vect = v;
	}
	static void stream(qbrt_value &dst, Stream *s)
	{
		set_void(dst);
		dst.type = &TYPE_STREAM;
		dst.data.stream = s;
	}
	static void fail(qbrt_value &dst, Failure *f)
	{
		set_void(dst);
		dst.type = &TYPE_FAILURE;
		dst.data.failure = f;
	}

	~qbrt_value() {}
};

struct qbrt_value_index
{
	virtual uint8_t num_values() const = 0;
	virtual qbrt_value & value(uint8_t) = 0;
	virtual const qbrt_value & value(uint8_t) const = 0;
};

int qbrt_compare(const qbrt_value &, const qbrt_value &);

static inline const Type & value_type(const qbrt_value &v)
{
	return *v.type;
}


// OP CODES

#define OP_NOOP		0x00
#define OP_CALL		0x01
#define OP_TAIL_CALL	0x02
#define OP_RETURN	0x03
#define OP_REF		0x04
#define OP_COPY		0x05
#define OP_MOVE		0x06
#define OP_CONSTI	0x08
#define OP_CONSTS	0x09
#define OP_LFUNC	0x0a
#define OP_LOADOBJ	0x0b
#define OP_LOADTYPE	0x0c
#define OP_LPFUNC	0x0d
#define OP_UNIMORPH	0x0e
#define OP_LCONTEXT	0x0f
#define OP_LONGBR
#define OP_GOTO		0x11
#define OP_BRF		0x12
#define OP_BRT		0x13
#define OP_BREQ		0x14
#define OP_BRNE		0x15
#define OP_BRLT		0x16
#define OP_BRGT		0x17
#define OP_BRLTEQ	0x18
#define OP_BRGTEQ	0x19
#define OP_BRFAIL	0x1a
#define OP_BRNFAIL	0x1b
#define OP_EQ
#define OP_LT
#define OP_LE
#define OP_NOT
#define OP_ADDI		0x30
#define OP_ISUB		0x31
#define OP_IMULT	0x32
#define OP_IDIV		0x33
#define OP_CTUPLE	0x40
#define OP_STUPLE	0x41
#define OP_CLIST	0x42
#define OP_CONS		0x43
#define OP_CONSTHASH	0x44
#define OP_STRACC	0x46
#define OP_CFAILURE	0x47
#define OP_CALL_LAZY	0x48
#define OP_CALL_REMOTE	0x49
#define OP_FORK		0x4a
#define OP_WAIT		0x4b
#define NUM_OP_CODES	0x100


// CONSTRUCT REGISTERS
#define REG(r)		((uint16_t) (0x80 | (r & 0x7f)))
#define REG2(p,s)	((uint16_t) (((p & 0x7f) << 8) | (s & 0x7f)))
#define REGC(c)		((uint16_t) (0x8000 | (0x3fff & c)))

// REGISTER QUERIES
#define IS_USER_REG(r)		((r & 0x8000) == 0x0000)
#define IS_SYSTEM_REG(r)	((r & 0x8000) == 0x8000)
#define IS_PRIMARY_REG(r)	((r & 0x8080) == 0x0080)
#define IS_SECONDARY_REG(r)	((r & 0x8080) == 0x0000)
#define IS_CONST_REG(r)		((r & 0xc000) == 0x8000)
#define IS_SPECIAL_REG(r)	((r & 0xc000) == 0xc000)

#define EXTRACT_PRIMARY_REG(r)		(r & 0x007f)
#define EXTRACT_SECONDARY_REG1(r)	((r >> 8) & 0x007f)
#define EXTRACT_SECONDARY_REG2(r)	(r & 0x007f)

#define REG_RESULT	0xc000

#define REG_VOID	REGC(0x000) // void
#define REG_FALSE	REGC(0x010) // false
#define REG_TRUE	REGC(0x011) // true
#define REG_FZERO	REGC(0x012) // 0.0
#define REG_EMPTYSTR	REGC(0x013) // ""
#define REG_NEWLINE	REGC(0x014) // "\n"
#define REG_EMPTYLIST	REGC(0x015) // []
#define REG_EMPTYVECT	REGC(0x016) // []
#define REG_CINT(i)	REGC(0x03f & i) // 0x0 - 0xf
#define NUM_CONST_REGISTERS 0x20

#define CONST_REG_ID(c)	(0x3fff & c)

// register types
// 0b0x/1 -> primary 128
// 0b0x/0 -> secondary registers
// 0b10/x -> const N
// 0b11/x -> special N


std::string pretty_reg(uint16_t);


// CODE

struct instruction
{
	inline const uint8_t & opcode() const
	{
		return *(const uint8_t *) this;
	}
};

struct jump_instruction
: public instruction
{
	int16_t jump() const { return *(int16_t *) (((char *) this) + 1); }
	void setjump(int16_t jmp)
	{
		(*(int16_t *) (((char *) this) + 1)) = jmp;
	}
};


extern uint8_t INSTRUCTION_SIZE[NUM_OP_CODES];
void init_instruction_sizes();

uint8_t isize(uint8_t opcode);

static inline uint8_t isize(const instruction &i)
{
	return isize(i.opcode());
}

static inline void endian_fix(uint16_t &i)
{
	uint8_t *f = (uint8_t *) &i;
	uint8_t tmp = f[0];
	f[0] = f[1];
	f[1] = tmp;
}

static inline uint16_t endian_swap(uint16_t i)
{
	uint8_t *f = (uint8_t *) &i;
	uint8_t tmp = f[0];
	f[0] = f[1];
	f[1] = tmp;
	return i;
}

#endif