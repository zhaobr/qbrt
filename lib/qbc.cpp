
#include <list>
#include <map>
#include <set>
#include <stdlib.h>
#include <vector>
#include <errno.h>
#include <iostream>
#include <sstream>
#include <memory>
#include <sys/stat.h>
#include <fcntl.h>
#include "qbrt/core.h"
#include "qbrt/module.h"
#include "instruction.h"
#include "qbtoken.h"
#include "qbparse.h"
#include "qbc.h"
#include <string.h>
#include <endian.h>

using namespace std;


Stmt::List *parsed_stmts;
uint16_t AsmResource::NULL_INDEX = 0;
string g_parse_module;

typedef std::list< ResourceInfo > ResourceIndex;

struct ObjectBuilder
{
	ObjectHeader header;
	ResourceSet rs;

	ObjectBuilder(const string &modname)
	: header()
	, rs(modname)
	{}
};


void asm_jump(AsmFunc &f, const string &lbl, jump_instruction *j)
{
	f.jump.push_back(jump_data(*j, lbl, f.code.size()));
	asm_instruction(f, j);
}

void label_next(AsmFunc &f, const string &lbl)
{
	label_map::const_iterator it(f.label.find(lbl));
	if (it != f.label.end()) {
		cerr << "duplicate label: " << lbl << " in function "
			<< f.name.value << endl;
		exit(1);
	}
	f.label[lbl] = f.code.size();
}


uint32_t label_index(const label_map &lmap, const std::string &lbl)
{
	label_map::const_iterator it(lmap.find(lbl));
	if (it == lmap.end()) {
		cerr << "unknown label: " << lbl << endl;
		return -1;
	}
	return it->second;
}


template < typename T >
int compare_asmptr(const T *a, const T *b)
{
	if (!(a || b)) {
		return 0;
	}
	if (!a) {
		return -1;
	}
	if (!b) {
		return 1;
	}
	return AsmCmp< T >::cmp(*a, *b);
}

template <>
int AsmCmp< AsmString >::cmp(const AsmString &a, const AsmString &b)
{
	if (a.value.empty() && b.value.empty()) {
		return 0;
	}
	if (a.value.empty()) {
		return -1;
	}
	if (b.value.empty()) {
		return 1;
	}
	if (a.value < b.value) {
		return -1;
	}
	if (a.value > b.value) {
		return 1;
	}
	return 0;
}

template <>
int AsmCmp< AsmHashTag >::cmp(const AsmHashTag &a, const AsmHashTag &b)
{
	if (a.value < b.value) {
		return -1;
	}
	if (a.value > b.value) {
		return 1;
	}
	return 0;
}

template <>
int AsmCmp< AsmImport >::cmp(const AsmImport &a, const AsmImport &b)
{
	// there should only be one of these per module
	cerr << "too many AsmImport objects\n";
	exit(1);
	return 0;
}

template <>
int AsmCmp< AsmModSym >::cmp(const AsmModSym &a, const AsmModSym &b)
{
	int comparison(AsmCmp< AsmString >::cmp(a.module, b.module));
	if (comparison) {
		return comparison;
	}
	return AsmCmp< AsmString >::cmp(a.symbol, b.symbol);
}

template <>
int AsmCmp< AsmModSymList >::cmp(const AsmModSymList &a, const AsmModSymList &b)
{
	AsmModSymList::const_iterator x(a.begin());
	AsmModSymList::const_iterator y(b.begin());
	int comparison;
	for (;;) {
		if (x == a.end() && y == b.end()) {
			break;
		}
		if (x == a.end()) {
			return -1;
		}
		if (y == b.end()) {
			return 1;
		}
		comparison = compare_asmptr< AsmModSym >(*x, *y);
		if (comparison) {
			return comparison;
		}
	}
	return 0;
}

template <>
int AsmCmp< AsmProtocol >::cmp(const AsmProtocol &a, const AsmProtocol &b)
{
	return AsmCmp< AsmString >::cmp(a.name, b.name);
}

template <>
int AsmCmp< AsmTypeSpecList >::cmp(
		const AsmTypeSpecList &, const AsmTypeSpecList &);

template <>
int AsmCmp< AsmPolymorph >::cmp(const AsmPolymorph &a, const AsmPolymorph &b)
{
	int comparison(AsmCmp< AsmModSym >::cmp(a.protocol, b.protocol));
	if (comparison) {
		return comparison;
	}

	return AsmCmp< AsmTypeSpecList >::cmp(a.type, b.type);
}

template <>
int AsmCmp< AsmTypeSpec >::cmp(const AsmTypeSpec &a, const AsmTypeSpec &b)
{
	int comparison(AsmCmp< AsmModSym >::cmp(*a.name, *b.name));
	if (comparison) {
		return comparison;
	}
	return compare_asmptr< AsmTypeSpecList >(a.args, b.args);
}

template <>
int AsmCmp< AsmTypeSpecList >::cmp(
		const AsmTypeSpecList &a, const AsmTypeSpecList &b)
{
	if (a.empty() && b.empty()) {
		return 0;
	}
	uint16_t argca(a.size());
	uint16_t argcb(b.size());
	if (argca < argcb) {
		return -1;
	}
	if (argca > argcb) {
		return 1;
	}

	AsmTypeSpecList::const_iterator ita(a.begin());
	AsmTypeSpecList::const_iterator itb(b.begin());
	while (ita!=a.end() && itb!=b.end()) {
		int comparison(AsmCmp< AsmTypeSpec >::cmp(**ita, **itb));
		if (comparison) {
			return comparison;
		}
		++ita;
		++itb;
	}
	return 0;
}

template <>
int AsmCmp< AsmConstruct >::cmp(const AsmConstruct &a, const AsmConstruct &b)
{
	return AsmCmp< AsmString >::cmp(a.name, b.name);
}

template <>
int AsmCmp< AsmDataType >::cmp(const AsmDataType &a, const AsmDataType &b)
{
	return AsmCmp< AsmString >::cmp(a.name, b.name);
}

/**
 * Comparison order is:
 *   function context
 *   if traditional:
 *     function name
 *   if protocol
 *     protocol name
 *     function name
 *   if polymorph
 *     protocol module
 *     protocol name
 *     function name
 *     function param types
 */
template <>
int AsmCmp< AsmFunc >::cmp(const AsmFunc &a, const AsmFunc &b)
{
	int fcta(PFC_TYPE(a.fcontext));
	int fctb(PFC_TYPE(b.fcontext));
	if (fcta < fctb) {
		return -1;
	} else if (fcta > fctb) {
		return 1;
	}
	// function contexts are equal

	int comparison;
	switch (fcta) {
		case FCT_TRADITIONAL:
			return AsmCmp< AsmString >::cmp(a.name, b.name);
		case FCT_PROTOCOL:
			comparison = compare_asmptr<AsmResource>(a.ctx, b.ctx);
			if (comparison) {
				return comparison;
			}
			return AsmCmp< AsmString >::cmp(a.name, b.name);
		case FCT_POLYMORPH:
			// fall throgh and reset indentation
			// everything else returned
			break;
	}

	const AsmPolymorph *pa = dynamic_cast< const AsmPolymorph * >(a.ctx);
	const AsmPolymorph *pb = dynamic_cast< const AsmPolymorph * >(b.ctx);
	comparison = AsmCmp< AsmModSym >::cmp(pa->protocol, pb->protocol);
	if (comparison) {
		return comparison;
	}

	comparison = AsmCmp< AsmString >::cmp(a.name, b.name);
	if (comparison) {
		return comparison;
	}

	return AsmCmp< AsmString >::cmp(a.param_types, b.param_types);
}

template < typename T >
int compare_asm(const AsmResource &a, const AsmResource &b)
{
	const T &x(static_cast< const T & >(a));
	const T &y(static_cast< const T & >(b));
	return AsmCmp< T >::cmp(x, y);
}

template <>
int AsmCmp< AsmResource >::cmp(const AsmResource &a, const AsmResource &b)
{
	if (a.type < b.type) {
		return -1;
	}
	if (a.type > b.type) {
		return 1;
	}

	switch (a.type) {
		case RESOURCE_CONSTRUCT:
			return compare_asm< AsmConstruct >(a, b);
		case RESOURCE_DATATYPE:
			return compare_asm< AsmDataType >(a, b);
		case RESOURCE_STRING:
			return compare_asm< AsmString >(a, b);
		case RESOURCE_MODSYM:
			return compare_asm< AsmModSym >(a, b);
		case RESOURCE_FUNCTION:
			return compare_asm< AsmFunc >(a, b);
		case RESOURCE_HASHTAG:
			return compare_asm< AsmHashTag >(a, b);
		case RESOURCE_IMPORT:
			return compare_asm< AsmImport >(a, b);
		case RESOURCE_PROTOCOL:
			return compare_asm< AsmProtocol >(a, b);
		case RESOURCE_POLYMORPH:
			return compare_asm< AsmPolymorph >(a, b);
		case RESOURCE_TYPESPEC:
			return compare_asm< AsmTypeSpec >(a, b);
	}
	cerr << "what type is this? not supported man.\n";
	return 0;
}

bool ResourceLess::operator() (const AsmResource *a, const AsmResource *b) const
{
	return compare_asmptr(a, b) < 0;
}


void RegAlloc::declare_arg(const string &name, const string &type)
{
	registry[name] = counter++;
}

void RegAlloc::assign_src(AsmReg &reg)
{
	// only indexed args already have the idx set
	if (reg.idx >= argc) {
		cerr << "argument register out of bounds: " << reg.name & DIE;
	}
	if (reg.idx >= 0 || reg.specialid) {
		return;
	}

	CountMap::const_iterator it(registry.find(reg.name));
	if (it != registry.end()) {
		reg.idx = it->second;
		return;
	}
	cerr << "src register is not yet allocated: " << reg.name & DIE;
}

void RegAlloc::alloc_dst(AsmReg &reg, const string &type)
{
	// only indexed args already have the idx set
	if (reg.idx >= argc) {
		cerr << "argument register out of bounds: " << reg.name & DIE;
	}
	if (reg.idx >= 0 || reg.specialid) {
		return;
	}

	CountMap::const_iterator it(registry.find(reg.name));
	if (it != registry.end()) {
		reg.idx = it->second;
		return;
	}
	reg.idx = counter++;
	registry[reg.name] = reg.idx;
}


AsmReg::operator reg_t () const
{
	reg_t id(0);
	switch (reg_type) {
		case '%':
		case '$':
			if (ext < 0) {
				id = PRIMARY_REG(idx);
			} else {
				id = SECONDARY_REG(idx, ext);
			}
			break;
		case 'c':
		case 's':
			id = specialid;
			break;
	}
	return id;
}

ostream & operator << (ostream &o, const AsmReg &r)
{
	switch (r.reg_type) {
		case '%':
		case '$':
			if (r.ext < 0) {
				o << r.reg_type << (int) r.idx;
			} else {
				o << r.reg_type << (int) r.idx
					<< '.' << (int) r.ext;
			}
			break;
		case 'c':
		case 's':
			o << r.reg_type << r.specialid;
			break;
		default:
			cerr << "Unknown register type code: "
				<< r.reg_type << endl;
			break;
	}
	return o;
}

AsmReg * AsmReg::var(const std::string &name)
{
	AsmReg *r = new AsmReg('$');
	r->name = name;
	return r;
}

AsmReg * AsmReg::arg(const std::string &index)
{
	istringstream in(index);
	uint16_t i;
	in >> i;

	AsmReg *r = new AsmReg('%');
	r->name = "%"+ index;
	r->idx = i;
	return r;
}

void AsmReg::parse_subindex(const std::string &subindex_str)
{
	istringstream in(subindex_str);
	uint16_t subindex;
	in >> subindex;
	this->ext = subindex;
}

AsmReg * AsmReg::create_special(uint16_t id)
{
	AsmReg *r = new AsmReg('s');
	r->specialid = SPECIAL_REG(id);
	return r;
}


ResourceSet::ResourceSet(const string &modname)
: index()
, imports()
, _module_name(modname)
{
	collect(imports);
	collect(_module_name);
}

void ResourceSet::set_module_version(uint16_t ver, const std::string &iter)
{
	module_version = ver;
	_module_iteration.value = iter;
	collect(_module_iteration);
}

bool ResourceSet::collect(AsmResource &res)
{
	pair< AsmResource *, uint16_t > rp(&res, 0);
	pair< map< AsmResource *, uint16_t >::iterator, bool > result;
	result = index.insert(rp);
	res.index = &result.first->second;
	return result.second;
}

void ResourceSet::import(AsmString &module)
{
	if (module.value == "*"
			|| module.value == _module_name.value)
	{
		// don't both importing the typevar module (*) or
		// the current module
	} else {
		imports.modules.insert(&module);
		imports.names.insert(module.value);
	}
	collect(module);
}

uint32_t AsmImport::write(std::ostream &o) const
{
	uint16_t num(modules.size());
	o.write((const char *) &num, 2);
	uint32_t bytes(2);
	set< AsmString * >::const_iterator it(modules.begin());
	for (; it!=modules.end(); ++it) {
		o.write((const char *) (*it)->index, 2);
		bytes += 2;
	}
	return bytes;
}

std::ostream & AsmImport::pretty(std::ostream &o) const
{
	return o << "import " << modules.size();
}

bool collect_resource(ResourceSet &rs, AsmResource &res)
{
	return rs.collect(res);
}

bool collect_string(ResourceSet &rs, AsmString &str)
{
	return rs.collect(str);
}

bool collect_modsym(ResourceSet &rs, AsmModSym &modsym)
{
	rs.import(modsym.module);
	rs.collect(modsym.symbol);
	return rs.collect(modsym);
}

bool collect_typespec(ResourceSet &rs, AsmTypeSpec &type)
{
	ostringstream fmt;
	type.serialize(fmt);
	type.fullname.value = fmt.str();

	collect_string(rs, type.fullname);
	collect_modsym(rs, *type.name);
	collect_resource(rs, type);

	if (!type.args || type.args->empty()) {
		return true;
	}

	AsmTypeSpecList::iterator it(type.args->begin());
	for (; it!=type.args->end(); ++it) {
		collect_typespec(rs, **it);
	}
	return true;
}


int16_t count_jump_bytes(codeblock::const_iterator &it, int16_t jump_count
		, int16_t jump_bytes)
{
	if (jump_count < 0) {
		jump_bytes -= isize(**(--it));
		return count_jump_bytes(it, ++jump_count, jump_bytes);
	} else if (jump_count > 0) {
		jump_bytes += isize(**(it++));
		return count_jump_bytes(it, --jump_count, jump_bytes);
	}
	return jump_bytes;
}

int16_t count_jump_bytes(codeblock::iterator &it)
{
	jump_instruction *jumpi = (jump_instruction *) (*it);
	codeblock::const_iterator cit(it);
	int16_t jmp(count_jump_bytes(cit, jumpi->jump(), 0));
	jumpi->setjump(jmp);
	return jmp;
}

void measure_jump_bytes(codeblock &code)
{
	codeblock::iterator it;
	for (it=code.begin(); it!=code.end(); ++it) {
		switch ((*it)->opcode()) {
			case OP_GOTO:
			case OP_IF:
			case OP_IFNOT:
			case OP_IFFAIL:
			case OP_IFNOTFAIL:
			case OP_MATCH:
			case OP_MATCHARGS:
			case OP_FORK:
				count_jump_bytes(it);
				break;
			default:
				continue;
		}
	}
}

void measure_function_jumps(AsmFunc &f)
{
	list< jump_data >::iterator jump_it(f.jump.begin());
	for (; jump_it!=f.jump.end(); ++jump_it) {
		uint32_t lbl_idx = label_index(f.label, jump_it->label);
		int16_t jump_count(lbl_idx - jump_it->jump_offset);
		jump_it->ji.setjump(jump_count);
	}

	measure_jump_bytes(f.code);
}

void measure_jumps(ResourceSet &rs)
{
	ResourceSet::iterator it(rs.begin());
	for (; it!=rs.end(); ++it) {
		if (it->first->type == RESOURCE_FUNCTION) {
			AsmFunc *func = dynamic_cast< AsmFunc * >(it->first);
			measure_function_jumps(*func);
		}
	}
}

map< string, uint16_t > CONSTNUM;
map< string, uint16_t > CONSTSTR;
map< string, uint16_t > CONSTKEY;

void init_constant_registers()
{
	CONSTNUM["0"] = REG_CINT(0x0);
	CONSTNUM["1"] = REG_CINT(0x1);
	CONSTNUM["2"] = REG_CINT(0x2);
	CONSTNUM["3"] = REG_CINT(0x3);
	CONSTNUM["4"] = REG_CINT(0x4);
	CONSTNUM["5"] = REG_CINT(0x5);
	CONSTNUM["6"] = REG_CINT(0x6);
	CONSTNUM["7"] = REG_CINT(0x7);
	CONSTNUM["8"] = REG_CINT(0x8);
	CONSTNUM["9"] = REG_CINT(0x9);
	CONSTNUM["10"] = REG_CINT(0xa);
	CONSTNUM["11"] = REG_CINT(0xb);
	CONSTNUM["12"] = REG_CINT(0xc);
	CONSTNUM["13"] = REG_CINT(0xd);
	CONSTNUM["14"] = REG_CINT(0xe);
	CONSTNUM["15"] = REG_CINT(0xf);
	CONSTNUM["0.0"] = REG_FZERO;
	CONSTSTR[""] = REG_EMPTYSTR;
	CONSTSTR["\n"] = REG_NEWLINE;
	CONSTKEY["false"] = REG_FALSE;
	CONSTKEY["true"] = REG_TRUE;
	CONSTKEY["void"] = REG_VOID;
}

/**
 * Write the header in byte packed format
 */
void write_header(ostream &out, const ObjectHeader &h)
{
	ObjectHeader be;
	be.magic[0] = h.magic[0];
	be.magic[1] = h.magic[1];
	be.magic[2] = h.magic[2];
	be.magic[3] = h.magic[3];
	be.qbrt_version = htobe32(h.qbrt_version);
	be.flags = htobe64(h.flags);
	be.name = htobe16(h.name);
	be.version = htobe16(h.version);
	be.iteration = htobe16(h.iteration);
	be.imports = htobe16(h.imports);
	be.source_filename = htobe16(h.source_filename);
	out.write((const char *) &be, ObjectHeader::SIZE);
}

/**
 * Write the resource data in byte packed format
 */
void write_resource_header(ostream &out, const ResourceTableHeader &h)
{
	uint16_t count_plus_1 = h.resource_count + 1;
	out.write((char *) &h.data_size, 4);
	out.write((char *) &count_plus_1, 2);
}


ostream & AsmTypeSpec::serialize(ostream &out) const
{
	out << *name;
	if (this->empty()) {
		return out;
	}
	out << '(';
	AsmTypeSpecList::const_iterator it(args->begin());
	bool first(true);
	for (; it!=args->end(); ++it) {
		if (!first) {
			out << ',';
		} else {
			first = false;
		}
		(*it)->serialize(out);
	}
	out << ')';
	return out;
}

ostream & AsmTypeSpec::pretty(ostream &out) const
{
	out << "typespec:";
	return this->serialize(out);
}

uint32_t AsmTypeSpec::write(ostream &out) const
{
	out.write((const char *) name->index, 2);
	out.write((const char *) fullname.index, 2);
	uint32_t bytes(4);
	if (this->empty()) {
		return bytes;
	}

	AsmTypeSpecList::const_iterator it(args->begin());
	for (; it!=args->end(); ++it) {
		out.write((const char *) (*it)->index, 2);
		bytes += 2;
	}
	return bytes;
}


AsmFunc::AsmFunc(const AsmString &name, const AsmTypeSpec &result)
	: AsmResource(RESOURCE_FUNCTION)
	, stmts(NULL)
	, ctx(NULL)
	, name(name)
	, result_type(result)
	, doc()
	, line_no(0)
	, fcontext(PFC_NULL)
	, argc(0)
	, regc(0)
{}

bool AsmFunc::has_code() const
{
	return !this->code.empty();
}

uint32_t AsmFunc::write(ostream &out) const
{
	out.write((const char *) name.index, 2);
	out.write((const char *) doc.index, 2);
	out.write((const char *) (&line_no), 2);
	if (ctx) {
		out.write((const char *) ctx->index, 2);
	} else {
		uint16_t zero(0);
		out.write((const char *) &zero, 2);
	}
	out.write((const char *) param_types.index, 2);
	out.write((const char *) result_type.index, 2);
	out.put(this->fcontext);
	out.put(argc);
	out.put(regc);
	out.put('\0');
	uint32_t func_size(FunctionHeader::SIZE);

	AsmParamList::const_iterator pit(params.begin());
	for (; pit!=params.end(); ++pit) {
		out.write((const char *) (*pit)->name.index, 2);
		out.write((const char *) (*pit)->type.index, 2);
		func_size += 4;
	}

	codeblock::const_iterator ci(code.begin());
	for (; ci!=code.end(); ++ci) {
		uint8_t isize(write_instruction(out, **ci));
		func_size += isize;
	}
	return func_size;
}

ostream & AsmFunc::pretty(ostream &o) const
{
	o << "function:" << name.value;
	switch (this->fcontext) {
		case PFC_ABSTRACT:
			o << " abstract of ";
			this->ctx->pretty(o);
			break;
		case PFC_DEFAULT:
			o << " default of ";
			this->ctx->pretty(o);
			break;
		case PFC_OVERRIDE:
			o << " binds ";
			this->ctx->pretty(o);
			break;
	}
	if (!param_types.value.empty()) {
		o << " " << param_types.value << " ->";
	}
	o << " " << result_type.fullname.value;
	return o;
}

uint32_t AsmProtocol::write(ostream &out) const
{
	uint16_t num_funcs(0);
	if (num_funcs >= (1 << 10)) {
		cerr << "oh shit that's a lot of functions" << endl;
	}
	uint16_t arg_func_counts((argc << 10) | num_funcs);
	endian_fix(arg_func_counts);

	out.write((const char *) name.index, 2);
	out.write((const char *) doc.index, 2);
	out.write((const char *) &line_no, 2);
	out.write((const char *) &arg_func_counts, 2);

	uint32_t proto_size(ProtocolResource::SIZE);

	list< AsmString * >::const_iterator it(typevar->begin());
	for (; it!=typevar->end(); ++it) {
		out.write((const char *) (*it)->index, 2);
		proto_size += 2;
	}

	return proto_size;
}

uint32_t AsmPolymorph::write(ostream &out) const
{
	uint16_t num_types(type.size());
	uint16_t num_funcs(0);

	out.write((const char *) protocol.index, 2);
	out.write((const char *) doc.index, 2);
	out.write((const char *) &line_no, 2);
	out.write((const char *) &num_types, 2);
	out.write((const char *) &num_funcs, 2);

	uint32_t poly_size(PolymorphResource::HEADER_SIZE);
	list< AsmTypeSpec * >::const_iterator it(type.begin());
	for (; it!=type.end(); ++it) {
		out.write((const char *) (*it)->index, 2);
		poly_size += 2;
	}
	return poly_size;
}

ostream & AsmPolymorph::pretty(std::ostream &o) const
{
	o << "polymorph:" << protocol.module.value << '/'
		<< protocol.symbol.value;
	AsmTypeSpecList::const_iterator it(type.begin());
	for (; it!=type.end(); ++it) {
		o << ' ' << (*it)->fullname.value;
	}
	return o;
}


uint32_t AsmConstruct::write(std::ostream &out) const
{
	uint8_t fld_count(fields.size());

	out.write((const char *) name.index, 2);
	out.write((const char *) doc.index, 2);
	out.write((const char *) filename.index, 2);
	out.write((const char *) &line_no, 2);
	out.write((const char *) datatype.index, 2);
	out.put(fld_count);
	out.put('\0');

	uint32_t size(12);
	AsmParamList::const_iterator it(fields.begin());
	for (; it!=fields.end(); ++it) {
		out.write((const char *) (*it)->name.index, 2);
		out.write((const char *) (*it)->type.index, 2);
		size += 4;
	}

	return size;
}

ostream & AsmConstruct::pretty(ostream &o) const
{
	o << "construct:" << name.value;
}

uint32_t AsmDataType::write(std::ostream &out) const
{
	out.write((const char *) name.index, 2);
	out.write((const char *) doc.index, 2);
	out.write((const char *) filename.index, 2);
	out.write((const char *) &line_no, 2);
	out.put(argc);

	return 9;
}

ostream & AsmDataType::pretty(ostream &o) const
{
	o << "datatype:" << name.value << '/' << (int) argc;
}


void write_resource_data(ostream &out, ResourceIndex &index
		, const ResourceSet &rs)
{
	uint32_t offset(0);
	ResourceSet::const_iterator it(rs.begin());
	for (; it!=rs.end(); ++it) {
		index.push_back(ResourceInfo(offset, it->first->type));
		offset += it->first->write(out);
	}
}

void write_resource_index(ostream &out, const ResourceIndex &index)
{
	ResourceInfo null_resource(0, 0);
	out.write((const char *) &null_resource, ResourceInfo::SIZE);

	ResourceIndex::const_iterator it(index.begin());
	for (; it!=index.end(); ++it) {
		out.write((const char *) &(*it), ResourceInfo::SIZE);
	}
}

ResourceTableHeader write_resource_table(ostream &out, const ResourceSet &rs)
{
	ResourceIndex index;
	uint32_t pre_data(out.tellp());
	write_resource_data(out, index, rs);
	uint32_t post_data(out.tellp());
	write_resource_index(out, index);

	ResourceTableHeader tbl_header;
	tbl_header.data_size = post_data - pre_data;
	tbl_header.resource_count = rs.size();
	return tbl_header;
}

void write_object(ostream &out, ObjectBuilder &obj)
{
	ResourceTableHeader tbl_header;

	write_header(out, obj.header);
	write_resource_header(out, tbl_header);
	tbl_header = write_resource_table(out, obj.rs);

	// and rewrite the header w/ correct offsets & sizes
	out.seekp(0);
	write_header(out, obj.header);
	write_resource_header(out, tbl_header);
}


ostream & operator << (ostream &out, const Token &t)
{
	switch (t.type) {
		case TOKEN_ARG:
			out << "ARG";
			break;
		case TOKEN_CALL:
			out << "call";
			break;
		case TOKEN_CONST:
			out << "const";
			break;
		case TOKEN_END:
			out << "end.";
			break;
		case TOKEN_FORK:
			out << "fork";
			break;
		case TOKEN_INT:
			out << "INT";
			break;
		case TOKEN_LABEL:
			out << "LABEL";
			break;
		case TOKEN_LCONTEXT:
			out << "lcontext";
			break;
		case TOKEN_LCONSTRUCT:
			out << "lconstruct";
			break;
		case TOKEN_LFUNC:
			out << "lfunc";
			break;
		case TOKEN_NEWPROC:
			out << "newproc";
			break;
		case TOKEN_RECV:
			out << "recv";
			break;
		case TOKEN_STR:
			out << "STRING";
			break;
		case TOKEN_REF:
			out << "ref";
			break;
		case TOKEN_REG:
			out << "REG";
			break;
		case TOKEN_RESULT:
			out << "result";
			break;
		case TOKEN_STRACC:
			out << "stracc";
			break;
		case TOKEN_SUBIDX:
			out << "SUBIDX";
			break;
		case TOKEN_SUBNAME:
			out << "SUBNAME";
			break;
		case TOKEN_VAR:
			out << "VAR";
			break;
		default:
			out << t.type;
			break;
	}
	return out;
}

class TokenState
{
public:
	TokenState(int32_t lineno, const std::string &line)
		: lineno(lineno)
		, p(line.begin())
		, end(line.end())
	{}
	bool eol() const { return p == end; }
	bool space() const { return isspace(*p); }
	bool digit() const { return isdigit(*p); }
	char operator * () { return *p; }
	void operator ++ () { ++p; ++col; }
	const int32_t lineno;
	int32_t colno() const { return col; }

private:
	string::const_iterator p;
	string::const_iterator end;
	int32_t col;
};

void print_tokens(ostream &out, const list< vector< Token * > > &file)
{
	cout << "num lines: " << file.size() << endl;
	list< vector< Token * > >::const_iterator line(file.begin());
	for (; line!=file.end(); ++line) {
		cout << line->size() << ":";
		vector< Token * >::const_iterator tok(line->begin());
		for (; tok!=line->end(); ++tok) {
			out << " " << **tok;
		}
		out << endl;
	}
}

struct ClistStatement
{
	ClistStatement(uint16_t dst)
		: dst(dst)
	{}

	uint16_t dst;

	void pretty(ostream &out) const
	{
		out << "clist " << dst;
	}
};

struct ConsStatement
{
	ConsStatement(uint16_t head, uint16_t item)
		: head(head)
		, item(item)
	{}

	uint16_t head;
	uint16_t item;

	void pretty(ostream &out) const
	{
		out << "cons " << head <<" "<< item;
	}
};

/**
 * wtf is this function trying to do?
 * I think it's trying to assign protocol info for functions
 */
void set_function_context(Stmt::List &funcs, uint8_t pfc
		, AsmResource *ctx)
{
	Stmt::List::iterator it(funcs.begin());
	for (; it!=funcs.end(); ++it) {
		(*it)->set_function_context(pfc, ctx);
	}
}

void allocate_registers(Stmt::List &stmts, RegAlloc *rc)
{
	Stmt::List::iterator it(stmts.begin());
	for (; it!=stmts.end(); ++it) {
		(*it)->allocate_registers(rc);
	}
}


struct LoadTypeStatement
{
	LoadTypeStatement(uint16_t dstreg, const std::string &modname
			, const std::string &type)
		: dst(dstreg)
		, modname(modname)
		, type(type)
	{}
	uint16_t dst;
	std::string modname;
	std::string type;

	void pretty(ostream &out) const
	{
		out << "ltype " << dst <<" \""<< modname << "\""
			<<" \""<< type << "\"";
	}
};

struct LoadObjStatement
{
	LoadObjStatement(const std::string &modname)
		: modname(modname)
	{}
	std::string modname;

	void pretty(ostream &out) const
	{
		out << "lobj \""<< modname << "\"";
	}
};

struct StupleStatement
{
	StupleStatement(uint16_t tup, uint16_t idx, uint16_t data)
		: tup(tup)
		, idx(idx)
		, data(data)
	{}

	uint16_t tup;
	uint16_t idx;
	uint16_t data;

	void pretty(ostream &out) const
	{
		out << "stuple " << tup <<" "<< idx <<" "<< data;
	}
};


inline bool valid_register(int32_t r)
{
	// must be a valid number in the uint16_t range
	int32_t rcmp((uint16_t) r);
	return r == rcmp;
}

void print_function(ostream &out, const AsmFunc &func)
{
	out << func.name.value << " -> " << func.stmts->size()
		<< " statements";
}

void asm_instruction(AsmFunc &f, instruction *i)
{
	f.code.push_back(i);
}

Stmt::List * parse(istream &in)
{
	string s;
	int toktype;
	void *parser = ParseAlloc(malloc);

	while (! in.eof()) {
		QBRTBEGIN(QBRTINITIAL);
		getline(in, s);
		yy_scan_string(s.c_str());
		while (toktype=yylex()) {
			Token::s_column += yyleng;
		}

		while (!lexqueue.empty()) {
			const Token *tok = lexqueue.front();
			Token::s_lineno = tok->span_front.lineno;
			Token::s_column = tok->span_front.column;
			lexqueue.pop();
			Parse(parser, tok->type, tok);
		}
		++Token::s_lineno;
		Token::s_column = 1;
	}

	Parse(parser, 0, NULL);
	ParseFree(parser, free);

	Stmt::List *result = parsed_stmts;
	parsed_stmts = NULL;
	return result;
}

void collect_resources(ResourceSet &rs, Stmt::List &stmts)
{
	Stmt::List::iterator it(stmts.begin());
	for (; it!=stmts.end(); ++it) {
		(*it)->collect_resources(rs);
	}
}

void index_resources(ResourceSet &rs)
{
	uint16_t index(1);
	ResourceSet::iterator it(rs.begin());
	for (; it!=rs.end(); ++it) {
		it->second = index++;
	}
}

void print_resources(const ResourceSet &rs)
{
	cout << "collected " << rs.size() << " resources\n";

	ResourceSet::const_iterator it(rs.begin());
	for (; it!=rs.end(); ++it) {
		cout << it->second << "\t";
		it->first->pretty(cout);
		cout << endl;
	}
}

void generate_codeblock(AsmFunc &func, const Stmt::List &stmts)
{
	Stmt::List::const_iterator it(stmts.begin());
	for (; it!=stmts.end(); ++it) {
		(*it)->generate_code(func);
	}
}

void generate_code(ResourceSet &rs)
{
	ResourceSet::iterator it(rs.begin());
	AsmFunc *func;
	for (; it!=rs.end(); ++it) {
		if (it->first->type != RESOURCE_FUNCTION) {
			// no code to generate for non-functions
			continue;
		}
		func = static_cast< AsmFunc * >(it->first);
		if (!func->stmts) {
			continue;
		}
		generate_codeblock(*func, *func->stmts);
	}

	measure_jumps(rs);
}

void init_compiler() {
	init_instruction_sizes();
	init_writers();
	init_constant_registers();
}

bool compile_module(ModuleMap &, const string &module_name);

void load_imported_modules(ModuleMap &modmap, const set< string > &modnames)
{
	set< string >::const_iterator it(modnames.begin());
	map< string, Module * > modules;
	for (; it!=modnames.end(); ++it) {
		if (modmap.find(*it) != modmap.end()) {
			continue;
		}
		Module *mod = read_module(*it);
		if (!mod) {
			compile_module(modmap, *it);
			mod = read_module(*it);
			if (!mod) {
				cerr << "could not load module: "<< (*it) & DIE;
			}
			modmap[*it] = mod;
		}
	}
}

bool compile_module(ModuleMap &modmap, const string &module_name)
{
	string input_name(module_name +".uqb");
	string output_name(module_name +".qb");

	cout << "compile " << input_name << " to " << output_name << endl;
	ifstream fin;
	fin.open(input_name.c_str());
	if (!fin) {
		cerr << "error opening source file: " << input_name << endl;
		return false;
	}

	ObjectBuilder obj(module_name);
	obj.header.qbrt_version = 13;
	obj.header.set_application(1);

	cout << "---\n";
	g_parse_module = module_name;
	Stmt::List *stmts = parse(fin);
	g_parse_module.clear();
	set_function_context(*stmts, FCT_TRADITIONAL, NULL);

	obj.rs.set_module_version(22, "bbd");
	collect_resources(obj.rs, *stmts);
	index_resources(obj.rs);
	print_resources(obj.rs);
	obj.header.name = obj.rs.module_name();
	obj.header.version = obj.rs.module_version;
	obj.header.iteration = obj.rs.module_iteration();
	obj.header.imports = obj.rs.imports_index();

	load_imported_modules(modmap, obj.rs.imported_modules());

	allocate_registers(*stmts, NULL);
	cout << "---\n";

	generate_code(obj.rs);

	ofstream out;
	out.open(output_name.c_str(), ios::binary | ios::out);
	if (!out) {
		cerr << "error opening outputfile: " << output_name << endl;
		return false;
	}
	write_object(out, obj);
	out.close();
	return true;
}

int main(int argc, const char **argv)
{
	if (argc != 2) {
		cerr << "usage: " << argv[0] << " <module>\n";
		return 1;
	}

	init_compiler();
	ModuleMap modules;
	compile_module(modules, argv[1]);

	return 0;
}
