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
static Value cfv_longitud(const Value&v){if(auto p=std::get_if<std::string>(&v.data))return (double)p->size();if(auto p=std::get_if<Lista>(&v.data))return (double)(*p)->size();if(auto p=std::get_if<Mapa>(&v.data))return (double)(*p)->size();if(auto p=std::get_if<FastArray>(&v.data))return (double)(*p)->size();if(auto p=std::get_if<DenseMatrix>(&v.data))return (double)(*p)->rows;if(auto p=std::get_if<Tupla>(&v.data))return (double)(*p)->values.size();if(auto p=std::get_if<Conjunto>(&v.data))return (double)(*p)->values.size();throw std::runtime_error("longitud requiere una colección");}
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

// runtime_extra.cpp — helpers para el compilador_nativo, sin duplicar runtime.cpp

static bool booleano(const Value& v) {
  if (auto p = std::get_if<bool>(&v.data)) return *p;
  if (v.index() == 0) return false;
  if (auto p = std::get_if<double>(&v.data)) return *p != 0.0;
  if (auto p = std::get_if<std::string>(&v.data)) return !p->empty();
  return true;
}

static Value _mk_lista(std::initializer_list<Value> items) {
  auto v = std::make_shared<std::vector<Value>>(items);
  return Value{v};
}
static Value _mk_mapa_vacio() {
  return Value{std::make_shared<std::map<std::string,Value>>()};
}
static Value _mk_mapa(std::initializer_list<std::pair<const std::string,Value>> items) {
  auto m = std::make_shared<std::map<std::string,Value>>();
  for (auto& [k,v] : items) (*m)[k] = v;
  return Value{m};
}
static Value _idx(const Value& obj, const Value& key) {
  if (auto p = std::get_if<Lista>(&obj.data)) {
    double n = numero(key);
    if (n < 0 || std::floor(n) != n || (size_t)n >= (*p)->size())
      throw std::runtime_error("indice fuera de rango");
    return (**p)[(size_t)n];
  }
  if (auto p = std::get_if<Mapa>(&obj.data)) {
    auto it = (*p)->find(std::get<std::string>(key.data));
    if (it == (*p)->end()) return Value{};
    return it->second;
  }
  if (auto p = std::get_if<std::string>(&obj.data)) {
    size_t idx = (size_t)numero(key);
    if (idx >= p->size()) throw std::runtime_error("indice fuera de rango");
    return Value{std::string(1, (*p)[idx])};
  }
  throw std::runtime_error("no admite indices");
}
static Value _campo(const Value& obj, const std::string& campo) {
  if (auto p = std::get_if<Mapa>(&obj.data)) {
    auto it = (*p)->find(campo);
    if (it == (*p)->end()) return Value{};
    return it->second;
  }
  if (campo == "length" || campo == "longitud") return cfv_longitud(obj);
  throw std::runtime_error("campo desconocido: " + campo);
}
static void _set_campo(Value obj, const std::string& campo, Value val) {
  if (auto p = std::get_if<Mapa>(&obj.data)) {
    (*p)->operator[](campo) = std::move(val);
    return;
  }
  throw std::runtime_error("_set_campo requiere mapa");
}
static Value _add(const Value& a, const Value& b) {
  if (auto pa = std::get_if<std::string>(&a.data)) {
    if (auto pb = std::get_if<std::string>(&b.data)) return Value{*pa + *pb};
    return Value{*pa + texto(b)};
  }
  if (std::get_if<std::string>(&b.data)) return Value{texto(a) + std::get<std::string>(b.data)};
  return Value{numero(a) + numero(b)};
}
static Value _sub(const Value& a, const Value& b) { return Value{numero(a) - numero(b)}; }
static Value _mul(const Value& a, const Value& b) { return Value{numero(a) * numero(b)}; }
static Value _div(const Value& a, const Value& b) {
  double d = numero(b);
  if (d == 0) throw std::runtime_error("division por cero");
  return Value{numero(a) / d};
}
static Value _mod(const Value& a, const Value& b) { return Value{std::fmod(numero(a), numero(b))}; }

Value cfv_es_letra(Value cfv_c);
Value cfv_es_digito(Value cfv_c);
Value cfv_es_alfanumerico(Value cfv_c);
Value cfv_es_espacio_blanco(Value cfv_c);
Value cfv_contiene_char(Value cfv_s,Value cfv_c);
Value cfv_subcad(Value cfv_s,Value cfv_inicio,Value cfv_fin);
Value cfv_texto_contiene(Value cfv_s,Value cfv_sub);
Value cfv_texto_reemplazar(Value cfv_s,Value cfv_viejo,Value cfv_nuevo);
Value cfv_num_a_txt(Value cfv_n);
Value cfv_juntar(Value cfv_lista_txt,Value cfv_sep);
Value cfv_escapar_cpp(Value cfv_s);
Value cfv_safe(Value cfv_nombre);
Value cfv_es_keyword(Value cfv_s);
Value cfv_mk_tok(Value cfv_tipo,Value cfv_valor,Value cfv_linea);
Value cfv_scan_string_dq(Value cfv_fuente,Value cfv_inicio,Value cfv_ln_in);
Value cfv_scan_string_sq(Value cfv_fuente,Value cfv_inicio,Value cfv_ln_in);
Value cfv_tokenizar(Value cfv_fuente);
Value cfv_tok_actual(Value cfv_st);
Value cfv_tok_peek(Value cfv_st,Value cfv_offset);
Value cfv_tok_es(Value cfv_st,Value cfv_tipo,Value cfv_valor);
Value cfv_tok_es_tipo(Value cfv_st,Value cfv_tipo);
Value cfv_tok_es_key(Value cfv_st,Value cfv_v);
Value cfv_tok_es_delim(Value cfv_st,Value cfv_v);
Value cfv_tok_es_op(Value cfv_st,Value cfv_v);
Value cfv_avanzar(Value cfv_st);
Value cfv_consumir(Value cfv_st,Value cfv_tipo,Value cfv_valor);
Value cfv_consumir_tipo(Value cfv_st,Value cfv_tipo);
Value cfv_parse_tipo(Value cfv_st);
Value cfv_parse_expr(Value cfv_st);
Value cfv_parse_asignacion(Value cfv_st);
Value cfv_parse_or(Value cfv_st);
Value cfv_parse_and(Value cfv_st);
Value cfv_parse_igual(Value cfv_st);
Value cfv_parse_cmp(Value cfv_st);
Value cfv_parse_suma(Value cfv_st);
Value cfv_parse_mul(Value cfv_st);
Value cfv_parse_unario(Value cfv_st);
Value cfv_parse_postfijo(Value cfv_st);
Value cfv_parse_args(Value cfv_st);
Value cfv_parse_primario(Value cfv_st);
Value cfv_parse_bloque(Value cfv_st);
Value cfv_parse_stmt(Value cfv_st);
Value cfv_parse_programa(Value cfv_st);
Value cfv_pad(Value cfv_st);
Value cfv_gen_expr(Value cfv_st,Value cfv_nodo);
Value cfv_gen_stmt(Value cfv_st,Value cfv_nodo);
Value cfv_es_letra(Value cfv_c){
  return Value{booleano(Value{booleano(Value{booleano(compara(cfv_c,Value{std::string("a")},">="))&&booleano(compara(cfv_c,Value{std::string("z")},"<="))})||booleano(Value{booleano(compara(cfv_c,Value{std::string("A")},">="))&&booleano(compara(cfv_c,Value{std::string("Z")},"<="))})})||booleano(compara(cfv_c,Value{std::string("_")},"=="))};
return Value{};}

Value cfv_es_digito(Value cfv_c){
  return Value{booleano(compara(cfv_c,Value{std::string("0")},">="))&&booleano(compara(cfv_c,Value{std::string("9")},"<="))};
return Value{};}

Value cfv_es_alfanumerico(Value cfv_c){
  return Value{booleano(cfv_es_letra(cfv_c))||booleano(cfv_es_digito(cfv_c))};
return Value{};}

Value cfv_es_espacio_blanco(Value cfv_c){
  return Value{booleano(Value{booleano(compara(cfv_c,Value{std::string(" ")},"=="))||booleano(compara(cfv_c,Value{std::string("\t")},"=="))})||booleano(compara(cfv_c,Value{std::string("\\r")},"=="))};
return Value{};}

Value cfv_contiene_char(Value cfv_s,Value cfv_c){
  Value cfv_i=Value{(double)0};
  while(booleano(compara(cfv_i,cfv_longitud(cfv_s),"<"))){
    if(booleano(compara(_idx(cfv_s,cfv_i),cfv_c,"=="))){
      return Value{true};
    }
    cfv_i=_add(cfv_i,Value{(double)1});
  }
  return Value{false};
return Value{};}

Value cfv_subcad(Value cfv_s,Value cfv_inicio,Value cfv_fin){
  Value cfv_r=Value{std::string("")};
  Value cfv_i=cfv_inicio;
  while(booleano(Value{booleano(compara(cfv_i,cfv_fin,"<"))&&booleano(compara(cfv_i,cfv_longitud(cfv_s),"<"))})){
    cfv_r=_add(cfv_r,_idx(cfv_s,cfv_i));
    cfv_i=_add(cfv_i,Value{(double)1});
  }
  return cfv_r;
return Value{};}

Value cfv_texto_contiene(Value cfv_s,Value cfv_sub){
  Value cfv_ls=cfv_longitud(cfv_s);
  Value cfv_lsub=cfv_longitud(cfv_sub);
  if(booleano(compara(cfv_lsub,Value{(double)0},"=="))){
    return Value{true};
  }
  Value cfv_i=Value{(double)0};
  while(booleano(compara(cfv_i,_sub(cfv_ls,cfv_lsub),"<="))){
    if(booleano(compara(cfv_subcad(cfv_s,cfv_i,_add(cfv_i,cfv_lsub)),cfv_sub,"=="))){
      return Value{true};
    }
    cfv_i=_add(cfv_i,Value{(double)1});
  }
  return Value{false};
return Value{};}

Value cfv_texto_reemplazar(Value cfv_s,Value cfv_viejo,Value cfv_nuevo){
  Value cfv_r=Value{std::string("")};
  Value cfv_ls=cfv_longitud(cfv_s);
  Value cfv_lv=cfv_longitud(cfv_viejo);
  Value cfv_i=Value{(double)0};
  while(booleano(compara(cfv_i,cfv_ls,"<"))){
    if(booleano(Value{booleano(Value{booleano(compara(cfv_lv,Value{(double)0},">"))&&booleano(compara(_add(cfv_i,cfv_lv),cfv_ls,"<="))})&&booleano(compara(cfv_subcad(cfv_s,cfv_i,_add(cfv_i,cfv_lv)),cfv_viejo,"=="))})){
      cfv_r=_add(cfv_r,cfv_nuevo);
      cfv_i=_add(cfv_i,cfv_lv);
    } else{
      cfv_r=_add(cfv_r,_idx(cfv_s,cfv_i));
      cfv_i=_add(cfv_i,Value{(double)1});
    }
  }
  return cfv_r;
return Value{};}

Value cfv_num_a_txt(Value cfv_n){
  return cfv_a_texto(cfv_n);
return Value{};}

Value cfv_juntar(Value cfv_lista_txt,Value cfv_sep){
  Value cfv_r=Value{std::string("")};
  Value cfv_i=Value{(double)0};
  while(booleano(compara(cfv_i,cfv_longitud(cfv_lista_txt),"<"))){
    if(booleano(compara(cfv_i,Value{(double)0},">"))){
      cfv_r=_add(cfv_r,cfv_sep);
    }
    cfv_r=_add(cfv_r,_idx(cfv_lista_txt,cfv_i));
    cfv_i=_add(cfv_i,Value{(double)1});
  }
  return cfv_r;
return Value{};}

Value cfv_escapar_cpp(Value cfv_s){
  Value cfv_r=Value{std::string("")};
  Value cfv_i=Value{(double)0};
  while(booleano(compara(cfv_i,cfv_longitud(cfv_s),"<"))){
    Value cfv_c=_idx(cfv_s,cfv_i);
    if(booleano(compara(cfv_c,Value{std::string("\\")},"=="))){
      cfv_r=_add(cfv_r,Value{std::string("\\\\")});
    } else if(booleano(compara(cfv_c,Value{std::string("\"")},"=="))){
      cfv_r=_add(cfv_r,Value{std::string("\\\"")});
    } else if(booleano(compara(cfv_c,Value{std::string("\n")},"=="))){
      cfv_r=_add(cfv_r,Value{std::string("\\n")});
    } else if(booleano(compara(cfv_c,Value{std::string("\t")},"=="))){
      cfv_r=_add(cfv_r,Value{std::string("\\t")});
    } else if(booleano(compara(cfv_c,Value{std::string("\\r")},"=="))){
      cfv_r=_add(cfv_r,Value{std::string("\\r")});
    } else{
      cfv_r=_add(cfv_r,cfv_c);
    }
    cfv_i=_add(cfv_i,Value{(double)1});
  }
  return cfv_r;
return Value{};}

Value cfv_safe(Value cfv_nombre){
  Value cfv_r=Value{std::string("cfv_")};
  Value cfv_i=Value{(double)0};
  while(booleano(compara(cfv_i,cfv_longitud(cfv_nombre),"<"))){
    Value cfv_c=_idx(cfv_nombre,cfv_i);
    if(booleano(cfv_es_alfanumerico(cfv_c))){
      cfv_r=_add(cfv_r,cfv_c);
    } else{
      cfv_r=_add(cfv_r,Value{std::string("_")});
    }
    cfv_i=_add(cfv_i,Value{(double)1});
  }
  return cfv_r;
return Value{};}

Value cfv_es_keyword(Value cfv_s){
  Value cfv_kws=_mk_lista({Value{std::string("sea")},Value{std::string("funcion")},Value{std::string("si")},Value{std::string("sino")},Value{std::string("mientras")},Value{std::string("para")},Value{std::string("retornar")},Value{std::string("romper")},Value{std::string("continuar")},Value{std::string("verdadero")},Value{std::string("falso")},Value{std::string("nulo")},Value{std::string("estructura")},Value{std::string("clase")},Value{std::string("intentar")},Value{std::string("capturar")},Value{std::string("lanzar")},Value{std::string("importar")},Value{std::string("usar")},Value{std::string("exportar")},Value{std::string("gpu")},Value{std::string("y")},Value{std::string("o")},Value{std::string("no")},Value{std::string("cualquiera")},Value{std::string("numero")},Value{std::string("texto")},Value{std::string("booleano")},Value{std::string("lista")},Value{std::string("mapa")},Value{std::string("afirmar")},Value{std::string("mostrar")},Value{std::string("en")}});
  Value cfv_i=Value{(double)0};
  while(booleano(compara(cfv_i,cfv_longitud(cfv_kws),"<"))){
    if(booleano(compara(_idx(cfv_kws,cfv_i),cfv_s,"=="))){
      return Value{true};
    }
    cfv_i=_add(cfv_i,Value{(double)1});
  }
  return Value{false};
return Value{};}

Value cfv_mk_tok(Value cfv_tipo,Value cfv_valor,Value cfv_linea){
  Value cfv_t=_mk_mapa_vacio();
  asignar_indice(cfv_t,Value{std::string("k")},cfv_tipo);
  asignar_indice(cfv_t,Value{std::string("v")},cfv_valor);
  asignar_indice(cfv_t,Value{std::string("l")},cfv_linea);
  return cfv_t;
return Value{};}

Value cfv_scan_string_dq(Value cfv_fuente,Value cfv_inicio,Value cfv_ln_in){
  Value cfv_i=_add(cfv_inicio,Value{(double)1});
  Value cfv_n=cfv_longitud(cfv_fuente);
  Value cfv_s=Value{std::string("")};
  Value cfv_ln=cfv_ln_in;
  while(booleano(Value{booleano(compara(cfv_i,cfv_n,"<"))&&booleano(compara(_idx(cfv_fuente,cfv_i),Value{std::string("\"")},"!="))})){
    if(booleano(Value{booleano(compara(_idx(cfv_fuente,cfv_i),Value{std::string("\\")},"=="))&&booleano(compara(_add(cfv_i,Value{(double)1}),cfv_n,"<"))})){
      Value cfv_esc=_idx(cfv_fuente,_add(cfv_i,Value{(double)1}));
      if(booleano(compara(cfv_esc,Value{std::string("n")},"=="))){
        cfv_s=_add(cfv_s,Value{std::string("\n")});
      } else if(booleano(compara(cfv_esc,Value{std::string("t")},"=="))){
        cfv_s=_add(cfv_s,Value{std::string("\t")});
      } else if(booleano(compara(cfv_esc,Value{std::string("r")},"=="))){
        cfv_s=_add(cfv_s,Value{std::string("\\r")});
      } else if(booleano(compara(cfv_esc,Value{std::string("\"")},"=="))){
        cfv_s=_add(cfv_s,Value{std::string("\"")});
      } else if(booleano(compara(cfv_esc,Value{std::string("\\")},"=="))){
        cfv_s=_add(cfv_s,Value{std::string("\\")});
      } else{
        cfv_s=_add(_add(cfv_s,Value{std::string("\\")}),cfv_esc);
      }
      cfv_i=_add(cfv_i,Value{(double)2});
    } else{
      if(booleano(compara(_idx(cfv_fuente,cfv_i),Value{std::string("\n")},"=="))){
        cfv_ln=_add(cfv_ln,Value{(double)1});
      }
      cfv_s=_add(cfv_s,_idx(cfv_fuente,cfv_i));
      cfv_i=_add(cfv_i,Value{(double)1});
    }
  }
  Value cfv_res=_mk_mapa_vacio();
  asignar_indice(cfv_res,Value{std::string("s")},cfv_s);
  asignar_indice(cfv_res,Value{std::string("i")},_add(cfv_i,Value{(double)1}));
  asignar_indice(cfv_res,Value{std::string("ln")},cfv_ln);
  return cfv_res;
return Value{};}

Value cfv_scan_string_sq(Value cfv_fuente,Value cfv_inicio,Value cfv_ln_in){
  Value cfv_i=_add(cfv_inicio,Value{(double)1});
  Value cfv_n=cfv_longitud(cfv_fuente);
  Value cfv_s=Value{std::string("")};
  Value cfv_ln=cfv_ln_in;
  while(booleano(Value{booleano(compara(cfv_i,cfv_n,"<"))&&booleano(compara(_idx(cfv_fuente,cfv_i),Value{std::string("'")},"!="))})){
    if(booleano(Value{booleano(compara(_idx(cfv_fuente,cfv_i),Value{std::string("\\")},"=="))&&booleano(compara(_add(cfv_i,Value{(double)1}),cfv_n,"<"))})){
      Value cfv_esc=_idx(cfv_fuente,_add(cfv_i,Value{(double)1}));
      if(booleano(compara(cfv_esc,Value{std::string("n")},"=="))){
        cfv_s=_add(cfv_s,Value{std::string("\n")});
      } else if(booleano(compara(cfv_esc,Value{std::string("t")},"=="))){
        cfv_s=_add(cfv_s,Value{std::string("\t")});
      } else if(booleano(compara(cfv_esc,Value{std::string("'")},"=="))){
        cfv_s=_add(cfv_s,Value{std::string("'")});
      } else if(booleano(compara(cfv_esc,Value{std::string("\\")},"=="))){
        cfv_s=_add(cfv_s,Value{std::string("\\")});
      } else{
        cfv_s=_add(_add(cfv_s,Value{std::string("\\")}),cfv_esc);
      }
      cfv_i=_add(cfv_i,Value{(double)2});
    } else{
      cfv_s=_add(cfv_s,_idx(cfv_fuente,cfv_i));
      cfv_i=_add(cfv_i,Value{(double)1});
    }
  }
  Value cfv_res=_mk_mapa_vacio();
  asignar_indice(cfv_res,Value{std::string("s")},cfv_s);
  asignar_indice(cfv_res,Value{std::string("i")},_add(cfv_i,Value{(double)1}));
  asignar_indice(cfv_res,Value{std::string("ln")},cfv_ln);
  return cfv_res;
return Value{};}

Value cfv_tokenizar(Value cfv_fuente){
  Value cfv_toks=_mk_lista({});
  Value cfv_i=Value{(double)0};
  Value cfv_ln=Value{(double)1};
  Value cfv_n=cfv_longitud(cfv_fuente);
  while(booleano(compara(cfv_i,cfv_n,"<"))){
    Value cfv_c=_idx(cfv_fuente,cfv_i);
    if(booleano(compara(cfv_c,Value{std::string("\n")},"=="))){
      cfv_ln=_add(cfv_ln,Value{(double)1});
      cfv_i=_add(cfv_i,Value{(double)1});
    } else if(booleano(cfv_es_espacio_blanco(cfv_c))){
      cfv_i=_add(cfv_i,Value{(double)1});
    } else if(booleano(Value{booleano(Value{booleano(compara(cfv_c,Value{std::string("/")},"=="))&&booleano(compara(_add(cfv_i,Value{(double)1}),cfv_n,"<"))})&&booleano(compara(_idx(cfv_fuente,_add(cfv_i,Value{(double)1})),Value{std::string("/")},"=="))})){
      while(booleano(Value{booleano(compara(cfv_i,cfv_n,"<"))&&booleano(compara(_idx(cfv_fuente,cfv_i),Value{std::string("\n")},"!="))})){
        cfv_i=_add(cfv_i,Value{(double)1});
      }
    } else if(booleano(Value{booleano(Value{booleano(compara(cfv_c,Value{std::string("/")},"=="))&&booleano(compara(_add(cfv_i,Value{(double)1}),cfv_n,"<"))})&&booleano(compara(_idx(cfv_fuente,_add(cfv_i,Value{(double)1})),Value{std::string("*")},"=="))})){
      cfv_i=_add(cfv_i,Value{(double)2});
      while(booleano(Value{booleano(compara(_add(cfv_i,Value{(double)1}),cfv_n,"<"))&&booleano(Value{!booleano(Value{booleano(compara(_idx(cfv_fuente,cfv_i),Value{std::string("*")},"=="))&&booleano(compara(_idx(cfv_fuente,_add(cfv_i,Value{(double)1})),Value{std::string("/")},"=="))})})})){
        if(booleano(compara(_idx(cfv_fuente,cfv_i),Value{std::string("\n")},"=="))){
          cfv_ln=_add(cfv_ln,Value{(double)1});
        }
        cfv_i=_add(cfv_i,Value{(double)1});
      }
      cfv_i=_add(cfv_i,Value{(double)2});
    } else if(booleano(compara(cfv_c,Value{std::string("\"")},"=="))){
      Value cfv_r=cfv_scan_string_dq(cfv_fuente,cfv_i,cfv_ln);
      cfv_agregar(cfv_toks,cfv_mk_tok(Value{std::string("TEXT")},_idx(cfv_r,Value{std::string("s")}),_idx(cfv_r,Value{std::string("ln")})));
      cfv_i=_idx(cfv_r,Value{std::string("i")});
      cfv_ln=_idx(cfv_r,Value{std::string("ln")});
    } else if(booleano(compara(cfv_c,Value{std::string("'")},"=="))){
      Value cfv_r=cfv_scan_string_sq(cfv_fuente,cfv_i,cfv_ln);
      cfv_agregar(cfv_toks,cfv_mk_tok(Value{std::string("TEXT")},_idx(cfv_r,Value{std::string("s")}),_idx(cfv_r,Value{std::string("ln")})));
      cfv_i=_idx(cfv_r,Value{std::string("i")});
      cfv_ln=_idx(cfv_r,Value{std::string("ln")});
    } else if(booleano(cfv_es_digito(cfv_c))){
      Value cfv_num=Value{std::string("")};
      while(booleano(Value{booleano(compara(cfv_i,cfv_n,"<"))&&booleano(cfv_es_digito(_idx(cfv_fuente,cfv_i)))})){
        cfv_num=_add(cfv_num,_idx(cfv_fuente,cfv_i));
        cfv_i=_add(cfv_i,Value{(double)1});
      }
      if(booleano(Value{booleano(Value{booleano(Value{booleano(compara(cfv_i,cfv_n,"<"))&&booleano(compara(_idx(cfv_fuente,cfv_i),Value{std::string(".")},"=="))})&&booleano(compara(_add(cfv_i,Value{(double)1}),cfv_n,"<"))})&&booleano(cfv_es_digito(_idx(cfv_fuente,_add(cfv_i,Value{(double)1}))))})){
        cfv_num=_add(cfv_num,Value{std::string(".")});
        cfv_i=_add(cfv_i,Value{(double)1});
        while(booleano(Value{booleano(compara(cfv_i,cfv_n,"<"))&&booleano(cfv_es_digito(_idx(cfv_fuente,cfv_i)))})){
          cfv_num=_add(cfv_num,_idx(cfv_fuente,cfv_i));
          cfv_i=_add(cfv_i,Value{(double)1});
        }
      }
      cfv_agregar(cfv_toks,cfv_mk_tok(Value{std::string("NUM")},cfv_num,cfv_ln));
    } else if(booleano(cfv_es_letra(cfv_c))){
      Value cfv_id=Value{std::string("")};
      while(booleano(Value{booleano(compara(cfv_i,cfv_n,"<"))&&booleano(cfv_es_alfanumerico(_idx(cfv_fuente,cfv_i)))})){
        cfv_id=_add(cfv_id,_idx(cfv_fuente,cfv_i));
        cfv_i=_add(cfv_i,Value{(double)1});
      }
      if(booleano(cfv_es_keyword(cfv_id))){
        cfv_agregar(cfv_toks,cfv_mk_tok(Value{std::string("KEY")},cfv_id,cfv_ln));
      } else{
        cfv_agregar(cfv_toks,cfv_mk_tok(Value{std::string("ID")},cfv_id,cfv_ln));
      }
    } else if(booleano(Value{booleano(compara(_add(cfv_i,Value{(double)1}),cfv_n,"<"))&&booleano(cfv_contiene_char(Value{std::string("=!<>+-%*/&|")},cfv_c))})){
      Value cfv_dos=_add(cfv_c,_idx(cfv_fuente,_add(cfv_i,Value{(double)1})));
      if(booleano(Value{booleano(Value{booleano(Value{booleano(Value{booleano(Value{booleano(Value{booleano(Value{booleano(Value{booleano(Value{booleano(Value{booleano(Value{booleano(Value{booleano(Value{booleano(compara(cfv_dos,Value{std::string("==")},"=="))||booleano(compara(cfv_dos,Value{std::string("!=")},"=="))})||booleano(compara(cfv_dos,Value{std::string("<=")},"=="))})||booleano(compara(cfv_dos,Value{std::string(">=")},"=="))})||booleano(compara(cfv_dos,Value{std::string("+=")},"=="))})||booleano(compara(cfv_dos,Value{std::string("-=")},"=="))})||booleano(compara(cfv_dos,Value{std::string("*=")},"=="))})||booleano(compara(cfv_dos,Value{std::string("/=")},"=="))})||booleano(compara(cfv_dos,Value{std::string("%=")},"=="))})||booleano(compara(cfv_dos,Value{std::string("&&")},"=="))})||booleano(compara(cfv_dos,Value{std::string("||")},"=="))})||booleano(compara(cfv_dos,Value{std::string("->")},"=="))})||booleano(compara(cfv_dos,Value{std::string("::")},"=="))})||booleano(compara(cfv_dos,Value{std::string("**")},"=="))})){
        cfv_agregar(cfv_toks,cfv_mk_tok(Value{std::string("OP")},cfv_dos,cfv_ln));
        cfv_i=_add(cfv_i,Value{(double)2});
      } else if(booleano(cfv_contiene_char(Value{std::string("+-*/%=<>!&|^~")},cfv_c))){
        cfv_agregar(cfv_toks,cfv_mk_tok(Value{std::string("OP")},cfv_c,cfv_ln));
        cfv_i=_add(cfv_i,Value{(double)1});
      } else{
        cfv_i=_add(cfv_i,Value{(double)1});
      }
    } else if(booleano(cfv_contiene_char(Value{std::string("+-*/%=<>!&|^~")},cfv_c))){
      cfv_agregar(cfv_toks,cfv_mk_tok(Value{std::string("OP")},cfv_c,cfv_ln));
      cfv_i=_add(cfv_i,Value{(double)1});
    } else if(booleano(cfv_contiene_char(Value{std::string("(){}[],;:.")},cfv_c))){
      cfv_agregar(cfv_toks,cfv_mk_tok(Value{std::string("DELIM")},cfv_c,cfv_ln));
      cfv_i=_add(cfv_i,Value{(double)1});
    } else{
      cfv_i=_add(cfv_i,Value{(double)1});
    }
  }
  cfv_agregar(cfv_toks,cfv_mk_tok(Value{std::string("EOF")},Value{std::string("")},cfv_ln));
  return cfv_toks;
return Value{};}

Value cfv_tok_actual(Value cfv_st){
  return _idx(_idx(cfv_st,Value{std::string("toks")}),_idx(cfv_st,Value{std::string("pos")}));
return Value{};}

Value cfv_tok_peek(Value cfv_st,Value cfv_offset){
  Value cfv_idx=_add(_idx(cfv_st,Value{std::string("pos")}),cfv_offset);
  if(booleano(compara(cfv_idx,cfv_longitud(_idx(cfv_st,Value{std::string("toks")})),"<"))){
    return _idx(_idx(cfv_st,Value{std::string("toks")}),cfv_idx);
  }
  return _idx(_idx(cfv_st,Value{std::string("toks")}),_sub(cfv_longitud(_idx(cfv_st,Value{std::string("toks")})),Value{(double)1}));
return Value{};}

Value cfv_tok_es(Value cfv_st,Value cfv_tipo,Value cfv_valor){
  Value cfv_t=cfv_tok_actual(cfv_st);
  return Value{booleano(compara(_idx(cfv_t,Value{std::string("k")}),cfv_tipo,"=="))&&booleano(compara(_idx(cfv_t,Value{std::string("v")}),cfv_valor,"=="))};
return Value{};}

Value cfv_tok_es_tipo(Value cfv_st,Value cfv_tipo){
  return compara(_idx(cfv_tok_actual(cfv_st),Value{std::string("k")}),cfv_tipo,"==");
return Value{};}

Value cfv_tok_es_key(Value cfv_st,Value cfv_v){
  Value cfv_t=cfv_tok_actual(cfv_st);
  return Value{booleano(compara(_idx(cfv_t,Value{std::string("k")}),Value{std::string("KEY")},"=="))&&booleano(compara(_idx(cfv_t,Value{std::string("v")}),cfv_v,"=="))};
return Value{};}

Value cfv_tok_es_delim(Value cfv_st,Value cfv_v){
  Value cfv_t=cfv_tok_actual(cfv_st);
  return Value{booleano(compara(_idx(cfv_t,Value{std::string("k")}),Value{std::string("DELIM")},"=="))&&booleano(compara(_idx(cfv_t,Value{std::string("v")}),cfv_v,"=="))};
return Value{};}

Value cfv_tok_es_op(Value cfv_st,Value cfv_v){
  Value cfv_t=cfv_tok_actual(cfv_st);
  return Value{booleano(compara(_idx(cfv_t,Value{std::string("k")}),Value{std::string("OP")},"=="))&&booleano(compara(_idx(cfv_t,Value{std::string("v")}),cfv_v,"=="))};
return Value{};}

Value cfv_avanzar(Value cfv_st){
  Value cfv_t=_idx(_idx(cfv_st,Value{std::string("toks")}),_idx(cfv_st,Value{std::string("pos")}));
  if(booleano(compara(_add(_idx(cfv_st,Value{std::string("pos")}),Value{(double)1}),cfv_longitud(_idx(cfv_st,Value{std::string("toks")})),"<"))){
    asignar_indice(cfv_st,Value{std::string("pos")},_add(_idx(cfv_st,Value{std::string("pos")}),Value{(double)1}));
  }
  return cfv_t;
return Value{};}

Value cfv_consumir(Value cfv_st,Value cfv_tipo,Value cfv_valor){
  Value cfv_t=cfv_tok_actual(cfv_st);
  if(booleano(Value{booleano(compara(_idx(cfv_t,Value{std::string("k")}),cfv_tipo,"!="))||booleano(compara(_idx(cfv_t,Value{std::string("v")}),cfv_valor,"!="))})){
    cfv_afirmar(Value{false},_add(_add(_add(_add(_add(_add(Value{std::string("Error línea ")},cfv_num_a_txt(_idx(cfv_t,Value{std::string("l")}))),Value{std::string(": se esperaba '")}),cfv_valor),Value{std::string("' pero se encontró '")}),_idx(cfv_t,Value{std::string("v")})),Value{std::string("'")}));
  }
  return cfv_avanzar(cfv_st);
return Value{};}

Value cfv_consumir_tipo(Value cfv_st,Value cfv_tipo){
  Value cfv_t=cfv_tok_actual(cfv_st);
  if(booleano(Value{booleano(compara(cfv_tipo,Value{std::string("ID")},"=="))&&booleano(compara(_idx(cfv_t,Value{std::string("k")}),Value{std::string("KEY")},"=="))})){
    return cfv_avanzar(cfv_st);
  }
  if(booleano(compara(_idx(cfv_t,Value{std::string("k")}),cfv_tipo,"!="))){
    cfv_afirmar(Value{false},_add(_add(_add(_add(_add(_add(Value{std::string("Error línea ")},cfv_num_a_txt(_idx(cfv_t,Value{std::string("l")}))),Value{std::string(": se esperaba ")}),cfv_tipo),Value{std::string(" pero se encontró '")}),_idx(cfv_t,Value{std::string("v")})),Value{std::string("'")}));
  }
  return cfv_avanzar(cfv_st);
return Value{};}

Value cfv_parse_tipo(Value cfv_st){
  Value cfv_t=cfv_tok_actual(cfv_st);
  Value cfv_tipos_base=_mk_lista({Value{std::string("cualquiera")},Value{std::string("numero")},Value{std::string("texto")},Value{std::string("booleano")},Value{std::string("lista")},Value{std::string("mapa")},Value{std::string("nulo")}});
  Value cfv_i=Value{(double)0};
  while(booleano(compara(cfv_i,cfv_longitud(cfv_tipos_base),"<"))){
    if(booleano(Value{booleano(compara(_idx(cfv_t,Value{std::string("k")}),Value{std::string("KEY")},"=="))&&booleano(compara(_idx(cfv_t,Value{std::string("v")}),_idx(cfv_tipos_base,cfv_i),"=="))})){
      cfv_avanzar(cfv_st);
      if(booleano(cfv_tok_es_op(cfv_st,Value{std::string("<")}))){
        cfv_avanzar(cfv_st);
        cfv_parse_tipo(cfv_st);
        cfv_consumir(cfv_st,Value{std::string("OP")},Value{std::string(">")});
      }
      return _idx(cfv_tipos_base,cfv_i);
    }
    cfv_i=_add(cfv_i,Value{(double)1});
  }
  if(booleano(compara(_idx(cfv_t,Value{std::string("k")}),Value{std::string("ID")},"=="))){
    Value cfv_nombre=_idx(cfv_t,Value{std::string("v")});
    cfv_avanzar(cfv_st);
    return cfv_nombre;
  }
  return Value{std::string("cualquiera")};
return Value{};}

Value cfv_parse_expr(Value cfv_st){
  return cfv_parse_asignacion(cfv_st);
return Value{};}

Value cfv_parse_asignacion(Value cfv_st){
  Value cfv_izq=cfv_parse_or(cfv_st);
  Value cfv_t=cfv_tok_actual(cfv_st);
  if(booleano(Value{booleano(compara(_idx(cfv_t,Value{std::string("k")}),Value{std::string("OP")},"=="))&&booleano(Value{booleano(Value{booleano(Value{booleano(Value{booleano(Value{booleano(compara(_idx(cfv_t,Value{std::string("v")}),Value{std::string("=")},"=="))||booleano(compara(_idx(cfv_t,Value{std::string("v")}),Value{std::string("+=")},"=="))})||booleano(compara(_idx(cfv_t,Value{std::string("v")}),Value{std::string("-=")},"=="))})||booleano(compara(_idx(cfv_t,Value{std::string("v")}),Value{std::string("*=")},"=="))})||booleano(compara(_idx(cfv_t,Value{std::string("v")}),Value{std::string("/=")},"=="))})||booleano(compara(_idx(cfv_t,Value{std::string("v")}),Value{std::string("%=")},"=="))})})){
    Value cfv_op=_idx(cfv_t,Value{std::string("v")});
    cfv_avanzar(cfv_st);
    Value cfv_der=cfv_parse_asignacion(cfv_st);
    Value cfv_nodo=_mk_mapa_vacio();
    asignar_indice(cfv_nodo,Value{std::string("k")},Value{std::string("asgn")});
    asignar_indice(cfv_nodo,Value{std::string("op")},cfv_op);
    asignar_indice(cfv_nodo,Value{std::string("l")},cfv_izq);
    asignar_indice(cfv_nodo,Value{std::string("r")},cfv_der);
    return cfv_nodo;
  }
  return cfv_izq;
return Value{};}

Value cfv_parse_or(Value cfv_st){
  Value cfv_izq=cfv_parse_and(cfv_st);
  while(booleano(Value{booleano(cfv_tok_es_key(cfv_st,Value{std::string("o")}))||booleano(cfv_tok_es_op(cfv_st,Value{std::string("||")}))})){
    cfv_avanzar(cfv_st);
    Value cfv_der=cfv_parse_and(cfv_st);
    Value cfv_nodo=_mk_mapa_vacio();
    asignar_indice(cfv_nodo,Value{std::string("k")},Value{std::string("bin")});
    asignar_indice(cfv_nodo,Value{std::string("op")},Value{std::string("||")});
    asignar_indice(cfv_nodo,Value{std::string("l")},cfv_izq);
    asignar_indice(cfv_nodo,Value{std::string("r")},cfv_der);
    cfv_izq=cfv_nodo;
  }
  return cfv_izq;
return Value{};}

Value cfv_parse_and(Value cfv_st){
  Value cfv_izq=cfv_parse_igual(cfv_st);
  while(booleano(Value{booleano(cfv_tok_es_key(cfv_st,Value{std::string("y")}))||booleano(cfv_tok_es_op(cfv_st,Value{std::string("&&")}))})){
    cfv_avanzar(cfv_st);
    Value cfv_der=cfv_parse_igual(cfv_st);
    Value cfv_nodo=_mk_mapa_vacio();
    asignar_indice(cfv_nodo,Value{std::string("k")},Value{std::string("bin")});
    asignar_indice(cfv_nodo,Value{std::string("op")},Value{std::string("&&")});
    asignar_indice(cfv_nodo,Value{std::string("l")},cfv_izq);
    asignar_indice(cfv_nodo,Value{std::string("r")},cfv_der);
    cfv_izq=cfv_nodo;
  }
  return cfv_izq;
return Value{};}

Value cfv_parse_igual(Value cfv_st){
  Value cfv_izq=cfv_parse_cmp(cfv_st);
  while(booleano(Value{booleano(cfv_tok_es_op(cfv_st,Value{std::string("==")}))||booleano(cfv_tok_es_op(cfv_st,Value{std::string("!=")}))})){
    Value cfv_op=_idx(cfv_tok_actual(cfv_st),Value{std::string("v")});
    cfv_avanzar(cfv_st);
    Value cfv_der=cfv_parse_cmp(cfv_st);
    Value cfv_nodo=_mk_mapa_vacio();
    asignar_indice(cfv_nodo,Value{std::string("k")},Value{std::string("bin")});
    asignar_indice(cfv_nodo,Value{std::string("op")},cfv_op);
    asignar_indice(cfv_nodo,Value{std::string("l")},cfv_izq);
    asignar_indice(cfv_nodo,Value{std::string("r")},cfv_der);
    cfv_izq=cfv_nodo;
  }
  return cfv_izq;
return Value{};}

Value cfv_parse_cmp(Value cfv_st){
  Value cfv_izq=cfv_parse_suma(cfv_st);
  while(booleano(Value{booleano(Value{booleano(Value{booleano(cfv_tok_es_op(cfv_st,Value{std::string("<")}))||booleano(cfv_tok_es_op(cfv_st,Value{std::string(">")}))})||booleano(cfv_tok_es_op(cfv_st,Value{std::string("<=")}))})||booleano(cfv_tok_es_op(cfv_st,Value{std::string(">=")}))})){
    Value cfv_op=_idx(cfv_tok_actual(cfv_st),Value{std::string("v")});
    cfv_avanzar(cfv_st);
    Value cfv_der=cfv_parse_suma(cfv_st);
    Value cfv_nodo=_mk_mapa_vacio();
    asignar_indice(cfv_nodo,Value{std::string("k")},Value{std::string("bin")});
    asignar_indice(cfv_nodo,Value{std::string("op")},cfv_op);
    asignar_indice(cfv_nodo,Value{std::string("l")},cfv_izq);
    asignar_indice(cfv_nodo,Value{std::string("r")},cfv_der);
    cfv_izq=cfv_nodo;
  }
  return cfv_izq;
return Value{};}

Value cfv_parse_suma(Value cfv_st){
  Value cfv_izq=cfv_parse_mul(cfv_st);
  while(booleano(Value{booleano(cfv_tok_es_op(cfv_st,Value{std::string("+")}))||booleano(cfv_tok_es_op(cfv_st,Value{std::string("-")}))})){
    Value cfv_op=_idx(cfv_tok_actual(cfv_st),Value{std::string("v")});
    cfv_avanzar(cfv_st);
    Value cfv_der=cfv_parse_mul(cfv_st);
    Value cfv_nodo=_mk_mapa_vacio();
    asignar_indice(cfv_nodo,Value{std::string("k")},Value{std::string("bin")});
    asignar_indice(cfv_nodo,Value{std::string("op")},cfv_op);
    asignar_indice(cfv_nodo,Value{std::string("l")},cfv_izq);
    asignar_indice(cfv_nodo,Value{std::string("r")},cfv_der);
    cfv_izq=cfv_nodo;
  }
  return cfv_izq;
return Value{};}

Value cfv_parse_mul(Value cfv_st){
  Value cfv_izq=cfv_parse_unario(cfv_st);
  while(booleano(Value{booleano(Value{booleano(cfv_tok_es_op(cfv_st,Value{std::string("*")}))||booleano(cfv_tok_es_op(cfv_st,Value{std::string("/")}))})||booleano(cfv_tok_es_op(cfv_st,Value{std::string("%")}))})){
    Value cfv_op=_idx(cfv_tok_actual(cfv_st),Value{std::string("v")});
    cfv_avanzar(cfv_st);
    Value cfv_der=cfv_parse_unario(cfv_st);
    Value cfv_nodo=_mk_mapa_vacio();
    asignar_indice(cfv_nodo,Value{std::string("k")},Value{std::string("bin")});
    asignar_indice(cfv_nodo,Value{std::string("op")},cfv_op);
    asignar_indice(cfv_nodo,Value{std::string("l")},cfv_izq);
    asignar_indice(cfv_nodo,Value{std::string("r")},cfv_der);
    cfv_izq=cfv_nodo;
  }
  return cfv_izq;
return Value{};}

Value cfv_parse_unario(Value cfv_st){
  if(booleano(Value{booleano(cfv_tok_es_key(cfv_st,Value{std::string("no")}))||booleano(cfv_tok_es_op(cfv_st,Value{std::string("!")}))})){
    cfv_avanzar(cfv_st);
    Value cfv_v=cfv_parse_unario(cfv_st);
    Value cfv_nodo=_mk_mapa_vacio();
    asignar_indice(cfv_nodo,Value{std::string("k")},Value{std::string("un")});
    asignar_indice(cfv_nodo,Value{std::string("op")},Value{std::string("!")});
    asignar_indice(cfv_nodo,Value{std::string("v")},cfv_v);
    return cfv_nodo;
  }
  if(booleano(cfv_tok_es_op(cfv_st,Value{std::string("-")}))){
    cfv_avanzar(cfv_st);
    Value cfv_v=cfv_parse_unario(cfv_st);
    Value cfv_nodo=_mk_mapa_vacio();
    asignar_indice(cfv_nodo,Value{std::string("k")},Value{std::string("un")});
    asignar_indice(cfv_nodo,Value{std::string("op")},Value{std::string("-")});
    asignar_indice(cfv_nodo,Value{std::string("v")},cfv_v);
    return cfv_nodo;
  }
  return cfv_parse_postfijo(cfv_st);
return Value{};}

Value cfv_parse_postfijo(Value cfv_st){
  Value cfv_base=cfv_parse_primario(cfv_st);
  Value cfv__pf_cont=Value{(double)1};
  while(booleano(compara(cfv__pf_cont,Value{(double)0},">"))){
    if(booleano(cfv_tok_es_delim(cfv_st,Value{std::string("[")}))){
      cfv_avanzar(cfv_st);
      Value cfv_idx=cfv_parse_expr(cfv_st);
      cfv_consumir(cfv_st,Value{std::string("DELIM")},Value{std::string("]")});
      Value cfv_nodo=_mk_mapa_vacio();
      asignar_indice(cfv_nodo,Value{std::string("k")},Value{std::string("idx")});
      asignar_indice(cfv_nodo,Value{std::string("o")},cfv_base);
      asignar_indice(cfv_nodo,Value{std::string("i")},cfv_idx);
      cfv_base=cfv_nodo;
    } else if(booleano(cfv_tok_es_delim(cfv_st,Value{std::string(".")}))){
      cfv_avanzar(cfv_st);
      Value cfv_campo=cfv_consumir_tipo(cfv_st,Value{std::string("ID")});
      if(booleano(cfv_tok_es_delim(cfv_st,Value{std::string("(")}))){
        cfv_avanzar(cfv_st);
        Value cfv_args=_mk_lista({});
        if(booleano(Value{!booleano(cfv_tok_es_delim(cfv_st,Value{std::string(")")}))})){
          cfv_agregar(cfv_args,cfv_parse_expr(cfv_st));
          while(booleano(cfv_tok_es_delim(cfv_st,Value{std::string(",")}))){
            cfv_avanzar(cfv_st);
            cfv_agregar(cfv_args,cfv_parse_expr(cfv_st));
          }
        }
        cfv_consumir(cfv_st,Value{std::string("DELIM")},Value{std::string(")")});
        Value cfv_nodo=_mk_mapa_vacio();
        asignar_indice(cfv_nodo,Value{std::string("k")},Value{std::string("metodo")});
        asignar_indice(cfv_nodo,Value{std::string("o")},cfv_base);
        asignar_indice(cfv_nodo,Value{std::string("m")},_idx(cfv_campo,Value{std::string("v")}));
        asignar_indice(cfv_nodo,Value{std::string("a")},cfv_args);
        cfv_base=cfv_nodo;
      } else{
        Value cfv_nodo=_mk_mapa_vacio();
        asignar_indice(cfv_nodo,Value{std::string("k")},Value{std::string("dot")});
        asignar_indice(cfv_nodo,Value{std::string("o")},cfv_base);
        asignar_indice(cfv_nodo,Value{std::string("f")},_idx(cfv_campo,Value{std::string("v")}));
        cfv_base=cfv_nodo;
      }
    } else{
      cfv__pf_cont=Value{(double)0};
    }
  }
  return cfv_base;
return Value{};}

Value cfv_parse_args(Value cfv_st){
  Value cfv_args=_mk_lista({});
  if(booleano(Value{!booleano(cfv_tok_es_delim(cfv_st,Value{std::string(")")}))})){
    cfv_agregar(cfv_args,cfv_parse_expr(cfv_st));
    Value cfv__args_sigue=Value{booleano(cfv_tok_es_delim(cfv_st,Value{std::string(",")}))&&booleano(Value{!booleano(cfv_tok_es_delim(cfv_st,Value{std::string(")")}))})};
    while(booleano(cfv__args_sigue)){
      cfv_avanzar(cfv_st);
      if(booleano(Value{!booleano(cfv_tok_es_delim(cfv_st,Value{std::string(")")}))})){
        cfv_agregar(cfv_args,cfv_parse_expr(cfv_st));
      }
      cfv__args_sigue=Value{booleano(cfv_tok_es_delim(cfv_st,Value{std::string(",")}))&&booleano(Value{!booleano(cfv_tok_es_delim(cfv_st,Value{std::string(")")}))})};
    }
  }
  return cfv_args;
return Value{};}

Value cfv_parse_primario(Value cfv_st){
  Value cfv_t=cfv_tok_actual(cfv_st);
  if(booleano(compara(_idx(cfv_t,Value{std::string("k")}),Value{std::string("NUM")},"=="))){
    cfv_avanzar(cfv_st);
    Value cfv_nodo=_mk_mapa_vacio();
    asignar_indice(cfv_nodo,Value{std::string("k")},Value{std::string("num")});
    asignar_indice(cfv_nodo,Value{std::string("v")},cfv_a_numero(_idx(cfv_t,Value{std::string("v")})));
    return cfv_nodo;
  }
  if(booleano(compara(_idx(cfv_t,Value{std::string("k")}),Value{std::string("TEXT")},"=="))){
    cfv_avanzar(cfv_st);
    Value cfv_nodo=_mk_mapa_vacio();
    asignar_indice(cfv_nodo,Value{std::string("k")},Value{std::string("txt")});
    asignar_indice(cfv_nodo,Value{std::string("v")},_idx(cfv_t,Value{std::string("v")}));
    return cfv_nodo;
  }
  if(booleano(Value{booleano(compara(_idx(cfv_t,Value{std::string("k")}),Value{std::string("KEY")},"=="))&&booleano(compara(_idx(cfv_t,Value{std::string("v")}),Value{std::string("verdadero")},"=="))})){
    cfv_avanzar(cfv_st);
    Value cfv_nodo=_mk_mapa_vacio();
    asignar_indice(cfv_nodo,Value{std::string("k")},Value{std::string("bool")});
    asignar_indice(cfv_nodo,Value{std::string("v")},Value{true});
    return cfv_nodo;
  }
  if(booleano(Value{booleano(compara(_idx(cfv_t,Value{std::string("k")}),Value{std::string("KEY")},"=="))&&booleano(compara(_idx(cfv_t,Value{std::string("v")}),Value{std::string("falso")},"=="))})){
    cfv_avanzar(cfv_st);
    Value cfv_nodo=_mk_mapa_vacio();
    asignar_indice(cfv_nodo,Value{std::string("k")},Value{std::string("bool")});
    asignar_indice(cfv_nodo,Value{std::string("v")},Value{false});
    return cfv_nodo;
  }
  if(booleano(Value{booleano(compara(_idx(cfv_t,Value{std::string("k")}),Value{std::string("KEY")},"=="))&&booleano(compara(_idx(cfv_t,Value{std::string("v")}),Value{std::string("nulo")},"=="))})){
    cfv_avanzar(cfv_st);
    Value cfv_nodo=_mk_mapa_vacio();
    asignar_indice(cfv_nodo,Value{std::string("k")},Value{std::string("nul")});
    return cfv_nodo;
  }
  if(booleano(Value{booleano(compara(_idx(cfv_t,Value{std::string("k")}),Value{std::string("DELIM")},"=="))&&booleano(compara(_idx(cfv_t,Value{std::string("v")}),Value{std::string("[")},"=="))})){
    cfv_avanzar(cfv_st);
    Value cfv_items=_mk_lista({});
    if(booleano(Value{!booleano(cfv_tok_es_delim(cfv_st,Value{std::string("]")}))})){
      cfv_agregar(cfv_items,cfv_parse_expr(cfv_st));
      Value cfv__lst_sigue=Value{booleano(cfv_tok_es_delim(cfv_st,Value{std::string(",")}))&&booleano(Value{!booleano(cfv_tok_es_delim(cfv_st,Value{std::string("]")}))})};
      while(booleano(cfv__lst_sigue)){
        cfv_avanzar(cfv_st);
        if(booleano(Value{!booleano(cfv_tok_es_delim(cfv_st,Value{std::string("]")}))})){
          cfv_agregar(cfv_items,cfv_parse_expr(cfv_st));
        }
        cfv__lst_sigue=Value{booleano(cfv_tok_es_delim(cfv_st,Value{std::string(",")}))&&booleano(Value{!booleano(cfv_tok_es_delim(cfv_st,Value{std::string("]")}))})};
      }
    }
    cfv_consumir(cfv_st,Value{std::string("DELIM")},Value{std::string("]")});
    Value cfv_nodo=_mk_mapa_vacio();
    asignar_indice(cfv_nodo,Value{std::string("k")},Value{std::string("lst")});
    asignar_indice(cfv_nodo,Value{std::string("v")},cfv_items);
    return cfv_nodo;
  }
  if(booleano(Value{booleano(compara(_idx(cfv_t,Value{std::string("k")}),Value{std::string("DELIM")},"=="))&&booleano(compara(_idx(cfv_t,Value{std::string("v")}),Value{std::string("{")},"=="))})){
    Value cfv_siguiente=cfv_tok_peek(cfv_st,Value{(double)1});
    Value cfv_es_mapa=Value{false};
    if(booleano(Value{booleano(compara(_idx(cfv_siguiente,Value{std::string("k")}),Value{std::string("TEXT")},"=="))||booleano(compara(_idx(cfv_siguiente,Value{std::string("k")}),Value{std::string("ID")},"=="))})){
      Value cfv_despues=cfv_tok_peek(cfv_st,Value{(double)2});
      if(booleano(Value{booleano(compara(_idx(cfv_despues,Value{std::string("k")}),Value{std::string("DELIM")},"=="))&&booleano(compara(_idx(cfv_despues,Value{std::string("v")}),Value{std::string(":")},"=="))})){
        cfv_es_mapa=Value{true};
      }
    }
    if(booleano(Value{booleano(compara(_idx(cfv_tok_peek(cfv_st,Value{(double)1}),Value{std::string("k")}),Value{std::string("DELIM")},"=="))&&booleano(compara(_idx(cfv_tok_peek(cfv_st,Value{(double)1}),Value{std::string("v")}),Value{std::string("}")},"=="))})){
      cfv_avanzar(cfv_st);
      cfv_avanzar(cfv_st);
      Value cfv_nodo=_mk_mapa_vacio();
      asignar_indice(cfv_nodo,Value{std::string("k")},Value{std::string("mapa_lit")});
      asignar_indice(cfv_nodo,Value{std::string("claves")},_mk_lista({}));
      asignar_indice(cfv_nodo,Value{std::string("vals")},_mk_lista({}));
      return cfv_nodo;
    }
    if(booleano(cfv_es_mapa)){
      cfv_avanzar(cfv_st);
      Value cfv_claves=_mk_lista({});
      Value cfv_vals=_mk_lista({});
      while(booleano(Value{!booleano(cfv_tok_es_delim(cfv_st,Value{std::string("}")}))})){
        Value cfv_clave=cfv_tok_actual(cfv_st);
        cfv_avanzar(cfv_st);
        cfv_consumir(cfv_st,Value{std::string("DELIM")},Value{std::string(":")});
        Value cfv_val=cfv_parse_expr(cfv_st);
        cfv_agregar(cfv_claves,_idx(cfv_clave,Value{std::string("v")}));
        cfv_agregar(cfv_vals,cfv_val);
        if(booleano(cfv_tok_es_delim(cfv_st,Value{std::string(",")}))){
          cfv_avanzar(cfv_st);
        }
      }
      cfv_consumir(cfv_st,Value{std::string("DELIM")},Value{std::string("}")});
      Value cfv_nodo=_mk_mapa_vacio();
      asignar_indice(cfv_nodo,Value{std::string("k")},Value{std::string("mapa_lit")});
      asignar_indice(cfv_nodo,Value{std::string("claves")},cfv_claves);
      asignar_indice(cfv_nodo,Value{std::string("vals")},cfv_vals);
      return cfv_nodo;
    }
  }
  if(booleano(Value{booleano(compara(_idx(cfv_t,Value{std::string("k")}),Value{std::string("DELIM")},"=="))&&booleano(compara(_idx(cfv_t,Value{std::string("v")}),Value{std::string("(")},"=="))})){
    cfv_avanzar(cfv_st);
    Value cfv_e=cfv_parse_expr(cfv_st);
    cfv_consumir(cfv_st,Value{std::string("DELIM")},Value{std::string(")")});
    return cfv_e;
  }
  if(booleano(Value{booleano(compara(_idx(cfv_t,Value{std::string("k")}),Value{std::string("ID")},"=="))||booleano(compara(_idx(cfv_t,Value{std::string("k")}),Value{std::string("KEY")},"=="))})){
    Value cfv_nombre=_idx(cfv_t,Value{std::string("v")});
    cfv_avanzar(cfv_st);
    if(booleano(cfv_tok_es_delim(cfv_st,Value{std::string("(")}))){
      cfv_avanzar(cfv_st);
      Value cfv_args=cfv_parse_args(cfv_st);
      cfv_consumir(cfv_st,Value{std::string("DELIM")},Value{std::string(")")});
      Value cfv_nodo=_mk_mapa_vacio();
      asignar_indice(cfv_nodo,Value{std::string("k")},Value{std::string("call")});
      asignar_indice(cfv_nodo,Value{std::string("n")},cfv_nombre);
      asignar_indice(cfv_nodo,Value{std::string("a")},cfv_args);
      return cfv_nodo;
    }
    Value cfv_nodo=_mk_mapa_vacio();
    asignar_indice(cfv_nodo,Value{std::string("k")},Value{std::string("id")});
    asignar_indice(cfv_nodo,Value{std::string("v")},cfv_nombre);
    return cfv_nodo;
  }
  Value cfv_nodo=_mk_mapa_vacio();
  asignar_indice(cfv_nodo,Value{std::string("k")},Value{std::string("nul")});
  return cfv_nodo;
return Value{};}

Value cfv_parse_bloque(Value cfv_st){
  cfv_consumir(cfv_st,Value{std::string("DELIM")},Value{std::string("{")});
  Value cfv_stmts=_mk_lista({});
  while(booleano(Value{booleano(Value{!booleano(cfv_tok_es_delim(cfv_st,Value{std::string("}")}))})&&booleano(compara(_idx(cfv_tok_actual(cfv_st),Value{std::string("k")}),Value{std::string("EOF")},"!="))})){
    cfv_agregar(cfv_stmts,cfv_parse_stmt(cfv_st));
  }
  cfv_consumir(cfv_st,Value{std::string("DELIM")},Value{std::string("}")});
  return cfv_stmts;
return Value{};}

Value cfv_parse_stmt(Value cfv_st){
  Value cfv_t=cfv_tok_actual(cfv_st);
  if(booleano(Value{booleano(compara(_idx(cfv_t,Value{std::string("k")}),Value{std::string("KEY")},"=="))&&booleano(compara(_idx(cfv_t,Value{std::string("v")}),Value{std::string("sea")},"=="))})){
    cfv_avanzar(cfv_st);
    Value cfv_nombre=cfv_consumir_tipo(cfv_st,Value{std::string("ID")});
    Value cfv_tipo_dato=Value{std::string("cualquiera")};
    if(booleano(cfv_tok_es_delim(cfv_st,Value{std::string(":")}))){
      cfv_avanzar(cfv_st);
      cfv_tipo_dato=cfv_parse_tipo(cfv_st);
    }
    Value cfv_val=Value{};
    if(booleano(cfv_tok_es_op(cfv_st,Value{std::string("=")}))){
      cfv_avanzar(cfv_st);
      cfv_val=cfv_parse_expr(cfv_st);
    }
    if(booleano(cfv_tok_es_delim(cfv_st,Value{std::string(";")}))){
      cfv_avanzar(cfv_st);
    }
    Value cfv_nodo=_mk_mapa_vacio();
    asignar_indice(cfv_nodo,Value{std::string("k")},Value{std::string("var")});
    asignar_indice(cfv_nodo,Value{std::string("n")},_idx(cfv_nombre,Value{std::string("v")}));
    asignar_indice(cfv_nodo,Value{std::string("t")},cfv_tipo_dato);
    asignar_indice(cfv_nodo,Value{std::string("v")},cfv_val);
    return cfv_nodo;
  }
  if(booleano(Value{booleano(compara(_idx(cfv_t,Value{std::string("k")}),Value{std::string("KEY")},"=="))&&booleano(compara(_idx(cfv_t,Value{std::string("v")}),Value{std::string("funcion")},"=="))})){
    cfv_avanzar(cfv_st);
    Value cfv_nombre=cfv_consumir_tipo(cfv_st,Value{std::string("ID")});
    cfv_consumir(cfv_st,Value{std::string("DELIM")},Value{std::string("(")});
    Value cfv_params=_mk_lista({});
    Value cfv_param_tipos=_mk_lista({});
    while(booleano(Value{!booleano(cfv_tok_es_delim(cfv_st,Value{std::string(")")}))})){
      Value cfv_pnombre=cfv_consumir_tipo(cfv_st,Value{std::string("ID")});
      Value cfv_ptipo=Value{std::string("cualquiera")};
      if(booleano(cfv_tok_es_delim(cfv_st,Value{std::string(":")}))){
        cfv_avanzar(cfv_st);
        cfv_ptipo=cfv_parse_tipo(cfv_st);
      }
      cfv_agregar(cfv_params,_idx(cfv_pnombre,Value{std::string("v")}));
      cfv_agregar(cfv_param_tipos,cfv_ptipo);
      if(booleano(cfv_tok_es_delim(cfv_st,Value{std::string(",")}))){
        cfv_avanzar(cfv_st);
      }
    }
    cfv_consumir(cfv_st,Value{std::string("DELIM")},Value{std::string(")")});
    Value cfv_ret_tipo=Value{std::string("cualquiera")};
    if(booleano(cfv_tok_es_delim(cfv_st,Value{std::string(":")}))){
      cfv_avanzar(cfv_st);
      cfv_ret_tipo=cfv_parse_tipo(cfv_st);
    }
    Value cfv_cuerpo=cfv_parse_bloque(cfv_st);
    Value cfv_nodo=_mk_mapa_vacio();
    asignar_indice(cfv_nodo,Value{std::string("k")},Value{std::string("fun")});
    asignar_indice(cfv_nodo,Value{std::string("n")},_idx(cfv_nombre,Value{std::string("v")}));
    asignar_indice(cfv_nodo,Value{std::string("p")},cfv_params);
    asignar_indice(cfv_nodo,Value{std::string("pt")},cfv_param_tipos);
    asignar_indice(cfv_nodo,Value{std::string("rt")},cfv_ret_tipo);
    asignar_indice(cfv_nodo,Value{std::string("b")},cfv_cuerpo);
    return cfv_nodo;
  }
  if(booleano(Value{booleano(compara(_idx(cfv_t,Value{std::string("k")}),Value{std::string("KEY")},"=="))&&booleano(compara(_idx(cfv_t,Value{std::string("v")}),Value{std::string("retornar")},"=="))})){
    cfv_avanzar(cfv_st);
    Value cfv_val=Value{};
    if(booleano(Value{booleano(Value{booleano(Value{!booleano(cfv_tok_es_delim(cfv_st,Value{std::string(";")}))})&&booleano(Value{!booleano(cfv_tok_es_delim(cfv_st,Value{std::string("}")}))})})&&booleano(compara(_idx(cfv_tok_actual(cfv_st),Value{std::string("k")}),Value{std::string("EOF")},"!="))})){
      cfv_val=cfv_parse_expr(cfv_st);
    }
    if(booleano(cfv_tok_es_delim(cfv_st,Value{std::string(";")}))){
      cfv_avanzar(cfv_st);
    }
    Value cfv_nodo=_mk_mapa_vacio();
    asignar_indice(cfv_nodo,Value{std::string("k")},Value{std::string("ret")});
    asignar_indice(cfv_nodo,Value{std::string("v")},cfv_val);
    return cfv_nodo;
  }
  if(booleano(Value{booleano(compara(_idx(cfv_t,Value{std::string("k")}),Value{std::string("KEY")},"=="))&&booleano(compara(_idx(cfv_t,Value{std::string("v")}),Value{std::string("romper")},"=="))})){
    cfv_avanzar(cfv_st);
    if(booleano(cfv_tok_es_delim(cfv_st,Value{std::string(";")}))){
      cfv_avanzar(cfv_st);
    }
    Value cfv_nodo=_mk_mapa_vacio();
    asignar_indice(cfv_nodo,Value{std::string("k")},Value{std::string("rom")});
    return cfv_nodo;
  }
  if(booleano(Value{booleano(compara(_idx(cfv_t,Value{std::string("k")}),Value{std::string("KEY")},"=="))&&booleano(compara(_idx(cfv_t,Value{std::string("v")}),Value{std::string("continuar")},"=="))})){
    cfv_avanzar(cfv_st);
    if(booleano(cfv_tok_es_delim(cfv_st,Value{std::string(";")}))){
      cfv_avanzar(cfv_st);
    }
    Value cfv_nodo=_mk_mapa_vacio();
    asignar_indice(cfv_nodo,Value{std::string("k")},Value{std::string("cont")});
    return cfv_nodo;
  }
  if(booleano(Value{booleano(compara(_idx(cfv_t,Value{std::string("k")}),Value{std::string("KEY")},"=="))&&booleano(compara(_idx(cfv_t,Value{std::string("v")}),Value{std::string("lanzar")},"=="))})){
    cfv_avanzar(cfv_st);
    Value cfv_val=cfv_parse_expr(cfv_st);
    if(booleano(cfv_tok_es_delim(cfv_st,Value{std::string(";")}))){
      cfv_avanzar(cfv_st);
    }
    Value cfv_nodo=_mk_mapa_vacio();
    asignar_indice(cfv_nodo,Value{std::string("k")},Value{std::string("throw")});
    asignar_indice(cfv_nodo,Value{std::string("v")},cfv_val);
    return cfv_nodo;
  }
  if(booleano(Value{booleano(compara(_idx(cfv_t,Value{std::string("k")}),Value{std::string("KEY")},"=="))&&booleano(Value{booleano(compara(_idx(cfv_t,Value{std::string("v")}),Value{std::string("mostrar")},"=="))||booleano(compara(_idx(cfv_t,Value{std::string("v")}),Value{std::string("afirmar")},"=="))})})){
    Value cfv_nombre=_idx(cfv_t,Value{std::string("v")});
    cfv_avanzar(cfv_st);
    cfv_consumir(cfv_st,Value{std::string("DELIM")},Value{std::string("(")});
    Value cfv_args=cfv_parse_args(cfv_st);
    cfv_consumir(cfv_st,Value{std::string("DELIM")},Value{std::string(")")});
    if(booleano(cfv_tok_es_delim(cfv_st,Value{std::string(";")}))){
      cfv_avanzar(cfv_st);
    }
    Value cfv_nodo=_mk_mapa_vacio();
    asignar_indice(cfv_nodo,Value{std::string("k")},Value{std::string("call_stmt")});
    asignar_indice(cfv_nodo,Value{std::string("n")},cfv_nombre);
    asignar_indice(cfv_nodo,Value{std::string("a")},cfv_args);
    return cfv_nodo;
  }
  if(booleano(Value{booleano(compara(_idx(cfv_t,Value{std::string("k")}),Value{std::string("KEY")},"=="))&&booleano(compara(_idx(cfv_t,Value{std::string("v")}),Value{std::string("si")},"=="))})){
    cfv_avanzar(cfv_st);
    cfv_consumir(cfv_st,Value{std::string("DELIM")},Value{std::string("(")});
    Value cfv_cond=cfv_parse_expr(cfv_st);
    cfv_consumir(cfv_st,Value{std::string("DELIM")},Value{std::string(")")});
    Value cfv_entonces=cfv_parse_bloque(cfv_st);
    Value cfv_sino_ramas=_mk_lista({});
    Value cfv__sino_sigue=cfv_tok_es_key(cfv_st,Value{std::string("sino")});
    while(booleano(cfv__sino_sigue)){
      cfv_avanzar(cfv_st);
      if(booleano(cfv_tok_es_key(cfv_st,Value{std::string("si")}))){
        cfv_avanzar(cfv_st);
        cfv_consumir(cfv_st,Value{std::string("DELIM")},Value{std::string("(")});
        Value cfv_cond2=cfv_parse_expr(cfv_st);
        cfv_consumir(cfv_st,Value{std::string("DELIM")},Value{std::string(")")});
        Value cfv_bloque2=cfv_parse_bloque(cfv_st);
        Value cfv_rama=_mk_mapa_vacio();
        asignar_indice(cfv_rama,Value{std::string("c")},cfv_cond2);
        asignar_indice(cfv_rama,Value{std::string("b")},cfv_bloque2);
        cfv_agregar(cfv_sino_ramas,cfv_rama);
        cfv__sino_sigue=cfv_tok_es_key(cfv_st,Value{std::string("sino")});
      } else{
        Value cfv_bloque_sino=cfv_parse_bloque(cfv_st);
        Value cfv_rama=_mk_mapa_vacio();
        asignar_indice(cfv_rama,Value{std::string("c")},Value{});
        asignar_indice(cfv_rama,Value{std::string("b")},cfv_bloque_sino);
        cfv_agregar(cfv_sino_ramas,cfv_rama);
        cfv__sino_sigue=Value{false};
      }
    }
    Value cfv_nodo=_mk_mapa_vacio();
    asignar_indice(cfv_nodo,Value{std::string("k")},Value{std::string("si")});
    asignar_indice(cfv_nodo,Value{std::string("c")},cfv_cond);
    asignar_indice(cfv_nodo,Value{std::string("t")},cfv_entonces);
    asignar_indice(cfv_nodo,Value{std::string("e")},cfv_sino_ramas);
    return cfv_nodo;
  }
  if(booleano(Value{booleano(compara(_idx(cfv_t,Value{std::string("k")}),Value{std::string("KEY")},"=="))&&booleano(compara(_idx(cfv_t,Value{std::string("v")}),Value{std::string("mientras")},"=="))})){
    cfv_avanzar(cfv_st);
    cfv_consumir(cfv_st,Value{std::string("DELIM")},Value{std::string("(")});
    Value cfv_cond=cfv_parse_expr(cfv_st);
    cfv_consumir(cfv_st,Value{std::string("DELIM")},Value{std::string(")")});
    Value cfv_cuerpo=cfv_parse_bloque(cfv_st);
    Value cfv_nodo=_mk_mapa_vacio();
    asignar_indice(cfv_nodo,Value{std::string("k")},Value{std::string("mien")});
    asignar_indice(cfv_nodo,Value{std::string("c")},cfv_cond);
    asignar_indice(cfv_nodo,Value{std::string("b")},cfv_cuerpo);
    return cfv_nodo;
  }
  if(booleano(Value{booleano(compara(_idx(cfv_t,Value{std::string("k")}),Value{std::string("KEY")},"=="))&&booleano(compara(_idx(cfv_t,Value{std::string("v")}),Value{std::string("para")},"=="))})){
    cfv_avanzar(cfv_st);
    cfv_consumir(cfv_st,Value{std::string("DELIM")},Value{std::string("(")});
    Value cfv_var_nombre=cfv_consumir_tipo(cfv_st,Value{std::string("ID")});
    cfv_consumir(cfv_st,Value{std::string("KEY")},Value{std::string("en")});
    Value cfv_iterable=cfv_parse_expr(cfv_st);
    cfv_consumir(cfv_st,Value{std::string("DELIM")},Value{std::string(")")});
    Value cfv_cuerpo=cfv_parse_bloque(cfv_st);
    Value cfv_nodo=_mk_mapa_vacio();
    asignar_indice(cfv_nodo,Value{std::string("k")},Value{std::string("para")});
    asignar_indice(cfv_nodo,Value{std::string("v")},_idx(cfv_var_nombre,Value{std::string("v")}));
    asignar_indice(cfv_nodo,Value{std::string("i")},cfv_iterable);
    asignar_indice(cfv_nodo,Value{std::string("b")},cfv_cuerpo);
    return cfv_nodo;
  }
  if(booleano(Value{booleano(compara(_idx(cfv_t,Value{std::string("k")}),Value{std::string("KEY")},"=="))&&booleano(compara(_idx(cfv_t,Value{std::string("v")}),Value{std::string("intentar")},"=="))})){
    cfv_avanzar(cfv_st);
    Value cfv_cuerpo=cfv_parse_bloque(cfv_st);
    Value cfv_err_nombre=Value{std::string("e")};
    Value cfv_cuerpo_cap=_mk_lista({});
    if(booleano(cfv_tok_es_key(cfv_st,Value{std::string("capturar")}))){
      cfv_avanzar(cfv_st);
      cfv_consumir(cfv_st,Value{std::string("DELIM")},Value{std::string("(")});
      Value cfv_en=cfv_consumir_tipo(cfv_st,Value{std::string("ID")});
      cfv_err_nombre=_idx(cfv_en,Value{std::string("v")});
      if(booleano(cfv_tok_es_delim(cfv_st,Value{std::string(":")}))){
        cfv_avanzar(cfv_st);
        cfv_parse_tipo(cfv_st);
      }
      cfv_consumir(cfv_st,Value{std::string("DELIM")},Value{std::string(")")});
      cfv_cuerpo_cap=cfv_parse_bloque(cfv_st);
    }
    Value cfv_nodo=_mk_mapa_vacio();
    asignar_indice(cfv_nodo,Value{std::string("k")},Value{std::string("try")});
    asignar_indice(cfv_nodo,Value{std::string("b")},cfv_cuerpo);
    asignar_indice(cfv_nodo,Value{std::string("en")},cfv_err_nombre);
    asignar_indice(cfv_nodo,Value{std::string("eb")},cfv_cuerpo_cap);
    return cfv_nodo;
  }
  if(booleano(Value{booleano(compara(_idx(cfv_t,Value{std::string("k")}),Value{std::string("KEY")},"=="))&&booleano(compara(_idx(cfv_t,Value{std::string("v")}),Value{std::string("estructura")},"=="))})){
    cfv_avanzar(cfv_st);
    Value cfv_nombre=cfv_consumir_tipo(cfv_st,Value{std::string("ID")});
    cfv_consumir(cfv_st,Value{std::string("DELIM")},Value{std::string("{")});
    Value cfv_campos=_mk_lista({});
    Value cfv_campo_tipos=_mk_lista({});
    while(booleano(Value{!booleano(cfv_tok_es_delim(cfv_st,Value{std::string("}")}))})){
      if(booleano(cfv_tok_es_key(cfv_st,Value{std::string("funcion")}))){
        cfv_parse_stmt(cfv_st);
      } else{
        Value cfv_campo=cfv_consumir_tipo(cfv_st,Value{std::string("ID")});
        Value cfv_ctipo=Value{std::string("cualquiera")};
        if(booleano(cfv_tok_es_delim(cfv_st,Value{std::string(":")}))){
          cfv_avanzar(cfv_st);
          cfv_ctipo=cfv_parse_tipo(cfv_st);
        }
        if(booleano(cfv_tok_es_delim(cfv_st,Value{std::string(";")}))){
          cfv_avanzar(cfv_st);
        }
        cfv_agregar(cfv_campos,_idx(cfv_campo,Value{std::string("v")}));
        cfv_agregar(cfv_campo_tipos,cfv_ctipo);
      }
    }
    cfv_consumir(cfv_st,Value{std::string("DELIM")},Value{std::string("}")});
    Value cfv_nodo=_mk_mapa_vacio();
    asignar_indice(cfv_nodo,Value{std::string("k")},Value{std::string("struct")});
    asignar_indice(cfv_nodo,Value{std::string("n")},_idx(cfv_nombre,Value{std::string("v")}));
    asignar_indice(cfv_nodo,Value{std::string("f")},cfv_campos);
    asignar_indice(cfv_nodo,Value{std::string("ft")},cfv_campo_tipos);
    return cfv_nodo;
  }
  if(booleano(Value{booleano(compara(_idx(cfv_t,Value{std::string("k")}),Value{std::string("KEY")},"=="))&&booleano(compara(_idx(cfv_t,Value{std::string("v")}),Value{std::string("importar")},"=="))})){
    cfv_avanzar(cfv_st);
    Value cfv_ruta=cfv_consumir_tipo(cfv_st,Value{std::string("TEXT")});
    if(booleano(cfv_tok_es_delim(cfv_st,Value{std::string(";")}))){
      cfv_avanzar(cfv_st);
    }
    Value cfv_nodo=_mk_mapa_vacio();
    asignar_indice(cfv_nodo,Value{std::string("k")},Value{std::string("import")});
    asignar_indice(cfv_nodo,Value{std::string("r")},_idx(cfv_ruta,Value{std::string("v")}));
    return cfv_nodo;
  }
  Value cfv_expr=cfv_parse_expr(cfv_st);
  if(booleano(cfv_tok_es_delim(cfv_st,Value{std::string(";")}))){
    cfv_avanzar(cfv_st);
  }
  Value cfv_nodo=_mk_mapa_vacio();
  asignar_indice(cfv_nodo,Value{std::string("k")},Value{std::string("expr_stmt")});
  asignar_indice(cfv_nodo,Value{std::string("v")},cfv_expr);
  return cfv_nodo;
return Value{};}

Value cfv_parse_programa(Value cfv_st){
  Value cfv_stmts=_mk_lista({});
  while(booleano(compara(_idx(cfv_tok_actual(cfv_st),Value{std::string("k")}),Value{std::string("EOF")},"!="))){
    cfv_agregar(cfv_stmts,cfv_parse_stmt(cfv_st));
  }
  return cfv_stmts;
return Value{};}

Value cfv_pad(Value cfv_st){
  Value cfv_r=Value{std::string("")};
  Value cfv_i=Value{(double)0};
  while(booleano(compara(cfv_i,_idx(cfv_st,Value{std::string("indent")}),"<"))){
    cfv_r=_add(cfv_r,Value{std::string("  ")});
    cfv_i=_add(cfv_i,Value{(double)1});
  }
  return cfv_r;
return Value{};}

Value cfv_gen_expr(Value cfv_st,Value cfv_nodo){
  if(booleano(compara(cfv_tipo_de(cfv_nodo),Value{std::string("nulo")},"=="))){
    return Value{std::string("Value{}")};
  }
  Value cfv_k=_idx(cfv_nodo,Value{std::string("k")});
  if(booleano(compara(cfv_k,Value{std::string("num")},"=="))){
    Value cfv_v=_idx(cfv_nodo,Value{std::string("v")});
    return _add(_add(Value{std::string("Value{(double)")},cfv_num_a_txt(cfv_v)),Value{std::string("}")});
  }
  if(booleano(compara(cfv_k,Value{std::string("txt")},"=="))){
    return _add(_add(Value{std::string("Value{std::string(\"")},cfv_escapar_cpp(_idx(cfv_nodo,Value{std::string("v")}))),Value{std::string("\")}")});
  }
  if(booleano(compara(cfv_k,Value{std::string("bool")},"=="))){
    if(booleano(_idx(cfv_nodo,Value{std::string("v")}))){
      return Value{std::string("Value{true}")};
    }
    return Value{std::string("Value{false}")};
  }
  if(booleano(compara(cfv_k,Value{std::string("nul")},"=="))){
    return Value{std::string("Value{}")};
  }
  if(booleano(compara(cfv_k,Value{std::string("id")},"=="))){
    return cfv_safe(_idx(cfv_nodo,Value{std::string("v")}));
  }
  if(booleano(compara(cfv_k,Value{std::string("bin")},"=="))){
    Value cfv_l=cfv_gen_expr(cfv_st,_idx(cfv_nodo,Value{std::string("l")}));
    Value cfv_r=cfv_gen_expr(cfv_st,_idx(cfv_nodo,Value{std::string("r")}));
    Value cfv_op=_idx(cfv_nodo,Value{std::string("op")});
    if(booleano(compara(cfv_op,Value{std::string("+")},"=="))){
      return _add(_add(_add(_add(Value{std::string("_add(")},cfv_l),Value{std::string(",")}),cfv_r),Value{std::string(")")});
    }
    if(booleano(compara(cfv_op,Value{std::string("-")},"=="))){
      return _add(_add(_add(_add(Value{std::string("_sub(")},cfv_l),Value{std::string(",")}),cfv_r),Value{std::string(")")});
    }
    if(booleano(compara(cfv_op,Value{std::string("*")},"=="))){
      return _add(_add(_add(_add(Value{std::string("_mul(")},cfv_l),Value{std::string(",")}),cfv_r),Value{std::string(")")});
    }
    if(booleano(compara(cfv_op,Value{std::string("/")},"=="))){
      return _add(_add(_add(_add(Value{std::string("_div(")},cfv_l),Value{std::string(",")}),cfv_r),Value{std::string(")")});
    }
    if(booleano(compara(cfv_op,Value{std::string("%")},"=="))){
      return _add(_add(_add(_add(Value{std::string("_mod(")},cfv_l),Value{std::string(",")}),cfv_r),Value{std::string(")")});
    }
    if(booleano(compara(cfv_op,Value{std::string("==")},"=="))){
      return _add(_add(_add(_add(Value{std::string("compara(")},cfv_l),Value{std::string(",")}),cfv_r),Value{std::string(",\"==\")")});
    }
    if(booleano(compara(cfv_op,Value{std::string("!=")},"=="))){
      return _add(_add(_add(_add(Value{std::string("compara(")},cfv_l),Value{std::string(",")}),cfv_r),Value{std::string(",\"!=\")")});
    }
    if(booleano(compara(cfv_op,Value{std::string("<")},"=="))){
      return _add(_add(_add(_add(Value{std::string("compara(")},cfv_l),Value{std::string(",")}),cfv_r),Value{std::string(",\"<\")")});
    }
    if(booleano(compara(cfv_op,Value{std::string(">")},"=="))){
      return _add(_add(_add(_add(Value{std::string("compara(")},cfv_l),Value{std::string(",")}),cfv_r),Value{std::string(",\">\")")});
    }
    if(booleano(compara(cfv_op,Value{std::string("<=")},"=="))){
      return _add(_add(_add(_add(Value{std::string("compara(")},cfv_l),Value{std::string(",")}),cfv_r),Value{std::string(",\"<=\")")});
    }
    if(booleano(compara(cfv_op,Value{std::string(">=")},"=="))){
      return _add(_add(_add(_add(Value{std::string("compara(")},cfv_l),Value{std::string(",")}),cfv_r),Value{std::string(",\">=\")")});
    }
    if(booleano(compara(cfv_op,Value{std::string("&&")},"=="))){
      return _add(_add(_add(_add(Value{std::string("Value{booleano(")},cfv_l),Value{std::string(")&&booleano(")}),cfv_r),Value{std::string(")}")});
    }
    if(booleano(compara(cfv_op,Value{std::string("||")},"=="))){
      return _add(_add(_add(_add(Value{std::string("Value{booleano(")},cfv_l),Value{std::string(")||booleano(")}),cfv_r),Value{std::string(")}")});
    }
    return _add(_add(cfv_l,cfv_op),cfv_r);
  }
  if(booleano(compara(cfv_k,Value{std::string("un")},"=="))){
    Value cfv_v=cfv_gen_expr(cfv_st,_idx(cfv_nodo,Value{std::string("v")}));
    Value cfv_op=_idx(cfv_nodo,Value{std::string("op")});
    if(booleano(compara(cfv_op,Value{std::string("!")},"=="))){
      return _add(_add(Value{std::string("Value{!booleano(")},cfv_v),Value{std::string(")}")});
    }
    if(booleano(compara(cfv_op,Value{std::string("-")},"=="))){
      return _add(_add(Value{std::string("Value{-numero(")},cfv_v),Value{std::string(")}")});
    }
    return _add(cfv_op,cfv_v);
  }
  if(booleano(compara(cfv_k,Value{std::string("call")},"=="))){
    Value cfv_nombre=_idx(cfv_nodo,Value{std::string("n")});
    Value cfv_args=_idx(cfv_nodo,Value{std::string("a")});
    Value cfv_args_cpp=_mk_lista({});
    Value cfv_i=Value{(double)0};
    while(booleano(compara(cfv_i,cfv_longitud(cfv_args),"<"))){
      cfv_agregar(cfv_args_cpp,cfv_gen_expr(cfv_st,_idx(cfv_args,cfv_i)));
      cfv_i=_add(cfv_i,Value{(double)1});
    }
    Value cfv_args_str=cfv_juntar(cfv_args_cpp,Value{std::string(",")});
    if(booleano(Value{booleano(compara(cfv_nombre,Value{std::string("mostrar")},"=="))||booleano(compara(cfv_nombre,Value{std::string("print")},"=="))})){
      return _add(_add(Value{std::string("mostrar(")},cfv_args_str),Value{std::string(")")});
    }
    if(booleano(compara(cfv_nombre,Value{std::string("longitud")},"=="))){
      return _add(_add(Value{std::string("cfv_longitud(")},cfv_args_str),Value{std::string(")")});
    }
    if(booleano(compara(cfv_nombre,Value{std::string("agregar")},"=="))){
      return _add(_add(Value{std::string("cfv_agregar(")},cfv_args_str),Value{std::string(")")});
    }
    if(booleano(compara(cfv_nombre,Value{std::string("a_texto")},"=="))){
      return _add(_add(Value{std::string("cfv_a_texto(")},cfv_args_str),Value{std::string(")")});
    }
    if(booleano(compara(cfv_nombre,Value{std::string("a_numero")},"=="))){
      return _add(_add(Value{std::string("cfv_a_numero(")},cfv_args_str),Value{std::string(")")});
    }
    if(booleano(compara(cfv_nombre,Value{std::string("tipo_de")},"=="))){
      return _add(_add(Value{std::string("cfv_tipo_de(")},cfv_args_str),Value{std::string(")")});
    }
    if(booleano(compara(cfv_nombre,Value{std::string("tiene_clave")},"=="))){
      return _add(_add(Value{std::string("cfv_tiene_clave(")},cfv_args_str),Value{std::string(")")});
    }
    if(booleano(compara(cfv_nombre,Value{std::string("claves")},"=="))){
      return _add(_add(Value{std::string("cfv_claves(")},cfv_args_str),Value{std::string(")")});
    }
    if(booleano(compara(cfv_nombre,Value{std::string("leer_archivo")},"=="))){
      return _add(_add(Value{std::string("cfv_leer_archivo(")},cfv_args_str),Value{std::string(")")});
    }
    if(booleano(compara(cfv_nombre,Value{std::string("escribir_archivo")},"=="))){
      return _add(_add(Value{std::string("cfv_escribir_archivo(")},cfv_args_str),Value{std::string(")")});
    }
    if(booleano(compara(cfv_nombre,Value{std::string("existe_archivo")},"=="))){
      return _add(_add(Value{std::string("cfv_existe_archivo(")},cfv_args_str),Value{std::string(")")});
    }
    if(booleano(compara(cfv_nombre,Value{std::string("sys_run")},"=="))){
      return _add(_add(Value{std::string("cfv_sys_run(")},cfv_args_str),Value{std::string(")")});
    }
    if(booleano(compara(cfv_nombre,Value{std::string("argumentos_programa")},"=="))){
      return Value{std::string("cfv_argumentos_programa()")};
    }
    if(booleano(compara(cfv_nombre,Value{std::string("raiz")},"=="))){
      return _add(_add(Value{std::string("cfv_raiz(")},cfv_args_str),Value{std::string(")")});
    }
    if(booleano(compara(cfv_nombre,Value{std::string("absoluto")},"=="))){
      return _add(_add(Value{std::string("cfv_absoluto(")},cfv_args_str),Value{std::string(")")});
    }
    if(booleano(compara(cfv_nombre,Value{std::string("redondear")},"=="))){
      return _add(_add(Value{std::string("cfv_redondear(")},cfv_args_str),Value{std::string(")")});
    }
    if(booleano(compara(cfv_nombre,Value{std::string("potencia")},"=="))){
      return _add(_add(Value{std::string("cfv_potencia(")},cfv_args_str),Value{std::string(")")});
    }
    if(booleano(compara(cfv_nombre,Value{std::string("afirmar")},"=="))){
      return _add(_add(Value{std::string("cfv_afirmar(")},cfv_args_str),Value{std::string(")")});
    }
    return _add(_add(_add(cfv_safe(cfv_nombre),Value{std::string("(")}),cfv_args_str),Value{std::string(")")});
  }
  if(booleano(compara(cfv_k,Value{std::string("idx")},"=="))){
    Value cfv_obj=cfv_gen_expr(cfv_st,_idx(cfv_nodo,Value{std::string("o")}));
    Value cfv_idx=cfv_gen_expr(cfv_st,_idx(cfv_nodo,Value{std::string("i")}));
    return _add(_add(_add(_add(Value{std::string("_idx(")},cfv_obj),Value{std::string(",")}),cfv_idx),Value{std::string(")")});
  }
  if(booleano(compara(cfv_k,Value{std::string("dot")},"=="))){
    Value cfv_obj=cfv_gen_expr(cfv_st,_idx(cfv_nodo,Value{std::string("o")}));
    Value cfv_campo=_idx(cfv_nodo,Value{std::string("f")});
    return _add(_add(_add(_add(Value{std::string("_campo(")},cfv_obj),Value{std::string(",\"")}),cfv_campo),Value{std::string("\")")});
  }
  if(booleano(compara(cfv_k,Value{std::string("metodo")},"=="))){
    Value cfv_obj=cfv_gen_expr(cfv_st,_idx(cfv_nodo,Value{std::string("o")}));
    Value cfv_metodo=_idx(cfv_nodo,Value{std::string("m")});
    Value cfv_args=_idx(cfv_nodo,Value{std::string("a")});
    Value cfv_args_cpp=_mk_lista({});
    Value cfv_i=Value{(double)0};
    while(booleano(compara(cfv_i,cfv_longitud(cfv_args),"<"))){
      cfv_agregar(cfv_args_cpp,cfv_gen_expr(cfv_st,_idx(cfv_args,cfv_i)));
      cfv_i=_add(cfv_i,Value{(double)1});
    }
    Value cfv_args_str=cfv_juntar(cfv_args_cpp,Value{std::string(",")});
    if(booleano(Value{booleano(compara(cfv_metodo,Value{std::string("length")},"=="))||booleano(compara(cfv_metodo,Value{std::string("longitud")},"=="))})){
      return _add(_add(Value{std::string("cfv_longitud(")},cfv_obj),Value{std::string(")")});
    }
    return _add(_add(_add(_add(_add(_add(Value{std::string("cfv_metodo(")},cfv_obj),Value{std::string(",\"")}),cfv_metodo),Value{std::string("\",")}),cfv_args_str),Value{std::string(")")});
  }
  if(booleano(compara(cfv_k,Value{std::string("lst")},"=="))){
    Value cfv_items=_idx(cfv_nodo,Value{std::string("v")});
    Value cfv_items_cpp=_mk_lista({});
    Value cfv_i=Value{(double)0};
    while(booleano(compara(cfv_i,cfv_longitud(cfv_items),"<"))){
      cfv_agregar(cfv_items_cpp,cfv_gen_expr(cfv_st,_idx(cfv_items,cfv_i)));
      cfv_i=_add(cfv_i,Value{(double)1});
    }
    Value cfv_items_str=cfv_juntar(cfv_items_cpp,Value{std::string(",")});
    return _add(_add(Value{std::string("_mk_lista({")},cfv_items_str),Value{std::string("})")});
  }
  if(booleano(compara(cfv_k,Value{std::string("mapa_lit")},"=="))){
    Value cfv_claves=_idx(cfv_nodo,Value{std::string("claves")});
    Value cfv_vals=_idx(cfv_nodo,Value{std::string("vals")});
    if(booleano(compara(cfv_longitud(cfv_claves),Value{(double)0},"=="))){
      return Value{std::string("_mk_mapa_vacio()")};
    }
    Value cfv_pares=_mk_lista({});
    Value cfv_i=Value{(double)0};
    while(booleano(compara(cfv_i,cfv_longitud(cfv_claves),"<"))){
      cfv_agregar(cfv_pares,_add(_add(_add(_add(Value{std::string("{\"")},cfv_escapar_cpp(_idx(cfv_claves,cfv_i))),Value{std::string("\",")}),cfv_gen_expr(cfv_st,_idx(cfv_vals,cfv_i))),Value{std::string("}")}));
      cfv_i=_add(cfv_i,Value{(double)1});
    }
    return _add(_add(Value{std::string("_mk_mapa({")},cfv_juntar(cfv_pares,Value{std::string(",")})),Value{std::string("})")});
  }
  if(booleano(compara(cfv_k,Value{std::string("asgn")},"=="))){
    Value cfv_op=_idx(cfv_nodo,Value{std::string("op")});
    Value cfv_izq=_idx(cfv_nodo,Value{std::string("l")});
    Value cfv_der=cfv_gen_expr(cfv_st,_idx(cfv_nodo,Value{std::string("r")}));
    if(booleano(compara(_idx(cfv_izq,Value{std::string("k")}),Value{std::string("idx")},"=="))){
      Value cfv_obj=cfv_gen_expr(cfv_st,_idx(cfv_izq,Value{std::string("o")}));
      Value cfv_idx=cfv_gen_expr(cfv_st,_idx(cfv_izq,Value{std::string("i")}));
      if(booleano(compara(cfv_op,Value{std::string("=")},"=="))){
        return _add(_add(_add(_add(_add(_add(Value{std::string("asignar_indice(")},cfv_obj),Value{std::string(",")}),cfv_idx),Value{std::string(",")}),cfv_der),Value{std::string(")")});
      }
      Value cfv_tmp=_add(_add(_add(_add(Value{std::string("_idx(")},cfv_obj),Value{std::string(",")}),cfv_idx),Value{std::string(")")});
      if(booleano(compara(cfv_op,Value{std::string("+=")},"=="))){
        return _add(_add(_add(_add(_add(_add(_add(_add(Value{std::string("asignar_indice(")},cfv_obj),Value{std::string(",")}),cfv_idx),Value{std::string(",cfv_add(")}),cfv_tmp),Value{std::string(",")}),cfv_der),Value{std::string("))")});
      }
      if(booleano(compara(cfv_op,Value{std::string("-=")},"=="))){
        return _add(_add(_add(_add(_add(_add(_add(_add(Value{std::string("asignar_indice(")},cfv_obj),Value{std::string(",")}),cfv_idx),Value{std::string(",cfv_sub(")}),cfv_tmp),Value{std::string(",")}),cfv_der),Value{std::string("))")});
      }
      return _add(_add(_add(_add(_add(_add(Value{std::string("asignar_indice(")},cfv_obj),Value{std::string(",")}),cfv_idx),Value{std::string(",")}),cfv_der),Value{std::string(")")});
    }
    if(booleano(compara(_idx(cfv_izq,Value{std::string("k")}),Value{std::string("dot")},"=="))){
      Value cfv_obj=cfv_gen_expr(cfv_st,_idx(cfv_izq,Value{std::string("o")}));
      Value cfv_campo=_idx(cfv_izq,Value{std::string("f")});
      if(booleano(compara(cfv_op,Value{std::string("=")},"=="))){
        return _add(_add(_add(_add(_add(_add(Value{std::string("_set_campo(")},cfv_obj),Value{std::string(",\"")}),cfv_campo),Value{std::string("\",")}),cfv_der),Value{std::string(")")});
      }
    }
    Value cfv_var_cpp=cfv_gen_expr(cfv_st,cfv_izq);
    if(booleano(compara(cfv_op,Value{std::string("=")},"=="))){
      return _add(_add(cfv_var_cpp,Value{std::string("=")}),cfv_der);
    }
    if(booleano(compara(cfv_op,Value{std::string("+=")},"=="))){
      return _add(_add(_add(_add(_add(cfv_var_cpp,Value{std::string("=cfv_add(")}),cfv_var_cpp),Value{std::string(",")}),cfv_der),Value{std::string(")")});
    }
    if(booleano(compara(cfv_op,Value{std::string("-=")},"=="))){
      return _add(_add(_add(_add(_add(cfv_var_cpp,Value{std::string("=Value{numero(")}),cfv_var_cpp),Value{std::string(")-numero(")}),cfv_der),Value{std::string(")}")});
    }
    if(booleano(compara(cfv_op,Value{std::string("*=")},"=="))){
      return _add(_add(_add(_add(_add(cfv_var_cpp,Value{std::string("=Value{numero(")}),cfv_var_cpp),Value{std::string(")*numero(")}),cfv_der),Value{std::string(")}")});
    }
    if(booleano(compara(cfv_op,Value{std::string("/=")},"=="))){
      return _add(_add(_add(_add(_add(cfv_var_cpp,Value{std::string("=Value{numero(")}),cfv_var_cpp),Value{std::string(")/numero(")}),cfv_der),Value{std::string(")}")});
    }
    return _add(_add(cfv_var_cpp,cfv_op),cfv_der);
  }
  return Value{std::string("Value{}")};
return Value{};}

Value cfv_gen_stmt(Value cfv_st,Value cfv_nodo){
  Value cfv_k=_idx(cfv_nodo,Value{std::string("k")});
  Value cfv_p=cfv_pad(cfv_st);
  if(booleano(compara(cfv_k,Value{std::string("var")},"=="))){
    Value cfv_nombre_cpp=cfv_safe(_idx(cfv_nodo,Value{std::string("n")}));
    Value cfv_val_str=Value{std::string("Value{}")};
    if(booleano(compara(_idx(cfv_nodo,Value{std::string("v")}),Value{},"!="))){
      cfv_val_str=cfv_gen_expr(cfv_st,_idx(cfv_nodo,Value{std::string("v")}));
    }
    return _add(_add(_add(_add(_add(cfv_p,Value{std::string("Value ")}),cfv_nombre_cpp),Value{std::string("=")}),cfv_val_str),Value{std::string(";\n")});
  }
  if(booleano(compara(cfv_k,Value{std::string("fun")},"=="))){
    Value cfv_nombre_cpp=cfv_safe(_idx(cfv_nodo,Value{std::string("n")}));
    Value cfv_params=_idx(cfv_nodo,Value{std::string("p")});
    Value cfv_params_cpp=_mk_lista({});
    Value cfv_i=Value{(double)0};
    while(booleano(compara(cfv_i,cfv_longitud(cfv_params),"<"))){
      cfv_agregar(cfv_params_cpp,_add(Value{std::string("Value ")},cfv_safe(_idx(cfv_params,cfv_i))));
      cfv_i=_add(cfv_i,Value{(double)1});
    }
    Value cfv_params_str=cfv_juntar(cfv_params_cpp,Value{std::string(",")});
    Value cfv_r=_add(_add(_add(_add(_add(cfv_p,Value{std::string("Value ")}),cfv_nombre_cpp),Value{std::string("(")}),cfv_params_str),Value{std::string("){\n")});
    asignar_indice(cfv_st,Value{std::string("indent")},_add(_idx(cfv_st,Value{std::string("indent")}),Value{(double)1}));
    Value cfv_i2=Value{(double)0};
    while(booleano(compara(cfv_i2,cfv_longitud(_idx(cfv_nodo,Value{std::string("b")})),"<"))){
      cfv_r=_add(cfv_r,cfv_gen_stmt(cfv_st,_idx(_idx(cfv_nodo,Value{std::string("b")}),cfv_i2)));
      cfv_i2=_add(cfv_i2,Value{(double)1});
    }
    asignar_indice(cfv_st,Value{std::string("indent")},_sub(_idx(cfv_st,Value{std::string("indent")}),Value{(double)1}));
    cfv_r=_add(_add(cfv_r,cfv_p),Value{std::string("return Value{};}\n")});
    return cfv_r;
  }
  if(booleano(compara(cfv_k,Value{std::string("ret")},"=="))){
    if(booleano(compara(_idx(cfv_nodo,Value{std::string("v")}),Value{},"=="))){
      return _add(cfv_p,Value{std::string("return Value{};\n")});
    }
    return _add(_add(_add(cfv_p,Value{std::string("return ")}),cfv_gen_expr(cfv_st,_idx(cfv_nodo,Value{std::string("v")}))),Value{std::string(";\n")});
  }
  if(booleano(compara(cfv_k,Value{std::string("rom")},"=="))){
    return _add(cfv_p,Value{std::string("break;\n")});
  }
  if(booleano(compara(cfv_k,Value{std::string("cont")},"=="))){
    return _add(cfv_p,Value{std::string("continue;\n")});
  }
  if(booleano(compara(cfv_k,Value{std::string("throw")},"=="))){
    return _add(_add(_add(cfv_p,Value{std::string("throw std::runtime_error(texto(")}),cfv_gen_expr(cfv_st,_idx(cfv_nodo,Value{std::string("v")}))),Value{std::string("));\n")});
  }
  if(booleano(compara(cfv_k,Value{std::string("si")},"=="))){
    Value cfv_r=_add(_add(_add(cfv_p,Value{std::string("if(booleano(")}),cfv_gen_expr(cfv_st,_idx(cfv_nodo,Value{std::string("c")}))),Value{std::string(")){\n")});
    asignar_indice(cfv_st,Value{std::string("indent")},_add(_idx(cfv_st,Value{std::string("indent")}),Value{(double)1}));
    Value cfv_i=Value{(double)0};
    while(booleano(compara(cfv_i,cfv_longitud(_idx(cfv_nodo,Value{std::string("t")})),"<"))){
      cfv_r=_add(cfv_r,cfv_gen_stmt(cfv_st,_idx(_idx(cfv_nodo,Value{std::string("t")}),cfv_i)));
      cfv_i=_add(cfv_i,Value{(double)1});
    }
    asignar_indice(cfv_st,Value{std::string("indent")},_sub(_idx(cfv_st,Value{std::string("indent")}),Value{(double)1}));
    cfv_r=_add(_add(cfv_r,cfv_p),Value{std::string("}")});
    Value cfv_ramas=_idx(cfv_nodo,Value{std::string("e")});
    Value cfv_j=Value{(double)0};
    while(booleano(compara(cfv_j,cfv_longitud(cfv_ramas),"<"))){
      Value cfv_rama=_idx(cfv_ramas,cfv_j);
      if(booleano(compara(_idx(cfv_rama,Value{std::string("c")}),Value{},"!="))){
        cfv_r=_add(_add(_add(cfv_r,Value{std::string(" else if(booleano(")}),cfv_gen_expr(cfv_st,_idx(cfv_rama,Value{std::string("c")}))),Value{std::string(")){\n")});
      } else{
        cfv_r=_add(cfv_r,Value{std::string(" else{\n")});
      }
      asignar_indice(cfv_st,Value{std::string("indent")},_add(_idx(cfv_st,Value{std::string("indent")}),Value{(double)1}));
      Value cfv_i2=Value{(double)0};
      while(booleano(compara(cfv_i2,cfv_longitud(_idx(cfv_rama,Value{std::string("b")})),"<"))){
        cfv_r=_add(cfv_r,cfv_gen_stmt(cfv_st,_idx(_idx(cfv_rama,Value{std::string("b")}),cfv_i2)));
        cfv_i2=_add(cfv_i2,Value{(double)1});
      }
      asignar_indice(cfv_st,Value{std::string("indent")},_sub(_idx(cfv_st,Value{std::string("indent")}),Value{(double)1}));
      cfv_r=_add(_add(cfv_r,cfv_p),Value{std::string("}")});
      cfv_j=_add(cfv_j,Value{(double)1});
    }
    cfv_r=_add(cfv_r,Value{std::string("\n")});
    return cfv_r;
  }
  if(booleano(compara(cfv_k,Value{std::string("mien")},"=="))){
    Value cfv_r=_add(_add(_add(cfv_p,Value{std::string("while(booleano(")}),cfv_gen_expr(cfv_st,_idx(cfv_nodo,Value{std::string("c")}))),Value{std::string(")){\n")});
    asignar_indice(cfv_st,Value{std::string("indent")},_add(_idx(cfv_st,Value{std::string("indent")}),Value{(double)1}));
    Value cfv_i=Value{(double)0};
    while(booleano(compara(cfv_i,cfv_longitud(_idx(cfv_nodo,Value{std::string("b")})),"<"))){
      cfv_r=_add(cfv_r,cfv_gen_stmt(cfv_st,_idx(_idx(cfv_nodo,Value{std::string("b")}),cfv_i)));
      cfv_i=_add(cfv_i,Value{(double)1});
    }
    asignar_indice(cfv_st,Value{std::string("indent")},_sub(_idx(cfv_st,Value{std::string("indent")}),Value{(double)1}));
    cfv_r=_add(_add(cfv_r,cfv_p),Value{std::string("}\n")});
    return cfv_r;
  }
  if(booleano(compara(cfv_k,Value{std::string("para")},"=="))){
    Value cfv_var_cpp=cfv_safe(_idx(cfv_nodo,Value{std::string("v")}));
    Value cfv_iter=cfv_gen_expr(cfv_st,_idx(cfv_nodo,Value{std::string("i")}));
    Value cfv_tmp=_add(cfv_var_cpp,Value{std::string("_iter")});
    Value cfv_r=_add(_add(_add(_add(_add(cfv_p,Value{std::string("{\nValue ")}),cfv_tmp),Value{std::string("=")}),cfv_iter),Value{std::string(";\n")});
    cfv_r=_add(_add(_add(_add(_add(_add(cfv_r,cfv_p),Value{std::string("auto ")}),cfv_var_cpp),Value{std::string("_lst=std::get_if<Lista>(&")}),cfv_tmp),Value{std::string(".data);\n")});
    cfv_r=_add(_add(_add(_add(_add(_add(_add(_add(cfv_r,cfv_p),Value{std::string("if(")}),cfv_var_cpp),Value{std::string("_lst){for(Value ")}),cfv_var_cpp),Value{std::string(":*(*")}),cfv_var_cpp),Value{std::string("_lst)){\n")});
    asignar_indice(cfv_st,Value{std::string("indent")},_add(_idx(cfv_st,Value{std::string("indent")}),Value{(double)1}));
    Value cfv_i=Value{(double)0};
    while(booleano(compara(cfv_i,cfv_longitud(_idx(cfv_nodo,Value{std::string("b")})),"<"))){
      cfv_r=_add(cfv_r,cfv_gen_stmt(cfv_st,_idx(_idx(cfv_nodo,Value{std::string("b")}),cfv_i)));
      cfv_i=_add(cfv_i,Value{(double)1});
    }
    asignar_indice(cfv_st,Value{std::string("indent")},_sub(_idx(cfv_st,Value{std::string("indent")}),Value{(double)1}));
    cfv_r=_add(_add(cfv_r,cfv_p),Value{std::string("}}\n")});
    cfv_r=_add(_add(cfv_r,cfv_p),Value{std::string("}\n")});
    return cfv_r;
  }
  if(booleano(compara(cfv_k,Value{std::string("try")},"=="))){
    Value cfv_r=_add(cfv_p,Value{std::string("try{\n")});
    asignar_indice(cfv_st,Value{std::string("indent")},_add(_idx(cfv_st,Value{std::string("indent")}),Value{(double)1}));
    Value cfv_i=Value{(double)0};
    while(booleano(compara(cfv_i,cfv_longitud(_idx(cfv_nodo,Value{std::string("b")})),"<"))){
      cfv_r=_add(cfv_r,cfv_gen_stmt(cfv_st,_idx(_idx(cfv_nodo,Value{std::string("b")}),cfv_i)));
      cfv_i=_add(cfv_i,Value{(double)1});
    }
    asignar_indice(cfv_st,Value{std::string("indent")},_sub(_idx(cfv_st,Value{std::string("indent")}),Value{(double)1}));
    cfv_r=_add(_add(_add(_add(cfv_r,cfv_p),Value{std::string("} catch(const std::exception& ")}),cfv_safe(_idx(cfv_nodo,Value{std::string("en")}))),Value{std::string("_ex){\n")});
    cfv_r=_add(_add(_add(_add(_add(_add(cfv_r,cfv_p),Value{std::string("  Value ")}),cfv_safe(_idx(cfv_nodo,Value{std::string("en")}))),Value{std::string("=Value{std::string(")}),cfv_safe(_idx(cfv_nodo,Value{std::string("en")}))),Value{std::string("_ex.what())};\n")});
    asignar_indice(cfv_st,Value{std::string("indent")},_add(_idx(cfv_st,Value{std::string("indent")}),Value{(double)1}));
    Value cfv_j=Value{(double)0};
    while(booleano(compara(cfv_j,cfv_longitud(_idx(cfv_nodo,Value{std::string("eb")})),"<"))){
      cfv_r=_add(cfv_r,cfv_gen_stmt(cfv_st,_idx(_idx(cfv_nodo,Value{std::string("eb")}),cfv_j)));
      cfv_j=_add(cfv_j,Value{(double)1});
    }
    asignar_indice(cfv_st,Value{std::string("indent")},_sub(_idx(cfv_st,Value{std::string("indent")}),Value{(double)1}));
    cfv_r=_add(_add(_add(_add(cfv_r,cfv_p),Value{std::string("} catch(const Value& ")}),cfv_safe(_idx(cfv_nodo,Value{std::string("en")}))),Value{std::string("_v){\n")});
    cfv_r=_add(_add(_add(_add(_add(_add(cfv_r,cfv_p),Value{std::string("  Value ")}),cfv_safe(_idx(cfv_nodo,Value{std::string("en")}))),Value{std::string("=")}),cfv_safe(_idx(cfv_nodo,Value{std::string("en")}))),Value{std::string("_v;\n")});
    asignar_indice(cfv_st,Value{std::string("indent")},_add(_idx(cfv_st,Value{std::string("indent")}),Value{(double)1}));
    Value cfv_j2=Value{(double)0};
    while(booleano(compara(cfv_j2,cfv_longitud(_idx(cfv_nodo,Value{std::string("eb")})),"<"))){
      cfv_r=_add(cfv_r,cfv_gen_stmt(cfv_st,_idx(_idx(cfv_nodo,Value{std::string("eb")}),cfv_j2)));
      cfv_j2=_add(cfv_j2,Value{(double)1});
    }
    asignar_indice(cfv_st,Value{std::string("indent")},_sub(_idx(cfv_st,Value{std::string("indent")}),Value{(double)1}));
    cfv_r=_add(_add(cfv_r,cfv_p),Value{std::string("}\n")});
    return cfv_r;
  }
  if(booleano(compara(cfv_k,Value{std::string("struct")},"=="))){
    Value cfv_nombre=_idx(cfv_nodo,Value{std::string("n")});
    Value cfv_campos=_idx(cfv_nodo,Value{std::string("f")});
    Value cfv_params_cpp=_mk_lista({});
    Value cfv_i=Value{(double)0};
    while(booleano(compara(cfv_i,cfv_longitud(cfv_campos),"<"))){
      cfv_agregar(cfv_params_cpp,_add(Value{std::string("Value ")},cfv_safe(_idx(cfv_campos,cfv_i))));
      cfv_i=_add(cfv_i,Value{(double)1});
    }
    Value cfv_r=_add(_add(_add(_add(_add(cfv_p,Value{std::string("Value ")}),cfv_safe(cfv_nombre)),Value{std::string("(")}),cfv_juntar(cfv_params_cpp,Value{std::string(",")})),Value{std::string("){\n")});
    cfv_r=_add(_add(cfv_r,cfv_p),Value{std::string("  auto m=std::make_shared<std::map<std::string,Value>>();\n")});
    Value cfv_j=Value{(double)0};
    while(booleano(compara(cfv_j,cfv_longitud(cfv_campos),"<"))){
      cfv_r=_add(_add(_add(_add(_add(_add(cfv_r,cfv_p),Value{std::string("  (*m)[\"")}),_idx(cfv_campos,cfv_j)),Value{std::string("\"]=")}),cfv_safe(_idx(cfv_campos,cfv_j))),Value{std::string(";\n")});
      cfv_j=_add(cfv_j,Value{(double)1});
    }
    cfv_r=_add(_add(cfv_r,cfv_p),Value{std::string("  return Value{m};\n")});
    cfv_r=_add(_add(cfv_r,cfv_p),Value{std::string("}\n")});
    return cfv_r;
  }
  if(booleano(compara(cfv_k,Value{std::string("import")},"=="))){
    return Value{std::string("")};
  }
  if(booleano(compara(cfv_k,Value{std::string("call_stmt")},"=="))){
    Value cfv_nombre=_idx(cfv_nodo,Value{std::string("n")});
    Value cfv_args=_idx(cfv_nodo,Value{std::string("a")});
    Value cfv_args_cpp=_mk_lista({});
    Value cfv_i=Value{(double)0};
    while(booleano(compara(cfv_i,cfv_longitud(cfv_args),"<"))){
      cfv_agregar(cfv_args_cpp,cfv_gen_expr(cfv_st,_idx(cfv_args,cfv_i)));
      cfv_i=_add(cfv_i,Value{(double)1});
    }
    Value cfv_args_str=cfv_juntar(cfv_args_cpp,Value{std::string(",")});
    if(booleano(Value{booleano(compara(cfv_nombre,Value{std::string("mostrar")},"=="))||booleano(compara(cfv_nombre,Value{std::string("print")},"=="))})){
      return _add(_add(_add(cfv_p,Value{std::string("mostrar(")}),cfv_args_str),Value{std::string(");\n")});
    }
    if(booleano(compara(cfv_nombre,Value{std::string("afirmar")},"=="))){
      return _add(_add(_add(cfv_p,Value{std::string("cfv_afirmar(")}),cfv_args_str),Value{std::string(");\n")});
    }
    return _add(_add(_add(_add(cfv_p,cfv_safe(cfv_nombre)),Value{std::string("(")}),cfv_args_str),Value{std::string(");\n")});
  }
  if(booleano(compara(cfv_k,Value{std::string("expr_stmt")},"=="))){
    Value cfv_e=_idx(cfv_nodo,Value{std::string("v")});
    if(booleano(compara(_idx(cfv_e,Value{std::string("k")}),Value{std::string("asgn")},"=="))){
      return _add(_add(cfv_p,cfv_gen_expr(cfv_st,cfv_e)),Value{std::string(";\n")});
    }
    if(booleano(compara(_idx(cfv_e,Value{std::string("k")}),Value{std::string("call")},"=="))){
      return _add(_add(cfv_p,cfv_gen_expr(cfv_st,cfv_e)),Value{std::string(";\n")});
    }
    return _add(_add(cfv_p,cfv_gen_expr(cfv_st,cfv_e)),Value{std::string(";\n")});
  }
  return Value{std::string("")};
return Value{};}

int main(int argc,char**argv){
  try{
    auto cfv_args_lista=std::make_shared<std::vector<Value>>();
    for(int i=1;i<argc;++i) cfv_args_lista->push_back(Value{std::string(argv[i])});
    cfv_argumentos_global=Value{cfv_args_lista};
  Value cfv_KEYWORDS=_mk_lista({Value{std::string("sea")},Value{std::string("funcion")},Value{std::string("si")},Value{std::string("sino")},Value{std::string("mientras")},Value{std::string("para")},Value{std::string("retornar")},Value{std::string("romper")},Value{std::string("continuar")},Value{std::string("verdadero")},Value{std::string("falso")},Value{std::string("nulo")},Value{std::string("estructura")},Value{std::string("clase")},Value{std::string("intentar")},Value{std::string("capturar")},Value{std::string("lanzar")},Value{std::string("importar")},Value{std::string("usar")},Value{std::string("exportar")},Value{std::string("gpu")},Value{std::string("y")},Value{std::string("o")},Value{std::string("no")},Value{std::string("cualquiera")},Value{std::string("numero")},Value{std::string("texto")},Value{std::string("booleano")},Value{std::string("lista")},Value{std::string("mapa")},Value{std::string("afirmar")},Value{std::string("mostrar")},Value{std::string("en")}});
  Value cfv_G_STATE=_mk_mapa_vacio();
  asignar_indice(cfv_G_STATE,Value{std::string("toks")},_mk_lista({}));
  asignar_indice(cfv_G_STATE,Value{std::string("pos")},Value{(double)0});
  asignar_indice(cfv_G_STATE,Value{std::string("indent")},Value{(double)0});
  Value cfv_RUNTIME_EXTRA=cfv_leer_archivo(Value{std::string("runtime_extra.cpp")});
  Value cfv_args=cfv_argumentos_programa();
  Value cfv_cfv_ok=compara(cfv_longitud(cfv_args),Value{(double)2},">=");
  if(booleano(Value{!booleano(cfv_cfv_ok)})){
    mostrar(Value{std::string("uso: compilador_nativo.cfv <fuente.cfv> <salida>")});
    mostrar(Value{std::string("  ejemplo: ./cforgev compilador_nativo.cfv programa.cfv programa")});
  }
  if(booleano(cfv_cfv_ok)){
    Value cfv_ruta_fuente=_idx(cfv_args,Value{(double)0});
    Value cfv_ruta_salida=_idx(cfv_args,Value{(double)1});
    Value cfv_ruta_cpp=_add(cfv_ruta_salida,Value{std::string(".cpp")});
    mostrar(_add(_add(_add(Value{std::string("C-Forge compilador nativo: ")},cfv_ruta_fuente),Value{std::string(" -> ")}),cfv_ruta_salida));
    Value cfv_fuente=cfv_leer_archivo(cfv_ruta_fuente);
    asignar_indice(cfv_G_STATE,Value{std::string("toks")},cfv_tokenizar(cfv_fuente));
    asignar_indice(cfv_G_STATE,Value{std::string("pos")},Value{(double)0});
    Value cfv_programa=cfv_parse_programa(cfv_G_STATE);
    Value cfv_runtime_path=Value{std::string("runtime.cpp")};
    Value cfv_runtime_str=cfv_leer_archivo(cfv_runtime_path);
    Value cfv_prototipos=_mk_lista({});
    Value cfv_funciones=_mk_lista({});
    Value cfv_main_code=Value{std::string("")};
    asignar_indice(cfv_G_STATE,Value{std::string("indent")},Value{(double)1});
    Value cfv_i=Value{(double)0};
    while(booleano(compara(cfv_i,cfv_longitud(cfv_programa),"<"))){
      Value cfv_stmt=_idx(cfv_programa,cfv_i);
      if(booleano(compara(_idx(cfv_stmt,Value{std::string("k")}),Value{std::string("fun")},"=="))){
        Value cfv_nombre_cpp=cfv_safe(_idx(cfv_stmt,Value{std::string("n")}));
        Value cfv_params=_idx(cfv_stmt,Value{std::string("p")});
        Value cfv_params_cpp=_mk_lista({});
        Value cfv_j=Value{(double)0};
        while(booleano(compara(cfv_j,cfv_longitud(cfv_params),"<"))){
          cfv_agregar(cfv_params_cpp,_add(Value{std::string("Value ")},cfv_safe(_idx(cfv_params,cfv_j))));
          cfv_j=_add(cfv_j,Value{(double)1});
        }
        cfv_agregar(cfv_prototipos,_add(_add(_add(_add(Value{std::string("Value ")},cfv_nombre_cpp),Value{std::string("(")}),cfv_juntar(cfv_params_cpp,Value{std::string(",")})),Value{std::string(");")}));
        asignar_indice(cfv_G_STATE,Value{std::string("indent")},Value{(double)0});
        cfv_agregar(cfv_funciones,cfv_gen_stmt(cfv_G_STATE,cfv_stmt));
      } else{
        asignar_indice(cfv_G_STATE,Value{std::string("indent")},Value{(double)1});
        cfv_main_code=_add(cfv_main_code,cfv_gen_stmt(cfv_G_STATE,cfv_stmt));
      }
      cfv_i=_add(cfv_i,Value{(double)1});
    }
    Value cfv_cpp=_add(cfv_runtime_str,Value{std::string("\n")});
    cfv_cpp=_add(_add(cfv_cpp,cfv_RUNTIME_EXTRA),Value{std::string("\n")});
    cfv_cpp=_add(_add(cfv_cpp,cfv_juntar(cfv_prototipos,Value{std::string("\n")})),Value{std::string("\n")});
    cfv_cpp=_add(_add(cfv_cpp,cfv_juntar(cfv_funciones,Value{std::string("\n")})),Value{std::string("\n")});
    cfv_cpp=_add(cfv_cpp,Value{std::string("int main(int argc,char**argv){\n")});
    cfv_cpp=_add(cfv_cpp,Value{std::string("  try{\n")});
    cfv_cpp=_add(cfv_cpp,Value{std::string("    auto cfv_args_lista=std::make_shared<std::vector<Value>>();\n")});
    cfv_cpp=_add(cfv_cpp,Value{std::string("    for(int i=1;i<argc;++i) cfv_args_lista->push_back(Value{std::string(argv[i])});\n")});
    cfv_cpp=_add(cfv_cpp,Value{std::string("    cfv_argumentos_global=Value{cfv_args_lista};\n")});
    cfv_cpp=_add(cfv_cpp,cfv_main_code);
    cfv_cpp=_add(cfv_cpp,Value{std::string("    return 0;\n")});
    cfv_cpp=_add(cfv_cpp,Value{std::string("  } catch(const std::exception& e){\n")});
    cfv_cpp=_add(cfv_cpp,Value{std::string("    std::cerr<<\"[C-Forge] \"<<e.what()<<'\\n';return 1;\n")});
    cfv_cpp=_add(cfv_cpp,Value{std::string("  }\n")});
    cfv_cpp=_add(cfv_cpp,Value{std::string("}\n")});
    cfv_escribir_archivo(cfv_ruta_cpp,cfv_cpp);
    mostrar(_add(Value{std::string("C++ generado: ")},cfv_ruta_cpp));
    Value cfv_flags_extra=Value{std::string("")};
    Value cfv_info_plat=cfv_sys_run(Value{std::string("uname -s")});
    if(booleano(Value{booleano(compara(_idx(cfv_info_plat,Value{std::string("estado")}),Value{(double)0},"=="))&&booleano(cfv_texto_contiene(_idx(cfv_info_plat,Value{std::string("salida")}),Value{std::string("Linux")}))})){
      cfv_flags_extra=Value{std::string(" -ldl")};
    }
    Value cfv_cmd=_add(_add(_add(_add(Value{std::string("clang++ -std=c++17 -O2 -Iinclude ")},cfv_ruta_cpp),Value{std::string(" -o ")}),cfv_ruta_salida),cfv_flags_extra);
    mostrar(_add(Value{std::string("Compilando: ")},cfv_cmd));
    Value cfv_resultado=cfv_sys_run(cfv_cmd);
    if(booleano(compara(_idx(cfv_resultado,Value{std::string("estado")}),Value{(double)0},"!="))){
      Value cfv_cmd2=_add(_add(_add(_add(Value{std::string("g++ -std=c++17 -O2 -Iinclude ")},cfv_ruta_cpp),Value{std::string(" -o ")}),cfv_ruta_salida),cfv_flags_extra);
      mostrar(_add(Value{std::string("Reintentando con g++: ")},cfv_cmd2));
      cfv_resultado=cfv_sys_run(cfv_cmd2);
    }
    if(booleano(compara(_idx(cfv_resultado,Value{std::string("estado")}),Value{(double)0},"!="))){
      mostrar(Value{std::string("Error del compilador C++:")});
      mostrar(_idx(cfv_resultado,Value{std::string("salida")}));
      mostrar(_idx(cfv_resultado,Value{std::string("error")}));
    }
    mostrar(_add(Value{std::string("Binario listo: ")},cfv_ruta_salida));
  }
    return 0;
  } catch(const std::exception& e){
    std::cerr<<"[C-Forge] "<<e.what()<<'\n';return 1;
  }
}
