#include "qbrt/schedule.h"
#include "qbrt/module.h"
#include "io.h"

using namespace std;

#define MAX_EPOLL_EVENTS 16


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

void CodeFrame::backtrace(Failure &f, const CodeFrame *frame)
{
	if (!frame) {
		return;
	}
	const FunctionCall &call(frame->function_call());
	f.trace_up(call.mod->name, call.name(), frame->pc);
	backtrace(f, frame->parent);
}


FunctionCall::FunctionCall(const QbrtFunction &func, qbrt_value_index &vals)
: CodeFrame(CFT_CALL)
, result(NULL)
, regv(vals)
, header(func.header)
, mod(func.mod)
{}

const char * FunctionCall::name() const
{
	return fetch_string(mod->resource, header->name_idx);
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
, thread()
, thread_attr()
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

bool Worker::empty() const
{
	bool empty = !current
		&& (!fresh || fresh->empty())
		&& (!stale || stale->empty());
	return empty;
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

const Module * find_module(Worker &w, const std::string &modname)
{
	if (modname == "./") {
		// if the module name is empty, return the current module
		return current_module(w);
	}

	ModuleMap::const_iterator it(w.module.find(modname));
	if (it != w.module.end()) {
		return it->second;
	}
	it = w.app.module.find(modname);
	if (it != w.app.module.end()) {
		w.module[modname] = it->second;
		return it->second;
	}
	return NULL;
}

const QbrtFunction * find_override(Worker &w, const char *protocol_mod
		, const char *protocol_name, const char *funcname
		, const string &param_types)
{
	ModuleMap::const_iterator it(w.module.begin());
	for (; it!=w.module.end(); ++it) {
		const QbrtFunction *f(it->second->fetch_override(protocol_mod
				, protocol_name, funcname, param_types));
		if (f) {
			return f;
		}
	}
	return NULL;
}

const CFunction * find_c_override(Worker &w, const std::string &protomod
		, const std::string &protoname, const std::string &name
		, const std::string &param_types)
{
	const CFunction *cf;
	ModuleMap::const_iterator it(w.module.begin());
	for (; it != w.module.end(); ++it) {
		cf = fetch_c_override(*it->second, protomod
				, protoname, name, param_types);
		if (cf) {
			return cf;
		}
	}
	return find_c_override(w.app, protomod, protoname, name, param_types);
}

const Function * find_default_function(Worker &w, const Function &func)
{
	if (func.fcontext() != PFC_OVERRIDE) {
		return NULL; // do nothing here
	}

	const char *proto_mod = func.protocol_module();
	const Module *mod(find_module(w, proto_mod));
	if (!mod) {
		return NULL;
	}

	const char *proto_name = func.protocol_name();
	return mod->fetch_protocol_function(proto_name, func.name());
}

const Module * load_module(Worker &w, const string &objname)
{
	ModuleMap::const_iterator it;
	it = w.module.find(objname);
	if (it != w.module.end()) {
		return it->second;
	}
	const Module *mod = find_app_module(w.app, objname);
	if (mod) {
		w.module[objname] = mod;
		return mod;
	}
	mod = read_module(objname);
	w.module[objname] = mod;
	load_module(w.app, mod);
	return mod;
}

static void assign_process(Worker &w, ProcessRoot *proc)
{
	proc->owner = &w;
	w.process[proc->pid] = proc;
	w.stale->push_back(proc->call);
}

void iopush(Worker &w)
{
	// add this io
	StreamIO &io(*w.current->io);
	epoll_event ev;
	ev.events = io.events;
	ev.data.ptr = w.current;
	epoll_ctl(w.epfd, EPOLL_CTL_ADD, io.stream->fd, &ev);
	++w.iocount;
	w.current = NULL;
}

void iopop(Worker &w, CodeFrame *cf)
{
	epoll_ctl(w.epfd, EPOLL_CTL_DEL, cf->io->stream->fd, NULL);
	--w.iocount;
	cf->io_pop();
	cf->cfstate = CFS_READY;
	w.stale->push_back(cf);
}

void iowork(Worker &w)
{
	epoll_event events[MAX_EPOLL_EVENTS];
	int timeout(w.fresh->empty() && w.stale->empty() ? 100 : 0);
	int fdcnt(epoll_wait(w.epfd, events, MAX_EPOLL_EVENTS, timeout));
	if (fdcnt == -1) {
		perror("epoll_wait");
		return;
	}
	for (int i(0); i<fdcnt; ++i) {
		CodeFrame *cf = static_cast< CodeFrame * >(events[i].data.ptr);
		cf->io->handle();
		iopop(w, cf);
	}
}

void execute_instruction(Worker &, const instruction &);

void gotowork(Worker &w)
{
	while (w.app.running) {
		/*
		string ready;
		inspect_call_frame(cerr, *w.task->cframe);
		getline(cin, ready);
		*/
		if (!w.current) {
			// never going to get out of this loop
			// but ok for now. can find a new proc
			// from the application later.
			timespec qtp;
			qtp.tv_sec = 0;
			qtp.tv_nsec = 2000;
			nanosleep(&qtp, NULL);
			sched_yield();
			findtask(w);
			continue;
		}

		const instruction &i(frame_instruction(*w.current));
		execute_instruction(w, i);

		if (w.current->io) {
			iopush(w);
		}
		if (w.iocount > 0) {
			iowork(w);
			if (!w.current) {
				findtask(w);
			}
		}

		switch (w.current->cfstate) {
			case CFS_READY:
				// continue as normal
				break;
			case CFS_IOWAIT:
			case CFS_NEW:
			case CFS_PEERWAIT:
				w.stale->push_back(w.current);
				w.current = NULL;
				break;
			case CFS_FAILED:
			case CFS_COMPLETE:
				w.current->finish_frame(w);
				break;
		}
	}
}

void * launch_worker(void *void_worker)
{
	Worker *w = static_cast< Worker * >(void_worker);
	pthread_detach(w->thread);
	gotowork(*w);
	return NULL;
}


/// Application

Application::Application()
: next_workerid(1)
, pid_count(0)
, running(true)
{
	pthread_spin_init(&application_lock, PTHREAD_PROCESS_PRIVATE);
}

Application::~Application()
{
	pthread_spin_destroy(&application_lock);
}

const Module * find_app_module(Application &app, const string &modname)
{
	ModuleMap::iterator it;
	it = app.module.find(modname);
	if (it == app.module.end()) {
		return NULL;
	}
	return it->second;
}

const Module * load_module(Application &app, const string &modname)
{
	const Module *mod = find_app_module(app, modname);
	if (mod) {
		return mod;
	}
	mod = read_module(modname);
	app.module[modname] = mod;
	return mod;
}

void load_module(Application &app, const Module *mod)
{
	app.module[mod->name] = mod;
}

const CFunction * find_c_override(Application &app, const std::string &protomod
		, const std::string &protoname, const std::string &name
		, const std::string &param_types)
{
	const CFunction *cf;
	ModuleMap::const_iterator it(app.module.begin());
	for (; it != app.module.end(); ++it) {
		cf = fetch_c_override(*it->second, protomod
				, protoname, name, param_types);
		if (cf) {
			return cf;
		}
	}
	return NULL;
}

bool send_msg(Application &app, uint64_t pid, const qbrt_value &src)
{
	ProcessRoot::Map::iterator it(app.recv.find(pid));
	if (it == app.recv.end()) {
		return false;
	}
	it->second->recv.push(qbrt_value::dup(src));
	return true;
}

ProcessRoot * new_process(Application &app, FunctionCall *call)
{
	pthread_spin_lock(&app.application_lock);
	ProcessRoot *proc = new ProcessRoot(++app.pid_count, call);
	call->proc = proc;
	app.newproc[proc->pid] = proc;
	app.recv[proc->pid] = proc;
	pthread_spin_unlock(&app.application_lock);
	return proc;
}

Worker & new_worker(Application &app)
{
	Worker *w = new Worker(app, app.next_workerid++);
	w->module = app.module;
	app.worker[w->id] = w;
	return *w;
}

void cycle_distributor(Application::WorkerMap::iterator &it, Application &app)
{
	++it;
	if (it == app.worker.end()) {
		it = app.worker.begin();
	}
}

void distribute_work(Application::WorkerMap::iterator &it, Application &app)
{
	while (!app.newproc.empty() && it != app.worker.end()) {
		ProcessRoot::Map::iterator proc(app.newproc.begin());
		assign_process(*it->second, proc->second);
		app.newproc.erase(proc);
		cycle_distributor(it, app);
	}
}

void application_loop(Application &app)
{
	timespec qtp;
	qtp.tv_sec = 0;
	qtp.tv_nsec = 1000000;

	Application::WorkerMap::iterator distributor(app.worker.begin());
	Application::WorkerMap::iterator it;
	for (;;) {
		distribute_work(distributor, app);
		bool all_empty(true);
		for (it=app.worker.begin(); it!=app.worker.end(); ++it) {
			if (!it->second->empty()) {
				all_empty = false;
				break;
			}
		}
		if (all_empty) {
			app.running = false;
			break;
		} else {
			nanosleep(&qtp, NULL);
		}
	}
}
