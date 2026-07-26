#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

typedef struct CfvNumberSlice {
    const double *data;
    unsigned long long length;
} CfvNumberSlice;

typedef void (*CfvReleaseFunction)(void *owner);
typedef struct CfvOwnedNumberList {
    const double *data;
    unsigned long long length;
    void *owner;
    CfvReleaseFunction release;
} CfvOwnedNumberList;

typedef struct CfvNumberMapView {
    const char *const *keys;
    const double *values;
    unsigned long long length;
} CfvNumberMapView;

typedef struct CfvAbiValue {
    int type;
    long long integer;
    double decimal;
    const char *text;
    void *owner;
    CfvReleaseFunction release;
} CfvAbiValue;
typedef struct CfvRecordField { const char *name; CfvAbiValue value; } CfvRecordField;
typedef struct CfvRecordView {
    const char *type_name;
    const CfvRecordField *fields;
    unsigned long long field_count;
} CfvRecordView;

static void native_release_numbers(void *owner) { free(owner); }

double native_add(double a, double b) {
    return a + b;
}

const char *native_echo(const char *value) {
    return value;
}

bool native_not(bool value) {
    return !value;
}

double native_sum(const CfvNumberSlice *values) {
    double total = 0.0;
    if (!values || (!values->data && values->length != 0)) return 0.0;
    for (unsigned long long index = 0; index < values->length; ++index) {
        total += values->data[index];
    }
    return total;
}

double native_map_total(const CfvNumberMapView *values) {
    double total = 0.0;
    if (!values) return 0.0;
    for (unsigned long long index = 0; index < values->length; ++index) {
        if (!values->keys[index]) return 0.0;
        total += values->values[index];
    }
    return total;
}

double native_person_score(const CfvRecordView *person) {
    if (!person || person->field_count != 3) return -1.0;
    return person->fields[1].value.decimal + person->fields[2].value.integer;
}

int native_range(double count, CfvOwnedNumberList *output, const char **error) {
    if (count < 0 || count > 1000000) {
        *error = "native_range: tamaño inválido";
        return 1;
    }
    unsigned long long length = (unsigned long long)count;
    double *values = length ? (double *)malloc(sizeof(double) * length) : NULL;
    if (length && !values) { *error = "native_range: sin memoria"; return 2; }
    for (unsigned long long index = 0; index < length; ++index) values[index] = (double)index;
    output->data = values; output->length = length;
    output->owner = values; output->release = native_release_numbers;
    *error = NULL;
    return 0;
}

int native_divide(double a, double b, double *output, const char **error) {
    if (b == 0.0) {
        *error = "native_divide: divisor cero";
        return 1;
    }
    *output = a / b;
    *error = 0;
    return 0;
}
