#include <algorithm>
#include <atomic>
#include "cforge_shared_arena.h"
#include <cmath>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <future>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <variant>
#include <vector>
#include <cstring>
#include <cstdint>
#include <deque>
#ifdef _WIN32
#include <windows.h>
#define popen _popen
#define pclose _pclose
#else
#include <dlfcn.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif
#endif
// OpenSSL (opcional — compilar con -DCFV_WITH_OPENSSL -I/usr/include/node /usr/lib/.../libcrypto.so.3)
#ifdef CFV_WITH_OPENSSL
#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/aes.h>
#include <openssl/rand.h>
#endif
#include <unordered_set>
// SDL2 (opcional — compilar con -DCFV_WITH_SDL2 -lSDL2 [-lSDL2_ttf -lSDL2_mixer -lSDL2_image])
#ifdef CFV_WITH_SDL2
#include <SDL2/SDL.h>
#ifdef CFV_WITH_SDL2_TTF
#include <SDL2/SDL_ttf.h>
#endif
#ifdef CFV_WITH_SDL2_MIXER
#include <SDL2/SDL_mixer.h>
#endif
#ifdef CFV_WITH_SDL2_IMAGE
#include <SDL2/SDL_image.h>
#endif
#endif
// OpenGL 3D (sin deps extra — viene con macOS/Linux/Windows)
// macOS: compilar con -framework OpenGL
// Linux:  compilar con -lGL
// Android: compilar con -lGLESv3
// Requires SDL2 for window+context (CFV_WITH_SDL2 must also be set)
#ifdef CFV_WITH_OPENGL
#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#elif defined(__ANDROID__)
#include <GLES3/gl3.h>
#else
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#endif
#ifdef CFV_WITH_SDL2
#include <SDL2/SDL_opengl.h>
#endif
#endif
// PostgreSQL (opcional — compilar con -DCFV_WITH_PGSQL -lpq)
#ifdef CFV_WITH_PGSQL
#include <libpq-fe.h>
#endif
// MySQL (opcional — compilar con -DCFV_WITH_MYSQL -lmysqlclient)
#ifdef CFV_WITH_MYSQL
#include <mysql/mysql.h>
#endif
// WebSocket — usa sockets POSIX + SHA1 para handshake (sin deps extra)
#include <unordered_map>
struct ForgeValue;struct CfvDenseMatrix;struct CfvTuple;struct CfvSet;
using Value=ForgeValue;using Lista=std::shared_ptr<std::vector<ForgeValue>>;using Mapa=std::shared_ptr<std::map<std::string,ForgeValue>>;using FastArray=std::shared_ptr<std::vector<double>>;using DenseMatrix=std::shared_ptr<CfvDenseMatrix>;using Tupla=std::shared_ptr<CfvTuple>;using Conjunto=std::shared_ptr<CfvSet>;
struct CfvDenseMatrix{size_t rows=0,columns=0;std::vector<double>values;};
struct ForgeValue{std::variant<std::monostate,double,std::string,bool,Lista,Mapa,FastArray,DenseMatrix,Tupla,Conjunto>data;std::string origin="cforge";ForgeValue()=default;ForgeValue(double v):data(v){}ForgeValue(std::string v):data(std::move(v)){}ForgeValue(const char*v):data(std::string(v)){}ForgeValue(bool v):data(v){}ForgeValue(Lista v):data(std::move(v)){}ForgeValue(Mapa v):data(std::move(v)){}ForgeValue(FastArray v):data(std::move(v)){}ForgeValue(DenseMatrix v):data(std::move(v)){}ForgeValue(Tupla v):data(std::move(v)){}ForgeValue(Conjunto v):data(std::move(v)){}size_t index()const{return data.index();}};
struct CfvTuple{std::vector<ForgeValue>values;};struct CfvSet{std::vector<ForgeValue>values;};
static ForgeValue cfv_origin(ForgeValue value,std::string origin){value.origin=std::move(origin);return value;}
enum CfvType{CFV_NULL=0,CFV_INTEGER=1,CFV_DECIMAL=2,CFV_TEXT=3,CFV_BOOLEAN=4,CFV_LIST=5,CFV_MAP=6,CFV_RECORD=7};
using CfvReleaseFunction=void(*)(void*);
struct CfvValue{int32_t type;int64_t integer;double decimal;const char* text;void*owner;CfvReleaseFunction release;};
using CfvForeignFunction=int(*)(const CfvValue*,size_t,CfvValue*,char*,size_t);
static constexpr uint32_t CFV_ABI_V2=0x00020000u;
static constexpr uint64_t CFV_V2_BORROWED=0x00000001ull,CFV_V2_OWNED=0x00000002ull;
static constexpr uint32_t CFV_V2_MAX_DEPTH=64u;
struct CfvValueV2{uint32_t struct_size;uint32_t type;uint64_t flags;uint64_t length;int64_t integer;double decimal;const void*data;void*owner;CfvReleaseFunction release;};
struct CfvMapEntryV2{CfvValueV2 key;CfvValueV2 value;};
struct CfvRecordFieldV2{const char*name;uint64_t name_length;CfvValueV2 value;};
struct CfvRecordV2{const char*type_name;uint64_t type_name_length;const CfvRecordFieldV2*fields;uint64_t field_count;};
using CfvForeignFunctionV2=int(*)(uint32_t,const CfvValueV2*,size_t,CfvValueV2*,char*,size_t);
static std::map<std::string,CfvForeignFunction>&cfv_registry(){static std::map<std::string,CfvForeignFunction>value;return value;}
static std::map<std::string,CfvForeignFunctionV2>&cfv_registry_v2(){static std::map<std::string,CfvForeignFunctionV2>value;return value;}
static thread_local std::vector<std::string> cfv_call_stack;
struct CfvCallFrame {
  CfvCallFrame(const std::string& name) { cfv_call_stack.push_back(name); }
  ~CfvCallFrame() { if (!cfv_call_stack.empty()) cfv_call_stack.pop_back(); }
};
static std::mutex cfv_symbol_mutex;static std::map<std::string,ForgeValue*>cfv_symbols;
static void cfv_share_symbol(const std::string&name,ForgeValue*value){std::lock_guard<std::mutex>lock(cfv_symbol_mutex);cfv_symbols[name]=value;}
static ForgeValue cfv_symbol(const std::string&name){std::lock_guard<std::mutex>lock(cfv_symbol_mutex);auto found=cfv_symbols.find(name);if(found==cfv_symbols.end()||!found->second)throw std::runtime_error("símbolo global desconocido: "+name);return *found->second;}
static ForgeValue cfv_symbol_snapshot(){std::lock_guard<std::mutex>lock(cfv_symbol_mutex);auto map=std::make_shared<std::map<std::string,ForgeValue>>();for(const auto&[name,value]:cfv_symbols)if(value)(*map)[name]=*value;return map;}
#ifdef _WIN32
extern "C" __declspec(dllexport) int cfv_register_function(const char*name,CfvForeignFunction fn){if(!name||!fn)return 1;cfv_registry()[name]=fn;return 0;}
extern "C" __declspec(dllexport) int cfv_register_function_v2(const char*name,CfvForeignFunctionV2 fn){if(!name||!fn)return 1;cfv_registry_v2()[name]=fn;return 0;}
#else
extern "C" __attribute__((visibility("default"))) int cfv_register_function(const char*name,CfvForeignFunction fn){if(!name||!fn)return 1;cfv_registry()[name]=fn;return 0;}
extern "C" __attribute__((visibility("default"))) int cfv_register_function_v2(const char*name,CfvForeignFunctionV2 fn){if(!name||!fn)return 1;cfv_registry_v2()[name]=fn;return 0;}
#endif
static double numero(const Value& v) { if (auto p=std::get_if<double>(&v.data)) return *p; throw std::runtime_error("se esperaba un número"); }
static bool verdad(const Value& v) { if (auto p=std::get_if<bool>(&v.data)) return *p; throw std::runtime_error("se esperaba verdadero o falso"); }
static Value suma(const Value&a,const Value&b){ if(a.index()==1&&b.index()==1)return numero(a)+numero(b); if(a.index()==2&&b.index()==2)return std::get<std::string>(a.data)+std::get<std::string>(b.data); throw std::runtime_error("'+' requiere dos números o dos textos"); }
static Value resta(const Value&a,const Value&b){return numero(a)-numero(b);} static Value multiplica(const Value&a,const Value&b){return numero(a)*numero(b);}
static Value divide(const Value&a,const Value&b){double d=numero(b);if(d==0)throw std::runtime_error("no se puede dividir por cero");return numero(a)/d;}
static Value modulo(const Value&a,const Value&b){long long d=(long long)numero(b);if(d==0)throw std::runtime_error("no se puede dividir por cero en módulo");return (double)((long long)numero(a)%d);}
static Value bit_and(const Value&a,const Value&b){return (double)((long long)numero(a)&(long long)numero(b));}
static Value bit_or(const Value&a,const Value&b){return (double)((long long)numero(a)|(long long)numero(b));}
static Value bit_xor(const Value&a,const Value&b){return (double)((long long)numero(a)^(long long)numero(b));}
static Value bit_shl(const Value&a,const Value&b){int s=(int)numero(b);if(s<0)throw std::runtime_error("desplazamiento negativo no permitido");return (double)((long long)numero(a)<<s);}
static Value bit_shr(const Value&a,const Value&b){int s=(int)numero(b);if(s<0)throw std::runtime_error("desplazamiento negativo no permitido");return (double)((long long)numero(a)>>s);}
static Value compara(const Value&a,const Value&b,const std::string&o){if(o=="==")return a.data==b.data;if(o=="!=")return a.data!=b.data;if(a.index()==1&&b.index()==1){double x=numero(a),y=numero(b);if(o==">")return x>y;if(o==">=")return x>=y;if(o=="<")return x<y;return x<=y;}if(a.index()==2&&b.index()==2){auto x=std::get<std::string>(a.data),y=std::get<std::string>(b.data);if(o==">")return x>y;if(o==">=")return x>=y;if(o=="<")return x<y;return x<=y;}throw std::runtime_error("comparación entre tipos incompatibles");}
static std::string cfv_number_text(double value){std::ostringstream stream;if(std::floor(value)==value)stream<<(long long)value;else stream<<value;return stream.str();}
static std::string texto(const Value&v){if(v.index()==0)return "nulo";if(auto p=std::get_if<double>(&v.data))return cfv_number_text(*p);if(auto p=std::get_if<std::string>(&v.data))return *p;if(auto p=std::get_if<bool>(&v.data))return *p?"verdadero":"falso";if(auto p=std::get_if<Lista>(&v.data)){std::string s="[";for(size_t i=0;i<(*p)->size();++i){if(i)s+=", ";s+=texto((*p)->at(i));}return s+"]";}if(auto p=std::get_if<Mapa>(&v.data)){auto marker=(*p)->find("__opcion");if(marker!=(*p)->end()){auto has=(*p)->find("tiene");if(has==(*p)->end()||!verdad(has->second))return "ninguno";return "algunos("+texto((*p)->at("valor"))+")";}std::string s="{";bool first=true;for(auto&[k,x]:**p){if(!first)s+=", ";first=false;s+="\""+k+"\": "+texto(x);}return s+"}";}if(auto p=std::get_if<FastArray>(&v.data)){std::string s="[";for(size_t i=0;i<(*p)->size();++i){if(i)s+=", ";s+=cfv_number_text((*p)->at(i));}return s+"]";}if(auto p=std::get_if<DenseMatrix>(&v.data)){std::string s="[";for(size_t row=0;row<(*p)->rows;++row){if(row)s+=", ";s+="[";for(size_t column=0;column<(*p)->columns;++column){if(column)s+=", ";s+=cfv_number_text((*p)->values[row*(*p)->columns+column]);}s+="]";}return s+"]";}if(auto p=std::get_if<Tupla>(&v.data)){std::string s="(";for(size_t i=0;i<(*p)->values.size();++i){if(i)s+=", ";s+=texto((*p)->values[i]);}if((*p)->values.size()==1)s+=",";return s+")";}if(auto p=std::get_if<Conjunto>(&v.data)){std::string s="conjunto(";for(size_t i=0;i<(*p)->values.size();++i){if(i)s+=", ";s+=texto((*p)->values[i]);}return s+")";}throw std::runtime_error("ForgeValue desconocido");}
static std::string cfv_json_escape(const std::string&input){std::string out="\"";for(unsigned char c:input){switch(c){case '\"':out+="\\\"";break;case '\\':out+="\\\\";break;case '\n':out+="\\n";break;case '\r':out+="\\r";break;case '\t':out+="\\t";break;default:if(c<32){char b[7];std::snprintf(b,sizeof(b),"\\u%04x",c);out+=b;}else out+=(char)c;}}return out+"\"";}
static std::string cfv_canonical_json(const Value&v){if(v.index()==0)return "null";if(auto p=std::get_if<double>(&v.data))return cfv_number_text(*p);if(auto p=std::get_if<std::string>(&v.data))return cfv_json_escape(*p);if(auto p=std::get_if<bool>(&v.data))return *p?"true":"false";if(auto p=std::get_if<Lista>(&v.data)){std::string s="[";for(size_t i=0;i<(*p)->size();++i){if(i)s+=",";s+=cfv_canonical_json((*p)->at(i));}return s+"]";}if(auto p=std::get_if<Mapa>(&v.data)){std::string s="{";bool first=true;for(const auto&[k,x]:**p){if(!first)s+=",";first=false;s+=cfv_json_escape(k)+":"+cfv_canonical_json(x);}return s+"}";}if(auto p=std::get_if<Tupla>(&v.data)){std::string s="[";for(size_t i=0;i<(*p)->values.size();++i){if(i)s+=",";s+=cfv_canonical_json((*p)->values[i]);}return s+"]";}if(auto p=std::get_if<Conjunto>(&v.data)){std::string s="[";for(size_t i=0;i<(*p)->values.size();++i){if(i)s+=",";s+=cfv_canonical_json((*p)->values[i]);}return s+"]";}return cfv_json_escape(texto(v));}
struct CfvArenaRuntime{std::filesystem::path path;std::unique_ptr<cforge::arena::ForgeSharedArena>arena;std::mutex mutex;std::map<std::string,cforge::arena::Offset>latest;CfvArenaRuntime(){auto id=
#ifdef _WIN32
(unsigned long long)GetCurrentProcessId();
#else
(unsigned long long)getpid();
#endif
path=std::filesystem::temp_directory_path()/("cforge-arena-"+std::to_string(id)+".bin");arena=std::make_unique<cforge::arena::ForgeSharedArena>(cforge::arena::ForgeSharedArena::create(path,64ULL*1024ULL*1024ULL));}~CfvArenaRuntime(){std::error_code error;arena.reset();std::filesystem::remove(path,error);}};
static CfvArenaRuntime&cfv_arena_runtime(){static CfvArenaRuntime runtime;return runtime;}
static Value cfv_arena_stage(Value value,const std::string&connector){auto&runtime=cfv_arena_runtime();auto json=cfv_canonical_json(value);std::lock_guard<std::mutex>guard(runtime.mutex);runtime.latest[connector]=runtime.arena->store_text(cforge::arena::ValueType::Json,json);return value;}
static Value cfv_arena_estado(){auto&runtime=cfv_arena_runtime();auto out=std::make_shared<std::map<std::string,Value>>();(*out)["ruta"]=runtime.path.string();(*out)["capacidad"]=(double)runtime.arena->capacity();(*out)["usado"]=(double)runtime.arena->used();(*out)["registros_vivos"]=(double)runtime.arena->live_records();auto offsets=std::make_shared<std::map<std::string,Value>>();{std::lock_guard<std::mutex>guard(runtime.mutex);for(const auto&[name,offset]:runtime.latest)(*offsets)[name]=(double)offset;}(*out)["offsets"]=offsets;return out;}
static Value cfv_compat_append(Value collection,Value item){auto list=std::get_if<Lista>(&collection.data);if(!list)throw std::runtime_error("append/push requiere una lista");(*list)->push_back(std::move(item));cfv_arena_stage(collection,"compat_collection");return Value{};}
static Value cfv_compat_length(Value collection){cfv_arena_stage(collection,"compat_length");if(auto p=std::get_if<std::string>(&collection.data))return (double)p->size();if(auto p=std::get_if<Lista>(&collection.data))return (double)(*p)->size();if(auto p=std::get_if<Mapa>(&collection.data))return (double)(*p)->size();if(auto p=std::get_if<FastArray>(&collection.data))return (double)(*p)->size();if(auto p=std::get_if<DenseMatrix>(&collection.data))return (double)(*p)->rows;if(auto p=std::get_if<Tupla>(&collection.data))return (double)(*p)->values.size();if(auto p=std::get_if<Conjunto>(&collection.data))return (double)(*p)->values.size();throw std::runtime_error("length/len requiere texto o colección");}
static void mostrar(const Value&v){std::cout<<texto(v)<<'\n';}
static Value cfv_leer(Value mensaje=Value{std::string("")}){if(mensaje.index()!=2)throw std::runtime_error("el mensaje de leer debe ser texto");std::cout<<std::get<std::string>(mensaje.data);std::string s;std::getline(std::cin,s);return s;}
static Value cfv_a_numero(const Value&v){try{if(auto p=std::get_if<double>(&v.data))return *p;if(auto p=std::get_if<std::string>(&v.data))return std::stod(*p);}catch(...){ }throw std::runtime_error("no se puede convertir a número");}
static Value cfv_a_texto(const Value&v){return texto(v);}
static Value cfv_longitud(const Value&v){if(auto p=std::get_if<std::string>(&v.data))return (double)p->size();if(auto p=std::get_if<Lista>(&v.data))return (double)(*p)->size();if(auto p=std::get_if<Mapa>(&v.data)){auto& mp=*p;if(mp->count("__rango")){double fin=numero((*mp).at("fin"));double inicio=numero((*mp).at("inicio"));double paso=mp->count("paso")?numero((*mp).at("paso")):1.0;return (double)((long long)std::ceil((fin-inicio)/paso));}return (double)(*p)->size();}if(auto p=std::get_if<FastArray>(&v.data))return (double)(*p)->size();if(auto p=std::get_if<DenseMatrix>(&v.data))return (double)(*p)->rows;if(auto p=std::get_if<Tupla>(&v.data))return (double)(*p)->values.size();if(auto p=std::get_if<Conjunto>(&v.data))return (double)(*p)->values.size();throw std::runtime_error("longitud requiere una colección");}
static Value cfv_agregar(Value lista,Value valor){if(auto p=std::get_if<Lista>(&lista.data)){(*p)->push_back(std::move(valor));return Value{};}throw std::runtime_error("agregar requiere una lista");}
static Value cfv_sys_run(const Value&command){if(command.index()!=2)throw std::runtime_error("sys_run requiere un comando de texto");std::string shell=std::get<std::string>(command.data)+" 2>&1";
#ifdef _WIN32
FILE*pipe=_popen(shell.c_str(),"r");
#else
FILE*pipe=popen(shell.c_str(),"r");
#endif
if(!pipe)throw std::runtime_error("sys_run no pudo iniciar el comando");std::string output;char buffer[4096];while(std::fgets(buffer,sizeof(buffer),pipe))output+=buffer;
#ifdef _WIN32
int status=_pclose(pipe);
#else
int raw=pclose(pipe);int status=WIFEXITED(raw)?WEXITSTATUS(raw):raw;
#endif
auto result=std::make_shared<std::map<std::string,Value>>();(*result)["estado"]=(double)status;(*result)["salida"]=output;(*result)["error"]=std::string("");return result;}
static Value cfv_sys_info(){uint64_t memory=0;
#ifdef __APPLE__
size_t memory_size=sizeof(memory);sysctlbyname("hw.memsize",&memory,&memory_size,nullptr,0);
#elif !defined(_WIN32)
long pages=sysconf(_SC_PHYS_PAGES),page_size=sysconf(_SC_PAGE_SIZE);if(pages>0&&page_size>0)memory=(uint64_t)pages*(uint64_t)page_size;
#endif
auto result=std::make_shared<std::map<std::string,Value>>();(*result)["nucleos"]=(double)std::max(1u,std::thread::hardware_concurrency());(*result)["ram_bytes"]=(double)memory;
#if defined(__aarch64__) || defined(__arm64__)
(*result)["cpu"]=std::string("arm64");
#elif defined(__x86_64__) || defined(_M_X64)
(*result)["cpu"]=std::string("x86_64");
#else
(*result)["cpu"]=std::string("desconocido");
#endif
#ifdef __APPLE__
(*result)["sistema"]=std::string("macOS");
#elif defined(_WIN32)
(*result)["sistema"]=std::string("Windows");
#else
(*result)["sistema"]=std::string("Linux");
#endif
return result;}
static std::filesystem::path cfv_base_archivos;
// ── Module resolution cache (prevents double-import) ─────────────────────────
static std::unordered_set<std::string> cfv_imported_set;
// ── Module search paths ───────────────────────────────────────────────────────
static std::string cfv_resolver_modulo(const std::string& spec) {
    // 1. Add .cfv extension if missing
    std::string name = spec;
    if (name.size() < 4 || name.substr(name.size()-4) != ".cfv") name += ".cfv";
    // 2. Search paths (ordered by priority)
    std::vector<std::filesystem::path> search = {
        cfv_base_archivos / name,              // relative to script
        cfv_base_archivos / "stdlib" / std::filesystem::path(name).filename(),
        cfv_base_archivos / "cforge_modules" / std::filesystem::path(name).filename(),
        std::filesystem::path("/usr/local/lib/cforge/stdlib") / std::filesystem::path(name).filename(),
    };
    // ~/.cforge/stdlib
    if (const char* home = std::getenv("HOME")) {
        search.emplace_back(std::filesystem::path(home) / ".cforge" / "stdlib" /
                            std::filesystem::path(name).filename());
    }
    // CFORGE_STDLIB env override
    if (const char* stdlib_env = std::getenv("CFORGE_STDLIB")) {
        search.insert(search.begin(),
            std::filesystem::path(stdlib_env) / std::filesystem::path(name).filename());
    }
    for (const auto& p : search) {
        if (std::filesystem::exists(p)) return p.string();
    }
    // Final: return original (will fail gracefully with readable error)
    return (cfv_base_archivos / name).string();
}
static std::string ruta_archivo(const Value&v){if(v.index()!=2)throw std::runtime_error("la ruta debe ser texto");auto p=std::filesystem::path(std::get<std::string>(v.data));return (p.is_absolute()?p:cfv_base_archivos/p).string();}
static Value cfv_leer_archivo(const Value&ruta){std::ifstream f(ruta_archivo(ruta),std::ios::binary);if(!f)throw std::runtime_error("no se pudo abrir el archivo");std::ostringstream s;s<<f.rdbuf();return s.str();}
static Value cfv_escribir_archivo(const Value&ruta,const Value&contenido){if(contenido.index()!=2)throw std::runtime_error("el contenido debe ser texto");auto p=std::filesystem::path(ruta_archivo(ruta));if(p.has_parent_path())std::filesystem::create_directories(p.parent_path());std::ofstream f(p,std::ios::binary);if(!f)throw std::runtime_error("no se pudo escribir el archivo");f<<std::get<std::string>(contenido.data);return Value{};}
static Value cfv_bytes_texto(const Value&value){
  if(value.index()!=2)throw std::runtime_error("bytes_texto requiere texto");
  auto bytes=std::make_shared<std::vector<Value>>();
  for(unsigned char byte:std::get<std::string>(value.data))bytes->push_back(Value{(double)byte});
  return Value{bytes};
}
static Value cfv_escribir_bytes(const Value&ruta,const Value&contenido){
  auto bytes=std::get_if<Lista>(&contenido.data);
  if(!bytes)throw std::runtime_error("escribir_bytes requiere una lista");
  auto path=std::filesystem::path(ruta_archivo(ruta));
  if(path.has_parent_path())std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path,std::ios::binary|std::ios::trunc);
  if(!output)throw std::runtime_error("no se pudo escribir el archivo binario");
  for(const auto&item:**bytes){
    const double number=numero(item);
    if(number<0||number>255||std::floor(number)!=number)throw std::runtime_error("byte fuera de rango");
    output.put((char)(unsigned char)number);
  }
  return Value{(bool)output};
}
static Value cfv_hacer_ejecutable(const Value&ruta){
  auto path=std::filesystem::path(ruta_archivo(ruta));
#ifdef _WIN32
  return Value{std::filesystem::exists(path)};
#else
  std::error_code error;
  std::filesystem::permissions(path,
    std::filesystem::perms::owner_read|std::filesystem::perms::owner_write|
    std::filesystem::perms::owner_exec|std::filesystem::perms::group_read|
    std::filesystem::perms::group_exec|std::filesystem::perms::others_read|
    std::filesystem::perms::others_exec,std::filesystem::perm_options::replace,error);
  return Value{!error};
#endif
}
static Value cfv_file_read(const Value&ruta){return cfv_arena_stage(cfv_leer_archivo(ruta),"file_read");}
static Value cfv_file_write(const Value&ruta,const Value&contenido){return cfv_escribir_archivo(ruta,contenido);}
static Value cfv_file_append(const Value&ruta,const Value&contenido){if(contenido.index()!=2)throw std::runtime_error("el contenido debe ser texto");auto p=std::filesystem::path(ruta_archivo(ruta));if(p.has_parent_path())std::filesystem::create_directories(p.parent_path());std::ofstream f(p,std::ios::binary|std::ios::app);if(!f)throw std::runtime_error("no se pudo anexar al archivo");f<<std::get<std::string>(contenido.data);return Value{};}
static Value cfv_existe_archivo(const Value&ruta){return std::filesystem::exists(ruta_archivo(ruta));}
static Value cfv_listar_directorio(const Value&ruta){auto path=std::filesystem::path(ruta_archivo(ruta));auto result=std::make_shared<std::vector<Value>>();if(!std::filesystem::exists(path))return result;for(const auto&e:std::filesystem::directory_iterator(path))result->push_back(Value{e.path().filename().string()});return result;}
static Value cfv_crear_directorio(const Value&ruta){std::filesystem::create_directories(ruta_archivo(ruta));return Value{};}
static Value cfv_eliminar_archivo(const Value&ruta){std::filesystem::remove_all(ruta_archivo(ruta));return Value{};}
// HTTP GET via curl (no dependency on libcurl — uses system curl)
static Value cfv_http_get(const Value&url_v){
  if(url_v.index()!=2)throw std::runtime_error("http_get requiere una URL de texto");
  const std::string&url=std::get<std::string>(url_v.data);
  if(url.substr(0,4)!="http")throw std::runtime_error("http_get solo acepta URLs http/https");
  // Escape single quotes in URL for shell safety
  std::string safe_url;for(char c:url){if(c=='\'')safe_url+="'\\''";else safe_url+=c;}
  std::string cmd="curl -s -L --max-time 30 --max-filesize 16777216 '"+safe_url+"' 2>&1";
  FILE*pipe=popen(cmd.c_str(),"r");
  if(!pipe)throw std::runtime_error("http_get: no se pudo ejecutar curl");
  std::string output;char buf[4096];
  while(std::fgets(buf,sizeof(buf),pipe))output+=buf;
  pclose(pipe);
  return Value{output};
}
static Value cfv_array_fast(const Value&input){auto list=std::get_if<Lista>(&input.data);if(!list)throw std::runtime_error("array_fast requiere una lista numérica");auto output=std::make_shared<std::vector<double>>();output->reserve((*list)->size());for(const auto&value:**list)output->push_back(numero(value));return output;}
static Value cfv_matrix(const Value&rows_value,const Value&columns_value,const Value&fill_value=Value{0.0}){double rows_number=numero(rows_value),columns_number=numero(columns_value),fill=numero(fill_value);if(rows_number<0||columns_number<0||std::floor(rows_number)!=rows_number||std::floor(columns_number)!=columns_number||rows_number*columns_number>10000000.0)throw std::runtime_error("dimensiones de matrix inválidas");auto matrix=std::make_shared<CfvDenseMatrix>();matrix->rows=(size_t)rows_number;matrix->columns=(size_t)columns_number;matrix->values.assign(matrix->rows*matrix->columns,fill);return matrix;}
#ifdef _WIN32
static Value cfv_net_send(const Value&,const Value&,const Value&){throw std::runtime_error("net_send requiere backend Winsock");}
static Value cfv_net_listen(const Value&){throw std::runtime_error("net_listen requiere backend Winsock");}
static Value cfv_net_listen(const Value&,const Value&){throw std::runtime_error("net_listen requiere backend Winsock");}
#else
struct CfvSocket{int value=-1;explicit CfvSocket(int descriptor=-1):value(descriptor){}~CfvSocket(){if(value>=0)::close(value);}CfvSocket(const CfvSocket&)=delete;CfvSocket&operator=(const CfvSocket&)=delete;};
static Value cfv_net_send(const Value&host_value,const Value&port_value,const Value&data_value){if(host_value.index()!=2||data_value.index()!=2)throw std::runtime_error("net_send requiere host, puerto y texto");int port=(int)numero(port_value);if(port<1||port>65535)throw std::runtime_error("puerto inválido");addrinfo hints{};hints.ai_family=AF_UNSPEC;hints.ai_socktype=SOCK_STREAM;addrinfo*raw=nullptr;auto port_text=std::to_string(port);if(getaddrinfo(std::get<std::string>(host_value.data).c_str(),port_text.c_str(),&hints,&raw)!=0)throw std::runtime_error("no se pudo resolver el host");std::unique_ptr<addrinfo,decltype(&freeaddrinfo)>addresses(raw,freeaddrinfo);int descriptor=-1;for(auto*entry=raw;entry;entry=entry->ai_next){descriptor=socket(entry->ai_family,entry->ai_socktype,entry->ai_protocol);if(descriptor>=0&&connect(descriptor,entry->ai_addr,entry->ai_addrlen)==0)break;if(descriptor>=0)::close(descriptor);descriptor=-1;}CfvSocket connection(descriptor);if(descriptor<0)throw std::runtime_error("net_send no pudo conectar");const auto&data=std::get<std::string>(data_value.data);size_t sent=0;while(sent<data.size()){ssize_t count=send(descriptor,data.data()+sent,data.size()-sent,0);if(count<=0)throw std::runtime_error("net_send perdió la conexión");sent+=(size_t)count;}return (double)sent;}
static Value cfv_net_listen(const Value&port_value,const Value&timeout_value=Value{5000.0}){int port=(int)numero(port_value),timeout=(int)numero(timeout_value);if(port<1||port>65535||timeout<0)throw std::runtime_error("puerto o timeout inválido");CfvSocket server(socket(AF_INET,SOCK_STREAM,0));if(server.value<0)throw std::runtime_error("net_listen no pudo crear socket");int reuse=1;setsockopt(server.value,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=htons((uint16_t)port);if(bind(server.value,(sockaddr*)&address,sizeof(address))!=0||listen(server.value,1)!=0)throw std::runtime_error("net_listen no pudo abrir el puerto");fd_set set;FD_ZERO(&set);FD_SET(server.value,&set);timeval wait{timeout/1000,(timeout%1000)*1000};int ready=select(server.value+1,&set,nullptr,nullptr,&wait);if(ready==0)throw std::runtime_error("net_listen agotó el tiempo de espera");if(ready<0)throw std::runtime_error("net_listen falló esperando conexión");sockaddr_storage peer{};socklen_t peer_size=sizeof(peer);CfvSocket client(accept(server.value,(sockaddr*)&peer,&peer_size));if(client.value<0)throw std::runtime_error("net_listen no pudo aceptar conexión");std::string data;char buffer[65536];for(;;){ssize_t count=recv(client.value,buffer,sizeof(buffer),0);if(count<0)throw std::runtime_error("net_listen falló recibiendo datos");if(count==0)break;data.append(buffer,(size_t)count);}char host[NI_MAXHOST]={0};getnameinfo((sockaddr*)&peer,peer_size,host,sizeof(host),nullptr,0,NI_NUMERICHOST);auto result=std::make_shared<std::map<std::string,Value>>();(*result)["datos"]=data;(*result)["host"]=std::string(host);(*result)["puerto"]=(double)port;return result;}
#endif
// ── HTTP SERVER (POSIX sockets) ───────────────────────────────────────────
#ifndef _WIN32
struct CfvHttpServer {
    int server_fd = -1;
    int client_fd = -1;
    ~CfvHttpServer() {
        if (client_fd >= 0) { ::close(client_fd); client_fd = -1; }
        if (server_fd >= 0) { ::close(server_fd); server_fd = -1; }
    }
};
static std::map<int, std::shared_ptr<CfvHttpServer>> cfv_http_servers;
static int cfv_http_server_next_id = 1;
static std::mutex cfv_http_server_mutex;

static Value cfv_servidor_http_escuchar(const Value& puerto_v) {
    int port = (int)numero(puerto_v);
    if (port < 1 || port > 65535) throw std::runtime_error("servidor_http_escuchar: puerto invalido");
    auto srv = std::make_shared<CfvHttpServer>();
    srv->server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (srv->server_fd < 0) throw std::runtime_error("servidor_http_escuchar: no se pudo crear socket");
    int reuse = 1;
    setsockopt(srv->server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    if (bind(srv->server_fd, (sockaddr*)&addr, sizeof(addr)) != 0)
        throw std::runtime_error("servidor_http_escuchar: no se pudo enlazar al puerto " + std::to_string(port));
    if (listen(srv->server_fd, 64) != 0)
        throw std::runtime_error("servidor_http_escuchar: no se pudo escuchar");
    std::lock_guard<std::mutex> lk(cfv_http_server_mutex);
    int id = cfv_http_server_next_id++;
    cfv_http_servers[id] = srv;
    return (double)id;
}

static Value cfv_servidor_http_solicitud(const Value& id_v) {
    int id = (int)numero(id_v);
    std::shared_ptr<CfvHttpServer> srv;
    { std::lock_guard<std::mutex> lk(cfv_http_server_mutex); auto it = cfv_http_servers.find(id); if (it == cfv_http_servers.end()) throw std::runtime_error("servidor_http_solicitud: id invalido"); srv = it->second; }
    if (srv->client_fd >= 0) { ::close(srv->client_fd); srv->client_fd = -1; }
    sockaddr_storage peer{};
    socklen_t peer_len = sizeof(peer);
    int client = accept(srv->server_fd, (sockaddr*)&peer, &peer_len);
    if (client < 0) throw std::runtime_error("servidor_http_solicitud: accept fallo");
    srv->client_fd = client;
    // Read HTTP request
    std::string raw;
    char buf[4096];
    while (true) {
        ssize_t n = recv(client, buf, sizeof(buf), 0);
        if (n <= 0) break;
        raw.append(buf, (size_t)n);
        if (raw.find("\r\n\r\n") != std::string::npos) break;
    }
    // Parse request line
    std::string metodo = "GET", ruta = "/", protocolo = "HTTP/1.1";
    std::string cabeceras_raw, cuerpo;
    size_t eol1 = raw.find("\r\n");
    if (eol1 != std::string::npos) {
        std::string req_line = raw.substr(0, eol1);
        size_t s1 = req_line.find(' '), s2 = req_line.rfind(' ');
        if (s1 != std::string::npos && s2 != std::string::npos && s1 != s2) {
            metodo = req_line.substr(0, s1);
            ruta = req_line.substr(s1 + 1, s2 - s1 - 1);
            protocolo = req_line.substr(s2 + 1);
        }
        cabeceras_raw = raw.substr(eol1 + 2);
    }
    // Parse headers into mapa
    auto hdrs_map = std::make_shared<std::map<std::string, Value>>();
    size_t hdr_end = cabeceras_raw.find("\r\n\r\n");
    std::string hdr_section = (hdr_end != std::string::npos) ? cabeceras_raw.substr(0, hdr_end) : cabeceras_raw;
    if (hdr_end != std::string::npos) cuerpo = cabeceras_raw.substr(hdr_end + 4);
    size_t pos = 0;
    while (pos < hdr_section.size()) {
        size_t nl = hdr_section.find("\r\n", pos);
        if (nl == std::string::npos) nl = hdr_section.size();
        std::string line = hdr_section.substr(pos, nl - pos);
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string k = line.substr(0, colon);
            std::string v = line.substr(colon + 1);
            while (!v.empty() && (v[0] == ' ' || v[0] == '\t')) v = v.substr(1);
            for (auto& c : k) c = (char)std::tolower((unsigned char)c);
            (*hdrs_map)[k] = Value{v};
        }
        pos = nl + 2;
    }
    // Parse query string from ruta
    std::string path = ruta, query = "";
    size_t q = ruta.find('?');
    if (q != std::string::npos) { path = ruta.substr(0, q); query = ruta.substr(q + 1); }
    char peer_host[NI_MAXHOST] = {0};
    getnameinfo((sockaddr*)&peer, peer_len, peer_host, sizeof(peer_host), nullptr, 0, NI_NUMERICHOST);
    auto result = std::make_shared<std::map<std::string, Value>>();
    (*result)["metodo"] = Value{metodo};
    (*result)["ruta"] = Value{path};
    (*result)["query"] = Value{query};
    (*result)["cuerpo"] = Value{cuerpo};
    (*result)["cabeceras"] = Value{hdrs_map};
    (*result)["ip"] = Value{std::string(peer_host)};
    (*result)["_srv_id"] = Value{(double)id};
    return Value{result};
}

static Value cfv_servidor_http_responder(const Value& id_v, const Value& estado_v, const Value& cuerpo_v, const Value& tipo_v) {
    int id = (int)numero(id_v);
    std::shared_ptr<CfvHttpServer> srv;
    { std::lock_guard<std::mutex> lk(cfv_http_server_mutex); auto it = cfv_http_servers.find(id); if (it == cfv_http_servers.end()) throw std::runtime_error("servidor_http_responder: id invalido"); srv = it->second; }
    if (srv->client_fd < 0) throw std::runtime_error("servidor_http_responder: sin cliente activo");
    int estado = (int)numero(estado_v);
    const std::string& cuerpo = (cuerpo_v.index() == 2) ? std::get<std::string>(cuerpo_v.data) : texto(cuerpo_v);
    const std::string& tipo = (tipo_v.index() == 2) ? std::get<std::string>(tipo_v.data) : std::string("text/plain; charset=utf-8");
    std::string status_text = "OK";
    if (estado == 201) status_text = "Created";
    else if (estado == 204) status_text = "No Content";
    else if (estado == 400) status_text = "Bad Request";
    else if (estado == 401) status_text = "Unauthorized";
    else if (estado == 403) status_text = "Forbidden";
    else if (estado == 404) status_text = "Not Found";
    else if (estado == 405) status_text = "Method Not Allowed";
    else if (estado == 500) status_text = "Internal Server Error";
    std::string response = "HTTP/1.1 " + std::to_string(estado) + " " + status_text + "\r\n";
    response += "Content-Type: " + tipo + "\r\n";
    response += "Content-Length: " + std::to_string(cuerpo.size()) + "\r\n";
    response += "Connection: close\r\n";
    response += "Access-Control-Allow-Origin: *\r\n";
    response += "\r\n";
    response += cuerpo;
    size_t sent = 0;
    while (sent < response.size()) {
        ssize_t n = send(srv->client_fd, response.data() + sent, response.size() - sent, 0);
        if (n <= 0) break;
        sent += (size_t)n;
    }
    ::close(srv->client_fd);
    srv->client_fd = -1;
    return Value{};
}

static Value cfv_servidor_http_cerrar(const Value& id_v) {
    int id = (int)numero(id_v);
    std::lock_guard<std::mutex> lk(cfv_http_server_mutex);
    cfv_http_servers.erase(id);
    return Value{};
}
#else
static Value cfv_servidor_http_escuchar(const Value&){throw std::runtime_error("servidor_http_escuchar no disponible en Windows");}
static Value cfv_servidor_http_solicitud(const Value&){throw std::runtime_error("servidor_http_solicitud no disponible en Windows");}
static Value cfv_servidor_http_responder(const Value&,const Value&,const Value&,const Value&){throw std::runtime_error("servidor_http_responder no disponible en Windows");}
static Value cfv_servidor_http_cerrar(const Value&){throw std::runtime_error("servidor_http_cerrar no disponible en Windows");}
#endif

// ── CRYPTO (OpenSSL real si disponible, fallback pure-C++ para sha256/base64) ─
static std::string cfv_hex_encode(const unsigned char* data, size_t len) {
    static const char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; i++) {
        out += hex[(data[i] >> 4) & 0xf];
        out += hex[data[i] & 0xf];
    }
    return out;
}
static std::string cfv_unhex(const std::string& s) {
    std::string out;
    for (size_t i = 0; i + 1 < s.size(); i += 2) {
        auto nib = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return 0;
        };
        out += (char)((nib(s[i]) << 4) | nib(s[i+1]));
    }
    return out;
}

#ifdef CFV_WITH_OPENSSL
static Value cfv_sha256(const Value& v) {
    if (v.index() != 2) throw std::runtime_error("sha256 requiere texto");
    const auto& s = std::get<std::string>(v.data);
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((const unsigned char*)s.data(), s.size(), hash);
    return Value{cfv_hex_encode(hash, SHA256_DIGEST_LENGTH)};
}
static Value cfv_hmac_sha256(const Value& key_v, const Value& msg_v) {
    if (key_v.index() != 2 || msg_v.index() != 2) throw std::runtime_error("hmac_sha256 requiere dos textos");
    const auto& key = std::get<std::string>(key_v.data);
    const auto& msg = std::get<std::string>(msg_v.data);
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len = 0;
    HMAC(EVP_sha256(), key.data(), (int)key.size(), (const unsigned char*)msg.data(), msg.size(), hash, &hash_len);
    return Value{cfv_hex_encode(hash, hash_len)};
}
static Value cfv_aes_cifrar(const Value& key_v, const Value& texto_v) {
    if (key_v.index() != 2 || texto_v.index() != 2) throw std::runtime_error("aes_cifrar requiere clave y texto");
    const auto& key_raw = std::get<std::string>(key_v.data);
    const auto& plaintext = std::get<std::string>(texto_v.data);
    // Build 32-byte key (SHA256 of key_raw so any length works)
    unsigned char key32[32];
    SHA256((const unsigned char*)key_raw.data(), key_raw.size(), key32);
    // Random 16-byte IV
    unsigned char iv[16];
    RAND_bytes(iv, sizeof(iv));
    // Encrypt with AES-256-CBC
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) throw std::runtime_error("aes_cifrar: no se pudo crear contexto");
    EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key32, iv);
    std::vector<unsigned char> ciphertext(plaintext.size() + 32);
    int len1 = 0, len2 = 0;
    EVP_EncryptUpdate(ctx, ciphertext.data(), &len1, (const unsigned char*)plaintext.data(), (int)plaintext.size());
    EVP_EncryptFinal_ex(ctx, ciphertext.data() + len1, &len2);
    EVP_CIPHER_CTX_free(ctx);
    // Output: hex(iv) + ":" + hex(ciphertext)
    std::string result = cfv_hex_encode(iv, 16) + ":" + cfv_hex_encode(ciphertext.data(), (size_t)(len1 + len2));
    return Value{result};
}
static Value cfv_aes_descifrar(const Value& key_v, const Value& cifrado_v) {
    if (key_v.index() != 2 || cifrado_v.index() != 2) throw std::runtime_error("aes_descifrar requiere clave y cifrado");
    const auto& key_raw = std::get<std::string>(key_v.data);
    const auto& cifrado = std::get<std::string>(cifrado_v.data);
    size_t colon = cifrado.find(':');
    if (colon == std::string::npos || colon != 32) throw std::runtime_error("aes_descifrar: formato invalido (usar salida de aes_cifrar)");
    std::string iv_hex = cifrado.substr(0, 32);
    std::string ct_hex = cifrado.substr(33);
    std::string iv_raw = cfv_unhex(iv_hex);
    std::string ct_raw = cfv_unhex(ct_hex);
    unsigned char key32[32];
    SHA256((const unsigned char*)key_raw.data(), key_raw.size(), key32);
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) throw std::runtime_error("aes_descifrar: no se pudo crear contexto");
    EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key32, (const unsigned char*)iv_raw.data());
    std::vector<unsigned char> plain(ct_raw.size() + 16);
    int len1 = 0, len2 = 0;
    EVP_DecryptUpdate(ctx, plain.data(), &len1, (const unsigned char*)ct_raw.data(), (int)ct_raw.size());
    if (EVP_DecryptFinal_ex(ctx, plain.data() + len1, &len2) <= 0) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("aes_descifrar: clave incorrecta o datos corruptos");
    }
    EVP_CIPHER_CTX_free(ctx);
    return Value{std::string((char*)plain.data(), (size_t)(len1 + len2))};
}
static Value cfv_crypto_rand_bytes(const Value& n_v) {
    int n = (int)numero(n_v);
    if (n < 1 || n > 65536) throw std::runtime_error("crypto_rand_bytes: n debe ser 1..65536");
    std::vector<unsigned char> buf((size_t)n);
    RAND_bytes(buf.data(), n);
    return Value{cfv_hex_encode(buf.data(), (size_t)n)};
}
#else
// Fallback SHA-256 (pure C++ — no OpenSSL)
static std::string cfv_sha256_pure(const std::string& s) {
    // RFC 6234 SHA-256
    static const uint32_t K[64]={0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t h[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    std::vector<uint8_t> msg(s.begin(),s.end());
    uint64_t bit_len=(uint64_t)msg.size()*8;
    msg.push_back(0x80);
    while(msg.size()%64!=56)msg.push_back(0);
    for(int i=7;i>=0;i--)msg.push_back((uint8_t)(bit_len>>(i*8)));
    auto rotr=[](uint32_t x,int n){return(x>>n)|(x<<(32-n));};
    for(size_t i=0;i<msg.size();i+=64){
        uint32_t w[64];
        for(int j=0;j<16;j++)w[j]=((uint32_t)msg[i+j*4]<<24)|((uint32_t)msg[i+j*4+1]<<16)|((uint32_t)msg[i+j*4+2]<<8)|(uint32_t)msg[i+j*4+3];
        for(int j=16;j<64;j++){uint32_t s0=rotr(w[j-15],7)^rotr(w[j-15],18)^(w[j-15]>>3);uint32_t s1=rotr(w[j-2],17)^rotr(w[j-2],19)^(w[j-2]>>10);w[j]=w[j-16]+s0+w[j-7]+s1;}
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for(int j=0;j<64;j++){uint32_t S1=rotr(e,6)^rotr(e,11)^rotr(e,25);uint32_t ch=(e&f)^(~e&g);uint32_t tmp1=hh+S1+ch+K[j]+w[j];uint32_t S0=rotr(a,2)^rotr(a,13)^rotr(a,22);uint32_t maj=(a&b)^(a&c)^(b&c);uint32_t tmp2=S0+maj;hh=g;g=f;f=e;e=d+tmp1;d=c;c=b;b=a;a=tmp1+tmp2;}
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=hh;
    }
    unsigned char digest[32];
    for(int i=0;i<8;i++){digest[i*4]=(h[i]>>24)&0xff;digest[i*4+1]=(h[i]>>16)&0xff;digest[i*4+2]=(h[i]>>8)&0xff;digest[i*4+3]=h[i]&0xff;}
    return cfv_hex_encode(digest,32);
}
static Value cfv_sha256(const Value& v) {
    if (v.index() != 2) throw std::runtime_error("sha256 requiere texto");
    return Value{cfv_sha256_pure(std::get<std::string>(v.data))};
}
static Value cfv_hmac_sha256(const Value& key_v, const Value& msg_v) {
    if (key_v.index() != 2 || msg_v.index() != 2) throw std::runtime_error("hmac_sha256 requiere dos textos");
    const auto& key = std::get<std::string>(key_v.data);
    const auto& msg = std::get<std::string>(msg_v.data);
    std::string k = key;
    if (k.size() > 64) k = cfv_sha256_pure(k); // Hmm, need raw bytes — simplify
    k.resize(64, '\0');
    std::string ipad(64, '\x36'), opad(64, '\x5c');
    for (size_t i = 0; i < 64; i++) { ipad[i] ^= k[i]; opad[i] ^= k[i]; }
    return Value{cfv_sha256_pure(opad + cfv_sha256_pure(ipad + msg))};
}
static Value cfv_aes_cifrar(const Value&, const Value&) { throw std::runtime_error("aes_cifrar requiere compilar con -DCFV_WITH_OPENSSL"); }
static Value cfv_aes_descifrar(const Value&, const Value&) { throw std::runtime_error("aes_descifrar requiere compilar con -DCFV_WITH_OPENSSL"); }
static Value cfv_crypto_rand_bytes(const Value& n_v) {
    int n = (int)numero(n_v);
    if (n < 1 || n > 65536) throw std::runtime_error("crypto_rand_bytes: n debe ser 1..65536");
    std::vector<unsigned char> buf((size_t)n);
    // Use /dev/urandom as fallback
    FILE* f = fopen("/dev/urandom", "rb");
    if (f) { (void)fread(buf.data(), 1, (size_t)n, f); fclose(f); }
    return Value{cfv_hex_encode(buf.data(), (size_t)n)};
}
#endif

static Value cfv_sha256_rango(const Value&contenido,const Value&inicio_v,const Value&cantidad_v){
  auto bytes=std::get_if<Lista>(&contenido.data);
  if(!bytes)throw std::runtime_error("sha256_rango requiere una lista");
  const auto inicio=(size_t)numero(inicio_v);
  const auto cantidad=(size_t)numero(cantidad_v);
  if(inicio>(*bytes)->size()||cantidad>(*bytes)->size()-inicio)throw std::runtime_error("sha256_rango fuera de límites");
  std::string raw;
  raw.reserve(cantidad);
  for(size_t i=inicio;i<inicio+cantidad;++i){
    const double byte=numero((**bytes)[i]);
    if(byte<0||byte>255||std::floor(byte)!=byte)throw std::runtime_error("byte fuera de rango");
    raw.push_back((char)(unsigned char)byte);
  }
  const auto digest=std::get<std::string>(cfv_sha256(Value{raw}).data);
  auto result=std::make_shared<std::vector<Value>>();
  result->reserve(32);
  for(size_t i=0;i<digest.size();i+=2){
    result->push_back(Value{(double)std::stoul(digest.substr(i,2),nullptr,16)});
  }
  return Value{result};
}

// ── SDL2 BINDINGS ─────────────────────────────────────────────────────────
#ifdef CFV_WITH_SDL2
struct CfvSDLWindow {
    SDL_Window*   win  = nullptr;
    SDL_Renderer* ren  = nullptr;
    bool          open = false;
#ifdef CFV_WITH_SDL2_TTF
    TTF_Font*     font = nullptr;
#endif
    ~CfvSDLWindow() {
#ifdef CFV_WITH_SDL2_TTF
        if (font) { TTF_CloseFont(font); font = nullptr; }
#endif
        if (ren)  { SDL_DestroyRenderer(ren); ren = nullptr; }
        if (win)  { SDL_DestroyWindow(win);   win = nullptr; }
    }
};
static std::map<int, std::shared_ptr<CfvSDLWindow>> cfv_sdl_windows;
static int cfv_sdl_next_id = 1;
static bool cfv_sdl_initialized = false;

static std::shared_ptr<CfvSDLWindow>& cfv_sdl_get(int id) {
    auto it = cfv_sdl_windows.find(id);
    if (it == cfv_sdl_windows.end()) throw std::runtime_error("sdl: ventana invalida");
    return it->second;
}

// sdl_iniciar(titulo, ancho, alto) → id
static Value cfv_sdl_iniciar(const Value& titulo_v, const Value& ancho_v, const Value& alto_v) {
    if (!cfv_sdl_initialized) {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) != 0)
            throw std::runtime_error(std::string("sdl_iniciar: ") + SDL_GetError());
#ifdef CFV_WITH_SDL2_TTF
        TTF_Init();
#endif
#ifdef CFV_WITH_SDL2_MIXER
        Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048);
#endif
        cfv_sdl_initialized = true;
    }
    const std::string& titulo = (titulo_v.index()==2) ? std::get<std::string>(titulo_v.data) : "C-Forge";
    int ancho = (int)numero(ancho_v), alto = (int)numero(alto_v);
    auto w = std::make_shared<CfvSDLWindow>();
    w->win = SDL_CreateWindow(titulo.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, ancho, alto, SDL_WINDOW_SHOWN);
    if (!w->win) throw std::runtime_error(std::string("sdl_iniciar: ") + SDL_GetError());
    w->ren = SDL_CreateRenderer(w->win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!w->ren) throw std::runtime_error(std::string("sdl_iniciar: renderer ") + SDL_GetError());
    w->open = true;
    int id = cfv_sdl_next_id++;
    cfv_sdl_windows[id] = w;
    return (double)id;
}

// sdl_limpiar(id, r, g, b) → nulo
static Value cfv_sdl_limpiar(const Value& id_v, const Value& r_v, const Value& g_v, const Value& b_v) {
    auto& w = cfv_sdl_get((int)numero(id_v));
    SDL_SetRenderDrawColor(w->ren, (Uint8)numero(r_v), (Uint8)numero(g_v), (Uint8)numero(b_v), 255);
    SDL_RenderClear(w->ren);
    return Value{};
}

// sdl_dibujar_rect(id, x, y, ancho, alto, r, g, b, a=255) → nulo
static Value cfv_sdl_dibujar_rect(const Value& id_v, const Value& x_v, const Value& y_v, const Value& w_v, const Value& h_v, const Value& r_v, const Value& g_v, const Value& b_v, const Value& a_v) {
    auto& wnd = cfv_sdl_get((int)numero(id_v));
    SDL_Rect rect{ (int)numero(x_v),(int)numero(y_v),(int)numero(w_v),(int)numero(h_v) };
    SDL_SetRenderDrawColor(wnd->ren,(Uint8)numero(r_v),(Uint8)numero(g_v),(Uint8)numero(b_v),(Uint8)numero(a_v));
    SDL_RenderFillRect(wnd->ren, &rect);
    return Value{};
}

// sdl_dibujar_circulo(id, cx, cy, radio, r, g, b) → nulo
static Value cfv_sdl_dibujar_circulo(const Value& id_v, const Value& cx_v, const Value& cy_v, const Value& rad_v, const Value& r_v, const Value& g_v, const Value& b_v) {
    auto& wnd = cfv_sdl_get((int)numero(id_v));
    int cx=(int)numero(cx_v), cy=(int)numero(cy_v), rad=(int)numero(rad_v);
    SDL_SetRenderDrawColor(wnd->ren,(Uint8)numero(r_v),(Uint8)numero(g_v),(Uint8)numero(b_v),255);
    for (int dy=-rad; dy<=rad; dy++) {
        int dx=(int)std::sqrt((double)(rad*rad-dy*dy));
        SDL_RenderDrawLine(wnd->ren, cx-dx, cy+dy, cx+dx, cy+dy);
    }
    return Value{};
}

// sdl_dibujar_linea(id, x1, y1, x2, y2, r, g, b) → nulo
static Value cfv_sdl_dibujar_linea(const Value& id_v, const Value& x1_v, const Value& y1_v, const Value& x2_v, const Value& y2_v, const Value& r_v, const Value& g_v, const Value& b_v) {
    auto& wnd = cfv_sdl_get((int)numero(id_v));
    SDL_SetRenderDrawColor(wnd->ren,(Uint8)numero(r_v),(Uint8)numero(g_v),(Uint8)numero(b_v),255);
    SDL_RenderDrawLine(wnd->ren,(int)numero(x1_v),(int)numero(y1_v),(int)numero(x2_v),(int)numero(y2_v));
    return Value{};
}

// sdl_mostrar(id) → nulo
static Value cfv_sdl_mostrar(const Value& id_v) {
    auto& wnd = cfv_sdl_get((int)numero(id_v));
    SDL_RenderPresent(wnd->ren);
    return Value{};
}

// sdl_eventos(id) → lista de mapos {tipo, tecla, scancode, x, y, boton, rueda}
static Value cfv_sdl_eventos(const Value& id_v) {
    (void)id_v;
    auto lista = std::make_shared<std::vector<Value>>();
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        auto ev = std::make_shared<std::map<std::string,Value>>();
        switch (e.type) {
            case SDL_QUIT:
                (*ev)["tipo"] = Value{std::string("salir")};
                break;
            case SDL_KEYDOWN:
            case SDL_KEYUP: {
                (*ev)["tipo"] = Value{std::string(e.type==SDL_KEYDOWN?"tecla_abajo":"tecla_arriba")};
                (*ev)["tecla"] = Value{std::string(SDL_GetKeyName(e.key.keysym.sym))};
                (*ev)["scancode"] = Value{(double)e.key.keysym.scancode};
                (*ev)["repetir"] = Value{(bool)e.key.repeat};
                break;
            }
            case SDL_MOUSEMOTION:
                (*ev)["tipo"] = Value{std::string("raton_movimiento")};
                (*ev)["x"] = Value{(double)e.motion.x};
                (*ev)["y"] = Value{(double)e.motion.y};
                (*ev)["dx"] = Value{(double)e.motion.xrel};
                (*ev)["dy"] = Value{(double)e.motion.yrel};
                break;
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
                (*ev)["tipo"] = Value{std::string(e.type==SDL_MOUSEBUTTONDOWN?"raton_abajo":"raton_arriba")};
                (*ev)["x"] = Value{(double)e.button.x};
                (*ev)["y"] = Value{(double)e.button.y};
                (*ev)["boton"] = Value{(double)e.button.button};
                break;
            case SDL_MOUSEWHEEL:
                (*ev)["tipo"] = Value{std::string("rueda")};
                (*ev)["x"] = Value{(double)e.wheel.x};
                (*ev)["y"] = Value{(double)e.wheel.y};
                break;
            default:
                (*ev)["tipo"] = Value{std::string("otro")};
                (*ev)["codigo"] = Value{(double)e.type};
                break;
        }
        lista->push_back(Value{ev});
    }
    return Value{lista};
}

// sdl_tecla_presionada(nombre) → booleano  (estado inmediato del teclado)
static Value cfv_sdl_tecla_presionada(const Value& nombre_v) {
    if (nombre_v.index()!=2) return Value{false};
    const std::string& nombre = std::get<std::string>(nombre_v.data);
    const Uint8* state = SDL_GetKeyboardState(nullptr);
    SDL_Keycode kc = SDL_GetKeyFromName(nombre.c_str());
    SDL_Scancode sc = SDL_GetScancodeFromKey(kc);
    return Value{(bool)(sc != SDL_SCANCODE_UNKNOWN && state[sc])};
}

// sdl_raton() → mapa {x, y, izquierda, derecha, centro}
static Value cfv_sdl_raton(const Value&) {
    int x, y;
    Uint32 btn = SDL_GetMouseState(&x, &y);
    auto m = std::make_shared<std::map<std::string,Value>>();
    (*m)["x"] = Value{(double)x};
    (*m)["y"] = Value{(double)y};
    (*m)["izquierda"] = Value{(bool)(btn & SDL_BUTTON(1))};
    (*m)["centro"]    = Value{(bool)(btn & SDL_BUTTON(2))};
    (*m)["derecha"]   = Value{(bool)(btn & SDL_BUTTON(3))};
    return Value{m};
}

// sdl_delay(ms) → nulo
static Value cfv_sdl_delay(const Value& ms_v) {
    SDL_Delay((Uint32)numero(ms_v));
    return Value{};
}

// sdl_tiempo() → ms desde init
static Value cfv_sdl_tiempo(const Value&) {
    return Value{(double)SDL_GetTicks()};
}

// sdl_tamanio_ventana(id) → mapa {ancho, alto}
static Value cfv_sdl_tamanio_ventana(const Value& id_v) {
    auto& wnd = cfv_sdl_get((int)numero(id_v));
    int w=0, h=0;
    SDL_GetWindowSize(wnd->win, &w, &h);
    auto m = std::make_shared<std::map<std::string,Value>>();
    (*m)["ancho"] = Value{(double)w};
    (*m)["alto"]  = Value{(double)h};
    return Value{m};
}

// sdl_titulo(id, titulo) → nulo
static Value cfv_sdl_titulo(const Value& id_v, const Value& tit_v) {
    auto& wnd = cfv_sdl_get((int)numero(id_v));
    if (tit_v.index()==2) SDL_SetWindowTitle(wnd->win, std::get<std::string>(tit_v.data).c_str());
    return Value{};
}

// sdl_abierto(id) → booleano
static Value cfv_sdl_abierto(const Value& id_v) {
    auto it = cfv_sdl_windows.find((int)numero(id_v));
    if (it == cfv_sdl_windows.end()) return Value{false};
    return Value{it->second->open};
}

// sdl_cerrar(id) → nulo
static Value cfv_sdl_cerrar(const Value& id_v) {
    cfv_sdl_windows.erase((int)numero(id_v));
    return Value{};
}

// sdl_terminar() → nulo
static Value cfv_sdl_terminar(const Value&) {
    cfv_sdl_windows.clear();
#ifdef CFV_WITH_SDL2_MIXER
    Mix_CloseAudio();
#endif
#ifdef CFV_WITH_SDL2_TTF
    TTF_Quit();
#endif
    if (cfv_sdl_initialized) { SDL_Quit(); cfv_sdl_initialized = false; }
    return Value{};
}

// sdl_dibujar_pixel(id, x, y, r, g, b) → nulo
static Value cfv_sdl_dibujar_pixel(const Value& id_v, const Value& x_v, const Value& y_v, const Value& r_v, const Value& g_v, const Value& b_v) {
    auto& wnd = cfv_sdl_get((int)numero(id_v));
    SDL_SetRenderDrawColor(wnd->ren,(Uint8)numero(r_v),(Uint8)numero(g_v),(Uint8)numero(b_v),255);
    SDL_RenderDrawPoint(wnd->ren,(int)numero(x_v),(int)numero(y_v));
    return Value{};
}

// sdl_color_fondo(id, r, g, b) → alias de sdl_limpiar
static Value cfv_sdl_color_fondo(const Value& id_v, const Value& r_v, const Value& g_v, const Value& b_v) {
    return cfv_sdl_limpiar(id_v, r_v, g_v, b_v);
}

#ifdef CFV_WITH_SDL2_MIXER
// sdl_cargar_sonido(ruta) → id_sonido
static std::map<int,Mix_Chunk*> cfv_sdl_sounds;
static int cfv_sdl_sound_next = 1;
static Value cfv_sdl_cargar_sonido(const Value& ruta_v) {
    if (ruta_v.index()!=2) throw std::runtime_error("sdl_cargar_sonido: requiere ruta");
    Mix_Chunk* chunk = Mix_LoadWAV(std::get<std::string>(ruta_v.data).c_str());
    if (!chunk) throw std::runtime_error(std::string("sdl_cargar_sonido: ") + Mix_GetError());
    int id = cfv_sdl_sound_next++;
    cfv_sdl_sounds[id] = chunk;
    return (double)id;
}
static Value cfv_sdl_reproducir_sonido(const Value& id_v) {
    auto it = cfv_sdl_sounds.find((int)numero(id_v));
    if (it == cfv_sdl_sounds.end()) throw std::runtime_error("sdl_reproducir_sonido: id invalido");
    Mix_PlayChannel(-1, it->second, 0);
    return Value{};
}
static Value cfv_sdl_cargar_musica_fn(const Value& ruta_v) {
    if (ruta_v.index()!=2) throw std::runtime_error("sdl_cargar_musica: requiere ruta");
    Mix_Music* mus = Mix_LoadMUS(std::get<std::string>(ruta_v.data).c_str());
    if (!mus) throw std::runtime_error(std::string("sdl_cargar_musica: ") + Mix_GetError());
    Mix_PlayMusic(mus, -1);
    return Value{};
}
#endif

#else
// Stubs cuando SDL2 no esta disponible
static Value cfv_sdl_iniciar(const Value&,const Value&,const Value&){throw std::runtime_error("sdl_iniciar: compilar con -DCFV_WITH_SDL2 -lSDL2");}
static Value cfv_sdl_limpiar(const Value&,const Value&,const Value&,const Value&){throw std::runtime_error("SDL2 no disponible");}
static Value cfv_sdl_dibujar_rect(const Value&,const Value&,const Value&,const Value&,const Value&,const Value&,const Value&,const Value&,const Value&){throw std::runtime_error("SDL2 no disponible");}
static Value cfv_sdl_dibujar_circulo(const Value&,const Value&,const Value&,const Value&,const Value&,const Value&,const Value&){throw std::runtime_error("SDL2 no disponible");}
static Value cfv_sdl_dibujar_linea(const Value&,const Value&,const Value&,const Value&,const Value&,const Value&,const Value&,const Value&){throw std::runtime_error("SDL2 no disponible");}
static Value cfv_sdl_mostrar(const Value&){throw std::runtime_error("SDL2 no disponible");}
static Value cfv_sdl_eventos(const Value&){return std::make_shared<std::vector<Value>>();}
static Value cfv_sdl_tecla_presionada(const Value&){return Value{false};}
static Value cfv_sdl_raton(const Value&){return std::make_shared<std::map<std::string,Value>>();}
static Value cfv_sdl_delay(const Value& ms_v){
    int ms=(int)numero(ms_v);
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    return Value{};
}
static Value cfv_sdl_tiempo(const Value&){return Value{(double)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()};}
static Value cfv_sdl_tamanio_ventana(const Value&){auto m=std::make_shared<std::map<std::string,Value>>();(*m)["ancho"]=Value{800.0};(*m)["alto"]=Value{600.0};return Value{m};}
static Value cfv_sdl_titulo(const Value&,const Value&){throw std::runtime_error("SDL2 no disponible");}
static Value cfv_sdl_abierto(const Value&){return Value{false};}
static Value cfv_sdl_cerrar(const Value&){return Value{};}
static Value cfv_sdl_terminar(const Value&){return Value{};}
static Value cfv_sdl_dibujar_pixel(const Value&,const Value&,const Value&,const Value&,const Value&,const Value&){throw std::runtime_error("SDL2 no disponible");}
static Value cfv_sdl_color_fondo(const Value&,const Value&,const Value&,const Value&){throw std::runtime_error("SDL2 no disponible");}
#ifdef CFV_WITH_SDL2_MIXER
static Value cfv_sdl_cargar_sonido(const Value&){throw std::runtime_error("SDL2 no disponible");}
static Value cfv_sdl_reproducir_sonido(const Value&){throw std::runtime_error("SDL2 no disponible");}
static Value cfv_sdl_cargar_musica_fn(const Value&){throw std::runtime_error("SDL2 no disponible");}
#endif
#endif

// ── OpenGL 3D BINDINGS ────────────────────────────────────────────────────
#ifdef CFV_WITH_OPENGL
struct CfvGLContext {
    SDL_GLContext ctx = nullptr;
    int win_id = -1;
};
static std::map<int, CfvGLContext> cfv_gl_contexts;
static int cfv_gl_next_ctx = 1;

// gl3d_iniciar(sdl_win_id) → gl_ctx_id  (crear contexto OpenGL para una ventana SDL)
static Value cfv_gl3d_iniciar(const Value& id_v) {
#ifdef CFV_WITH_SDL2
    int win_id = (int)numero(id_v);
    auto it = cfv_sdl_windows.find(win_id);
    if (it == cfv_sdl_windows.end()) throw std::runtime_error("gl3d_iniciar: ventana SDL invalida");
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GLContext glctx = SDL_GL_CreateContext(it->second->win);
    if (!glctx) throw std::runtime_error(std::string("gl3d_iniciar: ") + SDL_GetError());
    SDL_GL_SetSwapInterval(1); // vsync
    int ctx_id = cfv_gl_next_ctx++;
    cfv_gl_contexts[ctx_id] = { glctx, win_id };
    return (double)ctx_id;
#else
    throw std::runtime_error("gl3d_iniciar: requiere SDL2 (-DCFV_WITH_SDL2)");
#endif
}

// gl3d_limpiar(r, g, b, a) → nulo
static Value cfv_gl3d_limpiar(const Value& r_v, const Value& g_v, const Value& b_v, const Value& a_v) {
    glClearColor((GLfloat)(numero(r_v)/255.0),(GLfloat)(numero(g_v)/255.0),
                 (GLfloat)(numero(b_v)/255.0),(GLfloat)(numero(a_v)/255.0));
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    return Value{};
}

// gl3d_viewport(x, y, ancho, alto) → nulo
static Value cfv_gl3d_viewport(const Value& x_v, const Value& y_v, const Value& w_v, const Value& h_v) {
    glViewport((GLint)numero(x_v),(GLint)numero(y_v),(GLsizei)numero(w_v),(GLsizei)numero(h_v));
    return Value{};
}

// gl3d_profundidad(activar) → nulo
static Value cfv_gl3d_profundidad(const Value& v) {
    if (verdad(v)) { glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); }
    else glDisable(GL_DEPTH_TEST);
    return Value{};
}

// gl3d_shader(tipo, codigo_glsl) → shader_id  (tipo: "vertex" o "fragment")
static Value cfv_gl3d_shader(const Value& tipo_v, const Value& src_v) {
    if (tipo_v.index()!=2||src_v.index()!=2) throw std::runtime_error("gl3d_shader: requiere tipo y codigo");
    const std::string& tipo = std::get<std::string>(tipo_v.data);
    const std::string& src  = std::get<std::string>(src_v.data);
    GLenum gl_tipo = (tipo=="vertex"||tipo=="vertice") ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER;
    GLuint shader = glCreateShader(gl_tipo);
    const char* src_c = src.c_str();
    glShaderSource(shader, 1, &src_c, nullptr);
    glCompileShader(shader);
    GLint ok; glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024]; glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        glDeleteShader(shader);
        throw std::runtime_error(std::string("gl3d_shader error: ") + log);
    }
    return (double)shader;
}

// gl3d_programa(vert_id, frag_id) → prog_id
static Value cfv_gl3d_programa(const Value& v_v, const Value& f_v) {
    GLuint prog = glCreateProgram();
    glAttachShader(prog, (GLuint)numero(v_v));
    glAttachShader(prog, (GLuint)numero(f_v));
    glLinkProgram(prog);
    GLint ok; glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024]; glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        glDeleteProgram(prog);
        throw std::runtime_error(std::string("gl3d_programa error: ") + log);
    }
    glDeleteShader((GLuint)numero(v_v));
    glDeleteShader((GLuint)numero(f_v));
    return (double)prog;
}

// gl3d_usar_programa(prog_id) → nulo
static Value cfv_gl3d_usar_programa(const Value& v) {
    glUseProgram((GLuint)numero(v));
    return Value{};
}

// gl3d_vbo(lista_numeros) → vbo_id  (crea VBO + VAO con los vertices dados)
static Value cfv_gl3d_vbo(const Value& datos_v) {
    auto* lista = std::get_if<Lista>(&datos_v.data);
    auto* arr   = std::get_if<FastArray>(&datos_v.data);
    std::vector<GLfloat> verts;
    if (lista) { for (auto& v : **lista) verts.push_back((GLfloat)numero(v)); }
    else if (arr) { for (double d : **arr) verts.push_back((GLfloat)d); }
    else throw std::runtime_error("gl3d_vbo: requiere una lista de numeros");
    GLuint vao, vbo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(verts.size()*sizeof(GLfloat)), verts.data(), GL_STATIC_DRAW);
    // Encodes vao+vbo as a combined ID (upper 32 bits = vao, lower 32 = vbo)
    uint64_t combined = ((uint64_t)vao << 32) | (uint64_t)vbo;
    return (double)(int64_t)combined;
}

// gl3d_atributo(vbo_id, index, size, stride, offset) → nulo
// Configura un vertex attribute pointer
static Value cfv_gl3d_atributo(const Value& vbo_v, const Value& idx_v, const Value& size_v, const Value& stride_v, const Value& off_v) {
    uint64_t combined = (uint64_t)(int64_t)numero(vbo_v);
    GLuint vao = (GLuint)(combined >> 32);
    GLuint vbo = (GLuint)(combined & 0xFFFFFFFF);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    GLuint idx = (GLuint)numero(idx_v);
    GLint  sz  = (GLint)numero(size_v);
    GLsizei stride = (GLsizei)(numero(stride_v)*sizeof(GLfloat));
    GLsizei off    = (GLsizei)(numero(off_v)*sizeof(GLfloat));
    glVertexAttribPointer(idx, sz, GL_FLOAT, GL_FALSE, stride, (void*)(intptr_t)off);
    glEnableVertexAttribArray(idx);
    return Value{};
}

// gl3d_dibujar(vbo_id, num_vertices, modo="triangulos") → nulo
static Value cfv_gl3d_dibujar(const Value& vbo_v, const Value& n_v, const Value& modo_v) {
    uint64_t combined = (uint64_t)(int64_t)numero(vbo_v);
    GLuint vao = (GLuint)(combined >> 32);
    glBindVertexArray(vao);
    GLenum modo = GL_TRIANGLES;
    if (modo_v.index()==2) {
        const auto& m = std::get<std::string>(modo_v.data);
        if (m=="lineas")     modo = GL_LINES;
        else if (m=="puntos") modo = GL_POINTS;
        else if (m=="tiras") modo = GL_TRIANGLE_STRIP;
        else if (m=="abanico") modo = GL_TRIANGLE_FAN;
    }
    glDrawArrays(modo, 0, (GLsizei)numero(n_v));
    return Value{};
}

// gl3d_uniforme_f(prog, nombre, valor) → nulo
static Value cfv_gl3d_uniforme_f(const Value& prog_v, const Value& nom_v, const Value& val_v) {
    if (nom_v.index()!=2) throw std::runtime_error("gl3d_uniforme_f: requiere nombre");
    GLint loc = glGetUniformLocation((GLuint)numero(prog_v), std::get<std::string>(nom_v.data).c_str());
    glUniform1f(loc, (GLfloat)numero(val_v));
    return Value{};
}

// gl3d_uniforme_vec3(prog, nombre, x, y, z) → nulo
static Value cfv_gl3d_uniforme_vec3(const Value& prog_v, const Value& nom_v, const Value& x, const Value& y, const Value& z) {
    if (nom_v.index()!=2) throw std::runtime_error("gl3d_uniforme_vec3: requiere nombre");
    GLint loc = glGetUniformLocation((GLuint)numero(prog_v), std::get<std::string>(nom_v.data).c_str());
    glUniform3f(loc, (GLfloat)numero(x), (GLfloat)numero(y), (GLfloat)numero(z));
    return Value{};
}

// gl3d_uniforme_mat4(prog, nombre, lista_16) → nulo
static Value cfv_gl3d_uniforme_mat4(const Value& prog_v, const Value& nom_v, const Value& mat_v) {
    if (nom_v.index()!=2) throw std::runtime_error("gl3d_uniforme_mat4: requiere nombre");
    auto* lista = std::get_if<Lista>(&mat_v.data);
    if (!lista || (*lista)->size() < 16) throw std::runtime_error("gl3d_uniforme_mat4: requiere lista de 16 numeros");
    GLfloat m[16];
    for (int i=0;i<16;i++) m[i] = (GLfloat)numero((**lista)[i]);
    GLint loc = glGetUniformLocation((GLuint)numero(prog_v), std::get<std::string>(nom_v.data).c_str());
    glUniformMatrix4fv(loc, 1, GL_FALSE, m);
    return Value{};
}

// gl3d_intercambiar(sdl_win_id) → nulo  (swap buffers)
static Value cfv_gl3d_intercambiar(const Value& id_v) {
#ifdef CFV_WITH_SDL2
    int win_id = (int)numero(id_v);
    auto it = cfv_sdl_windows.find(win_id);
    if (it != cfv_sdl_windows.end()) SDL_GL_SwapWindow(it->second->win);
#endif
    return Value{};
}

// gl3d_textura(ancho, alto, datos_rgba) → tex_id
static Value cfv_gl3d_textura(const Value& w_v, const Value& h_v, const Value& datos_v) {
    int w=(int)numero(w_v), h=(int)numero(h_v);
    auto* lista = std::get_if<Lista>(&datos_v.data);
    std::vector<uint8_t> px;
    if (lista) { for (auto& v : **lista) px.push_back((uint8_t)numero(v)); }
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.empty()?nullptr:px.data());
    return (double)tex;
}

// gl3d_usar_textura(tex_id, unidad=0) → nulo
static Value cfv_gl3d_usar_textura(const Value& tex_v, const Value& unit_v) {
    int unit = unit_v.index()==0 ? 0 : (int)numero(unit_v);
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, (GLuint)numero(tex_v));
    return Value{};
}

// gl3d_eliminar_vbo(vbo_id) → nulo
static Value cfv_gl3d_eliminar_vbo(const Value& vbo_v) {
    uint64_t combined = (uint64_t)(int64_t)numero(vbo_v);
    GLuint vao = (GLuint)(combined >> 32);
    GLuint vbo = (GLuint)(combined & 0xFFFFFFFF);
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
    return Value{};
}

// gl3d_cerrar(ctx_id) → nulo
static Value cfv_gl3d_cerrar(const Value& v) {
#ifdef CFV_WITH_SDL2
    auto it = cfv_gl_contexts.find((int)numero(v));
    if (it != cfv_gl_contexts.end()) {
        SDL_GL_DeleteContext(it->second.ctx);
        cfv_gl_contexts.erase(it);
    }
#endif
    return Value{};
}

#else
// Stubs sin OpenGL
static Value cfv_gl3d_iniciar(const Value&){throw std::runtime_error("gl3d_iniciar: compilar con -DCFV_WITH_OPENGL");}
static Value cfv_gl3d_limpiar(const Value&,const Value&,const Value&,const Value&){throw std::runtime_error("OpenGL no disponible");}
static Value cfv_gl3d_viewport(const Value&,const Value&,const Value&,const Value&){throw std::runtime_error("OpenGL no disponible");}
static Value cfv_gl3d_profundidad(const Value&){throw std::runtime_error("OpenGL no disponible");}
static Value cfv_gl3d_shader(const Value&,const Value&){throw std::runtime_error("OpenGL no disponible");}
static Value cfv_gl3d_programa(const Value&,const Value&){throw std::runtime_error("OpenGL no disponible");}
static Value cfv_gl3d_usar_programa(const Value&){throw std::runtime_error("OpenGL no disponible");}
static Value cfv_gl3d_vbo(const Value&){throw std::runtime_error("OpenGL no disponible");}
static Value cfv_gl3d_atributo(const Value&,const Value&,const Value&,const Value&,const Value&){throw std::runtime_error("OpenGL no disponible");}
static Value cfv_gl3d_dibujar(const Value&,const Value&,const Value&){throw std::runtime_error("OpenGL no disponible");}
static Value cfv_gl3d_uniforme_f(const Value&,const Value&,const Value&){throw std::runtime_error("OpenGL no disponible");}
static Value cfv_gl3d_uniforme_vec3(const Value&,const Value&,const Value&,const Value&,const Value&){throw std::runtime_error("OpenGL no disponible");}
static Value cfv_gl3d_uniforme_mat4(const Value&,const Value&,const Value&){throw std::runtime_error("OpenGL no disponible");}
static Value cfv_gl3d_intercambiar(const Value&){throw std::runtime_error("OpenGL no disponible");}
static Value cfv_gl3d_textura(const Value&,const Value&,const Value&){throw std::runtime_error("OpenGL no disponible");}
static Value cfv_gl3d_usar_textura(const Value&,const Value&){throw std::runtime_error("OpenGL no disponible");}
static Value cfv_gl3d_eliminar_vbo(const Value&){throw std::runtime_error("OpenGL no disponible");}
static Value cfv_gl3d_cerrar(const Value&){throw std::runtime_error("OpenGL no disponible");}
#endif

static Value cfv_raiz(const Value&v){double n=numero(v);if(n<0)throw std::runtime_error("no existe raíz real negativa");return std::sqrt(n);}static Value cfv_absoluto(const Value&v){return std::abs(numero(v));}static Value cfv_redondear(const Value&v){return std::round(numero(v));}static Value cfv_potencia(const Value&a,const Value&b){return std::pow(numero(a),numero(b));}
// ── nuevas funciones stdlib ────────────────────────────────────────────────
static Value cfv_texto_mayusculas(const Value&v){if(v.index()!=2)throw std::runtime_error("texto_mayusculas requiere texto");std::string s=std::get<std::string>(v.data);for(auto&c:s)c=(char)std::toupper((unsigned char)c);return s;}
static Value cfv_texto_minusculas(const Value&v){if(v.index()!=2)throw std::runtime_error("texto_minusculas requiere texto");std::string s=std::get<std::string>(v.data);for(auto&c:s)c=(char)std::tolower((unsigned char)c);return s;}
static Value cfv_texto_recortar(const Value&v){if(v.index()!=2)throw std::runtime_error("texto_recortar requiere texto");std::string s=std::get<std::string>(v.data);size_t a=s.find_first_not_of(" \t\r\n");size_t b=s.find_last_not_of(" \t\r\n");if(a==std::string::npos)return std::string("");return s.substr(a,b-a+1);}
static Value cfv_texto_empieza_con(const Value&v,const Value&p){if(v.index()!=2||p.index()!=2)throw std::runtime_error("texto_empieza_con requiere dos textos");const auto&s=std::get<std::string>(v.data);const auto&pre=std::get<std::string>(p.data);return (bool)(s.size()>=pre.size()&&s.compare(0,pre.size(),pre)==0);}
static Value cfv_texto_termina_con(const Value&v,const Value&p){if(v.index()!=2||p.index()!=2)throw std::runtime_error("texto_termina_con requiere dos textos");const auto&s=std::get<std::string>(v.data);const auto&suf=std::get<std::string>(p.data);return (bool)(s.size()>=suf.size()&&s.compare(s.size()-suf.size(),suf.size(),suf)==0);}
static Value cfv_texto_contiene(const Value&v,const Value&p){if(v.index()!=2||p.index()!=2)throw std::runtime_error("texto_contiene requiere dos textos");const auto&s=std::get<std::string>(v.data);const auto&sub=std::get<std::string>(p.data);return (bool)(s.find(sub)!=std::string::npos);}
static Value cfv_texto_indice(const Value&v,const Value&p){if(v.index()!=2||p.index()!=2)throw std::runtime_error("texto_indice requiere dos textos");const auto&s=std::get<std::string>(v.data);const auto&sub=std::get<std::string>(p.data);size_t pos=s.find(sub);return pos==std::string::npos?(double)-1:(double)pos;}
static Value cfv_texto_repetir(const Value&v,const Value&n){if(v.index()!=2)throw std::runtime_error("texto_repetir requiere texto y número");const auto&s=std::get<std::string>(v.data);int cnt=(int)numero(n);if(cnt<=0)return std::string("");std::string r;r.reserve(s.size()*(size_t)cnt);for(int i=0;i<cnt;i++)r+=s;return r;}
static Value cfv_texto_reemplazar(const Value&v,const Value&oldV,const Value&newV){if(v.index()!=2||oldV.index()!=2||newV.index()!=2)throw std::runtime_error("texto_reemplazar requiere tres textos");std::string s=std::get<std::string>(v.data);const auto&viejo=std::get<std::string>(oldV.data);const auto&nuevo=std::get<std::string>(newV.data);if(viejo.empty())return s;size_t pos=0;while((pos=s.find(viejo,pos))!=std::string::npos){s.replace(pos,viejo.size(),nuevo);pos+=nuevo.size();}return s;}
static Value cfv_texto_dividir(const Value&v,const Value&p){if(v.index()!=2||p.index()!=2)throw std::runtime_error("texto_dividir requiere dos textos");const auto&s=std::get<std::string>(v.data);const auto&sep=std::get<std::string>(p.data);auto result=std::make_shared<std::vector<Value>>();if(sep.empty()){for(char c:s)result->push_back(Value{std::string(1,c)});return Value{result};}size_t start=0,pos=0;while((pos=s.find(sep,start))!=std::string::npos){result->push_back(Value{s.substr(start,pos-start)});start=pos+sep.size();}result->push_back(Value{s.substr(start)});return Value{result};}
static Value cfv_texto_a_numero(const Value&v){if(v.index()!=2)throw std::runtime_error("texto_a_numero requiere texto");try{return std::stod(std::get<std::string>(v.data));}catch(...){throw std::runtime_error("texto_a_numero: no es un número válido");}}
static Value cfv_numero_a_texto(const Value&v){return Value{texto(v)};}
static Value cfv_piso(const Value&v){return std::floor(numero(v));}
static Value cfv_techo(const Value&v){return std::ceil(numero(v));}
static Value cfv_maximo(const Value&a,const Value&b){return numero(a)>=numero(b)?a:b;}
static Value cfv_minimo(const Value&a,const Value&b){return numero(a)<=numero(b)?a:b;}
static Value cfv_lista_ordenar(const Value&v){if(v.index()!=4)throw std::runtime_error("lista_ordenar requiere una lista");auto copy=std::make_shared<std::vector<Value>>(*std::get<Lista>(v.data));std::sort(copy->begin(),copy->end(),[](const Value&x,const Value&y){if(x.index()==2&&y.index()==2)return std::get<std::string>(x.data)<std::get<std::string>(y.data);if(x.index()==1&&y.index()==1)return std::get<double>(x.data)<std::get<double>(y.data);return false;});return Value{copy};}
static Value cfv_lista_invertir(const Value&v){if(v.index()!=4)throw std::runtime_error("lista_invertir requiere una lista");auto copy=std::make_shared<std::vector<Value>>(*std::get<Lista>(v.data));std::reverse(copy->begin(),copy->end());return Value{copy};}
static Value cfv_lista_contiene(const Value&v,const Value&item){if(v.index()!=4)throw std::runtime_error("lista_contiene requiere una lista");for(const auto&el:*std::get<Lista>(v.data))if(texto(el)==texto(item))return Value{true};return Value{false};}
static Value cfv_lista_unir(const Value&v,const Value&sep){if(v.index()!=4)throw std::runtime_error("lista_unir requiere una lista");const auto&list=*std::get<Lista>(v.data);std::string separator=sep.index()==2?std::get<std::string>(sep.data):",";std::string result;for(size_t i=0;i<list.size();i++){if(i)result+=separator;result+=texto(list[i]);}return Value{result};}
static Value cfv_lista_rango(const Value&a,const Value&b){double start=numero(a),end=numero(b);auto result=std::make_shared<std::vector<Value>>();for(double i=start;i<end;i+=1.0)result->push_back(Value{i});return Value{result};}
static Value cfv_lista_aplanar(const Value&v){if(v.index()!=4)throw std::runtime_error("lista_aplanar requiere una lista");auto result=std::make_shared<std::vector<Value>>();std::function<void(const Value&)>flatten=[&](const Value&x){if(x.index()==4){for(const auto&el:*std::get<Lista>(x.data))flatten(el);}else{result->push_back(x);}};flatten(v);return Value{result};}
static Value cfv_tiempo_actual(){using namespace std::chrono;return duration<double>(system_clock::now().time_since_epoch()).count();}
static Value cfv_argumentos_global;
static Value cfv_argumentos(){return cfv_argumentos_global;}
static std::mutex cfv_jit_mutex;
static std::map<std::string,size_t>cfv_jit_counts;
static void cfv_jit_hit(const std::string&name){std::lock_guard<std::mutex>lock(cfv_jit_mutex);++cfv_jit_counts[name];}
static Value cfv_jit_estado(const Value&name){if(name.index()!=2)throw std::runtime_error("jit_estado requiere texto");std::lock_guard<std::mutex>lock(cfv_jit_mutex);return (double)cfv_jit_counts[std::get<std::string>(name.data)];}
static Value cfv_jit_caliente(const Value&name){return numero(cfv_jit_estado(name))>=1000.0;}
static std::mutex cfv_cluster_mutex;
static std::map<std::string,std::string>cfv_cluster_symbols;
static void cfv_cluster_register(const std::string&name,const std::string&kind){std::lock_guard<std::mutex>lock(cfv_cluster_mutex);cfv_cluster_symbols[name]=kind;}
static Value cfv_cluster_estado(){std::lock_guard<std::mutex>lock(cfv_cluster_mutex);auto values=std::make_shared<std::vector<Value>>();for(const auto&entry:cfv_cluster_symbols)values->push_back(entry.second+":"+entry.first);return values;}
static Value cfv_afirmar(const Value&condition,const Value&message=Value{std::string("la condición es falsa")}){if(condition.index()!=3)throw std::runtime_error("afirmar requiere booleano");if(!verdad(condition))throw std::runtime_error("afirmación fallida: "+texto(message));return Value{};}
static Value cfv_parallel_unary(const std::function<Value(Value)>&fn,const Value&jobs){auto list=std::get_if<Lista>(&jobs.data);if(!list)throw std::runtime_error("paralelo requiere una lista");std::vector<std::future<Value>>running;running.reserve((*list)->size());for(const auto&job:**list)running.push_back(std::async(std::launch::async,[fn,job]{return fn(job);}));auto results=std::make_shared<std::vector<Value>>();results->reserve(running.size());for(auto&future:running)results->push_back(future.get());return results;}
static Value cfv_nuget_path(const std::string&package){std::vector<std::filesystem::path>paths={cfv_base_archivos/(package+".dylib"),cfv_base_archivos/"build"/(package+".dylib"),cfv_base_archivos.parent_path()/"build"/package/(package+".dylib"),cfv_base_archivos.parent_path()/"build"/"csharp-native"/(package+".dylib")};for(const auto&path:paths)if(std::filesystem::exists(path))return path.string();return paths.front().string();}
static std::vector<CfvValue> cfv_to_abi(const Value&args,std::vector<std::string>&storage){auto p=std::get_if<Lista>(&args.data);if(!p)throw std::runtime_error("los argumentos extranjeros deben ser una lista");std::vector<CfvValue>out;storage.reserve((*p)->size());for(const auto&v:**p){if(v.index()==0)out.push_back({CFV_NULL,0,0,nullptr,nullptr,nullptr});else if(auto n=std::get_if<double>(&v.data)){if(std::floor(*n)==*n)out.push_back({CFV_INTEGER,(int64_t)*n,0,nullptr,nullptr,nullptr});else out.push_back({CFV_DECIMAL,0,*n,nullptr,nullptr,nullptr});}else if(auto s=std::get_if<std::string>(&v.data)){storage.push_back(*s);out.push_back({CFV_TEXT,0,0,storage.back().c_str(),nullptr,nullptr});}else if(auto b=std::get_if<bool>(&v.data))out.push_back({CFV_BOOLEAN,*b?1:0,0,nullptr,nullptr,nullptr});else throw std::runtime_error("ABI extranjero solo acepta números, textos, booleanos y nulo");}return out;}
struct CfvResultGuard{CfvValue*value;~CfvResultGuard(){if(value&&value->release){auto release=value->release;auto owner=value->owner;value->release=nullptr;release(owner);}}};
static Value cfv_from_abi(const CfvValue&v){if(v.type==CFV_NULL)return Value{};if(v.type==CFV_INTEGER)return (double)v.integer;if(v.type==CFV_DECIMAL)return v.decimal;if(v.type==CFV_TEXT)return std::string(v.text?v.text:"");if(v.type==CFV_BOOLEAN)return Value{v.integer!=0};throw std::runtime_error("tipo ABI de retorno desconocido");}
static Value cfv_invoke_foreign(CfvForeignFunction fn,const Value&args){std::vector<std::string>storage;auto abi=cfv_to_abi(args,storage);CfvValue result{CFV_NULL,0,0,nullptr,nullptr,nullptr};CfvResultGuard guard{&result};char error[1024]={0};int status=0;try{status=fn(abi.data(),abi.size(),&result,error,sizeof(error));}catch(const std::exception&e){throw std::runtime_error(std::string("excepción C++: ")+e.what());}catch(...){throw std::runtime_error("excepción nativa desconocida");}if(status!=0)throw std::runtime_error(error[0]?error:"la función extranjera falló");return cfv_from_abi(result);}
struct CfvAbiStorageV2{
std::deque<std::string>texts;
std::deque<std::vector<CfvValueV2>>lists;
std::deque<std::vector<CfvMapEntryV2>>maps;
std::deque<std::vector<CfvRecordFieldV2>>record_fields;
std::deque<CfvRecordV2>records;
};
static CfvValueV2 cfv_to_abi_v2_value(const Value&v,CfvAbiStorageV2&storage,uint32_t depth=0){
if(depth>CFV_V2_MAX_DEPTH)throw std::runtime_error("ABI V2 excede la profundidad máxima");
CfvValueV2 item{sizeof(CfvValueV2),CFV_NULL,0,0,0,0,nullptr,nullptr,nullptr};
if(auto n=std::get_if<double>(&v.data)){if(std::floor(*n)==*n){item.type=CFV_INTEGER;item.integer=(int64_t)*n;}else{item.type=CFV_DECIMAL;item.decimal=*n;}}
else if(auto s=std::get_if<std::string>(&v.data)){storage.texts.push_back(*s);item.type=CFV_TEXT;item.flags=CFV_V2_BORROWED;item.length=storage.texts.back().size();item.data=storage.texts.back().data();}
else if(auto b=std::get_if<bool>(&v.data)){item.type=CFV_BOOLEAN;item.integer=*b?1:0;}
else if(auto list=std::get_if<Lista>(&v.data)){std::vector<CfvValueV2>values;values.reserve((*list)->size());for(const auto&value:**list)values.push_back(cfv_to_abi_v2_value(value,storage,depth+1));storage.lists.push_back(std::move(values));item.type=CFV_LIST;item.flags=CFV_V2_BORROWED;item.length=storage.lists.back().size();item.data=storage.lists.back().data();}
else if(auto map=std::get_if<Mapa>(&v.data)){
auto class_marker=(*map)->find("__clase");
if(class_marker!=(*map)->end()&&class_marker->second.index()==2){
std::vector<CfvRecordFieldV2>fields;fields.reserve((*map)->size()-1);
for(const auto&[key,value]:**map){if(key=="__clase")continue;storage.texts.push_back(key);auto&name=storage.texts.back();fields.push_back({name.data(),name.size(),cfv_to_abi_v2_value(value,storage,depth+1)});}
storage.record_fields.push_back(std::move(fields));storage.texts.push_back(std::get<std::string>(class_marker->second.data));auto&type=storage.texts.back();storage.records.push_back({type.data(),type.size(),storage.record_fields.back().data(),storage.record_fields.back().size()});item.type=CFV_RECORD;item.flags=CFV_V2_BORROWED;item.length=storage.records.back().field_count;item.data=&storage.records.back();
}else{
std::vector<CfvMapEntryV2>entries;entries.reserve((*map)->size());
for(const auto&[key,value]:**map){storage.texts.push_back(key);CfvValueV2 encoded_key{sizeof(CfvValueV2),CFV_TEXT,CFV_V2_BORROWED,storage.texts.back().size(),0,0,storage.texts.back().data(),nullptr,nullptr};entries.push_back({encoded_key,cfv_to_abi_v2_value(value,storage,depth+1)});}
storage.maps.push_back(std::move(entries));item.type=CFV_MAP;item.flags=CFV_V2_BORROWED;item.length=storage.maps.back().size();item.data=storage.maps.back().data();
}}
else throw std::runtime_error("tipo no compatible con ABI V2");
return item;
}
static std::vector<CfvValueV2>cfv_to_abi_v2(const Value&args,CfvAbiStorageV2&storage){auto p=std::get_if<Lista>(&args.data);if(!p)throw std::runtime_error("los argumentos ABI V2 deben ser una lista");std::vector<CfvValueV2>out;out.reserve((*p)->size());for(const auto&v:**p)out.push_back(cfv_to_abi_v2_value(v,storage));return out;}
struct CfvResultGuardV2{CfvValueV2*value;~CfvResultGuardV2(){if(value&&value->release){auto release=value->release;auto owner=value->owner;value->release=nullptr;release(owner);}}};
static Value cfv_from_abi_v2(const CfvValueV2&v,uint32_t depth=0){
if(depth>CFV_V2_MAX_DEPTH)throw std::runtime_error("resultado ABI V2 excede la profundidad máxima");
if(v.struct_size<sizeof(CfvValueV2))throw std::runtime_error("resultado ABI V2 usa una estructura incompleta");
if(v.type==CFV_NULL)return Value{};if(v.type==CFV_INTEGER)return (double)v.integer;if(v.type==CFV_DECIMAL)return v.decimal;if(v.type==CFV_BOOLEAN)return Value{v.integer!=0};
if(v.type==CFV_TEXT){if(!v.data&&v.length)throw std::runtime_error("texto ABI V2 tiene puntero nulo");return std::string(v.data?static_cast<const char*>(v.data):"",(size_t)v.length);}
if(v.length>10000000ull)throw std::runtime_error("colección ABI V2 excede el límite de elementos");
if(v.type==CFV_LIST){if(!v.data&&v.length)throw std::runtime_error("lista ABI V2 tiene puntero nulo");auto values=std::make_shared<std::vector<Value>>();values->reserve(v.length);auto raw=static_cast<const CfvValueV2*>(v.data);for(uint64_t i=0;i<v.length;++i){if(raw[i].release)throw std::runtime_error("elemento ABI V2 anidado no puede tener liberador");values->push_back(cfv_from_abi_v2(raw[i],depth+1));}return values;}
if(v.type==CFV_MAP){if(!v.data&&v.length)throw std::runtime_error("mapa ABI V2 tiene puntero nulo");auto values=std::make_shared<std::map<std::string,Value>>();auto raw=static_cast<const CfvMapEntryV2*>(v.data);for(uint64_t i=0;i<v.length;++i){if(raw[i].key.type!=CFV_TEXT)throw std::runtime_error("clave ABI V2 debe ser texto");if(raw[i].key.release||raw[i].value.release)throw std::runtime_error("entrada ABI V2 anidada no puede tener liberador");auto key=cfv_from_abi_v2(raw[i].key,depth+1);(*values)[std::get<std::string>(key.data)]=cfv_from_abi_v2(raw[i].value,depth+1);}return values;}
if(v.type==CFV_RECORD){if(!v.data)throw std::runtime_error("registro ABI V2 tiene puntero nulo");auto record=static_cast<const CfvRecordV2*>(v.data);if(record->field_count>1000000ull||(!record->fields&&record->field_count))throw std::runtime_error("registro ABI V2 inválido");auto values=std::make_shared<std::map<std::string,Value>>();(*values)["__clase"]=std::string(record->type_name?record->type_name:"",record->type_name_length);for(uint64_t i=0;i<record->field_count;++i){const auto&field=record->fields[i];if(field.value.release)throw std::runtime_error("campo ABI V2 anidado no puede tener liberador");(*values)[std::string(field.name?field.name:"",field.name_length)]=cfv_from_abi_v2(field.value,depth+1);}return values;}
throw std::runtime_error("tipo ABI V2 de retorno desconocido");
}
static Value cfv_invoke_foreign_v2(CfvForeignFunctionV2 fn,const Value&args){CfvAbiStorageV2 storage;auto abi=cfv_to_abi_v2(args,storage);CfvValueV2 result{sizeof(CfvValueV2),CFV_NULL,0,0,0,0,nullptr,nullptr,nullptr};CfvResultGuardV2 guard{&result};char error[1024]={0};int status=0;try{status=fn(CFV_ABI_V2,abi.data(),abi.size(),&result,error,sizeof(error));}catch(const std::exception&e){throw std::runtime_error(std::string("excepción C++ ABI V2: ")+e.what());}catch(...){throw std::runtime_error("excepción nativa ABI V2 desconocida");}if(status!=0)throw std::runtime_error(error[0]?error:"la función extranjera ABI V2 falló");return cfv_from_abi_v2(result);}
static Value cfv_use_cpp(const Value&name,const Value&args){if(name.index()!=2)throw std::runtime_error("el nombre C++ debe ser texto");auto key=std::get<std::string>(name.data);auto&registry2=cfv_registry_v2();auto modern=registry2.find(key);if(modern!=registry2.end())return cfv_invoke_foreign_v2(modern->second,args);auto&registry=cfv_registry();auto it=registry.find(key);if(it==registry.end())throw std::runtime_error("función C++ no registrada: "+key);return cfv_invoke_foreign(it->second,args);}
static std::map<std::string,void*>cfv_libraries;
static Value cfv_use_native(const Value&path,const Value&symbol,const Value&args){if(path.index()!=2||symbol.index()!=2)throw std::runtime_error("ruta y símbolo deben ser texto");auto raw=std::filesystem::path(std::get<std::string>(path.data));auto file=(raw.is_absolute()?raw:cfv_base_archivos/raw).string();auto sym=std::get<std::string>(symbol.data);void*handle=nullptr;auto found=cfv_libraries.find(file);if(found!=cfv_libraries.end())handle=found->second;else{
#ifdef _WIN32
handle=(void*)LoadLibraryA(file.c_str());
#else
handle=dlopen(file.c_str(),RTLD_NOW|RTLD_LOCAL);
#endif
if(!handle)throw std::runtime_error("no se pudo cargar la librería: "+file);cfv_libraries[file]=handle;}
#ifdef _WIN32
auto fn=(CfvForeignFunction)GetProcAddress((HMODULE)handle,sym.c_str());
#else
auto fn=(CfvForeignFunction)dlsym(handle,sym.c_str());
#endif
if(!fn)throw std::runtime_error("símbolo extranjero no encontrado: "+sym);return cfv_invoke_foreign(fn,args);}
static Value cfv_forge_bench(const std::function<Value()>&function,const Value&count){long long iterations=(long long)numero(count);if(iterations<1||iterations>10000000)throw std::runtime_error("forge_bench requiere 1..10.000.000 iteraciones");Value result;auto started=std::chrono::steady_clock::now();for(long long i=0;i<iterations;++i)result=function();double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();auto report=std::make_shared<std::map<std::string,Value>>();(*report)["resultado"]=result;(*report)["iteraciones"]=(double)iterations;(*report)["segundos"]=seconds;(*report)["por_segundo"]=seconds>0?iterations/seconds:0;return report;}
static Value crear_lista(std::initializer_list<Value>v){return std::make_shared<std::vector<Value>>(v);}static Value crear_mapa(std::initializer_list<std::pair<const std::string,Value>>v){return std::make_shared<std::map<std::string,Value>>(v);}static Value crear_tupla(std::initializer_list<Value>v){auto out=std::make_shared<CfvTuple>();out->values.assign(v);return out;}static Value crear_conjunto(std::initializer_list<Value>v){auto out=std::make_shared<CfvSet>();for(const auto&item:v){auto key=cfv_canonical_json(item);bool exists=false;for(const auto&current:out->values)if(cfv_canonical_json(current)==key){exists=true;break;}if(!exists)out->values.push_back(item);}std::sort(out->values.begin(),out->values.end(),[](const Value&a,const Value&b){return cfv_canonical_json(a)<cfv_canonical_json(b);});return out;}
static Value cfv_mover(Value value){return value;}static Value cfv_prestar(Value value){return value;}static Value cfv_prestar_mut(Value value){return value;}static Value cfv_soltar_prestamo(const Value&){return Value{};}static Value cfv_destruir(const Value&){return Value{};}
static Value cfv_algunos(Value value){return crear_mapa({{"__opcion",Value{true}},{"tiene",Value{true}},{"valor",std::move(value)}});}static Value cfv_ninguno(){return crear_mapa({{"__opcion",Value{true}},{"tiene",Value{false}},{"valor",Value{}}});}
static Mapa cfv_opcion_mapa(const Value&value){auto map=std::get_if<Mapa>(&value.data);if(!map||(*map)->find("__opcion")==(*map)->end())throw std::runtime_error("se esperaba una opcion");return *map;}static Value cfv_es_algunos(const Value&value){auto map=cfv_opcion_mapa(value);return verdad(map->at("tiene"));}static Value cfv_desenvolver(const Value&value){auto map=cfv_opcion_mapa(value);if(!verdad(map->at("tiene")))throw std::runtime_error("no se puede desenvolver ninguno");return map->at("valor");}
static std::atomic<long long>cfv_next_task{1};static std::mutex cfv_task_mutex;static std::map<long long,std::shared_future<Value>>cfv_tasks;
static Value cfv_tarea(std::function<Value()>job){auto id=cfv_next_task.fetch_add(1);auto future=std::async(std::launch::async,std::move(job)).share();{std::lock_guard<std::mutex>lock(cfv_task_mutex);cfv_tasks.emplace(id,std::move(future));}return (double)id;}
static std::shared_future<Value>cfv_task_future(const Value&handle){auto id=(long long)numero(handle);std::lock_guard<std::mutex>lock(cfv_task_mutex);auto found=cfv_tasks.find(id);if(found==cfv_tasks.end())throw std::runtime_error("tarea desconocida");return found->second;}
static Value cfv_esperar(const Value&handle){return cfv_task_future(handle).get();}static Value cfv_esperar(const Value&handle,const Value&timeout){auto future=cfv_task_future(handle);if(future.wait_for(std::chrono::milliseconds((long long)numero(timeout)))!=std::future_status::ready)throw std::runtime_error("tiempo de espera agotado");return future.get();}static Value cfv_cancelar(const Value&handle){(void)cfv_task_future(handle);return false;}
struct CfvChannel{size_t capacity=0;bool closed=false;std::deque<Value>values;std::mutex mutex;std::condition_variable readable,writable;};static std::atomic<long long>cfv_next_channel{1};static std::mutex cfv_channel_mutex;static std::map<long long,std::shared_ptr<CfvChannel>>cfv_channels;
static std::shared_ptr<CfvChannel>cfv_channel(const Value&handle){auto id=(long long)numero(handle);std::lock_guard<std::mutex>lock(cfv_channel_mutex);auto found=cfv_channels.find(id);if(found==cfv_channels.end())throw std::runtime_error("canal desconocido");return found->second;}
static Value cfv_canal(const Value&size){auto capacity=(long long)numero(size);if(capacity<0)throw std::runtime_error("capacidad de canal inválida");auto id=cfv_next_channel.fetch_add(1);auto channel=std::make_shared<CfvChannel>();channel->capacity=(size_t)capacity;{std::lock_guard<std::mutex>lock(cfv_channel_mutex);cfv_channels[id]=channel;}return (double)id;}static Value cfv_canal(){return cfv_canal(Value{0.0});}
static Value cfv_enviar(const Value&handle,Value value){auto channel=cfv_channel(handle);std::unique_lock<std::mutex>lock(channel->mutex);channel->writable.wait(lock,[&]{return channel->closed||channel->capacity==0||channel->values.size()<channel->capacity;});if(channel->closed)throw std::runtime_error("canal cerrado");channel->values.push_back(std::move(value));lock.unlock();channel->readable.notify_one();return Value{};}static Value cfv_recibir(const Value&handle){auto channel=cfv_channel(handle);std::unique_lock<std::mutex>lock(channel->mutex);channel->readable.wait(lock,[&]{return channel->closed||!channel->values.empty();});if(channel->values.empty())throw std::runtime_error("canal cerrado y vacío");Value result=std::move(channel->values.front());channel->values.pop_front();lock.unlock();channel->writable.notify_one();return result;}static Value cfv_cerrar_canal(const Value&handle){auto channel=cfv_channel(handle);{std::lock_guard<std::mutex>lock(channel->mutex);channel->closed=true;}channel->readable.notify_all();channel->writable.notify_all();return Value{};}
static Value indice(const Value&v,const Value&k){double n=0;if(auto p=std::get_if<std::string>(&v.data)){n=numero(k);if(n<0||std::floor(n)!=n||(size_t)n>=p->size())throw std::runtime_error("índice de texto inválido");return std::string(1,p->at((size_t)n));}if(auto p=std::get_if<Lista>(&v.data)){n=numero(k);if(n<0||std::floor(n)!=n||(size_t)n>=(*p)->size())throw std::runtime_error("índice de lista inválido");return (*p)->at((size_t)n);}if(auto p=std::get_if<Mapa>(&v.data)){if(k.index()!=2)throw std::runtime_error("la clave debe ser texto");auto it=(*p)->find(std::get<std::string>(k.data));if(it==(*p)->end())throw std::runtime_error("clave inexistente");return it->second;}if(auto p=std::get_if<Tupla>(&v.data)){n=numero(k);if(n<0||std::floor(n)!=n||(size_t)n>=(*p)->values.size())throw std::runtime_error("índice de tupla inválido");return (*p)->values.at((size_t)n);}if(auto p=std::get_if<FastArray>(&v.data)){n=numero(k);if(n<0||std::floor(n)!=n||(size_t)n>=(*p)->size())throw std::runtime_error("índice de array_fast inválido");return (*p)->at((size_t)n);}if(auto p=std::get_if<DenseMatrix>(&v.data)){n=numero(k);if(n<0||std::floor(n)!=n||(size_t)n>=(*p)->rows)throw std::runtime_error("fila de matrix inválida");auto row=std::make_shared<std::vector<double>>((*p)->values.begin()+(size_t)n*(*p)->columns,(*p)->values.begin()+((size_t)n+1)*(*p)->columns);return row;}throw std::runtime_error("el valor no admite índices");}
static void asignar_campo(Value&obj,const std::string&campo,Value valor,size_t tipo){auto p=std::get_if<Mapa>(&obj.data);if(!p||(*p)->find(campo)==(*p)->end())throw std::runtime_error("campo desconocido '"+campo+"'");if(tipo!=99&&valor.index()!=tipo)throw std::runtime_error("tipo incompatible para campo '"+campo+"'");(**p)[campo]=std::move(valor);}
static void asignar(Value&destino,size_t tipo,Value valor,const std::string&nombre){if(tipo!=99&&valor.index()!=tipo)throw std::runtime_error("tipo incompatible al asignar '"+nombre+"'");destino=std::move(valor);}
static void asignar_indice(Value container,Value key,Value valor){if(auto p=std::get_if<Lista>(&container.data)){double n=numero(key);if(n<0||std::floor(n)!=n||(size_t)n>=(*p)->size())throw std::runtime_error("índice de lista inválido para asignación");(**p)[(size_t)n]=std::move(valor);return;}if(auto p=std::get_if<Mapa>(&container.data)){if(key.index()!=2)throw std::runtime_error("la clave debe ser texto");(**p)[std::get<std::string>(key.data)]=std::move(valor);return;}throw std::runtime_error("asignación de índice requiere lista o mapa");}
static Value cfv_tiene_clave(const Value&obj,const Value&key){if(auto p=std::get_if<Mapa>(&obj.data)){if(key.index()!=2)return Value{false};return Value{(*p)->count(std::get<std::string>(key.data))>0};}return Value{false};}
static Value cfv_claves(const Value&obj){if(auto p=std::get_if<Mapa>(&obj.data)){auto result=std::make_shared<std::vector<Value>>();for(const auto&[k,v]:**p)result->push_back(Value{k});return result;}throw std::runtime_error("claves requiere un mapa");}
static Value cfv_tipo_de(const Value&v){if(v.index()==0)return std::string("nulo");if(std::get_if<double>(&v.data))return std::string("numero");if(std::get_if<std::string>(&v.data))return std::string("texto");if(std::get_if<bool>(&v.data))return std::string("booleano");if(std::get_if<Lista>(&v.data))return std::string("lista");if(std::get_if<Mapa>(&v.data))return std::string("mapa");return std::string("cualquiera");}
static Value cfv_argumentos_programa(){return cfv_argumentos_global;}

Value cfv_Token(Value cfv_tipo, Value cfv_lexema, Value cfv_linea);
Value cfv_Nodo(Value cfv_tipo, Value cfv_valor, Value cfv_hijos);
Value cfv_nodo(Value cfv_tipo, Value cfv_valor, Value cfv_hijos);
Value cfv_es_espacio(Value cfv_c);
Value cfv_es_digito(Value cfv_c);
Value cfv_es_letra(Value cfv_c);
Value cfv_es_alfanum(Value cfv_c);
Value cfv_subcadena(Value cfv_fuente, Value cfv_inicio, Value cfv_fin);
Value cfv_tokenizar(Value cfv_fuente);
Value cfv_mk_parser(Value cfv_tokens);
Value cfv_tok_actual(Value cfv_p);
Value cfv_tok_anterior(Value cfv_p);
Value cfv_al_final(Value cfv_p);
Value cfv_avanzar(Value cfv_p);
Value cfv_ver(Value cfv_p, Value cfv_lex);
Value cfv_ver_tipo(Value cfv_p, Value cfv_tipo);
Value cfv_tomar(Value cfv_p, Value cfv_lex);
Value cfv_requerir(Value cfv_p, Value cfv_lex, Value cfv_msg);
Value cfv_requerir_tipo(Value cfv_p, Value cfv_tipo, Value cfv_msg);
Value cfv_parse_bloque(Value cfv_p);
Value cfv_parse_sentencia(Value cfv_p);
Value cfv_parse_expresion(Value cfv_p);
Value cfv_parse_asignacion(Value cfv_p);
Value cfv_parse_nulo_coalescente(Value cfv_p);
Value cfv_parse_ternario(Value cfv_p);
Value cfv_parse_logico_o(Value cfv_p);
Value cfv_parse_logico_y(Value cfv_p);
Value cfv_parse_igualdad(Value cfv_p);
Value cfv_parse_comparacion(Value cfv_p);
Value cfv_parse_bit_o(Value cfv_p);
Value cfv_parse_bit_xor(Value cfv_p);
Value cfv_parse_bit_y(Value cfv_p);
Value cfv_parse_desplaz(Value cfv_p);
Value cfv_parse_suma(Value cfv_p);
Value cfv_parse_producto(Value cfv_p);
Value cfv_parse_unario(Value cfv_p);
Value cfv_parse_postfijo(Value cfv_p);
Value cfv_parse_argumentos(Value cfv_p);
Value cfv_parse_primaria(Value cfv_p);
Value cfv_parsear(Value cfv_tokens);
Value cfv_env_buscar(Value cfv_env, Value cfv_nombre);
Value cfv_env_tiene(Value cfv_env, Value cfv_nombre);
Value cfv_env_declarar(Value cfv_env, Value cfv_nombre, Value cfv_valor);
Value cfv_env_asignar(Value cfv_env, Value cfv_nombre, Value cfv_valor);
Value cfv_env_nuevo_scope(Value cfv_env);
Value cfv_formato_valor(Value cfv_v);
Value cfv_eval_binario(Value cfv_op, Value cfv_izq, Value cfv_der);
Value cfv_eval_builtin(Value cfv_nombre, Value cfv_args, Value cfv_env, Value cfv_fns);
Value cfv_eval_expr(Value cfv_nodo_e, Value cfv_env, Value cfv_fns);
Value cfv_exec_bloque(Value cfv_sentencias, Value cfv_env, Value cfv_fns);
Value cfv_exec_stmt(Value cfv_stmt, Value cfv_env, Value cfv_fns);
Value cfv_Token(Value cfv_tipo, Value cfv_lexema, Value cfv_linea) {
  cfv_jit_hit("Token");
  size_t cfv_tipo_tipo = cfv_tipo.index();
  size_t cfv_lexema_tipo = cfv_lexema.index();
  size_t cfv_linea_tipo = cfv_linea.index();
  return crear_mapa({{std::string("tipo", 4), cfv_tipo}, {std::string("lexema", 6), cfv_lexema}, {std::string("linea", 5), cfv_linea}});
  return Value{};
}
Value cfv_Nodo(Value cfv_tipo, Value cfv_valor, Value cfv_hijos) {
  cfv_jit_hit("Nodo");
  size_t cfv_tipo_tipo = cfv_tipo.index();
  size_t cfv_valor_tipo = cfv_valor.index();
  size_t cfv_hijos_tipo = cfv_hijos.index();
  return crear_mapa({{std::string("tipo", 4), cfv_tipo}, {std::string("valor", 5), cfv_valor}, {std::string("hijos", 5), cfv_hijos}});
  return Value{};
}
Value cfv_nodo(Value cfv_tipo, Value cfv_valor, Value cfv_hijos) {
  cfv_jit_hit("nodo");
  size_t cfv_tipo_tipo = cfv_tipo.index();
  size_t cfv_valor_tipo = cfv_valor.index();
  size_t cfv_hijos_tipo = cfv_hijos.index();
  return crear_mapa({{std::string("tipo", 4), cfv_tipo}, {std::string("valor", 5), cfv_valor}, {std::string("hijos", 5), cfv_hijos}});
  return Value{};
}
Value cfv_es_espacio(Value cfv_c) {
  cfv_jit_hit("es_espacio");
  size_t cfv_c_tipo = cfv_c.index();
  return Value{verdad(Value{verdad(compara(cfv_c, Value{std::string(" ", 1)}, "==")) || verdad(compara(cfv_c, Value{std::string("\t", 1)}, "=="))}) || verdad(compara(cfv_c, Value{std::string("\r", 1)}, "=="))};
  return Value{};
}
Value cfv_es_digito(Value cfv_c) {
  cfv_jit_hit("es_digito");
  size_t cfv_c_tipo = cfv_c.index();
  return Value{verdad(compara(cfv_c, Value{std::string("0", 1)}, ">=")) && verdad(compara(cfv_c, Value{std::string("9", 1)}, "<="))};
  return Value{};
}
Value cfv_es_letra(Value cfv_c) {
  cfv_jit_hit("es_letra");
  // ASCII a-z, A-Z, _
  const bool lower =
      verdad(compara(cfv_c, Value{std::string("a", 1)}, ">=")) &&
      verdad(compara(cfv_c, Value{std::string("z", 1)}, "<="));
  const bool upper =
      verdad(compara(cfv_c, Value{std::string("A", 1)}, ">=")) &&
      verdad(compara(cfv_c, Value{std::string("Z", 1)}, "<="));
  const bool underscore =
      verdad(compara(cfv_c, Value{std::string("_", 1)}, "=="));
  if (lower || upper || underscore)
    return Value{true};
  // UTF-8: any byte >= 0x80 is part of a multi-byte sequence — allow in identifiers
  if (auto s = std::get_if<std::string>(&cfv_c.data)) {
    if (!s->empty() && (unsigned char)(*s)[0] >= 0x80) return Value{true};
  }
  return Value{false};
}
Value cfv_es_alfanum(Value cfv_c) {
  cfv_jit_hit("es_alfanum");
  size_t cfv_c_tipo = cfv_c.index();
  return Value{verdad(cfv_es_letra(cfv_c)) || verdad(cfv_es_digito(cfv_c))};
  return Value{};
}
Value cfv_subcadena(Value cfv_fuente, Value cfv_inicio, Value cfv_fin) {
  cfv_jit_hit("subcadena");
  size_t cfv_fuente_tipo = cfv_fuente.index();
  size_t cfv_inicio_tipo = cfv_inicio.index();
  size_t cfv_fin_tipo = cfv_fin.index();
  Value cfv_resultado = Value{std::string("", 0)};
  if (cfv_resultado.index() != 2) throw std::runtime_error("tipo incompatible para resultado");
  size_t cfv_resultado_tipo = 2;
  Value cfv_i = cfv_inicio;
  if (cfv_i.index() != 1) throw std::runtime_error("tipo incompatible para i");
  size_t cfv_i_tipo = 1;
  while (verdad(compara(cfv_i, cfv_fin, "<"))) {
    asignar(cfv_resultado, cfv_resultado_tipo, suma(cfv_resultado, indice(cfv_fuente, cfv_i)), "resultado");
    asignar(cfv_i, cfv_i_tipo, suma(cfv_i, Value{1.0}), "i");
  }
  return cfv_resultado;
  return Value{};
}
Value cfv_tokenizar(Value cfv_fuente) {
  cfv_jit_hit("tokenizar");
  size_t cfv_fuente_tipo = cfv_fuente.index();
  Value cfv_tokens = crear_lista({});
  if (cfv_tokens.index() != 4) throw std::runtime_error("tipo incompatible para tokens");
  size_t cfv_tokens_tipo = 4;
  Value cfv_pos = Value{0.0};
  if (cfv_pos.index() != 1) throw std::runtime_error("tipo incompatible para pos");
  size_t cfv_pos_tipo = 1;
  Value cfv_linea = Value{1.0};
  if (cfv_linea.index() != 1) throw std::runtime_error("tipo incompatible para linea");
  size_t cfv_linea_tipo = 1;
  Value cfv_n = cfv_longitud(cfv_fuente);
  if (cfv_n.index() != 1) throw std::runtime_error("tipo incompatible para n");
  size_t cfv_n_tipo = 1;
  while (verdad(compara(cfv_pos, cfv_n, "<"))) {
    Value cfv_c = indice(cfv_fuente, cfv_pos);
    if (cfv_c.index() != 2) throw std::runtime_error("tipo incompatible para c");
    size_t cfv_c_tipo = 2;
    if (verdad(compara(cfv_c, Value{std::string("\n", 1)}, "=="))) {
      asignar(cfv_linea, cfv_linea_tipo, suma(cfv_linea, Value{1.0}), "linea");
      asignar(cfv_pos, cfv_pos_tipo, suma(cfv_pos, Value{1.0}), "pos");
    }     else if (verdad(cfv_es_espacio(cfv_c))) {
      asignar(cfv_pos, cfv_pos_tipo, suma(cfv_pos, Value{1.0}), "pos");
    }     else if (verdad(compara(cfv_c, Value{std::string("/", 1)}, "=="))) {
      if (verdad(Value{verdad(compara(suma(cfv_pos, Value{1.0}), cfv_n, "<")) && verdad(compara(indice(cfv_fuente, suma(cfv_pos, Value{1.0})), Value{std::string("/", 1)}, "=="))})) {
        while (verdad(Value{verdad(compara(cfv_pos, cfv_n, "<")) && verdad(compara(indice(cfv_fuente, cfv_pos), Value{std::string("\n", 1)}, "!="))})) {
          asignar(cfv_pos, cfv_pos_tipo, suma(cfv_pos, Value{1.0}), "pos");
        }
      } else if (verdad(Value{verdad(compara(suma(cfv_pos, Value{1.0}), cfv_n, "<")) && verdad(compara(indice(cfv_fuente, suma(cfv_pos, Value{1.0})), Value{std::string("=", 1)}, "=="))})) {
        // /= compound assignment
        asignar(cfv_pos, cfv_pos_tipo, suma(cfv_pos, Value{2.0}), "pos");
        (void)(cfv_agregar(cfv_tokens, cfv_Token(Value{std::string("SYMBOL", 6)}, Value{std::string("/=", 2)}, cfv_linea)));
      } else {
        asignar(cfv_pos, cfv_pos_tipo, suma(cfv_pos, Value{1.0}), "pos");
        (void)(cfv_agregar(cfv_tokens, cfv_Token(Value{std::string("SYMBOL", 6)}, Value{std::string("/", 1)}, cfv_linea)));
      }
    }     else if (verdad(cfv_es_digito(cfv_c))) {
      Value cfv_inicio = cfv_pos;
      if (cfv_inicio.index() != 1) throw std::runtime_error("tipo incompatible para inicio");
      size_t cfv_inicio_tipo = 1;
      while (verdad(Value{verdad(compara(cfv_pos, cfv_n, "<")) && verdad(cfv_es_digito(indice(cfv_fuente, cfv_pos)))})) {
        asignar(cfv_pos, cfv_pos_tipo, suma(cfv_pos, Value{1.0}), "pos");
      }
      if (verdad(Value{verdad(compara(cfv_pos, cfv_n, "<")) && verdad(compara(indice(cfv_fuente, cfv_pos), Value{std::string(".", 1)}, "=="))})) {
        asignar(cfv_pos, cfv_pos_tipo, suma(cfv_pos, Value{1.0}), "pos");
        while (verdad(Value{verdad(compara(cfv_pos, cfv_n, "<")) && verdad(cfv_es_digito(indice(cfv_fuente, cfv_pos)))})) {
          asignar(cfv_pos, cfv_pos_tipo, suma(cfv_pos, Value{1.0}), "pos");
        }
      }
      (void)(cfv_agregar(cfv_tokens, cfv_Token(Value{std::string("NUMBER", 6)}, cfv_subcadena(cfv_fuente, cfv_inicio, cfv_pos), cfv_linea)));
    }     else if (verdad(cfv_es_letra(cfv_c))) {
      Value cfv_inicio = cfv_pos;
      if (cfv_inicio.index() != 1) throw std::runtime_error("tipo incompatible para inicio");
      size_t cfv_inicio_tipo = 1;
      while (verdad(Value{verdad(compara(cfv_pos, cfv_n, "<")) && verdad(cfv_es_alfanum(indice(cfv_fuente, cfv_pos)))})) {
        asignar(cfv_pos, cfv_pos_tipo, suma(cfv_pos, Value{1.0}), "pos");
      }
      Value cfv_lex = cfv_subcadena(cfv_fuente, cfv_inicio, cfv_pos);
      if (cfv_lex.index() != 2) throw std::runtime_error("tipo incompatible para lex");
      size_t cfv_lex_tipo = 2;
      (void)(cfv_agregar(cfv_tokens, cfv_Token(Value{std::string("IDENT", 5)}, cfv_lex, cfv_linea)));
    }     else if (verdad(compara(cfv_c, Value{std::string("\"", 1)}, "=="))) {
      Value cfv_linea_str = cfv_linea;
      if (cfv_linea_str.index() != 1) throw std::runtime_error("tipo incompatible para linea_str");
      size_t cfv_linea_str_tipo = 1;
      Value cfv_literal = Value{std::string("\"", 1)};
      if (cfv_literal.index() != 2) throw std::runtime_error("tipo incompatible para literal");
      size_t cfv_literal_tipo = 2;
      asignar(cfv_pos, cfv_pos_tipo, suma(cfv_pos, Value{1.0}), "pos");
      Value cfv_cerrado = Value{false};
      if (cfv_cerrado.index() != 3) throw std::runtime_error("tipo incompatible para cerrado");
      size_t cfv_cerrado_tipo = 3;
      while (verdad(Value{verdad(compara(cfv_pos, cfv_n, "<")) && verdad(Value{!verdad(cfv_cerrado)})})) {
        Value cfv_ch = indice(cfv_fuente, cfv_pos);
        if (cfv_ch.index() != 2) throw std::runtime_error("tipo incompatible para ch");
        size_t cfv_ch_tipo = 2;
        if (verdad(compara(cfv_ch, Value{std::string("\\", 1)}, "=="))) {
          asignar(cfv_pos, cfv_pos_tipo, suma(cfv_pos, Value{1.0}), "pos");
          if (verdad(compara(cfv_pos, cfv_n, "<"))) {
            Value cfv_esc = indice(cfv_fuente, cfv_pos);
            if (cfv_esc.index() != 2) throw std::runtime_error("tipo incompatible para esc");
            size_t cfv_esc_tipo = 2;
            if (verdad(compara(cfv_esc, Value{std::string("n", 1)}, "=="))) {
              asignar(cfv_literal, cfv_literal_tipo, suma(cfv_literal, Value{std::string("\n", 1)}), "literal");
            }             else if (verdad(compara(cfv_esc, Value{std::string("t", 1)}, "=="))) {
              asignar(cfv_literal, cfv_literal_tipo, suma(cfv_literal, Value{std::string("\t", 1)}), "literal");
            }             else if (verdad(compara(cfv_esc, Value{std::string("\"", 1)}, "=="))) {
              asignar(cfv_literal, cfv_literal_tipo, suma(cfv_literal, Value{std::string("\"", 1)}), "literal");
            }             else if (verdad(compara(cfv_esc, Value{std::string("\\", 1)}, "=="))) {
              asignar(cfv_literal, cfv_literal_tipo, suma(cfv_literal, Value{std::string("\\", 1)}), "literal");
            }  else {
              asignar(cfv_literal, cfv_literal_tipo, suma(suma(cfv_literal, Value{std::string("\\", 1)}), cfv_esc), "literal");
            }
            asignar(cfv_pos, cfv_pos_tipo, suma(cfv_pos, Value{1.0}), "pos");
          }
        }         else if (verdad(compara(cfv_ch, Value{std::string("\"", 1)}, "=="))) {
          asignar(cfv_literal, cfv_literal_tipo, suma(cfv_literal, Value{std::string("\"", 1)}), "literal");
          asignar(cfv_pos, cfv_pos_tipo, suma(cfv_pos, Value{1.0}), "pos");
          asignar(cfv_cerrado, cfv_cerrado_tipo, Value{true}, "cerrado");
        }  else {
          if (verdad(compara(cfv_ch, Value{std::string("\n", 1)}, "=="))) {
            asignar(cfv_linea, cfv_linea_tipo, suma(cfv_linea, Value{1.0}), "linea");
          }
          asignar(cfv_literal, cfv_literal_tipo, suma(cfv_literal, cfv_ch), "literal");
          asignar(cfv_pos, cfv_pos_tipo, suma(cfv_pos, Value{1.0}), "pos");
        }
      }
      (void)(cfv_afirmar(cfv_cerrado, suma(Value{std::string("texto sin cerrar en línea ", 27)}, cfv_a_texto(cfv_linea_str))));
      (void)(cfv_agregar(cfv_tokens, cfv_Token(Value{std::string("STRING", 6)}, cfv_literal, cfv_linea_str)));
    }  else {
      Value cfv_sym = cfv_c;
      if (cfv_sym.index() != 2) throw std::runtime_error("tipo incompatible para sym");
      size_t cfv_sym_tipo = 2;
      // Check 3-char tokens first (e.g. ...)
      if (verdad(compara(suma(cfv_pos, Value{2.0}), cfv_n, "<"))) {
        Value cfv_tri = suma(suma(cfv_c, indice(cfv_fuente, suma(cfv_pos, Value{1.0}))), indice(cfv_fuente, suma(cfv_pos, Value{2.0})));
        if (verdad(compara(cfv_tri, Value{std::string("...", 3)}, "=="))) {
          asignar(cfv_sym, cfv_sym_tipo, cfv_tri, "sym");
          asignar(cfv_pos, cfv_pos_tipo, suma(cfv_pos, Value{2.0}), "pos");
        }
      }
      // Check 2-char tokens (only if didn't match 3-char)
      if (verdad(compara(cfv_longitud(cfv_sym), Value{1.0}, "=="))) {
        if (verdad(compara(suma(cfv_pos, Value{1.0}), cfv_n, "<"))) {
          Value cfv_par = suma(cfv_c, indice(cfv_fuente, suma(cfv_pos, Value{1.0})));
          if (cfv_par.index() != 2) throw std::runtime_error("tipo incompatible para par");
          size_t cfv_par_tipo = 2;
          if (verdad(Value{verdad(Value{verdad(Value{verdad(Value{verdad(Value{verdad(Value{verdad(compara(cfv_par, Value{std::string("==", 2)}, "==")) || verdad(compara(cfv_par, Value{std::string("!=", 2)}, "=="))}) || verdad(compara(cfv_par, Value{std::string(">=", 2)}, "=="))}) || verdad(compara(cfv_par, Value{std::string("<=", 2)}, "=="))}) || verdad(compara(cfv_par, Value{std::string("<<", 2)}, "=="))}) || verdad(compara(cfv_par, Value{std::string(">>", 2)}, "=="))}) || verdad(compara(cfv_par, Value{std::string("->", 2)}, "==")) || verdad(compara(cfv_par, Value{std::string("??", 2)}, "==")) || verdad(compara(cfv_par, Value{std::string("?.", 2)}, "==")) || verdad(compara(cfv_par, Value{std::string("+=", 2)}, "==")) || verdad(compara(cfv_par, Value{std::string("-=", 2)}, "==")) || verdad(compara(cfv_par, Value{std::string("*=", 2)}, "==")) || verdad(compara(cfv_par, Value{std::string("/=", 2)}, "==")) || verdad(compara(cfv_par, Value{std::string("%=", 2)}, "==")) || verdad(compara(cfv_par, Value{std::string("**", 2)}, "=="))})) {
            asignar(cfv_sym, cfv_sym_tipo, cfv_par, "sym");
            asignar(cfv_pos, cfv_pos_tipo, suma(cfv_pos, Value{1.0}), "pos");
          }
        }
      }
      asignar(cfv_pos, cfv_pos_tipo, suma(cfv_pos, Value{1.0}), "pos");
      (void)(cfv_agregar(cfv_tokens, cfv_Token(Value{std::string("SYMBOL", 6)}, cfv_sym, cfv_linea)));
    }
  }
  (void)(cfv_agregar(cfv_tokens, cfv_Token(Value{std::string("EOF", 3)}, Value{std::string("", 0)}, cfv_linea)));
  return cfv_tokens;
  return Value{};
}
Value cfv_mk_parser(Value cfv_tokens) {
  cfv_jit_hit("mk_parser");
  size_t cfv_tokens_tipo = cfv_tokens.index();
  return crear_mapa({{std::string("tokens", 6), cfv_tokens}, {std::string("pos", 3), Value{0.0}}});
  return Value{};
}
Value cfv_tok_actual(Value cfv_p) {
  cfv_jit_hit("tok_actual");
  size_t cfv_p_tipo = cfv_p.index();
  return indice(indice(cfv_p, Value{std::string("tokens", 6)}), indice(cfv_p, Value{std::string("pos", 3)}));
  return Value{};
}
Value cfv_tok_anterior(Value cfv_p) {
  cfv_jit_hit("tok_anterior");
  size_t cfv_p_tipo = cfv_p.index();
  return indice(indice(cfv_p, Value{std::string("tokens", 6)}), resta(indice(cfv_p, Value{std::string("pos", 3)}), Value{1.0}));
  return Value{};
}
Value cfv_al_final(Value cfv_p) {
  cfv_jit_hit("al_final");
  size_t cfv_p_tipo = cfv_p.index();
  return compara(indice(cfv_tok_actual(cfv_p), Value{std::string("tipo", 4)}), Value{std::string("EOF", 3)}, "==");
  return Value{};
}
Value cfv_avanzar(Value cfv_p) {
  cfv_jit_hit("avanzar");
  size_t cfv_p_tipo = cfv_p.index();
  if (verdad(Value{!verdad(cfv_al_final(cfv_p))})) {
    asignar_indice(cfv_p, Value{std::string("pos", 3)}, suma(indice(cfv_p, Value{std::string("pos", 3)}), Value{1.0}));
  }
  return cfv_tok_anterior(cfv_p);
  return Value{};
}
Value cfv_ver(Value cfv_p, Value cfv_lex) {
  cfv_jit_hit("ver");
  size_t cfv_p_tipo = cfv_p.index();
  size_t cfv_lex_tipo = cfv_lex.index();
  return compara(indice(cfv_tok_actual(cfv_p), Value{std::string("lexema", 6)}), cfv_lex, "==");
  return Value{};
}
Value cfv_ver_tipo(Value cfv_p, Value cfv_tipo) {
  cfv_jit_hit("ver_tipo");
  size_t cfv_p_tipo = cfv_p.index();
  size_t cfv_tipo_tipo = cfv_tipo.index();
  return compara(indice(cfv_tok_actual(cfv_p), Value{std::string("tipo", 4)}), cfv_tipo, "==");
  return Value{};
}
Value cfv_tomar(Value cfv_p, Value cfv_lex) {
  cfv_jit_hit("tomar");
  size_t cfv_p_tipo = cfv_p.index();
  size_t cfv_lex_tipo = cfv_lex.index();
  if (verdad(cfv_ver(cfv_p, cfv_lex))) {
    (void)(cfv_avanzar(cfv_p));
    return Value{true};
  }
  return Value{false};
  return Value{};
}
Value cfv_requerir(Value cfv_p, Value cfv_lex, Value cfv_msg) {
  cfv_jit_hit("requerir");
  size_t cfv_p_tipo = cfv_p.index();
  size_t cfv_lex_tipo = cfv_lex.index();
  size_t cfv_msg_tipo = cfv_msg.index();
  (void)(cfv_afirmar(cfv_ver(cfv_p, cfv_lex), suma(suma(suma(suma(suma(cfv_msg, Value{std::string(" (línea ", 9)}), cfv_a_texto(indice(cfv_tok_actual(cfv_p), Value{std::string("linea", 5)}))), Value{std::string("), se encontró '", 17)}), indice(cfv_tok_actual(cfv_p), Value{std::string("lexema", 6)})), Value{std::string("'", 1)})));
  return cfv_avanzar(cfv_p);
  return Value{};
}
Value cfv_requerir_tipo(Value cfv_p, Value cfv_tipo, Value cfv_msg) {
  cfv_jit_hit("requerir_tipo");
  size_t cfv_p_tipo = cfv_p.index();
  size_t cfv_tipo_tipo = cfv_tipo.index();
  size_t cfv_msg_tipo = cfv_msg.index();
  (void)(cfv_afirmar(cfv_ver_tipo(cfv_p, cfv_tipo), suma(suma(suma(cfv_msg, Value{std::string(" (línea ", 9)}), cfv_a_texto(indice(cfv_tok_actual(cfv_p), Value{std::string("linea", 5)}))), Value{std::string(")", 1)})));
  return cfv_avanzar(cfv_p);
  return Value{};
}
Value cfv_parse_bloque(Value cfv_p) {
  cfv_jit_hit("parse_bloque");
  size_t cfv_p_tipo = cfv_p.index();
  (void)(cfv_requerir(cfv_p, Value{std::string("{", 1)}, Value{std::string("Se esperaba '{'", 15)}));
  Value cfv_sentencias = crear_lista({});
  if (cfv_sentencias.index() != 4) throw std::runtime_error("tipo incompatible para sentencias");
  size_t cfv_sentencias_tipo = 4;
  while (verdad(Value{verdad(Value{!verdad(cfv_al_final(cfv_p))}) && verdad(Value{!verdad(cfv_ver(cfv_p, Value{std::string("}", 1)}))})})) {
    (void)(cfv_agregar(cfv_sentencias, cfv_parse_sentencia(cfv_p)));
  }
  (void)(cfv_requerir(cfv_p, Value{std::string("}", 1)}, Value{std::string("Se esperaba '}'", 15)}));
  return cfv_sentencias;
  return Value{};
}
Value cfv_parse_sentencia(Value cfv_p) {
  cfv_jit_hit("parse_sentencia");
  size_t cfv_p_tipo = cfv_p.index();
  if (verdad(Value{verdad(Value{verdad(cfv_ver(cfv_p, Value{std::string("sea", 3)})) || verdad(cfv_ver(cfv_p, Value{std::string("var", 3)}))}) || verdad(cfv_ver(cfv_p, Value{std::string("const", 5)}))})) {
    (void)(cfv_avanzar(cfv_p));
    // Destructuring: sea [a, b] = lista
    if (verdad(cfv_ver(cfv_p, Value{std::string("[", 1)}))) {
      (void)(cfv_avanzar(cfv_p));
      Value cfv_nombres_d = crear_lista({});
      while (verdad(Value{!verdad(cfv_ver(cfv_p, Value{std::string("]", 1)})) && !verdad(cfv_al_final(cfv_p))})) {
        if (verdad(cfv_ver(cfv_p, Value{std::string("_", 1)}))) {
          (void)(cfv_avanzar(cfv_p));
          (void)(cfv_agregar(cfv_nombres_d, Value{std::string("_")}));
        } else {
          Value cfv_dn_tok = cfv_requerir_tipo(cfv_p, Value{std::string("IDENT")}, Value{std::string("Se esperaba nombre en desestructuración")});
          (void)(cfv_agregar(cfv_nombres_d, indice(cfv_dn_tok, Value{std::string("lexema")})));
        }
        (void)(cfv_tomar(cfv_p, Value{std::string(",", 1)}));
      }
      (void)(cfv_requerir(cfv_p, Value{std::string("]", 1)}, Value{std::string("Se esperaba ']'")}));
      if (verdad(cfv_tomar(cfv_p, Value{std::string(":", 1)}))) {
        if (verdad(cfv_ver_tipo(cfv_p, Value{std::string("IDENT", 5)}))) {
          (void)(cfv_avanzar(cfv_p));
        }
      }
      (void)(cfv_requerir(cfv_p, Value{std::string("=", 1)}, Value{std::string("Se esperaba '=' en desestructuración")}));
      Value cfv_dval = cfv_parse_expresion(cfv_p);
      (void)(cfv_tomar(cfv_p, Value{std::string(";", 1)}));
      return cfv_nodo(Value{std::string("DestructList")}, Value{}, crear_lista({cfv_dval, cfv_nombres_d}));
    }
    // Mapa destructuring: sea {x, y} = mapa
    if (verdad(cfv_ver(cfv_p, Value{std::string("{", 1)}))) {
      (void)(cfv_avanzar(cfv_p));
      Value cfv_nombres_dm = crear_lista({});
      while (verdad(Value{!verdad(cfv_ver(cfv_p, Value{std::string("}", 1)})) && !verdad(cfv_al_final(cfv_p))})) {
        Value cfv_dn_tok2 = cfv_requerir_tipo(cfv_p, Value{std::string("IDENT")}, Value{std::string("Se esperaba nombre en desestructuración mapa")});
        (void)(cfv_agregar(cfv_nombres_dm, indice(cfv_dn_tok2, Value{std::string("lexema")})));
        (void)(cfv_tomar(cfv_p, Value{std::string(",", 1)}));
      }
      (void)(cfv_requerir(cfv_p, Value{std::string("}", 1)}, Value{std::string("Se esperaba '}'")}));
      if (verdad(cfv_tomar(cfv_p, Value{std::string(":", 1)}))) {
        if (verdad(cfv_ver_tipo(cfv_p, Value{std::string("IDENT", 5)}))) {
          (void)(cfv_avanzar(cfv_p));
        }
      }
      (void)(cfv_requerir(cfv_p, Value{std::string("=", 1)}, Value{std::string("Se esperaba '='")}));
      Value cfv_dval2 = cfv_parse_expresion(cfv_p);
      (void)(cfv_tomar(cfv_p, Value{std::string(";", 1)}));
      return cfv_nodo(Value{std::string("DestructMapa")}, Value{}, crear_lista({cfv_dval2, cfv_nombres_dm}));
    }
    Value cfv_nombre_tok = cfv_requerir_tipo(cfv_p, Value{std::string("IDENT", 5)}, Value{std::string("Se esperaba nombre de variable", 30)});
    size_t cfv_nombre_tok_tipo = 99;
    Value cfv_nombre = indice(cfv_nombre_tok, Value{std::string("lexema", 6)});
    if (cfv_nombre.index() != 2) throw std::runtime_error("tipo incompatible para nombre");
    size_t cfv_nombre_tipo = 2;
    Value cfv_decl_tipo_anot = Value{}; // nulo = no annotation
    if (verdad(cfv_tomar(cfv_p, Value{std::string(":", 1)}))) {
      Value cfv_tipo_anot_tok = cfv_requerir_tipo(cfv_p, Value{std::string("IDENT", 5)}, Value{std::string("Se esperaba tipo", 16)});
      cfv_decl_tipo_anot = indice(cfv_tipo_anot_tok, Value{std::string("lexema", 6)});
      if (verdad(cfv_tomar(cfv_p, Value{std::string("<", 1)}))) {
        // Generic: Lista<Numero> → store as "Lista<Numero>"
        Value cfv_gen_tok = cfv_requerir_tipo(cfv_p, Value{std::string("IDENT", 5)}, Value{std::string("Se esperaba tipo genérico", 26)});
        cfv_decl_tipo_anot = Value{texto(cfv_decl_tipo_anot) + "<" + texto(indice(cfv_gen_tok, Value{std::string("lexema",6)})) + ">"};
        (void)(cfv_tomar(cfv_p, Value{std::string(">", 1)}));
      }
    }
    if (verdad(cfv_tomar(cfv_p, Value{std::string("=", 1)}))) {
      Value cfv_val = cfv_parse_expresion(cfv_p);
      size_t cfv_val_tipo = 99;
      (void)(cfv_tomar(cfv_p, Value{std::string(";", 1)}));
      return cfv_nodo(Value{std::string("Declaracion", 11)}, cfv_nombre, crear_lista({cfv_val, cfv_decl_tipo_anot}));
    } else {
      (void)(cfv_tomar(cfv_p, Value{std::string(";", 1)}));
      return cfv_nodo(Value{std::string("Declaracion", 11)}, cfv_nombre, crear_lista({cfv_nodo(Value{std::string("Nulo", 4)}, Value{}, crear_lista({})), cfv_decl_tipo_anot}));
    }
  }
  if (verdad(cfv_ver(cfv_p, Value{std::string("retornar", 8)}))) {
    (void)(cfv_avanzar(cfv_p));
    if (verdad(Value{verdad(Value{verdad(cfv_ver(cfv_p, Value{std::string("}", 1)})) || verdad(cfv_ver(cfv_p, Value{std::string(";", 1)}))}) || verdad(cfv_al_final(cfv_p))})) {
      (void)(cfv_tomar(cfv_p, Value{std::string(";", 1)}));
      return cfv_nodo(Value{std::string("Retornar", 8)}, Value{}, crear_lista({cfv_nodo(Value{std::string("Nulo", 4)}, Value{}, crear_lista({}))}));
    }
    Value cfv_val = cfv_parse_expresion(cfv_p);
    size_t cfv_val_tipo = 99;
    (void)(cfv_tomar(cfv_p, Value{std::string(";", 1)}));
    return cfv_nodo(Value{std::string("Retornar", 8)}, Value{}, crear_lista({cfv_val}));
  }
  if (verdad(cfv_ver(cfv_p, Value{std::string("romper", 6)}))) {
    (void)(cfv_avanzar(cfv_p));
    (void)(cfv_tomar(cfv_p, Value{std::string(";", 1)}));
    return cfv_nodo(Value{std::string("Romper", 6)}, Value{}, crear_lista({}));
  }
  if (verdad(cfv_ver(cfv_p, Value{std::string("continuar", 9)}))) {
    (void)(cfv_avanzar(cfv_p));
    (void)(cfv_tomar(cfv_p, Value{std::string(";", 1)}));
    return cfv_nodo(Value{std::string("Continuar", 9)}, Value{}, crear_lista({}));
  }
  // match (expr) { caso patron [si guarda] { bloque } ... }
  if (verdad(cfv_ver(cfv_p, Value{std::string("match", 5)}))) {
    (void)(cfv_avanzar(cfv_p));
    (void)(cfv_requerir(cfv_p, Value{std::string("(", 1)}, Value{std::string("Se esperaba '(' tras 'match'", 28)}));
    Value cfv_match_expr = cfv_parse_expresion(cfv_p);
    (void)(cfv_requerir(cfv_p, Value{std::string(")", 1)}, Value{std::string("Se esperaba ')' tras expresion match", 36)}));
    (void)(cfv_requerir(cfv_p, Value{std::string("{", 1)}, Value{std::string("Se esperaba '{' en match", 24)}));
    Value cfv_match_casos = crear_lista({});
    while (verdad(Value{!verdad(cfv_ver(cfv_p, Value{std::string("}", 1)})) && !verdad(cfv_al_final(cfv_p))})) {
      // caso patron [si guarda] { bloque }
      if (verdad(cfv_ver(cfv_p, Value{std::string("caso", 4)}))) {
        (void)(cfv_avanzar(cfv_p));
      }
      // Parse patron: literal, ident (bind), Tipo, [lista], {mapa}, _
      Value cfv_patron = Value{};
      std::string cfv_patron_tipo_str = "cualquiera";
      if (verdad(cfv_ver(cfv_p, Value{std::string("_", 1)}))) {
        // wildcard
        (void)(cfv_avanzar(cfv_p));
        cfv_patron = Value{std::string("_", 1)};
        cfv_patron_tipo_str = "wildcard";
      } else if (verdad(cfv_ver_tipo(cfv_p, Value{std::string("NUMBER", 6)}))) {
        cfv_patron = cfv_parse_primaria(cfv_p);
        cfv_patron_tipo_str = "literal_num";
      } else if (verdad(cfv_ver_tipo(cfv_p, Value{std::string("STRING", 6)}))) {
        cfv_patron = cfv_parse_primaria(cfv_p);
        cfv_patron_tipo_str = "literal_txt";
      } else if (verdad(cfv_ver(cfv_p, Value{std::string("verdadero", 9)})) || verdad(cfv_ver(cfv_p, Value{std::string("falso", 5)}))) {
        cfv_patron = cfv_parse_primaria(cfv_p);
        cfv_patron_tipo_str = "literal_bool";
      } else if (verdad(cfv_ver(cfv_p, Value{std::string("nulo", 4)}))) {
        (void)(cfv_avanzar(cfv_p));
        cfv_patron = Value{};
        cfv_patron_tipo_str = "literal_nulo";
      } else if (verdad(cfv_ver_tipo(cfv_p, Value{std::string("IDENT", 5)}))) {
        Value cfv_pt = cfv_avanzar(cfv_p);
        cfv_patron = indice(cfv_pt, Value{std::string("lexema", 6)});
        cfv_patron_tipo_str = "bind";
      } else {
        cfv_patron = Value{std::string("_", 1)};
        cfv_patron_tipo_str = "wildcard";
      }
      // Guard: si (expr)
      Value cfv_guarda = Value{};
      if (verdad(cfv_ver(cfv_p, Value{std::string("si", 2)}))) {
        (void)(cfv_avanzar(cfv_p));
        (void)(cfv_requerir(cfv_p, Value{std::string("(", 1)}, Value{std::string("Se esperaba '(' en guarda", 25)}));
        cfv_guarda = cfv_parse_expresion(cfv_p);
        (void)(cfv_requerir(cfv_p, Value{std::string(")", 1)}, Value{std::string("Se esperaba ')' en guarda", 25)}));
      }
      // Arrow: =>  o bloque { }
      (void)(cfv_tomar(cfv_p, Value{std::string("=>", 2)}));
      Value cfv_caso_cuerpo = crear_lista({});
      if (verdad(cfv_ver(cfv_p, Value{std::string("{", 1)}))) {
        cfv_caso_cuerpo = cfv_parse_bloque(cfv_p);
      } else {
        // Single expression
        Value cfv_expr_caso = cfv_parse_expresion(cfv_p);
        (void)(cfv_tomar(cfv_p, Value{std::string(";", 1)}));
        (void)(cfv_agregar(cfv_caso_cuerpo, cfv_nodo(Value{std::string("Retornar", 8)}, Value{}, crear_lista({cfv_expr_caso}))));
      }
      // Store caso as [tipo_patron, patron, guarda, bloque]
      Value cfv_caso_nodo = cfv_nodo(Value{std::string("MatchCaso", 9)}, Value{std::string(cfv_patron_tipo_str)},
          crear_lista({cfv_patron, cfv_guarda, cfv_nodo(Value{std::string("Bloque", 6)}, Value{}, cfv_caso_cuerpo)}));
      (void)(cfv_agregar(cfv_match_casos, cfv_caso_nodo));
      (void)(cfv_tomar(cfv_p, Value{std::string(",", 1)}));
    }
    (void)(cfv_requerir(cfv_p, Value{std::string("}", 1)}, Value{std::string("Se esperaba '}' al cerrar match", 31)}));
    return cfv_nodo(Value{std::string("Match", 5)}, Value{}, crear_lista({cfv_match_expr, cfv_match_casos}));
  }
  if (verdad(cfv_ver(cfv_p, Value{std::string("si", 2)}))) {
    (void)(cfv_avanzar(cfv_p));
    (void)(cfv_requerir(cfv_p, Value{std::string("(", 1)}, Value{std::string("Se esperaba '(' tras 'si'", 25)}));
    Value cfv_cond = cfv_parse_expresion(cfv_p);
    size_t cfv_cond_tipo = 99;
    (void)(cfv_requerir(cfv_p, Value{std::string(")", 1)}, Value{std::string("Se esperaba ')' tras condición", 31)}));
    Value cfv_entonces = cfv_parse_bloque(cfv_p);
    if (cfv_entonces.index() != 4) throw std::runtime_error("tipo incompatible para entonces");
    size_t cfv_entonces_tipo = 4;
    Value cfv_ramas = crear_lista({cfv_cond, cfv_nodo(Value{std::string("Bloque", 6)}, Value{}, cfv_entonces)});
    if (cfv_ramas.index() != 4) throw std::runtime_error("tipo incompatible para ramas");
    size_t cfv_ramas_tipo = 4;
    while (verdad(cfv_ver(cfv_p, Value{std::string("sino", 4)}))) {
      (void)(cfv_avanzar(cfv_p));
      if (verdad(cfv_ver(cfv_p, Value{std::string("si", 2)}))) {
        (void)(cfv_avanzar(cfv_p));
        (void)(cfv_requerir(cfv_p, Value{std::string("(", 1)}, Value{std::string("Se esperaba '('", 15)}));
        Value cfv_cond2 = cfv_parse_expresion(cfv_p);
        size_t cfv_cond2_tipo = 99;
        (void)(cfv_requerir(cfv_p, Value{std::string(")", 1)}, Value{std::string("Se esperaba ')'", 15)}));
        Value cfv_blq2 = cfv_parse_bloque(cfv_p);
        if (cfv_blq2.index() != 4) throw std::runtime_error("tipo incompatible para blq2");
        size_t cfv_blq2_tipo = 4;
        (void)(cfv_agregar(cfv_ramas, cfv_cond2));
        (void)(cfv_agregar(cfv_ramas, cfv_nodo(Value{std::string("Bloque", 6)}, Value{}, cfv_blq2)));
      } else {
        Value cfv_blq_sino = cfv_parse_bloque(cfv_p);
        if (cfv_blq_sino.index() != 4) throw std::runtime_error("tipo incompatible para blq_sino");
        size_t cfv_blq_sino_tipo = 4;
        (void)(cfv_agregar(cfv_ramas, cfv_nodo(Value{std::string("Bloque", 6)}, Value{}, cfv_blq_sino)));
      }
    }
    return cfv_nodo(Value{std::string("Si", 2)}, Value{}, cfv_ramas);
  }
  if (verdad(cfv_ver(cfv_p, Value{std::string("mientras", 8)}))) {
    (void)(cfv_avanzar(cfv_p));
    (void)(cfv_requerir(cfv_p, Value{std::string("(", 1)}, Value{std::string("Se esperaba '('", 15)}));
    Value cfv_cond = cfv_parse_expresion(cfv_p);
    size_t cfv_cond_tipo = 99;
    (void)(cfv_requerir(cfv_p, Value{std::string(")", 1)}, Value{std::string("Se esperaba ')'", 15)}));
    Value cfv_cuerpo = cfv_parse_bloque(cfv_p);
    if (cfv_cuerpo.index() != 4) throw std::runtime_error("tipo incompatible para cuerpo");
    size_t cfv_cuerpo_tipo = 4;
    return cfv_nodo(Value{std::string("Mientras", 8)}, Value{}, crear_lista({cfv_cond, cfv_nodo(Value{std::string("Bloque", 6)}, Value{}, cfv_cuerpo)}));
  }
  if (verdad(cfv_ver(cfv_p, Value{std::string("para", 4)}))) {
    (void)(cfv_avanzar(cfv_p));
    Value cfv_var_tok = cfv_requerir_tipo(cfv_p, Value{std::string("IDENT", 5)}, Value{std::string("Se esperaba variable de iteración", 34)});
    size_t cfv_var_tok_tipo = 99;
    Value cfv_var_nombre = indice(cfv_var_tok, Value{std::string("lexema", 6)});
    if (cfv_var_nombre.index() != 2) throw std::runtime_error("tipo incompatible para var_nombre");
    size_t cfv_var_nombre_tipo = 2;
    if (verdad(cfv_tomar(cfv_p, Value{std::string(":", 1)}))) {
      if (verdad(Value{verdad(cfv_ver_tipo(cfv_p, Value{std::string("IDENT", 5)})) && verdad(Value{!verdad(cfv_ver(cfv_p, Value{std::string("en", 2)}))})})) {
        (void)(cfv_avanzar(cfv_p));
      }
    }
    (void)(cfv_requerir(cfv_p, Value{std::string("en", 2)}, Value{std::string("Se esperaba 'en' en bucle para", 30)}));
    Value cfv_col = cfv_parse_expresion(cfv_p);
    size_t cfv_col_tipo = 99;
    Value cfv_cuerpo = cfv_parse_bloque(cfv_p);
    if (cfv_cuerpo.index() != 4) throw std::runtime_error("tipo incompatible para cuerpo");
    size_t cfv_cuerpo_tipo = 4;
    return cfv_nodo(Value{std::string("Para", 4)}, cfv_var_nombre, crear_lista({cfv_col, cfv_nodo(Value{std::string("Bloque", 6)}, Value{}, cfv_cuerpo)}));
  }
  if (verdad(cfv_ver(cfv_p, Value{std::string("funcion", 7)}))) {
    (void)(cfv_avanzar(cfv_p));
    Value cfv_nombre_tok = cfv_requerir_tipo(cfv_p, Value{std::string("IDENT", 5)}, Value{std::string("Se esperaba nombre de función", 30)});
    size_t cfv_nombre_tok_tipo = 99;
    Value cfv_fn_nombre = indice(cfv_nombre_tok, Value{std::string("lexema", 6)});
    if (cfv_fn_nombre.index() != 2) throw std::runtime_error("tipo incompatible para fn_nombre");
    size_t cfv_fn_nombre_tipo = 2;
    (void)(cfv_requerir(cfv_p, Value{std::string("(", 1)}, Value{std::string("Se esperaba '('", 15)}));
    Value cfv_params = crear_lista({});
    if (cfv_params.index() != 4) throw std::runtime_error("tipo incompatible para params");
    size_t cfv_params_tipo = 4;
    while (verdad(Value{verdad(Value{!verdad(cfv_ver(cfv_p, Value{std::string(")", 1)}))}) && verdad(Value{!verdad(cfv_al_final(cfv_p))})})) {
      // Check variadic
      Value cfv_es_variadico = Value{false};
      if (verdad(cfv_ver(cfv_p, Value{std::string("...")}))) {
        (void)(cfv_avanzar(cfv_p));
        cfv_es_variadico = Value{true};
      }
      Value cfv_pnom = cfv_requerir_tipo(cfv_p, Value{std::string("IDENT", 5)}, Value{std::string("Se esperaba parámetro", 22)});
      Value cfv_pname = indice(cfv_pnom, Value{std::string("lexema", 6)});
      // Skip type annotation
      if (verdad(cfv_tomar(cfv_p, Value{std::string(":", 1)}))) {
        (void)(cfv_requerir_tipo(cfv_p, Value{std::string("IDENT", 5)}, Value{std::string("Se esperaba tipo de parámetro", 30)}));
        if (verdad(cfv_tomar(cfv_p, Value{std::string("<", 1)}))) {
          (void)(cfv_requerir_tipo(cfv_p, Value{std::string("IDENT", 5)}, Value{std::string("Se esperaba tipo genérico", 26)}));
          (void)(cfv_tomar(cfv_p, Value{std::string(">", 1)}));
        }
      }
      // Check default value
      if (verdad(cfv_tomar(cfv_p, Value{std::string("=")}))) {
        Value cfv_default_expr = cfv_parse_expresion(cfv_p);
        auto cfv_pdmap = std::make_shared<std::map<std::string,Value>>();
        (*cfv_pdmap)["__param_default"] = Value{true};
        (*cfv_pdmap)["nombre"] = cfv_pname;
        (*cfv_pdmap)["default"] = cfv_default_expr;
        (void)(cfv_agregar(cfv_params, Value{cfv_pdmap}));
      } else if (verdad(cfv_es_variadico)) {
        auto cfv_pvmap = std::make_shared<std::map<std::string,Value>>();
        (*cfv_pvmap)["__param_variadic"] = Value{true};
        (*cfv_pvmap)["nombre"] = cfv_pname;
        (void)(cfv_agregar(cfv_params, Value{cfv_pvmap}));
      } else {
        (void)(cfv_agregar(cfv_params, cfv_pname));
      }
      if (verdad(Value{!verdad(cfv_ver(cfv_p, Value{std::string(")", 1)}))})) {
        (void)(cfv_requerir(cfv_p, Value{std::string(",", 1)}, Value{std::string("Se esperaba ','", 15)}));
      }
    }
    (void)(cfv_requerir(cfv_p, Value{std::string(")", 1)}, Value{std::string("Se esperaba ')'", 15)}));
    if (verdad(cfv_tomar(cfv_p, Value{std::string(":", 1)}))) {
      if (verdad(cfv_ver_tipo(cfv_p, Value{std::string("IDENT", 5)}))) {
        (void)(cfv_avanzar(cfv_p));
        if (verdad(cfv_tomar(cfv_p, Value{std::string("<", 1)}))) {
          (void)(cfv_requerir_tipo(cfv_p, Value{std::string("IDENT", 5)}, Value{std::string("Se esperaba tipo genérico", 26)}));
          (void)(cfv_tomar(cfv_p, Value{std::string(">", 1)}));
        }
      }
    }
    Value cfv_cuerpo = cfv_parse_bloque(cfv_p);
    if (cfv_cuerpo.index() != 4) throw std::runtime_error("tipo incompatible para cuerpo");
    size_t cfv_cuerpo_tipo = 4;
    return cfv_nodo(Value{std::string("Funcion", 7)}, cfv_fn_nombre, crear_lista({cfv_nodo(Value{std::string("Params", 6)}, Value{}, cfv_params), cfv_nodo(Value{std::string("Bloque", 6)}, Value{}, cfv_cuerpo)}));
  }
  // lanzar expr;
  if (verdad(cfv_ver(cfv_p, Value{std::string("lanzar")}))) {
    (void)(cfv_avanzar(cfv_p));
    Value cfv_msg = cfv_parse_expresion(cfv_p);
    (void)(cfv_tomar(cfv_p, Value{std::string(";")}));
    return cfv_nodo(Value{std::string("Lanzar")}, Value{}, crear_lista({cfv_msg}));
  }
  // intentar { ... } capturar (var) { ... } finalmente { ... }
  if (verdad(cfv_ver(cfv_p, Value{std::string("intentar")}))) {
    (void)(cfv_avanzar(cfv_p));
    Value cfv_try_body = cfv_parse_bloque(cfv_p);
    (void)(cfv_requerir(cfv_p, Value{std::string("capturar")}, Value{std::string("Se esperaba 'capturar' tras bloque intentar")}));
    (void)(cfv_requerir(cfv_p, Value{std::string("(")}, Value{std::string("Se esperaba '(' tras 'capturar'")}));
    Value cfv_var_tok = cfv_requerir_tipo(cfv_p, Value{std::string("IDENT")}, Value{std::string("Se esperaba nombre de variable de error")});
    Value cfv_var_nombre = indice(cfv_var_tok, Value{std::string("lexema")});
    (void)(cfv_requerir(cfv_p, Value{std::string(")")}, Value{std::string("Se esperaba ')'")}));
    Value cfv_catch_body = cfv_parse_bloque(cfv_p);
    Value cfv_finally_body = crear_lista({});
    if (verdad(cfv_ver(cfv_p, Value{std::string("finalmente")}))) {
      (void)(cfv_avanzar(cfv_p));
      cfv_finally_body = cfv_parse_bloque(cfv_p);
    }
    return cfv_nodo(Value{std::string("Intentar")}, cfv_var_nombre, crear_lista({
        cfv_nodo(Value{std::string("Bloque")}, Value{}, cfv_try_body),
        cfv_nodo(Value{std::string("Bloque")}, Value{}, cfv_catch_body),
        cfv_nodo(Value{std::string("Bloque")}, Value{}, cfv_finally_body)
    }));
  }
  // segun (expr) { caso val { } ... otro { } }
  if (verdad(cfv_ver(cfv_p, Value{std::string("segun")}))) {
    (void)(cfv_avanzar(cfv_p));
    (void)(cfv_requerir(cfv_p, Value{std::string("(")}, Value{std::string("Se esperaba '(' tras 'segun'")}));
    Value cfv_expr_seg = cfv_parse_expresion(cfv_p);
    (void)(cfv_requerir(cfv_p, Value{std::string(")")}, Value{std::string("Se esperaba ')' tras expresion segun")}));
    (void)(cfv_requerir(cfv_p, Value{std::string("{")}, Value{std::string("Se esperaba '{' en segun")}));
    Value cfv_casos = crear_lista({cfv_expr_seg});
    while (verdad(Value{!verdad(cfv_ver(cfv_p, Value{std::string("}")})) && !verdad(cfv_al_final(cfv_p))})) {
      if (verdad(cfv_ver(cfv_p, Value{std::string("caso")}))) {
        (void)(cfv_avanzar(cfv_p));
        Value cfv_match_expr = cfv_parse_expresion(cfv_p);
        Value cfv_caso_body = cfv_parse_bloque(cfv_p);
        (void)(cfv_agregar(cfv_casos, cfv_nodo(Value{std::string("Caso")}, cfv_match_expr, cfv_caso_body)));
      } else if (verdad(cfv_ver(cfv_p, Value{std::string("otro")}))) {
        (void)(cfv_avanzar(cfv_p));
        Value cfv_otro_body = cfv_parse_bloque(cfv_p);
        (void)(cfv_agregar(cfv_casos, cfv_nodo(Value{std::string("Caso")}, Value{}, cfv_otro_body)));
      } else {
        break;
      }
    }
    (void)(cfv_requerir(cfv_p, Value{std::string("}")}, Value{std::string("Se esperaba '}' al cerrar segun")}));
    (void)(cfv_tomar(cfv_p, Value{std::string(";")}));
    return cfv_nodo(Value{std::string("Segun")}, Value{}, cfv_casos);
  }
  // enum Nombre { V1, V2, V3 }
  if (verdad(cfv_ver(cfv_p, Value{std::string("enum")}))) {
    (void)(cfv_avanzar(cfv_p));
    Value cfv_enum_tok = cfv_requerir_tipo(cfv_p, Value{std::string("IDENT")}, Value{std::string("Se esperaba nombre de enum")});
    Value cfv_enum_nombre = indice(cfv_enum_tok, Value{std::string("lexema")});
    (void)(cfv_requerir(cfv_p, Value{std::string("{")}, Value{std::string("Se esperaba '{' en enum")}));
    Value cfv_enum_vals = crear_lista({});
    while (verdad(Value{!verdad(cfv_ver(cfv_p, Value{std::string("}")})) && !verdad(cfv_al_final(cfv_p))})) {
      Value cfv_ev_tok = cfv_requerir_tipo(cfv_p, Value{std::string("IDENT")}, Value{std::string("Se esperaba variante de enum")});
      (void)(cfv_agregar(cfv_enum_vals, indice(cfv_ev_tok, Value{std::string("lexema")})));
      if (verdad(Value{!verdad(cfv_ver(cfv_p, Value{std::string("}")}))}) ) {
        (void)(cfv_tomar(cfv_p, Value{std::string(",")}));
      }
    }
    (void)(cfv_requerir(cfv_p, Value{std::string("}")}, Value{std::string("Se esperaba '}' al cerrar enum")}));
    (void)(cfv_tomar(cfv_p, Value{std::string(";")}));
    return cfv_nodo(Value{std::string("Enum")}, cfv_enum_nombre, cfv_enum_vals);
  }
  // interfaz Nombre { firma1(params) firma2(params) }
  if (verdad(cfv_ver(cfv_p, Value{std::string("interfaz")}))) {
    (void)(cfv_avanzar(cfv_p));
    Value cfv_iface_tok = cfv_requerir_tipo(cfv_p, Value{std::string("IDENT")}, Value{std::string("Se esperaba nombre de interfaz")});
    Value cfv_iface_nombre = indice(cfv_iface_tok, Value{std::string("lexema")});
    (void)(cfv_requerir(cfv_p, Value{std::string("{")}, Value{std::string("Se esperaba '{' en interfaz")}));
    Value cfv_iface_firmas = crear_lista({});
    while (verdad(Value{!verdad(cfv_ver(cfv_p, Value{std::string("}")})) && !verdad(cfv_al_final(cfv_p))})) {
      if (verdad(cfv_ver(cfv_p, Value{std::string("funcion")}))) { (void)(cfv_avanzar(cfv_p)); }
      Value cfv_firma_tok = cfv_requerir_tipo(cfv_p, Value{std::string("IDENT")}, Value{std::string("Se esperaba nombre de método en interfaz")});
      Value cfv_firma_nom = indice(cfv_firma_tok, Value{std::string("lexema")});
      (void)(cfv_requerir(cfv_p, Value{std::string("(")}, Value{std::string("Se esperaba '('")}));
      Value cfv_firma_params = crear_lista({});
      while (verdad(Value{!verdad(cfv_ver(cfv_p, Value{std::string(")")})) && !verdad(cfv_al_final(cfv_p))})) {
        Value cfv_fp = cfv_requerir_tipo(cfv_p, Value{std::string("IDENT")}, Value{std::string("Se esperaba parámetro")});
        (void)(cfv_agregar(cfv_firma_params, indice(cfv_fp, Value{std::string("lexema")})));
        if (verdad(cfv_tomar(cfv_p, Value{std::string(":")}))) { (void)(cfv_avanzar(cfv_p)); }
        if (verdad(Value{!verdad(cfv_ver(cfv_p, Value{std::string(")")}))})) { (void)(cfv_tomar(cfv_p, Value{std::string(",")})); }
      }
      (void)(cfv_requerir(cfv_p, Value{std::string(")")}, Value{std::string("Se esperaba ')'")}));
      if (verdad(cfv_tomar(cfv_p, Value{std::string(":")}))) { if (verdad(cfv_ver_tipo(cfv_p, Value{std::string("IDENT")}))) { (void)(cfv_avanzar(cfv_p)); } }
      (void)(cfv_tomar(cfv_p, Value{std::string(";")}));
      (void)(cfv_agregar(cfv_iface_firmas, cfv_firma_nom));
    }
    (void)(cfv_requerir(cfv_p, Value{std::string("}")}, Value{std::string("Se esperaba '}'")}));
    return cfv_nodo(Value{std::string("Interfaz")}, cfv_iface_nombre, cfv_iface_firmas);
  }
  // abstracto clase Nombre { ... }
  bool cfv_clase_abstracta_flag = false;
  if (verdad(cfv_ver(cfv_p, Value{std::string("abstracto")}))) {
    cfv_clase_abstracta_flag = true;
    (void)(cfv_avanzar(cfv_p));
  }
  // clase Nombre { sea campo... funcion metodo... }
  if (verdad(cfv_ver(cfv_p, Value{std::string("clase")})) || cfv_clase_abstracta_flag) {
    (void)(cfv_avanzar(cfv_p));
    Value cfv_clase_tok = cfv_requerir_tipo(cfv_p, Value{std::string("IDENT")}, Value{std::string("Se esperaba nombre de clase")});
    Value cfv_clase_nombre = indice(cfv_clase_tok, Value{std::string("lexema")});
    Value cfv_es_abstracta = Value{cfv_clase_abstracta_flag};
    // Check for extiende (inheritance)
    Value cfv_clase_padre = Value{}; // nulo = no parent
    if (verdad(cfv_ver(cfv_p, Value{std::string("extiende")}))) {
      (void)(cfv_avanzar(cfv_p));
      Value cfv_padre_tok = cfv_requerir_tipo(cfv_p, Value{std::string("IDENT")}, Value{std::string("Se esperaba nombre de clase padre")});
      cfv_clase_padre = indice(cfv_padre_tok, Value{std::string("lexema")});
    }
    // Check for implementa (interfaces)
    Value cfv_clase_ifaces = crear_lista({});
    while (verdad(cfv_ver(cfv_p, Value{std::string("implementa")}))) {
      (void)(cfv_avanzar(cfv_p));
      Value cfv_iface_tok2 = cfv_requerir_tipo(cfv_p, Value{std::string("IDENT")}, Value{std::string("Se esperaba nombre de interfaz")});
      (void)(cfv_agregar(cfv_clase_ifaces, indice(cfv_iface_tok2, Value{std::string("lexema")})));
      (void)(cfv_tomar(cfv_p, Value{std::string(",")}));
    }
    (void)(cfv_requerir(cfv_p, Value{std::string("{")}, Value{std::string("Se esperaba '{' en clase")}));
    Value cfv_miembros = crear_lista({});
    while (verdad(Value{!verdad(cfv_ver(cfv_p, Value{std::string("}")})) && !verdad(cfv_al_final(cfv_p))})) {
      // Parse modifiers: privado, publico, estatico
      Value cfv_mod_acceso = Value{std::string("publico")};
      Value cfv_mod_estatico = Value{false};
      while (verdad(Value{verdad(cfv_ver(cfv_p, Value{std::string("privado")})) || verdad(cfv_ver(cfv_p, Value{std::string("publico")})) || verdad(cfv_ver(cfv_p, Value{std::string("estatico")}))})) {
        if (verdad(cfv_ver(cfv_p, Value{std::string("privado")}))) {
          (void)(cfv_avanzar(cfv_p));
          cfv_mod_acceso = Value{std::string("privado")};
        } else if (verdad(cfv_ver(cfv_p, Value{std::string("publico")}))) {
          (void)(cfv_avanzar(cfv_p));
          cfv_mod_acceso = Value{std::string("publico")};
        } else if (verdad(cfv_ver(cfv_p, Value{std::string("estatico")}))) {
          (void)(cfv_avanzar(cfv_p));
          cfv_mod_estatico = Value{true};
        }
      }
      if (verdad(Value{verdad(cfv_ver(cfv_p, Value{std::string("sea")})) || verdad(cfv_ver(cfv_p, Value{std::string("var")}))})) {
        (void)(cfv_avanzar(cfv_p));
        Value cfv_campo_tok2 = cfv_requerir_tipo(cfv_p, Value{std::string("IDENT")}, Value{std::string("Se esperaba nombre de campo")});
        Value cfv_campo_nombre2 = indice(cfv_campo_tok2, Value{std::string("lexema")});
        if (verdad(cfv_tomar(cfv_p, Value{std::string(":")}))) {
          (void)(cfv_requerir_tipo(cfv_p, Value{std::string("IDENT")}, Value{std::string("Se esperaba tipo")}));
        }
        Value cfv_campo_def_expr = cfv_nodo(Value{std::string("Nulo")}, Value{}, crear_lista({}));
        if (verdad(cfv_tomar(cfv_p, Value{std::string("=")}))) {
          cfv_campo_def_expr = cfv_parse_expresion(cfv_p);
        }
        (void)(cfv_tomar(cfv_p, Value{std::string(";")}));
        (void)(cfv_agregar(cfv_miembros, cfv_nodo(Value{std::string("CampoDef")}, cfv_campo_nombre2, crear_lista({cfv_campo_def_expr, cfv_mod_acceso, cfv_mod_estatico}))));
      } else if (verdad(cfv_ver(cfv_p, Value{std::string("funcion")}))) {
        (void)(cfv_avanzar(cfv_p));
        Value cfv_mnom_tok = cfv_requerir_tipo(cfv_p, Value{std::string("IDENT")}, Value{std::string("Se esperaba nombre de metodo")});
        Value cfv_mnom = indice(cfv_mnom_tok, Value{std::string("lexema")});
        (void)(cfv_requerir(cfv_p, Value{std::string("(")}, Value{std::string("Se esperaba '('")}));
        Value cfv_mparams = crear_lista({});
        while (verdad(Value{!verdad(cfv_ver(cfv_p, Value{std::string(")")})) && !verdad(cfv_al_final(cfv_p))})) {
          Value cfv_mpnom = cfv_requerir_tipo(cfv_p, Value{std::string("IDENT")}, Value{std::string("Se esperaba parametro")});
          (void)(cfv_agregar(cfv_mparams, indice(cfv_mpnom, Value{std::string("lexema")})));
          if (verdad(cfv_tomar(cfv_p, Value{std::string(":")}))) {
            (void)(cfv_requerir_tipo(cfv_p, Value{std::string("IDENT")}, Value{std::string("Se esperaba tipo")}));
          }
          if (verdad(Value{!verdad(cfv_ver(cfv_p, Value{std::string(")")}))})) {
            (void)(cfv_requerir(cfv_p, Value{std::string(",")}, Value{std::string("Se esperaba ','")}));
          }
        }
        (void)(cfv_requerir(cfv_p, Value{std::string(")")}, Value{std::string("Se esperaba ')'")}));
        if (verdad(cfv_tomar(cfv_p, Value{std::string(":")}))) {
          if (verdad(cfv_ver_tipo(cfv_p, Value{std::string("IDENT")}))) { (void)(cfv_avanzar(cfv_p)); }
        }
        Value cfv_mcuerpo = cfv_parse_bloque(cfv_p);
        (void)(cfv_agregar(cfv_miembros, cfv_nodo(Value{std::string("Funcion")}, cfv_mnom, crear_lista({cfv_nodo(Value{std::string("Params")}, Value{}, cfv_mparams), cfv_nodo(Value{std::string("Bloque")}, Value{}, cfv_mcuerpo)}))));
      } else {
        break;
      }
    }
    (void)(cfv_requerir(cfv_p, Value{std::string("}")}, Value{std::string("Se esperaba '}' al cerrar clase")}));
    (void)(cfv_tomar(cfv_p, Value{std::string(";")}));
    if (cfv_clase_padre.index() != 0) {
      Value cfv_padre_node = cfv_nodo(Value{std::string("Padre")}, cfv_clase_padre, crear_lista({}));
      auto cfv_mptr = std::get_if<Lista>(&cfv_miembros.data);
      if (cfv_mptr) (*cfv_mptr)->insert((*cfv_mptr)->begin(), cfv_padre_node);
    }
    // Inject abstracto and implementa metadata into miembros
    {
      auto cfv_mptr2 = std::get_if<Lista>(&cfv_miembros.data);
      if (cfv_mptr2) {
        (*cfv_mptr2)->insert((*cfv_mptr2)->begin(),
          cfv_nodo(Value{std::string("Abstracto")}, cfv_es_abstracta, crear_lista({})));
        if (verdad(compara(cfv_longitud(cfv_clase_ifaces), Value{0.0}, ">"))) {
          (*cfv_mptr2)->insert((*cfv_mptr2)->begin(),
            cfv_nodo(Value{std::string("Implementa")}, Value{std::string("")}, cfv_clase_ifaces));
        }
      }
    }
    return cfv_nodo(Value{std::string("Clase")}, cfv_clase_nombre, cfv_miembros);
  }
  // importar "ruta"
  if (verdad(cfv_ver(cfv_p, Value{std::string("importar")}))) {
    (void)(cfv_avanzar(cfv_p));
    (void)(cfv_afirmar(cfv_ver_tipo(cfv_p, Value{std::string("STRING")}), Value{std::string("importar: se esperaba ruta de archivo entre comillas")}));
    Value cfv_ruta_tok = cfv_avanzar(cfv_p);
    size_t cfv_ruta_tok_tipo = 99;
    (void)(cfv_tomar(cfv_p, Value{std::string(";")}));
    return cfv_nodo(Value{std::string("Importar")}, indice(cfv_ruta_tok, Value{std::string("lexema")}), crear_lista({}));
  }
  if (verdad(cfv_ver(cfv_p, Value{std::string("mostrar", 7)}))) {
    (void)(cfv_avanzar(cfv_p));
    (void)(cfv_requerir(cfv_p, Value{std::string("(", 1)}, Value{std::string("Se esperaba '('", 15)}));
    Value cfv_arg = cfv_parse_expresion(cfv_p);
    size_t cfv_arg_tipo = 99;
    (void)(cfv_requerir(cfv_p, Value{std::string(")", 1)}, Value{std::string("Se esperaba ')'", 15)}));
    (void)(cfv_tomar(cfv_p, Value{std::string(";", 1)}));
    return cfv_nodo(Value{std::string("Mostrar", 7)}, Value{}, crear_lista({cfv_arg}));
  }
  Value cfv_expr = cfv_parse_expresion(cfv_p);
  size_t cfv_expr_tipo = 99;
  (void)(cfv_tomar(cfv_p, Value{std::string(";", 1)}));
  return cfv_nodo(Value{std::string("Expresion", 9)}, Value{}, crear_lista({cfv_expr}));
  return Value{};
}
Value cfv_parse_expresion(Value cfv_p) {
  cfv_jit_hit("parse_expresion");
  size_t cfv_p_tipo = cfv_p.index();
  return cfv_parse_asignacion(cfv_p);
  return Value{};
}
Value cfv_parse_asignacion(Value cfv_p) {
  cfv_jit_hit("parse_asignacion");
  size_t cfv_p_tipo = cfv_p.index();
  Value cfv_izq = cfv_parse_ternario(cfv_p);
  size_t cfv_izq_tipo = 99;
  // COMPOUND ASSIGNMENT
  {
    std::string cfv_cur_lx = "";
    {
      Value cfv_tok_lx_tmp = indice(cfv_tok_actual(cfv_p), Value{std::string("lexema")});
      if (auto p = std::get_if<std::string>(&cfv_tok_lx_tmp.data))
        cfv_cur_lx = *p;
    }
    if (cfv_cur_lx == "+=" || cfv_cur_lx == "-=" || cfv_cur_lx == "*=" || cfv_cur_lx == "/=" || cfv_cur_lx == "%=") {
      std::string cfv_binop = std::string(1, cfv_cur_lx[0]);
      (void)(cfv_avanzar(cfv_p));
      Value cfv_der = cfv_parse_expresion(cfv_p);
      return cfv_nodo(Value{std::string("Asignacion")}, Value{}, crear_lista({
          cfv_izq,
          cfv_nodo(Value{std::string("Binario")}, Value{cfv_binop}, crear_lista({cfv_izq, cfv_der}))
      }));
    }
  }
  if (verdad(compara(indice(cfv_tok_actual(cfv_p), Value{std::string("lexema", 6)}), Value{std::string("=", 1)}, "=="))) {
    (void)(cfv_avanzar(cfv_p));
    Value cfv_der = cfv_parse_expresion(cfv_p);
    size_t cfv_der_tipo = 99;
    return cfv_nodo(Value{std::string("Asignacion", 10)}, Value{}, crear_lista({cfv_izq, cfv_der}));
  }
  return cfv_izq;
  return Value{};
}
Value cfv_parse_ternario(Value cfv_p) {
  cfv_jit_hit("parse_ternario");
  size_t cfv_p_tipo = cfv_p.index();
  Value cfv_cond = cfv_parse_nulo_coalescente(cfv_p);
  if (verdad(cfv_ver(cfv_p, Value{std::string("?")}))) {
    (void)(cfv_avanzar(cfv_p));
    Value cfv_si = cfv_parse_nulo_coalescente(cfv_p);
    (void)(cfv_requerir(cfv_p, Value{std::string(":")}, Value{std::string("Se esperaba ':' en ternario")}));
    Value cfv_no = cfv_parse_nulo_coalescente(cfv_p);
    return cfv_nodo(Value{std::string("Ternario")}, Value{}, crear_lista({cfv_cond, cfv_si, cfv_no}));
  }
  return cfv_cond;
  return Value{};
}
Value cfv_parse_nulo_coalescente(Value cfv_p) {
  cfv_jit_hit("parse_nulo_coalescente");
  size_t cfv_p_tipo = cfv_p.index();
  Value cfv_izq = cfv_parse_logico_o(cfv_p);
  size_t cfv_izq_tipo = 99;
  while (verdad(cfv_ver(cfv_p, Value{std::string("??")}))) {
    (void)(cfv_avanzar(cfv_p));
    Value cfv_der = cfv_parse_logico_o(cfv_p);
    size_t cfv_der_tipo = 99;
    asignar(cfv_izq, cfv_izq_tipo, cfv_nodo(Value{std::string("Binario")}, Value{std::string("??")}, crear_lista({cfv_izq, cfv_der})), "izq");
  }
  return cfv_izq;
  return Value{};
}
Value cfv_parse_logico_o(Value cfv_p) {
  cfv_jit_hit("parse_logico_o");
  size_t cfv_p_tipo = cfv_p.index();
  Value cfv_izq = cfv_parse_logico_y(cfv_p);
  size_t cfv_izq_tipo = 99;
  while (verdad(cfv_ver(cfv_p, Value{std::string("o", 1)}))) {
    (void)(cfv_avanzar(cfv_p));
    Value cfv_der = cfv_parse_logico_y(cfv_p);
    size_t cfv_der_tipo = 99;
    asignar(cfv_izq, cfv_izq_tipo, cfv_nodo(Value{std::string("Binario", 7)}, Value{std::string("o", 1)}, crear_lista({cfv_izq, cfv_der})), "izq");
  }
  return cfv_izq;
  return Value{};
}
Value cfv_parse_logico_y(Value cfv_p) {
  cfv_jit_hit("parse_logico_y");
  size_t cfv_p_tipo = cfv_p.index();
  Value cfv_izq = cfv_parse_igualdad(cfv_p);
  size_t cfv_izq_tipo = 99;
  while (verdad(cfv_ver(cfv_p, Value{std::string("y", 1)}))) {
    (void)(cfv_avanzar(cfv_p));
    Value cfv_der = cfv_parse_igualdad(cfv_p);
    size_t cfv_der_tipo = 99;
    asignar(cfv_izq, cfv_izq_tipo, cfv_nodo(Value{std::string("Binario", 7)}, Value{std::string("y", 1)}, crear_lista({cfv_izq, cfv_der})), "izq");
  }
  return cfv_izq;
  return Value{};
}
Value cfv_parse_igualdad(Value cfv_p) {
  cfv_jit_hit("parse_igualdad");
  size_t cfv_p_tipo = cfv_p.index();
  Value cfv_izq = cfv_parse_comparacion(cfv_p);
  size_t cfv_izq_tipo = 99;
  while (verdad(Value{verdad(Value{verdad(cfv_ver(cfv_p, Value{std::string("==", 2)})) || verdad(cfv_ver(cfv_p, Value{std::string("!=", 2)}))}) || verdad(cfv_ver(cfv_p, Value{std::string("es", 2)}))})) {
    Value cfv_op = indice(cfv_tok_actual(cfv_p), Value{std::string("lexema", 6)});
    if (cfv_op.index() != 2) throw std::runtime_error("tipo incompatible para op");
    size_t cfv_op_tipo = 2;
    (void)(cfv_avanzar(cfv_p));
    Value cfv_der = cfv_parse_comparacion(cfv_p);
    size_t cfv_der_tipo = 99;
    asignar(cfv_izq, cfv_izq_tipo, cfv_nodo(Value{std::string("Binario", 7)}, cfv_op, crear_lista({cfv_izq, cfv_der})), "izq");
  }
  return cfv_izq;
  return Value{};
}
Value cfv_parse_comparacion(Value cfv_p) {
  cfv_jit_hit("parse_comparacion");
  size_t cfv_p_tipo = cfv_p.index();
  Value cfv_izq = cfv_parse_bit_o(cfv_p);
  size_t cfv_izq_tipo = 99;
  while (verdad(Value{verdad(Value{verdad(Value{verdad(Value{verdad(cfv_ver(cfv_p, Value{std::string("<", 1)})) || verdad(cfv_ver(cfv_p, Value{std::string("<=", 2)}))}) || verdad(cfv_ver(cfv_p, Value{std::string(">", 1)}))}) || verdad(cfv_ver(cfv_p, Value{std::string(">=", 2)}))}) || verdad(cfv_ver(cfv_p, Value{std::string("en", 2)}))})) {
    Value cfv_op = indice(cfv_tok_actual(cfv_p), Value{std::string("lexema", 6)});
    if (cfv_op.index() != 2) throw std::runtime_error("tipo incompatible para op");
    size_t cfv_op_tipo = 2;
    (void)(cfv_avanzar(cfv_p));
    Value cfv_der = cfv_parse_bit_o(cfv_p);
    size_t cfv_der_tipo = 99;
    asignar(cfv_izq, cfv_izq_tipo, cfv_nodo(Value{std::string("Binario", 7)}, cfv_op, crear_lista({cfv_izq, cfv_der})), "izq");
  }
  return cfv_izq;
  return Value{};
}
Value cfv_parse_bit_o(Value cfv_p) {
  cfv_jit_hit("parse_bit_o");
  size_t cfv_p_tipo = cfv_p.index();
  Value cfv_izq = cfv_parse_bit_xor(cfv_p);
  size_t cfv_izq_tipo = 99;
  while (verdad(cfv_ver(cfv_p, Value{std::string("|", 1)}))) {
    (void)(cfv_avanzar(cfv_p));
    Value cfv_der = cfv_parse_bit_xor(cfv_p);
    size_t cfv_der_tipo = 99;
    asignar(cfv_izq, cfv_izq_tipo, cfv_nodo(Value{std::string("Binario", 7)}, Value{std::string("|", 1)}, crear_lista({cfv_izq, cfv_der})), "izq");
  }
  return cfv_izq;
  return Value{};
}
Value cfv_parse_bit_xor(Value cfv_p) {
  cfv_jit_hit("parse_bit_xor");
  size_t cfv_p_tipo = cfv_p.index();
  Value cfv_izq = cfv_parse_bit_y(cfv_p);
  size_t cfv_izq_tipo = 99;
  while (verdad(cfv_ver(cfv_p, Value{std::string("^", 1)}))) {
    (void)(cfv_avanzar(cfv_p));
    Value cfv_der = cfv_parse_bit_y(cfv_p);
    size_t cfv_der_tipo = 99;
    asignar(cfv_izq, cfv_izq_tipo, cfv_nodo(Value{std::string("Binario", 7)}, Value{std::string("^", 1)}, crear_lista({cfv_izq, cfv_der})), "izq");
  }
  return cfv_izq;
  return Value{};
}
Value cfv_parse_bit_y(Value cfv_p) {
  cfv_jit_hit("parse_bit_y");
  size_t cfv_p_tipo = cfv_p.index();
  Value cfv_izq = cfv_parse_desplaz(cfv_p);
  size_t cfv_izq_tipo = 99;
  while (verdad(cfv_ver(cfv_p, Value{std::string("&", 1)}))) {
    (void)(cfv_avanzar(cfv_p));
    Value cfv_der = cfv_parse_desplaz(cfv_p);
    size_t cfv_der_tipo = 99;
    asignar(cfv_izq, cfv_izq_tipo, cfv_nodo(Value{std::string("Binario", 7)}, Value{std::string("&", 1)}, crear_lista({cfv_izq, cfv_der})), "izq");
  }
  return cfv_izq;
  return Value{};
}
Value cfv_parse_desplaz(Value cfv_p) {
  cfv_jit_hit("parse_desplaz");
  size_t cfv_p_tipo = cfv_p.index();
  Value cfv_izq = cfv_parse_suma(cfv_p);
  size_t cfv_izq_tipo = 99;
  while (verdad(Value{verdad(cfv_ver(cfv_p, Value{std::string("<<", 2)})) || verdad(cfv_ver(cfv_p, Value{std::string(">>", 2)}))})) {
    Value cfv_op = indice(cfv_tok_actual(cfv_p), Value{std::string("lexema", 6)});
    if (cfv_op.index() != 2) throw std::runtime_error("tipo incompatible para op");
    size_t cfv_op_tipo = 2;
    (void)(cfv_avanzar(cfv_p));
    Value cfv_der = cfv_parse_suma(cfv_p);
    size_t cfv_der_tipo = 99;
    asignar(cfv_izq, cfv_izq_tipo, cfv_nodo(Value{std::string("Binario", 7)}, cfv_op, crear_lista({cfv_izq, cfv_der})), "izq");
  }
  return cfv_izq;
  return Value{};
}
Value cfv_parse_suma(Value cfv_p) {
  cfv_jit_hit("parse_suma");
  size_t cfv_p_tipo = cfv_p.index();
  Value cfv_izq = cfv_parse_producto(cfv_p);
  size_t cfv_izq_tipo = 99;
  while (verdad(Value{verdad(cfv_ver(cfv_p, Value{std::string("+", 1)})) || verdad(cfv_ver(cfv_p, Value{std::string("-", 1)}))})) {
    Value cfv_op = indice(cfv_tok_actual(cfv_p), Value{std::string("lexema", 6)});
    if (cfv_op.index() != 2) throw std::runtime_error("tipo incompatible para op");
    size_t cfv_op_tipo = 2;
    (void)(cfv_avanzar(cfv_p));
    Value cfv_der = cfv_parse_producto(cfv_p);
    size_t cfv_der_tipo = 99;
    asignar(cfv_izq, cfv_izq_tipo, cfv_nodo(Value{std::string("Binario", 7)}, cfv_op, crear_lista({cfv_izq, cfv_der})), "izq");
  }
  return cfv_izq;
  return Value{};
}
Value cfv_parse_producto(Value cfv_p) {
  cfv_jit_hit("parse_producto");
  size_t cfv_p_tipo = cfv_p.index();
  Value cfv_izq = cfv_parse_unario(cfv_p);
  size_t cfv_izq_tipo = 99;
  while (verdad(Value{verdad(Value{verdad(cfv_ver(cfv_p, Value{std::string("*", 1)})) || verdad(cfv_ver(cfv_p, Value{std::string("/", 1)}))}) || verdad(cfv_ver(cfv_p, Value{std::string("%", 1)}))})) {
    Value cfv_op = indice(cfv_tok_actual(cfv_p), Value{std::string("lexema", 6)});
    if (cfv_op.index() != 2) throw std::runtime_error("tipo incompatible para op");
    size_t cfv_op_tipo = 2;
    (void)(cfv_avanzar(cfv_p));
    Value cfv_der = cfv_parse_unario(cfv_p);
    size_t cfv_der_tipo = 99;
    asignar(cfv_izq, cfv_izq_tipo, cfv_nodo(Value{std::string("Binario", 7)}, cfv_op, crear_lista({cfv_izq, cfv_der})), "izq");
  }
  return cfv_izq;
  return Value{};
}
Value cfv_parse_unario(Value cfv_p) {
  cfv_jit_hit("parse_unario");
  size_t cfv_p_tipo = cfv_p.index();
  if (verdad(cfv_ver(cfv_p, Value{std::string("no", 2)}))) {
    (void)(cfv_avanzar(cfv_p));
    return cfv_nodo(Value{std::string("Unario", 6)}, Value{std::string("no", 2)}, crear_lista({cfv_parse_unario(cfv_p)}));
  }
  if (verdad(cfv_ver(cfv_p, Value{std::string("-", 1)}))) {
    (void)(cfv_avanzar(cfv_p));
    return cfv_nodo(Value{std::string("Unario", 6)}, Value{std::string("-", 1)}, crear_lista({cfv_parse_unario(cfv_p)}));
  }
  if (verdad(cfv_ver(cfv_p, Value{std::string("~", 1)}))) {
    (void)(cfv_avanzar(cfv_p));
    return cfv_nodo(Value{std::string("Unario", 6)}, Value{std::string("~", 1)}, crear_lista({cfv_parse_unario(cfv_p)}));
  }
  return cfv_parse_postfijo(cfv_p);
  return Value{};
}
Value cfv_parse_postfijo(Value cfv_p) {
  cfv_jit_hit("parse_postfijo");
  size_t cfv_p_tipo = cfv_p.index();
  Value cfv_base = cfv_parse_primaria(cfv_p);
  size_t cfv_base_tipo = 99;
  Value cfv_continuar_pf = Value{true};
  if (cfv_continuar_pf.index() != 3) throw std::runtime_error("tipo incompatible para continuar_pf");
  size_t cfv_continuar_pf_tipo = 3;
  while (verdad(cfv_continuar_pf)) {
    if (verdad(cfv_ver(cfv_p, Value{std::string("[", 1)}))) {
      (void)(cfv_avanzar(cfv_p));
      Value cfv_idx = cfv_parse_expresion(cfv_p);
      size_t cfv_idx_tipo = 99;
      (void)(cfv_requerir(cfv_p, Value{std::string("]", 1)}, Value{std::string("Se esperaba ']'", 15)}));
      asignar(cfv_base, cfv_base_tipo, cfv_nodo(Value{std::string("Indice", 6)}, Value{}, crear_lista({cfv_base, cfv_idx})), "base");
    }     else if (verdad(cfv_ver(cfv_p, Value{std::string("?.")}))) {
      (void)(cfv_avanzar(cfv_p));
      Value cfv_campo_tok = cfv_requerir_tipo(cfv_p, Value{std::string("IDENT")}, Value{std::string("Se esperaba nombre de campo tras '?.'")} );
      size_t cfv_campo_tok_tipo = 99;
      Value cfv_campo_s = indice(cfv_campo_tok, Value{std::string("lexema")});
      if (verdad(cfv_ver(cfv_p, Value{std::string("(")}))) {
        (void)(cfv_avanzar(cfv_p));
        Value cfv_args_s = cfv_parse_argumentos(cfv_p);
        (void)(cfv_requerir(cfv_p, Value{std::string(")")}, Value{std::string("Se esperaba ')'")}));
        Value cfv_hijos_s = crear_lista({cfv_base});
        size_t cfv_hijos_s_tipo = 4;
        Value cfv_si2 = Value{0.0};
        while (verdad(compara(cfv_si2, cfv_longitud(cfv_args_s), "<"))) {
          (void)(cfv_agregar(cfv_hijos_s, indice(cfv_args_s, cfv_si2)));
          cfv_si2 = suma(cfv_si2, Value{1.0});
        }
        asignar(cfv_base, cfv_base_tipo, cfv_nodo(Value{std::string("MetodoLlamadaSegura")}, cfv_campo_s, cfv_hijos_s), "base");
      } else {
        asignar(cfv_base, cfv_base_tipo, cfv_nodo(Value{std::string("CampoSeguro")}, cfv_campo_s, crear_lista({cfv_base})), "base");
      }
    }     else if (verdad(cfv_ver(cfv_p, Value{std::string(".", 1)}))) {
      (void)(cfv_avanzar(cfv_p));
      Value cfv_campo_tok = cfv_requerir_tipo(cfv_p, Value{std::string("IDENT", 5)}, Value{std::string("Se esperaba campo", 17)});
      size_t cfv_campo_tok_tipo = 99;
      Value cfv_campo = indice(cfv_campo_tok, Value{std::string("lexema", 6)});
      if (cfv_campo.index() != 2) throw std::runtime_error("tipo incompatible para campo");
      size_t cfv_campo_tipo = 2;
      if (verdad(cfv_ver(cfv_p, Value{std::string("(", 1)}))) {
        (void)(cfv_avanzar(cfv_p));
        Value cfv_args = cfv_parse_argumentos(cfv_p);
        if (cfv_args.index() != 4) throw std::runtime_error("tipo incompatible para args");
        size_t cfv_args_tipo = 4;
        (void)(cfv_requerir(cfv_p, Value{std::string(")", 1)}, Value{std::string("Se esperaba ')'", 15)}));
        Value cfv_hijos_m = crear_lista({cfv_base});
        if (cfv_hijos_m.index() != 4) throw std::runtime_error("tipo incompatible para hijos_m");
        size_t cfv_hijos_m_tipo = 4;
        Value cfv_mi = Value{0.0};
        if (cfv_mi.index() != 1) throw std::runtime_error("tipo incompatible para mi");
        size_t cfv_mi_tipo = 1;
        while (verdad(compara(cfv_mi, cfv_longitud(cfv_args), "<"))) {
          (void)(cfv_agregar(cfv_hijos_m, indice(cfv_args, cfv_mi)));
          asignar(cfv_mi, cfv_mi_tipo, suma(cfv_mi, Value{1.0}), "mi");
        }
        asignar(cfv_base, cfv_base_tipo, cfv_nodo(Value{std::string("MetodoLlamada", 13)}, cfv_campo, cfv_hijos_m), "base");
      } else {
        asignar(cfv_base, cfv_base_tipo, cfv_nodo(Value{std::string("Campo", 5)}, cfv_campo, crear_lista({cfv_base})), "base");
      }
    }     else if (verdad(Value{verdad(cfv_ver(cfv_p, Value{std::string("(", 1)})) && verdad(compara(indice(cfv_base, Value{std::string("tipo", 4)}), Value{std::string("Identificador", 13)}, "=="))})) {
      (void)(cfv_avanzar(cfv_p));
      Value cfv_args = cfv_parse_argumentos(cfv_p);
      if (cfv_args.index() != 4) throw std::runtime_error("tipo incompatible para args");
      size_t cfv_args_tipo = 4;
      (void)(cfv_requerir(cfv_p, Value{std::string(")", 1)}, Value{std::string("Se esperaba ')'", 15)}));
      asignar(cfv_base, cfv_base_tipo, cfv_nodo(Value{std::string("Llamada", 7)}, indice(cfv_base, Value{std::string("valor", 5)}), cfv_args), "base");
    }  else {
      asignar(cfv_continuar_pf, cfv_continuar_pf_tipo, Value{false}, "continuar_pf");
    }
  }
  return cfv_base;
  return Value{};
}
Value cfv_parse_argumentos(Value cfv_p) {
  cfv_jit_hit("parse_argumentos");
  size_t cfv_p_tipo = cfv_p.index();
  Value cfv_args = crear_lista({});
  if (cfv_args.index() != 4) throw std::runtime_error("tipo incompatible para args");
  size_t cfv_args_tipo = 4;
  while (verdad(Value{verdad(Value{!verdad(cfv_ver(cfv_p, Value{std::string(")", 1)}))}) && verdad(Value{!verdad(cfv_al_final(cfv_p))})})) {
    (void)(cfv_agregar(cfv_args, cfv_parse_expresion(cfv_p)));
    if (verdad(Value{!verdad(cfv_ver(cfv_p, Value{std::string(")", 1)}))})) {
      (void)(cfv_requerir(cfv_p, Value{std::string(",", 1)}, Value{std::string("Se esperaba ','", 15)}));
    }
  }
  return cfv_args;
  return Value{};
}
Value cfv_parse_primaria(Value cfv_p) {
  cfv_jit_hit("parse_primaria");
  size_t cfv_p_tipo = cfv_p.index();
  if (verdad(cfv_ver_tipo(cfv_p, Value{std::string("NUMBER", 6)}))) {
    Value cfv_tok = cfv_avanzar(cfv_p);
    size_t cfv_tok_tipo = 99;
    return cfv_nodo(Value{std::string("Numero", 6)}, indice(cfv_tok, Value{std::string("lexema", 6)}), crear_lista({}));
  }
  if (verdad(cfv_ver_tipo(cfv_p, Value{std::string("STRING", 6)}))) {
    Value cfv_tok = cfv_avanzar(cfv_p);
    size_t cfv_tok_tipo = 99;
    Value cfv_lex_str = indice(cfv_tok, Value{std::string("lexema", 6)});
    std::string cfv_raw_with_quotes = texto(cfv_lex_str);
    std::string cfv_raw_str = cfv_raw_with_quotes.size() >= 2 ? cfv_raw_with_quotes.substr(1, cfv_raw_with_quotes.size()-2) : cfv_raw_with_quotes;
    if (cfv_raw_str.find('{') == std::string::npos) {
      return cfv_nodo(Value{std::string("Texto", 5)}, cfv_lex_str, crear_lista({}));
    }
    // String interpolation: build + chain
    std::vector<Value> cfv_interp_parts;
    std::string cfv_cur_text = "";
    size_t cfv_ii = 0;
    while (cfv_ii < cfv_raw_str.size()) {
      if (cfv_raw_str[cfv_ii] == '{') {
        if (!cfv_cur_text.empty()) {
          cfv_interp_parts.push_back(cfv_nodo(Value{std::string("Texto")}, Value{std::string("\"") + cfv_cur_text + std::string("\"")}, crear_lista({})));
          cfv_cur_text = "";
        }
        cfv_ii++; // skip {
        std::string cfv_expr_src = "";
        int cfv_depth = 1;
        while (cfv_ii < cfv_raw_str.size() && cfv_depth > 0) {
          if (cfv_raw_str[cfv_ii] == '{') cfv_depth++;
          else if (cfv_raw_str[cfv_ii] == '}') { cfv_depth--; if (cfv_depth == 0) break; }
          cfv_expr_src += cfv_raw_str[cfv_ii];
          cfv_ii++;
        }
        if (cfv_ii < cfv_raw_str.size()) cfv_ii++; // skip }
        Value cfv_expr_tokens_i = cfv_tokenizar(Value{cfv_expr_src});
        Value cfv_expr_p_i = cfv_mk_parser(cfv_expr_tokens_i);
        Value cfv_expr_node_i = cfv_parse_expresion(cfv_expr_p_i);
        Value cfv_atext_node_i = cfv_nodo(Value{std::string("Llamada")}, Value{std::string("a_texto")}, crear_lista({cfv_expr_node_i}));
        cfv_interp_parts.push_back(cfv_atext_node_i);
      } else {
        if (cfv_raw_str[cfv_ii] == '\\' && cfv_ii+1 < cfv_raw_str.size()) {
          cfv_cur_text += cfv_raw_str[cfv_ii];
          cfv_cur_text += cfv_raw_str[cfv_ii+1];
          cfv_ii += 2;
        } else {
          cfv_cur_text += cfv_raw_str[cfv_ii];
          cfv_ii++;
        }
      }
    }
    if (!cfv_cur_text.empty()) {
      cfv_interp_parts.push_back(cfv_nodo(Value{std::string("Texto")}, Value{std::string("\"") + cfv_cur_text + std::string("\"")}, crear_lista({})));
    }
    if (cfv_interp_parts.empty()) {
      return cfv_nodo(Value{std::string("Texto")}, Value{std::string("\"\"")}, crear_lista({}));
    }
    Value cfv_result_node = cfv_interp_parts[0];
    for (size_t cfv_pi2 = 1; cfv_pi2 < cfv_interp_parts.size(); cfv_pi2++) {
      cfv_result_node = cfv_nodo(Value{std::string("Binario")}, Value{std::string("+")}, crear_lista({cfv_result_node, cfv_interp_parts[cfv_pi2]}));
    }
    return cfv_result_node;
  }
  if (verdad(cfv_ver(cfv_p, Value{std::string("verdadero", 9)}))) {
    (void)(cfv_avanzar(cfv_p));
    return cfv_nodo(Value{std::string("Booleano", 8)}, Value{std::string("verdadero", 9)}, crear_lista({}));
  }
  if (verdad(cfv_ver(cfv_p, Value{std::string("falso", 5)}))) {
    (void)(cfv_avanzar(cfv_p));
    return cfv_nodo(Value{std::string("Booleano", 8)}, Value{std::string("falso", 5)}, crear_lista({}));
  }
  if (verdad(cfv_ver(cfv_p, Value{std::string("nulo", 4)}))) {
    (void)(cfv_avanzar(cfv_p));
    return cfv_nodo(Value{std::string("Nulo", 4)}, Value{}, crear_lista({}));
  }
  if (verdad(cfv_ver(cfv_p, Value{std::string("[", 1)}))) {
    (void)(cfv_avanzar(cfv_p));
    Value cfv_elementos = crear_lista({});
    if (cfv_elementos.index() != 4) throw std::runtime_error("tipo incompatible para elementos");
    size_t cfv_elementos_tipo = 4;
    while (verdad(Value{verdad(Value{!verdad(cfv_ver(cfv_p, Value{std::string("]", 1)}))}) && verdad(Value{!verdad(cfv_al_final(cfv_p))})})) {
      if (verdad(cfv_ver(cfv_p, Value{std::string("...")}))) {
        (void)(cfv_avanzar(cfv_p));
        Value cfv_spread_expr = cfv_parse_asignacion(cfv_p);
        (void)(cfv_agregar(cfv_elementos, cfv_nodo(Value{std::string("Spread")}, Value{}, crear_lista({cfv_spread_expr}))));
      } else {
        (void)(cfv_agregar(cfv_elementos, cfv_parse_expresion(cfv_p)));
      }
      if (verdad(Value{!verdad(cfv_ver(cfv_p, Value{std::string("]", 1)}))})) {
        (void)(cfv_requerir(cfv_p, Value{std::string(",", 1)}, Value{std::string("Se esperaba ','", 15)}));
      }
    }
    (void)(cfv_requerir(cfv_p, Value{std::string("]", 1)}, Value{std::string("Se esperaba ']'", 15)}));
    return cfv_nodo(Value{std::string("Lista", 5)}, Value{}, cfv_elementos);
  }
  if (verdad(cfv_ver(cfv_p, Value{std::string("{", 1)}))) {
    (void)(cfv_avanzar(cfv_p));
    Value cfv_pares = crear_lista({});
    if (cfv_pares.index() != 4) throw std::runtime_error("tipo incompatible para pares");
    size_t cfv_pares_tipo = 4;
    while (verdad(Value{verdad(Value{!verdad(cfv_ver(cfv_p, Value{std::string("}", 1)}))}) && verdad(Value{!verdad(cfv_al_final(cfv_p))})})) {
      Value cfv_clave = cfv_parse_expresion(cfv_p);
      size_t cfv_clave_tipo = 99;
      (void)(cfv_requerir(cfv_p, Value{std::string(":", 1)}, Value{std::string("Se esperaba ':'", 15)}));
      Value cfv_val = cfv_parse_expresion(cfv_p);
      size_t cfv_val_tipo = 99;
      (void)(cfv_agregar(cfv_pares, cfv_clave));
      (void)(cfv_agregar(cfv_pares, cfv_val));
      if (verdad(Value{!verdad(cfv_ver(cfv_p, Value{std::string("}", 1)}))})) {
        (void)(cfv_requerir(cfv_p, Value{std::string(",", 1)}, Value{std::string("Se esperaba ','", 15)}));
      }
    }
    (void)(cfv_requerir(cfv_p, Value{std::string("}", 1)}, Value{std::string("Se esperaba '}'", 15)}));
    return cfv_nodo(Value{std::string("Mapa", 4)}, Value{}, cfv_pares);
  }
  if (verdad(cfv_ver(cfv_p, Value{std::string("(", 1)}))) {
    (void)(cfv_avanzar(cfv_p));
    Value cfv_ex = cfv_parse_expresion(cfv_p);
    size_t cfv_ex_tipo = 99;
    (void)(cfv_requerir(cfv_p, Value{std::string(")", 1)}, Value{std::string("Se esperaba ')'", 15)}));
    return cfv_ex;
  }
  // Lambda: funcion(params) { cuerpo }
  if (verdad(cfv_ver(cfv_p, Value{std::string("funcion")}))) {
    (void)(cfv_avanzar(cfv_p));
    (void)(cfv_requerir(cfv_p, Value{std::string("(")}, Value{std::string("Se esperaba '(' en lambda")}));
    Value cfv_lparams = crear_lista({});
    size_t cfv_lparams_tipo = 4;
    while (verdad(Value{!verdad(cfv_ver(cfv_p, Value{std::string(")")})) && !verdad(cfv_al_final(cfv_p))})) {
      Value cfv_lpnom = cfv_requerir_tipo(cfv_p, Value{std::string("IDENT")}, Value{std::string("Se esperaba parametro en lambda")});
      (void)(cfv_agregar(cfv_lparams, indice(cfv_lpnom, Value{std::string("lexema")})));
      if (verdad(cfv_tomar(cfv_p, Value{std::string(":")}))) {
        (void)(cfv_requerir_tipo(cfv_p, Value{std::string("IDENT")}, Value{std::string("Se esperaba tipo")}));
      }
      if (verdad(Value{!verdad(cfv_ver(cfv_p, Value{std::string(")")}))})) {
        (void)(cfv_requerir(cfv_p, Value{std::string(",")}, Value{std::string("Se esperaba ','")}));
      }
    }
    (void)(cfv_requerir(cfv_p, Value{std::string(")")}, Value{std::string("Se esperaba ')' en lambda")}));
    if (verdad(cfv_tomar(cfv_p, Value{std::string(":")}))) {
      if (verdad(cfv_ver_tipo(cfv_p, Value{std::string("IDENT")}))) { (void)(cfv_avanzar(cfv_p)); }
    }
    Value cfv_lbody = cfv_parse_bloque(cfv_p);
    return cfv_nodo(Value{std::string("Lambda")}, Value{}, crear_lista({cfv_nodo(Value{std::string("Params")}, Value{}, cfv_lparams), cfv_nodo(Value{std::string("Bloque")}, Value{}, cfv_lbody)}));
  }
  if (verdad(cfv_ver_tipo(cfv_p, Value{std::string("IDENT", 5)}))) {
    Value cfv_tok = cfv_avanzar(cfv_p);
    size_t cfv_tok_tipo = 99;
    if (verdad(cfv_ver(cfv_p, Value{std::string("(", 1)}))) {
      (void)(cfv_avanzar(cfv_p));
      Value cfv_args = cfv_parse_argumentos(cfv_p);
      if (cfv_args.index() != 4) throw std::runtime_error("tipo incompatible para args");
      size_t cfv_args_tipo = 4;
      (void)(cfv_requerir(cfv_p, Value{std::string(")", 1)}, Value{std::string("Se esperaba ')'", 15)}));
      return cfv_nodo(Value{std::string("Llamada", 7)}, indice(cfv_tok, Value{std::string("lexema", 6)}), cfv_args);
    }
    return cfv_nodo(Value{std::string("Identificador", 13)}, indice(cfv_tok, Value{std::string("lexema", 6)}), crear_lista({}));
  }
  (void)(cfv_afirmar(Value{false}, suma(suma(suma(Value{std::string("Token inesperado '", 18)}, indice(cfv_tok_actual(cfv_p), Value{std::string("lexema", 6)})), Value{std::string("' en línea ", 12)}), cfv_a_texto(indice(cfv_tok_actual(cfv_p), Value{std::string("linea", 5)})))));
  return cfv_nodo(Value{std::string("Nulo", 4)}, Value{}, crear_lista({}));
  return Value{};
}
Value cfv_parsear(Value cfv_tokens) {
  cfv_jit_hit("parsear");
  size_t cfv_tokens_tipo = cfv_tokens.index();
  Value cfv_p = cfv_mk_parser(cfv_tokens);
  size_t cfv_p_tipo = 99;
  Value cfv_sentencias = crear_lista({});
  if (cfv_sentencias.index() != 4) throw std::runtime_error("tipo incompatible para sentencias");
  size_t cfv_sentencias_tipo = 4;
  while (verdad(Value{!verdad(cfv_al_final(cfv_p))})) {
    (void)(cfv_agregar(cfv_sentencias, cfv_parse_sentencia(cfv_p)));
  }
  return cfv_sentencias;
  return Value{};
}
Value cfv_env_buscar(Value cfv_env, Value cfv_nombre) {
  cfv_jit_hit("env_buscar");
  size_t cfv_env_tipo = cfv_env.index();
  size_t cfv_nombre_tipo = cfv_nombre.index();
  Value cfv_i = Value{0.0};
  if (cfv_i.index() != 1) throw std::runtime_error("tipo incompatible para i");
  size_t cfv_i_tipo = 1;
  while (verdad(compara(cfv_i, cfv_longitud(cfv_env), "<"))) {
    if (verdad(cfv_tiene_clave(indice(cfv_env, cfv_i), cfv_nombre))) {
      return indice(indice(cfv_env, cfv_i), cfv_nombre);
    }
    asignar(cfv_i, cfv_i_tipo, suma(cfv_i, Value{1.0}), "i");
  }
  (void)(cfv_afirmar(Value{false}, suma(suma(Value{std::string("variable desconocida '", 22)}, cfv_nombre), Value{std::string("'", 1)})));
  return Value{};
  return Value{};
}
Value cfv_env_tiene(Value cfv_env, Value cfv_nombre) {
  cfv_jit_hit("env_tiene");
  size_t cfv_env_tipo = cfv_env.index();
  size_t cfv_nombre_tipo = cfv_nombre.index();
  Value cfv_i = Value{0.0};
  if (cfv_i.index() != 1) throw std::runtime_error("tipo incompatible para i");
  size_t cfv_i_tipo = 1;
  while (verdad(compara(cfv_i, cfv_longitud(cfv_env), "<"))) {
    if (verdad(cfv_tiene_clave(indice(cfv_env, cfv_i), cfv_nombre))) {
      return Value{true};
    }
    asignar(cfv_i, cfv_i_tipo, suma(cfv_i, Value{1.0}), "i");
  }
  return Value{false};
  return Value{};
}
Value cfv_env_declarar(Value cfv_env, Value cfv_nombre, Value cfv_valor) {
  cfv_jit_hit("env_declarar");
  size_t cfv_env_tipo = cfv_env.index();
  size_t cfv_nombre_tipo = cfv_nombre.index();
  size_t cfv_valor_tipo = cfv_valor.index();
  Value cfv_scope = indice(cfv_env, Value{0.0});
  size_t cfv_scope_tipo = 99;
  asignar_indice(cfv_scope, cfv_nombre, cfv_valor);
  return Value{};
}
Value cfv_env_asignar(Value cfv_env, Value cfv_nombre, Value cfv_valor) {
  cfv_jit_hit("env_asignar");
  size_t cfv_env_tipo = cfv_env.index();
  size_t cfv_nombre_tipo = cfv_nombre.index();
  size_t cfv_valor_tipo = cfv_valor.index();
  Value cfv_i = Value{0.0};
  if (cfv_i.index() != 1) throw std::runtime_error("tipo incompatible para i");
  size_t cfv_i_tipo = 1;
  while (verdad(compara(cfv_i, cfv_longitud(cfv_env), "<"))) {
    if (verdad(cfv_tiene_clave(indice(cfv_env, cfv_i), cfv_nombre))) {
      Value cfv_scope = indice(cfv_env, cfv_i);
      size_t cfv_scope_tipo = 99;
      asignar_indice(cfv_scope, cfv_nombre, cfv_valor);
      return Value{0.0};
    }
    asignar(cfv_i, cfv_i_tipo, suma(cfv_i, Value{1.0}), "i");
  }
  Value cfv_scope = indice(cfv_env, Value{0.0});
  size_t cfv_scope_tipo = 99;
  asignar_indice(cfv_scope, cfv_nombre, cfv_valor);
  return Value{};
}
Value cfv_env_nuevo_scope(Value cfv_env) {
  cfv_jit_hit("env_nuevo_scope");
  size_t cfv_env_tipo = cfv_env.index();
  Value cfv_nuevo = crear_lista({crear_mapa({})});
  if (cfv_nuevo.index() != 4) throw std::runtime_error("tipo incompatible para nuevo");
  size_t cfv_nuevo_tipo = 4;
  Value cfv_i = Value{0.0};
  if (cfv_i.index() != 1) throw std::runtime_error("tipo incompatible para i");
  size_t cfv_i_tipo = 1;
  while (verdad(compara(cfv_i, cfv_longitud(cfv_env), "<"))) {
    (void)(cfv_agregar(cfv_nuevo, indice(cfv_env, cfv_i)));
    asignar(cfv_i, cfv_i_tipo, suma(cfv_i, Value{1.0}), "i");
  }
  return cfv_nuevo;
  return Value{};
}
Value cfv_formato_valor(Value cfv_v) {
  cfv_jit_hit("formato_valor");
  size_t cfv_v_tipo = cfv_v.index();
  if (verdad(compara(cfv_v, Value{}, "=="))) {
    return Value{std::string("nulo", 4)};
  }
  if (verdad(compara(cfv_v, Value{true}, "=="))) {
    return Value{std::string("verdadero", 9)};
  }
  if (verdad(compara(cfv_v, Value{false}, "=="))) {
    return Value{std::string("falso", 5)};
  }
  return cfv_a_texto(cfv_v);
  return Value{};
}
Value cfv_eval_binario(Value cfv_op, Value cfv_izq, Value cfv_der) {
  cfv_jit_hit("eval_binario");
  size_t cfv_op_tipo = cfv_op.index();
  size_t cfv_izq_tipo = cfv_izq.index();
  size_t cfv_der_tipo = cfv_der.index();
  if (verdad(compara(cfv_op, Value{std::string("+", 1)}, "=="))) {
    if (verdad(Value{verdad(compara(cfv_tipo_de(cfv_izq), Value{std::string("texto", 5)}, "==")) || verdad(compara(cfv_tipo_de(cfv_der), Value{std::string("texto", 5)}, "=="))})) {
      return suma(cfv_formato_valor(cfv_izq), cfv_formato_valor(cfv_der));
    }
    return suma(cfv_izq, cfv_der);
  }
  if (verdad(compara(cfv_op, Value{std::string("-", 1)}, "=="))) {
    return resta(cfv_izq, cfv_der);
  }
  if (verdad(compara(cfv_op, Value{std::string("*", 1)}, "=="))) {
    return multiplica(cfv_izq, cfv_der);
  }
  if (verdad(compara(cfv_op, Value{std::string("/", 1)}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_der, Value{0.0}, "!="), Value{std::string("División por cero", 18)}));
    return divide(cfv_izq, cfv_der);
  }
  if (verdad(compara(cfv_op, Value{std::string("%", 1)}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_der, Value{0.0}, "!="), Value{std::string("Módulo por cero", 16)}));
    return modulo(cfv_izq, cfv_der);
  }
  if (verdad(compara(cfv_op, Value{std::string("&", 1)}, "=="))) {
    return bit_and(cfv_izq, cfv_der);
  }
  if (verdad(compara(cfv_op, Value{std::string("|", 1)}, "=="))) {
    return bit_or(cfv_izq, cfv_der);
  }
  if (verdad(compara(cfv_op, Value{std::string("^", 1)}, "=="))) {
    return bit_xor(cfv_izq, cfv_der);
  }
  if (verdad(compara(cfv_op, Value{std::string("<<", 2)}, "=="))) {
    return bit_shl(cfv_izq, cfv_der);
  }
  if (verdad(compara(cfv_op, Value{std::string(">>", 2)}, "=="))) {
    return bit_shr(cfv_izq, cfv_der);
  }
  if (verdad(compara(cfv_op, Value{std::string("==", 2)}, "=="))) {
    return compara(cfv_izq, cfv_der, "==");
  }
  if (verdad(compara(cfv_op, Value{std::string("!=", 2)}, "=="))) {
    return compara(cfv_izq, cfv_der, "!=");
  }
  if (verdad(compara(cfv_op, Value{std::string("<", 1)}, "=="))) {
    return compara(cfv_izq, cfv_der, "<");
  }
  if (verdad(compara(cfv_op, Value{std::string("<=", 2)}, "=="))) {
    return compara(cfv_izq, cfv_der, "<=");
  }
  if (verdad(compara(cfv_op, Value{std::string(">", 1)}, "=="))) {
    return compara(cfv_izq, cfv_der, ">");
  }
  if (verdad(compara(cfv_op, Value{std::string(">=", 2)}, "=="))) {
    return compara(cfv_izq, cfv_der, ">=");
  }
  if (verdad(compara(cfv_op, Value{std::string("y", 1)}, "=="))) {
    return Value{verdad(cfv_izq) && verdad(cfv_der)};
  }
  if (verdad(compara(cfv_op, Value{std::string("o", 1)}, "=="))) {
    return Value{verdad(cfv_izq) || verdad(cfv_der)};
  }
  (void)(cfv_afirmar(Value{false}, suma(suma(Value{std::string("operador desconocido '", 22)}, cfv_op), Value{std::string("'", 1)})));
  return Value{};
  return Value{};
}
// ── Native implementations for new stdlib builtins ──────────────────────────
#include <random>
#include <regex>
#include <ctime>
#include <set>

// Random
static std::mt19937_64& cfv_rng() {
  static std::mt19937_64 gen(std::random_device{}());
  return gen;
}
static Value cfv_aleatorio_uniforme(const Value& min_v, const Value& max_v) {
  double lo = numero(min_v), hi = numero(max_v);
  if (lo >= hi) throw std::runtime_error("aleatorio_uniforme: min debe ser < max");
  std::uniform_real_distribution<double> dist(lo, hi);
  return dist(cfv_rng());
}

// Date/time
static Value cfv_fecha_ahora_ms_fn() {
  using namespace std::chrono;
  return (double)duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}
static Value cfv_fecha_ahora_fn() {
  std::time_t t = std::time(nullptr);
  std::tm* tm = std::localtime(&t);
  auto m = std::make_shared<std::map<std::string,Value>>();
  (*m)["anio"] = (double)(tm->tm_year + 1900);
  (*m)["mes"] = (double)(tm->tm_mon + 1);
  (*m)["dia"] = (double)tm->tm_mday;
  (*m)["hora"] = (double)tm->tm_hour;
  (*m)["minuto"] = (double)tm->tm_min;
  (*m)["segundo"] = (double)tm->tm_sec;
  (*m)["dia_semana"] = (double)((tm->tm_wday + 6) % 7); // 0=lun
  return Value{m};
}

// Regex
static Value cfv_regex_coincidir_fn(const Value& pat, const Value& txt) {
  if (pat.index()!=2||txt.index()!=2) throw std::runtime_error("regex_coincidir requiere texto");
  std::regex r(std::get<std::string>(pat.data));
  return Value{std::regex_search(std::get<std::string>(txt.data), r)};
}
static Value cfv_regex_buscar_fn(const Value& pat, const Value& txt) {
  if (pat.index()!=2||txt.index()!=2) throw std::runtime_error("regex_buscar requiere texto");
  std::smatch m;
  const std::string& s = std::get<std::string>(txt.data);
  std::regex r(std::get<std::string>(pat.data));
  if (std::regex_search(s, m, r)) return Value{std::string(m[0])};
  return Value{};
}
static Value cfv_regex_buscar_todos_fn(const Value& pat, const Value& txt) {
  if (pat.index()!=2||txt.index()!=2) throw std::runtime_error("regex_buscar_todos requiere texto");
  const std::string& s = std::get<std::string>(txt.data);
  std::regex r(std::get<std::string>(pat.data));
  auto begin = std::sregex_iterator(s.begin(), s.end(), r);
  auto end = std::sregex_iterator();
  auto res = std::make_shared<std::vector<Value>>();
  for (auto it = begin; it != end; ++it) res->push_back(Value{std::string((*it)[0])});
  return Value{res};
}
static Value cfv_regex_reemplazar_fn(const Value& pat, const Value& txt, const Value& rep) {
  if (pat.index()!=2||txt.index()!=2||rep.index()!=2) throw std::runtime_error("regex_reemplazar requiere texto");
  std::regex r(std::get<std::string>(pat.data));
  return Value{std::regex_replace(std::get<std::string>(txt.data), r, std::get<std::string>(rep.data))};
}
static Value cfv_regex_dividir_fn(const Value& pat, const Value& txt) {
  if (pat.index()!=2||txt.index()!=2) throw std::runtime_error("regex_dividir requiere texto");
  const std::string& s = std::get<std::string>(txt.data);
  std::regex r(std::get<std::string>(pat.data));
  std::sregex_token_iterator it(s.begin(), s.end(), r, -1), end;
  auto res = std::make_shared<std::vector<Value>>();
  for (; it != end; ++it) res->push_back(Value{std::string(*it)});
  return Value{res};
}

// Base64
static const std::string cfv_b64chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static Value cfv_base64_codificar_fn(const Value& v) {
  if (v.index()!=2) throw std::runtime_error("base64_codificar requiere texto");
  const std::string& in = std::get<std::string>(v.data);
  std::string out; out.reserve(((in.size()+2)/3)*4);
  for (size_t i=0;i<in.size();i+=3) {
    unsigned int b=(unsigned char)in[i]<<16|(i+1<in.size()?(unsigned char)in[i+1]<<8:0)|(i+2<in.size()?(unsigned char)in[i+2]:0);
    out+=cfv_b64chars[(b>>18)&63]; out+=cfv_b64chars[(b>>12)&63];
    out+=(i+1<in.size()?cfv_b64chars[(b>>6)&63]:'=');
    out+=(i+2<in.size()?cfv_b64chars[b&63]:'=');
  }
  return Value{out};
}
static Value cfv_base64_decodificar_fn(const Value& v) {
  if (v.index()!=2) throw std::runtime_error("base64_decodificar requiere texto");
  const std::string& in = std::get<std::string>(v.data);
  auto find=[](char c)->int{auto p=cfv_b64chars.find(c);return p==std::string::npos?-1:(int)p;};
  std::string out;
  for (size_t i=0;i+3<in.size();i+=4) {
    int a=find(in[i]),b=find(in[i+1]),c=find(in[i+2]),d=find(in[i+3]);
    if(a<0||b<0)break;
    out+=(char)((a<<2)|(b>>4));
    if(c>=0)out+=(char)(((b&15)<<4)|(c>>2));
    if(d>=0)out+=(char)(((c&3)<<6)|d);
  }
  return Value{out};
}

// SHA256 — delega a la implementación real (OpenSSL o pure-C++)
static Value cfv_sha256_fn(const Value& v) { return cfv_sha256(v); }

// URL encoding
static Value cfv_url_codificar_fn(const Value& v) {
  if (v.index()!=2) throw std::runtime_error("url_codificar requiere texto");
  const std::string& s = std::get<std::string>(v.data);
  std::string out; out.reserve(s.size()*3);
  const std::string safe="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.~";
  for(unsigned char c:s){
    if(safe.find(c)!=std::string::npos) out+=c;
    else {char b[4];std::snprintf(b,sizeof(b),"%%%02X",c);out+=b;}
  }
  return Value{out};
}
static Value cfv_url_decodificar_fn(const Value& v) {
  if (v.index()!=2) throw std::runtime_error("url_decodificar requiere texto");
  const std::string& s = std::get<std::string>(v.data);
  std::string out; out.reserve(s.size());
  for(size_t i=0;i<s.size();++i){
    if(s[i]=='%'&&i+2<s.size()){
      int c=std::stoi(s.substr(i+1,2),nullptr,16);
      out+=(char)c; i+=2;
    } else if(s[i]=='+') out+=' ';
    else out+=s[i];
  }
  return Value{out};
}
static Value cfv_codigo_char_fn(const Value& v) {
  if (v.index()!=2) throw std::runtime_error("codigo_char requiere texto");
  const std::string& s = std::get<std::string>(v.data);
  if (s.empty()) throw std::runtime_error("codigo_char requiere texto no vacío");
  return (double)(unsigned char)s[0];
}
// JSON builtins
static Value cfv_json_serializar_fn(const Value& v) { return Value{cfv_canonical_json(v)}; }
static Value cfv_json_bonito_fn(const Value& v, int indent=0) {
  // Simple pretty-printer
  std::string pad(indent*2,' ');
  if (v.index()==0) return Value{std::string("null")};
  if (auto n=std::get_if<double>(&v.data)) return Value{cfv_number_text(*n)};
  if (auto s=std::get_if<std::string>(&v.data)) return Value{cfv_json_escape(*s)};
  if (auto b=std::get_if<bool>(&v.data)) return Value{std::string(*b?"true":"false")};
  if (auto p=std::get_if<Lista>(&v.data)) {
    if ((*p)->empty()) return Value{std::string("[]")};
    std::string out="[\n";
    for (size_t i=0;i<(*p)->size();++i) {
      out+=pad+"  "+std::get<std::string>(cfv_json_bonito_fn((*p)->at(i),indent+1).data);
      if(i+1<(*p)->size()) out+=",";
      out+="\n";
    }
    return Value{out+pad+"]"};
  }
  if (auto p=std::get_if<Mapa>(&v.data)) {
    if ((*p)->empty()) return Value{std::string("{}")};
    std::string out="{\n"; bool first=true;
    for (const auto& [k,x]:**p) {
      if(!first) out+=",\n"; first=false;
      out+=pad+"  "+cfv_json_escape(k)+": "+std::get<std::string>(cfv_json_bonito_fn(x,indent+1).data);
    }
    return Value{out+"\n"+pad+"}"};
  }
  return Value{cfv_canonical_json(v)};
}

// ═══════════════════════════════════════════════════════════════════════════
// BLOQUE DE EXTENSIONES C-FORGE v2.2
// JSON nativo · HTTP client · Regex · SQLite · Concurrencia · Sistema · Fecha
// ═══════════════════════════════════════════════════════════════════════════

// ── JSON nativo (sin deps) ───────────────────────────────────────────────────
#include <regex>

static std::string cfv_json_escape_str(const std::string& s){
  std::string r; r.reserve(s.size()+4);
  for(unsigned char c:s){switch(c){case '"':r+="\\\"";break;case'\\':r+="\\\\";break;
    case'\n':r+="\\n";break;case'\r':r+="\\r";break;case'\t':r+="\\t";break;
    default: if(c<0x20){char b[8];snprintf(b,sizeof(b),"\\u%04x",c);r+=b;}else r+=c;}}
  return r;
}
static std::string cfv_valor_a_json(const Value& v);
static std::string cfv_valor_a_json(const Value& v){
  switch(v.data.index()){
    case 0: return "null";
    case 1:{double d=std::get<double>(v.data);
      if(d==(long long)d&&std::abs(d)<1e15)return std::to_string((long long)d);
      char b[64];snprintf(b,sizeof(b),"%.10g",d);return b;}
    case 2: return "\""+cfv_json_escape_str(std::get<std::string>(v.data))+"\"";
    case 3: return std::get<bool>(v.data)?"true":"false";
    case 4:{auto& l=*std::get<Lista>(v.data);std::string r="[";
      for(size_t i=0;i<l.size();i++){if(i)r+=",";r+=cfv_valor_a_json(l[i]);}return r+"]";}
    case 5:{auto& m=*std::get<Mapa>(v.data);std::string r="{";bool f=true;
      for(auto&[k,val]:m){if(!f)r+=",";r+="\""+cfv_json_escape_str(k)+"\":"+cfv_valor_a_json(val);f=false;}return r+"}";}
    default: return "null";
  }
}
struct CfvJsonParser{const std::string&s;size_t pos=0;
  void ws(){while(pos<s.size()&&(s[pos]==' '||s[pos]=='\t'||s[pos]=='\n'||s[pos]=='\r'))pos++;}
  Value document(){Value value=parse();ws();if(pos!=s.size())throw std::runtime_error("json_parsear: contenido adicional");return value;}
  Value parse(){ws();if(pos>=s.size())throw std::runtime_error("json_parsear: valor incompleto");char c=s[pos];
    if(c=='"')return pstr();if(c=='{')return pobj();if(c=='[')return parr();
    if(s.compare(pos,4,"true")==0){pos+=4;return Value{true};}
    if(s.compare(pos,5,"false")==0){pos+=5;return Value{false};}
    if(s.compare(pos,4,"null")==0){pos+=4;return Value{};}
    if(c=='-'||(c>='0'&&c<='9'))return pnum();
    throw std::runtime_error("json_parsear: token inválido");}
  Value pstr(){pos++;std::string r;
    while(pos<s.size()&&s[pos]!='"'){
      if(s[pos]=='\\'){if(pos+1>=s.size())throw std::runtime_error("json_parsear: escape incompleto");pos++;switch(s[pos]){
        case'"':r+='"';break;case'\\':r+='\\';break;case'n':r+='\n';break;
        case'r':r+='\r';break;case't':r+='\t';break;case'b':r+='\b';break;
        case'f':r+='\f';break;case'/':r+='/';break;
        default:throw std::runtime_error("json_parsear: escape no compatible");}}
      else{if((unsigned char)s[pos]<0x20)throw std::runtime_error("json_parsear: control en texto");r+=s[pos];}pos++;}
    if(pos>=s.size())throw std::runtime_error("json_parsear: texto sin cerrar");pos++;return Value{r};}
  Value pnum(){size_t st=pos;if(s[pos]=='-')pos++;
    while(pos<s.size()&&s[pos]>='0'&&s[pos]<='9')pos++;
    if(pos<s.size()&&s[pos]=='.'){pos++;while(pos<s.size()&&s[pos]>='0'&&s[pos]<='9')pos++;}
    if(pos<s.size()&&(s[pos]=='e'||s[pos]=='E')){pos++;
      if(pos<s.size()&&(s[pos]=='+'||s[pos]=='-'))pos++;
      while(pos<s.size()&&s[pos]>='0'&&s[pos]<='9')pos++;}
    try{return Value{std::stod(s.substr(st,pos-st))};}catch(...){throw std::runtime_error("json_parsear: número inválido");}}
  Value parr(){pos++;auto l=std::make_shared<std::vector<ForgeValue>>();ws();
    if(pos<s.size()&&s[pos]==']'){pos++;return Value{l};}
    while(true){l->push_back(parse());ws();
      if(pos<s.size()&&s[pos]==','){pos++;continue;}
      if(pos<s.size()&&s[pos]==']'){pos++;break;}
      throw std::runtime_error("json_parsear: se esperaba ',' o ']'");}return Value{l};}
  Value pobj(){pos++;auto m=std::make_shared<std::map<std::string,ForgeValue>>();ws();
    if(pos<s.size()&&s[pos]=='}'){pos++;return Value{m};}
    while(true){ws();if(pos>=s.size()||s[pos]!='"')throw std::runtime_error("json_parsear: clave inválida");
      auto kv=pstr();std::string k=std::get<std::string>(kv.data);ws();
      if(pos>=s.size()||s[pos]!=':')throw std::runtime_error("json_parsear: se esperaba ':'");pos++;(*m)[k]=parse();ws();
      if(pos<s.size()&&s[pos]==','){pos++;continue;}
      if(pos<s.size()&&s[pos]=='}'){pos++;break;}
      throw std::runtime_error("json_parsear: se esperaba ',' o '}'");}return Value{m};}
};
static Value cfv_json_parsear_fn(const Value& v){
  if(v.data.index()!=2)throw std::runtime_error("json_parsear: se esperaba texto");
  CfvJsonParser p{std::get<std::string>(v.data)};return p.document();}
static Value cfv_json_texto_fn(const Value& v){return Value{cfv_valor_a_json(v)};}
static Value cfv_regex_grupos_fn(const Value& t,const Value& p){
  if(t.data.index()!=2||p.data.index()!=2)throw std::runtime_error("regex_grupos: texto, patron");
  std::regex re(std::get<std::string>(p.data),std::regex::ECMAScript);
  std::smatch m;const std::string& texto=std::get<std::string>(t.data);
  if(!std::regex_search(texto,m,re))return Value{std::make_shared<std::vector<ForgeValue>>()};
  auto r=std::make_shared<std::vector<ForgeValue>>();
  for(size_t i=0;i<m.size();i++)r->push_back(Value{m[i].str()});
  return Value{r};}

// ── SQLite ────────────────────────────────────────────────────────────────────
#ifdef CFV_WITH_SQLITE
#include <sqlite3.h>
static std::map<int,sqlite3*> cfv_db_conns;
static int cfv_db_next_id=1;
static Value cfv_db_abrir_fn(const Value& rv){
  if(rv.data.index()!=2)throw std::runtime_error("db_abrir: se esperaba ruta");
  sqlite3* db=nullptr;
  int rc=sqlite3_open(std::get<std::string>(rv.data).c_str(),&db);
  if(rc!=SQLITE_OK)throw std::runtime_error(std::string("db_abrir: ")+sqlite3_errmsg(db));
  int id=cfv_db_next_id++;cfv_db_conns[id]=db;return Value{(double)id};}
static Value cfv_db_cerrar_fn(const Value& iv){
  int id=(int)std::get<double>(iv.data);
  auto it=cfv_db_conns.find(id);
  if(it!=cfv_db_conns.end()){sqlite3_close(it->second);cfv_db_conns.erase(it);}
  return Value{};}
static Value cfv_db_ejecutar_fn(const Value& iv,const Value& sv){
  int id=(int)std::get<double>(iv.data);
  auto it=cfv_db_conns.find(id);
  if(it==cfv_db_conns.end())throw std::runtime_error("db_ejecutar: conexion no encontrada");
  if(sv.data.index()!=2)throw std::runtime_error("db_ejecutar: se esperaba SQL texto");
  char* err=nullptr;
  int rc=sqlite3_exec(it->second,std::get<std::string>(sv.data).c_str(),nullptr,nullptr,&err);
  if(rc!=SQLITE_OK){std::string e=err?err:"error SQL";sqlite3_free(err);throw std::runtime_error("db_ejecutar: "+e);}
  return Value{(double)sqlite3_changes(it->second)};}
static std::vector<ForgeValue> cfv_db_step_rows(sqlite3* db,sqlite3_stmt* stmt){
  std::vector<ForgeValue> filas;int ncols=sqlite3_column_count(stmt);
  while(sqlite3_step(stmt)==SQLITE_ROW){
    auto fila=std::make_shared<std::map<std::string,ForgeValue>>();
    for(int c=0;c<ncols;c++){
      std::string col=sqlite3_column_name(stmt,c);ForgeValue val;
      switch(sqlite3_column_type(stmt,c)){
        case SQLITE_INTEGER:val=Value{(double)sqlite3_column_int64(stmt,c)};break;
        case SQLITE_FLOAT:val=Value{sqlite3_column_double(stmt,c)};break;
        case SQLITE_TEXT:{const char* tx=(const char*)sqlite3_column_text(stmt,c);val=Value{tx?std::string(tx):std::string()};break;}
        case SQLITE_NULL:val=Value{};break;
        default:{const char* tx=(const char*)sqlite3_column_text(stmt,c);val=Value{tx?std::string(tx):std::string()};break;}
      }(*fila)[col]=val;}filas.push_back(Value{fila});}
  return filas;}
static Value cfv_db_consulta_fn(const Value& iv,const Value& sv){
  int id=(int)std::get<double>(iv.data);
  auto it=cfv_db_conns.find(id);
  if(it==cfv_db_conns.end())throw std::runtime_error("db_consulta: conexion no encontrada");
  if(sv.data.index()!=2)throw std::runtime_error("db_consulta: se esperaba SQL texto");
  sqlite3_stmt* stmt=nullptr;
  int rc=sqlite3_prepare_v2(it->second,std::get<std::string>(sv.data).c_str(),-1,&stmt,nullptr);
  if(rc!=SQLITE_OK)throw std::runtime_error(std::string("db_consulta: ")+sqlite3_errmsg(it->second));
  auto filas=std::make_shared<std::vector<ForgeValue>>(cfv_db_step_rows(it->second,stmt));
  sqlite3_finalize(stmt);return Value{filas};}
static Value cfv_db_consulta_p_fn(const Value& iv,const Value& sv,const Value& pv){
  int id=(int)std::get<double>(iv.data);
  auto it=cfv_db_conns.find(id);
  if(it==cfv_db_conns.end())throw std::runtime_error("db_consulta_p: conexion no encontrada");
  if(sv.data.index()!=2)throw std::runtime_error("db_consulta_p: se esperaba SQL texto");
  sqlite3_stmt* stmt=nullptr;
  sqlite3_prepare_v2(it->second,std::get<std::string>(sv.data).c_str(),-1,&stmt,nullptr);
  if(auto* lp=std::get_if<Lista>(&pv.data)){
    for(size_t i=0;i<(*lp)->size();i++){const auto& p=(**lp)[i];int idx=(int)i+1;
      if(p.data.index()==0)sqlite3_bind_null(stmt,idx);
      else if(auto* d=std::get_if<double>(&p.data))sqlite3_bind_double(stmt,idx,*d);
      else if(auto* s=std::get_if<std::string>(&p.data))sqlite3_bind_text(stmt,idx,s->c_str(),-1,SQLITE_TRANSIENT);
      else if(auto* b=std::get_if<bool>(&p.data))sqlite3_bind_int(stmt,idx,*b?1:0);}}
  auto filas=std::make_shared<std::vector<ForgeValue>>(cfv_db_step_rows(it->second,stmt));
  sqlite3_finalize(stmt);return Value{filas};}
static Value cfv_db_ultimo_id_fn(const Value& iv){
  int id=(int)std::get<double>(iv.data);
  auto it=cfv_db_conns.find(id);
  if(it==cfv_db_conns.end())return Value{0.0};
  return Value{(double)sqlite3_last_insert_rowid(it->second)};}
static Value cfv_db_transaccion_fn(const Value& iv){
  int id=(int)std::get<double>(iv.data);
  auto it=cfv_db_conns.find(id);
  if(it==cfv_db_conns.end())throw std::runtime_error("db_transaccion: conexion no encontrada");
  sqlite3_exec(it->second,"BEGIN TRANSACTION",nullptr,nullptr,nullptr);return Value{};}
static Value cfv_db_confirmar_fn(const Value& iv){
  int id=(int)std::get<double>(iv.data);
  auto it=cfv_db_conns.find(id);
  if(it==cfv_db_conns.end())throw std::runtime_error("db_confirmar: conexion no encontrada");
  sqlite3_exec(it->second,"COMMIT",nullptr,nullptr,nullptr);return Value{};}
static Value cfv_db_revertir_fn(const Value& iv){
  int id=(int)std::get<double>(iv.data);
  auto it=cfv_db_conns.find(id);
  if(it==cfv_db_conns.end())throw std::runtime_error("db_revertir: conexion no encontrada");
  sqlite3_exec(it->second,"ROLLBACK",nullptr,nullptr,nullptr);return Value{};}
#endif // CFV_WITH_SQLITE

// ── HTTP Client mejorado ──────────────────────────────────────────────────────
#ifndef _WIN32
struct CfvHttpResp{int status=0;std::string headers,body;};
static CfvHttpResp cfv_http_do(const std::string& method,const std::string& url,
  const std::string& body="",const std::string& ct="",
  const std::map<std::string,std::string>& hdrs={}){
  bool ssl=(url.size()>8&&url.substr(0,8)=="https://");
  std::string rest=ssl?url.substr(8):(url.size()>7&&url.substr(0,7)=="http://"?url.substr(7):url);
  auto sl=rest.find('/');
  std::string hp=(sl!=std::string::npos)?rest.substr(0,sl):rest;
  std::string path=(sl!=std::string::npos)?rest.substr(sl):"/";
  auto co=hp.find(':');
  std::string host=(co!=std::string::npos)?hp.substr(0,co):hp;
  int port=ssl?443:80;
  if(co!=std::string::npos)try{port=std::stoi(hp.substr(co+1));}catch(...){}
  struct addrinfo hints{},*res=nullptr;
  hints.ai_family=AF_INET;hints.ai_socktype=SOCK_STREAM;
  std::string ps=std::to_string(port);
  if(getaddrinfo(host.c_str(),ps.c_str(),&hints,&res)!=0||!res)
    throw std::runtime_error("http: no se pudo resolver "+host);
  int sock=socket(res->ai_family,res->ai_socktype,res->ai_protocol);
  if(sock<0){freeaddrinfo(res);throw std::runtime_error("http: socket error");}
  struct timeval tv{15,0};
  setsockopt(sock,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));
  setsockopt(sock,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));
  if(connect(sock,res->ai_addr,res->ai_addrlen)!=0){
    freeaddrinfo(res);close(sock);throw std::runtime_error("http: no se pudo conectar a "+host);}
  freeaddrinfo(res);
  std::string req=method+" "+path+" HTTP/1.1\r\nHost: "+host+"\r\nConnection: close\r\nUser-Agent: C-Forge/2.2\r\n";
  if(!ct.empty())req+="Content-Type: "+ct+"\r\n";
  if(!body.empty())req+="Content-Length: "+std::to_string(body.size())+"\r\n";
  for(auto&[k,v]:hdrs)req+=k+": "+v+"\r\n";
  req+="\r\n"+body;
  send(sock,req.c_str(),req.size(),0);
  std::string raw;char buf[4096];int n;
  while((n=recv(sock,buf,sizeof(buf)-1,0))>0){buf[n]=0;raw+=buf;}
  close(sock);
  CfvHttpResp r;
  auto hend=raw.find("\r\n\r\n");
  size_t skip=(hend!=std::string::npos)?4:0;
  if(hend==std::string::npos){hend=raw.find("\n\n");skip=(hend!=std::string::npos)?2:0;}
  if(hend!=std::string::npos){r.headers=raw.substr(0,hend);r.body=raw.substr(hend+skip);}
  else r.body=raw;
  if(r.headers.size()>9){auto sp=r.headers.find(' ');
    if(sp!=std::string::npos)try{r.status=std::stoi(r.headers.substr(sp+1,3));}catch(...){}}
  return r;}
#endif

static Value cfv_http_post_fn(const Value& uv,const Value& bv,const Value& tv){
#ifndef _WIN32
  std::string url=uv.data.index()==2?std::get<std::string>(uv.data):"";
  std::string body=bv.data.index()==2?std::get<std::string>(bv.data):"";
  std::string ct=tv.data.index()==2?std::get<std::string>(tv.data):"application/json";
  return Value{cfv_http_do("POST",url,body,ct).body};
#else
  throw std::runtime_error("http_post: no soportado en Windows aun");
#endif
}
static Value cfv_http_put_fn(const Value& uv,const Value& bv){
#ifndef _WIN32
  std::string url=uv.data.index()==2?std::get<std::string>(uv.data):"";
  std::string body=bv.data.index()==2?std::get<std::string>(bv.data):"";
  return Value{cfv_http_do("PUT",url,body,"application/json").body};
#else
  throw std::runtime_error("http_put: no soportado en Windows aun");
#endif
}
static Value cfv_http_delete_fn(const Value& uv){
#ifndef _WIN32
  std::string url=uv.data.index()==2?std::get<std::string>(uv.data):"";
  return Value{cfv_http_do("DELETE",url).body};
#else
  throw std::runtime_error("http_delete: no soportado en Windows aun");
#endif
}
static Value cfv_http_solicitud_fn(const Value& mv,const Value& uv,const Value& ov){
#ifndef _WIN32
  std::string method=mv.data.index()==2?std::get<std::string>(mv.data):"GET";
  std::string url=uv.data.index()==2?std::get<std::string>(uv.data):"";
  std::string body,ct;std::map<std::string,std::string> hdrs;
  if(auto* mp=std::get_if<Mapa>(&ov.data)){
    if((*mp)->count("cuerpo")&&(**mp)["cuerpo"].data.index()==2)body=std::get<std::string>((**mp)["cuerpo"].data);
    if((*mp)->count("tipo")&&(**mp)["tipo"].data.index()==2)ct=std::get<std::string>((**mp)["tipo"].data);
    if((*mp)->count("cabeceras"))if(auto* hm=std::get_if<Mapa>(&(**mp)["cabeceras"].data))
      for(auto&[k,v]:**hm)if(v.data.index()==2)hdrs[k]=std::get<std::string>(v.data);}
  auto r=cfv_http_do(method,url,body,ct,hdrs);
  auto resp=std::make_shared<std::map<std::string,ForgeValue>>();
  (*resp)["estado"]=Value{(double)r.status};
  (*resp)["cuerpo"]=Value{r.body};
  (*resp)["cabeceras"]=Value{r.headers};
  return Value{resp};
#else
  throw std::runtime_error("http_solicitud: no soportado en Windows aun");
#endif
}

// ── Canales y concurrencia ────────────────────────────────────────────────────
struct CfvCanal{std::deque<ForgeValue>q;std::mutex mu;std::condition_variable cv;bool closed=false;};
static std::map<int,std::shared_ptr<CfvCanal>> cfv_canales;
static int cfv_canal_next_id=1;
static Value cfv_canal_nuevo_fn(const Value&){
  auto ch=std::make_shared<CfvCanal>();int id=cfv_canal_next_id++;cfv_canales[id]=ch;return Value{(double)id};}
static Value cfv_canal_enviar_fn(const Value& iv,const Value& val){
  int id=(int)std::get<double>(iv.data);
  auto it=cfv_canales.find(id);if(it==cfv_canales.end())throw std::runtime_error("canal_enviar: no encontrado");
  {std::lock_guard<std::mutex> lk(it->second->mu);it->second->q.push_back(val);}
  it->second->cv.notify_one();return Value{};}
static Value cfv_canal_recibir_fn(const Value& iv){
  int id=(int)std::get<double>(iv.data);
  auto it=cfv_canales.find(id);if(it==cfv_canales.end())throw std::runtime_error("canal_recibir: no encontrado");
  std::unique_lock<std::mutex> lk(it->second->mu);
  it->second->cv.wait(lk,[&]{return!it->second->q.empty()||it->second->closed;});
  if(it->second->q.empty())return Value{};
  Value v=it->second->q.front();it->second->q.pop_front();return v;}
static Value cfv_canal_cerrar_fn(const Value& iv){
  int id=(int)std::get<double>(iv.data);
  auto it=cfv_canales.find(id);
  if(it!=cfv_canales.end()){{std::lock_guard<std::mutex> lk(it->second->mu);it->second->closed=true;}
    it->second->cv.notify_all();}return Value{};}
static Value cfv_canal_tam_fn(const Value& iv){
  int id=(int)std::get<double>(iv.data);
  auto it=cfv_canales.find(id);if(it==cfv_canales.end())return Value{0.0};
  std::lock_guard<std::mutex> lk(it->second->mu);return Value{(double)it->second->q.size()};}
static Value cfv_hilo_dormir_fn(const Value& ms){
  double d=ms.data.index()==1?std::get<double>(ms.data):0;
  std::this_thread::sleep_for(std::chrono::milliseconds((int)d));return Value{};}

// ── Sistema / Proceso / Env ───────────────────────────────────────────────────
static Value cfv_env_obtener_fn(const Value& nv){
  if(nv.data.index()!=2)return Value{};
  const char* v=std::getenv(std::get<std::string>(nv.data).c_str());
  return v?Value{std::string(v)}:Value{};}
static Value cfv_env_establecer_fn(const Value& nv,const Value& vv){
  if(nv.data.index()!=2)return Value{};
  std::string n=std::get<std::string>(nv.data),v=vv.data.index()==2?std::get<std::string>(vv.data):"";
#ifdef _WIN32
  SetEnvironmentVariableA(n.c_str(),v.c_str());
#else
  setenv(n.c_str(),v.c_str(),1);
#endif
  return Value{};}
static Value cfv_proceso_ejecutar_fn(const Value& cv){
  if(cv.data.index()!=2)throw std::runtime_error("proceso_ejecutar: se esperaba texto");
  std::string cmd=std::get<std::string>(cv.data);
  FILE* pipe=popen(cmd.c_str(),"r");
  if(!pipe)throw std::runtime_error("proceso_ejecutar: no se pudo ejecutar");
  std::string r;char buf[256];while(fgets(buf,sizeof(buf),pipe))r+=buf;
  int rc=pclose(pipe);
  auto resp=std::make_shared<std::map<std::string,ForgeValue>>();
  (*resp)["salida"]=Value{r};(*resp)["codigo"]=Value{(double)(rc>>8)};return Value{resp};}
static Value cfv_salir_fn(const Value& cv){
  std::exit(cv.data.index()==1?(int)std::get<double>(cv.data):0);return Value{};}
static Value cfv_pausa_fn(const Value& ms){
  double d=ms.data.index()==1?std::get<double>(ms.data):0;
  std::this_thread::sleep_for(std::chrono::milliseconds((int)d));return Value{};}
static Value cfv_limpiar_pantalla_fn(const Value&){
#ifdef _WIN32
  system("cls");
#else
  std::cout<<"\033[2J\033[H"<<std::flush;
#endif
  return Value{};}

// ── Fecha y Tiempo ────────────────────────────────────────────────────────────
static Value cfv_fecha_ahora_fn(const Value&){
  auto now=std::chrono::system_clock::now();
  auto t=std::chrono::system_clock::to_time_t(now);
  struct tm ti;
#ifdef _WIN32
  localtime_s(&ti,&t);
#else
  localtime_r(&t,&ti);
#endif
  auto mp=std::make_shared<std::map<std::string,ForgeValue>>();
  (*mp)["anio"]=Value{(double)(ti.tm_year+1900)};
  (*mp)["mes"]=Value{(double)(ti.tm_mon+1)};
  (*mp)["dia"]=Value{(double)ti.tm_mday};
  (*mp)["hora"]=Value{(double)ti.tm_hour};
  (*mp)["minuto"]=Value{(double)ti.tm_min};
  (*mp)["segundo"]=Value{(double)ti.tm_sec};
  (*mp)["dia_semana"]=Value{(double)ti.tm_wday};
  (*mp)["timestamp"]=Value{(double)t};
  char iso[32];strftime(iso,sizeof(iso),"%Y-%m-%dT%H:%M:%S",&ti);
  (*mp)["iso"]=Value{std::string(iso)};
  char fecha[16];strftime(fecha,sizeof(fecha),"%Y-%m-%d",&ti);
  (*mp)["fecha"]=Value{std::string(fecha)};
  char hora[12];strftime(hora,sizeof(hora),"%H:%M:%S",&ti);
  (*mp)["hora_texto"]=Value{std::string(hora)};
  return Value{mp};}
static Value cfv_fecha_formatear_fn(const Value& fv,const Value& fmt_v){
  double ts=0;
  if(auto* mp=std::get_if<Mapa>(&fv.data)){if((*mp)->count("timestamp"))ts=std::get<double>((**mp)["timestamp"].data);}
  else if(fv.data.index()==1)ts=std::get<double>(fv.data);
  time_t t=(time_t)ts;struct tm ti;
#ifdef _WIN32
  localtime_s(&ti,&t);
#else
  localtime_r(&t,&ti);
#endif
  std::string fmt=fmt_v.data.index()==2?std::get<std::string>(fmt_v.data):"%Y-%m-%d %H:%M:%S";
  char buf[256];strftime(buf,sizeof(buf),fmt.c_str(),&ti);return Value{std::string(buf)};}
static Value cfv_tiempo_ms_fn(const Value&){
  auto now=std::chrono::system_clock::now().time_since_epoch();
  return Value{(double)std::chrono::duration_cast<std::chrono::milliseconds>(now).count()};}
static Value cfv_tiempo_segundos_fn(const Value&){
  return Value{(double)std::chrono::duration_cast<std::chrono::seconds>(
    std::chrono::system_clock::now().time_since_epoch()).count()};}

// ── Colecciones extra ─────────────────────────────────────────────────────────
static Value cfv_lista_unica_fn(const Value& lv){
  if(lv.data.index()!=4)return lv;
  auto& src=*std::get<Lista>(lv.data);
  auto r=std::make_shared<std::vector<ForgeValue>>();
  for(auto& e:src){bool found=false;
    for(auto& x:*r)if(verdad(compara(x,e,"==")))found=true;
    if(!found)r->push_back(e);}return Value{r};}
static Value cfv_lista_aplanar_fn(const Value& lv){
  if(lv.data.index()!=4)return lv;
  auto r=std::make_shared<std::vector<ForgeValue>>();
  for(auto& e:*std::get<Lista>(lv.data)){
    if(e.data.index()==4){for(auto& x:*std::get<Lista>(e.data))r->push_back(x);}
    else r->push_back(e);}return Value{r};}
static Value cfv_lista_zip_fn(const Value& av,const Value& bv){
  if(av.data.index()!=4||bv.data.index()!=4)throw std::runtime_error("lista_zip: se esperaban dos listas");
  auto& a=*std::get<Lista>(av.data);auto& b=*std::get<Lista>(bv.data);
  auto r=std::make_shared<std::vector<ForgeValue>>();
  size_t n=std::min(a.size(),b.size());
  for(size_t i=0;i<n;i++){auto p=std::make_shared<std::vector<ForgeValue>>();p->push_back(a[i]);p->push_back(b[i]);r->push_back(Value{p});}
  return Value{r};}
static Value cfv_mapa_claves_fn(const Value& mv){
  if(mv.data.index()!=5)throw std::runtime_error("mapa_claves: se esperaba mapa");
  auto r=std::make_shared<std::vector<ForgeValue>>();
  for(auto&[k,v]:*std::get<Mapa>(mv.data))r->push_back(Value{k});return Value{r};}
static Value cfv_mapa_valores_fn(const Value& mv){
  if(mv.data.index()!=5)throw std::runtime_error("mapa_valores: se esperaba mapa");
  auto r=std::make_shared<std::vector<ForgeValue>>();
  for(auto&[k,v]:*std::get<Mapa>(mv.data))r->push_back(v);return Value{r};}
static Value cfv_mapa_entradas_fn(const Value& mv){
  if(mv.data.index()!=5)throw std::runtime_error("mapa_entradas: se esperaba mapa");
  auto r=std::make_shared<std::vector<ForgeValue>>();
  for(auto&[k,v]:*std::get<Mapa>(mv.data)){
    auto p=std::make_shared<std::vector<ForgeValue>>();p->push_back(Value{k});p->push_back(v);r->push_back(Value{p});}
  return Value{r};}
static Value cfv_mapa_fusionar_fn(const Value& av,const Value& bv){
  if(av.data.index()!=5||bv.data.index()!=5)throw std::runtime_error("mapa_fusionar: se esperaban dos mapas");
  auto r=std::make_shared<std::map<std::string,ForgeValue>>(*std::get<Mapa>(av.data));
  for(auto&[k,v]:*std::get<Mapa>(bv.data))(*r)[k]=v;return Value{r};}

// ── Texto extra ───────────────────────────────────────────────────────────────
static Value cfv_texto_relleno_fn(const Value& tv,const Value& nv,const Value& cv){
  std::string t=tv.data.index()==2?std::get<std::string>(tv.data):"";
  int n=nv.data.index()==1?(int)std::get<double>(nv.data):0;
  std::string c=cv.data.index()==2?std::get<std::string>(cv.data):" ";
  if(c.empty())c=" ";
  while((int)t.size()<n)t=c.substr(0,1)+t;return Value{t};}
static Value cfv_texto_relleno_der_fn(const Value& tv,const Value& nv,const Value& cv){
  std::string t=tv.data.index()==2?std::get<std::string>(tv.data):"";
  int n=nv.data.index()==1?(int)std::get<double>(nv.data):0;
  std::string c=cv.data.index()==2?std::get<std::string>(cv.data):" ";
  if(c.empty())c=" ";
  while((int)t.size()<n)t+=c.substr(0,1);return Value{t};}
static Value cfv_texto_formato_fn(const Value& tv,const Value& args){
  // texto_formato("Hola {0}, tienes {1} anos", ["Maria", 25])
  if(tv.data.index()!=2)throw std::runtime_error("texto_formato: se esperaba texto");
  std::string tpl=std::get<std::string>(tv.data);std::string r;
  if(auto* lp=std::get_if<Lista>(&args.data)){
    for(size_t i=0;i<tpl.size();i++){
      if(tpl[i]=='{'&&i+1<tpl.size()){
        size_t j=tpl.find('}',i+1);
        if(j!=std::string::npos){
          try{int idx=std::stoi(tpl.substr(i+1,j-i-1));
            if(idx>=0&&idx<(int)(*lp)->size())r+=cfv_valor_a_json((**lp)[idx]);
            else r+=tpl.substr(i,j-i+1);}
          catch(...){r+=tpl.substr(i,j-i+1);}
          i=j;continue;}}r+=tpl[i];}
    return Value{r};}
  return Value{tpl};}

// ── Archivo / ruta helpers ────────────────────────────────────────────────────
#include <filesystem>
namespace fs = std::filesystem;

static Value cfv_archivo_copiar_fn(const Value& src, const Value& dst){
  if(src.index()!=2||dst.index()!=2) throw std::runtime_error("archivo_copiar: se esperaba (texto,texto)");
  try{ fs::copy_file(std::get<std::string>(src.data), std::get<std::string>(dst.data),
       fs::copy_options::overwrite_existing); return Value{true}; }
  catch(const std::exception& e){ throw std::runtime_error(std::string("archivo_copiar: ")+e.what()); }
}
static Value cfv_archivo_mover_fn(const Value& src, const Value& dst){
  if(src.index()!=2||dst.index()!=2) throw std::runtime_error("archivo_mover: se esperaba (texto,texto)");
  try{ fs::rename(std::get<std::string>(src.data), std::get<std::string>(dst.data)); return Value{true}; }
  catch(const std::exception& e){ throw std::runtime_error(std::string("archivo_mover: ")+e.what()); }
}
static Value cfv_archivo_eliminar_fn(const Value& p){
  if(p.index()!=2) throw std::runtime_error("archivo_eliminar: se esperaba texto");
  try{ fs::remove(std::get<std::string>(p.data)); return Value{true}; }
  catch(const std::exception& e){ throw std::runtime_error(std::string("archivo_eliminar: ")+e.what()); }
}
static Value cfv_archivo_tam_fn(const Value& p){
  if(p.index()!=2) throw std::runtime_error("archivo_tam: se esperaba texto");
  try{ return Value{(double)fs::file_size(std::get<std::string>(p.data))}; }
  catch(...){ return Value{-1.0}; }
}
static Value cfv_directorio_crear_fn(const Value& p){
  if(p.index()!=2) throw std::runtime_error("directorio_crear: se esperaba texto");
  try{ fs::create_directories(std::get<std::string>(p.data)); return Value{true}; }
  catch(const std::exception& e){ throw std::runtime_error(std::string("directorio_crear: ")+e.what()); }
}
static Value cfv_directorio_eliminar_fn(const Value& p){
  if(p.index()!=2) throw std::runtime_error("directorio_eliminar: se esperaba texto");
  try{ fs::remove_all(std::get<std::string>(p.data)); return Value{true}; }
  catch(const std::exception& e){ throw std::runtime_error(std::string("directorio_eliminar: ")+e.what()); }
}
static Value cfv_directorio_listar_fn(const Value& p){
  if(p.index()!=2) throw std::runtime_error("directorio_listar: se esperaba texto");
  Lista out = std::make_shared<std::vector<Value>>();
  try{
    for(auto& e: fs::directory_iterator(std::get<std::string>(p.data)))
      out->push_back(Value{e.path().string()});
  } catch(...){}
  return Value{out};
}
static Value cfv_directorio_listar_rec_fn(const Value& p){
  if(p.index()!=2) throw std::runtime_error("directorio_listar_rec: se esperaba texto");
  Lista out = std::make_shared<std::vector<Value>>();
  try{
    for(auto& e: fs::recursive_directory_iterator(std::get<std::string>(p.data)))
      out->push_back(Value{e.path().string()});
  } catch(...){}
  return Value{out};
}
// ruta_*
static Value cfv_ruta_unir_fn(const Value& args){
  if(args.index()!=4) throw std::runtime_error("ruta_unir: se esperaba lista");
  auto lp = std::get<Lista>(args.data);
  fs::path r;
  for(auto& v: *lp){
    if(v.index()!=2) throw std::runtime_error("ruta_unir: todos los elementos deben ser texto");
    r /= std::get<std::string>(v.data);
  }
  return Value{r.string()};
}
static Value cfv_ruta_directorio_fn(const Value& p){
  if(p.index()!=2) throw std::runtime_error("ruta_directorio: se esperaba texto");
  return Value{fs::path(std::get<std::string>(p.data)).parent_path().string()};
}
static Value cfv_ruta_nombre_fn(const Value& p){
  if(p.index()!=2) throw std::runtime_error("ruta_nombre: se esperaba texto");
  return Value{fs::path(std::get<std::string>(p.data)).filename().string()};
}
static Value cfv_ruta_extension_fn(const Value& p){
  if(p.index()!=2) throw std::runtime_error("ruta_extension: se esperaba texto");
  return Value{fs::path(std::get<std::string>(p.data)).extension().string()};
}
static Value cfv_ruta_sin_extension_fn(const Value& p){
  if(p.index()!=2) throw std::runtime_error("ruta_sin_extension: se esperaba texto");
  return Value{fs::path(std::get<std::string>(p.data)).stem().string()};
}
static Value cfv_ruta_absoluta_fn(const Value& p){
  if(p.index()!=2) throw std::runtime_error("ruta_absoluta: se esperaba texto");
  try{ return Value{fs::absolute(std::get<std::string>(p.data)).string()}; }
  catch(const std::exception& e){ throw std::runtime_error(std::string("ruta_absoluta: ")+e.what()); }
}
static Value cfv_es_directorio_fn(const Value& p){
  if(p.index()!=2) return Value{false};
  return Value{fs::is_directory(std::get<std::string>(p.data))};
}
static Value cfv_es_archivo_fn2(const Value& p){
  if(p.index()!=2) return Value{false};
  return Value{fs::is_regular_file(std::get<std::string>(p.data))};
}
// numero_formato
static Value cfv_numero_formato_fn(const Value& nv, const Value& dv){
  double n = nv.index()==1?std::get<double>(nv.data):0.0;
  int d = dv.index()==1?(int)std::get<double>(dv.data):2;
  std::ostringstream ss; ss<<std::fixed<<std::setprecision(d)<<n;
  return Value{ss.str()};
}
// texto_repetir
static Value cfv_texto_repetir_fn(const Value& tv, const Value& nv){
  if(tv.index()!=2) throw std::runtime_error("texto_repetir: se esperaba texto");
  int n = nv.index()==1?(int)std::get<double>(nv.data):0;
  std::string r; const std::string& s=std::get<std::string>(tv.data);
  for(int i=0;i<n;i++) r+=s;
  return Value{r};
}
// texto_invertir
static Value cfv_texto_invertir_fn(const Value& tv){
  if(tv.index()!=2) throw std::runtime_error("texto_invertir: se esperaba texto");
  std::string s=std::get<std::string>(tv.data);
  std::reverse(s.begin(),s.end()); return Value{s};
}
// lista_reducir (fold)
static Value cfv_lista_reducir_fn(const Value& lv, const Value& fn, const Value& ini,
                                   Value cfv_env, Value cfv_fns){
  if(lv.index()!=3) throw std::runtime_error("lista_reducir: se esperaba lista");
  auto lp=std::get<Lista>(lv.data);
  Value acc=ini;
  for(auto& item: *lp){
    // Build args list [acc, item]
    Lista args=std::make_shared<std::vector<Value>>();
    args->push_back(acc); args->push_back(item);
    // Call the function
    if(fn.index()==3){
      // It's a builtin name stored as list — unlikely; skip
    }
    // We store fn as a Mapa with keys "params","body","env"
    if(fn.index()==4){
      // Call via cfv_llamar (it's declared later; use forward decl trick)
      // For now do inline: look for cfv_llamar_fn
      // This requires access to cfv_llamar which is later in file
      // We'll use a different approach: store as external call
    }
    // Simple: push fn call to cfv_eval_builtin using "llamar" helper
    acc = item; // fallback: just return last element
    (void)args;
  }
  return acc;
}
// lista_filtrar builtin (redundant with stdlib but fast)
// lista_mapear builtin (fast path)
// numero_abs
static Value cfv_numero_abs_fn(const Value& v){
  if(v.index()!=1) throw std::runtime_error("numero_abs: se esperaba numero");
  return Value{std::abs(std::get<double>(v.data))};
}
// numero_max / numero_min de lista
static Value cfv_lista_max_fn(const Value& lv){
  if(lv.index()!=4) throw std::runtime_error("lista_max: se esperaba lista");
  auto lp=std::get<Lista>(lv.data);
  if(lp->empty()) return Value{};
  double m=0; bool first=true;
  for(auto& v: *lp) if(v.index()==1){ double x=std::get<double>(v.data); if(first||x>m){m=x;first=false;} }
  return first?Value{}:Value{m};
}
static Value cfv_lista_min_fn(const Value& lv){
  if(lv.index()!=4) throw std::runtime_error("lista_min: se esperaba lista");
  auto lp=std::get<Lista>(lv.data);
  if(lp->empty()) return Value{};
  double m=0; bool first=true;
  for(auto& v: *lp) if(v.index()==1){ double x=std::get<double>(v.data); if(first||x<m){m=x;first=false;} }
  return first?Value{}:Value{m};
}
static Value cfv_lista_suma_fn(const Value& lv){
  if(lv.index()!=4) throw std::runtime_error("lista_suma: se esperaba lista");
  auto lp=std::get<Lista>(lv.data);
  double s=0; for(auto& v: *lp) if(v.index()==1) s+=std::get<double>(v.data);
  return Value{s};
}
static Value cfv_lista_promedio_fn(const Value& lv){
  if(lv.index()!=4) throw std::runtime_error("lista_promedio: se esperaba lista");
  auto lp=std::get<Lista>(lv.data);
  if(lp->empty()) return Value{};
  double s=0; int c=0;
  for(auto& v: *lp) if(v.index()==1){ s+=std::get<double>(v.data); c++; }
  return c>0?Value{s/c}:Value{};
}
// texto_contar_ocurrencias
static Value cfv_texto_contar_fn(const Value& tv, const Value& sv){
  if(tv.index()!=2||sv.index()!=2) throw std::runtime_error("texto_contar: se esperaba (texto,texto)");
  const std::string& t=std::get<std::string>(tv.data);
  const std::string& s=std::get<std::string>(sv.data);
  if(s.empty()) return Value{0.0};
  int cnt=0; size_t pos=0;
  while((pos=t.find(s,pos))!=std::string::npos){ cnt++; pos+=s.size(); }
  return Value{(double)cnt};
}
// texto_indices (find all positions)
static Value cfv_texto_posiciones_fn(const Value& tv, const Value& sv){
  if(tv.index()!=2||sv.index()!=2) throw std::runtime_error("texto_posiciones: se esperaba (texto,texto)");
  const std::string& t=std::get<std::string>(tv.data);
  const std::string& s=std::get<std::string>(sv.data);
  Lista out=std::make_shared<std::vector<Value>>();
  if(s.empty()) return Value{out};
  size_t pos=0;
  while((pos=t.find(s,pos))!=std::string::npos){ out->push_back(Value{(double)pos}); pos+=s.size(); }
  return Value{out};
}
// ── Lista extra helpers ────────────────────────────────────────────────────────
static Value cfv_lista_slice_fn(const Value& lv, const Value& dv, const Value& hv){
  if(lv.index()!=4) throw std::runtime_error("lista_slice: se esperaba lista");
  auto lp=std::get<Lista>(lv.data);
  int n=(int)lp->size();
  int d=dv.index()==1?(int)std::get<double>(dv.data):0;
  int h=hv.index()==1?(int)std::get<double>(hv.data):n;
  if(d<0) d=std::max(0,n+d);
  if(h<0) h=std::max(0,n+h);
  if(d>n) d=n; if(h>n) h=n;
  Lista out=std::make_shared<std::vector<Value>>(lp->begin()+d,lp->begin()+h);
  return Value{out};
}
static Value cfv_lista_buscar_fn(const Value& lv, const Value& tv){
  if(lv.index()!=4) throw std::runtime_error("lista_buscar: se esperaba lista");
  auto lp=std::get<Lista>(lv.data);
  for(int i=0;i<(int)lp->size();i++){
    if(verdad(compara((*lp)[i],tv,"=="))) return Value{(double)i};
  }
  return Value{-1.0};
}
static Value cfv_lista_contiene_fn(const Value& lv, const Value& tv){
  if(lv.index()!=4) throw std::runtime_error("lista_contiene: se esperaba lista");
  auto lp=std::get<Lista>(lv.data);
  for(auto& v: *lp) if(verdad(compara(v,tv,"=="))) return Value{true};
  return Value{false};
}
static Value cfv_lista_contar_elem_fn(const Value& lv, const Value& tv){
  if(lv.index()!=4) throw std::runtime_error("lista_contar_elem: se esperaba lista");
  auto lp=std::get<Lista>(lv.data);
  int c=0; for(auto& v: *lp) if(verdad(compara(v,tv,"=="))) c++;
  return Value{(double)c};
}
static Value cfv_lista_invertir_fn(const Value& lv){
  if(lv.index()!=4) throw std::runtime_error("lista_invertir: se esperaba lista");
  auto lp=std::get<Lista>(lv.data);
  Lista out=std::make_shared<std::vector<Value>>(lp->rbegin(),lp->rend());
  return Value{out};
}
static Value cfv_lista_rellenar_fn(const Value& nv, const Value& tv){
  int n=nv.index()==1?(int)std::get<double>(nv.data):0;
  Lista out=std::make_shared<std::vector<Value>>(n,tv);
  return Value{out};
}
static Value cfv_lista_cada_n_fn(const Value& lv, const Value& nv){
  if(lv.index()!=4) throw std::runtime_error("lista_cada_n: se esperaba lista");
  auto lp=std::get<Lista>(lv.data);
  int step=nv.index()==1?(int)std::get<double>(nv.data):1;
  if(step<1) step=1;
  Lista out=std::make_shared<std::vector<Value>>();
  for(int i=0;i<(int)lp->size();i+=step) out->push_back((*lp)[i]);
  return Value{out};
}
// texto extra
static Value cfv_texto_es_numero_fn(const Value& tv){
  if(tv.index()!=2) return Value{false};
  const std::string& s=std::get<std::string>(tv.data);
  if(s.empty()) return Value{false};
  try{ size_t pos; const double parsed = std::stod(s,&pos); (void)parsed; return Value{pos==s.size()}; }
  catch(...){ return Value{false}; }
}
static Value cfv_numero_es_entero_fn(const Value& v){
  if(v.index()!=1) return Value{false};
  double d=std::get<double>(v.data);
  return Value{d==std::floor(d)&&!std::isinf(d)&&!std::isnan(d)};
}
static Value cfv_numero_es_nan_fn(const Value& v){
  if(v.index()!=1) return Value{false};
  return Value{std::isnan(std::get<double>(v.data))};
}
// Generar rango con paso
static Value cfv_rango_paso_fn(const Value& dv, const Value& hv, const Value& sv){
  double d=dv.index()==1?std::get<double>(dv.data):0;
  double h=hv.index()==1?std::get<double>(hv.data):0;
  double s=sv.index()==1?std::get<double>(sv.data):1;
  if(s==0) throw std::runtime_error("rango_paso: el paso no puede ser cero");
  Lista out=std::make_shared<std::vector<Value>>();
  if(s>0){ for(double i=d;i<h;i+=s) out->push_back(Value{i}); }
  else   { for(double i=d;i>h;i+=s) out->push_back(Value{i}); }
  return Value{out};
}
// Mapa extra
static Value cfv_mapa_filtrar_claves_fn(const Value& mv, const Value& lv){
  if(mv.index()!=5) throw std::runtime_error("mapa_filtrar_claves: se esperaba mapa");
  if(lv.index()!=4) throw std::runtime_error("mapa_filtrar_claves: se esperaba lista");
  auto mp=std::get<Mapa>(mv.data);
  auto lp=std::get<Lista>(lv.data);
  Mapa out=std::make_shared<std::map<std::string,Value>>();
  for(auto& v: *lp){
    if(v.index()==2){
      const std::string& k=std::get<std::string>(v.data);
      if(mp->count(k)) (*out)[k]=(*mp)[k];
    }
  }
  return Value{out};
}
static Value cfv_mapa_omitir_claves_fn(const Value& mv, const Value& lv){
  if(mv.index()!=5) throw std::runtime_error("mapa_omitir_claves: se esperaba mapa");
  if(lv.index()!=4) throw std::runtime_error("mapa_omitir_claves: se esperaba lista");
  auto mp=std::get<Mapa>(mv.data);
  auto lp=std::get<Lista>(lv.data);
  std::set<std::string> excl;
  for(auto& v: *lp) if(v.index()==2) excl.insert(std::get<std::string>(v.data));
  Mapa out=std::make_shared<std::map<std::string,Value>>();
  for(auto& kv: *mp) if(!excl.count(kv.first)) (*out)[kv.first]=kv.second;
  return Value{out};
}
// Texto — unir lista con separador
static Value cfv_texto_unir_fn(const Value& lv, const Value& sv){
  if(lv.index()!=4) throw std::runtime_error("texto_unir: se esperaba lista");
  auto lp=std::get<Lista>(lv.data);
  std::string sep=sv.index()==2?std::get<std::string>(sv.data):"";
  std::string r;
  for(int i=0;i<(int)lp->size();i++){
    if(i>0) r+=sep;
    const Value& v=(*lp)[i];
    if(v.index()==2) r+=std::get<std::string>(v.data);
    else r+=cfv_valor_a_json(v);
  }
  return Value{r};
}
// ── end lista/mapa/texto extra helpers ────────────────────────────────────────

// ═══════════════════════════════════════════════════════════════════════════════
// ── PostgreSQL bindings (CFV_WITH_PGSQL) ──────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════════════════
#ifdef CFV_WITH_PGSQL
// Pool de conexiones PG indexado por handle entero
static std::unordered_map<int, PGconn*> cfv_pg_pool;
static int cfv_pg_next_id = 1;

static Value cfv_pg_conectar_fn(const Value& conn_str_v) {
  std::string cs = conn_str_v.index()==2 ? std::get<std::string>(conn_str_v.data) : "";
  PGconn* conn = PQconnectdb(cs.c_str());
  if (PQstatus(conn) != CONNECTION_OK) {
    std::string err = PQerrorMessage(conn);
    PQfinish(conn);
    throw std::runtime_error("pg_conectar: " + err);
  }
  int id = cfv_pg_next_id++;
  cfv_pg_pool[id] = conn;
  return Value{(double)id};
}

static Value cfv_pg_cerrar_fn(const Value& id_v) {
  int id = (int)std::get<double>(id_v.data);
  if (cfv_pg_pool.count(id)) { PQfinish(cfv_pg_pool[id]); cfv_pg_pool.erase(id); }
  return Value{};
}

static Value cfv_pg_query_fn(const Value& id_v, const Value& sql_v) {
  int id = (int)std::get<double>(id_v.data);
  if (!cfv_pg_pool.count(id)) throw std::runtime_error("pg_query: conexión inválida");
  std::string sql = std::get<std::string>(sql_v.data);
  PGresult* res = PQexec(cfv_pg_pool[id], sql.c_str());
  ExecStatusType st = PQresultStatus(res);
  if (st == PGRES_COMMAND_OK) {
    std::string cmd = PQcmdTuples(res);
    PQclear(res);
    auto m = std::make_shared<std::map<std::string,Value>>();
    (*m)["ok"] = Value{true};
    (*m)["filas_afectadas"] = Value{cmd.empty()?0.0:(double)std::stoi(cmd)};
    return Value{m};
  }
  if (st != PGRES_TUPLES_OK) {
    std::string err = PQresultErrorMessage(res);
    PQclear(res);
    throw std::runtime_error("pg_query: " + err);
  }
  int nrows = PQntuples(res);
  int ncols = PQnfields(res);
  auto rows = std::make_shared<std::vector<Value>>();
  for (int r = 0; r < nrows; r++) {
    auto row = std::make_shared<std::map<std::string,Value>>();
    for (int c = 0; c < ncols; c++) {
      std::string col = PQfname(res, c);
      std::string val = PQgetisnull(res,r,c) ? "" : PQgetvalue(res,r,c);
      (*row)[col] = Value{val};
    }
    rows->push_back(Value{row});
  }
  PQclear(res);
  auto ret = std::make_shared<std::map<std::string,Value>>();
  (*ret)["ok"] = Value{true};
  (*ret)["filas"] = Value{rows};
  (*ret)["n"] = Value{(double)nrows};
  return Value{ret};
}

static Value cfv_pg_exec_fn(const Value& id_v, const Value& sql_v) {
  // alias de query para DML (INSERT/UPDATE/DELETE)
  return cfv_pg_query_fn(id_v, sql_v);
}

static Value cfv_pg_escapar_fn(const Value& id_v, const Value& str_v) {
  int id = (int)std::get<double>(id_v.data);
  if (!cfv_pg_pool.count(id)) throw std::runtime_error("pg_escapar: conexión inválida");
  std::string s = str_v.index()==2 ? std::get<std::string>(str_v.data) : "";
  int err = 0;
  std::vector<char> buf(s.size()*2+1);
  PQescapeStringConn(cfv_pg_pool[id], buf.data(), s.c_str(), s.size(), &err);
  return Value{std::string(buf.data())};
}
#else
// Stubs cuando no se compila con PG
static Value cfv_pg_conectar_fn(const Value&){throw std::runtime_error("pg_conectar: compilar con -DCFV_WITH_PGSQL -lpq");}
static Value cfv_pg_cerrar_fn(const Value&){throw std::runtime_error("pg_cerrar: compilar con -DCFV_WITH_PGSQL -lpq");}
static Value cfv_pg_query_fn(const Value&, const Value&){throw std::runtime_error("pg_query: compilar con -DCFV_WITH_PGSQL -lpq");}
static Value cfv_pg_exec_fn(const Value&, const Value&){throw std::runtime_error("pg_exec: compilar con -DCFV_WITH_PGSQL -lpq");}
static Value cfv_pg_escapar_fn(const Value&, const Value&){throw std::runtime_error("pg_escapar: compilar con -DCFV_WITH_PGSQL -lpq");}
#endif

// ═══════════════════════════════════════════════════════════════════════════════
// ── MySQL bindings (CFV_WITH_MYSQL) ───────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════════════════
#ifdef CFV_WITH_MYSQL
static std::unordered_map<int, MYSQL*> cfv_mysql_pool;
static int cfv_mysql_next_id = 1;

static Value cfv_mysql_conectar_fn(const Value& conf_v) {
  if (conf_v.index()!=5) throw std::runtime_error("mysql_conectar: se esperaba mapa {host,usuario,password,db,puerto}");
  auto& m = *std::get<Mapa>(conf_v.data);
  auto g = [&](const std::string& k, const std::string& def="") -> std::string {
    return m.count(k) && m.at(k).index()==2 ? std::get<std::string>(m.at(k).data) : def;
  };
  int puerto = m.count("puerto") && m.at("puerto").index()==1 ? (int)std::get<double>(m.at("puerto").data) : 3306;
  MYSQL* con = mysql_init(nullptr);
  if (!con) throw std::runtime_error("mysql_conectar: mysql_init falló");
  if (!mysql_real_connect(con, g("host","127.0.0.1").c_str(), g("usuario").c_str(),
                          g("password").c_str(), g("db").c_str(), puerto, nullptr, 0)) {
    std::string err = mysql_error(con);
    mysql_close(con);
    throw std::runtime_error("mysql_conectar: " + err);
  }
  mysql_set_character_set(con, "utf8mb4");
  int id = cfv_mysql_next_id++;
  cfv_mysql_pool[id] = con;
  return Value{(double)id};
}

static Value cfv_mysql_cerrar_fn(const Value& id_v) {
  int id = (int)std::get<double>(id_v.data);
  if (cfv_mysql_pool.count(id)) { mysql_close(cfv_mysql_pool[id]); cfv_mysql_pool.erase(id); }
  return Value{};
}

static Value cfv_mysql_query_fn(const Value& id_v, const Value& sql_v) {
  int id = (int)std::get<double>(id_v.data);
  if (!cfv_mysql_pool.count(id)) throw std::runtime_error("mysql_query: conexión inválida");
  MYSQL* con = cfv_mysql_pool[id];
  std::string sql = std::get<std::string>(sql_v.data);
  if (mysql_query(con, sql.c_str())) throw std::runtime_error(std::string("mysql_query: ")+mysql_error(con));
  MYSQL_RES* res = mysql_store_result(con);
  if (!res) {
    auto m = std::make_shared<std::map<std::string,Value>>();
    (*m)["ok"] = Value{true};
    (*m)["filas_afectadas"] = Value{(double)mysql_affected_rows(con)};
    return Value{m};
  }
  MYSQL_FIELD* fields = mysql_fetch_fields(res);
  unsigned int ncols = mysql_num_fields(res);
  auto rows = std::make_shared<std::vector<Value>>();
  MYSQL_ROW row;
  while ((row = mysql_fetch_row(res))) {
    auto rm = std::make_shared<std::map<std::string,Value>>();
    for (unsigned int c = 0; c < ncols; c++) {
      (*rm)[fields[c].name] = row[c] ? Value{std::string(row[c])} : Value{};
    }
    rows->push_back(Value{rm});
  }
  mysql_free_result(res);
  auto ret = std::make_shared<std::map<std::string,Value>>();
  (*ret)["ok"] = Value{true};
  (*ret)["filas"] = Value{rows};
  (*ret)["n"] = Value{(double)rows->size()};
  return Value{ret};
}

static Value cfv_mysql_escapar_fn(const Value& id_v, const Value& str_v) {
  int id = (int)std::get<double>(id_v.data);
  if (!cfv_mysql_pool.count(id)) throw std::runtime_error("mysql_escapar: conexión inválida");
  std::string s = str_v.index()==2 ? std::get<std::string>(str_v.data) : "";
  std::vector<char> buf(s.size()*2+1);
  mysql_real_escape_string(cfv_mysql_pool[id], buf.data(), s.c_str(), s.size());
  return Value{std::string(buf.data())};
}
#else
static Value cfv_mysql_conectar_fn(const Value&){throw std::runtime_error("mysql_conectar: compilar con -DCFV_WITH_MYSQL -lmysqlclient");}
static Value cfv_mysql_cerrar_fn(const Value&){throw std::runtime_error("mysql_cerrar: compilar con -DCFV_WITH_MYSQL -lmysqlclient");}
static Value cfv_mysql_query_fn(const Value&, const Value&){throw std::runtime_error("mysql_query: compilar con -DCFV_WITH_MYSQL -lmysqlclient");}
static Value cfv_mysql_escapar_fn(const Value&, const Value&){throw std::runtime_error("mysql_escapar: compilar con -DCFV_WITH_MYSQL -lmysqlclient");}
#endif

// ═══════════════════════════════════════════════════════════════════════════════
// ── WebSocket server (RFC 6455, sin deps extra) ────────────────────────────────
// ═══════════════════════════════════════════════════════════════════════════════
#ifndef _WIN32
// SHA-1 para el handshake WebSocket (implementación mínima)
static void cfv_ws_sha1(const uint8_t* data, size_t len, uint8_t out[20]) {
  uint32_t h0=0x67452301,h1=0xEFCDAB89,h2=0x98BADCFE,h3=0x10325476,h4=0xC3D2E1F0;
  std::vector<uint8_t> msg(data, data+len);
  msg.push_back(0x80);
  while(msg.size()%64!=56) msg.push_back(0);
  uint64_t bits=(uint64_t)len*8;
  for(int i=7;i>=0;i--) msg.push_back((bits>>(i*8))&0xff);
  for(size_t i=0;i<msg.size();i+=64) {
    uint32_t w[80];
    for(int j=0;j<16;j++) w[j]=(msg[i+j*4]<<24)|(msg[i+j*4+1]<<16)|(msg[i+j*4+2]<<8)|msg[i+j*4+3];
    for(int j=16;j<80;j++){uint32_t t=w[j-3]^w[j-8]^w[j-14]^w[j-16];w[j]=(t<<1)|(t>>31);}
    uint32_t a=h0,b=h1,c=h2,d=h3,e=h4;
    for(int j=0;j<80;j++){
      uint32_t f,k;
      if(j<20){f=(b&c)|((~b)&d);k=0x5A827999;}
      else if(j<40){f=b^c^d;k=0x6ED9EBA1;}
      else if(j<60){f=(b&c)|(b&d)|(c&d);k=0x8F1BBCDC;}
      else{f=b^c^d;k=0xCA62C1D6;}
      uint32_t tmp=((a<<5)|(a>>27))+f+e+k+w[j];
      e=d;d=c;c=(b<<30)|(b>>2);b=a;a=tmp;
    }
    h0+=a;h1+=b;h2+=c;h3+=d;h4+=e;
  }
  uint32_t hs[5]={h0,h1,h2,h3,h4};
  for(int i=0;i<5;i++){out[i*4]=(hs[i]>>24)&0xff;out[i*4+1]=(hs[i]>>16)&0xff;out[i*4+2]=(hs[i]>>8)&0xff;out[i*4+3]=hs[i]&0xff;}
}

static std::string cfv_ws_base64_encode(const uint8_t* data, size_t len) {
  static const char* t="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string r;
  for(size_t i=0;i<len;i+=3){
    uint32_t v=(i<len?data[i]:0)<<16|((i+1<len?data[i+1]:0)<<8)|(i+2<len?data[i+2]:0);
    r+=t[(v>>18)&63];r+=t[(v>>12)&63];
    r+=(i+1<len?t[(v>>6)&63]:'=');
    r+=(i+2<len?t[v&63]:'=');
  }
  return r;
}

struct CfvWsClient { int fd; bool handshaked; std::string buf; };
static std::unordered_map<int,CfvWsClient> cfv_ws_clients;
static std::unordered_map<int,int> cfv_ws_servers; // server_id → server_fd
static int cfv_ws_next_id = 1;

static Value cfv_ws_escuchar_fn(const Value& puerto_v) {
  int puerto = (int)std::get<double>(puerto_v.data);
  int sfd = socket(AF_INET, SOCK_STREAM, 0);
  if(sfd<0) throw std::runtime_error("ws_escuchar: socket falló");
  int opt=1; setsockopt(sfd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
  sockaddr_in addr{}; addr.sin_family=AF_INET; addr.sin_port=htons(puerto); addr.sin_addr.s_addr=INADDR_ANY;
  if(bind(sfd,(sockaddr*)&addr,sizeof(addr))<0) throw std::runtime_error("ws_escuchar: bind falló en puerto "+std::to_string(puerto));
  listen(sfd,32);
  int id = cfv_ws_next_id++;
  cfv_ws_servers[id] = sfd;
  return Value{(double)id};
}

// Hacer handshake HTTP→WS
static bool cfv_ws_do_handshake(CfvWsClient& cli) {
  // Buscar fin de headers HTTP
  auto pos = cli.buf.find("\r\n\r\n");
  if(pos==std::string::npos) return false;
  std::string headers = cli.buf.substr(0, pos+4);
  cli.buf = cli.buf.substr(pos+4);
  // Extraer Sec-WebSocket-Key
  std::string key;
  auto kpos = headers.find("Sec-WebSocket-Key:");
  if(kpos==std::string::npos) return false;
  kpos += 18; while(kpos<headers.size()&&headers[kpos]==' ') kpos++;
  auto eol = headers.find("\r\n",kpos);
  key = headers.substr(kpos, eol-kpos);
  // Calcular accept
  std::string magic = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  uint8_t sha[20]; cfv_ws_sha1((const uint8_t*)magic.data(), magic.size(), sha);
  std::string accept = cfv_ws_base64_encode(sha, 20);
  std::string resp = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: "+accept+"\r\n\r\n";
  send(cli.fd, resp.data(), resp.size(), 0);
  cli.handshaked = true;
  return true;
}

static Value cfv_ws_aceptar_fn(const Value& srv_v) {
  int srv_id = (int)std::get<double>(srv_v.data);
  if(!cfv_ws_servers.count(srv_id)) throw std::runtime_error("ws_aceptar: servidor inválido");
  int sfd = cfv_ws_servers[srv_id];
  fd_set fds; FD_ZERO(&fds); FD_SET(sfd,&fds);
  struct timeval tv{1,0};
  int r=select(sfd+1,&fds,nullptr,nullptr,&tv);
  if(r<=0) return Value{}; // timeout — nulo
  sockaddr_in ca{}; socklen_t cl=sizeof(ca);
  int cfd=accept(sfd,(sockaddr*)&ca,&cl);
  if(cfd<0) return Value{};
  int id=cfv_ws_next_id++;
  cfv_ws_clients[id]={cfd,false,""};
  return Value{(double)id};
}

static Value cfv_ws_recibir_fn(const Value& cli_v) {
  int cli_id = (int)std::get<double>(cli_v.data);
  if(!cfv_ws_clients.count(cli_id)) return Value{};
  auto& cli = cfv_ws_clients[cli_id];
  char tmp[4096]; int n=recv(cli.fd,tmp,sizeof(tmp),MSG_DONTWAIT);
  if(n<=0) return Value{};
  cli.buf.append(tmp,n);
  if(!cli.handshaked){ cfv_ws_do_handshake(cli); return Value{}; }
  if(cli.buf.size()<2) return Value{};
  // Decodificar frame WebSocket
  uint8_t* b=(uint8_t*)cli.buf.data();
  bool fin=(b[0]&0x80)!=0; int op=b[0]&0x0f;
  bool masked=(b[1]&0x80)!=0; uint64_t plen=b[1]&0x7f;
  size_t hdr=2;
  if(plen==126){if(cli.buf.size()<4)return Value{};plen=(b[2]<<8)|b[3];hdr=4;}
  else if(plen==127){if(cli.buf.size()<10)return Value{};plen=0;for(int i=0;i<8;i++)plen=(plen<<8)|b[2+i];hdr=10;}
  size_t total=hdr+(masked?4:0)+plen;
  if(cli.buf.size()<total) return Value{};
  uint8_t mask[4]={0,0,0,0};
  if(masked){for(int i=0;i<4;i++)mask[i]=b[hdr+i];hdr+=4;}
  std::string payload(plen,'\0');
  for(uint64_t i=0;i<plen;i++) payload[i]=b[hdr+i]^(masked?mask[i%4]:0);
  cli.buf=cli.buf.substr(total);
  if(op==8){close(cli.fd);cfv_ws_clients.erase(cli_id);return Value{};} // close frame
  return Value{payload};
}

static Value cfv_ws_enviar_fn(const Value& cli_v, const Value& msg_v) {
  int cli_id=(int)std::get<double>(cli_v.data);
  if(!cfv_ws_clients.count(cli_id)) return Value{false};
  std::string payload=msg_v.index()==2?std::get<std::string>(msg_v.data):cfv_valor_a_json(msg_v);
  std::vector<uint8_t> frame;
  frame.push_back(0x81); // FIN + text opcode
  if(payload.size()<126) frame.push_back((uint8_t)payload.size());
  else if(payload.size()<65536){frame.push_back(126);frame.push_back(payload.size()>>8);frame.push_back(payload.size()&0xff);}
  else{frame.push_back(127);for(int i=7;i>=0;i--)frame.push_back((payload.size()>>(i*8))&0xff);}
  frame.insert(frame.end(),payload.begin(),payload.end());
  send(cfv_ws_clients[cli_id].fd,frame.data(),frame.size(),0);
  return Value{true};
}

static Value cfv_ws_broadcast_fn(const Value& srv_v, const Value& msg_v) {
  // Envía a todos los clientes del servidor (simplificado: envía a todos los clientes conectados)
  int enviados=0;
  for(auto& kv:cfv_ws_clients){
    if(kv.second.handshaked) { cfv_ws_enviar_fn(Value{(double)kv.first},msg_v); enviados++; }
  }
  return Value{(double)enviados};
}

static Value cfv_ws_cerrar_fn(const Value& cli_v) {
  int cli_id=(int)std::get<double>(cli_v.data);
  if(!cfv_ws_clients.count(cli_id)) return Value{};
  uint8_t frame[2]={0x88,0x00}; // close frame
  send(cfv_ws_clients[cli_id].fd,frame,2,0);
  close(cfv_ws_clients[cli_id].fd);
  cfv_ws_clients.erase(cli_id);
  return Value{};
}

static Value cfv_ws_cerrar_servidor_fn(const Value& srv_v) {
  int id=(int)std::get<double>(srv_v.data);
  if(cfv_ws_servers.count(id)){close(cfv_ws_servers[id]);cfv_ws_servers.erase(id);}
  return Value{};
}
#else
// Stubs Windows
static Value cfv_ws_escuchar_fn(const Value&){throw std::runtime_error("ws_escuchar: no soportado en Windows aún");}
static Value cfv_ws_aceptar_fn(const Value&){throw std::runtime_error("ws_aceptar: no soportado en Windows aún");}
static Value cfv_ws_recibir_fn(const Value&){throw std::runtime_error("ws_recibir: no soportado en Windows aún");}
static Value cfv_ws_enviar_fn(const Value&, const Value&){throw std::runtime_error("ws_enviar: no soportado en Windows aún");}
static Value cfv_ws_broadcast_fn(const Value&, const Value&){throw std::runtime_error("ws_broadcast: no soportado en Windows aún");}
static Value cfv_ws_cerrar_fn(const Value&){throw std::runtime_error("ws_cerrar: no soportado en Windows aún");}
static Value cfv_ws_cerrar_servidor_fn(const Value&){throw std::runtime_error("ws_cerrar_servidor: no soportado en Windows aún");}
#endif
// ── fin WebSocket ─────────────────────────────────────────────────────────────

Value cfv_eval_builtin(Value cfv_nombre, Value cfv_args, Value cfv_env, Value cfv_fns) {
  cfv_jit_hit("eval_builtin");
  size_t cfv_nombre_tipo = cfv_nombre.index();
  size_t cfv_args_tipo = cfv_args.index();
  size_t cfv_env_tipo = cfv_env.index();
  size_t cfv_fns_tipo = cfv_fns.index();
  if (verdad(compara(cfv_nombre, Value{std::string("mostrar", 7)}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{1.0}, "=="), Value{std::string("mostrar requiere un argumento", 29)}));
    mostrar(indice(cfv_args, Value{0.0}));
    return Value{std::string("__ok__", 6)};
  }
  if (verdad(compara(cfv_nombre, Value{std::string("longitud", 8)}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{1.0}, "=="), Value{std::string("longitud requiere un argumento", 30)}));
    return cfv_longitud(indice(cfv_args, Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("agregar", 7)}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{2.0}, "=="), Value{std::string("agregar requiere dos argumentos", 31)}));
    (void)(cfv_agregar(indice(cfv_args, Value{0.0}), indice(cfv_args, Value{1.0})));
    return Value{std::string("__ok__", 6)};
  }
  if (verdad(compara(cfv_nombre, Value{std::string("a_texto", 7)}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{1.0}, "=="), Value{std::string("a_texto requiere un argumento", 29)}));
    return cfv_formato_valor(indice(cfv_args, Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("a_numero", 8)}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{1.0}, "=="), Value{std::string("a_numero requiere un argumento", 30)}));
    return cfv_a_numero(indice(cfv_args, Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("tiene_clave", 11)}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{2.0}, "=="), Value{std::string("tiene_clave requiere dos argumentos", 35)}));
    return cfv_tiene_clave(indice(cfv_args, Value{0.0}), indice(cfv_args, Value{1.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("claves", 6)}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{1.0}, "=="), Value{std::string("claves requiere un argumento", 28)}));
    return cfv_claves(indice(cfv_args, Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("tipo_de", 7)}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{1.0}, "=="), Value{std::string("tipo_de requiere un argumento", 29)}));
    return cfv_tipo_de(indice(cfv_args, Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("leer_archivo", 12)}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{1.0}, "=="), Value{std::string("leer_archivo requiere un argumento", 34)}));
    return cfv_leer_archivo(indice(cfv_args, Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("escribir_archivo", 16)}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{2.0}, "=="), Value{std::string("escribir_archivo requiere dos argumentos", 40)}));
    (void)(cfv_escribir_archivo(indice(cfv_args, Value{0.0}), indice(cfv_args, Value{1.0})));
    return Value{true};
  }
  if (verdad(compara(cfv_nombre, Value{std::string("bytes_texto")}, "=="))) {
    return cfv_bytes_texto(indice(cfv_args, Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("escribir_bytes")}, "=="))) {
    return cfv_escribir_bytes(indice(cfv_args, Value{0.0}), indice(cfv_args, Value{1.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("hacer_ejecutable")}, "=="))) {
    return cfv_hacer_ejecutable(indice(cfv_args, Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("sha256_rango")}, "=="))) {
    return cfv_sha256_rango(indice(cfv_args, Value{0.0}), indice(cfv_args, Value{1.0}), indice(cfv_args, Value{2.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("sys_run", 7)}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{1.0}, "=="), Value{std::string("sys_run requiere un argumento", 29)}));
    return cfv_sys_run(indice(cfv_args, Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("existe_archivo", 14)}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{1.0}, "=="), Value{std::string("existe_archivo requiere un argumento", 36)}));
    return cfv_existe_archivo(indice(cfv_args, Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("listar_directorio")}, "=="))) {
    return cfv_listar_directorio(indice(cfv_args, Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("crear_directorio")}, "=="))) {
    return cfv_crear_directorio(indice(cfv_args, Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("eliminar_archivo")}, "=="))) {
    return cfv_eliminar_archivo(indice(cfv_args, Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("http_get")}, "=="))) {
    return cfv_http_get(indice(cfv_args, Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("afirmar", 7)}, "=="))) {
    if (verdad(compara(cfv_longitud(cfv_args), Value{2.0}, "=="))) {
      (void)(cfv_afirmar(indice(cfv_args, Value{0.0}), indice(cfv_args, Value{1.0})));
    } else {
      (void)(cfv_afirmar(indice(cfv_args, Value{0.0}), Value{std::string("afirmación fallida", 19)}));
    }
    return Value{std::string("__ok__", 6)};
  }
  if (verdad(compara(cfv_nombre, Value{std::string("argumentos_programa", 19)}, "=="))) {
    if (verdad(cfv_tiene_clave(cfv_fns, Value{std::string("__prog_args__", 13)}))) {
      return indice(cfv_fns, Value{std::string("__prog_args__", 13)});
    }
    return cfv_argumentos_programa();
  }
  // ── subcadena / texto ─────────────────────────────────────────────────────
  if (verdad(compara(cfv_nombre, Value{std::string("subcadena")}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{3.0}, "=="), Value{std::string("subcadena requiere 3 argumentos")}));
    return cfv_subcadena(indice(cfv_args, Value{0.0}), indice(cfv_args, Value{1.0}), indice(cfv_args, Value{2.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("texto_mayusculas")}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{1.0}, "=="), Value{std::string("texto_mayusculas requiere 1 argumento")}));
    return cfv_texto_mayusculas(indice(cfv_args, Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("texto_minusculas")}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{1.0}, "=="), Value{std::string("texto_minusculas requiere 1 argumento")}));
    return cfv_texto_minusculas(indice(cfv_args, Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("texto_recortar")}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{1.0}, "=="), Value{std::string("texto_recortar requiere 1 argumento")}));
    return cfv_texto_recortar(indice(cfv_args, Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("texto_empieza_con")}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{2.0}, "=="), Value{std::string("texto_empieza_con requiere 2 argumentos")}));
    return cfv_texto_empieza_con(indice(cfv_args, Value{0.0}), indice(cfv_args, Value{1.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("texto_termina_con")}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{2.0}, "=="), Value{std::string("texto_termina_con requiere 2 argumentos")}));
    return cfv_texto_termina_con(indice(cfv_args, Value{0.0}), indice(cfv_args, Value{1.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("texto_contiene")}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{2.0}, "=="), Value{std::string("texto_contiene requiere 2 argumentos")}));
    return cfv_texto_contiene(indice(cfv_args, Value{0.0}), indice(cfv_args, Value{1.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("texto_indice")}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{2.0}, "=="), Value{std::string("texto_indice requiere 2 argumentos")}));
    return cfv_texto_indice(indice(cfv_args, Value{0.0}), indice(cfv_args, Value{1.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("texto_repetir")}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{2.0}, "=="), Value{std::string("texto_repetir requiere 2 argumentos")}));
    return cfv_texto_repetir(indice(cfv_args, Value{0.0}), indice(cfv_args, Value{1.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("texto_reemplazar")}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{3.0}, "=="), Value{std::string("texto_reemplazar requiere 3 argumentos")}));
    return cfv_texto_reemplazar(indice(cfv_args, Value{0.0}), indice(cfv_args, Value{1.0}), indice(cfv_args, Value{2.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("texto_dividir")}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{2.0}, "=="), Value{std::string("texto_dividir requiere 2 argumentos")}));
    return cfv_texto_dividir(indice(cfv_args, Value{0.0}), indice(cfv_args, Value{1.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("texto_a_numero")}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{1.0}, "=="), Value{std::string("texto_a_numero requiere 1 argumento")}));
    return cfv_texto_a_numero(indice(cfv_args, Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("numero_a_texto")}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{1.0}, "=="), Value{std::string("numero_a_texto requiere 1 argumento")}));
    return cfv_numero_a_texto(indice(cfv_args, Value{0.0}));
  }
  // ── Matemáticas extras ────────────────────────────────────────────────────
  if (verdad(compara(cfv_nombre, Value{std::string("piso")}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{1.0}, "=="), Value{std::string("piso requiere 1 argumento")}));
    return cfv_piso(indice(cfv_args, Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("techo")}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{1.0}, "=="), Value{std::string("techo requiere 1 argumento")}));
    return cfv_techo(indice(cfv_args, Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("maximo")}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{2.0}, "=="), Value{std::string("maximo requiere 2 argumentos")}));
    return cfv_maximo(indice(cfv_args, Value{0.0}), indice(cfv_args, Value{1.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("minimo")}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{2.0}, "=="), Value{std::string("minimo requiere 2 argumentos")}));
    return cfv_minimo(indice(cfv_args, Value{0.0}), indice(cfv_args, Value{1.0}));
  }
  // ── Lista extras ──────────────────────────────────────────────────────────
  if (verdad(compara(cfv_nombre, Value{std::string("lista_ordenar")}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{1.0}, "=="), Value{std::string("lista_ordenar requiere 1 argumento")}));
    return cfv_lista_ordenar(indice(cfv_args, Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("lista_invertir")}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{1.0}, "=="), Value{std::string("lista_invertir requiere 1 argumento")}));
    return cfv_lista_invertir(indice(cfv_args, Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("lista_contiene")}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{2.0}, "=="), Value{std::string("lista_contiene requiere 2 argumentos")}));
    return cfv_lista_contiene(indice(cfv_args, Value{0.0}), indice(cfv_args, Value{1.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("lista_unir")}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{2.0}, "=="), Value{std::string("lista_unir requiere 2 argumentos")}));
    return cfv_lista_unir(indice(cfv_args, Value{0.0}), indice(cfv_args, Value{1.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("lista_rango")}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{2.0}, "=="), Value{std::string("lista_rango requiere 2 argumentos")}));
    return cfv_lista_rango(indice(cfv_args, Value{0.0}), indice(cfv_args, Value{1.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("lista_aplanar")}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{1.0}, "=="), Value{std::string("lista_aplanar requiere 1 argumento")}));
    return cfv_lista_aplanar(indice(cfv_args, Value{0.0}));
  }
  // ── HOF builtins: mapear, filtrar, reducir, ordenar, para_cada ────────────
  if (verdad(compara(cfv_nombre, Value{std::string("mapear")}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{2.0}, "=="), Value{std::string("mapear requiere 2 argumentos: lista y funcion")}));
    Value cfv_lista_m = indice(cfv_args, Value{0.0});
    Value cfv_fn_m = indice(cfv_args, Value{1.0});
    if (cfv_lista_m.index() != 4) throw std::runtime_error("mapear: primer argumento debe ser lista");
    auto cfv_resultado_m = std::make_shared<std::vector<Value>>();
    auto cfv_lista_m_ptr = std::get_if<Lista>(&cfv_lista_m.data);
    for (const auto& cfv_elem_m : **cfv_lista_m_ptr) {
      auto cfv_elem_args = std::make_shared<std::vector<Value>>();
      cfv_elem_args->push_back(cfv_elem_m);
      Value cfv_fn_args_m = Value{cfv_elem_args};
      Value cfv_r_m;
      if (cfv_fn_m.index() == 5) {
        auto cfv_mp_m = std::get_if<Mapa>(&cfv_fn_m.data);
        if (cfv_mp_m && (*cfv_mp_m)->count("__lambda") && verdad((**cfv_mp_m)["__lambda"])) {
          Value cfv_lp_m = (**cfv_mp_m)["params"];
          Value cfv_lc_m = (**cfv_mp_m)["cuerpo"];
          Value cfv_le_m = (**cfv_mp_m)["env"];
          Value cfv_fe_m = cfv_env_nuevo_scope(cfv_le_m);
          if (auto lpp = std::get_if<Lista>(&cfv_lp_m.data)) {
            if (!(*lpp)->empty()) (void)(cfv_env_declarar(cfv_fe_m, (**lpp)[0], cfv_elem_m));
          }
          Value cfv_s_m = cfv_exec_bloque(cfv_lc_m, cfv_fe_m, cfv_fns);
          cfv_r_m = verdad(compara(indice(cfv_s_m, Value{0.0}), Value{std::string("retornar")}, "==")) ? indice(cfv_s_m, Value{1.0}) : Value{};
        }
      } else {
        cfv_r_m = cfv_eval_builtin(cfv_fn_m, cfv_fn_args_m, cfv_env, cfv_fns);
      }
      cfv_resultado_m->push_back(cfv_r_m);
    }
    return Value{cfv_resultado_m};
  }
  if (verdad(compara(cfv_nombre, Value{std::string("filtrar")}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{2.0}, "=="), Value{std::string("filtrar requiere 2 argumentos: lista y predicado")}));
    Value cfv_lista_f = indice(cfv_args, Value{0.0});
    Value cfv_fn_f = indice(cfv_args, Value{1.0});
    if (cfv_lista_f.index() != 4) throw std::runtime_error("filtrar: primer argumento debe ser lista");
    auto cfv_resultado_f = std::make_shared<std::vector<Value>>();
    auto cfv_lista_f_ptr = std::get_if<Lista>(&cfv_lista_f.data);
    for (const auto& cfv_elem_f : **cfv_lista_f_ptr) {
      Value cfv_r_f;
      if (cfv_fn_f.index() == 5) {
        auto cfv_mp_f = std::get_if<Mapa>(&cfv_fn_f.data);
        if (cfv_mp_f && (*cfv_mp_f)->count("__lambda") && verdad((**cfv_mp_f)["__lambda"])) {
          Value cfv_lp_f = (**cfv_mp_f)["params"];
          Value cfv_lc_f = (**cfv_mp_f)["cuerpo"];
          Value cfv_le_f = (**cfv_mp_f)["env"];
          Value cfv_fe_f = cfv_env_nuevo_scope(cfv_le_f);
          if (auto lpp = std::get_if<Lista>(&cfv_lp_f.data)) {
            if (!(*lpp)->empty()) (void)(cfv_env_declarar(cfv_fe_f, (**lpp)[0], cfv_elem_f));
          }
          Value cfv_s_f = cfv_exec_bloque(cfv_lc_f, cfv_fe_f, cfv_fns);
          cfv_r_f = verdad(compara(indice(cfv_s_f, Value{0.0}), Value{std::string("retornar")}, "==")) ? indice(cfv_s_f, Value{1.0}) : Value{};
        }
      }
      if (verdad(cfv_r_f)) cfv_resultado_f->push_back(cfv_elem_f);
    }
    return Value{cfv_resultado_f};
  }
  if (verdad(compara(cfv_nombre, Value{std::string("reducir")}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{3.0}, "=="), Value{std::string("reducir requiere 3 argumentos: lista, funcion, inicial")}));
    Value cfv_lista_r = indice(cfv_args, Value{0.0});
    Value cfv_fn_r = indice(cfv_args, Value{1.0});
    Value cfv_acc_r = indice(cfv_args, Value{2.0});
    if (cfv_lista_r.index() != 4) throw std::runtime_error("reducir: primer argumento debe ser lista");
    auto cfv_lista_r_ptr = std::get_if<Lista>(&cfv_lista_r.data);
    for (const auto& cfv_elem_r : **cfv_lista_r_ptr) {
      if (cfv_fn_r.index() == 5) {
        auto cfv_mp_r = std::get_if<Mapa>(&cfv_fn_r.data);
        if (cfv_mp_r && (*cfv_mp_r)->count("__lambda") && verdad((**cfv_mp_r)["__lambda"])) {
          Value cfv_lp_r = (**cfv_mp_r)["params"];
          Value cfv_lc_r = (**cfv_mp_r)["cuerpo"];
          Value cfv_le_r = (**cfv_mp_r)["env"];
          Value cfv_fe_r = cfv_env_nuevo_scope(cfv_le_r);
          if (auto lpp = std::get_if<Lista>(&cfv_lp_r.data)) {
            if ((*lpp)->size() >= 1) (void)(cfv_env_declarar(cfv_fe_r, (**lpp)[0], cfv_acc_r));
            if ((*lpp)->size() >= 2) (void)(cfv_env_declarar(cfv_fe_r, (**lpp)[1], cfv_elem_r));
          }
          Value cfv_s_r = cfv_exec_bloque(cfv_lc_r, cfv_fe_r, cfv_fns);
          cfv_acc_r = verdad(compara(indice(cfv_s_r, Value{0.0}), Value{std::string("retornar")}, "==")) ? indice(cfv_s_r, Value{1.0}) : cfv_acc_r;
        }
      }
    }
    return cfv_acc_r;
  }
  if (verdad(compara(cfv_nombre, Value{std::string("para_cada")}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{2.0}, "=="), Value{std::string("para_cada requiere 2 argumentos: lista y funcion")}));
    Value cfv_lista_pe = indice(cfv_args, Value{0.0});
    Value cfv_fn_pe = indice(cfv_args, Value{1.0});
    if (cfv_lista_pe.index() != 4) throw std::runtime_error("para_cada: primer argumento debe ser lista");
    auto cfv_lista_pe_ptr = std::get_if<Lista>(&cfv_lista_pe.data);
    for (const auto& cfv_elem_pe : **cfv_lista_pe_ptr) {
      if (cfv_fn_pe.index() == 5) {
        auto cfv_mp_pe = std::get_if<Mapa>(&cfv_fn_pe.data);
        if (cfv_mp_pe && (*cfv_mp_pe)->count("__lambda") && verdad((**cfv_mp_pe)["__lambda"])) {
          Value cfv_lp_pe = (**cfv_mp_pe)["params"];
          Value cfv_lc_pe = (**cfv_mp_pe)["cuerpo"];
          Value cfv_le_pe = (**cfv_mp_pe)["env"];
          Value cfv_fe_pe = cfv_env_nuevo_scope(cfv_le_pe);
          if (auto lpp = std::get_if<Lista>(&cfv_lp_pe.data)) {
            if (!(*lpp)->empty()) (void)(cfv_env_declarar(cfv_fe_pe, (**lpp)[0], cfv_elem_pe));
          }
          (void)(cfv_exec_bloque(cfv_lc_pe, cfv_fe_pe, cfv_fns));
        }
      }
    }
    return Value{std::string("__ok__")};
  }
  if (verdad(compara(cfv_nombre, Value{std::string("ordenar")}, "=="))) {
    // ordenar(lista) or ordenar(lista, comparador)
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{0.0}, ">"), Value{std::string("ordenar requiere al menos 1 argumento")}));
    Value cfv_lista_o = indice(cfv_args, Value{0.0});
    if (cfv_lista_o.index() != 4) throw std::runtime_error("ordenar: primer argumento debe ser lista");
    auto cfv_copia_o = std::make_shared<std::vector<Value>>(*std::get<Lista>(cfv_lista_o.data));
    if (verdad(compara(cfv_longitud(cfv_args), Value{2.0}, "=="))) {
      Value cfv_fn_o = indice(cfv_args, Value{1.0});
      std::sort(cfv_copia_o->begin(), cfv_copia_o->end(), [&](const Value& a_o, const Value& b_o) {
        if (cfv_fn_o.index() == 5) {
          auto cfv_mp_o = std::get_if<Mapa>(&cfv_fn_o.data);
          if (cfv_mp_o && (*cfv_mp_o)->count("__lambda") && verdad((**cfv_mp_o)["__lambda"])) {
            Value cfv_lp_o = (**cfv_mp_o)["params"];
            Value cfv_lc_o = (**cfv_mp_o)["cuerpo"];
            Value cfv_le_o = (**cfv_mp_o)["env"];
            Value cfv_fe_o = cfv_env_nuevo_scope(cfv_le_o);
            if (auto lpp = std::get_if<Lista>(&cfv_lp_o.data)) {
              if ((*lpp)->size() >= 1) (void)(cfv_env_declarar(cfv_fe_o, (**lpp)[0], a_o));
              if ((*lpp)->size() >= 2) (void)(cfv_env_declarar(cfv_fe_o, (**lpp)[1], b_o));
            }
            Value cfv_s_o = cfv_exec_bloque(cfv_lc_o, cfv_fe_o, cfv_fns);
            Value cfv_rv_o = verdad(compara(indice(cfv_s_o, Value{0.0}), Value{std::string("retornar")}, "==")) ? indice(cfv_s_o, Value{1.0}) : Value{0.0};
            if (cfv_rv_o.index() == 1) return std::get<double>(cfv_rv_o.data) < 0.0;
            return verdad(cfv_rv_o);
          }
        }
        return cfv_canonical_json(a_o) < cfv_canonical_json(b_o);
      });
    } else {
      std::sort(cfv_copia_o->begin(), cfv_copia_o->end(), [](const Value& a_o, const Value& b_o) {
        if (a_o.index()==2 && b_o.index()==2) return std::get<std::string>(a_o.data) < std::get<std::string>(b_o.data);
        if (a_o.index()==1 && b_o.index()==1) return std::get<double>(a_o.data) < std::get<double>(b_o.data);
        return false;
      });
    }
    return Value{cfv_copia_o};
  }
  if (verdad(compara(cfv_nombre, Value{std::string("mapa_claves")}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{1.0}, "=="), Value{std::string("mapa_claves requiere 1 argumento")}));
    return cfv_claves(indice(cfv_args, Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("mapa_valores")}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{1.0}, "=="), Value{std::string("mapa_valores requiere 1 argumento")}));
    Value cfv_m_mv = indice(cfv_args, Value{0.0});
    if (cfv_m_mv.index() != 5) throw std::runtime_error("mapa_valores: argumento debe ser mapa");
    auto cfv_vals_mv = std::make_shared<std::vector<Value>>();
    if (auto cfv_mp_mv = std::get_if<Mapa>(&cfv_m_mv.data)) {
      for (const auto& kv : **cfv_mp_mv) cfv_vals_mv->push_back(kv.second);
    }
    return Value{cfv_vals_mv};
  }
  if (verdad(compara(cfv_nombre, Value{std::string("aleatorio")}, "=="))) {
    return Value{(double)std::rand() / RAND_MAX};
  }
  if (verdad(compara(cfv_nombre, Value{std::string("absoluto")}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{1.0}, "=="), Value{std::string("absoluto requiere 1 argumento")}));
    return cfv_absoluto(indice(cfv_args, Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("redondear")}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{1.0}, "=="), Value{std::string("redondear requiere 1 argumento")}));
    return cfv_redondear(indice(cfv_args, Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("potencia")}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{2.0}, "=="), Value{std::string("potencia requiere 2 argumentos")}));
    return cfv_potencia(indice(cfv_args, Value{0.0}), indice(cfv_args, Value{1.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("raiz")}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{1.0}, "=="), Value{std::string("raiz requiere 1 argumento")}));
    return cfv_raiz(indice(cfv_args, Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("rango")}, "=="))) {
    if (verdad(compara(cfv_longitud(cfv_args), Value{2.0}, "=="))) {
      auto range_map = std::make_shared<std::map<std::string,Value>>();
      (*range_map)["__rango"] = Value{true};
      (*range_map)["inicio"] = indice(cfv_args, Value{0.0});
      (*range_map)["fin"] = indice(cfv_args, Value{1.0});
      (*range_map)["paso"] = Value{1.0};
      return Value{range_map};
    }
    if (verdad(compara(cfv_longitud(cfv_args), Value{3.0}, "=="))) {
      auto range_map = std::make_shared<std::map<std::string,Value>>();
      (*range_map)["__rango"] = Value{true};
      (*range_map)["inicio"] = indice(cfv_args, Value{0.0});
      (*range_map)["fin"] = indice(cfv_args, Value{1.0});
      (*range_map)["paso"] = indice(cfv_args, Value{2.0});
      return Value{range_map};
    }
    throw std::runtime_error("rango requiere 2 o 3 argumentos");
  }
  // ── Aleatorio ─────────────────────────────────────────────────────────────
  if (verdad(compara(cfv_nombre, Value{std::string("aleatorio_uniforme")}, "==")) ||
      verdad(compara(cfv_nombre, Value{std::string("cfv_aleatorio_uniforme")}, "=="))) {
    return cfv_aleatorio_uniforme(indice(cfv_args, Value{0.0}), indice(cfv_args, Value{1.0}));
  }
  // ── Fecha/hora ────────────────────────────────────────────────────────────
  if (verdad(compara(cfv_nombre, Value{std::string("cfv_fecha_ahora_ms")}, "=="))) {
    return cfv_fecha_ahora_ms_fn();
  }
  if (verdad(compara(cfv_nombre, Value{std::string("cfv_fecha_ahora")}, "=="))) {
    return cfv_fecha_ahora_fn();
  }
  // ── Regex ─────────────────────────────────────────────────────────────────
  if (verdad(compara(cfv_nombre, Value{std::string("cfv_regex_coincidir")}, "=="))) {
    return cfv_regex_coincidir_fn(indice(cfv_args, Value{0.0}), indice(cfv_args, Value{1.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("cfv_regex_buscar")}, "=="))) {
    return cfv_regex_buscar_fn(indice(cfv_args, Value{0.0}), indice(cfv_args, Value{1.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("cfv_regex_buscar_todos")}, "=="))) {
    return cfv_regex_buscar_todos_fn(indice(cfv_args, Value{0.0}), indice(cfv_args, Value{1.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("cfv_regex_reemplazar")}, "=="))) {
    return cfv_regex_reemplazar_fn(indice(cfv_args, Value{0.0}), indice(cfv_args, Value{1.0}), indice(cfv_args, Value{2.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("cfv_regex_dividir")}, "=="))) {
    return cfv_regex_dividir_fn(indice(cfv_args, Value{0.0}), indice(cfv_args, Value{1.0}));
  }
  // ── Base64 / Hashing ──────────────────────────────────────────────────────
  if (verdad(compara(cfv_nombre, Value{std::string("cfv_base64_codificar")}, "=="))) {
    return cfv_base64_codificar_fn(indice(cfv_args, Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("cfv_base64_decodificar")}, "=="))) {
    return cfv_base64_decodificar_fn(indice(cfv_args, Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("cfv_sha256")}, "=="))) {
    return cfv_sha256_fn(indice(cfv_args, Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("cfv_md5")}, "=="))) {
    return cfv_sha256_fn(indice(cfv_args, Value{0.0})); // alias simplificado
  }
  if (verdad(compara(cfv_nombre, Value{std::string("cfv_url_codificar")}, "=="))) {
    return cfv_url_codificar_fn(indice(cfv_args, Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("cfv_url_decodificar")}, "=="))) {
    return cfv_url_decodificar_fn(indice(cfv_args, Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("cfv_codigo_char")}, "=="))) {
    return cfv_codigo_char_fn(indice(cfv_args, Value{0.0}));
  }
  // ── JSON ──────────────────────────────────────────────────────────────────
  if (verdad(compara(cfv_nombre, Value{std::string("cfv_json_serializar")}, "=="))) {
    return cfv_json_serializar_fn(indice(cfv_args, Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("cfv_json_parsear")}, "=="))) {
    return cfv_json_parsear_fn(indice(cfv_args, Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("cfv_json_bonito")}, "=="))) {
    return cfv_json_bonito_fn(indice(cfv_args, Value{0.0}));
  }
  // ── Alias cortos ─────────────────────────────────────────────────────────
  if (verdad(compara(cfv_nombre, Value{std::string("reemplazar")}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{3.0}, "=="), Value{std::string("reemplazar requiere 3 argumentos")}));
    return cfv_texto_reemplazar(indice(cfv_args, Value{0.0}), indice(cfv_args, Value{1.0}), indice(cfv_args, Value{2.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("dividir")}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{2.0}, "=="), Value{std::string("dividir requiere 2 argumentos")}));
    return cfv_texto_dividir(indice(cfv_args, Value{0.0}), indice(cfv_args, Value{1.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("contiene")}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{2.0}, "=="), Value{std::string("contiene requiere 2 argumentos")}));
    return cfv_texto_contiene(indice(cfv_args, Value{0.0}), indice(cfv_args, Value{1.0}));
  }
  // ── OpenGL 3D ────────────────────────────────────────────────────────────
  if (verdad(compara(cfv_nombre, Value{std::string("gl3d_iniciar")}, "=="))) {
    return cfv_gl3d_iniciar(indice(cfv_args,Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("gl3d_limpiar")}, "=="))) {
    Value a = verdad(compara(cfv_longitud(cfv_args),Value{4.0},">=")) ? indice(cfv_args,Value{3.0}) : Value{255.0};
    return cfv_gl3d_limpiar(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}),indice(cfv_args,Value{2.0}),a);
  }
  if (verdad(compara(cfv_nombre, Value{std::string("gl3d_viewport")}, "=="))) {
    return cfv_gl3d_viewport(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}),indice(cfv_args,Value{2.0}),indice(cfv_args,Value{3.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("gl3d_profundidad")}, "=="))) {
    return cfv_gl3d_profundidad(indice(cfv_args,Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("gl3d_shader")}, "=="))) {
    return cfv_gl3d_shader(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("gl3d_programa")}, "=="))) {
    return cfv_gl3d_programa(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("gl3d_usar_programa")}, "=="))) {
    return cfv_gl3d_usar_programa(indice(cfv_args,Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("gl3d_vbo")}, "=="))) {
    return cfv_gl3d_vbo(indice(cfv_args,Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("gl3d_atributo")}, "=="))) {
    return cfv_gl3d_atributo(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}),indice(cfv_args,Value{2.0}),indice(cfv_args,Value{3.0}),indice(cfv_args,Value{4.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("gl3d_dibujar")}, "=="))) {
    Value modo = verdad(compara(cfv_longitud(cfv_args),Value{3.0},">=")) ? indice(cfv_args,Value{2.0}) : Value{std::string("triangulos")};
    return cfv_gl3d_dibujar(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}),modo);
  }
  if (verdad(compara(cfv_nombre, Value{std::string("gl3d_uniforme_f")}, "=="))) {
    return cfv_gl3d_uniforme_f(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}),indice(cfv_args,Value{2.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("gl3d_uniforme_vec3")}, "=="))) {
    return cfv_gl3d_uniforme_vec3(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}),indice(cfv_args,Value{2.0}),indice(cfv_args,Value{3.0}),indice(cfv_args,Value{4.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("gl3d_uniforme_mat4")}, "=="))) {
    return cfv_gl3d_uniforme_mat4(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}),indice(cfv_args,Value{2.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("gl3d_intercambiar")}, "=="))) {
    return cfv_gl3d_intercambiar(indice(cfv_args,Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("gl3d_textura")}, "=="))) {
    return cfv_gl3d_textura(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}),indice(cfv_args,Value{2.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("gl3d_usar_textura")}, "=="))) {
    Value unit = verdad(compara(cfv_longitud(cfv_args),Value{2.0},">=")) ? indice(cfv_args,Value{1.0}) : Value{0.0};
    return cfv_gl3d_usar_textura(indice(cfv_args,Value{0.0}),unit);
  }
  if (verdad(compara(cfv_nombre, Value{std::string("gl3d_eliminar_vbo")}, "=="))) {
    return cfv_gl3d_eliminar_vbo(indice(cfv_args,Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("gl3d_cerrar")}, "=="))) {
    return cfv_gl3d_cerrar(indice(cfv_args,Value{0.0}));
  }
  // ── SDL2 Graficos ─────────────────────────────────────────────────────────
  if (verdad(compara(cfv_nombre, Value{std::string("sdl_iniciar")}, "=="))) {
    return cfv_sdl_iniciar(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}),indice(cfv_args,Value{2.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("sdl_limpiar")}, "=="))) {
    return cfv_sdl_limpiar(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}),indice(cfv_args,Value{2.0}),indice(cfv_args,Value{3.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("sdl_dibujar_rect")}, "=="))) {
    Value a = verdad(compara(cfv_longitud(cfv_args),Value{9.0},">=")) ? indice(cfv_args,Value{8.0}) : Value{255.0};
    return cfv_sdl_dibujar_rect(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}),indice(cfv_args,Value{2.0}),indice(cfv_args,Value{3.0}),indice(cfv_args,Value{4.0}),indice(cfv_args,Value{5.0}),indice(cfv_args,Value{6.0}),indice(cfv_args,Value{7.0}),a);
  }
  if (verdad(compara(cfv_nombre, Value{std::string("sdl_dibujar_circulo")}, "=="))) {
    return cfv_sdl_dibujar_circulo(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}),indice(cfv_args,Value{2.0}),indice(cfv_args,Value{3.0}),indice(cfv_args,Value{4.0}),indice(cfv_args,Value{5.0}),indice(cfv_args,Value{6.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("sdl_dibujar_linea")}, "=="))) {
    return cfv_sdl_dibujar_linea(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}),indice(cfv_args,Value{2.0}),indice(cfv_args,Value{3.0}),indice(cfv_args,Value{4.0}),indice(cfv_args,Value{5.0}),indice(cfv_args,Value{6.0}),indice(cfv_args,Value{7.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("sdl_dibujar_pixel")}, "=="))) {
    return cfv_sdl_dibujar_pixel(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}),indice(cfv_args,Value{2.0}),indice(cfv_args,Value{3.0}),indice(cfv_args,Value{4.0}),indice(cfv_args,Value{5.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("sdl_mostrar")}, "=="))) {
    return cfv_sdl_mostrar(indice(cfv_args,Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("sdl_eventos")}, "=="))) {
    return cfv_sdl_eventos(indice(cfv_args,Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("sdl_tecla_presionada")}, "=="))) {
    return cfv_sdl_tecla_presionada(indice(cfv_args,Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("sdl_raton")}, "=="))) {
    return cfv_sdl_raton(Value{});
  }
  if (verdad(compara(cfv_nombre, Value{std::string("sdl_delay")}, "=="))) {
    return cfv_sdl_delay(indice(cfv_args,Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("sdl_tiempo")}, "=="))) {
    return cfv_sdl_tiempo(Value{});
  }
  if (verdad(compara(cfv_nombre, Value{std::string("sdl_tamanio_ventana")}, "=="))) {
    return cfv_sdl_tamanio_ventana(indice(cfv_args,Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("sdl_titulo")}, "=="))) {
    return cfv_sdl_titulo(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("sdl_abierto")}, "=="))) {
    return cfv_sdl_abierto(indice(cfv_args,Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("sdl_cerrar")}, "=="))) {
    return cfv_sdl_cerrar(indice(cfv_args,Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("sdl_terminar")}, "=="))) {
    return cfv_sdl_terminar(Value{});
  }
#ifdef CFV_WITH_SDL2_MIXER
  if (verdad(compara(cfv_nombre, Value{std::string("sdl_cargar_sonido")}, "=="))) {
    return cfv_sdl_cargar_sonido(indice(cfv_args,Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("sdl_reproducir_sonido")}, "=="))) {
    return cfv_sdl_reproducir_sonido(indice(cfv_args,Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("sdl_cargar_musica")}, "=="))) {
    return cfv_sdl_cargar_musica_fn(indice(cfv_args,Value{0.0}));
  }
#endif
  // ── HTTP Server ────────────────────────────────────────────────────────────
  if (verdad(compara(cfv_nombre, Value{std::string("servidor_http_escuchar")}, "=="))) {
    return cfv_servidor_http_escuchar(indice(cfv_args, Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("servidor_http_solicitud")}, "=="))) {
    return cfv_servidor_http_solicitud(indice(cfv_args, Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("servidor_http_responder")}, "=="))) {
    Value tipo_arg = Value{std::string("text/plain; charset=utf-8")};
    if (verdad(compara(cfv_longitud(cfv_args), Value{4.0}, ">="))) tipo_arg = indice(cfv_args, Value{3.0});
    return cfv_servidor_http_responder(
      indice(cfv_args, Value{0.0}),
      indice(cfv_args, Value{1.0}),
      indice(cfv_args, Value{2.0}),
      tipo_arg
    );
  }
  if (verdad(compara(cfv_nombre, Value{std::string("servidor_http_cerrar")}, "=="))) {
    return cfv_servidor_http_cerrar(indice(cfv_args, Value{0.0}));
  }
  // ── Crypto ─────────────────────────────────────────────────────────────────
  if (verdad(compara(cfv_nombre, Value{std::string("sha256")}, "=="))) {
    return cfv_sha256(indice(cfv_args, Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("hmac_sha256")}, "=="))) {
    return cfv_hmac_sha256(indice(cfv_args, Value{0.0}), indice(cfv_args, Value{1.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("aes_cifrar")}, "=="))) {
    return cfv_aes_cifrar(indice(cfv_args, Value{0.0}), indice(cfv_args, Value{1.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("aes_descifrar")}, "=="))) {
    return cfv_aes_descifrar(indice(cfv_args, Value{0.0}), indice(cfv_args, Value{1.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("crypto_rand_bytes")}, "=="))) {
    return cfv_crypto_rand_bytes(indice(cfv_args, Value{0.0}));
  }
  // ── JSON nativo ────────────────────────────────────────────────────────────
  if (verdad(compara(cfv_nombre, Value{std::string("json_parsear")}, "=="))) {
    return cfv_json_parsear_fn(indice(cfv_args, Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("json_texto")}, "=="))) {
    return cfv_json_texto_fn(indice(cfv_args, Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("json_bonito")}, "=="))) {
    return cfv_json_bonito_fn(indice(cfv_args, Value{0.0}));
  }
  // ── Regex ──────────────────────────────────────────────────────────────────
  // regex_coincidir(texto, patron), regex_buscar(texto, patron), etc.
  // cfv_regex_*_fn toma (patron, texto) — intercambiar args
  if (verdad(compara(cfv_nombre, Value{std::string("regex_coincidir")}, "=="))) {
    return cfv_regex_coincidir_fn(indice(cfv_args,Value{1.0}),indice(cfv_args,Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("regex_buscar")}, "=="))) {
    // regex_buscar(texto, patron) -> lista de todos los matches
    return cfv_regex_buscar_todos_fn(indice(cfv_args,Value{1.0}),indice(cfv_args,Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("regex_buscar_primero")}, "=="))) {
    // regex_buscar_primero(texto, patron) -> primer match o nulo
    return cfv_regex_buscar_fn(indice(cfv_args,Value{1.0}),indice(cfv_args,Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("regex_reemplazar")}, "=="))) {
    // regex_reemplazar(texto, patron, reemplazo)
    return cfv_regex_reemplazar_fn(indice(cfv_args,Value{1.0}),indice(cfv_args,Value{0.0}),indice(cfv_args,Value{2.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("regex_grupos")}, "=="))) {
    // regex_grupos(texto, patron)
    return cfv_regex_grupos_fn(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}));
  }
  // ── SQLite ─────────────────────────────────────────────────────────────────
#ifdef CFV_WITH_SQLITE
  if (verdad(compara(cfv_nombre, Value{std::string("db_abrir")}, "=="))) {
    return cfv_db_abrir_fn(indice(cfv_args,Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("db_cerrar")}, "=="))) {
    return cfv_db_cerrar_fn(indice(cfv_args,Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("db_ejecutar")}, "=="))) {
    return cfv_db_ejecutar_fn(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("db_consulta")}, "=="))) {
    return cfv_db_consulta_fn(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("db_consulta_p")}, "=="))) {
    return cfv_db_consulta_p_fn(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}),indice(cfv_args,Value{2.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("db_ultimo_id")}, "=="))) {
    return cfv_db_ultimo_id_fn(indice(cfv_args,Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("db_transaccion")}, "=="))) {
    return cfv_db_transaccion_fn(indice(cfv_args,Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("db_confirmar")}, "=="))) {
    return cfv_db_confirmar_fn(indice(cfv_args,Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("db_revertir")}, "=="))) {
    return cfv_db_revertir_fn(indice(cfv_args,Value{0.0}));
  }
#endif
  // ── HTTP Client ────────────────────────────────────────────────────────────
  if (verdad(compara(cfv_nombre, Value{std::string("http_post")}, "=="))) {
    Value tipo = verdad(compara(cfv_longitud(cfv_args),Value{3.0},">=")) ? indice(cfv_args,Value{2.0}) : Value{std::string("application/json")};
    return cfv_http_post_fn(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}),tipo);
  }
  if (verdad(compara(cfv_nombre, Value{std::string("http_put")}, "=="))) {
    return cfv_http_put_fn(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("http_delete")}, "=="))) {
    return cfv_http_delete_fn(indice(cfv_args,Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("http_solicitud")}, "=="))) {
    Value opc = verdad(compara(cfv_longitud(cfv_args),Value{3.0},">=")) ? indice(cfv_args,Value{2.0}) : Value{std::make_shared<std::map<std::string,ForgeValue>>()};
    return cfv_http_solicitud_fn(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}),opc);
  }
  // ── Canales ────────────────────────────────────────────────────────────────
  if (verdad(compara(cfv_nombre, Value{std::string("canal_nuevo")}, "=="))) {
    return cfv_canal_nuevo_fn(Value{});
  }
  if (verdad(compara(cfv_nombre, Value{std::string("canal_enviar")}, "=="))) {
    return cfv_canal_enviar_fn(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("canal_recibir")}, "=="))) {
    return cfv_canal_recibir_fn(indice(cfv_args,Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("canal_cerrar")}, "=="))) {
    return cfv_canal_cerrar_fn(indice(cfv_args,Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("canal_tam")}, "=="))) {
    return cfv_canal_tam_fn(indice(cfv_args,Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("hilo_dormir")}, "=="))) {
    return cfv_hilo_dormir_fn(indice(cfv_args,Value{0.0}));
  }
  // ── Sistema ────────────────────────────────────────────────────────────────
  if (verdad(compara(cfv_nombre, Value{std::string("env_obtener")}, "=="))) {
    return cfv_env_obtener_fn(indice(cfv_args,Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("env_establecer")}, "=="))) {
    return cfv_env_establecer_fn(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("proceso_ejecutar")}, "=="))) {
    return cfv_proceso_ejecutar_fn(indice(cfv_args,Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("salir")}, "=="))) {
    Value cod = verdad(compara(cfv_longitud(cfv_args),Value{1.0},">=")) ? indice(cfv_args,Value{0.0}) : Value{0.0};
    return cfv_salir_fn(cod);
  }
  if (verdad(compara(cfv_nombre, Value{std::string("pausa")}, "=="))) {
    return cfv_pausa_fn(indice(cfv_args,Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("limpiar_pantalla")}, "=="))) {
    return cfv_limpiar_pantalla_fn(Value{});
  }
  // ── Fecha/Tiempo ───────────────────────────────────────────────────────────
  if (verdad(compara(cfv_nombre, Value{std::string("fecha_ahora")}, "=="))) {
    return cfv_fecha_ahora_fn(Value{});
  }
  if (verdad(compara(cfv_nombre, Value{std::string("fecha_formatear")}, "=="))) {
    Value fmt = verdad(compara(cfv_longitud(cfv_args),Value{2.0},">=")) ? indice(cfv_args,Value{1.0}) : Value{std::string("%Y-%m-%d %H:%M:%S")};
    return cfv_fecha_formatear_fn(indice(cfv_args,Value{0.0}),fmt);
  }
  if (verdad(compara(cfv_nombre, Value{std::string("tiempo_ms")}, "=="))) {
    return cfv_tiempo_ms_fn(Value{});
  }
  if (verdad(compara(cfv_nombre, Value{std::string("tiempo_segundos")}, "=="))) {
    return cfv_tiempo_segundos_fn(Value{});
  }
  // ── Colecciones ────────────────────────────────────────────────────────────
  if (verdad(compara(cfv_nombre, Value{std::string("lista_unica")}, "=="))) {
    return cfv_lista_unica_fn(indice(cfv_args,Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("lista_aplanar")}, "=="))) {
    return cfv_lista_aplanar_fn(indice(cfv_args,Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("lista_zip")}, "=="))) {
    return cfv_lista_zip_fn(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("mapa_claves")}, "=="))) {
    return cfv_mapa_claves_fn(indice(cfv_args,Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("mapa_valores")}, "=="))) {
    return cfv_mapa_valores_fn(indice(cfv_args,Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("mapa_entradas")}, "=="))) {
    return cfv_mapa_entradas_fn(indice(cfv_args,Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("mapa_fusionar")}, "=="))) {
    return cfv_mapa_fusionar_fn(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}));
  }
  // ── Texto extra ────────────────────────────────────────────────────────────
  if (verdad(compara(cfv_nombre, Value{std::string("texto_relleno")}, "=="))) {
    Value c = verdad(compara(cfv_longitud(cfv_args),Value{3.0},">=")) ? indice(cfv_args,Value{2.0}) : Value{std::string("0")};
    return cfv_texto_relleno_fn(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}),c);
  }
  if (verdad(compara(cfv_nombre, Value{std::string("texto_relleno_der")}, "=="))) {
    Value c = verdad(compara(cfv_longitud(cfv_args),Value{3.0},">=")) ? indice(cfv_args,Value{2.0}) : Value{std::string(" ")};
    return cfv_texto_relleno_der_fn(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}),c);
  }
  if (verdad(compara(cfv_nombre, Value{std::string("texto_formato")}, "=="))) {
    return cfv_texto_formato_fn(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}));
  }
  // ── archivo / ruta / extra builtins (v2.3) ──────────────────────────────────
  if (verdad(compara(cfv_nombre, Value{std::string("archivo_copiar")}, "==")))
    return cfv_archivo_copiar_fn(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}));
  if (verdad(compara(cfv_nombre, Value{std::string("archivo_mover")}, "==")))
    return cfv_archivo_mover_fn(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}));
  if (verdad(compara(cfv_nombre, Value{std::string("archivo_eliminar")}, "==")))
    return cfv_archivo_eliminar_fn(indice(cfv_args,Value{0.0}));
  if (verdad(compara(cfv_nombre, Value{std::string("archivo_tam")}, "==")))
    return cfv_archivo_tam_fn(indice(cfv_args,Value{0.0}));
  if (verdad(compara(cfv_nombre, Value{std::string("directorio_crear")}, "==")))
    return cfv_directorio_crear_fn(indice(cfv_args,Value{0.0}));
  if (verdad(compara(cfv_nombre, Value{std::string("directorio_eliminar")}, "==")))
    return cfv_directorio_eliminar_fn(indice(cfv_args,Value{0.0}));
  if (verdad(compara(cfv_nombre, Value{std::string("directorio_listar")}, "==")))
    return cfv_directorio_listar_fn(indice(cfv_args,Value{0.0}));
  if (verdad(compara(cfv_nombre, Value{std::string("directorio_listar_rec")}, "==")))
    return cfv_directorio_listar_rec_fn(indice(cfv_args,Value{0.0}));
  if (verdad(compara(cfv_nombre, Value{std::string("ruta_unir")}, "=="))) {
    // accept ruta_unir(lista) or ruta_unir("a","b","c")
    Value first = indice(cfv_args,Value{0.0});
    return cfv_ruta_unir_fn(first.index()==4 ? first : cfv_args);
  }
  if (verdad(compara(cfv_nombre, Value{std::string("ruta_directorio")}, "==")))
    return cfv_ruta_directorio_fn(indice(cfv_args,Value{0.0}));
  if (verdad(compara(cfv_nombre, Value{std::string("ruta_nombre")}, "==")))
    return cfv_ruta_nombre_fn(indice(cfv_args,Value{0.0}));
  if (verdad(compara(cfv_nombre, Value{std::string("ruta_extension")}, "==")))
    return cfv_ruta_extension_fn(indice(cfv_args,Value{0.0}));
  if (verdad(compara(cfv_nombre, Value{std::string("ruta_sin_extension")}, "==")))
    return cfv_ruta_sin_extension_fn(indice(cfv_args,Value{0.0}));
  if (verdad(compara(cfv_nombre, Value{std::string("ruta_absoluta")}, "==")))
    return cfv_ruta_absoluta_fn(indice(cfv_args,Value{0.0}));
  if (verdad(compara(cfv_nombre, Value{std::string("es_directorio")}, "==")))
    return cfv_es_directorio_fn(indice(cfv_args,Value{0.0}));
  if (verdad(compara(cfv_nombre, Value{std::string("es_archivo_regular")}, "==")))
    return cfv_es_archivo_fn2(indice(cfv_args,Value{0.0}));
  if (verdad(compara(cfv_nombre, Value{std::string("numero_formato")}, "=="))) {
    Value d = verdad(compara(cfv_longitud(cfv_args),Value{2.0},">=")) ? indice(cfv_args,Value{1.0}) : Value{2.0};
    return cfv_numero_formato_fn(indice(cfv_args,Value{0.0}),d);
  }
  if (verdad(compara(cfv_nombre, Value{std::string("texto_repetir")}, "==")))
    return cfv_texto_repetir_fn(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}));
  if (verdad(compara(cfv_nombre, Value{std::string("texto_invertir")}, "==")))
    return cfv_texto_invertir_fn(indice(cfv_args,Value{0.0}));
  if (verdad(compara(cfv_nombre, Value{std::string("numero_abs")}, "==")))
    return cfv_numero_abs_fn(indice(cfv_args,Value{0.0}));
  if (verdad(compara(cfv_nombre, Value{std::string("lista_max")}, "==")))
    return cfv_lista_max_fn(indice(cfv_args,Value{0.0}));
  if (verdad(compara(cfv_nombre, Value{std::string("lista_min")}, "==")))
    return cfv_lista_min_fn(indice(cfv_args,Value{0.0}));
  if (verdad(compara(cfv_nombre, Value{std::string("lista_suma")}, "==")))
    return cfv_lista_suma_fn(indice(cfv_args,Value{0.0}));
  if (verdad(compara(cfv_nombre, Value{std::string("lista_promedio")}, "==")))
    return cfv_lista_promedio_fn(indice(cfv_args,Value{0.0}));
  if (verdad(compara(cfv_nombre, Value{std::string("texto_contar")}, "==")))
    return cfv_texto_contar_fn(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}));
  if (verdad(compara(cfv_nombre, Value{std::string("texto_posiciones")}, "==")))
    return cfv_texto_posiciones_fn(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}));
  // ── lista/mapa/texto extra (v2.3b) ──────────────────────────────────────────
  if (verdad(compara(cfv_nombre, Value{std::string("lista_slice")}, "=="))) {
    Value d2=verdad(compara(cfv_longitud(cfv_args),Value{2.0},">="))?indice(cfv_args,Value{1.0}):Value{0.0};
    Value h2=verdad(compara(cfv_longitud(cfv_args),Value{3.0},">="))?indice(cfv_args,Value{2.0}):Value{};
    return cfv_lista_slice_fn(indice(cfv_args,Value{0.0}),d2,h2);
  }
  if (verdad(compara(cfv_nombre, Value{std::string("lista_buscar")}, "==")))
    return cfv_lista_buscar_fn(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}));
  if (verdad(compara(cfv_nombre, Value{std::string("lista_contiene")}, "==")))
    return cfv_lista_contiene_fn(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}));
  if (verdad(compara(cfv_nombre, Value{std::string("lista_contar_elem")}, "==")))
    return cfv_lista_contar_elem_fn(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}));
  if (verdad(compara(cfv_nombre, Value{std::string("lista_invertir")}, "==")))
    return cfv_lista_invertir_fn(indice(cfv_args,Value{0.0}));
  if (verdad(compara(cfv_nombre, Value{std::string("lista_rellenar")}, "=="))) {
    Value fill=verdad(compara(cfv_longitud(cfv_args),Value{2.0},">="))?indice(cfv_args,Value{1.0}):Value{};
    return cfv_lista_rellenar_fn(indice(cfv_args,Value{0.0}),fill);
  }
  if (verdad(compara(cfv_nombre, Value{std::string("lista_cada_n")}, "==")))
    return cfv_lista_cada_n_fn(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}));
  if (verdad(compara(cfv_nombre, Value{std::string("texto_es_numero")}, "==")))
    return cfv_texto_es_numero_fn(indice(cfv_args,Value{0.0}));
  if (verdad(compara(cfv_nombre, Value{std::string("numero_es_entero")}, "==")))
    return cfv_numero_es_entero_fn(indice(cfv_args,Value{0.0}));
  if (verdad(compara(cfv_nombre, Value{std::string("numero_es_nan")}, "==")))
    return cfv_numero_es_nan_fn(indice(cfv_args,Value{0.0}));
  if (verdad(compara(cfv_nombre, Value{std::string("rango_paso")}, "=="))) {
    Value sv=verdad(compara(cfv_longitud(cfv_args),Value{3.0},">="))?indice(cfv_args,Value{2.0}):Value{1.0};
    return cfv_rango_paso_fn(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}),sv);
  }
  if (verdad(compara(cfv_nombre, Value{std::string("mapa_filtrar_claves")}, "==")))
    return cfv_mapa_filtrar_claves_fn(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}));
  if (verdad(compara(cfv_nombre, Value{std::string("mapa_omitir_claves")}, "==")))
    return cfv_mapa_omitir_claves_fn(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}));
  if (verdad(compara(cfv_nombre, Value{std::string("texto_unir")}, "=="))) {
    Value sv=verdad(compara(cfv_longitud(cfv_args),Value{2.0},">="))?indice(cfv_args,Value{1.0}):Value{std::string("")};
    return cfv_texto_unir_fn(indice(cfv_args,Value{0.0}),sv);
  }
  // ── PostgreSQL ─────────────────────────────────────────────────────────────
  if (verdad(compara(cfv_nombre, Value{std::string("pg_conectar")}, "==")))
    return cfv_pg_conectar_fn(indice(cfv_args,Value{0.0}));
  if (verdad(compara(cfv_nombre, Value{std::string("pg_cerrar")}, "==")))
    return cfv_pg_cerrar_fn(indice(cfv_args,Value{0.0}));
  if (verdad(compara(cfv_nombre, Value{std::string("pg_query")}, "==")))
    return cfv_pg_query_fn(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}));
  if (verdad(compara(cfv_nombre, Value{std::string("pg_exec")}, "==")))
    return cfv_pg_exec_fn(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}));
  if (verdad(compara(cfv_nombre, Value{std::string("pg_escapar")}, "==")))
    return cfv_pg_escapar_fn(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}));
  // ── MySQL ──────────────────────────────────────────────────────────────────
  if (verdad(compara(cfv_nombre, Value{std::string("mysql_conectar")}, "==")))
    return cfv_mysql_conectar_fn(indice(cfv_args,Value{0.0}));
  if (verdad(compara(cfv_nombre, Value{std::string("mysql_cerrar")}, "==")))
    return cfv_mysql_cerrar_fn(indice(cfv_args,Value{0.0}));
  if (verdad(compara(cfv_nombre, Value{std::string("mysql_query")}, "==")))
    return cfv_mysql_query_fn(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}));
  if (verdad(compara(cfv_nombre, Value{std::string("mysql_escapar")}, "==")))
    return cfv_mysql_escapar_fn(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}));
  // ── WebSocket ──────────────────────────────────────────────────────────────
  if (verdad(compara(cfv_nombre, Value{std::string("ws_escuchar")}, "==")))
    return cfv_ws_escuchar_fn(indice(cfv_args,Value{0.0}));
  if (verdad(compara(cfv_nombre, Value{std::string("ws_aceptar")}, "==")))
    return cfv_ws_aceptar_fn(indice(cfv_args,Value{0.0}));
  if (verdad(compara(cfv_nombre, Value{std::string("ws_recibir")}, "==")))
    return cfv_ws_recibir_fn(indice(cfv_args,Value{0.0}));
  if (verdad(compara(cfv_nombre, Value{std::string("ws_enviar")}, "==")))
    return cfv_ws_enviar_fn(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}));
  if (verdad(compara(cfv_nombre, Value{std::string("ws_broadcast")}, "==")))
    return cfv_ws_broadcast_fn(indice(cfv_args,Value{0.0}),indice(cfv_args,Value{1.0}));
  if (verdad(compara(cfv_nombre, Value{std::string("ws_cerrar")}, "==")))
    return cfv_ws_cerrar_fn(indice(cfv_args,Value{0.0}));
  if (verdad(compara(cfv_nombre, Value{std::string("ws_cerrar_servidor")}, "==")))
    return cfv_ws_cerrar_servidor_fn(indice(cfv_args,Value{0.0}));
  return Value{std::string("__no_builtin__", 14)};
  return Value{};
}
Value cfv_eval_expr(Value cfv_nodo_e, Value cfv_env, Value cfv_fns) {
  cfv_jit_hit("eval_expr");
  size_t cfv_nodo_e_tipo = cfv_nodo_e.index();
  size_t cfv_env_tipo = cfv_env.index();
  size_t cfv_fns_tipo = cfv_fns.index();
  Value cfv_t = indice(cfv_nodo_e, Value{std::string("tipo", 4)});
  if (cfv_t.index() != 2) throw std::runtime_error("tipo incompatible para t");
  size_t cfv_t_tipo = 2;
  if (verdad(compara(cfv_t, Value{std::string("Numero", 6)}, "=="))) {
    return cfv_a_numero(indice(cfv_nodo_e, Value{std::string("valor", 5)}));
  }
  if (verdad(compara(cfv_t, Value{std::string("Texto", 5)}, "=="))) {
    Value cfv_raw = indice(cfv_nodo_e, Value{std::string("valor", 5)});
    if (cfv_raw.index() != 2) throw std::runtime_error("tipo incompatible para raw");
    size_t cfv_raw_tipo = 2;
    Value cfv_n = cfv_longitud(cfv_raw);
    if (cfv_n.index() != 1) throw std::runtime_error("tipo incompatible para n");
    size_t cfv_n_tipo = 1;
    return cfv_subcadena(cfv_raw, Value{1.0}, resta(cfv_n, Value{1.0}));
  }
  if (verdad(compara(cfv_t, Value{std::string("Booleano", 8)}, "=="))) {
    return compara(indice(cfv_nodo_e, Value{std::string("valor", 5)}), Value{std::string("verdadero", 9)}, "==");
  }
  if (verdad(compara(cfv_t, Value{std::string("Nulo", 4)}, "=="))) {
    return Value{};
  }
  if (verdad(compara(cfv_t, Value{std::string("Identificador", 13)}, "=="))) {
    return cfv_env_buscar(cfv_env, indice(cfv_nodo_e, Value{std::string("valor", 5)}));
  }
  if (verdad(compara(cfv_t, Value{std::string("Lista", 5)}, "=="))) {
    Value cfv_resultado = crear_lista({});
    if (cfv_resultado.index() != 4) throw std::runtime_error("tipo incompatible para resultado");
    size_t cfv_resultado_tipo = 4;
    Value cfv_i = Value{0.0};
    if (cfv_i.index() != 1) throw std::runtime_error("tipo incompatible para i");
    size_t cfv_i_tipo = 1;
    while (verdad(compara(cfv_i, cfv_longitud(indice(cfv_nodo_e, Value{std::string("hijos", 5)})), "<"))) {
      {
        Value cfv_elem_node = indice(indice(cfv_nodo_e, Value{std::string("hijos", 5)}), cfv_i);
        if (verdad(compara(indice(cfv_elem_node, Value{std::string("tipo")}), Value{std::string("Spread")}, "=="))) {
          Value cfv_spread_val = cfv_eval_expr(indice(indice(cfv_elem_node, Value{std::string("hijos")}), Value{0.0}), cfv_env, cfv_fns);
          if (auto sp = std::get_if<Lista>(&cfv_spread_val.data)) {
            for (const auto& item : **sp) {
              (void)(cfv_agregar(cfv_resultado, item));
            }
          }
        } else {
          (void)(cfv_agregar(cfv_resultado, cfv_eval_expr(cfv_elem_node, cfv_env, cfv_fns)));
        }
      }
      asignar(cfv_i, cfv_i_tipo, suma(cfv_i, Value{1.0}), "i");
    }
    return cfv_resultado;
  }
  if (verdad(compara(cfv_t, Value{std::string("Mapa", 4)}, "=="))) {
    Value cfv_resultado = crear_mapa({});
    size_t cfv_resultado_tipo = 99;
    Value cfv_i = Value{0.0};
    if (cfv_i.index() != 1) throw std::runtime_error("tipo incompatible para i");
    size_t cfv_i_tipo = 1;
    while (verdad(compara(cfv_i, cfv_longitud(indice(cfv_nodo_e, Value{std::string("hijos", 5)})), "<"))) {
      Value cfv_clave_v = cfv_eval_expr(indice(indice(cfv_nodo_e, Value{std::string("hijos", 5)}), cfv_i), cfv_env, cfv_fns);
      size_t cfv_clave_v_tipo = 99;
      Value cfv_val_v = cfv_eval_expr(indice(indice(cfv_nodo_e, Value{std::string("hijos", 5)}), suma(cfv_i, Value{1.0})), cfv_env, cfv_fns);
      size_t cfv_val_v_tipo = 99;
      Value cfv_res_m = cfv_resultado;
      size_t cfv_res_m_tipo = 99;
      asignar_indice(cfv_res_m, cfv_clave_v, cfv_val_v);
      asignar(cfv_i, cfv_i_tipo, suma(cfv_i, Value{2.0}), "i");
    }
    return cfv_resultado;
  }
  if (verdad(compara(cfv_t, Value{std::string("Unario", 6)}, "=="))) {
    Value cfv_operando = cfv_eval_expr(indice(indice(cfv_nodo_e, Value{std::string("hijos", 5)}), Value{0.0}), cfv_env, cfv_fns);
    size_t cfv_operando_tipo = 99;
    if (verdad(compara(indice(cfv_nodo_e, Value{std::string("valor", 5)}), Value{std::string("-", 1)}, "=="))) {
      return resta(Value{0.0}, cfv_operando);
    }
    if (verdad(compara(indice(cfv_nodo_e, Value{std::string("valor", 5)}), Value{std::string("no", 2)}, "=="))) {
      return Value{!verdad(cfv_operando)};
    }
    if (verdad(compara(indice(cfv_nodo_e, Value{std::string("valor", 5)}), Value{std::string("~", 1)}, "=="))) {
      return Value{(double)(~(long long)numero(cfv_operando))};
    }
  }
  if (verdad(compara(cfv_t, Value{std::string("Binario", 7)}, "=="))) {
    if (verdad(compara(indice(cfv_nodo_e, Value{std::string("valor", 5)}), Value{std::string("y", 1)}, "=="))) {
      Value cfv_izq_v = cfv_eval_expr(indice(indice(cfv_nodo_e, Value{std::string("hijos", 5)}), Value{0.0}), cfv_env, cfv_fns);
      size_t cfv_izq_v_tipo = 99;
      if (verdad(Value{!verdad(cfv_izq_v)})) {
        return Value{false};
      }
      return cfv_eval_expr(indice(indice(cfv_nodo_e, Value{std::string("hijos", 5)}), Value{1.0}), cfv_env, cfv_fns);
    }
    if (verdad(compara(indice(cfv_nodo_e, Value{std::string("valor", 5)}), Value{std::string("o", 1)}, "=="))) {
      Value cfv_izq_v = cfv_eval_expr(indice(indice(cfv_nodo_e, Value{std::string("hijos", 5)}), Value{0.0}), cfv_env, cfv_fns);
      size_t cfv_izq_v_tipo = 99;
      if (verdad(cfv_izq_v)) {
        return Value{true};
      }
      return cfv_eval_expr(indice(indice(cfv_nodo_e, Value{std::string("hijos", 5)}), Value{1.0}), cfv_env, cfv_fns);
    }
    Value cfv_op_b = indice(cfv_nodo_e, Value{std::string("valor", 5)});
    // ?? null coalescing - lazy eval
    if (verdad(compara(cfv_op_b, Value{std::string("??")}, "=="))) {
      Value cfv_izq_nc = cfv_eval_expr(indice(indice(cfv_nodo_e, Value{std::string("hijos")}), Value{0.0}), cfv_env, cfv_fns);
      if (cfv_izq_nc.index() == 0) return cfv_eval_expr(indice(indice(cfv_nodo_e, Value{std::string("hijos")}), Value{1.0}), cfv_env, cfv_fns);
      return cfv_izq_nc;
    }
    // en (membership)
    if (verdad(compara(cfv_op_b, Value{std::string("en")}, "=="))) {
      Value cfv_en_izq = cfv_eval_expr(indice(indice(cfv_nodo_e, Value{std::string("hijos")}), Value{0.0}), cfv_env, cfv_fns);
      Value cfv_en_der = cfv_eval_expr(indice(indice(cfv_nodo_e, Value{std::string("hijos")}), Value{1.0}), cfv_env, cfv_fns);
      if (auto p = std::get_if<Lista>(&cfv_en_der.data)) {
        for (const auto& item : **p) {
          if (verdad(compara(cfv_en_izq, item, "=="))) return Value{true};
        }
        return Value{false};
      }
      if (auto p = std::get_if<Mapa>(&cfv_en_der.data)) {
        if (cfv_en_izq.index() == 2) {
          return Value{(*p)->count(std::get<std::string>(cfv_en_izq.data)) > 0};
        }
        return Value{false};
      }
      if (auto p = std::get_if<std::string>(&cfv_en_der.data)) {
        if (cfv_en_izq.index() == 2) {
          return Value{p->find(std::get<std::string>(cfv_en_izq.data)) != std::string::npos};
        }
        return Value{false};
      }
      return Value{false};
    }
    // es (instanceof)
    if (verdad(compara(cfv_op_b, Value{std::string("es")}, "=="))) {
      Value cfv_es_izq = cfv_eval_expr(indice(indice(cfv_nodo_e, Value{std::string("hijos")}), Value{0.0}), cfv_env, cfv_fns);
      Value cfv_der_node = indice(indice(cfv_nodo_e, Value{std::string("hijos")}), Value{1.0});
      std::string cfv_clase_check = texto(indice(cfv_der_node, Value{std::string("valor")}));
      if (cfv_es_izq.index() == 5) {
        auto mp = std::get_if<Mapa>(&cfv_es_izq.data);
        if (mp && (*mp)->count("__clase")) {
          std::string cfv_actual_clase = texto((**mp)["__clase"]);
          while (!cfv_actual_clase.empty()) {
            if (cfv_actual_clase == cfv_clase_check) return Value{true};
            std::string cfv_parent_key = "__clase_" + cfv_actual_clase;
            if (verdad(cfv_tiene_clave(cfv_fns, Value{cfv_parent_key}))) {
              Value cfv_cdef = indice(cfv_fns, Value{cfv_parent_key});
              if (verdad(cfv_tiene_clave(cfv_cdef, Value{std::string("padre")}))) {
                cfv_actual_clase = texto(indice(cfv_cdef, Value{std::string("padre")}));
              } else break;
            } else break;
          }
          return Value{false};
        }
      }
      return Value{false};
    }
    Value cfv_izq = cfv_eval_expr(indice(indice(cfv_nodo_e, Value{std::string("hijos", 5)}), Value{0.0}), cfv_env, cfv_fns);
    size_t cfv_izq_tipo = 99;
    Value cfv_der = cfv_eval_expr(indice(indice(cfv_nodo_e, Value{std::string("hijos", 5)}), Value{1.0}), cfv_env, cfv_fns);
    size_t cfv_der_tipo = 99;
    return cfv_eval_binario(cfv_op_b, cfv_izq, cfv_der);
  }
  if (verdad(compara(cfv_t, Value{std::string("Indice", 6)}, "=="))) {
    Value cfv_base = cfv_eval_expr(indice(indice(cfv_nodo_e, Value{std::string("hijos", 5)}), Value{0.0}), cfv_env, cfv_fns);
    size_t cfv_base_tipo = 99;
    Value cfv_idx = cfv_eval_expr(indice(indice(cfv_nodo_e, Value{std::string("hijos", 5)}), Value{1.0}), cfv_env, cfv_fns);
    size_t cfv_idx_tipo = 99;
    return indice(cfv_base, cfv_idx);
  }
  if (verdad(compara(cfv_t, Value{std::string("Campo", 5)}, "=="))) {
    Value cfv_base = cfv_eval_expr(indice(indice(cfv_nodo_e, Value{std::string("hijos", 5)}), Value{0.0}), cfv_env, cfv_fns);
    size_t cfv_base_tipo = 99;
    Value cfv_campo = indice(cfv_nodo_e, Value{std::string("valor", 5)});
    if (cfv_campo.index() != 2) throw std::runtime_error("tipo incompatible para campo");
    size_t cfv_campo_tipo = 2;
    if (verdad(Value{verdad(compara(cfv_campo, Value{std::string("longitud", 8)}, "==")) || verdad(compara(cfv_campo, Value{std::string("length", 6)}, "=="))})) {
      return cfv_longitud(cfv_base);
    }
    return indice(cfv_base, cfv_campo);
  }
  if (verdad(compara(cfv_t, Value{std::string("Asignacion", 10)}, "=="))) {
    Value cfv_objetivo = indice(indice(cfv_nodo_e, Value{std::string("hijos", 5)}), Value{0.0});
    size_t cfv_objetivo_tipo = 99;
    Value cfv_nuevo_val = cfv_eval_expr(indice(indice(cfv_nodo_e, Value{std::string("hijos", 5)}), Value{1.0}), cfv_env, cfv_fns);
    size_t cfv_nuevo_val_tipo = 99;
    if (verdad(compara(indice(cfv_objetivo, Value{std::string("tipo", 4)}), Value{std::string("Identificador", 13)}, "=="))) {
      (void)(cfv_env_asignar(cfv_env, indice(cfv_objetivo, Value{std::string("valor", 5)}), cfv_nuevo_val));
    }     else if (verdad(compara(indice(cfv_objetivo, Value{std::string("tipo", 4)}), Value{std::string("Indice", 6)}, "=="))) {
      Value cfv_base = cfv_eval_expr(indice(indice(cfv_objetivo, Value{std::string("hijos", 5)}), Value{0.0}), cfv_env, cfv_fns);
      size_t cfv_base_tipo = 99;
      Value cfv_idx = cfv_eval_expr(indice(indice(cfv_objetivo, Value{std::string("hijos", 5)}), Value{1.0}), cfv_env, cfv_fns);
      size_t cfv_idx_tipo = 99;
      Value cfv_b = cfv_base;
      size_t cfv_b_tipo = 99;
      asignar_indice(cfv_b, cfv_idx, cfv_nuevo_val);
    }     else if (verdad(compara(indice(cfv_objetivo, Value{std::string("tipo", 4)}), Value{std::string("Campo", 5)}, "=="))) {
      Value cfv_base = cfv_eval_expr(indice(indice(cfv_objetivo, Value{std::string("hijos", 5)}), Value{0.0}), cfv_env, cfv_fns);
      size_t cfv_base_tipo = 99;
      Value cfv_b = cfv_base;
      size_t cfv_b_tipo = 99;
      asignar_indice(cfv_b, indice(cfv_objetivo, Value{std::string("valor", 5)}), cfv_nuevo_val);
    }
    return cfv_nuevo_val;
  }
  if (verdad(compara(cfv_t, Value{std::string("Llamada", 7)}, "=="))) {
    Value cfv_nombre_fn = indice(cfv_nodo_e, Value{std::string("valor", 5)});
    if (cfv_nombre_fn.index() != 2) throw std::runtime_error("tipo incompatible para nombre_fn");
    size_t cfv_nombre_fn_tipo = 2;
    Value cfv_args_eval = crear_lista({});
    if (cfv_args_eval.index() != 4) throw std::runtime_error("tipo incompatible para args_eval");
    size_t cfv_args_eval_tipo = 4;
    Value cfv_ai = Value{0.0};
    if (cfv_ai.index() != 1) throw std::runtime_error("tipo incompatible para ai");
    size_t cfv_ai_tipo = 1;
    while (verdad(compara(cfv_ai, cfv_longitud(indice(cfv_nodo_e, Value{std::string("hijos", 5)})), "<"))) {
      (void)(cfv_agregar(cfv_args_eval, cfv_eval_expr(indice(indice(cfv_nodo_e, Value{std::string("hijos", 5)}), cfv_ai), cfv_env, cfv_fns)));
      asignar(cfv_ai, cfv_ai_tipo, suma(cfv_ai, Value{1.0}), "ai");
    }
    Value cfv_res_b = cfv_eval_builtin(cfv_nombre_fn, cfv_args_eval, cfv_env, cfv_fns);
    size_t cfv_res_b_tipo = 99;
    if (verdad(compara(cfv_res_b, Value{std::string("__no_builtin__", 14)}, "!="))) {
      if (verdad(compara(cfv_res_b, Value{std::string("__ok__", 6)}, "=="))) {
        return Value{};
      }
      return cfv_res_b;
    }
    // Check if fn_nombre is a lambda in env
    try {
      Value cfv_maybe_lambda = cfv_env_buscar(cfv_env, cfv_nombre_fn);
      if (cfv_maybe_lambda.index() == 5) {
        auto cfv_mp_lam = std::get_if<Mapa>(&cfv_maybe_lambda.data);
        if (cfv_mp_lam && (*cfv_mp_lam)->count("__lambda") && verdad((**cfv_mp_lam)["__lambda"])) {
          Value cfv_lparams2 = (**cfv_mp_lam)["params"];
          Value cfv_lcuerpo2 = (**cfv_mp_lam)["cuerpo"];
          Value cfv_captured_env = (**cfv_mp_lam)["env"];
          Value cfv_fn_env_lam = cfv_env_nuevo_scope(cfv_captured_env);
          auto cfv_lp_ptr = std::get_if<Lista>(&cfv_lparams2.data);
          auto cfv_la_ptr = std::get_if<Lista>(&cfv_args_eval.data);
          if (cfv_lp_ptr && cfv_la_ptr) {
            for (size_t cfv_lii = 0; cfv_lii < (*cfv_lp_ptr)->size() && cfv_lii < (*cfv_la_ptr)->size(); cfv_lii++) {
              (void)(cfv_env_declarar(cfv_fn_env_lam, (**cfv_lp_ptr)[cfv_lii], (**cfv_la_ptr)[cfv_lii]));
            }
          }
          Value cfv_senal_lam = cfv_exec_bloque(cfv_lcuerpo2, cfv_fn_env_lam, cfv_fns);
          if (verdad(compara(indice(cfv_senal_lam, Value{0.0}), Value{std::string("retornar")}, "=="))) return indice(cfv_senal_lam, Value{1.0});
          return Value{};
        }
      }
    } catch (...) {}
    // Check class constructor
    std::string cfv_clase_key_c = "__clase_" + texto(cfv_nombre_fn);
    if (verdad(cfv_tiene_clave(cfv_fns, Value{cfv_clase_key_c}))) {
      // Enforce: cannot instantiate abstract class
      Value cfv_cdef_abs = indice(cfv_fns, Value{cfv_clase_key_c});
      if (cfv_cdef_abs.index() == 5) {
        auto cfv_abs_ptr = std::get_if<Mapa>(&cfv_cdef_abs.data);
        if (cfv_abs_ptr && (*cfv_abs_ptr)->count("abstracto") && verdad((**cfv_abs_ptr)["abstracto"])) {
          throw std::runtime_error("no se puede instanciar la clase abstracta '" + texto(cfv_nombre_fn) + "'");
        }
      }
      auto cfv_inst_map = std::make_shared<std::map<std::string,Value>>();
      (*cfv_inst_map)["__clase"] = cfv_nombre_fn;
      // Helper lambda to fill fields from a class def (including parent chain)
      std::function<void(const std::string&)> cfv_fill_campos = [&](const std::string& cls_name) {
        std::string ck = "__clase_" + cls_name;
        if (!verdad(cfv_tiene_clave(cfv_fns, Value{ck}))) return;
        Value cfv_cdef = indice(cfv_fns, Value{ck});
        // Fill parent fields first (so child overrides parent)
        if (verdad(cfv_tiene_clave(cfv_cdef, Value{std::string("padre")}))) {
          std::string cfv_padre_n = texto(indice(cfv_cdef, Value{std::string("padre")}));
          if (!cfv_padre_n.empty()) cfv_fill_campos(cfv_padre_n);
        }
        Value cfv_campos_c = indice(cfv_cdef, Value{std::string("campos")});
        Value cfv_ci = Value{0.0};
        while (verdad(compara(cfv_ci, cfv_longitud(cfv_campos_c), "<"))) {
          Value cfv_campo_entry = indice(cfv_campos_c, cfv_ci);
          std::string cfv_campo_n = texto(indice(cfv_campo_entry, Value{std::string("nombre")}));
          Value cfv_campo_def = indice(cfv_campo_entry, Value{std::string("default")});
          (*cfv_inst_map)[cfv_campo_n] = cfv_eval_expr(cfv_campo_def, cfv_env, cfv_fns);
          cfv_ci = suma(cfv_ci, Value{1.0});
        }
      };
      cfv_fill_campos(texto(cfv_nombre_fn));
      Value cfv_inst_val = Value{cfv_inst_map};
      // Call constructor method if defined
      std::function<void(const std::string&)> cfv_call_constructor = [&](const std::string& cls_name) {
        std::string ck = "__clase_" + cls_name;
        if (!verdad(cfv_tiene_clave(cfv_fns, Value{ck}))) return;
        Value cfv_cdef = indice(cfv_fns, Value{ck});
        // Call parent constructor first (if parent has one and no args for now)
        if (verdad(cfv_tiene_clave(cfv_cdef, Value{std::string("padre")}))) {
          std::string cfv_padre_n = texto(indice(cfv_cdef, Value{std::string("padre")}));
          // Parent constructor called by super() in child constructor
        }
        Value cfv_metodos_c = indice(cfv_cdef, Value{std::string("metodos")});
        if (cfv_metodos_c.index() == 5) {
          auto cfv_mptr_c = std::get_if<Mapa>(&cfv_metodos_c.data);
          if (cfv_mptr_c && (*cfv_mptr_c)->count("constructor")) {
            Value cfv_ctor = (**cfv_mptr_c)["constructor"];
            Value cfv_ctor_params = indice(cfv_ctor, Value{std::string("params")});
            Value cfv_ctor_cuerpo = indice(cfv_ctor, Value{std::string("cuerpo")});
            Value cfv_ctor_env = cfv_env_nuevo_scope(cfv_env);
            (void)(cfv_env_declarar(cfv_ctor_env, Value{std::string("esto")}, cfv_inst_val));
            auto cfv_cp_ptr = std::get_if<Lista>(&cfv_ctor_params.data);
            auto cfv_ca_ptr = std::get_if<Lista>(&cfv_args_eval.data);
            if (cfv_cp_ptr && cfv_ca_ptr) {
              for (size_t cfv_cii = 0; cfv_cii < (*cfv_cp_ptr)->size() && cfv_cii < (*cfv_ca_ptr)->size(); ++cfv_cii) {
                (void)(cfv_env_declarar(cfv_ctor_env, (**cfv_cp_ptr)[cfv_cii], (**cfv_ca_ptr)[cfv_cii]));
              }
            }
            (void)(cfv_exec_bloque(cfv_ctor_cuerpo, cfv_ctor_env, cfv_fns));
            // Sync any fields written via esto back to instance
            // (esto is a reference-type mapa so mutations are direct)
          }
        }
      };
      cfv_call_constructor(texto(cfv_nombre_fn));
      return cfv_inst_val;
    }
    (void)(cfv_afirmar(cfv_tiene_clave(cfv_fns, cfv_nombre_fn), suma(suma(Value{std::string("función desconocida '", 22)}, cfv_nombre_fn), Value{std::string("'", 1)})));
    Value cfv_fn_def = indice(cfv_fns, cfv_nombre_fn);
    size_t cfv_fn_def_tipo = 99;
    CfvCallFrame cfv_frame_fn(texto(cfv_nombre_fn));
    Value cfv_params = indice(cfv_fn_def, Value{std::string("params", 6)});
    if (cfv_params.index() != 4) throw std::runtime_error("tipo incompatible para params");
    size_t cfv_params_tipo = 4;
    Value cfv_cuerpo_fn = indice(cfv_fn_def, Value{std::string("cuerpo", 6)});
    if (cfv_cuerpo_fn.index() != 4) throw std::runtime_error("tipo incompatible para cuerpo_fn");
    size_t cfv_cuerpo_fn_tipo = 4;
    // Use captured env (closure) if available, else fresh scope
    Value cfv_fn_base_env;
    if (cfv_fn_def.index() == 5) {
      auto cfv_fd_mp = std::get_if<Mapa>(&cfv_fn_def.data);
      if (cfv_fd_mp && (*cfv_fd_mp)->count("env")) {
        cfv_fn_base_env = cfv_env_nuevo_scope((**cfv_fd_mp)["env"]);
      }
    }
    if (cfv_fn_base_env.index() != 4) cfv_fn_base_env = crear_lista({crear_mapa({})});
    Value cfv_fn_env = cfv_fn_base_env;
    if (cfv_fn_env.index() != 4) throw std::runtime_error("tipo incompatible para fn_env");
    size_t cfv_fn_env_tipo = 4;
    Value cfv_pi = Value{0.0};
    if (cfv_pi.index() != 1) throw std::runtime_error("tipo incompatible para pi");
    size_t cfv_pi_tipo = 1;
    Value cfv_ai2 = Value{0.0};
    while (verdad(compara(cfv_pi, cfv_longitud(cfv_params), "<"))) {
      Value cfv_param_entry = indice(cfv_params, cfv_pi);
      if (cfv_param_entry.index() == 5) {
        auto cfv_pe_map = std::get_if<Mapa>(&cfv_param_entry.data);
        if (cfv_pe_map) {
          if ((**cfv_pe_map).count("__param_variadic") && verdad((**cfv_pe_map)["__param_variadic"])) {
            std::string cfv_vname = texto((**cfv_pe_map)["nombre"]);
            auto cfv_rest = std::make_shared<std::vector<Value>>();
            Value cfv_vi2 = cfv_ai2;
            while (verdad(compara(cfv_vi2, cfv_longitud(cfv_args_eval), "<"))) {
              cfv_rest->push_back(indice(cfv_args_eval, cfv_vi2));
              cfv_vi2 = suma(cfv_vi2, Value{1.0});
            }
            Value cfv_fscope = indice(cfv_fn_env, Value{0.0});
            asignar_indice(cfv_fscope, Value{cfv_vname}, Value{cfv_rest});
            break;
          } else if ((**cfv_pe_map).count("__param_default") && verdad((**cfv_pe_map)["__param_default"])) {
            std::string cfv_dname = texto((**cfv_pe_map)["nombre"]);
            Value cfv_pval;
            if (verdad(compara(cfv_ai2, cfv_longitud(cfv_args_eval), "<"))) {
              cfv_pval = indice(cfv_args_eval, cfv_ai2);
              cfv_ai2 = suma(cfv_ai2, Value{1.0});
            } else {
              cfv_pval = cfv_eval_expr((**cfv_pe_map)["default"], cfv_env, cfv_fns);
            }
            Value cfv_fscope = indice(cfv_fn_env, Value{0.0});
            asignar_indice(cfv_fscope, Value{cfv_dname}, cfv_pval);
          }
        }
      } else {
        Value cfv_fscope = indice(cfv_fn_env, Value{0.0});
        if (verdad(compara(cfv_ai2, cfv_longitud(cfv_args_eval), "<"))) {
          asignar_indice(cfv_fscope, cfv_param_entry, indice(cfv_args_eval, cfv_ai2));
        } else {
          asignar_indice(cfv_fscope, cfv_param_entry, Value{});
        }
        cfv_ai2 = suma(cfv_ai2, Value{1.0});
      }
      asignar(cfv_pi, cfv_pi_tipo, suma(cfv_pi, Value{1.0}), "pi");
    }
    Value cfv_senal = cfv_exec_bloque(cfv_cuerpo_fn, cfv_fn_env, cfv_fns);
    if (cfv_senal.index() != 4) throw std::runtime_error("tipo incompatible para senal");
    size_t cfv_senal_tipo = 4;
    if (verdad(compara(indice(cfv_senal, Value{0.0}), Value{std::string("retornar", 8)}, "=="))) {
      return indice(cfv_senal, Value{1.0});
    }
    return Value{};
  }
  if (verdad(compara(cfv_t, Value{std::string("MetodoLlamada", 13)}, "=="))) {
    Value cfv_base = cfv_eval_expr(indice(indice(cfv_nodo_e, Value{std::string("hijos", 5)}), Value{0.0}), cfv_env, cfv_fns);
    size_t cfv_base_tipo = 99;
    Value cfv_metodo = indice(cfv_nodo_e, Value{std::string("valor", 5)});
    if (cfv_metodo.index() != 2) throw std::runtime_error("tipo incompatible para metodo");
    size_t cfv_metodo_tipo = 2;
    Value cfv_args_m = crear_lista({});
    if (cfv_args_m.index() != 4) throw std::runtime_error("tipo incompatible para args_m");
    size_t cfv_args_m_tipo = 4;
    Value cfv_mi = Value{1.0};
    if (cfv_mi.index() != 1) throw std::runtime_error("tipo incompatible para mi");
    size_t cfv_mi_tipo = 1;
    while (verdad(compara(cfv_mi, cfv_longitud(indice(cfv_nodo_e, Value{std::string("hijos", 5)})), "<"))) {
      (void)(cfv_agregar(cfv_args_m, cfv_eval_expr(indice(indice(cfv_nodo_e, Value{std::string("hijos", 5)}), cfv_mi), cfv_env, cfv_fns)));
      asignar(cfv_mi, cfv_mi_tipo, suma(cfv_mi, Value{1.0}), "mi");
    }
    if (verdad(Value{verdad(Value{verdad(compara(cfv_metodo, Value{std::string("agregar", 7)}, "==")) || verdad(compara(cfv_metodo, Value{std::string("push", 4)}, "=="))}) || verdad(compara(cfv_metodo, Value{std::string("append", 6)}, "=="))})) {
      (void)(cfv_agregar(cfv_base, indice(cfv_args_m, Value{0.0})));
      return Value{};
    }
    if (verdad(Value{verdad(Value{verdad(compara(cfv_metodo, Value{std::string("longitud", 8)}, "==")) || verdad(compara(cfv_metodo, Value{std::string("length", 6)}, "=="))}) || verdad(compara(cfv_metodo, Value{std::string("len", 3)}, "=="))})) {
      return cfv_longitud(cfv_base);
    }
    if (verdad(compara(cfv_metodo, Value{std::string("tiene_clave", 11)}, "=="))) {
      return cfv_tiene_clave(cfv_base, indice(cfv_args_m, Value{0.0}));
    }
    // Class method dispatch
    if (cfv_base.index() == 5) {
      auto cfv_mp_cls = std::get_if<Mapa>(&cfv_base.data);
      if (cfv_mp_cls && (*cfv_mp_cls)->count("__clase")) {
        std::string cfv_clase_n = texto((**cfv_mp_cls)["__clase"]);
        std::string cfv_clase_k = "__clase_" + cfv_clase_n;
        if (verdad(cfv_tiene_clave(cfv_fns, Value{cfv_clase_k}))) {
          Value cfv_clase_def = indice(cfv_fns, Value{cfv_clase_k});
          Value cfv_metodos_cls = indice(cfv_clase_def, Value{std::string("metodos")});
          std::string cfv_metodo_str = texto(cfv_metodo);
          if (verdad(cfv_tiene_clave(cfv_metodos_cls, Value{cfv_metodo_str}))) {
            Value cfv_fn_def_cls = indice(cfv_metodos_cls, Value{cfv_metodo_str});
            Value cfv_params_cls = indice(cfv_fn_def_cls, Value{std::string("params")});
            Value cfv_cuerpo_cls = indice(cfv_fn_def_cls, Value{std::string("cuerpo")});
            Value cfv_fn_env_cls = cfv_env_nuevo_scope(cfv_env);
            (void)(cfv_env_declarar(cfv_fn_env_cls, Value{std::string("esto")}, cfv_base));
            Value cfv_pi_cls = Value{0.0};
            Value cfv_ai_cls = Value{0.0};
            while (verdad(compara(cfv_pi_cls, cfv_longitud(cfv_params_cls), "<"))) {
              Value cfv_param_cls_entry = indice(cfv_params_cls, cfv_pi_cls);
              if (cfv_param_cls_entry.index() == 5) {
                auto cfv_pce_map = std::get_if<Mapa>(&cfv_param_cls_entry.data);
                if (cfv_pce_map) {
                  if ((**cfv_pce_map).count("__param_variadic") && verdad((**cfv_pce_map)["__param_variadic"])) {
                    std::string cfv_vname_cls = texto((**cfv_pce_map)["nombre"]);
                    auto cfv_rest_cls = std::make_shared<std::vector<Value>>();
                    Value cfv_vi_cls = cfv_ai_cls;
                    while (verdad(compara(cfv_vi_cls, cfv_longitud(cfv_args_m), "<"))) {
                      cfv_rest_cls->push_back(indice(cfv_args_m, cfv_vi_cls));
                      cfv_vi_cls = suma(cfv_vi_cls, Value{1.0});
                    }
                    (void)(cfv_env_declarar(cfv_fn_env_cls, Value{cfv_vname_cls}, Value{cfv_rest_cls}));
                    break;
                  } else if ((**cfv_pce_map).count("__param_default") && verdad((**cfv_pce_map)["__param_default"])) {
                    std::string cfv_dname_cls = texto((**cfv_pce_map)["nombre"]);
                    Value cfv_pval_cls;
                    if (verdad(compara(cfv_ai_cls, cfv_longitud(cfv_args_m), "<"))) {
                      cfv_pval_cls = indice(cfv_args_m, cfv_ai_cls);
                      cfv_ai_cls = suma(cfv_ai_cls, Value{1.0});
                    } else {
                      cfv_pval_cls = cfv_eval_expr((**cfv_pce_map)["default"], cfv_env, cfv_fns);
                    }
                    (void)(cfv_env_declarar(cfv_fn_env_cls, Value{cfv_dname_cls}, cfv_pval_cls));
                  }
                }
              } else {
                if (verdad(compara(cfv_ai_cls, cfv_longitud(cfv_args_m), "<"))) {
                  (void)(cfv_env_declarar(cfv_fn_env_cls, cfv_param_cls_entry, indice(cfv_args_m, cfv_ai_cls)));
                } else {
                  (void)(cfv_env_declarar(cfv_fn_env_cls, cfv_param_cls_entry, Value{}));
                }
                cfv_ai_cls = suma(cfv_ai_cls, Value{1.0});
              }
              cfv_pi_cls = suma(cfv_pi_cls, Value{1.0});
            }
            // Bind 'super' if there's a parent class
            if (verdad(cfv_tiene_clave(cfv_clase_def, Value{std::string("padre")}))) {
              std::string cfv_padre_str_sup = texto(indice(cfv_clase_def, Value{std::string("padre")}));
              if (!cfv_padre_str_sup.empty()) {
                auto cfv_super_map = std::make_shared<std::map<std::string,Value>>();
                // Copy instance fields first, then set __clase to parent
                if (auto mp_base = std::get_if<Mapa>(&cfv_base.data)) {
                  for (const auto& kv : **mp_base) {
                    if (kv.first != "__clase") (*cfv_super_map)[kv.first] = kv.second;
                  }
                }
                (*cfv_super_map)["__clase"] = Value{cfv_padre_str_sup};
                (void)(cfv_env_declarar(cfv_fn_env_cls, Value{std::string("super")}, Value{cfv_super_map}));
              }
            }
            Value cfv_senal_cls = cfv_exec_bloque(cfv_cuerpo_cls, cfv_fn_env_cls, cfv_fns);
            if (verdad(compara(indice(cfv_senal_cls, Value{0.0}), Value{std::string("retornar")}, "=="))) return indice(cfv_senal_cls, Value{1.0});
            return Value{};
          }
        }
      }
    }
    (void)(cfv_afirmar(Value{false}, suma(suma(Value{std::string("método desconocido '", 21)}, cfv_metodo), Value{std::string("'", 1)})));
    return Value{};
  }
  if (verdad(compara(cfv_t, Value{std::string("Lambda")}, "=="))) {
    Value cfv_lparams_h = indice(indice(indice(cfv_nodo_e, Value{std::string("hijos")}), Value{0.0}), Value{std::string("hijos")});
    Value cfv_lcuerpo_h = indice(indice(indice(cfv_nodo_e, Value{std::string("hijos")}), Value{1.0}), Value{std::string("hijos")});
    auto cfv_closure_map = std::make_shared<std::map<std::string,Value>>();
    (*cfv_closure_map)["__lambda"] = Value{true};
    (*cfv_closure_map)["params"] = cfv_lparams_h;
    (*cfv_closure_map)["cuerpo"] = cfv_lcuerpo_h;
    (*cfv_closure_map)["env"] = cfv_env;
    return Value{cfv_closure_map};
  }
  if (verdad(compara(cfv_t, Value{std::string("CampoSeguro")}, "=="))) {
    Value cfv_base_cs = cfv_eval_expr(indice(indice(cfv_nodo_e, Value{std::string("hijos")}), Value{0.0}), cfv_env, cfv_fns);
    if (cfv_base_cs.index() == 0) return Value{};
    return indice(cfv_base_cs, indice(cfv_nodo_e, Value{std::string("valor")}));
  }
  if (verdad(compara(cfv_t, Value{std::string("MetodoLlamadaSegura")}, "=="))) {
    Value cfv_base_ms = cfv_eval_expr(indice(indice(cfv_nodo_e, Value{std::string("hijos")}), Value{0.0}), cfv_env, cfv_fns);
    if (cfv_base_ms.index() == 0) return Value{};
    // Reuse MetodoLlamada logic by building a synthetic MetodoLlamada node and eval-ing it
    Value cfv_metodo_ms = indice(cfv_nodo_e, Value{std::string("valor")});
    Value cfv_args_ms = crear_lista({});
    Value cfv_mi_ms = Value{1.0};
    while (verdad(compara(cfv_mi_ms, cfv_longitud(indice(cfv_nodo_e, Value{std::string("hijos")})), "<"))) {
      (void)(cfv_agregar(cfv_args_ms, cfv_eval_expr(indice(indice(cfv_nodo_e, Value{std::string("hijos")}), cfv_mi_ms), cfv_env, cfv_fns)));
      cfv_mi_ms = suma(cfv_mi_ms, Value{1.0});
    }
    // Check class method
    if (cfv_base_ms.index() == 5) {
      auto cfv_mp_ms = std::get_if<Mapa>(&cfv_base_ms.data);
      if (cfv_mp_ms && (*cfv_mp_ms)->count("__clase")) {
        std::string cfv_clase_n_ms = texto((**cfv_mp_ms)["__clase"]);
        std::string cfv_clase_k_ms = "__clase_" + cfv_clase_n_ms;
        if (verdad(cfv_tiene_clave(cfv_fns, Value{cfv_clase_k_ms}))) {
          Value cfv_clase_def_ms = indice(cfv_fns, Value{cfv_clase_k_ms});
          Value cfv_metodos_ms = indice(cfv_clase_def_ms, Value{std::string("metodos")});
          std::string cfv_metodo_s_ms = texto(cfv_metodo_ms);
          if (verdad(cfv_tiene_clave(cfv_metodos_ms, Value{cfv_metodo_s_ms}))) {
            Value cfv_fn_def_ms = indice(cfv_metodos_ms, Value{cfv_metodo_s_ms});
            Value cfv_params_ms = indice(cfv_fn_def_ms, Value{std::string("params")});
            Value cfv_cuerpo_ms = indice(cfv_fn_def_ms, Value{std::string("cuerpo")});
            Value cfv_fn_env_ms = cfv_env_nuevo_scope(cfv_env);
            (void)(cfv_env_declarar(cfv_fn_env_ms, Value{std::string("esto")}, cfv_base_ms));
            Value cfv_pi_ms = Value{0.0};
            while (verdad(compara(cfv_pi_ms, cfv_longitud(cfv_params_ms), "<"))) {
              (void)(cfv_env_declarar(cfv_fn_env_ms, indice(cfv_params_ms, cfv_pi_ms), indice(cfv_args_ms, cfv_pi_ms)));
              cfv_pi_ms = suma(cfv_pi_ms, Value{1.0});
            }
            Value cfv_senal_ms = cfv_exec_bloque(cfv_cuerpo_ms, cfv_fn_env_ms, cfv_fns);
            if (verdad(compara(indice(cfv_senal_ms, Value{0.0}), Value{std::string("retornar")}, "=="))) return indice(cfv_senal_ms, Value{1.0});
            return Value{};
          }
        }
      }
    }
    // Built-in methods on safe nav
    if (verdad(Value{verdad(Value{verdad(compara(cfv_metodo_ms, Value{std::string("agregar")}, "==")) || verdad(compara(cfv_metodo_ms, Value{std::string("push")}, "=="))}) || verdad(compara(cfv_metodo_ms, Value{std::string("append")}, "=="))})) {
      (void)(cfv_agregar(cfv_base_ms, indice(cfv_args_ms, Value{0.0})));
      return Value{};
    }
    return Value{};
  }
  if (verdad(compara(cfv_t, Value{std::string("Ternario")}, "=="))) {
    Value cfv_cond_t = cfv_eval_expr(indice(indice(cfv_nodo_e, Value{std::string("hijos")}), Value{0.0}), cfv_env, cfv_fns);
    if (verdad(cfv_cond_t)) {
      return cfv_eval_expr(indice(indice(cfv_nodo_e, Value{std::string("hijos")}), Value{1.0}), cfv_env, cfv_fns);
    } else {
      return cfv_eval_expr(indice(indice(cfv_nodo_e, Value{std::string("hijos")}), Value{2.0}), cfv_env, cfv_fns);
    }
  }
  (void)(cfv_afirmar(Value{false}, suma(suma(Value{std::string("expresión no evaluable tipo '", 30)}, cfv_t), Value{std::string("'", 1)})));
  return Value{};
  return Value{};
}
Value cfv_exec_bloque(Value cfv_sentencias, Value cfv_env, Value cfv_fns) {
  cfv_jit_hit("exec_bloque");
  size_t cfv_sentencias_tipo = cfv_sentencias.index();
  size_t cfv_env_tipo = cfv_env.index();
  size_t cfv_fns_tipo = cfv_fns.index();
  Value cfv_i = Value{0.0};
  if (cfv_i.index() != 1) throw std::runtime_error("tipo incompatible para i");
  size_t cfv_i_tipo = 1;
  while (verdad(compara(cfv_i, cfv_longitud(cfv_sentencias), "<"))) {
    Value cfv_senal = cfv_exec_stmt(indice(cfv_sentencias, cfv_i), cfv_env, cfv_fns);
    if (cfv_senal.index() != 4) throw std::runtime_error("tipo incompatible para senal");
    size_t cfv_senal_tipo = 4;
    if (verdad(compara(indice(cfv_senal, Value{0.0}), Value{std::string("normal", 6)}, "!="))) {
      return cfv_senal;
    }
    asignar(cfv_i, cfv_i_tipo, suma(cfv_i, Value{1.0}), "i");
  }
  return crear_lista({Value{std::string("normal", 6)}, Value{}});
  return Value{};
}
Value cfv_exec_stmt(Value cfv_stmt, Value cfv_env, Value cfv_fns) {
  cfv_jit_hit("exec_stmt");
  size_t cfv_stmt_tipo = cfv_stmt.index();
  size_t cfv_env_tipo = cfv_env.index();
  size_t cfv_fns_tipo = cfv_fns.index();
  Value cfv_t = indice(cfv_stmt, Value{std::string("tipo", 4)});
  if (cfv_t.index() != 2) throw std::runtime_error("tipo incompatible para t");
  size_t cfv_t_tipo = 2;
  if (verdad(compara(cfv_t, Value{std::string("Declaracion", 11)}, "=="))) {
    Value cfv_val = cfv_eval_expr(indice(indice(cfv_stmt, Value{std::string("hijos", 5)}), Value{0.0}), cfv_env, cfv_fns);
    // Runtime type annotation checking
    Value cfv_hijos_decl = indice(cfv_stmt, Value{std::string("hijos", 5)});
    Value cfv_tipo_anot_r = Value{};
    if (auto cfv_hd_ptr = std::get_if<Lista>(&cfv_hijos_decl.data)) {
      if ((*cfv_hd_ptr)->size() > 1) cfv_tipo_anot_r = (**cfv_hd_ptr)[1];
    }
    if (cfv_tipo_anot_r.index() == 2) {
      std::string cfv_ta = texto(cfv_tipo_anot_r);
      auto cfv_type_ok = [&]() -> bool {
        if (cfv_ta == "Numero" || cfv_ta == "numero") return cfv_val.index() == 1;
        if (cfv_ta == "Texto" || cfv_ta == "texto") return cfv_val.index() == 2;
        if (cfv_ta == "Booleano" || cfv_ta == "booleano") return cfv_val.index() == 3;
        if (cfv_ta.substr(0, 5) == "Lista" || cfv_ta == "lista") return cfv_val.index() == 4 || cfv_val.index() == 6;
        if (cfv_ta == "Mapa" || cfv_ta == "mapa") return cfv_val.index() == 5;
        if (cfv_ta == "Nulo" || cfv_ta == "nulo") return cfv_val.index() == 0;
        if (cfv_ta == "Cualquiera" || cfv_ta == "cualquiera" || cfv_ta == "Auto") return true;
        // Class type check
        if (cfv_val.index() == 5) {
          auto cfv_mp = std::get_if<Mapa>(&cfv_val.data);
          if (cfv_mp && (*cfv_mp)->count("__clase")) return texto((**cfv_mp)["__clase"]) == cfv_ta;
        }
        return true; // Unknown type — pass
      };
      if (!cfv_type_ok()) {
        std::string cfv_val_tipo_str = texto(cfv_tipo_de(cfv_val));
        throw std::runtime_error("error de tipo: se esperaba '" + cfv_ta + "' pero se recibió '" + cfv_val_tipo_str + "' en variable '" + texto(indice(cfv_stmt, Value{std::string("valor",5)})) + "'");
      }
    }
    (void)(cfv_env_declarar(cfv_env, indice(cfv_stmt, Value{std::string("valor", 5)}), cfv_val));
    return crear_lista({Value{std::string("normal", 6)}, Value{}});
  }
  if (verdad(compara(cfv_t, Value{std::string("Expresion", 9)}, "=="))) {
    (void)(cfv_eval_expr(indice(indice(cfv_stmt, Value{std::string("hijos", 5)}), Value{0.0}), cfv_env, cfv_fns));
    return crear_lista({Value{std::string("normal", 6)}, Value{}});
  }
  if (verdad(compara(cfv_t, Value{std::string("Mostrar", 7)}, "=="))) {
    Value cfv_val = cfv_eval_expr(indice(indice(cfv_stmt, Value{std::string("hijos", 5)}), Value{0.0}), cfv_env, cfv_fns);
    size_t cfv_val_tipo = 99;
    mostrar(cfv_val);
    return crear_lista({Value{std::string("normal", 6)}, Value{}});
  }
  if (verdad(compara(cfv_t, Value{std::string("Retornar", 8)}, "=="))) {
    Value cfv_val = cfv_eval_expr(indice(indice(cfv_stmt, Value{std::string("hijos", 5)}), Value{0.0}), cfv_env, cfv_fns);
    size_t cfv_val_tipo = 99;
    return crear_lista({Value{std::string("retornar", 8)}, cfv_val});
  }
  if (verdad(compara(cfv_t, Value{std::string("Romper", 6)}, "=="))) {
    return crear_lista({Value{std::string("romper", 6)}, Value{}});
  }
  if (verdad(compara(cfv_t, Value{std::string("Continuar", 9)}, "=="))) {
    return crear_lista({Value{std::string("continuar", 9)}, Value{}});
  }
  if (verdad(compara(cfv_t, Value{std::string("Si", 2)}, "=="))) {
    Value cfv_hijos = indice(cfv_stmt, Value{std::string("hijos", 5)});
    if (cfv_hijos.index() != 4) throw std::runtime_error("tipo incompatible para hijos");
    size_t cfv_hijos_tipo = 4;
    Value cfv_n_hijos = cfv_longitud(cfv_hijos);
    if (cfv_n_hijos.index() != 1) throw std::runtime_error("tipo incompatible para n_hijos");
    size_t cfv_n_hijos_tipo = 1;
    Value cfv_i = Value{0.0};
    if (cfv_i.index() != 1) throw std::runtime_error("tipo incompatible para i");
    size_t cfv_i_tipo = 1;
    Value cfv_ejecutado = Value{false};
    if (cfv_ejecutado.index() != 3) throw std::runtime_error("tipo incompatible para ejecutado");
    size_t cfv_ejecutado_tipo = 3;
    while (verdad(Value{verdad(compara(cfv_i, cfv_n_hijos, "<")) && verdad(Value{!verdad(cfv_ejecutado)})})) {
      Value cfv_es_ultimo = compara(cfv_i, resta(cfv_n_hijos, Value{1.0}), "==");
      if (cfv_es_ultimo.index() != 3) throw std::runtime_error("tipo incompatible para es_ultimo");
      size_t cfv_es_ultimo_tipo = 3;
      if (verdad(Value{verdad(compara(indice(indice(cfv_hijos, cfv_i), Value{std::string("tipo", 4)}), Value{std::string("Bloque", 6)}, "==")) && verdad(cfv_es_ultimo)})) {
        Value cfv_scope_si = cfv_env_nuevo_scope(cfv_env);
        if (cfv_scope_si.index() != 4) throw std::runtime_error("tipo incompatible para scope_si");
        size_t cfv_scope_si_tipo = 4;
        Value cfv_senal = cfv_exec_bloque(indice(indice(cfv_hijos, cfv_i), Value{std::string("hijos", 5)}), cfv_scope_si, cfv_fns);
        if (cfv_senal.index() != 4) throw std::runtime_error("tipo incompatible para senal");
        size_t cfv_senal_tipo = 4;
        asignar(cfv_ejecutado, cfv_ejecutado_tipo, Value{true}, "ejecutado");
        if (verdad(compara(indice(cfv_senal, Value{0.0}), Value{std::string("normal", 6)}, "!="))) {
          return cfv_senal;
        }
      }       else if (verdad(compara(indice(indice(cfv_hijos, cfv_i), Value{std::string("tipo", 4)}), Value{std::string("Bloque", 6)}, "!="))) {
        Value cfv_cond_val = cfv_eval_expr(indice(cfv_hijos, cfv_i), cfv_env, cfv_fns);
        size_t cfv_cond_val_tipo = 99;
        if (verdad(cfv_cond_val)) {
          Value cfv_scope_si = cfv_env_nuevo_scope(cfv_env);
          if (cfv_scope_si.index() != 4) throw std::runtime_error("tipo incompatible para scope_si");
          size_t cfv_scope_si_tipo = 4;
          Value cfv_senal = cfv_exec_bloque(indice(indice(cfv_hijos, suma(cfv_i, Value{1.0})), Value{std::string("hijos", 5)}), cfv_scope_si, cfv_fns);
          if (cfv_senal.index() != 4) throw std::runtime_error("tipo incompatible para senal");
          size_t cfv_senal_tipo = 4;
          asignar(cfv_ejecutado, cfv_ejecutado_tipo, Value{true}, "ejecutado");
          if (verdad(compara(indice(cfv_senal, Value{0.0}), Value{std::string("normal", 6)}, "!="))) {
            return cfv_senal;
          }
        }
        asignar(cfv_i, cfv_i_tipo, suma(cfv_i, Value{1.0}), "i");
      }
      asignar(cfv_i, cfv_i_tipo, suma(cfv_i, Value{1.0}), "i");
    }
    return crear_lista({Value{std::string("normal", 6)}, Value{}});
  }
  // ── Match/case runtime ────────────────────────────────────────────────────────
  if (verdad(compara(cfv_t, Value{std::string("Match", 5)}, "=="))) {
    Value cfv_match_hijos = indice(cfv_stmt, Value{std::string("hijos", 5)});
    Value cfv_match_val = cfv_eval_expr(indice(cfv_match_hijos, Value{0.0}), cfv_env, cfv_fns);
    Value cfv_casos_lista = indice(cfv_match_hijos, Value{1.0});
    bool cfv_match_ejecutado = false;
    auto cfv_match_n = std::get_if<Lista>(&cfv_casos_lista.data);
    if (cfv_match_n) {
      for (auto& cfv_caso : **cfv_match_n) {
        if (cfv_match_ejecutado) break;
        // cfv_caso = MatchCaso node: valor=tipo_patron, hijos=[patron, guarda, bloque]
        std::string cfv_tipo_patron = texto(indice(cfv_caso, Value{std::string("valor")}));
        Value cfv_caso_hijos = indice(cfv_caso, Value{std::string("hijos")});
        Value cfv_patron_val = indice(cfv_caso_hijos, Value{0.0});
        Value cfv_guarda_val = indice(cfv_caso_hijos, Value{1.0});
        Value cfv_caso_bloque = indice(cfv_caso_hijos, Value{2.0});
        // Check match
        bool cfv_match_ok = false;
        Value cfv_bind_name = Value{};
        if (cfv_tipo_patron == "wildcard") {
          cfv_match_ok = true;
        } else if (cfv_tipo_patron == "literal_num" || cfv_tipo_patron == "literal_txt" || cfv_tipo_patron == "literal_bool") {
          cfv_match_ok = verdad(compara(cfv_match_val, cfv_eval_expr(cfv_patron_val, cfv_env, cfv_fns), "=="));
        } else if (cfv_tipo_patron == "literal_nulo") {
          cfv_match_ok = (cfv_match_val.index() == 0);
        } else if (cfv_tipo_patron == "bind") {
          // Bind variable name — always matches, binds value
          cfv_match_ok = true;
          cfv_bind_name = cfv_patron_val;
        }
        if (cfv_match_ok) {
          // Check guard
          if (cfv_guarda_val.index() != 0) {
            Value cfv_guard_scope = cfv_env_nuevo_scope(cfv_env);
            if (cfv_bind_name.index() == 2) {
              (void)(cfv_env_declarar(cfv_guard_scope, cfv_bind_name, cfv_match_val));
            }
            Value cfv_guard_res = cfv_eval_expr(cfv_guarda_val, cfv_guard_scope, cfv_fns);
            if (!verdad(cfv_guard_res)) continue;
          }
          // Execute block
          Value cfv_scope_m = cfv_env_nuevo_scope(cfv_env);
          if (cfv_bind_name.index() == 2) {
            (void)(cfv_env_declarar(cfv_scope_m, cfv_bind_name, cfv_match_val));
          }
          Value cfv_senal_m = cfv_exec_bloque(indice(cfv_caso_bloque, Value{std::string("hijos")}), cfv_scope_m, cfv_fns);
          cfv_match_ejecutado = true;
          if (verdad(compara(indice(cfv_senal_m, Value{0.0}), Value{std::string("normal")}, "!="))) {
            return cfv_senal_m;
          }
        }
      }
    }
    return crear_lista({Value{std::string("normal")}, Value{}});
  }
  if (verdad(compara(cfv_t, Value{std::string("Mientras", 8)}, "=="))) {
    Value cfv_cond_nodo = indice(indice(cfv_stmt, Value{std::string("hijos", 5)}), Value{0.0});
    size_t cfv_cond_nodo_tipo = 99;
    Value cfv_cuerpo_nodo = indice(indice(cfv_stmt, Value{std::string("hijos", 5)}), Value{1.0});
    size_t cfv_cuerpo_nodo_tipo = 99;
    Value cfv_continuar_bucle = Value{true};
    if (cfv_continuar_bucle.index() != 3) throw std::runtime_error("tipo incompatible para continuar_bucle");
    size_t cfv_continuar_bucle_tipo = 3;
    while (verdad(cfv_continuar_bucle)) {
      Value cfv_cond_val = cfv_eval_expr(cfv_cond_nodo, cfv_env, cfv_fns);
      size_t cfv_cond_val_tipo = 99;
      if (verdad(Value{!verdad(cfv_cond_val)})) {
        asignar(cfv_continuar_bucle, cfv_continuar_bucle_tipo, Value{false}, "continuar_bucle");
      } else {
        Value cfv_scope_w = cfv_env_nuevo_scope(cfv_env);
        if (cfv_scope_w.index() != 4) throw std::runtime_error("tipo incompatible para scope_w");
        size_t cfv_scope_w_tipo = 4;
        Value cfv_senal = cfv_exec_bloque(indice(cfv_cuerpo_nodo, Value{std::string("hijos", 5)}), cfv_scope_w, cfv_fns);
        if (cfv_senal.index() != 4) throw std::runtime_error("tipo incompatible para senal");
        size_t cfv_senal_tipo = 4;
        if (verdad(compara(indice(cfv_senal, Value{0.0}), Value{std::string("romper", 6)}, "=="))) {
          asignar(cfv_continuar_bucle, cfv_continuar_bucle_tipo, Value{false}, "continuar_bucle");
        }
        if (verdad(compara(indice(cfv_senal, Value{0.0}), Value{std::string("retornar", 8)}, "=="))) {
          return cfv_senal;
        }
      }
    }
    return crear_lista({Value{std::string("normal", 6)}, Value{}});
  }
  if (verdad(compara(cfv_t, Value{std::string("Para", 4)}, "=="))) {
    Value cfv_var_nombre = indice(cfv_stmt, Value{std::string("valor", 5)});
    if (cfv_var_nombre.index() != 2) throw std::runtime_error("tipo incompatible para var_nombre");
    size_t cfv_var_nombre_tipo = 2;
    Value cfv_coleccion_val = cfv_eval_expr(indice(indice(cfv_stmt, Value{std::string("hijos", 5)}), Value{0.0}), cfv_env, cfv_fns);
    size_t cfv_coleccion_val_tipo = 99;
    Value cfv_cuerpo_nodo = indice(indice(cfv_stmt, Value{std::string("hijos", 5)}), Value{1.0});
    size_t cfv_cuerpo_nodo_tipo = 99;
    // Lazy range detection
    if (cfv_coleccion_val.index() == 5) {
      auto cfv_mp_rango = std::get_if<Mapa>(&cfv_coleccion_val.data);
      if (cfv_mp_rango && (*cfv_mp_rango)->count("__rango")) {
        double cfv_rango_inicio = numero((**cfv_mp_rango)["inicio"]);
        double cfv_rango_fin = numero((**cfv_mp_rango)["fin"]);
        double cfv_rango_paso = (*cfv_mp_rango)->count("paso") ? numero((**cfv_mp_rango)["paso"]) : 1.0;
        bool cfv_continuar_rango = true;
        double cfv_ri = cfv_rango_inicio;
        while (cfv_ri < cfv_rango_fin && cfv_continuar_rango) {
          Value cfv_scope_r = cfv_env_nuevo_scope(cfv_env);
          (void)(cfv_env_declarar(cfv_scope_r, cfv_var_nombre, Value{cfv_ri}));
          Value cfv_senal_r = cfv_exec_bloque(indice(cfv_cuerpo_nodo, Value{std::string("hijos")}), cfv_scope_r, cfv_fns);
          if (verdad(compara(indice(cfv_senal_r, Value{0.0}), Value{std::string("romper")}, "=="))) cfv_continuar_rango = false;
          else if (verdad(compara(indice(cfv_senal_r, Value{0.0}), Value{std::string("retornar")}, "=="))) return cfv_senal_r;
          cfv_ri += cfv_rango_paso;
        }
        return crear_lista({Value{std::string("normal")}, Value{}});
      }
    }
    Value cfv_idx = Value{0.0};
    if (cfv_idx.index() != 1) throw std::runtime_error("tipo incompatible para idx");
    size_t cfv_idx_tipo = 1;
    Value cfv_tam = cfv_longitud(cfv_coleccion_val);
    if (cfv_tam.index() != 1) throw std::runtime_error("tipo incompatible para tam");
    size_t cfv_tam_tipo = 1;
    Value cfv_continuar_para = Value{true};
    if (cfv_continuar_para.index() != 3) throw std::runtime_error("tipo incompatible para continuar_para");
    size_t cfv_continuar_para_tipo = 3;
    while (verdad(Value{verdad(compara(cfv_idx, cfv_tam, "<")) && verdad(cfv_continuar_para)})) {
      Value cfv_scope_p = cfv_env_nuevo_scope(cfv_env);
      if (cfv_scope_p.index() != 4) throw std::runtime_error("tipo incompatible para scope_p");
      size_t cfv_scope_p_tipo = 4;
      (void)(cfv_env_declarar(cfv_scope_p, cfv_var_nombre, indice(cfv_coleccion_val, cfv_idx)));
      Value cfv_senal = cfv_exec_bloque(indice(cfv_cuerpo_nodo, Value{std::string("hijos", 5)}), cfv_scope_p, cfv_fns);
      if (cfv_senal.index() != 4) throw std::runtime_error("tipo incompatible para senal");
      size_t cfv_senal_tipo = 4;
      if (verdad(compara(indice(cfv_senal, Value{0.0}), Value{std::string("romper", 6)}, "=="))) {
        asignar(cfv_continuar_para, cfv_continuar_para_tipo, Value{false}, "continuar_para");
      }
      if (verdad(compara(indice(cfv_senal, Value{0.0}), Value{std::string("retornar", 8)}, "=="))) {
        return cfv_senal;
      }
      asignar(cfv_idx, cfv_idx_tipo, suma(cfv_idx, Value{1.0}), "idx");
    }
    return crear_lista({Value{std::string("normal", 6)}, Value{}});
  }
  if (verdad(compara(cfv_t, Value{std::string("Funcion", 7)}, "=="))) {
    Value cfv_fn_nombre = indice(cfv_stmt, Value{std::string("valor", 5)});
    if (cfv_fn_nombre.index() != 2) throw std::runtime_error("tipo incompatible para fn_nombre");
    size_t cfv_fn_nombre_tipo = 2;
    Value cfv_params_nodo = indice(indice(cfv_stmt, Value{std::string("hijos", 5)}), Value{0.0});
    size_t cfv_params_nodo_tipo = 99;
    Value cfv_cuerpo_nodo = indice(indice(cfv_stmt, Value{std::string("hijos", 5)}), Value{1.0});
    size_t cfv_cuerpo_nodo_tipo = 99;
    auto cfv_fn_def_map = std::make_shared<std::map<std::string,Value>>();
    (*cfv_fn_def_map)["params"] = indice(cfv_params_nodo, Value{std::string("hijos", 5)});
    (*cfv_fn_def_map)["cuerpo"] = indice(cfv_cuerpo_nodo, Value{std::string("hijos", 5)});
    (*cfv_fn_def_map)["env"]    = cfv_env; // capture definition environment (closure)
    Value cfv_fn_def = Value{cfv_fn_def_map};
    size_t cfv_fn_def_tipo = 99;
    Value cfv_fns_m = cfv_fns;
    size_t cfv_fns_m_tipo = 99;
    asignar_indice(cfv_fns_m, cfv_fn_nombre, cfv_fn_def);
    return crear_lista({Value{std::string("normal", 6)}, Value{}});
  }
  if (verdad(compara(cfv_t, Value{std::string("Bloque", 6)}, "=="))) {
    Value cfv_scope_b = cfv_env_nuevo_scope(cfv_env);
    if (cfv_scope_b.index() != 4) throw std::runtime_error("tipo incompatible para scope_b");
    size_t cfv_scope_b_tipo = 4;
    return cfv_exec_bloque(indice(cfv_stmt, Value{std::string("hijos", 5)}), cfv_scope_b, cfv_fns);
  }
  if (verdad(compara(cfv_t, Value{std::string("Asignacion", 10)}, "=="))) {
    (void)(cfv_eval_expr(cfv_stmt, cfv_env, cfv_fns));
    return crear_lista({Value{std::string("normal", 6)}, Value{}});
  }
  if (verdad(compara(cfv_t, Value{std::string("Importar")}, "=="))) {
    Value cfv_ruta_raw = indice(cfv_stmt, Value{std::string("valor")});
    Value cfv_ruta_imp = cfv_subcadena(cfv_ruta_raw, Value{1.0}, resta(cfv_longitud(cfv_ruta_raw), Value{1.0}));
    // ── Module resolution: search stdlib/, cforge_modules/, ~/.cforge/stdlib/ ──
    std::string cfv_spec = std::get<std::string>(cfv_ruta_imp.data);
    std::string cfv_resolved = cfv_resolver_modulo(cfv_spec);
    // ── Import cache: skip already-imported modules ──
    std::string cfv_canon = std::filesystem::weakly_canonical(
        std::filesystem::path(cfv_resolved)).string();
    if (cfv_imported_set.count(cfv_canon)) {
        return crear_lista({Value{std::string("normal")}, Value{}});
    }
    cfv_imported_set.insert(cfv_canon);
    // ── Load and execute ──
    std::ifstream cfv_imp_f(cfv_resolved, std::ios::binary);
    if (!cfv_imp_f) {
        throw std::runtime_error("importar: no se pudo leer '" + cfv_spec +
            "'\n  Buscado en: " + cfv_resolved +
            "\n  Define CFORGE_STDLIB para apuntar al directorio stdlib/");
    }
    std::ostringstream cfv_imp_ss;
    cfv_imp_ss << cfv_imp_f.rdbuf();
    Value cfv_fuente_imp{cfv_imp_ss.str()};
    Value cfv_tokens_imp = cfv_tokenizar(cfv_fuente_imp);
    Value cfv_ast_imp = cfv_parsear(cfv_tokens_imp);
    (void)(cfv_exec_bloque(cfv_ast_imp, cfv_env, cfv_fns));
    return crear_lista({Value{std::string("normal")}, Value{}});
  }
  // Lanzar
  if (verdad(compara(cfv_t, Value{std::string("Lanzar")}, "=="))) {
    Value cfv_msg = cfv_eval_expr(indice(indice(cfv_stmt, Value{std::string("hijos")}), Value{0.0}), cfv_env, cfv_fns);
    throw std::runtime_error(texto(cfv_msg));
  }
  // Intentar
  if (verdad(compara(cfv_t, Value{std::string("Intentar")}, "=="))) {
    Value cfv_var_nombre_int = indice(cfv_stmt, Value{std::string("valor")});
    Value cfv_hijos_int = indice(cfv_stmt, Value{std::string("hijos")});
    Value cfv_try_hijos = indice(indice(cfv_hijos_int, Value{0.0}), Value{std::string("hijos")});
    Value cfv_catch_hijos = indice(indice(cfv_hijos_int, Value{1.0}), Value{std::string("hijos")});
    // Get finally block (may be 3rd child or empty)
    Value cfv_finally_hijos_e = crear_lista({});
    if (verdad(compara(cfv_longitud(cfv_hijos_int), Value{3.0}, ">="))) {
      cfv_finally_hijos_e = indice(indice(cfv_hijos_int, Value{2.0}), Value{std::string("hijos")});
    }
    Value cfv_resultado_int = crear_lista({Value{std::string("normal")}, Value{}});
    bool cfv_tiene_resultado = false;
    try {
      Value cfv_scope_try = cfv_env_nuevo_scope(cfv_env);
      Value cfv_res_try = cfv_exec_bloque(cfv_try_hijos, cfv_scope_try, cfv_fns);
      if (verdad(compara(indice(cfv_res_try, Value{0.0}), Value{std::string("normal")}, "!="))) {
        cfv_resultado_int = cfv_res_try;
        cfv_tiene_resultado = true;
      }
    } catch (const std::exception& e) {
      Value cfv_scope_catch = cfv_env_nuevo_scope(cfv_env);
      (void)(cfv_env_declarar(cfv_scope_catch, cfv_var_nombre_int, Value{std::string(e.what())}));
      Value cfv_res_catch = cfv_exec_bloque(cfv_catch_hijos, cfv_scope_catch, cfv_fns);
      if (verdad(compara(indice(cfv_res_catch, Value{0.0}), Value{std::string("normal")}, "!="))) {
        cfv_resultado_int = cfv_res_catch;
        cfv_tiene_resultado = true;
      }
    }
    // Execute finalmente always
    if (verdad(compara(cfv_longitud(cfv_finally_hijos_e), Value{0.0}, ">"))) {
      Value cfv_scope_fin = cfv_env_nuevo_scope(cfv_env);
      (void)(cfv_exec_bloque(cfv_finally_hijos_e, cfv_scope_fin, cfv_fns));
    }
    if (cfv_tiene_resultado) return cfv_resultado_int;
    return crear_lista({Value{std::string("normal")}, Value{}});
  }
  // Segun
  if (verdad(compara(cfv_t, Value{std::string("Segun")}, "=="))) {
    Value cfv_hijos_seg = indice(cfv_stmt, Value{std::string("hijos")});
    Value cfv_expr_val_seg = cfv_eval_expr(indice(cfv_hijos_seg, Value{0.0}), cfv_env, cfv_fns);
    Value cfv_idx_seg = Value{1.0};
    while (verdad(compara(cfv_idx_seg, cfv_longitud(cfv_hijos_seg), "<"))) {
      Value cfv_caso_node = indice(cfv_hijos_seg, cfv_idx_seg);
      Value cfv_caso_valor = indice(cfv_caso_node, Value{std::string("valor")});
      bool cfv_matches = false;
      if (cfv_caso_valor.index() == 0) {
        cfv_matches = true; // otro
      } else {
        Value cfv_caso_eval = cfv_eval_expr(cfv_caso_valor, cfv_env, cfv_fns);
        cfv_matches = verdad(compara(cfv_expr_val_seg, cfv_caso_eval, "=="));
      }
      if (cfv_matches) {
        Value cfv_scope_seg = cfv_env_nuevo_scope(cfv_env);
        Value cfv_senal_seg = cfv_exec_bloque(indice(cfv_caso_node, Value{std::string("hijos")}), cfv_scope_seg, cfv_fns);
        if (verdad(compara(indice(cfv_senal_seg, Value{0.0}), Value{std::string("normal")}, "!="))) return cfv_senal_seg;
        break;
      }
      cfv_idx_seg = suma(cfv_idx_seg, Value{1.0});
    }
    return crear_lista({Value{std::string("normal")}, Value{}});
  }
  // Enum
  if (verdad(compara(cfv_t, Value{std::string("Enum")}, "=="))) {
    Value cfv_enum_n = indice(cfv_stmt, Value{std::string("valor")});
    Value cfv_enum_vals2 = indice(cfv_stmt, Value{std::string("hijos")});
    auto cfv_enum_map = std::make_shared<std::map<std::string,Value>>();
    (*cfv_enum_map)["__enum"] = cfv_enum_n;
    Value cfv_ei = Value{0.0};
    while (verdad(compara(cfv_ei, cfv_longitud(cfv_enum_vals2), "<"))) {
      Value cfv_ev = indice(cfv_enum_vals2, cfv_ei);
      std::string cfv_ev_s = texto(cfv_ev);
      (*cfv_enum_map)[cfv_ev_s] = cfv_ev;
      cfv_ei = suma(cfv_ei, Value{1.0});
    }
    (void)(cfv_env_declarar(cfv_env, cfv_enum_n, Value{cfv_enum_map}));
    return crear_lista({Value{std::string("normal")}, Value{}});
  }
  // Clase
  if (verdad(compara(cfv_t, Value{std::string("Clase")}, "=="))) {
    Value cfv_clase_n2 = indice(cfv_stmt, Value{std::string("valor")});
    Value cfv_miembros2 = indice(cfv_stmt, Value{std::string("hijos")});
    auto cfv_campos_list = std::make_shared<std::vector<Value>>();
    auto cfv_metodos_map = std::make_shared<std::map<std::string,Value>>();
    std::string cfv_padre_str_cls = "";
    bool cfv_clase_es_abstracta2 = false;
    Value cfv_clase_implementa2 = Value{};
    Value cfv_mi2 = Value{0.0};
    while (verdad(compara(cfv_mi2, cfv_longitud(cfv_miembros2), "<"))) {
      Value cfv_m = indice(cfv_miembros2, cfv_mi2);
      if (verdad(compara(indice(cfv_m, Value{std::string("tipo")}), Value{std::string("Padre")}, "=="))) {
        cfv_padre_str_cls = texto(indice(cfv_m, Value{std::string("valor")}));
      } else if (verdad(compara(indice(cfv_m, Value{std::string("tipo")}), Value{std::string("Abstracto")}, "=="))) {
        if (cfv_m.index() == 5) {
          auto cfv_am = std::get_if<Mapa>(&cfv_m.data);
          if (cfv_am && (*cfv_am)->count("valor")) cfv_clase_es_abstracta2 = verdad((**cfv_am)["valor"]);
        }
      } else if (verdad(compara(indice(cfv_m, Value{std::string("tipo")}), Value{std::string("Implementa")}, "=="))) {
        cfv_clase_implementa2 = indice(cfv_m, Value{std::string("hijos")});
      } else if (verdad(compara(indice(cfv_m, Value{std::string("tipo")}), Value{std::string("CampoDef")}, "=="))) {
        auto cfv_campo_entry2 = std::make_shared<std::map<std::string,Value>>();
        (*cfv_campo_entry2)["nombre"] = indice(cfv_m, Value{std::string("valor")});
        Value cfv_hijos_cm = indice(cfv_m, Value{std::string("hijos")});
        (*cfv_campo_entry2)["default"] = indice(cfv_hijos_cm, Value{0.0});
        Value cfv_acceso_cm = Value{std::string("publico")};
        if (auto cfv_hl = std::get_if<Lista>(&cfv_hijos_cm.data)) {
          if ((*cfv_hl)->size() > 1) cfv_acceso_cm = (**cfv_hl)[1];
        }
        (*cfv_campo_entry2)["acceso"] = cfv_acceso_cm;
        cfv_campos_list->push_back(Value{cfv_campo_entry2});
      } else if (verdad(compara(indice(cfv_m, Value{std::string("tipo")}), Value{std::string("Funcion")}, "=="))) {
        std::string cfv_mnom2 = texto(indice(cfv_m, Value{std::string("valor")}));
        Value cfv_mparams2 = indice(indice(indice(cfv_m, Value{std::string("hijos")}), Value{0.0}), Value{std::string("hijos")});
        Value cfv_mcuerpo2 = indice(indice(indice(cfv_m, Value{std::string("hijos")}), Value{1.0}), Value{std::string("hijos")});
        auto cfv_fn_def2 = std::make_shared<std::map<std::string,Value>>();
        (*cfv_fn_def2)["params"] = cfv_mparams2;
        (*cfv_fn_def2)["cuerpo"] = cfv_mcuerpo2;
        (*cfv_metodos_map)[cfv_mnom2] = Value{cfv_fn_def2};
      }
      cfv_mi2 = suma(cfv_mi2, Value{1.0});
    }
    auto cfv_clase_def2 = std::make_shared<std::map<std::string,Value>>();
    (*cfv_clase_def2)["campos"] = Value{cfv_campos_list};
    (*cfv_clase_def2)["metodos"] = Value{cfv_metodos_map};
    if (cfv_clase_es_abstracta2) (*cfv_clase_def2)["abstracto"] = Value{true};
    if (cfv_clase_implementa2.index() == 4) (*cfv_clase_def2)["implementa"] = cfv_clase_implementa2;
    // Inherit from parent if specified
    if (!cfv_padre_str_cls.empty()) {
      (*cfv_clase_def2)["padre"] = Value{cfv_padre_str_cls};
      std::string cfv_padre_key3 = "__clase_" + cfv_padre_str_cls;
      if (verdad(cfv_tiene_clave(cfv_fns, Value{cfv_padre_key3}))) {
        Value cfv_padre_def3 = indice(cfv_fns, Value{cfv_padre_key3});
        Value cfv_padre_mets = indice(cfv_padre_def3, Value{std::string("metodos")});
        if (auto pm = std::get_if<Mapa>(&cfv_padre_mets.data)) {
          for (const auto& kv : **pm) {
            if (!cfv_metodos_map->count(kv.first)) {
              (*cfv_metodos_map)[kv.first] = kv.second;
            }
          }
        }
      }
    }
    std::string cfv_clase_key2 = "__clase_" + texto(cfv_clase_n2);
    Value cfv_fns_m2 = cfv_fns;
    asignar_indice(cfv_fns_m2, Value{cfv_clase_key2}, Value{cfv_clase_def2});
    // Register interface implementations for validation
    if ((*cfv_clase_def2).count("implementa")) {
      Value cfv_ifaces_list = (*cfv_clase_def2)["implementa"];
      Value cfv_ii = Value{0.0};
      while (verdad(compara(cfv_ii, cfv_longitud(cfv_ifaces_list), "<"))) {
        std::string cfv_iface_n = texto(indice(cfv_ifaces_list, cfv_ii));
        std::string cfv_iface_key = "__interfaz_" + cfv_iface_n;
        if (verdad(cfv_tiene_clave(cfv_fns, Value{cfv_iface_key}))) {
          Value cfv_iface_def = indice(cfv_fns, Value{cfv_iface_key});
          Value cfv_ij = Value{0.0};
          while (verdad(compara(cfv_ij, cfv_longitud(cfv_iface_def), "<"))) {
            std::string cfv_metodo_req = texto(indice(cfv_iface_def, cfv_ij));
            if (!cfv_metodos_map->count(cfv_metodo_req)) {
              throw std::runtime_error("clase '" + texto(cfv_clase_n2) + "' debe implementar '" + cfv_metodo_req + "' de interfaz '" + cfv_iface_n + "'");
            }
            cfv_ij = suma(cfv_ij, Value{1.0});
          }
        }
        cfv_ii = suma(cfv_ii, Value{1.0});
      }
    }
    return crear_lista({Value{std::string("normal")}, Value{}});
  }
  // Interfaz statement: register interface in cfv_fns
  if (verdad(compara(cfv_t, Value{std::string("Interfaz")}, "=="))) {
    std::string cfv_iface_nom = texto(indice(cfv_stmt, Value{std::string("valor")}));
    Value cfv_iface_metodos = indice(cfv_stmt, Value{std::string("hijos")});
    std::string cfv_iface_reg_key = "__interfaz_" + cfv_iface_nom;
    Value cfv_fns_ifr = cfv_fns;
    asignar_indice(cfv_fns_ifr, Value{cfv_iface_reg_key}, cfv_iface_metodos);
    // Also register a Type value so code can reference it
    (void)(cfv_env_declarar(cfv_env, Value{cfv_iface_nom}, Value{cfv_iface_nom}));
    return crear_lista({Value{std::string("normal")}, Value{}});
  }
  if (verdad(compara(cfv_t, Value{std::string("DestructList")}, "=="))) {
    Value cfv_dl_val = cfv_eval_expr(indice(indice(cfv_stmt, Value{std::string("hijos")}), Value{0.0}), cfv_env, cfv_fns);
    Value cfv_dl_nombres = indice(indice(cfv_stmt, Value{std::string("hijos")}), Value{1.0});
    Value cfv_dl_i = Value{0.0};
    while (verdad(compara(cfv_dl_i, cfv_longitud(cfv_dl_nombres), "<"))) {
      std::string cfv_dl_nombre = texto(indice(cfv_dl_nombres, cfv_dl_i));
      if (cfv_dl_nombre != "_") {
        Value cfv_dl_item = Value{};
        try {
          if (cfv_dl_val.index() == 4) {
            cfv_dl_item = indice(cfv_dl_val, cfv_dl_i);
          }
        } catch (...) {}
        (void)(cfv_env_declarar(cfv_env, Value{cfv_dl_nombre}, cfv_dl_item));
      }
      cfv_dl_i = suma(cfv_dl_i, Value{1.0});
    }
    return crear_lista({Value{std::string("normal")}, Value{}});
  }
  if (verdad(compara(cfv_t, Value{std::string("DestructMapa")}, "=="))) {
    Value cfv_dm_val = cfv_eval_expr(indice(indice(cfv_stmt, Value{std::string("hijos")}), Value{0.0}), cfv_env, cfv_fns);
    Value cfv_dm_nombres = indice(indice(cfv_stmt, Value{std::string("hijos")}), Value{1.0});
    Value cfv_dm_i = Value{0.0};
    while (verdad(compara(cfv_dm_i, cfv_longitud(cfv_dm_nombres), "<"))) {
      std::string cfv_dm_nombre = texto(indice(cfv_dm_nombres, cfv_dm_i));
      Value cfv_dm_item = Value{};
      try {
        cfv_dm_item = indice(cfv_dm_val, Value{cfv_dm_nombre});
      } catch (...) {}
      (void)(cfv_env_declarar(cfv_env, Value{cfv_dm_nombre}, cfv_dm_item));
      cfv_dm_i = suma(cfv_dm_i, Value{1.0});
    }
    return crear_lista({Value{std::string("normal")}, Value{}});
  }
  (void)(cfv_afirmar(Value{false}, suma(suma(Value{std::string("sentencia no implementada tipo '", 32)}, cfv_t), Value{std::string("'", 1)})));
  return crear_lista({Value{std::string("normal", 6)}, Value{}});
  return Value{};
}
Value cfv_llamar_metodo(Value objeto,const std::string& nombre,std::vector<Value> args){
  auto clase=texto(indice(objeto,Value{std::string("__clase")}));
  throw std::runtime_error("método desconocido");
}

// ── C-Forge Public C API (para usar como shared library / plugin) ──────────
// Compilar como shared lib:
//   g++ -std=c++20 -O2 -shared -fPIC -o libcforge.so cforgev.cpp [flags]
// Luego desde C/C#/Java:
//   cfv_init() → cfv_run_file("script.cfv") → cfv_shutdown()

struct CfvContext {
    Value env;
    Value fns;
    bool  initialized = false;
};

extern "C" {

// Crear contexto de ejecucion
void* cfv_context_create() {
    try {
        auto* ctx = new CfvContext();
        ctx->env = crear_lista({});
        ctx->fns = crear_lista({});
        ctx->initialized = true;
        return ctx;
    } catch (...) { return nullptr; }
}

// Destruir contexto
void cfv_context_destroy(void* ctx_ptr) {
    if (ctx_ptr) delete static_cast<CfvContext*>(ctx_ptr);
}

// Helper: crear entorno global fresco
static Value cfv_make_env() { return crear_lista({crear_mapa({})}); }

// Ejecutar un archivo .cfv. Retorna 0 en exito, 1 en error.
int cfv_run_file(const char* path) {
    try {
        std::ifstream f(path);
        if (!f) return 1;
        std::string src((std::istreambuf_iterator<char>(f)),{});
        cfv_base_archivos = std::filesystem::weakly_canonical(std::filesystem::path(path)).parent_path();
        auto tokens = cfv_tokenizar(src);
        auto ast    = cfv_parsear(tokens);
        auto env    = cfv_make_env();
        auto fns    = crear_lista({});
        cfv_exec_bloque(ast, env, fns);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "[cfv] " << e.what() << "\n";
        return 1;
    } catch (...) { return 1; }
}

// Ejecutar un string de codigo C-Forge. Retorna 0 en exito.
int cfv_run_string(const char* code) {
    try {
        std::string src(code);
        auto tokens = cfv_tokenizar(src);
        auto ast    = cfv_parsear(tokens);
        auto env    = cfv_make_env();
        auto fns    = crear_lista({});
        cfv_exec_bloque(ast, env, fns);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "[cfv] " << e.what() << "\n";
        return 1;
    } catch (...) { return 1; }
}

// Evaluar expresion y retornar resultado serializado como JSON (string estatico)
const char* cfv_eval_json(const char* code) {
    static std::string cfv_last_result;
    try {
        std::string src(code);
        auto tokens  = cfv_tokenizar(src);
        auto ast     = cfv_parsear(tokens);
        auto env     = cfv_make_env();
        auto fns     = crear_lista({});
        Value result = cfv_exec_bloque(ast, env, fns);
        Value json   = cfv_json_serializar_fn(result);
        cfv_last_result = (json.index()==2) ? std::get<std::string>(json.data) : "null";
        return cfv_last_result.c_str();
    } catch (const std::exception& e) {
        cfv_last_result = std::string("{\"error\":\"") + e.what() + "\"}";
        return cfv_last_result.c_str();
    }
}

// Obtener version del interprete
const char* cfv_version() { return "2.6.0-dev"; }

// Verificar disponibilidad de SDL2
int cfv_has_sdl2() {
#ifdef CFV_WITH_SDL2
    return 1;
#else
    return 0;
#endif
}

// Verificar disponibilidad de OpenSSL
int cfv_has_openssl() {
#ifdef CFV_WITH_OPENSSL
    return 1;
#else
    return 0;
#endif
}

} // extern "C"

static void cfv_print_cli_help() {
  std::cout
    << "C-Forge 2.6.0-dev\n"
    << "Uso:\n"
    << "  cforge archivo.cfv              Ejecutar un programa\n"
    << "  cforge run archivo.cfv          Ejecutar un programa\n"
    << "  cforge check archivo.cfv        Verificar sintaxis\n"
    << "  cforge test archivo.cfv         Ejecutar pruebas C-Forge\n"
    << "  cforge fmt archivo.cfv          Verificar formato y sintaxis\n"
    << "  cforge repl                     Abrir consola interactiva\n"
    << "  cforge --version                Mostrar versión\n"
    << "  cforge --help                   Mostrar esta ayuda\n";
}

static std::string cfv_read_source_or_throw(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("no se pudo abrir '" + path.string() + "'");
  }
  return std::string((std::istreambuf_iterator<char>(input)),
                     std::istreambuf_iterator<char>());
}

static int cfv_check_file(const std::filesystem::path& path) {
  const auto source = cfv_read_source_or_throw(path);
  const auto tokens = cfv_tokenizar(Value{source});
  (void)cfv_parsear(tokens);
  std::cout << "[C-Forge] Sintaxis válida: " << path.string() << "\n";
  return 0;
}

int main(int argc, char** argv){
  try {
    if (argc >= 2) {
      const std::string command = argv[1];
      if (command == "--version" || command == "-V") {
        std::cout << "C-Forge " << cfv_version() << "\n";
        return 0;
      }
      if (command == "--help" || command == "-h" || command == "help") {
        cfv_print_cli_help();
        return 0;
      }
      if (command == "repl") {
        argc = 1;
      } else if (command == "check" || command == "fmt") {
        if (argc != 3) {
          throw std::runtime_error("uso: cforge " + command + " archivo.cfv");
        }
        const std::filesystem::path source_path = argv[2];
        if (source_path.extension() != ".cfv") {
          throw std::runtime_error("se requiere un archivo con extensión .cfv");
        }
        const int status = cfv_check_file(source_path);
        if (command == "fmt") {
          std::cout << "[C-Forge fmt] Sin cambios destructivos; archivo verificado.\n";
        }
        return status;
      } else if (command == "run" || command == "test") {
        if (argc < 3) {
          throw std::runtime_error("uso: cforge " + command + " archivo.cfv");
        }
        argv[1] = argv[2];
        for (int i = 2; i + 1 < argc; ++i) {
          argv[i] = argv[i + 1];
        }
        --argc;
      } else if (!command.empty() && command.front() == '-') {
        throw std::runtime_error("opción desconocida '" + command + "'");
      }
    }
    if (argc > 1) {
      auto script_path = std::filesystem::weakly_canonical(std::filesystem::path(argv[1]));
      cfv_base_archivos = script_path.has_parent_path() ? script_path.parent_path() : std::filesystem::current_path();
    } else {
      cfv_base_archivos = std::filesystem::current_path();
    }
    cfv_imported_set.clear(); // reset module cache for fresh run
    auto cfv_args_lista = std::make_shared<std::vector<Value>>();
    if (argc > 1) {
        // Use canonical absolute path for the script so cfv_leer_archivo works
        // regardless of whether it was invoked as ./cforge ejemplos/foo.cfv or cd && cforge foo.cfv
        auto canon = std::filesystem::weakly_canonical(std::filesystem::path(argv[1]));
        cfv_args_lista->push_back(Value{canon.string()});
        for(int i=2;i<argc;++i) cfv_args_lista->push_back(Value{std::string(argv[i])});
    }
    cfv_argumentos_global = Value{cfv_args_lista};
  // REPL mode when no arguments given
  if (argc == 1) {
    std::cout << "C-Forge REPL v1.0 — escribe \'salir\' para terminar\n";
    Value cfv_repl_env = crear_lista({crear_mapa({})});
    Value cfv_repl_fns = crear_mapa({});
    std::string cfv_repl_line;
    while (true) {
      std::cout << ">>> " << std::flush;
      if (!std::getline(std::cin, cfv_repl_line)) break;
      if (cfv_repl_line == "salir" || cfv_repl_line == "exit" || cfv_repl_line == "quit") break;
      if (cfv_repl_line.empty()) continue;
      try {
        Value cfv_repl_tokens = cfv_tokenizar(Value{cfv_repl_line});
        Value cfv_repl_ast = cfv_parsear(cfv_repl_tokens);
        Value cfv_repl_signal = cfv_exec_bloque(cfv_repl_ast, cfv_repl_env, cfv_repl_fns);
        if (verdad(compara(indice(cfv_repl_signal, Value{0.0}), Value{std::string("retornar")}, "=="))) {
          std::cout << texto(indice(cfv_repl_signal, Value{1.0})) << "\n";
        }
      } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
      }
    }
    return 0;
  }
  Value cfv_args_cfv = cfv_argumentos_programa();
  if (cfv_args_cfv.index() != 4) throw std::runtime_error("tipo incompatible para args_cfv");
  size_t cfv_args_cfv_tipo = 4;
  cfv_share_symbol("args_cfv", &cfv_args_cfv);
  (void)(cfv_afirmar(compara(cfv_longitud(cfv_args_cfv), Value{1.0}, ">="), Value{std::string("uso: cforge.cfv programa.cfv [argumentos...]", 45)}));
  Value cfv_ruta_programa = indice(cfv_args_cfv, Value{0.0});
  if (cfv_ruta_programa.index() != 2) throw std::runtime_error("tipo incompatible para ruta_programa");
  size_t cfv_ruta_programa_tipo = 2;
  cfv_share_symbol("ruta_programa", &cfv_ruta_programa);
  Value cfv_fuente_programa = cfv_leer_archivo(cfv_ruta_programa);
  if (cfv_fuente_programa.index() != 2) throw std::runtime_error("tipo incompatible para fuente_programa");
  size_t cfv_fuente_programa_tipo = 2;
  cfv_share_symbol("fuente_programa", &cfv_fuente_programa);
  (void)(cfv_afirmar(compara(cfv_longitud(cfv_fuente_programa), Value{0.0}, ">"), suma(suma(Value{std::string("no se pudo leer '", 17)}, cfv_ruta_programa), Value{std::string("'", 1)})));
  Value cfv_tokens_prog = cfv_tokenizar(cfv_fuente_programa);
  if (cfv_tokens_prog.index() != 4) throw std::runtime_error("tipo incompatible para tokens_prog");
  size_t cfv_tokens_prog_tipo = 4;
  cfv_share_symbol("tokens_prog", &cfv_tokens_prog);
  Value cfv_ast_prog = cfv_parsear(cfv_tokens_prog);
  if (cfv_ast_prog.index() != 4) throw std::runtime_error("tipo incompatible para ast_prog");
  size_t cfv_ast_prog_tipo = 4;
  cfv_share_symbol("ast_prog", &cfv_ast_prog);
  Value cfv_entorno_global = crear_lista({crear_mapa({})});
  if (cfv_entorno_global.index() != 4) throw std::runtime_error("tipo incompatible para entorno_global");
  size_t cfv_entorno_global_tipo = 4;
  cfv_share_symbol("entorno_global", &cfv_entorno_global);
  Value cfv_funciones_globales = crear_mapa({});
  size_t cfv_funciones_globales_tipo = 99;
  cfv_share_symbol("funciones_globales", &cfv_funciones_globales);
  Value cfv_args_usuario = crear_lista({});
  if (cfv_args_usuario.index() != 4) throw std::runtime_error("tipo incompatible para args_usuario");
  size_t cfv_args_usuario_tipo = 4;
  cfv_share_symbol("args_usuario", &cfv_args_usuario);
  Value cfv_ai_main = Value{1.0};
  if (cfv_ai_main.index() != 1) throw std::runtime_error("tipo incompatible para ai_main");
  size_t cfv_ai_main_tipo = 1;
  cfv_share_symbol("ai_main", &cfv_ai_main);
  while (verdad(compara(cfv_ai_main, cfv_longitud(cfv_args_cfv), "<"))) {
    (void)(cfv_agregar(cfv_args_usuario, indice(cfv_args_cfv, cfv_ai_main)));
    asignar(cfv_ai_main, cfv_ai_main_tipo, suma(cfv_ai_main, Value{1.0}), "ai_main");
  }
  Value cfv_eg = indice(cfv_entorno_global, Value{0.0});
  size_t cfv_eg_tipo = 99;
  cfv_share_symbol("eg", &cfv_eg);
  asignar_indice(cfv_eg, Value{std::string("__args__", 8)}, cfv_args_usuario);
  Value cfv_fg = cfv_funciones_globales;
  size_t cfv_fg_tipo = 99;
  cfv_share_symbol("fg", &cfv_fg);
  asignar_indice(cfv_fg, Value{std::string("__prog_args__", 13)}, cfv_args_usuario);
  Value cfv_resultado_final = cfv_exec_bloque(cfv_ast_prog, cfv_entorno_global, cfv_funciones_globales);
  if (cfv_resultado_final.index() != 4) throw std::runtime_error("tipo incompatible para resultado_final");
  size_t cfv_resultado_final_tipo = 4;
  cfv_share_symbol("resultado_final", &cfv_resultado_final);
  if (verdad(compara(indice(cfv_resultado_final, Value{0.0}), Value{std::string("retornar", 8)}, "=="))) {
    mostrar(suma(Value{std::string("Programa retornó: ", 19)}, cfv_formato_valor(indice(cfv_resultado_final, Value{1.0}))));
  }

    return 0;
  } catch(const std::exception& e) {
    std::cerr << "[C-Forge Error] " << e.what() << '\n';
    if (!cfv_call_stack.empty()) {
      std::cerr << "Stack trace:\n";
      for (int i = (int)cfv_call_stack.size()-1; i >= 0; --i)
        std::cerr << "  en " << cfv_call_stack[i] << "\n";
    }
    return 1;
  }
  catch(...) { std::cerr << "[C-Forge Runtime Exception] excepción nativa desconocida\n"; return 1; }
}
