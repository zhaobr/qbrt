#include "qbrt/core.h"
#include "qbrt/module.h"
#include "qbrt/function.h"
#include "qbrt/type.h"
#include <string.h>
#include <stdlib.h>
#include <iostream>

using namespace std;

bool operator < (const PolymorphArg &a, const PolymorphArg &b)
{
	// this is totally not the right thing
	return a.module < b.module;
}

uint32_t ResourceTable::offset(uint16_t i) const
{
	const ResourceInfo *info;
	info = (const ResourceInfo *) (index + i * ResourceInfo::SIZE);
	return info->offset + ResourceTable::DATA_OFFSET;
}

uint32_t ResourceTable::size(uint16_t i) const
{
	const ResourceInfo *info1;
	info1 = (const ResourceInfo *) (index + i * ResourceInfo::SIZE);
	if (i + 1 >= resource_count) {
		return data_size - info1->offset;
	}
	const ResourceInfo *info2;
	info2 = (const ResourceInfo *)
		(index + (i + 1) * ResourceInfo::SIZE);
	return info2->offset - info1->offset;
}

const char * Function::name() const
{
	return fetch_string(mod->resource, resource->name_idx);
}

uint32_t Function::code_offset() const
{
	return resource->code - mod->resource.data
		+ ResourceTable::DATA_OFFSET;
}

void add_c_function(Module &mod, const std::string &name, c_function f)
{
	mod.cfunction[name] = f;
}

Function Module::fetch_function(const std::string &name) const
{
	const ResourceTable &tbl(resource);
	const FunctionResource *f;
	const char *fname;
	for (uint16_t i(0); i<tbl.resource_count; ++i) {
		if (tbl.type(i) != RESOURCE_FUNCTION) {
			continue;
		}
		tbl.ptr(f, i);
		fname = fetch_string(tbl, f->name_idx);
		if (name != fname) {
			continue;
		}
		return Function(f, this);
	}
	return Function();
}


const Type * Module::fetch_struct(const std::string &type) const
{
	const StructResource *s;
	s = (const StructResource *) NULL; // fetch_resource(type);
	if (!s) {
		return NULL;
	}
	const char *structname = fetch_string(resource, s->name_id);
	return new Type(name, structname, s->field_count);
}

qbrt_value * find_global(Module &mod, const char *name)
{
	map< string, qbrt_value >::iterator it;
	it = mod.globals.find(name);
	if (it == mod.globals.end()) {
		return NULL;
	}
	return &it->second;
}

struct ProtocolSearch
{
	typedef ProtocolResource resource_t;
	static const uint16_t resource_type_id = RESOURCE_PROTOCOL;

	int compare(const ResourceTable &tbl, uint16_t i) const
	{
		const ProtocolResource *proto = tbl.ptr< ProtocolResource >(i);
		const char *pname = fetch_string(tbl, proto->name_idx);
		if (name < pname) {
			return -1;
		}
		if (name > pname) {
			return 1;
		}
		return 0;
	}

	const std::string name;
	ProtocolSearch(const std::string &name)
		: name(name)
	{}
	~ProtocolSearch() {}
};

const ProtocolResource * Module::fetch_protocol(const std::string &name) const
{
	return resource.find(ProtocolSearch(name));
}

struct ProtocolFunctionSearch
{
	typedef FunctionResource resource_t;
	static const uint16_t resource_type_id = RESOURCE_FUNCTION;

	/**
	 * Compare protocol & function to the function at i
	 */
	int compare(const ResourceTable &tbl, uint16_t i) const
	{
		const FunctionResource *f = tbl.ptr< FunctionResource >(i);
		uint8_t fct(PFC_TYPE(f->fcontext));
		if (FCT_PROTOCOL < fct) {
			return -1;
		}
		if (FCT_PROTOCOL > fct) {
			return 1;
		}

		const ProtocolResource *proto;
		proto = tbl.ptr< ProtocolResource >(f->context_idx);
		const char *pname = fetch_string(tbl, proto->name_idx);
		if (protocol < pname) {
			return -1;
		}
		if (protocol > pname) {
			return 1;
		}

		const char *fname = fetch_string(tbl, f->name_idx);
		if (function < fname) {
			return -1;
		}
		if (function > fname) {
			return 1;
		}
		return 0;
	}

	const std::string protocol;
	const std::string function;
	ProtocolFunctionSearch(const std::string &p, const std::string &f)
		: protocol(p)
		, function(f)
	{}
	~ProtocolFunctionSearch() {}
};

struct PolymorphFunctionSearch
{
	typedef FunctionResource resource_t;
	static const uint16_t resource_type_id = RESOURCE_FUNCTION;

	/**
	 * Compare protocol & function to the function at i
	 */
	int compare(const ResourceTable &tbl, uint16_t i) const
	{
		const FunctionResource *f = tbl.ptr< FunctionResource >(i);
		if (PFC_OVERRIDE < f->fcontext) {
			cerr << "wtf is this fcontext? " << f->fcontext
				<< endl;
			return 0;
		}
		if (PFC_OVERRIDE > f->fcontext) {
			return 1;
		}

		const PolymorphResource *poly;
		poly = tbl.ptr< PolymorphResource >(f->context_idx);
		const ModSym &protoms(fetch_modsym(tbl, poly->protocol_idx));
		// compare protocol module
		const char *other_mod = fetch_string(tbl, protoms.mod_name);
		if (this->proto_mod < other_mod) {
			return -1;
		}
		if (this->proto_mod > other_mod) {
			return 1;
		}
		// compare protocol symbol
		const char *other_sym = fetch_string(tbl, protoms.sym_name);
		if (this->proto_name < other_sym) {
			return -1;
		}
		if (this->proto_name > other_sym) {
			return 1;
		}

		if (poly->type_count > 1) {
			return -1;
		}
		if (poly->type_count < 1) {
			return 1;
		}

		const ModSym &polytype(fetch_modsym(tbl, poly->type[0]));
		const char *polytype_mod = fetch_string(tbl, polytype.mod_name);
		if (type.module < polytype_mod) {
			return -1;
		}
		if (type.module > polytype_mod) {
			return 1;
		}
		const char *polytype_name;
		polytype_name = fetch_string(tbl, polytype.sym_name);
		if (type.name < polytype_name) {
			return -1;
		}
		if (type.name > polytype_name) {
			return 1;
		}

		const char *fname = fetch_string(tbl, f->name_idx);
		if (function < fname) {
			return -1;
		}
		if (function > fname) {
			return 1;
		}
		return 0;
	}

	const std::string &proto_mod;
	const std::string &proto_name;
	const Type &type;
	const std::string &function;

	PolymorphFunctionSearch(const std::string &pmod
			, const std::string &pname, const std::string &f
			, const Type &type)
		: proto_mod(pmod)
		, proto_name(pname)
		, type(type)
		, function(f)
	{}
	~PolymorphFunctionSearch() {}
};

Function Module::fetch_protocol_function(const std::string &protoname
		, const std::string &fname) const
{
	const FunctionResource *f;
	f = resource.find(ProtocolFunctionSearch(protoname, fname));
	if (!f) {
		return Function();
	}
	return Function(f, this);
}

Function Module::fetch_override(const std::string &protomod
		, const std::string &protoname, const Type &type
		, const std::string &fname) const
{
	const FunctionResource *f;
	PolymorphFunctionSearch query(protomod, protoname, fname, type);
	f = resource.find(query);
	if (!f) {
		return Function();
	}
	return Function(f, this);
}

bool open_qb(ifstream &objstr, const std::string &objname)
		// , const Version &min, const Version &max);
{
	string filename(objname +".qb");
	objstr.open(filename.c_str(), ios::in | ios::binary);
	if (!objstr) {
		cerr << "failed to open object: " << objname << endl;
		return false;
	}
	return true;
}

void read_header(ObjectHeader &h, istream &input)
{
	input.read(h.magic, 4);
	input.read((char *) &h.qbrt_version, 4);
	input.read((char *) &h.flags.raw, 8);
	input.read(h.library_name, 24);
	input.read((char *) &h.library_version, 2);
	input.read(h.library_iteration, 6);
}

void read_resource_table(ResourceTable &tbl, istream &input)
{
	input.read((char *) &tbl.data_size, 4);
	input.read((char *) &tbl.resource_count, 2);
	uint32_t index_size(tbl.resource_count * ResourceInfo::SIZE);

	uint8_t *data = new uint8_t[tbl.data_size];
	uint8_t *index = new uint8_t[index_size];

	input.read((char *) data, tbl.data_size);
	input.read((char *) index, index_size);

	tbl.data = data;
	tbl.index = index;
}

Module * load_module(const string &objname)
{
	ifstream in;
	if (!open_qb(in, objname)) {
		return NULL;
	}
	Module *mod = new Module(objname);
	read_header(mod->header, in);
	if (mod->header.library_name != objname) {
		cerr << "module name mismatch: " << mod->header.library_name
			<< " != " << objname << endl;
		return NULL;
	}
	read_resource_table(mod->resource, in);
	in.close();

	return mod;
}