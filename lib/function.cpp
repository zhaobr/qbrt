#include "qbrt/function.h"
#include "qbrt/resourcetype.h"
#include "qbrt/module.h"
#include <cstdlib>

using namespace std;


const char * QbrtFunction::name() const
{
	return fetch_string(mod->resource, header->name_idx);
}

uint32_t QbrtFunction::code_offset() const
{
	return code - mod->resource.data
		+ ResourceTable::DATA_OFFSET;
}

const char * QbrtFunction::protocol_name() const
{
	uint16_t name_idx(0);
	switch (PFC_TYPE(header->fcontext)) {
		case FCT_TRADITIONAL:
			return NULL;
		case FCT_PROTOCOL: {
			const ProtocolResource *proto;
			proto = mod->resource.ptr< const ProtocolResource >(
					header->context_idx);
			name_idx = proto->name_idx;
			break; }
		case FCT_POLYMORPH: {
			const PolymorphResource *poly;
			poly = mod->resource.ptr< const PolymorphResource >(
					header->context_idx);
			const ModSym *protoms;
			protoms = mod->resource.ptr< const ModSym >(
					poly->protocol_idx);
			name_idx = protoms->sym_name;
			break; }
	}
	return fetch_string(mod->resource, name_idx);
}

const char * QbrtFunction::protocol_module() const
{
	uint16_t mod_idx(0);
	switch (PFC_TYPE(header->fcontext)) {
		case FCT_TRADITIONAL:
			return NULL;
		case FCT_PROTOCOL:
			return mod->name.c_str();
		case FCT_POLYMORPH: {
			const PolymorphResource *poly;
			poly = mod->resource.ptr< const PolymorphResource >(
					header->context_idx);
			const ModSym *protoms;
			protoms = mod->resource.ptr< const ModSym >(
					poly->protocol_idx);
			mod_idx = protoms->mod_name;
			break; }
	}
	return fetch_string(mod->resource, mod_idx);
}

const char * CFunction::protocol_module() const
{
	return proto_module.empty() ? NULL : proto_module.c_str();
}

const char * CFunction::protocol_name() const
{
	return proto_name.empty() ? NULL : proto_name.c_str();
}


function_value::function_value(const Function *f)
: func(f)
, argc(f->argc())
, regc(f->regtotal())
{
	regv = (qbrt_value *) malloc(regc * sizeof(qbrt_value));
	new (regv) qbrt_value[regc];
}

void function_value::realloc(uint8_t new_regc)
{
	size_t newsize(new_regc * sizeof(qbrt_value));
	qbrt_value *newreg =
		(qbrt_value *) ::realloc(this->regv, newsize);
	this->regv = newreg;
	for (int i(this->regc); i<new_regc; ++i) {
		new (&this->regv[i]) qbrt_value();
	}
	this->regc = new_regc;
}

void load_function_value_types(ostringstream &out, const function_value &func)
{
	bool first(true);
	for (int i(0); i<func.argc; ++i) {
		if (first) {
			first = false;
		} else {
			out << " -> ";
		}
		qbrt_value::append_type(out, func.value(i));
	}
}

void reassign_func(function_value &funcval, const Function *newfunc)
{
	int new_regc(newfunc->regtotal());
	if (funcval.regc < new_regc) {
		funcval.realloc(new_regc);
	}
	funcval.func = newfunc;
}


Failure::Failure(const std::string type_label, const string &module
		, const char *fname, int pc
		, const char *cfile, int cline)
: debug()
, usage()
, http_code(0)
{
	qbrt_value::hashtag(type, type_label);
	qbrt_value::i(exit_code, -1);
	trace.push_back(FailureEvent());
	FailureEvent &e(trace.back());
	e.set(module, fname, pc, cfile, cline);
}

string Failure::debug_msg() const
{
	return debug.str();
}

qbrt_value & Failure::value(uint8_t i)
{
	switch (i) {
		case 0:
			return type;
		case 1:
			return exit_code;
	}
	cerr << "no Failure value at index: " << i << endl;
	exit(1);
	return *(qbrt_value *) NULL;
}

const qbrt_value & Failure::value(uint8_t i) const
{
	switch (i) {
		case 0:
			return type;
		case 1:
			return exit_code;
	}
	cerr << "no Failure value at index: " << i << endl;
	exit(1);
	return *(const qbrt_value *) NULL;
}

void Failure::trace_up(const string &mod, const string &fname, uint16_t pc)
{
	trace.push_front(FailureEvent());
	FailureEvent &e(trace.front());
	e.up(mod, fname, pc);
}

void Failure::trace_down(const string &mod, const string &fname, uint16_t pc
		, const char *c_file, int c_line)
{
	trace.push_back(FailureEvent());
	FailureEvent &e(trace.back());
	e.down(mod, fname, pc, c_file, c_line);
}

void Failure::write(ostream &out, const Failure &f)
{
	out << "Failure: #" << *f.type.data.hashtag;
	string usage_msg(f.usage_msg());
	if (!usage_msg.empty()) {
		out << endl << usage_msg << endl;
	}
	out << endl;

	Failure::write_trace(out, f.trace);

	string debug_msg(f.debug_msg());
	if (!debug_msg.empty()) {
		out << debug_msg << endl;
	}
}

void Failure::write_trace(ostream &out, const list< FailureEvent > &trace)
{
	list< FailureEvent >::const_iterator it(trace.begin());
	for (; it!=trace.end(); ++it) {
		out << *it << endl;
	}
}


ostream & operator << (ostream &out, const FailureEvent &e)
{
	out << (e.direction <= 0 ? '<' : ' ');
	out << (e.direction >= 0 ? '>' : ' ');
	out << *e.module.data.str << '/';
	out << *e.function.data.str << ':' << e.pc.data.i;
	if (e.direction <= 0) {
		out << ' ' << *e.c_file.data.str << ':' << e.c_lineno.data.i;
	}
	return out;
}
