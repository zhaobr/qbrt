#include "qbrt/schedule.h"
#include "qbrt/module.h"

using namespace std;


bool Pipe::empty() const
{
	return data.empty();
}

void Pipe::push(qbrt_value *val)
{
	data.push_back(val);
}

qbrt_value * Pipe::pop()
{
	qbrt_value *val = data.front();
	data.pop_front();
	return val;
}

qbrt_value * get_context(CodeFrame *f, const string &name)
{
	while (f) {
		if (f->frame_context.find(name) == f->frame_context.end()) {
			f = f->parent;
			continue;
		}
		return &f->frame_context[name];
	}
	return NULL;
}

qbrt_value * add_context(CodeFrame *f, const string &name)
{
	qbrt_value *ctx = get_context(f, name);
	if (ctx) {
		return ctx;
	}
	return &f->frame_context[name];
}

void CodeFrame::io_pop()
{
	if (!io) {
		return;
	}
	delete io;
	io = NULL;
}


FunctionCall::FunctionCall(ProcessRoot &proc, function_value &func)
: CodeFrame(proc, CFT_CALL)
, result(proc.result)
, reg_data(func.reg)
, resource(func.func.resource)
, mod(func.func.mod)
, regc(func.num_values())
{}

const char * FunctionCall::name() const
{
	return fetch_string(mod->resource, resource->name_idx);
}

void FunctionCall::finish_frame(Worker &w)
{
	CodeFrame *call = w.current;
	w.current = w.current->parent;
	if (call->fork.empty()) {
		delete call;
	} else {
		w.stale->push_back(call);
	}
}

void ParallelPath::finish_frame(Worker &w)
{
	w.current->parent->fork.erase(w.current);
	if (w.current->fork.empty()) {
		delete w.current;
	} else {
		w.stale->push_back(w.current);
	}
	w.current = NULL;
	findtask(w);
}


Worker::Worker(Application &app, WorkerID id)
: app(app)
, module()
, current(NULL)
, process()
, fresh(new CodeFrame::List())
, stale(new CodeFrame::List())
, drain()
, epfd(0)
, iocount(0)
, id(id)
, next_taskid(0)
, next_pid(0)
{
	epfd = epoll_create(1);
	if (epfd < 0) {
		perror("epoll_create failure");
	}
}

ProcessRoot * new_process(Worker &w)
{
	ProcessRoot *proc = new ProcessRoot(w, ++w.next_pid);
	w.process[proc->pid] = proc;
	return proc;
}

void findtask(Worker &w)
{
	if (w.fresh->empty()) {
		if (w.stale->empty()) {
			// all tasks are waiting on io
			return;
		}
		// out of fresh, swap fresh and stale
		CodeFrame::List *tmp = w.fresh;
		w.fresh = w.stale;
		w.stale = tmp;
	}
	// move the first fresh task to task
	w.current = w.fresh->front();
	w.fresh->pop_front();
}

const Module * find_module(const Worker &w, const std::string &modname)
{
	if (modname.empty()) {
		// if the module name is empty, return the current module
		return current_module(w);
	}

	std::map< std::string, Module * >::const_iterator it(w.module.begin());
	for (; it!=w.module.end(); ++it) {
		if (modname == it->first) {
			return it->second;
		}
	}
	return NULL;
}

Function find_override(Worker &w, const char * protocol_mod
		, const char *protocol_name, const Type &t
		, const char *funcname)
{
	std::map< std::string, Module * >::const_iterator it;
	it = w.module.begin();
	for (; it!=w.module.end(); ++it) {
		Function f(it->second->fetch_override(protocol_mod
					, protocol_name, t, funcname));
		if (f) {
			return f;
		}
	}
	return Function();
}

const ProtocolResource * find_function_protocol(Worker &w, const Function &f)
{
	if (f.resource->fcontext == PFC_NONE) {
		cerr << "No function protocol for no context\n";
		return NULL;
	}

	int fct(PFC_TYPE(f.resource->fcontext));
	const ResourceTable &res(f.mod->resource);
	const ProtocolResource *proto;
	uint16_t ctx(f.resource->context_idx);
	if (fct == FCT_POLYMORPH) {
		const PolymorphResource *poly;
		poly = res.ptr< PolymorphResource >(ctx);
		const ModSym *protoms = res.ptr< ModSym >(poly->protocol_idx);
		const char *proto_modname;
		const char *protosym;
		proto_modname = fetch_string(res, protoms->mod_name);
		protosym = fetch_string(res, protoms->sym_name);
		const Module *proto_mod(find_module(w, proto_modname));
		proto = proto_mod->fetch_protocol(protosym);
		return proto;
	} else if (fct == FCT_PROTOCOL) {
		return res.ptr< ProtocolResource >(ctx);
	}
	cerr << "where'd you find that function context? " << fct << endl;
	return NULL;
}

Function find_overridden_function(Worker &w, const Function &func)
{
	if (func.resource->fcontext != PFC_OVERRIDE) {
		return Function();
	}

	const ResourceTable &res(func.mod->resource);
	const ProtocolResource *proto;

	uint16_t polymorph_index(func.resource->context_idx);
	const PolymorphResource *poly;
	poly = res.ptr< PolymorphResource >(polymorph_index);

	const ModSym *protoms = res.ptr< ModSym >(poly->protocol_idx);
	const char *proto_modname;
	const char *protosym;
	proto_modname = fetch_string(res, protoms->mod_name);
	protosym = fetch_string(res, protoms->sym_name);

	const Module *mod(find_module(w, proto_modname));
	if (!mod) {
		return Function();
	}
	return mod->fetch_protocol_function(protosym, func.name());
}

const Module * load_module(Worker &w, const string &objname)
{
	Module *mod = load_module(objname);
	w.module[objname] = mod;
	return mod;
}

void load_module(Worker &w, const string &modname, Module *mod)
{
	w.module[modname] = mod;
}

void * launch_worker(void *void_worker)
{
	Worker *w = static_cast< Worker * >(void_worker);
	pthread_detach(w->thread);
	gotowork(*w);
	return NULL;
}

Worker & new_worker(Application &app)
{
	Worker *w = new Worker(app, app.next_workerid++);
	app.worker[w->id] = w;
	return *w;
}
