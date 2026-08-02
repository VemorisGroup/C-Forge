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
// ── Texto extras ──────────────────────────────────────────────────────────
static Value cfv_subcadena(const Value&v,const Value&a,const Value&b){if(v.index()!=2)throw std::runtime_error("subcadena requiere texto");const auto&s=std::get<std::string>(v.data);size_t start=(size_t)numero(a),end=(size_t)numero(b);if(start>s.size())return std::string("");if(end>s.size())end=s.size();if(start>end)return std::string("");return s.substr(start,end-start);}
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
static Value cfv_texto_a_numero(const Value&v){if(v.index()!=2)throw std::runtime_error("texto_a_numero requiere texto");try{return std::stod(std::get<std::string>(v.data));}catch(...){throw std::runtime_error("texto_a_numero: '"+std::get<std::string>(v.data)+"' no es un número válido");}}
static Value cfv_numero_a_texto(const Value&v){return Value{texto(v)};}
// ── Matemáticas extras ────────────────────────────────────────────────────
static Value cfv_piso(const Value&v){return std::floor(numero(v));}
static Value cfv_techo(const Value&v){return std::ceil(numero(v));}
static Value cfv_maximo(const Value&a,const Value&b){return numero(a)>=numero(b)?a:b;}
static Value cfv_minimo(const Value&a,const Value&b){return numero(a)<=numero(b)?a:b;}
// ── Lista extras ──────────────────────────────────────────────────────────
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
