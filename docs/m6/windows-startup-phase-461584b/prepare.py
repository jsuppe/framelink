"""Generate a diagnostic patch from an exact git object; never edit the checkout."""
from pathlib import Path
import difflib, hashlib, json, subprocess

root = Path(__file__).parent
repo = Path('<linux-home>/dev/framelink-m6')
revision = '461584bbf5a6b8b70cec1578d69d35cb4b1ea2ff'
name = 'src/framelink_win.cpp'
old = subprocess.check_output(['git','show',f'{revision}:{name}'],cwd=repo,text=True)
instrument = r'''
// DIAGNOSTIC ONLY. Fixed storage; no allocation, file I/O or locking in a phase.
// Export completed records only at normal DLL/CRT teardown, after media work.
// Forced termination can lose this evidence; absence is not a zero-duration open.
namespace startup_phase_diag {
struct Stamp { const char* name=nullptr; LONGLONG wall=0; ULONGLONG kernel=0,user=0,steady=0,gst=0; bool cpu=false; };
struct Record { DWORD tid=0; unsigned size=0; bool done=false,success=false; Stamp stamps[24]{}; };
struct Store {
    Record records[64]{};
    std::atomic<unsigned> used{0};
    LARGE_INTEGER frequency{};
    char directory[MAX_PATH]{};
    bool enabled=false;
    using GstTimestamp = ULONGLONG (__cdecl*)();
    GstTimestamp gstTimestamp=nullptr;
    Store() {
        const DWORD n=GetEnvironmentVariableA("FRAMELINK_SETUP_PHASE_DIR",directory,MAX_PATH);
        enabled=n>0&&n<MAX_PATH&&QueryPerformanceFrequency(&frequency);
        // Optional bridge to the SAME timestamp API used by media-trace rows.
        // Query already loaded modules only; never load/link a GStreamer DLL.
        HMODULE module=GetModuleHandleA("gstreamer-1.0-0.dll");
        if(!module)module=GetModuleHandleA("libgstreamer-1.0-0.dll");
        if(module)gstTimestamp=reinterpret_cast<GstTimestamp>(GetProcAddress(module,"gst_util_get_timestamp"));
    }
    ~Store() {
        if(!enabled)return;
        // Diagnostic normal-exit dump only: no worker, join or GPU calls here.
        char path[MAX_PATH+64]{};
        std::snprintf(path,sizeof path,"%s/open-phases-%lu.csv",directory,GetCurrentProcessId());
        FILE* f=std::fopen(path,"w");if(!f)return;
        std::fprintf(f,"record,pid,tid,success,phase,qpc,qpc_hz,steady_ns,gst_ns,kernel_100ns,user_100ns,cpu_valid,attempts,capacity\n");
        const auto count=used.load();
        for(unsigned i=0;i<count&&i<64;++i){const auto&r=records[i];if(!r.done)continue;
            for(unsigned j=0;j<r.size;++j){const auto&s=r.stamps[j];
                std::fprintf(f,"%u,%lu,%lu,%u,%s,%lld,%lld,%llu,%llu,%llu,%llu,%u,%u,64\n",i,GetCurrentProcessId(),r.tid,
                    unsigned(r.success),s.name,s.wall,frequency.QuadPart,s.steady,s.gst,s.kernel,s.user,unsigned(s.cpu),count);
            }
        }
        std::fclose(f);
    }
};
static Store store;
static ULONGLONG ticks(FILETIME value){return (ULONGLONG(value.dwHighDateTime)<<32)|value.dwLowDateTime;}
struct Open {
    Record* record=nullptr;
    explicit Open(bool legacy) {
        if(!legacy||!store.enabled)return;
        const auto i=store.used.fetch_add(1);if(i>=64)return;
        record=&store.records[i];record->tid=GetCurrentThreadId();mark("enter");
    }
    void mark(const char* name) {
        if(!record||record->size>=24)return;
        auto&s=record->stamps[record->size++];s.name=name;
        LARGE_INTEGER now{};QueryPerformanceCounter(&now);s.wall=now.QuadPart;s.steady=fltrace::clockNs();
        if(store.gstTimestamp)s.gst=store.gstTimestamp();
        FILETIME created{},ended{},kernel{},user{};
        s.cpu=GetThreadTimes(GetCurrentThread(),&created,&ended,&kernel,&user)!=FALSE;
        if(s.cpu){s.kernel=ticks(kernel);s.user=ticks(user);}
    }
    void success(){if(record)record->success=true;}
    ~Open(){mark("return");if(record)record->done=true;}
};
}
'''
anchor = 'static flResult openProducer(const char* name, flChannel** out, SetupDeadline* setup) {'
assert old.count(anchor)==1
new = old.replace(anchor, instrument+'\n'+anchor+'\n    startup_phase_diag::Open phase(setup==nullptr);',1)
# stdio added explicitly rather than depending on transitive trace headers.
new = new.replace('#include <atomic>', '#include <atomic>\n#include <cstdio>',1)
start = new.index(anchor)
end = new.index('\nFL_API flResult flOpenProducer(',start)
body = new[start:end]
replacements = [
 ('    ch->link = fl::Channel::connect(name, connectTimeout, &busy);',
  '    phase.mark("connect_begin");\n    ch->link = fl::Channel::connect(name, connectTimeout, &busy);\n    phase.mark("connect_end");'),
 ('    auto helloMsg = ch->link.recv();',
  '    phase.mark("hello_receive_begin");\n    auto helloMsg = ch->link.recv();\n    phase.mark("hello_receive_end");'),
 ('    if (!ch->link.send(fl::MsgType::Hello, mine)) {',
  '    phase.mark("hello_send_begin");\n    if (!ch->link.send(fl::MsgType::Hello, mine)) {'),
 ('    // Open on the consumer\'s adapter.',
  '    phase.mark("hello_send_end");\n    // Open on the consumer\'s adapter.'),
 ('    ch->device = createDevice(srv->adapterLUID, srv->luidValid != 0, &adapterMissing);',
  '    phase.mark("device_create_begin");\n    ch->device = createDevice(srv->adapterLUID, srv->luidValid != 0, &adapterMissing);\n    phase.mark("device_create_end");'),
 ('    if (!ch->device || !finishDeviceSetup(ch)) {',
  '    phase.mark("device_setup_begin");\n    if (!ch->device || !finishDeviceSetup(ch)) {'),
 ('    memcpy(ch->info.adapterLUID, srv->adapterLUID, 8);',
  '    phase.mark("device_setup_end");\n    memcpy(ch->info.adapterLUID, srv->adapterLUID, 8);'),
 ('    auto poolMsg = ch->link.recv();',
  '    phase.mark("ring_receive_begin");\n    auto poolMsg = ch->link.recv();\n    phase.mark("ring_receive_end");'),
 ('    auto imported=importRing(ch,pd);',
  '    phase.mark("import_begin");\n    auto imported=importRing(ch,pd);\n    phase.mark("import_end");'),
 ('    *out = ch;', '    *out = ch;\n    phase.success();'),
]
for before,after in replacements:
    assert body.count(before)==1, before
    body=body.replace(before,after,1)
new=new[:start]+body+new[end:]
patch=''.join(difflib.unified_diff(old.splitlines(True),new.splitlines(True),fromfile='a/'+name,tofile='b/'+name))
(root/'startup-phases.patch').write_text(patch)
(root/'framelink_win.diagnostic.cpp').write_text(new)
tracked = ['src/framelink_win.cpp','test/m5_external_pipeline.py','test/m5_capture.py']
manifest={'revision':revision,'status':'prepared, not built or run','patch_sha256':hashlib.sha256(patch.encode()).hexdigest(),
          'diagnostic_source_sha256':hashlib.sha256(new.encode()).hexdigest(),'base_files':{}}
for path in tracked:
    data=subprocess.check_output(['git','show',f'{revision}:{path}'],cwd=repo)
    manifest['base_files'][path]=hashlib.sha256(data).hexdigest()
(root/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps(manifest,indent=2))
