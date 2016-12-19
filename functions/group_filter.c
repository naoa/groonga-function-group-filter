/*
  Copyright(C) 2016 Naoya Murakami <naoya@createfield.com>

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License version 2.1 as published by the Free Software Foundation.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <groonga/plugin.h>

#ifdef __GNUC__
# define GNUC_UNUSED __attribute__((__unused__))
#else
# define GNUC_UNUSED
#endif

#define GRN_GROUP_FILTER_SUM "#group_filter_sum"
#define GRN_GROUP_FILTER_SUM_LEN 17

#define GRN_GROUP_FILTER_SUM_SORT_DESC "-#group_filter_sum"
#define GRN_GROUP_FILTER_SUM_SORT_DESC_LEN 18

#define GRN_GROUP_FILTER_FROM_SYNONYMS "#group_filter_from_synonyms"
#define GRN_GROUP_FILTER_FROM_SYNONYMS_LEN 27

#define GRN_GROUP_FILTER_TO_SYNONYM "#group_filter_to_synonyms"
#define GRN_GROUP_FILTER_TO_SYNONYM_LEN 25

typedef struct {
  grn_obj *table;
  grn_obj *target_column;

  grn_obj *group_result;
  grn_obj *key_type;
  grn_obj *id_accessor;
  grn_obj *key_accessor;
  grn_obj *nsubrecs_accessor;

  grn_obj *count_table;
  grn_obj *sum_column;
  grn_obj *from_synonyms_column;
  grn_obj *to_synonym_column;
  grn_obj *top_n_table;
} group_counter;

static void
group_counter_fin(grn_ctx *ctx, group_counter *g)
{
  g->group_result = NULL;
  g->key_type = NULL;
  if (g->sum_column) {
    grn_obj_unlink(ctx, g->sum_column);
    g->sum_column = NULL;
  }
  if (g->from_synonyms_column) {
    grn_obj_unlink(ctx, g->from_synonyms_column);
    g->from_synonyms_column = NULL;
  }
  if (g->to_synonym_column) {
    grn_obj_unlink(ctx, g->to_synonym_column);
    g->to_synonym_column = NULL;
  }
  if (g->count_table) {
    grn_obj_unlink(ctx, g->count_table);
    g->count_table = NULL;
  }
  if (g->top_n_table) {
    grn_obj_unlink(ctx, g->top_n_table);
    g->top_n_table = NULL;
  }
  if (g->nsubrecs_accessor) {
    grn_obj_unlink(ctx, g->nsubrecs_accessor);
    g->nsubrecs_accessor = NULL;
  }
  if (g->id_accessor) {
    grn_obj_unlink(ctx, g->id_accessor);
    g->id_accessor = NULL;
  }
  if (g->key_accessor) {
    grn_obj_unlink(ctx, g->key_accessor);
    g->key_accessor = NULL;
  }
  if (g->target_column) {
    grn_obj_unlink(ctx, g->target_column);
    g->target_column = NULL;
  }
  g->table = NULL;
}

static grn_rc
group_counter_init(grn_ctx *ctx, group_counter *g, grn_obj *group_result, grn_obj *table, grn_obj *column_name)
{
  grn_rc rc = GRN_SUCCESS;

  g->group_result = group_result;
  g->key_type = grn_ctx_at(ctx, group_result->header.domain);
  g->id_accessor = NULL;
  g->key_accessor = NULL;
  g->nsubrecs_accessor = NULL;

  g->table = NULL;
  g->target_column = NULL;

  g->top_n_table = NULL;
  g->count_table = grn_table_create(ctx, NULL, 0, NULL, GRN_OBJ_TABLE_HASH_KEY,
                                    g->key_type, NULL);
  /* can't use _value because if it can't found value accessor if key_type is table */
  if (!g->count_table) {
    rc = GRN_NO_MEMORY_AVAILABLE;
    GRN_PLUGIN_ERROR(ctx, GRN_NO_MEMORY_AVAILABLE,
                     "group_filter(): couldn't create table");
    goto exit;
  }
  g->sum_column = grn_column_create(ctx, g->count_table,
                                    GRN_GROUP_FILTER_SUM,
                                    GRN_GROUP_FILTER_SUM_LEN,
                                    NULL,
                                    GRN_OBJ_COLUMN_SCALAR,
                                    grn_ctx_at(ctx, GRN_DB_UINT64));

  if (!g->sum_column) {
    rc = GRN_NO_MEMORY_AVAILABLE;
    GRN_PLUGIN_ERROR(ctx, GRN_NO_MEMORY_AVAILABLE,
                     "group_filter(): couldn't open column");
    goto exit;
  }

  g->from_synonyms_column = grn_column_create(ctx, g->count_table,
                                         GRN_GROUP_FILTER_FROM_SYNONYMS,
                                         GRN_GROUP_FILTER_FROM_SYNONYMS_LEN,
                                         NULL,
                                         GRN_OBJ_COLUMN_VECTOR,
                                         g->key_type);

  if (!g->from_synonyms_column) {
    rc = GRN_NO_MEMORY_AVAILABLE;
    GRN_PLUGIN_ERROR(ctx, GRN_NO_MEMORY_AVAILABLE,
                     "group_filter(): couldn't open column");
    goto exit;
  }

  g->to_synonym_column = grn_column_create(ctx, g->count_table,
                                           GRN_GROUP_FILTER_TO_SYNONYM,
                                           GRN_GROUP_FILTER_TO_SYNONYM_LEN,
                                           NULL,
                                           GRN_OBJ_COLUMN_SCALAR,
                                           g->key_type);

  if (!g->to_synonym_column) {
    rc = GRN_NO_MEMORY_AVAILABLE;
    GRN_PLUGIN_ERROR(ctx, GRN_NO_MEMORY_AVAILABLE,
                     "group_filter(): couldn't open column");
    goto exit;
  }

  g->nsubrecs_accessor = grn_obj_column(ctx, group_result,
                                        GRN_COLUMN_NAME_NSUBRECS,
                                        GRN_COLUMN_NAME_NSUBRECS_LEN);
  if (!g->nsubrecs_accessor) {
    rc = GRN_NO_MEMORY_AVAILABLE;
    GRN_PLUGIN_ERROR(ctx, GRN_NO_MEMORY_AVAILABLE,
                     "group_filter(): couldn't open column");
    goto exit;
  }

  g->id_accessor = grn_obj_column(ctx, group_result,
                                  GRN_COLUMN_NAME_ID,
                                  GRN_COLUMN_NAME_ID_LEN);
  if (!g->id_accessor) {
    rc = GRN_NO_MEMORY_AVAILABLE;
    GRN_PLUGIN_ERROR(ctx, GRN_NO_MEMORY_AVAILABLE,
                     "group_filter(): couldn't open column");
    goto exit;
  }

  g->key_accessor = grn_obj_column(ctx, group_result,
                                   GRN_COLUMN_NAME_KEY,
                                   GRN_COLUMN_NAME_KEY_LEN);
  if (!g->key_accessor) {
    rc = GRN_NO_MEMORY_AVAILABLE;
    GRN_PLUGIN_ERROR(ctx, GRN_NO_MEMORY_AVAILABLE,
                     "group_filter(): couldn't open column");
    goto exit;
  }

  g->table = table;
  g->target_column = grn_obj_column(ctx, table,
                                    GRN_TEXT_VALUE(column_name), GRN_TEXT_LEN(column_name));
  if (!g->target_column) {
    rc = GRN_INVALID_ARGUMENT;
    GRN_PLUGIN_ERROR(ctx, GRN_INVALID_ARGUMENT,
                      "group_filter(): couldn't open column");
    goto exit;
  }

exit :
  if (rc != GRN_SUCCESS) {
    group_counter_fin(ctx, g);
  }

  return rc;
}

static grn_rc
group_counter_load(grn_ctx *ctx, group_counter *g, const char *expr_str, unsigned int expr_str_len)
{
  grn_rc rc = GRN_SUCCESS;
  grn_obj record;
  grn_obj synonyms;
  grn_obj id_buf;
  grn_obj nsubrecs_buf;
  grn_obj *expression = NULL;
  grn_obj *expr_record = NULL;

  if (expr_str) {
    GRN_EXPR_CREATE_FOR_QUERY(ctx, g->group_result, expression, expr_record);
    if (!expression) {
      rc = ctx->rc;
      GRN_PLUGIN_ERROR(ctx,
                       GRN_INVALID_ARGUMENT,
                       "group_filter(): failed to create expression to compute value: %s",
                       ctx->errbuf);
      goto exit;
    }
    grn_expr_parse(ctx,
                   expression,
                   expr_str,
                   expr_str_len,
                   NULL,
                   GRN_OP_MATCH,
                   GRN_OP_AND,
                   GRN_EXPR_SYNTAX_SCRIPT);

    if (ctx->rc != GRN_SUCCESS) {
      GRN_PLUGIN_ERROR(ctx,
                       GRN_INVALID_ARGUMENT,
                       "group_filter(): failed to parse value: %s",
                       ctx->errbuf);
      goto exit;
    }
  }

  GRN_RECORD_INIT(&synonyms, GRN_OBJ_VECTOR, g->group_result->header.domain);
  GRN_RECORD_INIT(&record, 0, g->group_result->header.domain);
  GRN_UINT32_INIT(&nsubrecs_buf, 0);
  GRN_UINT32_INIT(&id_buf, 0);

  if (grn_obj_is_table(ctx, g->key_type)) {
    int added;
    grn_obj *value;
    GRN_HASH_EACH_BEGIN(ctx, (grn_hash *)g->group_result, cursor, id) {
      grn_id record_id = GRN_ID_NIL;
      grn_id group_id;
      grn_id expr_id = GRN_ID_NIL;
      GRN_BULK_REWIND(&synonyms);
      GRN_BULK_REWIND(&nsubrecs_buf);
      GRN_BULK_REWIND(&id_buf);
      grn_obj_get_value(ctx, g->nsubrecs_accessor, id, &nsubrecs_buf);
      grn_obj_get_value(ctx, g->id_accessor, id, &id_buf);
      group_id = GRN_UINT32_VALUE(&id_buf);

      if (expression) {
        GRN_RECORD_SET(ctx, expr_record, id);
        value = grn_expr_exec(ctx, expression, 0);
        expr_id = grn_table_get(ctx, g->key_type, GRN_BULK_HEAD(value), GRN_BULK_VSIZE(value));
      }
      if (expr_id != GRN_ID_NIL) {
        record_id = grn_table_add(ctx, g->count_table,
                                  &expr_id, sizeof(grn_id), &added);
      } else if (group_id != GRN_ID_NIL) {
        record_id = grn_table_add(ctx, g->count_table,
                                  &group_id, sizeof(grn_id), &added);
      }
      if (record_id != GRN_ID_NIL) {
        if (added) {
          grn_obj_set_value(ctx, g->sum_column, record_id, &nsubrecs_buf, GRN_OBJ_SET);
        } else {
          grn_obj_set_value(ctx, g->sum_column, record_id, &nsubrecs_buf, GRN_OBJ_INCR);
        }
        if (expr_id != GRN_ID_NIL && expr_id != group_id) {
          grn_id from_id;
          grn_obj_get_value(ctx, g->from_synonyms_column, record_id, &synonyms);
          GRN_RECORD_PUT(ctx, &synonyms, group_id);
          grn_obj_set_value(ctx, g->from_synonyms_column, record_id, &synonyms, GRN_OBJ_SET);

          GRN_BULK_REWIND(&record);
          GRN_RECORD_SET(ctx, &record, expr_id);
          from_id = grn_table_add(ctx, g->count_table, &group_id, sizeof(grn_id), NULL);
          if (from_id) {
            grn_obj_set_value(ctx, g->to_synonym_column, from_id, &record, GRN_OBJ_SET);
          }
        }
      }
    } GRN_HASH_EACH_END(ctx, cursor);
  } else {
    int added;
    grn_obj *value;
    GRN_HASH_EACH_BEGIN(ctx, (grn_hash *)g->group_result, cursor, id) {
      grn_id record_id = GRN_ID_NIL;
      GRN_BULK_REWIND(&record);
      GRN_BULK_REWIND(&nsubrecs_buf);
      grn_obj_get_value(ctx, g->nsubrecs_accessor, id, &nsubrecs_buf);
      grn_obj_get_value(ctx, g->key_accessor, id, &record);

      if (expression) {
        GRN_RECORD_SET(ctx, expr_record, id);
        value = grn_expr_exec(ctx, expression, 0);
        record_id = grn_table_add(ctx,g->count_table, 
                                  GRN_BULK_HEAD(value),
                                  GRN_BULK_VSIZE(value), &added);
      } else {
        record_id = grn_table_add(ctx,g->count_table, 
                                  GRN_BULK_HEAD(&record),
                                  GRN_BULK_VSIZE(&record), &added);
      }
      if (record_id != GRN_ID_NIL) {
        if (added) {
          grn_obj_set_value(ctx, g->sum_column, record_id, &nsubrecs_buf, GRN_OBJ_SET);
        } else {
          grn_obj_set_value(ctx, g->sum_column, record_id, &nsubrecs_buf, GRN_OBJ_INCR);
        }
      }
    } GRN_HASH_EACH_END(ctx, cursor);
  }
  GRN_OBJ_FIN(ctx, &record);
  GRN_OBJ_FIN(ctx, &synonyms);
  GRN_OBJ_FIN(ctx, &id_buf);
  GRN_OBJ_FIN(ctx, &nsubrecs_buf);

exit :
  if (expression) {
    grn_obj_close(ctx, expression);
  }
  if (expr_record) {
    grn_obj_unlink(ctx, expr_record);
  }
  return rc;
}

static grn_rc
group_counter_sort_and_slice(grn_ctx *ctx, group_counter *g, unsigned int top_n)
{
  grn_rc rc = GRN_SUCCESS;
  grn_table_sort_key *sort_keys;
  uint32_t n_sort_keys;

  sort_keys = grn_table_sort_key_from_str(ctx,
                                          GRN_GROUP_FILTER_SUM_SORT_DESC,
                                          GRN_GROUP_FILTER_SUM_SORT_DESC_LEN,
                                          g->count_table, &n_sort_keys);
  if (sort_keys) {
    grn_obj *sorted;
    sorted = grn_table_create(ctx, NULL, 0, NULL, GRN_OBJ_TABLE_NO_KEY,
                              NULL, g->count_table);
    if (sorted) {
      grn_table_sort(ctx, g->count_table, 0, top_n,
                     sorted, sort_keys, n_sort_keys);

      g->top_n_table = grn_table_create(ctx, NULL, 0, NULL, GRN_OBJ_TABLE_HASH_KEY,
                                         g->key_type, NULL);
      if (!g->top_n_table) {
        rc = GRN_NO_MEMORY_AVAILABLE;
        GRN_PLUGIN_ERROR(ctx, GRN_NO_MEMORY_AVAILABLE,
                          "group_filter(): couldn't create table");
        goto exit;
      }

      {
        if (grn_obj_is_table(ctx, g->key_type)) {
          //key accessor too dig to real bulk
          unsigned int i;
          grn_obj *id_accessor = NULL;
          grn_obj id_buf;
          grn_obj synonyms;
          grn_obj record;
          GRN_UINT32_INIT(&id_buf, 0);
          GRN_RECORD_INIT(&synonyms, GRN_OBJ_VECTOR, g->group_result->header.domain);
          GRN_RECORD_INIT(&record, 0, g->group_result->header.domain);
          id_accessor = grn_obj_column(ctx, sorted,
                                       GRN_COLUMN_NAME_ID,
                                       GRN_COLUMN_NAME_ID_LEN);
          if (id_accessor) {
            GRN_ARRAY_EACH(ctx, (grn_array *)sorted, 0, 0, id, NULL, {
              grn_id group_id;
              grn_id record_id;
              GRN_BULK_REWIND(&id_buf);
              grn_obj_get_value(ctx, id_accessor, id, &id_buf);
              group_id = GRN_UINT32_VALUE(&id_buf);
              grn_table_add(ctx, g->top_n_table, &group_id, sizeof(grn_id), NULL);
              record_id = grn_table_get(ctx, g->count_table, &group_id, sizeof(grn_id));
              if (record_id != GRN_ID_NIL) {
                GRN_BULK_REWIND(&synonyms);
                grn_obj_get_value(ctx, g->from_synonyms_column, record_id, &synonyms);
                for (i = 0; i < grn_vector_size(ctx, &synonyms); i++) {
                  grn_id from_group_id;
                  from_group_id = GRN_RECORD_VALUE_AT(&synonyms, i);
                  grn_table_add(ctx, g->top_n_table, &from_group_id, sizeof(grn_id), NULL);
                }
              }
            });
            grn_obj_unlink(ctx, id_accessor);
          }
          GRN_OBJ_FIN(ctx, &id_buf);
          GRN_OBJ_FIN(ctx, &synonyms);
          GRN_OBJ_FIN(ctx, &record);
        } else {
          grn_obj *key_accessor = NULL;
          grn_obj record;
          GRN_RECORD_INIT(&record, 0, g->group_result->header.domain);
          key_accessor = grn_obj_column(ctx, sorted,
                                        GRN_COLUMN_NAME_KEY,
                                        GRN_COLUMN_NAME_KEY_LEN);
          if (key_accessor) {
            GRN_ARRAY_EACH(ctx, (grn_array *)sorted, 0, 0, id, NULL, {
              GRN_BULK_REWIND(&record);
              grn_obj_get_value(ctx, key_accessor, id, &record);
              grn_table_add(ctx, g->top_n_table,
                            GRN_BULK_HEAD(&record),
                            GRN_BULK_VSIZE(&record), NULL);
            });
            grn_obj_unlink(ctx, key_accessor);
          }
          GRN_OBJ_FIN(ctx, &record);
        }
      }
exit : 
      grn_obj_unlink(ctx, sorted);
    }
    grn_table_sort_key_close(ctx, sort_keys, n_sort_keys);
  }
  return rc;
}

static grn_rc
group_counter_select_with_top_n_group_records(grn_ctx *ctx, group_counter *g,
                                              grn_obj *res, grn_operator op)
{
  grn_rc rc = GRN_SUCCESS;
  grn_obj *filter_proc = NULL;
  grn_obj *expr = NULL;
  grn_obj *expr_record = NULL;
  int n_values = 0;

  filter_proc = grn_ctx_get(ctx, "tag_search", strlen("tag_search"));
  if (!filter_proc) {
    filter_proc = grn_ctx_get(ctx, "in_values", strlen("in_values"));
    if (!filter_proc) {
      rc = GRN_NO_MEMORY_AVAILABLE;
      GRN_PLUGIN_ERROR(ctx, GRN_NO_MEMORY_AVAILABLE,
                       "group_filter(): couldn't open in_values proc");
      goto exit;
    }
  }

  GRN_EXPR_CREATE_FOR_QUERY(ctx, g->table, expr, expr_record);
  if (!expr || !expr_record) {
    rc = GRN_NO_MEMORY_AVAILABLE;
    GRN_PLUGIN_ERROR(ctx, GRN_NO_MEMORY_AVAILABLE,
                     "group_filter(): couldn't create expr");
    goto exit;
  }

  grn_expr_append_obj(ctx, expr, filter_proc, GRN_OP_PUSH, 1);
  grn_expr_append_obj(ctx, expr, g->target_column, GRN_OP_PUSH, 1);

  {
    grn_obj record;
    grn_obj *key_accessor = grn_obj_column(ctx, g->top_n_table,
                                           GRN_COLUMN_NAME_KEY,
                                           GRN_COLUMN_NAME_KEY_LEN);

    GRN_RECORD_INIT(&record, 0, g->group_result->header.domain);
    GRN_HASH_EACH_BEGIN(ctx, (grn_hash *)g->top_n_table, cursor, id) {
      GRN_BULK_REWIND(&record);
      grn_obj_get_value(ctx, key_accessor, id, &record);
      grn_expr_append_const(ctx, expr, &record, GRN_OP_PUSH, 1);
      n_values++;
    } GRN_HASH_EACH_END(ctx, cursor);

    grn_expr_append_op(ctx, expr, GRN_OP_CALL, n_values + 1);

    grn_table_select(ctx, g->table, expr, res, op);
    if (ctx->rc != GRN_SUCCESS) {
      rc = ctx->rc;
      GRN_PLUGIN_ERROR(ctx,
                       GRN_INVALID_ARGUMENT,
                       "group_filter(): failed to execute filter: %s",
                       ctx->errbuf);
    }
    GRN_OBJ_FIN(ctx, &record);
    grn_obj_unlink(ctx, key_accessor);
  }

exit :
  if (expr) {
    grn_obj_unlink(ctx, expr);
  }
  if (expr_record) {
    grn_obj_unlink(ctx, expr_record);
  }
  if (filter_proc) {
    grn_obj_unlink(ctx, filter_proc);
  }
  return rc;
}

/* make temp column has only top n records with sequential for vector column
    if res size is large, maybe it should be use ii
 */

static grn_rc
group_counter_apply_temp_column(grn_ctx *ctx, group_counter *g,
                                grn_obj *res, grn_obj *column_name)
{
  grn_rc rc = GRN_SUCCESS;
  grn_obj *group_column;
  grn_obj *temp_group_column;
  grn_obj temp_group_column_name;
  grn_obj buf;
  grn_obj write_buf;
  
  if ((g->target_column->header.flags & GRN_OBJ_COLUMN_TYPE_MASK) != GRN_OBJ_COLUMN_VECTOR) {
    return rc;
  }

  group_column = grn_obj_column(ctx, res,
                                GRN_TEXT_VALUE(column_name),
                                GRN_TEXT_LEN(column_name));
  if (!group_column) {
    rc = GRN_INVALID_ARGUMENT;
    GRN_PLUGIN_ERROR(ctx, GRN_INVALID_ARGUMENT,
                     "group_filter(): couldn't open column");
    goto exit;
  }

  GRN_TEXT_INIT(&temp_group_column_name, 0);
  grn_text_printf(ctx, &temp_group_column_name,
                  "#group_%.*s",
                  (int)GRN_TEXT_LEN(column_name),
                  GRN_TEXT_VALUE(column_name));
         
  temp_group_column = grn_column_create(ctx, res,
                                        GRN_TEXT_VALUE(&temp_group_column_name),
                                        GRN_TEXT_LEN(&temp_group_column_name),
                                        NULL,
                                        g->target_column->header.flags,
                                        g->key_type);
  GRN_OBJ_FIN(ctx, &temp_group_column_name);

  if (!temp_group_column) {
    rc = GRN_INVALID_ARGUMENT;
    GRN_PLUGIN_ERROR(ctx, GRN_INVALID_ARGUMENT,
                     "group_filter(): couldn't open column");
    goto exit;
  }

  if (grn_obj_is_table(ctx, g->key_type)) {
    GRN_RECORD_INIT(&buf, GRN_OBJ_VECTOR, g->group_result->header.domain);
    GRN_RECORD_INIT(&write_buf, GRN_OBJ_VECTOR, g->group_result->header.domain);
  } else {
    GRN_OBJ_INIT(&buf, GRN_VECTOR, 0, g->group_result->header.domain);
    GRN_OBJ_INIT(&write_buf, GRN_VECTOR, 0, g->group_result->header.domain);
  }
  grn_obj id_buf;
  GRN_RECORD_INIT(&id_buf, 0, g->group_result->header.domain);

  GRN_HASH_EACH_BEGIN(ctx, (grn_hash *)res, cursor, id) {
    unsigned int i;
    GRN_BULK_REWIND(&buf);
    GRN_BULK_REWIND(&write_buf);
    grn_obj_get_value(ctx, group_column, id, &buf);
    if (grn_obj_is_table(ctx, g->key_type)) {
      for (i = 0; i < grn_vector_size(ctx, &buf); i++) {
       grn_id group_id = GRN_RECORD_VALUE_AT(&buf, i);
       grn_id record_id;
       record_id = grn_table_get(ctx, g->top_n_table, &group_id, sizeof(grn_id));
       if (record_id != GRN_ID_NIL) {
         GRN_BULK_REWIND(&id_buf);
         record_id = grn_table_get(ctx, g->count_table, &group_id, sizeof(grn_id));
         grn_obj_get_value(ctx, g->to_synonym_column, record_id, &id_buf);
         if (GRN_RECORD_VALUE(&id_buf) != GRN_ID_NIL) {
           GRN_RECORD_PUT(ctx, &write_buf, GRN_RECORD_VALUE(&id_buf));
         } else {
           GRN_RECORD_PUT(ctx, &write_buf, group_id);
         }
       }
      }
    } else {
      for (i = 0; i < grn_vector_size(ctx, &buf); i++) {
        const char *content;
        unsigned int content_length;
        content_length = grn_vector_get_element(ctx, &buf, i,
                                                &content, NULL, &g->group_result->header.domain);
        if (grn_table_get(ctx, g->top_n_table, content, content_length) != GRN_ID_NIL) {
          grn_vector_add_element(ctx, &write_buf,
                                 content, content_length,
                                 0, g->group_result->header.domain);
        }
      }
    }
    if (grn_vector_size(ctx, &write_buf) > 0) {
      grn_obj_set_value(ctx, temp_group_column, id, &write_buf, GRN_OBJ_SET);
    }
  } GRN_HASH_EACH_END(ctx, cursor);
  GRN_OBJ_FIN(ctx, &id_buf);
  GRN_OBJ_FIN(ctx, &buf);
  GRN_OBJ_FIN(ctx, &write_buf);

exit :
  if (group_column) {
    grn_obj_unlink(ctx, group_column);
  }

  return rc;
}

static grn_rc
selector_group_filter(grn_ctx *ctx, GNUC_UNUSED grn_obj *table, GNUC_UNUSED grn_obj *index,
                      GNUC_UNUSED int nargs, grn_obj **args,
                      grn_obj *res, GNUC_UNUSED grn_operator op)
{
  grn_obj *target_table = table;
  grn_obj *group_key;
  const char *expr_str = NULL;
  unsigned int expr_str_len = 0;
  uint32_t top_n = 10;
  unsigned int n_hits;
  grn_rc rc = GRN_SUCCESS;

  if (nargs < 2 || nargs > 5) {
    GRN_PLUGIN_ERROR(ctx, GRN_INVALID_ARGUMENT,
                     "group_filter(): wrong number of arguments (%d for 1..3)",
                     nargs - 1);
    return GRN_INVALID_ARGUMENT;
  }
  group_key = args[1];

  if (nargs >= 3) {
    if (!(args[2]->header.type == GRN_BULK &&
          ((args[2]->header.domain == GRN_DB_INT32)))) {
      GRN_PLUGIN_ERROR(ctx, GRN_INVALID_ARGUMENT,
                       "group_filter(): 2nd argument must be UINT");
      return GRN_INVALID_ARGUMENT;
    }
    top_n = GRN_UINT32_VALUE(args[2]);
  }
  if (nargs >= 4) {
    if (!(args[3]->header.type == GRN_BULK &&
        grn_type_id_is_text_family(ctx, args[3]->header.domain))) {
      GRN_PLUGIN_ERROR(ctx, GRN_INVALID_ARGUMENT,
                       "group_filter(): 3rd argument must be text");
      return GRN_INVALID_ARGUMENT;
    }
    expr_str = GRN_TEXT_VALUE(args[3]);
    expr_str_len = GRN_TEXT_LEN(args[3]);
  }

  n_hits = grn_table_size(ctx, res);
  if (n_hits > 0) {
    target_table = res;
  }

  {
    grn_table_sort_key *keys = NULL;
    unsigned int n_keys = 0;
    grn_table_group_result result = {0};

    result.limit = 1;
    result.flags = GRN_TABLE_GROUP_CALC_COUNT;
    result.op = 0;
    result.max_n_subrecs = 0;
    result.key_begin = 0;
    result.key_end = 0;
    result.calc_target = NULL;

    keys = grn_table_sort_key_from_str(ctx,
                                       GRN_TEXT_VALUE(group_key),
                                       GRN_TEXT_LEN(group_key),
                                       target_table, &n_keys);
    if (!keys) {
      goto exit;
    }
    result.key_end = n_keys - 1;
    if (n_keys > 1) {
      result.max_n_subrecs = 1;

    }
    grn_table_group(ctx, target_table, keys, n_keys, &result, 1);

    if (keys) {
      grn_table_sort_key_close(ctx, keys, n_keys);
    }

    if (result.table) {
      group_counter group_counter;

      if (group_counter_init(ctx, &group_counter, result.table, table, group_key) == GRN_SUCCESS) {
         rc = group_counter_load(ctx, &group_counter, expr_str, expr_str_len);
         if (rc == GRN_SUCCESS) {
           rc = group_counter_sort_and_slice(ctx, &group_counter, top_n);
         }
         if (rc == GRN_SUCCESS) {
           rc = group_counter_select_with_top_n_group_records(ctx, &group_counter, res, op);
         }
         if (rc == GRN_SUCCESS) {
           rc = group_counter_apply_temp_column(ctx, &group_counter, res, group_key);
         }
         group_counter_fin(ctx, &group_counter);
      }
    } 

    if (result.table) {
      grn_obj_unlink(ctx, result.table);
    }
  }

exit :
  
  return rc;
}

grn_rc
GRN_PLUGIN_INIT(GNUC_UNUSED grn_ctx *ctx)
{
  return GRN_SUCCESS;
}

grn_rc
GRN_PLUGIN_REGISTER(grn_ctx *ctx)
{
  {
    grn_obj *selector_proc;

    selector_proc = grn_proc_create(ctx, "group_filter", -1, GRN_PROC_FUNCTION,
                                    NULL, NULL, NULL, 0, NULL);
    grn_proc_set_selector(ctx, selector_proc, selector_group_filter);
    grn_proc_set_selector_operator(ctx, selector_proc, GRN_OP_EQUAL);
  }


  return ctx->rc;
}

grn_rc
GRN_PLUGIN_FIN(GNUC_UNUSED grn_ctx *ctx)
{
  return GRN_SUCCESS;
}
