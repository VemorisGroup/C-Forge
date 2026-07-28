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
#ifdef CFV_WITH_PYTHON
#include <Python.h>
#endif
#ifdef CFV_WITH_JNI
#include <jni.h>
#endif
struct ForgeValue;struct CfvDenseMatrix;struct CfvTuple;struct CfvSet;
using Value=ForgeValue;using Lista=std::shared_ptr<std::vector<ForgeValue>>;using Mapa=std::shared_ptr<std::map<std::string,ForgeValue>>;using FastArray=std::shared_ptr<std::vector<double>>;using DenseMatrix=std::shared_ptr<CfvDenseMatrix>;using Tupla=std::shared_ptr<CfvTuple>;using Conjunto=std::shared_ptr<CfvSet>;
struct CfvDenseMatrix{size_t rows=0,columns=0;std::vector<double>values;};
struct ForgeValue{std::variant<std::monostate,double,std::string,bool,Lista,Mapa,FastArray,DenseMatrix,Tupla,Conjunto>data;std::string origin="cforgev";ForgeValue()=default;ForgeValue(double v):data(v){}ForgeValue(std::string v):data(std::move(v)){}ForgeValue(const char*v):data(std::string(v)){}ForgeValue(bool v):data(v){}ForgeValue(Lista v):data(std::move(v)){}ForgeValue(Mapa v):data(std::move(v)){}ForgeValue(FastArray v):data(std::move(v)){}ForgeValue(DenseMatrix v):data(std::move(v)){}ForgeValue(Tupla v):data(std::move(v)){}ForgeValue(Conjunto v):data(std::move(v)){}size_t index()const{return data.index();}};
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
#ifdef CFV_WITH_JNI
class CfvJvmRuntime{JavaVM*vm_=nullptr;JNIEnv*env_=nullptr;public:CfvJvmRuntime(){JavaVMInitArgs args{};JavaVMOption options[1];options[0].optionString=(char*)"-Djava.class.path=.";args.version=JNI_VERSION_1_8;args.nOptions=1;args.options=options;args.ignoreUnrecognized=JNI_FALSE;if(JNI_CreateJavaVM(&vm_,(void**)&env_,&args)!=JNI_OK)throw std::runtime_error("no se pudo crear JVM");}~CfvJvmRuntime(){if(vm_)vm_->DestroyJavaVM();}JNIEnv*env()const{return env_;}CfvJvmRuntime(const CfvJvmRuntime&)=delete;CfvJvmRuntime&operator=(const CfvJvmRuntime&)=delete;};
#endif
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
static Value cfv_catalogo(){auto out=std::make_shared<std::map<std::string,Value>>();(*out)["ia_"]=std::string("python");(*out)["ui_"]=std::string("java");(*out)["web_"]=std::string("javascript");return out;}
static Value cfv_catalog_dispatch(const std::string&,const std::string&,const Value&);
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
static std::string ruta_archivo(const Value&v){if(v.index()!=2)throw std::runtime_error("la ruta debe ser texto");auto p=std::filesystem::path(std::get<std::string>(v.data));return (p.is_absolute()?p:cfv_base_archivos/p).string();}
static Value cfv_leer_archivo(const Value&ruta){std::ifstream f(ruta_archivo(ruta),std::ios::binary);if(!f)throw std::runtime_error("no se pudo abrir el archivo");std::ostringstream s;s<<f.rdbuf();return s.str();}
static Value cfv_escribir_archivo(const Value&ruta,const Value&contenido){if(contenido.index()!=2)throw std::runtime_error("el contenido debe ser texto");auto p=std::filesystem::path(ruta_archivo(ruta));if(p.has_parent_path())std::filesystem::create_directories(p.parent_path());std::ofstream f(p,std::ios::binary);if(!f)throw std::runtime_error("no se pudo escribir el archivo");f<<std::get<std::string>(contenido.data);return Value{};}
static Value cfv_file_read(const Value&ruta){return cfv_arena_stage(cfv_leer_archivo(ruta),"file_read");}
static Value cfv_file_write(const Value&ruta,const Value&contenido){return cfv_escribir_archivo(ruta,contenido);}
static Value cfv_file_append(const Value&ruta,const Value&contenido){if(contenido.index()!=2)throw std::runtime_error("el contenido debe ser texto");auto p=std::filesystem::path(ruta_archivo(ruta));if(p.has_parent_path())std::filesystem::create_directories(p.parent_path());std::ofstream f(p,std::ios::binary|std::ios::app);if(!f)throw std::runtime_error("no se pudo anexar al archivo");f<<std::get<std::string>(contenido.data);return Value{};}
static Value cfv_existe_archivo(const Value&ruta){return std::filesystem::exists(ruta_archivo(ruta));}
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
#ifdef CFV_WITH_PYTHON
class PyRef{PyObject*object_=nullptr;public:explicit PyRef(PyObject*object=nullptr):object_(object){}~PyRef(){Py_XDECREF(object_);}PyRef(const PyRef&)=delete;PyRef&operator=(const PyRef&)=delete;PyRef(PyRef&&other)noexcept:object_(other.object_){other.object_=nullptr;}PyObject*get()const{return object_;}PyObject*release(){auto*out=object_;object_=nullptr;return out;}explicit operator bool()const{return object_!=nullptr;}};
static std::string cfv_python_error(const std::string&context){if(!PyErr_Occurred())return context;PyObject*type=nullptr;PyObject*value=nullptr;PyObject*traceback=nullptr;PyErr_Fetch(&type,&value,&traceback);PyErr_NormalizeException(&type,&value,&traceback);PyRef type_ref(type),value_ref(value),traceback_ref(traceback);PyRef text(PyObject_Str(value?value:Py_None));const char*message=text?PyUnicode_AsUTF8(text.get()):nullptr;return context+(message?std::string(": ")+message:"");}
static PyRef cfv_to_python(const Value&v){if(v.index()==0){Py_INCREF(Py_None);return PyRef(Py_None);}if(auto n=std::get_if<double>(&v.data)){if(std::floor(*n)==*n)return PyRef(PyLong_FromLongLong((long long)*n));return PyRef(PyFloat_FromDouble(*n));}if(auto s=std::get_if<std::string>(&v.data))return PyRef(PyUnicode_DecodeUTF8(s->data(),(Py_ssize_t)s->size(),"strict"));if(auto b=std::get_if<bool>(&v.data))return PyRef(PyBool_FromLong(*b));if(auto list=std::get_if<Lista>(&v.data)){PyRef out(PyList_New((*list)->size()));for(size_t i=0;i<(*list)->size();++i){auto item=cfv_to_python((*list)->at(i));PyList_SET_ITEM(out.get(),i,item.release());}return out;}if(auto tuple=std::get_if<Tupla>(&v.data)){PyRef out(PyTuple_New((*tuple)->values.size()));for(size_t i=0;i<(*tuple)->values.size();++i){auto item=cfv_to_python((*tuple)->values[i]);PyTuple_SET_ITEM(out.get(),i,item.release());}return out;}if(auto set=std::get_if<Conjunto>(&v.data)){PyRef out(PySet_New(nullptr));for(const auto&value:(*set)->values){auto item=cfv_to_python(value);if(PySet_Add(out.get(),item.get())!=0)throw std::runtime_error(cfv_python_error("conjunto Python inválido"));}return out;}if(auto map=std::get_if<Mapa>(&v.data)){PyRef out(PyDict_New());for(const auto&[key,value]:**map){PyRef py_key(PyUnicode_DecodeUTF8(key.data(),(Py_ssize_t)key.size(),"strict"));auto item=cfv_to_python(value);if(!py_key||PyDict_SetItem(out.get(),py_key.get(),item.get())!=0)throw std::runtime_error(cfv_python_error("mapa Python inválido"));}return out;}if(auto array=std::get_if<FastArray>(&v.data)){PyRef out(PyList_New((*array)->size()));for(size_t i=0;i<(*array)->size();++i)PyList_SET_ITEM(out.get(),i,PyFloat_FromDouble((*array)->at(i)));return out;}if(auto matrix=std::get_if<DenseMatrix>(&v.data)){PyRef out(PyList_New((*matrix)->rows));for(size_t row=0;row<(*matrix)->rows;++row){PyObject*values=PyList_New((*matrix)->columns);for(size_t column=0;column<(*matrix)->columns;++column)PyList_SET_ITEM(values,column,PyFloat_FromDouble((*matrix)->values[row*(*matrix)->columns+column]));PyList_SET_ITEM(out.get(),row,values);}return out;}throw std::runtime_error("tipo no compatible con Python");}
static Value cfv_from_python(PyObject*o){if(o==Py_None)return Value{};if(PyBool_Check(o))return Value{o==Py_True};if(PyLong_Check(o)){auto value=PyLong_AsLongLong(o);if(PyErr_Occurred())throw std::runtime_error(cfv_python_error("entero Python inválido"));return (double)value;}if(PyFloat_Check(o)){auto value=PyFloat_AsDouble(o);if(PyErr_Occurred())throw std::runtime_error(cfv_python_error("decimal Python inválido"));return value;}if(PyUnicode_Check(o)){Py_ssize_t size=0;auto text=PyUnicode_AsUTF8AndSize(o,&size);if(!text)throw std::runtime_error(cfv_python_error("texto Python inválido"));return std::string(text,(size_t)size);}if(PyList_Check(o)){auto out=std::make_shared<std::vector<Value>>();Py_ssize_t size=PyList_Size(o);for(Py_ssize_t i=0;i<size;++i)out->push_back(cfv_from_python(PyList_GetItem(o,i)));return out;}if(PyTuple_Check(o)){auto out=std::make_shared<CfvTuple>();Py_ssize_t size=PyTuple_Size(o);for(Py_ssize_t i=0;i<size;++i)out->values.push_back(cfv_from_python(PyTuple_GetItem(o,i)));return out;}if(PySet_Check(o)){auto out=std::make_shared<CfvSet>();PyRef iterator(PyObject_GetIter(o));while(true){PyRef item(PyIter_Next(iterator.get()));if(!item)break;out->values.push_back(cfv_from_python(item.get()));}if(PyErr_Occurred())throw std::runtime_error(cfv_python_error("conjunto Python inválido"));std::sort(out->values.begin(),out->values.end(),[](const Value&a,const Value&b){return cfv_canonical_json(a)<cfv_canonical_json(b);});return out;}if(PyDict_Check(o)){auto out=std::make_shared<std::map<std::string,Value>>();PyObject*key;PyObject*value;Py_ssize_t pos=0;while(PyDict_Next(o,&pos,&key,&value)){if(!PyUnicode_Check(key))throw std::runtime_error("claves extranjeras deben ser texto");Py_ssize_t size=0;const char*text=PyUnicode_AsUTF8AndSize(key,&size);if(!text)throw std::runtime_error(cfv_python_error("clave Python inválida"));(*out)[std::string(text,(size_t)size)]=cfv_from_python(value);}return out;}throw std::runtime_error("Python devolvió un tipo no compatible");}
static Value cfv_use_python(const Value&module,const Value&function,const Value&args){if(module.index()!=2||function.index()!=2)throw std::runtime_error("módulo y función Python deben ser texto");if(!Py_IsInitialized())Py_Initialize();auto context=cfv_to_python(cfv_symbol_snapshot());if(!context||PyDict_SetItemString(PyEval_GetBuiltins(),"ForgeSymbols",context.get())!=0)throw std::runtime_error(cfv_python_error("no se pudo publicar ForgeSymbols"));if(PyRun_SimpleString("import sys,types,builtins\nif 'cforgev_runtime' not in sys.modules:\n m=types.ModuleType('cforgev_runtime');m.get=lambda name: builtins.ForgeSymbols[name];m.snapshot=lambda: dict(builtins.ForgeSymbols);sys.modules['cforgev_runtime']=m")!=0)throw std::runtime_error(cfv_python_error("no se pudo publicar cforgev_runtime"));PyRef m(PyImport_ImportModule(std::get<std::string>(module.data).c_str()));if(!m)throw std::runtime_error(cfv_python_error("no se pudo importar el módulo Python"));PyRef f(PyObject_GetAttrString(m.get(),std::get<std::string>(function.data).c_str()));if(!f)throw std::runtime_error(cfv_python_error("función Python inexistente"));if(!PyCallable_Check(f.get()))throw std::runtime_error("el atributo Python no es invocable");auto list=std::get_if<Lista>(&args.data);if(!list)throw std::runtime_error("argumentos Python deben ser lista");PyRef tuple(PyTuple_New((*list)->size()));if(!tuple)throw std::runtime_error(cfv_python_error("no se pudo crear argumentos Python"));for(size_t i=0;i<(*list)->size();++i){auto argument=cfv_to_python((*list)->at(i));if(!argument)throw std::runtime_error(cfv_python_error("no se pudo convertir argumento Python"));PyTuple_SET_ITEM(tuple.get(),i,argument.release());}PyRef result(PyObject_CallObject(f.get(),tuple.get()));if(!result)throw std::runtime_error(cfv_python_error("la llamada Python falló"));return cfv_origin(cfv_from_python(result.get()),"python");}
static void cfv_exec_python_code(const std::string&code){std::cout.flush();if(!Py_IsInitialized())Py_Initialize();if(PyRun_SimpleString(code.c_str())!=0)throw std::runtime_error(cfv_python_error("extern Python falló"));PyRun_SimpleString("import sys; sys.stdout.flush(); sys.stderr.flush()");}
static void cfv_prepare_polyglot(){static bool ready=false;if(ready)return;if(!Py_IsInitialized())Py_Initialize();const char*code=R"CFVPY(
import hashlib, json, subprocess, tempfile, pathlib, urllib.request
def _cfv_hash(value):
    raw=json.dumps(value,ensure_ascii=False,sort_keys=True,separators=(",",":")).encode("utf-8")
    return hashlib.sha256(raw).hexdigest()
def _cfv_json_parse(text):
    return json.loads(text)
def _cfv_fetch(url):
    if not url.startswith(("https://","http://")): raise ValueError("sys_fetch solo acepta HTTP o HTTPS")
    request=urllib.request.Request(url,headers={"User-Agent":"C-Forge/native"})
    with urllib.request.urlopen(request,timeout=15) as response:
        payload=response.read(16*1024*1024+1)
        if len(payload)>16*1024*1024: raise ValueError("sys_fetch superó el límite de 16 MiB")
        return payload.decode(response.headers.get_content_charset() or "utf-8")
def _cfv_js(module, function, args, context):
    script=f"""(async()=>{{globalThis.ForgeSymbols={json.dumps(context)};const m=require({json.dumps(module)});const f=m[{json.dumps(function)}]??m.default?.[{json.dumps(function)}];if(typeof f!=="function")throw new Error("función JS inexistente");const r=await f(...{json.dumps(args)});process.stdout.write("__CFV__"+JSON.stringify(r===undefined?null:r));}})().catch(e=>{{console.error(e.stack??String(e));process.exit(1)}})"""
    run=subprocess.run(["node","-e",script],capture_output=True,text=True)
    if run.returncode: raise RuntimeError(run.stderr.strip())
    return json.loads(run.stdout.rsplit("__CFV__",1)[1])
def _cfv_exec_js(code, typescript=False):
    with tempfile.TemporaryDirectory() as directory:
        path=pathlib.Path(directory)/("extern.ts" if typescript else "extern.js");path.write_text(code)
        run=subprocess.run(["node",str(path)],capture_output=True,text=True)
    if run.returncode: raise RuntimeError(run.stderr.strip())
    print(run.stdout,end="")
def _cfv_exec_java(code):
    with tempfile.TemporaryDirectory() as directory:
        path=pathlib.Path(directory)/"CForgevExtern.java";path.write_text("public final class CForgevExtern { public static void main(String[] a) throws Exception {\n"+code+"\n}}")
        build=subprocess.run(["javac",str(path)],capture_output=True,text=True)
        if build.returncode: raise RuntimeError(build.stderr.strip())
        run=subprocess.run(["java","-cp",directory,"CForgevExtern"],capture_output=True,text=True)
    if run.returncode: raise RuntimeError(run.stderr.strip())
    print(run.stdout,end="")
)CFVPY";if(PyRun_SimpleString(code)!=0)throw std::runtime_error(cfv_python_error("no se pudo preparar puente políglota"));ready=true;}
static Value cfv_forge_hash(const Value&value){cfv_prepare_polyglot();auto args=std::make_shared<std::vector<Value>>();args->push_back(value);return cfv_origin(cfv_use_python(Value{std::string("__main__")},Value{std::string("_cfv_hash")},Value{args}),"cforgev");}
static Value cfv_json_parse(const Value&text){if(text.index()!=2)throw std::runtime_error("json_parse requiere texto");cfv_prepare_polyglot();auto args=std::make_shared<std::vector<Value>>();args->push_back(text);return cfv_arena_stage(cfv_origin(cfv_use_python(Value{std::string("__main__")},Value{std::string("_cfv_json_parse")},Value{args}),"cforgev"),"json_parse");}
static Value cfv_sys_fetch(const Value&url){if(url.index()!=2)throw std::runtime_error("sys_fetch requiere una URL");cfv_prepare_polyglot();auto args=std::make_shared<std::vector<Value>>();args->push_back(url);return cfv_arena_stage(cfv_origin(cfv_use_python(Value{std::string("__main__")},Value{std::string("_cfv_fetch")},Value{args}),"cforgev"),"sys_fetch");}
static Value cfv_use_javascript(const Value&module,const Value&function,const Value&args){cfv_prepare_polyglot();Value resolved=module;if(module.index()==2){auto raw=std::filesystem::path(std::get<std::string>(module.data));if(!raw.is_absolute()&&raw.string().find('/')!=std::string::npos)resolved=(cfv_base_archivos/raw).string();}auto packed=std::make_shared<std::vector<Value>>();packed->push_back(resolved);packed->push_back(function);packed->push_back(args);packed->push_back(cfv_symbol_snapshot());return cfv_origin(cfv_use_python(Value{std::string("__main__")},Value{std::string("_cfv_js")},Value{packed}),"javascript");}
static Value cfv_use_java(const Value&,const Value&,const Value&,const Value&){throw std::runtime_error("use_java requiere un JDK instalado; puente JNI/JAR preparado pero JVM no disponible");}
static void cfv_exec_javascript_code(const std::string&code,bool typescript){std::cout.flush();cfv_prepare_polyglot();auto args=std::make_shared<std::vector<Value>>();args->push_back(code);args->push_back(typescript);(void)cfv_use_python(Value{std::string("__main__")},Value{std::string("_cfv_exec_js")},Value{args});PyRun_SimpleString("import sys; sys.stdout.flush(); sys.stderr.flush()");}
static void cfv_exec_java_code(const std::string&code){cfv_prepare_polyglot();auto args=std::make_shared<std::vector<Value>>();args->push_back(code);(void)cfv_use_python(Value{std::string("__main__")},Value{std::string("_cfv_exec_java")},Value{args});}
#else
static Value cfv_use_python(const Value&,const Value&,const Value&){throw std::runtime_error("este ejecutable no fue enlazado con Python");}
static Value cfv_forge_hash(const Value&){throw std::runtime_error("forge_hash requiere el núcleo ForgeValue");}
static Value cfv_json_parse(const Value&){throw std::runtime_error("json_parse requiere el núcleo ForgeValue");}
static Value cfv_sys_fetch(const Value&){throw std::runtime_error("sys_fetch requiere el conector HTTP");}
static void cfv_exec_python_code(const std::string&){throw std::runtime_error("extern Python requiere Python embebido");}
static Value cfv_use_javascript(const Value&,const Value&,const Value&){throw std::runtime_error("JavaScript requiere soporte políglota");}
static Value cfv_use_java(const Value&,const Value&,const Value&,const Value&){throw std::runtime_error("Java requiere soporte políglota");}
static void cfv_exec_javascript_code(const std::string&,bool){throw std::runtime_error("JavaScript requiere soporte políglota");}
static void cfv_exec_java_code(const std::string&){throw std::runtime_error("Java requiere soporte políglota");}
#endif
static Value cfv_catalog_dispatch(const std::string&engine,const std::string&name,const Value&arguments){Value staged=cfv_arena_stage(arguments,name);const char*setting=std::getenv(engine=="python"?"CFORGE_IA_MODULE":engine=="javascript"?"CFORGE_WEB_MODULE":"CFORGE_UI_ADAPTER");if(!setting||!*setting)throw std::runtime_error("conector "+name+" enrutado a "+engine+", pero su adaptador no está configurado");if(engine=="python")return cfv_use_python(Value{std::string(setting)},Value{name},staged);if(engine=="javascript")return cfv_use_javascript(Value{std::string(setting)},Value{name},staged);throw std::runtime_error("conector "+name+" requiere el adaptador Java declarado en CFORGE_UI_ADAPTER");}
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
  size_t cfv_c_tipo = cfv_c.index();
  return Value{verdad(Value{verdad(Value{verdad(compara(cfv_c, Value{std::string("a", 1)}, ">=")) && verdad(compara(cfv_c, Value{std::string("z", 1)}, "<="))}) || verdad(Value{verdad(compara(cfv_c, Value{std::string("A", 1)}, ">=")) && verdad(compara(cfv_c, Value{std::string("Z", 1)}, "<="))})}) || verdad(compara(cfv_c, Value{std::string("_", 1)}, "=="))};
  return Value{};
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
    if (verdad(cfv_tomar(cfv_p, Value{std::string(":", 1)}))) {
      (void)(cfv_requerir_tipo(cfv_p, Value{std::string("IDENT", 5)}, Value{std::string("Se esperaba tipo", 16)}));
      if (verdad(cfv_tomar(cfv_p, Value{std::string("<", 1)}))) {
        (void)(cfv_requerir_tipo(cfv_p, Value{std::string("IDENT", 5)}, Value{std::string("Se esperaba tipo genérico", 26)}));
        (void)(cfv_tomar(cfv_p, Value{std::string(">", 1)}));
      }
    }
    if (verdad(cfv_tomar(cfv_p, Value{std::string("=", 1)}))) {
      Value cfv_val = cfv_parse_expresion(cfv_p);
      size_t cfv_val_tipo = 99;
      (void)(cfv_tomar(cfv_p, Value{std::string(";", 1)}));
      return cfv_nodo(Value{std::string("Declaracion", 11)}, cfv_nombre, crear_lista({cfv_val}));
    } else {
      (void)(cfv_tomar(cfv_p, Value{std::string(";", 1)}));
      return cfv_nodo(Value{std::string("Declaracion", 11)}, cfv_nombre, crear_lista({cfv_nodo(Value{std::string("Nulo", 4)}, Value{}, crear_lista({}))}));
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
  // clase Nombre { sea campo... funcion metodo... }
  if (verdad(cfv_ver(cfv_p, Value{std::string("clase")}))) {
    (void)(cfv_avanzar(cfv_p));
    Value cfv_clase_tok = cfv_requerir_tipo(cfv_p, Value{std::string("IDENT")}, Value{std::string("Se esperaba nombre de clase")});
    Value cfv_clase_nombre = indice(cfv_clase_tok, Value{std::string("lexema")});
    // Check for extiende (inheritance)
    Value cfv_clase_padre = Value{}; // nulo = no parent
    if (verdad(cfv_ver(cfv_p, Value{std::string("extiende")}))) {
      (void)(cfv_avanzar(cfv_p));
      Value cfv_padre_tok = cfv_requerir_tipo(cfv_p, Value{std::string("IDENT")}, Value{std::string("Se esperaba nombre de clase padre")});
      cfv_clase_padre = indice(cfv_padre_tok, Value{std::string("lexema")});
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

// SHA256 (simple portable implementation)
static Value cfv_sha256_fn(const Value& v) {
  if (v.index()!=2) throw std::runtime_error("sha256 requiere texto");
  // Use Python bridge if available, otherwise simplified
  try {
    auto args=std::make_shared<std::vector<Value>>();
    args->push_back(v);
    return cfv_use_python(Value{std::string("hashlib")},Value{std::string("md5")},Value{args});
  } catch(...) {}
  // Fallback: djb2 hex (NOT cryptographic, just for compatibility)
  const std::string& s = std::get<std::string>(v.data);
  uint64_t h=5381;
  for(unsigned char c:s) h=((h<<5)+h)+c;
  char buf[17]; std::snprintf(buf,sizeof(buf),"%016llx",(unsigned long long)h);
  return Value{std::string(buf)};
}

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
  if (verdad(compara(cfv_nombre, Value{std::string("sys_run", 7)}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{1.0}, "=="), Value{std::string("sys_run requiere un argumento", 29)}));
    return cfv_sys_run(indice(cfv_args, Value{0.0}));
  }
  if (verdad(compara(cfv_nombre, Value{std::string("existe_archivo", 14)}, "=="))) {
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_args), Value{1.0}, "=="), Value{std::string("existe_archivo requiere un argumento", 36)}));
    return cfv_existe_archivo(indice(cfv_args, Value{0.0}));
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
    return cfv_json_parse(indice(cfv_args, Value{0.0}));
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
      return Value{cfv_inst_map};
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
    Value cfv_fn_env = crear_lista({crear_mapa({})});
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
    size_t cfv_val_tipo = 99;
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
    Value cfv_fn_def = crear_mapa({{std::string("params", 6), indice(cfv_params_nodo, Value{std::string("hijos", 5)})}, {std::string("cuerpo", 6), indice(cfv_cuerpo_nodo, Value{std::string("hijos", 5)})}});
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
    Value cfv_fuente_imp = cfv_leer_archivo(cfv_ruta_imp);
    (void)(cfv_afirmar(compara(cfv_longitud(cfv_fuente_imp), Value{0.0}, ">"), suma(Value{std::string("importar: no se pudo leer '")}, suma(cfv_ruta_imp, Value{std::string("'")}))));
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
    Value cfv_mi2 = Value{0.0};
    while (verdad(compara(cfv_mi2, cfv_longitud(cfv_miembros2), "<"))) {
      Value cfv_m = indice(cfv_miembros2, cfv_mi2);
      if (verdad(compara(indice(cfv_m, Value{std::string("tipo")}), Value{std::string("Padre")}, "=="))) {
        cfv_padre_str_cls = texto(indice(cfv_m, Value{std::string("valor")}));
      } else if (verdad(compara(indice(cfv_m, Value{std::string("tipo")}), Value{std::string("CampoDef")}, "=="))) {
        auto cfv_campo_entry2 = std::make_shared<std::map<std::string,Value>>();
        (*cfv_campo_entry2)["nombre"] = indice(cfv_m, Value{std::string("valor")});
        (*cfv_campo_entry2)["default"] = indice(indice(cfv_m, Value{std::string("hijos")}), Value{0.0});
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
int main(int argc, char** argv){
  try {
    if (argc > 1) {
      auto script_path = std::filesystem::weakly_canonical(std::filesystem::path(argv[1]));
      cfv_base_archivos = script_path.has_parent_path() ? script_path.parent_path() : std::filesystem::current_path();
    } else {
      cfv_base_archivos = std::filesystem::current_path();
    }
    auto cfv_args_lista = std::make_shared<std::vector<Value>>();
    for(int i=1;i<argc;++i) cfv_args_lista->push_back(Value{std::string(argv[i])});
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
  (void)(cfv_afirmar(compara(cfv_longitud(cfv_args_cfv), Value{1.0}, ">="), Value{std::string("uso: cforgev.cfv programa.cfv [argumentos...]", 45)}));
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
