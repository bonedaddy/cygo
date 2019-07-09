#ifndef DATSTU_H
#define DATSTU_H

typedef struct crnmap crnmap;
typedef struct crnarray crnarray;
typedef struct crnqueue crnqueue;


crnmap* crnmap_new_uintptr();
void crnmap_free (crnmap *table);
enum cc_stat crnmap_add (crnmap *table, uintptr_t key, void *val);
enum cc_stat crnmap_get(crnmap *table, uintptr_t key, void **out);
enum cc_stat crnmap_remove(crnmap *table, uintptr_t key, void **out);
void crnmap_remove_all(crnmap *table);
bool crnmap_contains_key(crnmap *table, uintptr_t key);
size_t crnmap_size(crnmap *table);
size_t crnmap_capacity(crnmap *table);
enum cc_stat crnmap_get_keys(crnmap *table, Array **out);
enum cc_stat crnmap_get_values(crnmap *table, Array **out);

/////
crnqueue* crnqueue_new();
void crnqueu_free(crnqueue* q);
enum cc_stat crnqueue_peek(crnqueue* q, void **out);
enum cc_stat crnqueue_poll(crnqueue* q, void **out);
enum cc_stat crnqueue_enqueue(crnqueue* q, void *element);
size_t crnqueue_size(crnqueue* q);


#endif /* DATSTU_H */
