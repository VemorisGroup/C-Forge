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
