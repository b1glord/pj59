#include "parser.h"

struct schema_node * schema_node_create(struct schema *, enum schema_type, int);
void schema_node_destroy(struct schema *, struct schema_node *);
void schema_node_print(struct schema_node *, int, char *);

struct schema_state {
    struct schema * schema;
    struct strbuf * strbuf;
    char * key;
};

int schema_state_parse(enum event_type, struct string *, void *);
int schema_state_node(struct schema *, enum event_type, char *);

struct data_state {
    struct schema * schema;
    struct schema_node * data;
    parser_cb callback;
    void * context;
};

int data_state_parse(enum event_type, struct string *, void *);
int data_state_node(struct data_state *, struct schema_node *, enum event_type, struct string *);

struct schema_node * schema_node_create(struct schema * schema, enum schema_type type, int mark) {
    int status = 0;
    struct schema_node * node;

    node = store_calloc(&schema->store, sizeof(*node));
    if(!node) {
        status = panic("failed to calloc store object");
    } else {
        node->type = type;
        node->mark = mark;
        node->map = store_malloc(&schema->store, sizeof(*node->map));
        if(!node->map) {
            status = panic("failed to malloc store object");
        } else if(map_create(node->map, (map_compare_cb) strcmp, &schema->pool)) {
            status = panic("failed to create map object");
        } else {
            node->list = NULL;
        }
    }

    return status ? NULL : node;
}

void schema_node_destroy(struct schema * schema, struct schema_node * node) {
    struct map_kv kv;

    if(node->list)
        schema_node_destroy(schema, node->list);

    kv = map_start(node->map);
    while(kv.key) {
        schema_node_destroy(schema, kv.value);
        kv = map_next(node->map);
    }

    map_destroy(node->map);
}

void schema_node_print(struct schema_node * node, int indent, char * key) {
    int i;
    struct map_kv kv;

    for(i = 0; i < indent; i++)
        fputs("  ", stdout);

    if(key)
        fprintf(stdout, "[%s]", key);

    switch(node->type) {
        case list | map | string:
            fprintf(stdout, "[list | map | string]");
            break;
        case list | map:
            fprintf(stdout, "[list | map]");
            break;
        case list | string:
            fprintf(stdout, "[list | string]");
            break;
        case map | string:
            fprintf(stdout, "[map | string]");
            break;
        case list:
            fprintf(stdout, "[list]");
            break;
        case map:
            fprintf(stdout, "[map]");
            break;
        case string:
            fprintf(stdout, "[string]");
            break;
    }

    fprintf(stdout, "[%d]\n", node->mark);

    if(node->type & map) {
        kv = map_start(node->map);
        while(kv.key) {
            schema_node_print(kv.value, indent + 1, kv.key);
            kv = map_next(node->map);
        }
    }

    if(node->type & list)
        schema_node_print(node->list, indent + 1, NULL);
}

int schema_create(struct schema * schema, size_t size) {
    int status = 0;

    if(pool_create(&schema->pool, sizeof(struct map_node), size / sizeof(struct map_node))) {
        status = panic("failed to create pool object");
    } else {
        if(store_create(&schema->store, size)) {
            status = panic("failed to create store object");
        } else {
            schema->root = NULL;

            if(status)
                store_destroy(&schema->store);
        }
        if(status)
            pool_destroy(&schema->pool);
    }

    return status;
}

void schema_destroy(struct schema * schema) {
    schema_clear(schema);
    store_destroy(&schema->store);
    pool_destroy(&schema->pool);
}

void schema_clear(struct schema * schema) {
    struct schema_node * node;

    if(schema->root) {
        node = schema->root;
        schema->root = NULL;
        schema_node_destroy(schema, node);
    }

    store_clear(&schema->store);
}

void schema_print(struct schema * schema) {
    if(schema->root)
        schema_node_print(schema->root, 0, NULL);
}

void schema_push(struct schema * schema, struct schema_node * node, int state) {
    node->state = state;
    node->next = schema->root;
    schema->root = node;
}

struct schema_node * schema_add(struct schema * schema, enum schema_type type, int mark, char * key) {
    int status = 0;
    struct schema_node * node;

    if(key) {
        if(schema->root->type & map) {
            node = map_search(schema->root->map, key);
            if(node) {
                node->type |= type;
            } else {
                node = schema_node_create(schema, type, mark);
                if(!node) {
                    status = panic("failed to create schema node object");
                } else {
                    key = store_strcpy(&schema->store, key, strlen(key));
                    if(!key) {
                        status = panic("failed to strcpy store object");
                    } else if(map_insert(schema->root->map, key, node)) {
                        status = panic("failed to insert map object");
                    }
                    if(status)
                        schema_node_destroy(schema, node);
                }
            }
        } else {
            status = panic("expected map");
        }
    } else {
        if(schema->root->type & list) {
            node = schema->root->list;
            if(node) {
                node->type |= type;
            } else {
                node = schema_node_create(schema, type, mark);
                if(!node) {
                    status = panic("failed to create schema node object");
                } else {
                    schema->root->list = node;
                }
            }
        } else {
            status = panic("expected list");
        }
    }

    return status ? NULL : node;
}

struct schema_node * schema_get(struct schema * schema, char * key) {
    int status = 0;
    struct schema_node * node;

    if(key) {
        if(schema->root->type & map) {
            node = map_search(schema->root->map, key);
            if(!node)
                status = panic("invalid key - %s", key);
        } else {
            status = panic("expected map");
        }
    } else {
        if(schema->root->type & list) {
            node = schema->root->list;
            if(!node)
                status = panic("invalid node");
        } else {
            status = panic("expected list");
        }
    }

    return status ? NULL : node;
}

int schema_reload(struct schema * schema, struct schema_markup * array) {
    int status = 0;
    struct schema_node * node;

    schema_clear(schema);

    schema->root = schema_node_create(schema, list, 0);
    if(!schema->root) {
        status = panic("failed to create schema node object");
    } else {
        schema->root->state = 0;
        while(array->level > 0 && !status) {
            while(schema->root->state >= array->level)
                schema->root = schema->root->next;

            node = schema_add(schema, array->type, array->mark, array->key);
            if(!node) {
                status = panic("failed to add schema object");
            } else if(array->type & (list | map)) {
                schema_push(schema, node, array->level);
            }

            array++;
        }

        while(schema->root->next)
            schema->root = schema->root->next;
    }

    return status;
}

int schema_update(struct schema * schema, struct schema_markup * array) {
    int status = 0;
    enum schema_type type;
    struct schema_node * node;

    schema->root->state = 0;
    while(array->level > 0 && !status) {
        while(schema->root->state >= array->level)
            schema->root = schema->root->next;

        node = schema_get(schema, array->key);
        if(!node) {
            status = panic("failed to get schema object");
        } else {
            type = node->type & array->type;
            if(type) {
                node->mark = array->mark;
                if(type & (list | map))
                    schema_push(schema, node, array->level);
            } else {
                status = panic("expected %d", array->type);
            }
        }

        array++;
    }

    while(schema->root->next)
        schema->root = schema->root->next;

    return status;
}

/*
 *  node : string
 *       | start list end
 *       | start map end
 *
 *  list : node
 *       | list node
 *
 *  map  : string node
 *       | map string node
 */

int schema_state_parse(enum event_type type, struct string * value, void * context) {
    int status = 0;

    struct schema_state * state;
    struct schema * schema;

    state = context;
    schema = state->schema;
    if(schema->root->state == list) {
        if(type == event_list_end) {
            schema->root = schema->root->next;
        } else if(schema_state_node(state->schema, type, NULL)) {
            status = panic("failed to node schema state object");
        }
    } else if(schema->root->state == map) {
        if(state->key) {
            if(schema_state_node(state->schema, type, state->key))
                status = panic("failed to node schema state object");
            state->key = NULL;
            strbuf_clear(state->strbuf);
        } else if(type == event_map_end) {
            schema->root = schema->root->next;
        } else if(type == event_scalar) {
            if(strbuf_strcpy(state->strbuf, value->string, value->length)) {
                status = panic("failed to strcpy strbuf object");
            } else {
                state->key = strbuf_char(state->strbuf);
                if(!state->key)
                    status = panic("failed to char strbuf object");
            }
        } else {
            status = panic("invalid type - %d", type);
        }
    } else {
        status = panic("invalid node state -- %d", schema->root->state);
    }

    return status;
}

int schema_state_node(struct schema * schema, enum event_type type, char * key) {
    int status = 0;
    struct schema_node * node;

    if(type == event_list_start) {
        node = schema_add(schema, list, 0, key);
        if(!node) {
            status = panic("failed to add schema object");
        } else {
            schema_push(schema, node, list);
        }
    } else if(type == event_map_start) {
        node = schema_add(schema, map, 0, key);
        if(!node) {
            status = panic("failed to add schema object");
        } else {
            schema_push(schema, node, map);
        }
    } else if(type == event_scalar) {
        node = schema_add(schema, string, 0, key);
        if(!node)
            status = panic("failed to add schema object");
    } else {
        status = panic("invalid type - %d", type);
    }

    return status;
}

int data_state_parse(enum event_type type, struct string * value, void * context) {
    int status = 0;

    struct data_state * state;
    struct schema * schema;
    struct schema_node * node;

    state = context;
    schema = state->schema;
    if(schema->root->state == list) {
        if(type == event_list_end) {
            if(state->callback(end, schema->root->mark, NULL, state->context)) {
                status = panic("failed to process end event");
            } else {
                schema->root = schema->root->next;
            }
        } else {
            node = schema_get(state->schema, NULL);
            if(!node) {
                status = panic("failed to get schema object");
            } else if(data_state_node(state, node, type, value)) {
                status = panic("failed to node parser state object");
            }
        }
    } else if(schema->root->state == map) {
        if(state->data) {
            if(data_state_node(state, state->data, type, value)) {
                status = panic("failed to node parser state object");
            } else {
                state->data = NULL;
            }
        } else if(type == event_map_end) {
            if(state->callback(end, schema->root->mark, NULL, state->context)) {
                status = panic("failed to process end event");
            } else {
                schema->root = schema->root->next;
            }
        } else if(type == event_scalar) {
            node = schema_get(state->schema, value->string);
            if(!node) {
                status = panic("failed to get schema object");
            } else {
                state->data = node;
            }
        } else {
            status = panic("invalid type - %d", type);
        }
    } else {
        status = panic("invalid node state - %d", schema->root->state);
    }

    return status;
}

int data_state_node(struct data_state * state, struct schema_node * node, enum event_type type, struct string * value) {
    int status = 0;

    if(type == event_list_start) {
        if(node->type & list) {
            if(state->callback(start, node->mark, NULL, state->context)) {
                status = panic("failed to process start event");
            } else {
                schema_push(state->schema, node, list);
            }
        } else {
            status = panic("unexpected list");
        }
    } else if(type == event_map_start) {
        if(node->type & map) {
            if(state->callback(start, node->mark, NULL, state->context)) {
                status = panic("failed to process start event");
            } else {
                schema_push(state->schema, node, map);
            }
        } else {
            status = panic("unexpected map");
        }
    } else if(type == event_scalar) {
        if(node->type & string) {
            if(state->callback(next, node->mark, value, state->context))
                status = panic("failed to process next event");
        } else {
            status = panic("unexpected string");
        }
    } else {
        status = panic("invalid type - %d", type);
    }

    return status;
}

int parser_create(struct parser * parser, size_t size, struct heap * heap) {
    int status = 0;

    parser->size = size;

    if(csv_create(&parser->csv, parser->size, heap)) {
        status = panic("failed to create csv object");
    } else {
        if(json_create(&parser->json, parser->size)) {
            status = panic("failed to create json object");
        } else {
            if(yaml_create(&parser->yaml, parser->size, heap)) {
                status = panic("failed to create yaml object");
            } else {
                if(strbuf_create(&parser->strbuf, size)) {
                    status = panic("failed to create strbuf object");
                } else {
                    if(status)
                        strbuf_destroy(&parser->strbuf);
                }
                if(status)
                    yaml_destroy(&parser->yaml);
            }
            if(status)
                json_destroy(&parser->json);
        }
        if(status)
            csv_destroy(&parser->csv);
    }

    return status;
}

void parser_destroy(struct parser * parser) {
    strbuf_destroy(&parser->strbuf);
    yaml_destroy(&parser->yaml);
    json_destroy(&parser->json);
    csv_destroy(&parser->csv);
}

int parser_schema_parse(struct parser * parser, struct schema * schema, const char * path) {
    int status = 0;

    struct schema_state state;

    state.schema = schema;
    state.strbuf = &parser->strbuf;
    state.key = NULL;

    schema_clear(schema);

    schema->root = schema_node_create(schema, list, 0);
    if(!schema->root) {
        status = panic("failed to create schema node object");
    } else {
        schema->root->state = list;

        if(parser_parse(parser, path, schema_state_parse, &state))
            status = panic("failed to parse parser object");

        while(schema->root->next)
            schema->root = schema->root->next;
    }

    return status;
}

int parser_data_parse(struct parser * parser, struct schema * schema, parser_cb callback, void * context, const char * path) {
    int status = 0;

    struct data_state state;

    state.schema = schema;
    state.data = NULL;
    state.callback = callback;
    state.context = context;

    schema->root->state = list;

    if(parser_parse(parser, path, data_state_parse, &state))
        status = panic("failed to parse parser object");

    while(schema->root->next)
        schema->root = schema->root->next;

    return status;
}

int parser_parse(struct parser * parser, const char * path, event_cb callback, void * context) {
    int status = 0;

    char * ext;

    ext = strrchr(path, '.');
    if(!ext) {
        status = panic("failed to get file extension - %s", path);
    } else {
        if(!strcmp(ext, ".txt")) {
            if(csv_parse(&parser->csv, path, parser->size, callback, context))
                status = panic("failed to parse csv object");
        } else if(!strcmp(ext, ".json")) {
            if(json_parse(&parser->json, path, parser->size, callback, context))
                status = panic("failed to parse json object");
        } else if(!strcmp(ext, ".yaml") || !strcmp(ext, ".yml")) {
            if(yaml_parse(&parser->yaml, path, parser->size, callback, context))
                status = panic("failed to parse yaml object");
        } else {
            status = panic("unsupported extension - %s", ext);
        }
    }

    return status;
}
