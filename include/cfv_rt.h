/* cfv_rt.h — C-Forge C Runtime v1.0
 * Runtime de valores para codigo C-Forge compilado.
 * Include solo este header en el .c generado.
 * Compilar con: gcc -O2 out.c -o out -lm  (apps)
 *               gcc -O2 out.c -o out -lm -lSDL2 -lSDL2_image -lSDL2_ttf (juegos)
 */
#ifndef CFV_RT_H
#define CFV_RT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <time.h>

/* ── Tipos base ─────────────────────────────────────────────────────────── */

#define CFV_NULO   0
#define CFV_NUM    1
#define CFV_TEXTO  2
#define CFV_BOOL   3
#define CFV_LISTA  4
#define CFV_MAPA   5
#define CFV_FN     6

/* Forward declarations */
typedef struct CfvLista CfvLista;
typedef struct CfvMapa  CfvMapa;
typedef struct CfvVal   CfvVal;
typedef CfvVal (*CfvFnPtr)(CfvVal*, int);

/* CfvVal must be defined first so CfvPar can embed it by value */
struct CfvVal {
    int tag;
    union {
        double    n;
        char*     s;
        int       b;
        CfvLista* lst;
        CfvMapa*  map;
        CfvFnPtr  fn;
    };
};
struct CfvLista {
    CfvVal* items;
    int     len, cap;
};
typedef struct {
    char*  key;
    CfvVal val;
} CfvPar;
struct CfvMapa {
    CfvPar* pairs;
    int     len, cap;
};

/* ── Arena de memoria (sin GC — ok para CLIs y juegos) ─────────────────── */
#define CFV_ARENA_MB 64
static char  cfv_arena_buf[CFV_ARENA_MB * 1024 * 1024];
static size_t cfv_arena_pos = 0;
static void* cfv_arena_alloc(size_t n) {
    n = (n + 7) & ~7u; /* align 8 */
    if (cfv_arena_pos + n > sizeof(cfv_arena_buf)) {
        fprintf(stderr, "[C-Forge] Arena OOM\n"); exit(1);
    }
    void* p = cfv_arena_buf + cfv_arena_pos;
    cfv_arena_pos += n;
    return p;
}
static char* cfv_strdup(const char* s) {
    size_t n = strlen(s) + 1;
    char* p = (char*)cfv_arena_alloc(n);
    memcpy(p, s, n);
    return p;
}

/* ── Constructores ──────────────────────────────────────────────────────── */
static inline CfvVal cfv_nulo(void)          { CfvVal v; v.tag=CFV_NULO; v.n=0; return v; }
static inline CfvVal cfv_num(double n)        { CfvVal v; v.tag=CFV_NUM;  v.n=n; return v; }
static inline CfvVal cfv_bool(int b)          { CfvVal v; v.tag=CFV_BOOL; v.b=b; return v; }
static inline CfvVal cfv_texto(const char* s) { CfvVal v; v.tag=CFV_TEXTO; v.s=cfv_strdup(s); return v; }
static CfvVal cfv_lista_nueva(void) {
    CfvVal v; v.tag=CFV_LISTA;
    v.lst = (CfvLista*)cfv_arena_alloc(sizeof(CfvLista));
    v.lst->items = NULL; v.lst->len = 0; v.lst->cap = 0;
    return v;
}
static CfvVal cfv_mapa_nuevo(void) {
    CfvVal v; v.tag=CFV_MAPA;
    v.map = (CfvMapa*)cfv_arena_alloc(sizeof(CfvMapa));
    v.map->pairs = NULL; v.map->len = 0; v.map->cap = 0;
    return v;
}

/* ── Truthiness ─────────────────────────────────────────────────────────── */
static inline int cfv_truthy(CfvVal v) {
    switch (v.tag) {
        case CFV_NULO:  return 0;
        case CFV_BOOL:  return v.b;
        case CFV_NUM:   return v.n != 0.0;
        case CFV_TEXTO: return v.s && v.s[0] != '\0';
        case CFV_LISTA: return v.lst && v.lst->len > 0;
        case CFV_MAPA:  return v.map && v.map->len > 0;
        default:        return 1;
    }
}

/* ── Conversion a texto ─────────────────────────────────────────────────── */
static char cfv_num_buf[64];
static const char* cfv_as_cstr(CfvVal v) {
    switch (v.tag) {
        case CFV_NULO:  return "nulo";
        case CFV_BOOL:  return v.b ? "verdadero" : "falso";
        case CFV_TEXTO: return v.s ? v.s : "";
        case CFV_NUM: {
            double d = v.n;
            if (d == (long long)d && d >= -1e15 && d <= 1e15)
                snprintf(cfv_num_buf, sizeof(cfv_num_buf), "%lld", (long long)d);
            else
                snprintf(cfv_num_buf, sizeof(cfv_num_buf), "%g", d);
            return cfv_num_buf;
        }
        case CFV_LISTA: return "[lista]";
        case CFV_MAPA:  return "[mapa]";
        default:        return "[?]";
    }
}
static CfvVal cfv_a_texto(CfvVal v)  { return cfv_texto(cfv_as_cstr(v)); }
static CfvVal cfv_a_numero(CfvVal v) {
    if (v.tag==CFV_NUM)   return v;
    if (v.tag==CFV_BOOL)  return cfv_num(v.b);
    if (v.tag==CFV_TEXTO && v.s) return cfv_num(atof(v.s));
    return cfv_num(0);
}
static CfvVal cfv_a_booleano(CfvVal v) { return cfv_bool(cfv_truthy(v)); }

/* ── Aritmetica ─────────────────────────────────────────────────────────── */
static CfvVal cfv_add(CfvVal a, CfvVal b) {
    if (a.tag==CFV_TEXTO || b.tag==CFV_TEXTO) {
        const char* sa = cfv_as_cstr(a);
        const char* sb = cfv_as_cstr(b);
        size_t la = strlen(sa), lb = strlen(sb);
        char* r = (char*)cfv_arena_alloc(la+lb+1);
        memcpy(r, sa, la); memcpy(r+la, sb, lb); r[la+lb]=0;
        CfvVal v; v.tag=CFV_TEXTO; v.s=r; return v;
    }
    return cfv_num(cfv_a_numero(a).n + cfv_a_numero(b).n);
}
static CfvVal cfv_sub(CfvVal a, CfvVal b) { return cfv_num(cfv_a_numero(a).n - cfv_a_numero(b).n); }
static CfvVal cfv_mul(CfvVal a, CfvVal b) { return cfv_num(cfv_a_numero(a).n * cfv_a_numero(b).n); }
static CfvVal cfv_div(CfvVal a, CfvVal b) {
    double d = cfv_a_numero(b).n;
    if (d==0) { fprintf(stderr,"[C-Forge] Division por cero\n"); exit(1); }
    return cfv_num(cfv_a_numero(a).n / d);
}
static CfvVal cfv_mod(CfvVal a, CfvVal b) {
    double d = cfv_a_numero(b).n;
    if (d==0) { fprintf(stderr,"[C-Forge] Modulo por cero\n"); exit(1); }
    return cfv_num(fmod(cfv_a_numero(a).n, d));
}
static CfvVal cfv_pow(CfvVal a, CfvVal b) { return cfv_num(pow(cfv_a_numero(a).n, cfv_a_numero(b).n)); }
static CfvVal cfv_neg(CfvVal a)            { return cfv_num(-cfv_a_numero(a).n); }
static CfvVal cfv_bw_and(CfvVal a, CfvVal b) { return cfv_num((long long)cfv_a_numero(a).n & (long long)cfv_a_numero(b).n); }
static CfvVal cfv_bw_or (CfvVal a, CfvVal b) { return cfv_num((long long)cfv_a_numero(a).n | (long long)cfv_a_numero(b).n); }
static CfvVal cfv_bw_xor(CfvVal a, CfvVal b) { return cfv_num((long long)cfv_a_numero(a).n ^ (long long)cfv_a_numero(b).n); }
static CfvVal cfv_bw_not(CfvVal a)            { return cfv_num(~(long long)cfv_a_numero(a).n); }
static CfvVal cfv_shl(CfvVal a, CfvVal b) { return cfv_num((long long)cfv_a_numero(a).n << (int)cfv_a_numero(b).n); }
static CfvVal cfv_shr(CfvVal a, CfvVal b) { return cfv_num((long long)cfv_a_numero(a).n >> (int)cfv_a_numero(b).n); }

/* ── Comparacion ────────────────────────────────────────────────────────── */
static int cfv_vals_eq(CfvVal a, CfvVal b) {
    if (a.tag != b.tag) {
        if ((a.tag==CFV_NUM||a.tag==CFV_BOOL) && (b.tag==CFV_NUM||b.tag==CFV_BOOL))
            return cfv_a_numero(a).n == cfv_a_numero(b).n;
        return 0;
    }
    switch (a.tag) {
        case CFV_NULO:  return 1;
        case CFV_NUM:   return a.n == b.n;
        case CFV_BOOL:  return a.b == b.b;
        case CFV_TEXTO: return strcmp(a.s, b.s)==0;
        case CFV_LISTA: return a.lst == b.lst;
        case CFV_MAPA:  return a.map == b.map;
        default:        return 0;
    }
}
static CfvVal cfv_eq(CfvVal a, CfvVal b)  { return cfv_bool(cfv_vals_eq(a,b)); }
static CfvVal cfv_neq(CfvVal a, CfvVal b) { return cfv_bool(!cfv_vals_eq(a,b)); }
static CfvVal cfv_lt(CfvVal a, CfvVal b) {
    if (a.tag==CFV_TEXTO && b.tag==CFV_TEXTO) return cfv_bool(strcmp(a.s,b.s)<0);
    return cfv_bool(cfv_a_numero(a).n < cfv_a_numero(b).n);
}
static CfvVal cfv_le(CfvVal a, CfvVal b) {
    if (a.tag==CFV_TEXTO && b.tag==CFV_TEXTO) return cfv_bool(strcmp(a.s,b.s)<=0);
    return cfv_bool(cfv_a_numero(a).n <= cfv_a_numero(b).n);
}
static CfvVal cfv_gt(CfvVal a, CfvVal b)  { return cfv_lt(b,a); }
static CfvVal cfv_ge(CfvVal a, CfvVal b)  { return cfv_le(b,a); }
static CfvVal cfv_and(CfvVal a, CfvVal b) { return cfv_truthy(a) ? b : a; }
static CfvVal cfv_or (CfvVal a, CfvVal b) { return cfv_truthy(a) ? a : b; }
static CfvVal cfv_not(CfvVal a)            { return cfv_bool(!cfv_truthy(a)); }
static CfvVal cfv_nulo_coal(CfvVal a, CfvVal b) { return a.tag==CFV_NULO ? b : a; }

/* ── Listas ─────────────────────────────────────────────────────────────── */
static void cfv_lst_push(CfvLista* lst, CfvVal v) {
    if (lst->len >= lst->cap) {
        int nc = lst->cap ? lst->cap*2 : 8;
        CfvVal* nb = (CfvVal*)cfv_arena_alloc(nc * sizeof(CfvVal));
        if (lst->items) memcpy(nb, lst->items, lst->len*sizeof(CfvVal));
        lst->items = nb; lst->cap = nc;
    }
    lst->items[lst->len++] = v;
}
static CfvVal cfv_longitud(CfvVal v) {
    if (v.tag==CFV_LISTA) return cfv_num(v.lst->len);
    if (v.tag==CFV_TEXTO && v.s) return cfv_num(strlen(v.s));
    if (v.tag==CFV_MAPA)  return cfv_num(v.map->len);
    return cfv_num(0);
}
static CfvVal cfv_agregar(CfvVal lst, CfvVal item) {
    if (lst.tag!=CFV_LISTA) { fprintf(stderr,"[C-Forge] agregar: no es lista\n"); return cfv_nulo(); }
    cfv_lst_push(lst.lst, item);
    return cfv_nulo();
}
static CfvVal cfv_lista_of(int n, ...) {
    CfvVal v = cfv_lista_nueva();
    va_list ap; va_start(ap,n);
    for (int i=0;i<n;i++) cfv_lst_push(v.lst, va_arg(ap,CfvVal));
    va_end(ap);
    return v;
}

/* ── Mapas ──────────────────────────────────────────────────────────────── */
static void cfv_map_set(CfvMapa* m, const char* key, CfvVal val) {
    for (int i=0;i<m->len;i++) {
        if (strcmp(m->pairs[i].key,key)==0) { m->pairs[i].val=val; return; }
    }
    if (m->len >= m->cap) {
        int nc = m->cap ? m->cap*2 : 8;
        CfvPar* nb = (CfvPar*)cfv_arena_alloc(nc*sizeof(CfvPar));
        if (m->pairs) memcpy(nb,m->pairs,m->len*sizeof(CfvPar));
        m->pairs=nb; m->cap=nc;
    }
    m->pairs[m->len].key = cfv_strdup(key);
    m->pairs[m->len].val = val;
    m->len++;
}
static CfvVal cfv_map_get(CfvMapa* m, const char* key) {
    for (int i=0;i<m->len;i++)
        if (strcmp(m->pairs[i].key,key)==0) return m->pairs[i].val;
    return cfv_nulo();
}
static CfvVal cfv_mapa_of(int n, ...) {
    CfvVal v = cfv_mapa_nuevo();
    va_list ap; va_start(ap,n);
    for (int i=0;i<n;i++) {
        const char* k = va_arg(ap,const char*);
        CfvVal      val = va_arg(ap,CfvVal);
        cfv_map_set(v.map, k, val);
    }
    va_end(ap);
    return v;
}
static CfvVal cfv_claves(CfvVal v) {
    CfvVal r = cfv_lista_nueva();
    if (v.tag==CFV_MAPA)
        for (int i=0;i<v.map->len;i++)
            cfv_lst_push(r.lst, cfv_texto(v.map->pairs[i].key));
    return r;
}

/* ── Indexado ───────────────────────────────────────────────────────────── */
static CfvVal cfv_indice_get(CfvVal obj, CfvVal idx) {
    if (obj.tag==CFV_LISTA) {
        int i = (int)cfv_a_numero(idx).n;
        if (i<0) i += obj.lst->len;
        if (i<0||i>=obj.lst->len) return cfv_nulo();
        return obj.lst->items[i];
    }
    if (obj.tag==CFV_MAPA) {
        const char* k = cfv_as_cstr(idx);
        return cfv_map_get(obj.map, k);
    }
    if (obj.tag==CFV_TEXTO && idx.tag==CFV_NUM) {
        int i = (int)idx.n;
        if (i<0) i += (int)strlen(obj.s);
        if (!obj.s||i<0||i>=(int)strlen(obj.s)) return cfv_texto("");
        char buf[2]={obj.s[i],0};
        return cfv_texto(buf);
    }
    return cfv_nulo();
}
static void cfv_indice_set(CfvVal obj, CfvVal idx, CfvVal val) {
    if (obj.tag==CFV_LISTA) {
        int i=(int)cfv_a_numero(idx).n;
        if (i<0)i+=obj.lst->len;
        if (i>=0&&i<obj.lst->len) obj.lst->items[i]=val;
    } else if (obj.tag==CFV_MAPA) {
        cfv_map_set(obj.map, cfv_as_cstr(idx), val);
    }
}

/* ── I/O ────────────────────────────────────────────────────────────────── */
static CfvVal cfv_mostrar(CfvVal v) {
    printf("%s\n", cfv_as_cstr(v)); return cfv_nulo();
}
static CfvVal cfv_leer(CfvVal prompt) {
    if (prompt.tag!=CFV_NULO) printf("%s", cfv_as_cstr(prompt));
    char buf[4096]="";
    if (fgets(buf,sizeof(buf),stdin)) {
        size_t n=strlen(buf);
        if (n>0&&buf[n-1]=='\n') buf[n-1]=0;
    }
    return cfv_texto(buf);
}
static CfvVal cfv_leer_linea(void) { return cfv_leer(cfv_nulo()); }

/* ── Strings ────────────────────────────────────────────────────────────── */
static CfvVal cfv_mayusculas(CfvVal v) {
    if (v.tag!=CFV_TEXTO||!v.s) return v;
    size_t n=strlen(v.s); char* r=(char*)cfv_arena_alloc(n+1);
    for (size_t i=0;i<n;i++) r[i]=(v.s[i]>='a'&&v.s[i]<='z')?v.s[i]-32:v.s[i];
    r[n]=0; CfvVal out; out.tag=CFV_TEXTO; out.s=r; return out;
}
static CfvVal cfv_minusculas(CfvVal v) {
    if (v.tag!=CFV_TEXTO||!v.s) return v;
    size_t n=strlen(v.s); char* r=(char*)cfv_arena_alloc(n+1);
    for (size_t i=0;i<n;i++) r[i]=(v.s[i]>='A'&&v.s[i]<='Z')?v.s[i]+32:v.s[i];
    r[n]=0; CfvVal out; out.tag=CFV_TEXTO; out.s=r; return out;
}
static CfvVal cfv_contiene(CfvVal hay, CfvVal needle) {
    if (hay.tag!=CFV_TEXTO||needle.tag!=CFV_TEXTO) return cfv_bool(0);
    return cfv_bool(strstr(hay.s, needle.s)!=NULL);
}
static CfvVal cfv_empieza_con(CfvVal s, CfvVal pre) {
    if (s.tag!=CFV_TEXTO||pre.tag!=CFV_TEXTO) return cfv_bool(0);
    return cfv_bool(strncmp(s.s,pre.s,strlen(pre.s))==0);
}
static CfvVal cfv_termina_con(CfvVal s, CfvVal suf) {
    if (s.tag!=CFV_TEXTO||suf.tag!=CFV_TEXTO) return cfv_bool(0);
    size_t ls=strlen(s.s),lx=strlen(suf.s);
    if (lx>ls) return cfv_bool(0);
    return cfv_bool(strcmp(s.s+ls-lx,suf.s)==0);
}
static CfvVal cfv_subcadena(CfvVal s, CfvVal desde, CfvVal hasta) {
    if (s.tag!=CFV_TEXTO||!s.s) return cfv_texto("");
    int n=(int)strlen(s.s);
    int a=(int)cfv_a_numero(desde).n, b=(int)cfv_a_numero(hasta).n;
    if (a<0)a=0; if (b>n)b=n; if (a>=b) return cfv_texto("");
    char* r=(char*)cfv_arena_alloc(b-a+1);
    memcpy(r,s.s+a,b-a); r[b-a]=0;
    CfvVal out; out.tag=CFV_TEXTO; out.s=r; return out;
}
static CfvVal cfv_recortar(CfvVal v) {
    if (v.tag!=CFV_TEXTO||!v.s) return v;
    const char* s=v.s; while(*s==' '||*s=='\t'||*s=='\n'||*s=='\r') s++;
    size_t n=strlen(s); while(n>0&&(s[n-1]==' '||s[n-1]=='\t'||s[n-1]=='\n'||s[n-1]=='\r')) n--;
    char* r=(char*)cfv_arena_alloc(n+1); memcpy(r,s,n); r[n]=0;
    CfvVal out; out.tag=CFV_TEXTO; out.s=r; return out;
}
static CfvVal cfv_reemplazar(CfvVal s, CfvVal from, CfvVal to) {
    if (s.tag!=CFV_TEXTO||from.tag!=CFV_TEXTO||to.tag!=CFV_TEXTO) return s;
    char* p=strstr(s.s,from.s);
    if (!p) return s;
    size_t lf=strlen(from.s),lt=strlen(to.s),ls=strlen(s.s);
    size_t nc=ls-lf+lt+1;
    char* r=(char*)cfv_arena_alloc(nc);
    size_t off=p-s.s;
    memcpy(r,s.s,off); memcpy(r+off,to.s,lt); strcpy(r+off+lt,p+lf);
    CfvVal out; out.tag=CFV_TEXTO; out.s=r; return out;
}
static CfvVal cfv_dividir(CfvVal s, CfvVal sep) {
    CfvVal r=cfv_lista_nueva();
    if (s.tag!=CFV_TEXTO||sep.tag!=CFV_TEXTO||!s.s||!sep.s) return r;
    char* src=cfv_strdup(s.s);
    if (strlen(sep.s)==0) {
        for (size_t i=0;i<strlen(src);i++){char b[2]={src[i],0};cfv_lst_push(r.lst,cfv_texto(b));}
        return r;
    }
    char* tok=strtok(src,sep.s);
    while(tok){cfv_lst_push(r.lst,cfv_texto(tok));tok=strtok(NULL,sep.s);}
    return r;
}
static CfvVal cfv_unir(CfvVal lst, CfvVal sep) {
    if (lst.tag!=CFV_LISTA) return cfv_texto("");
    const char* sp=(sep.tag==CFV_TEXTO&&sep.s)?sep.s:"";
    size_t tot=0;
    for (int i=0;i<lst.lst->len;i++){
        tot+=strlen(cfv_as_cstr(lst.lst->items[i]));
        if(i<lst.lst->len-1)tot+=strlen(sp);
    }
    char* r=(char*)cfv_arena_alloc(tot+1); r[0]=0;
    for (int i=0;i<lst.lst->len;i++){
        strcat(r,cfv_as_cstr(lst.lst->items[i]));
        if(i<lst.lst->len-1)strcat(r,sp);
    }
    CfvVal out; out.tag=CFV_TEXTO; out.s=r; return out;
}
static CfvVal cfv_posicion(CfvVal s, CfvVal sub) {
    if (s.tag!=CFV_TEXTO||sub.tag!=CFV_TEXTO||!s.s||!sub.s) return cfv_num(-1);
    char* p=strstr(s.s,sub.s);
    return cfv_num(p?p-s.s:-1);
}

/* ── Math ───────────────────────────────────────────────────────────────── */
static CfvVal cfv_raiz(CfvVal v)     { return cfv_num(sqrt(cfv_a_numero(v).n)); }
static CfvVal cfv_abs_fn(CfvVal v)   { return cfv_num(fabs(cfv_a_numero(v).n)); }
static CfvVal cfv_truncar(CfvVal v)  { return cfv_num(trunc(cfv_a_numero(v).n)); }
static CfvVal cfv_redondear(CfvVal v){ return cfv_num(round(cfv_a_numero(v).n)); }
static CfvVal cfv_piso(CfvVal v)     { return cfv_num(floor(cfv_a_numero(v).n)); }
static CfvVal cfv_techo(CfvVal v)    { return cfv_num(ceil(cfv_a_numero(v).n)); }
static CfvVal cfv_max_fn(CfvVal a, CfvVal b){ return cfv_num(fmax(cfv_a_numero(a).n,cfv_a_numero(b).n)); }
static CfvVal cfv_min_fn(CfvVal a, CfvVal b){ return cfv_num(fmin(cfv_a_numero(a).n,cfv_a_numero(b).n)); }
static CfvVal cfv_aleatorio(void)    { return cfv_num((double)rand()/(double)RAND_MAX); }
static CfvVal cfv_aleatorio_entero(CfvVal a, CfvVal b){
    int lo=(int)cfv_a_numero(a).n, hi=(int)cfv_a_numero(b).n;
    return cfv_num(lo + rand()%(hi-lo+1));
}
static CfvVal cfv_sen(CfvVal v)  { return cfv_num(sin(cfv_a_numero(v).n)); }
static CfvVal cfv_cos(CfvVal v)  { return cfv_num(cos(cfv_a_numero(v).n)); }
static CfvVal cfv_tan(CfvVal v)  { return cfv_num(tan(cfv_a_numero(v).n)); }
static CfvVal cfv_log_fn(CfvVal v)  { return cfv_num(log(cfv_a_numero(v).n)); }
static CfvVal cfv_log10_fn(CfvVal v){ return cfv_num(log10(cfv_a_numero(v).n)); }

/* ── Manejo de errores — setjmp/longjmp ──────────────────────────────────── */
#define CFV_ERR_STACK 64
static jmp_buf cfv_err_jmps[CFV_ERR_STACK];
static CfvVal  cfv_err_vals[CFV_ERR_STACK];
static int     cfv_err_top = 0;

#define CFV_TRY \
    cfv_err_top++; \
    if (cfv_err_top >= CFV_ERR_STACK){fprintf(stderr,"[C-Forge] Error stack overflow\n");exit(1);} \
    if (setjmp(cfv_err_jmps[cfv_err_top-1]) == 0)

#define CFV_CATCH(var) \
    else { CfvVal var = cfv_err_vals[cfv_err_top-1]; cfv_err_top--;

#define CFV_CATCH_END \
    } cfv_err_top--;

#define CFV_FINALLY \
    cfv_err_top--;

static void cfv_lanzar(CfvVal v) {
    if (cfv_err_top == 0) {
        fprintf(stderr, "[C-Forge Error] %s\n", cfv_as_cstr(v));
        exit(1);
    }
    cfv_err_vals[cfv_err_top-1] = v;
    cfv_err_top--;
    longjmp(cfv_err_jmps[cfv_err_top], 1);
}
static CfvVal cfv_error(CfvVal msg) { cfv_lanzar(msg); return cfv_nulo(); }

/* ── Tiempo ─────────────────────────────────────────────────────────────── */
static CfvVal cfv_ahora_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
    return cfv_num((double)ts.tv_sec*1000.0 + (double)ts.tv_nsec/1e6);
}
static CfvVal cfv_dormir(CfvVal ms) {
    struct timespec ts;
    ts.tv_sec  = (time_t)(cfv_a_numero(ms).n/1000);
    ts.tv_nsec = (long)(fmod(cfv_a_numero(ms).n,1000)*1e6);
    nanosleep(&ts,NULL);
    return cfv_nulo();
}

/* ── Tipo checks ────────────────────────────────────────────────────────── */
static CfvVal cfv_es_nulo(CfvVal v)    { return cfv_bool(v.tag==CFV_NULO); }
static CfvVal cfv_es_numero(CfvVal v)  { return cfv_bool(v.tag==CFV_NUM); }
static CfvVal cfv_es_texto(CfvVal v)   { return cfv_bool(v.tag==CFV_TEXTO); }
static CfvVal cfv_es_lista(CfvVal v)   { return cfv_bool(v.tag==CFV_LISTA); }
static CfvVal cfv_es_mapa(CfvVal v)    { return cfv_bool(v.tag==CFV_MAPA); }
static CfvVal cfv_tipo_de(CfvVal v) {
    const char* t[] = {"nulo","numero","texto","booleano","lista","mapa","funcion"};
    return cfv_texto(v.tag<7?t[v.tag]:"?");
}

/* ── Lista ops ──────────────────────────────────────────────────────────── */
static CfvVal cfv_lista_slice(CfvVal lst, CfvVal desde, CfvVal hasta) {
    if (lst.tag!=CFV_LISTA) return cfv_lista_nueva();
    int n=lst.lst->len;
    int a=(int)cfv_a_numero(desde).n, b=(int)cfv_a_numero(hasta).n;
    if (a<0)a+=n; if (b<0)b+=n;
    if (a<0)a=0; if (b>n)b=n;
    CfvVal r=cfv_lista_nueva();
    for(int i=a;i<b;i++) cfv_lst_push(r.lst,lst.lst->items[i]);
    return r;
}
static CfvVal cfv_lista_invertir(CfvVal lst) {
    if (lst.tag!=CFV_LISTA) return lst;
    CfvVal r=cfv_lista_nueva();
    for(int i=lst.lst->len-1;i>=0;i--) cfv_lst_push(r.lst,lst.lst->items[i]);
    return r;
}
static CfvVal cfv_lista_contiene(CfvVal lst, CfvVal v) {
    if (lst.tag!=CFV_LISTA) return cfv_bool(0);
    for(int i=0;i<lst.lst->len;i++) if(cfv_vals_eq(lst.lst->items[i],v)) return cfv_bool(1);
    return cfv_bool(0);
}
static CfvVal cfv_lista_pop(CfvVal lst) {
    if (lst.tag!=CFV_LISTA||lst.lst->len==0) return cfv_nulo();
    return lst.lst->items[--lst.lst->len];
}
static CfvVal cfv_lista_insertar(CfvVal lst, CfvVal idx, CfvVal val) {
    if (lst.tag!=CFV_LISTA) return cfv_nulo();
    int i=(int)cfv_a_numero(idx).n;
    cfv_lst_push(lst.lst,cfv_nulo()); /* grow */
    for(int j=lst.lst->len-1;j>i;j--) lst.lst->items[j]=lst.lst->items[j-1];
    lst.lst->items[i]=val;
    return cfv_nulo();
}

/* ── Printf para strings formateadas ────────────────────────────────────── */
static CfvVal cfv_formato(CfvVal fmt, ...) {
    /* minimal: just return fmt as text */
    return cfv_a_texto(fmt);
}

/* ── Argv ───────────────────────────────────────────────────────────────── */
static CfvVal cfv_argv_global;

/* ── Init ───────────────────────────────────────────────────────────────── */
static void cfv_init(int argc, char** argv) {
    srand((unsigned)time(NULL));
    cfv_argv_global = cfv_lista_nueva();
    for (int i=0;i<argc;i++) cfv_lst_push(cfv_argv_global.lst, cfv_texto(argv[i]));
}

/* ── SDL2 game backend (incluye solo si CFV_SDL2) ───────────────────────── */
#ifdef CFV_SDL2
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#ifdef CFV_SDL2_TTF
/* ya incluido */
#endif
#ifdef CFV_SDL2_IMG
/* ya incluido */
#endif

static SDL_Window*   cfv_sdl_win  = NULL;
static SDL_Renderer* cfv_sdl_ren  = NULL;
static int           cfv_sdl_run  = 0;
static double        cfv_sdl_dt   = 0.0;
static Uint32        cfv_sdl_last = 0;
static double        cfv_sdl_fps_target = 60.0;

/* Input state */
static const Uint8*  cfv_sdl_keys   = NULL;
static int           cfv_sdl_mx=0, cfv_sdl_my=0;
static Uint32        cfv_sdl_mbuttons=0;
static char          cfv_sdl_input_char=0;

static CfvVal cfv_juego_iniciar(CfvVal titulo, CfvVal ancho, CfvVal alto) {
    SDL_Init(SDL_INIT_EVERYTHING);
#ifdef CFV_SDL2_TTF
    TTF_Init();
#endif
#ifdef CFV_SDL2_IMG
    IMG_Init(IMG_INIT_PNG|IMG_INIT_JPG);
#endif
    int w=(int)cfv_a_numero(ancho).n, h=(int)cfv_a_numero(alto).n;
    cfv_sdl_win = SDL_CreateWindow(cfv_as_cstr(titulo),
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h,
        SDL_WINDOW_SHOWN|SDL_WINDOW_RESIZABLE);
    cfv_sdl_ren = SDL_CreateRenderer(cfv_sdl_win,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
    cfv_sdl_run = 1;
    cfv_sdl_last= SDL_GetTicks();
    cfv_sdl_keys= SDL_GetKeyboardState(NULL);
    return cfv_nulo();
}
static CfvVal cfv_juego_corriendo(void) { return cfv_bool(cfv_sdl_run); }
static CfvVal cfv_juego_limpiar(CfvVal r, CfvVal g, CfvVal b) {
    SDL_SetRenderDrawColor(cfv_sdl_ren,(Uint8)cfv_a_numero(r).n,(Uint8)cfv_a_numero(g).n,(Uint8)cfv_a_numero(b).n,255);
    SDL_RenderClear(cfv_sdl_ren);
    return cfv_nulo();
}
static CfvVal cfv_juego_renderizar(void) {
    SDL_RenderPresent(cfv_sdl_ren);
    Uint32 now=SDL_GetTicks();
    cfv_sdl_dt=(now-cfv_sdl_last)/1000.0;
    cfv_sdl_last=now;
    return cfv_nulo();
}
static CfvVal cfv_juego_procesar_eventos(void) {
    cfv_sdl_input_char=0;
    SDL_Event e;
    while(SDL_PollEvent(&e)){
        if(e.type==SDL_QUIT) cfv_sdl_run=0;
        if(e.type==SDL_KEYDOWN && e.key.keysym.sym<128) cfv_sdl_input_char=(char)e.key.keysym.sym;
    }
    cfv_sdl_mbuttons=SDL_GetMouseState(&cfv_sdl_mx,&cfv_sdl_my);
    return cfv_nulo();
}
static CfvVal cfv_juego_dibujar_rectangulo(CfvVal x,CfvVal y,CfvVal w,CfvVal h,CfvVal r,CfvVal g,CfvVal b,CfvVal a){
    SDL_SetRenderDrawBlendMode(cfv_sdl_ren,SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(cfv_sdl_ren,(Uint8)cfv_a_numero(r).n,(Uint8)cfv_a_numero(g).n,(Uint8)cfv_a_numero(b).n,(Uint8)cfv_a_numero(a).n);
    SDL_Rect rect={(int)cfv_a_numero(x).n,(int)cfv_a_numero(y).n,(int)cfv_a_numero(w).n,(int)cfv_a_numero(h).n};
    SDL_RenderFillRect(cfv_sdl_ren,&rect);
    return cfv_nulo();
}
static CfvVal cfv_juego_dibujar_rectangulo_borde(CfvVal x,CfvVal y,CfvVal w,CfvVal h,CfvVal r,CfvVal g,CfvVal b,CfvVal a){
    SDL_SetRenderDrawBlendMode(cfv_sdl_ren,SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(cfv_sdl_ren,(Uint8)cfv_a_numero(r).n,(Uint8)cfv_a_numero(g).n,(Uint8)cfv_a_numero(b).n,(Uint8)cfv_a_numero(a).n);
    SDL_Rect rect={(int)cfv_a_numero(x).n,(int)cfv_a_numero(y).n,(int)cfv_a_numero(w).n,(int)cfv_a_numero(h).n};
    SDL_RenderDrawRect(cfv_sdl_ren,&rect);
    return cfv_nulo();
}
static CfvVal cfv_juego_dibujar_linea(CfvVal x1,CfvVal y1,CfvVal x2,CfvVal y2,CfvVal r,CfvVal g,CfvVal b,CfvVal a){
    SDL_SetRenderDrawBlendMode(cfv_sdl_ren,SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(cfv_sdl_ren,(Uint8)cfv_a_numero(r).n,(Uint8)cfv_a_numero(g).n,(Uint8)cfv_a_numero(b).n,(Uint8)cfv_a_numero(a).n);
    SDL_RenderDrawLine(cfv_sdl_ren,(int)cfv_a_numero(x1).n,(int)cfv_a_numero(y1).n,(int)cfv_a_numero(x2).n,(int)cfv_a_numero(y2).n);
    return cfv_nulo();
}
static CfvVal cfv_juego_dibujar_punto(CfvVal x,CfvVal y,CfvVal r,CfvVal g,CfvVal b,CfvVal a){
    SDL_SetRenderDrawBlendMode(cfv_sdl_ren,SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(cfv_sdl_ren,(Uint8)cfv_a_numero(r).n,(Uint8)cfv_a_numero(g).n,(Uint8)cfv_a_numero(b).n,(Uint8)cfv_a_numero(a).n);
    SDL_RenderDrawPoint(cfv_sdl_ren,(int)cfv_a_numero(x).n,(int)cfv_a_numero(y).n);
    return cfv_nulo();
}
static CfvVal cfv_juego_tecla_presionada(CfvVal nombre) {
    if (!cfv_sdl_keys||nombre.tag!=CFV_TEXTO) return cfv_bool(0);
    SDL_Keycode kc = SDL_GetKeyFromName(nombre.s);
    SDL_Scancode sc= SDL_GetScancodeFromKey(kc);
    return cfv_bool(cfv_sdl_keys[sc]);
}
static CfvVal cfv_juego_raton_x(void)    { return cfv_num(cfv_sdl_mx); }
static CfvVal cfv_juego_raton_y(void)    { return cfv_num(cfv_sdl_my); }
static CfvVal cfv_juego_raton_boton(CfvVal b){ return cfv_bool(cfv_sdl_mbuttons & SDL_BUTTON((int)cfv_a_numero(b).n)); }
static CfvVal cfv_juego_dt(void)          { return cfv_num(cfv_sdl_dt); }
static CfvVal cfv_juego_ancho(void)      { int w,h; SDL_GetWindowSize(cfv_sdl_win,&w,&h); return cfv_num(w); }
static CfvVal cfv_juego_alto(void)       { int w,h; SDL_GetWindowSize(cfv_sdl_win,&w,&h); return cfv_num(h); }
static CfvVal cfv_juego_fps_objetivo(CfvVal fps){ cfv_sdl_fps_target=cfv_a_numero(fps).n; return cfv_nulo(); }
static CfvVal cfv_juego_cerrar(void)     { cfv_sdl_run=0; return cfv_nulo(); }
static CfvVal cfv_juego_titulo(CfvVal t) { SDL_SetWindowTitle(cfv_sdl_win,cfv_as_cstr(t)); return cfv_nulo(); }

static CfvVal cfv_juego_terminar(void) {
    if(cfv_sdl_ren) SDL_DestroyRenderer(cfv_sdl_ren);
    if(cfv_sdl_win) SDL_DestroyWindow(cfv_sdl_win);
    TTF_Quit(); IMG_Quit(); SDL_Quit();
    return cfv_nulo();
}

/* ── Builtins SDL2 para código compilado (cfv_f_sdl_*) ─────────────────── */
#define _CFV_SDL_MAX 512
static SDL_Window*   _cfv_wins[_CFV_SDL_MAX];
static SDL_Renderer* _cfv_rens[_CFV_SDL_MAX];
static SDL_Texture*  _cfv_texs[_CFV_SDL_MAX];
static int           _cfv_obj_next = 0;

static CfvVal cfv_f_sdl_init(CfvVal flags) {
    SDL_Init((Uint32)cfv_a_numero(flags).n);
    TTF_Init(); IMG_Init(IMG_INIT_PNG|IMG_INIT_JPG);
    return cfv_nulo();
}
static CfvVal cfv_f_sdl_ventana(CfvVal titulo, CfvVal ancho, CfvVal alto, CfvVal flags) {
    int id = _cfv_obj_next++;
    _cfv_wins[id] = SDL_CreateWindow(cfv_as_cstr(titulo),
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        (int)cfv_a_numero(ancho).n, (int)cfv_a_numero(alto).n,
        (Uint32)cfv_a_numero(flags).n);
    return cfv_num(id);
}
static CfvVal cfv_f_sdl_renderer(CfvVal vid) {
    int wid = (int)cfv_a_numero(vid).n;
    int id  = _cfv_obj_next++;
    _cfv_rens[id] = SDL_CreateRenderer(_cfv_wins[wid], -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    return cfv_num(id);
}
static CfvVal cfv_f_sdl_color(CfvVal rid, CfvVal r, CfvVal g, CfvVal b, CfvVal a) {
    SDL_SetRenderDrawColor(_cfv_rens[(int)cfv_a_numero(rid).n],
        (Uint8)cfv_a_numero(r).n,(Uint8)cfv_a_numero(g).n,
        (Uint8)cfv_a_numero(b).n,(Uint8)cfv_a_numero(a).n);
    return cfv_nulo();
}
static CfvVal cfv_f_sdl_limpiar(CfvVal rid) {
    SDL_RenderClear(_cfv_rens[(int)cfv_a_numero(rid).n]);
    return cfv_nulo();
}
static CfvVal cfv_f_sdl_presentar(CfvVal rid) {
    SDL_RenderPresent(_cfv_rens[(int)cfv_a_numero(rid).n]);
    return cfv_nulo();
}
static CfvVal cfv_f_sdl_rect(CfvVal rid, CfvVal x, CfvVal y, CfvVal w, CfvVal h, CfvVal relleno) {
    SDL_Rect rc = {(int)cfv_a_numero(x).n,(int)cfv_a_numero(y).n,
                   (int)cfv_a_numero(w).n,(int)cfv_a_numero(h).n};
    SDL_Renderer* ren = _cfv_rens[(int)cfv_a_numero(rid).n];
    if (cfv_truthy(relleno)) SDL_RenderFillRect(ren,&rc);
    else                      SDL_RenderDrawRect(ren,&rc);
    return cfv_nulo();
}
static CfvVal cfv_f_sdl_linea(CfvVal rid, CfvVal x1, CfvVal y1, CfvVal x2, CfvVal y2) {
    SDL_RenderDrawLine(_cfv_rens[(int)cfv_a_numero(rid).n],
        (int)cfv_a_numero(x1).n,(int)cfv_a_numero(y1).n,
        (int)cfv_a_numero(x2).n,(int)cfv_a_numero(y2).n);
    return cfv_nulo();
}
static CfvVal cfv_f_sdl_ticks(void)         { return cfv_num((double)SDL_GetTicks()); }
static CfvVal cfv_f_sdl_esperar(CfvVal ms)  { SDL_Delay((Uint32)cfv_a_numero(ms).n); return cfv_nulo(); }
static CfvVal cfv_f_sdl_cerrar(void)        { IMG_Quit(); TTF_Quit(); SDL_Quit(); return cfv_nulo(); }

static CfvVal cfv_f_sdl_eventos(void) {
    CfvVal lista = cfv_lista_nueva();
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        CfvVal ev = cfv_mapa_nuevo();
        cfv_map_set(ev.map,"tipo",cfv_num(e.type));
        if (e.type==SDL_KEYDOWN||e.type==SDL_KEYUP) {
            cfv_map_set(ev.map,"tecla",cfv_num(e.key.keysym.scancode));
            cfv_map_set(ev.map,"presionada",cfv_bool(e.type==SDL_KEYDOWN));
        } else if (e.type==SDL_MOUSEMOTION) {
            cfv_map_set(ev.map,"x",cfv_num(e.motion.x));
            cfv_map_set(ev.map,"y",cfv_num(e.motion.y));
        } else if (e.type==SDL_MOUSEBUTTONDOWN||e.type==SDL_MOUSEBUTTONUP) {
            cfv_map_set(ev.map,"x",cfv_num(e.button.x));
            cfv_map_set(ev.map,"y",cfv_num(e.button.y));
            cfv_map_set(ev.map,"boton",cfv_num(e.button.button));
        } else if (e.type==SDL_QUIT) {
            cfv_map_set(ev.map,"salir",cfv_bool(1));
        }
        cfv_lst_push(lista.lst,ev);
    }
    return lista;
}
static CfvVal cfv_f_sdl_cargar_textura(CfvVal rid, CfvVal ruta) {
    int id = _cfv_obj_next++;
    _cfv_texs[id] = IMG_LoadTexture(_cfv_rens[(int)cfv_a_numero(rid).n], cfv_as_cstr(ruta));
    return cfv_num(id);
}
static CfvVal cfv_f_sdl_dibujar_textura(CfvVal rid, CfvVal tid,
    CfvVal dx, CfvVal dy, CfvVal dw, CfvVal dh, CfvVal ang) {
    SDL_Rect dst={(int)cfv_a_numero(dx).n,(int)cfv_a_numero(dy).n,
                  (int)cfv_a_numero(dw).n,(int)cfv_a_numero(dh).n};
    double a=cfv_a_numero(ang).n;
    SDL_Renderer* ren=_cfv_rens[(int)cfv_a_numero(rid).n];
    SDL_Texture*  tex=_cfv_texs[(int)cfv_a_numero(tid).n];
    if (a!=0.0) SDL_RenderCopyEx(ren,tex,NULL,&dst,a,NULL,SDL_FLIP_NONE);
    else        SDL_RenderCopy(ren,tex,NULL,&dst);
    return cfv_nulo();
}
static CfvVal cfv_f_sdl_destruir_textura(CfvVal tid) {
    int id=(int)cfv_a_numero(tid).n;
    if(_cfv_texs[id]){SDL_DestroyTexture(_cfv_texs[id]);_cfv_texs[id]=NULL;}
    return cfv_nulo();
}

#endif /* CFV_SDL2 */

#endif /* CFV_RT_H */
