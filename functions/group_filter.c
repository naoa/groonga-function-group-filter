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

  g->target_column = NULL;
  g->table = NULL;
}

static grn_rc
group_counter_init(grn_ctx *ctx, group_counter *g, grn_obj *group_result, grn_obj *table, grn_obj *column)
{
  grn_rc rc = GRN_SUCCESS;

  g->group_result = group_result;
  g->key_type = grn_ctx_at(ctx, group_result->header.domain);

  g->table = NULL;
  g->target_column = column;

  g->top_n_table = NULL;
  g->count_table = grn_table_create(ctx, NULL, 0, NULL, GRN_OBJ_TABLE_HASH_KEY,
                                    g->key_type, NULL);
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

  g->table = table;

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
  grn_obj *id_accessor = NULL;
  grn_obj *key_accessor = NULL;
  grn_obj *target_nrecs_accessor = NULL;

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

  id_accessor = grn_obj_column(ctx, g->group_result,
                               GRN_COLUMN_NAME_ID,
                               GRN_COLUMN_NAME_ID_LEN);
  if (!id_accessor) {
    rc = GRN_NO_MEMORY_AVAILABLE;
    GRN_PLUGIN_ERROR(ctx, GRN_NO_MEMORY_AVAILABLE,
                     "group_filter(): couldn't open column");
    goto exit;
  }

  key_accessor = grn_obj_column(ctx, g->group_result,
                                GRN_COLUMN_NAME_KEY,
                                GRN_COLUMN_NAME_KEY_LEN);
  if (!key_accessor) {
    rc = GRN_NO_MEMORY_AVAILABLE;
    GRN_PLUGIN_ERROR(ctx, GRN_NO_MEMORY_AVAILABLE,
                     "group_filter(): couldn't open column");
    goto exit;
  }

  if (g->target_column->header.flags & GRN_OBJ_WITH_WEIGHT) {
    target_nrecs_accessor = grn_obj_column(ctx, g->group_result,
                                           GRN_COLUMN_NAME_SUM,
                                           GRN_COLUMN_NAME_SUM_LEN);
  } else {
    target_nrecs_accessor = grn_obj_column(ctx, g->group_result,
                                           GRN_COLUMN_NAME_NSUBRECS,
                                           GRN_COLUMN_NAME_NSUBRECS_LEN);
  }
  if (!target_nrecs_accessor) {
    rc = GRN_NO_MEMORY_AVAILABLE;
    GRN_PLUGIN_ERROR(ctx, GRN_NO_MEMORY_AVAILABLE,
                     "group_filter(): couldn't open column");
    goto exit;
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
      grn_obj_get_value(ctx, target_nrecs_accessor, id, &nsubrecs_buf);
      grn_obj_get_value(ctx, id_accessor, id, &id_buf);
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
      grn_obj_get_value(ctx, target_nrecs_accessor, id, &nsubrecs_buf);
      grn_obj_get_value(ctx, key_accessor, id, &record);

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
  if (id_accessor) {
    grn_obj_unlink(ctx, id_accessor);
  }
  if (key_accessor) {
    grn_obj_unlink(ctx, key_accessor);
  }
  if (target_nrecs_accessor) {
    grn_obj_unlink(ctx, target_nrecs_accessor);
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
select_with_target_records(grn_ctx *ctx, grn_obj *table, grn_obj *column,
                           grn_obj *target_records_table, grn_id range_id,
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

  GRN_EXPR_CREATE_FOR_QUERY(ctx, table, expr, expr_record);
  if (!expr || !expr_record) {
    rc = GRN_NO_MEMORY_AVAILABLE;
    GRN_PLUGIN_ERROR(ctx, GRN_NO_MEMORY_AVAILABLE,
                     "group_filter(): couldn't create expr");
    goto exit;
  }

  grn_expr_append_obj(ctx, expr, filter_proc, GRN_OP_PUSH, 1);
  grn_expr_append_obj(ctx, expr, column, GRN_OP_PUSH, 1);

  {
    grn_obj record;
    grn_obj *key_accessor = grn_obj_column(ctx, target_records_table,
                                           GRN_COLUMN_NAME_KEY,
                                           GRN_COLUMN_NAME_KEY_LEN);

    GRN_RECORD_INIT(&record, 0, range_id);
    GRN_HASH_EACH_BEGIN(ctx, (grn_hash *)target_records_table, cursor, id) {
      GRN_BULK_REWIND(&record);
      grn_obj_get_value(ctx, key_accessor, id, &record);
      grn_expr_append_const(ctx, expr, &record, GRN_OP_PUSH, 1);
      n_values++;
    } GRN_HASH_EACH_END(ctx, cursor);

    grn_expr_append_op(ctx, expr, GRN_OP_CALL, n_values + 1);

    if (n_values > 0) {
      grn_table_select(ctx, table, expr, res, op);
      if (ctx->rc != GRN_SUCCESS) {
        rc = ctx->rc;
        GRN_PLUGIN_ERROR(ctx,
                         GRN_INVALID_ARGUMENT,
                         "group_filter(): failed to execute filter: %s",
                         ctx->errbuf);
      }
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

static grn_rc
group_counter_select_with_top_n_group_records(grn_ctx *ctx, group_counter *g,
                                              grn_obj *res, grn_operator op)
{
  return select_with_target_records(ctx, g->table, g->target_column,
                                    g->top_n_table, g->group_result->header.domain, res, op);
}

/* make temp column has only top n records with sequential for vector column
    if res size is large, maybe it should be use ii
 */
static grn_rc
apply_temp_column(grn_ctx *ctx, grn_obj *column, grn_obj *range,
                  grn_obj *target_records_table,
                  grn_obj *synonym_table,
                  grn_obj *to_synonym_column,
                  grn_obj *res)
{
  grn_rc rc = GRN_SUCCESS;
  grn_obj *group_column;
  grn_obj *temp_group_column;
  grn_obj temp_group_column_name;
  grn_obj buf;
  grn_obj write_buf;
  char column_name[GRN_TABLE_MAX_KEY_SIZE];
  int column_name_len = grn_column_name(ctx,
                                        column,
                                        column_name,
                                        GRN_TABLE_MAX_KEY_SIZE);
  grn_id range_id = grn_obj_id(ctx, range);
  if ((column->header.flags & GRN_OBJ_COLUMN_TYPE_MASK) != GRN_OBJ_COLUMN_VECTOR) {
    return rc;
  }

  group_column = grn_obj_column(ctx, res,
                                column_name,
                                column_name_len);
  if (!group_column) {
    rc = GRN_INVALID_ARGUMENT;
    GRN_PLUGIN_ERROR(ctx, GRN_INVALID_ARGUMENT,
                     "group_filter(): couldn't open column");
    goto exit;
  }

  GRN_TEXT_INIT(&temp_group_column_name, 0);
  grn_text_printf(ctx, &temp_group_column_name,
                  "#group_%.*s",
                  column_name_len,
                  column_name);
         
  temp_group_column = grn_obj_column(ctx, res,
                                     GRN_TEXT_VALUE(&temp_group_column_name),
                                     GRN_TEXT_LEN(&temp_group_column_name));
  if (temp_group_column) {
    GRN_BULK_REWIND(&temp_group_column_name);
    grn_text_printf(ctx, &temp_group_column_name,
                    "#group_%.*s_2",
                    column_name_len,
                    column_name);
  }
 
  temp_group_column = grn_column_create(ctx, res,
                                        GRN_TEXT_VALUE(&temp_group_column_name),
                                        GRN_TEXT_LEN(&temp_group_column_name),
                                        NULL,
                                        column->header.flags,
                                        range);
  GRN_OBJ_FIN(ctx, &temp_group_column_name);

  if (!temp_group_column) {
    rc = GRN_INVALID_ARGUMENT;
    GRN_PLUGIN_ERROR(ctx, GRN_INVALID_ARGUMENT,
                     "group_filter(): couldn't open column");
    goto exit;
  }

  if (grn_obj_is_table(ctx, range)) {
    GRN_RECORD_INIT(&buf, GRN_OBJ_VECTOR, range_id);
    GRN_RECORD_INIT(&write_buf, GRN_OBJ_VECTOR, range_id);
  } else {
    GRN_OBJ_INIT(&buf, GRN_VECTOR, 0, range_id);
    GRN_OBJ_INIT(&write_buf, GRN_VECTOR, 0, range_id);
  }
  if ((column->header.flags & GRN_OBJ_WITH_WEIGHT)) {
    buf.header.flags |= GRN_OBJ_WITH_WEIGHT;
    write_buf.header.flags |= GRN_OBJ_WITH_WEIGHT;
  }

  grn_obj id_buf;
  GRN_RECORD_INIT(&id_buf, 0, range_id);

  GRN_HASH_EACH_BEGIN(ctx, (grn_hash *)res, cursor, id) {
    unsigned int i;
    GRN_BULK_REWIND(&buf);
    GRN_BULK_REWIND(&write_buf);
    grn_obj_get_value(ctx, group_column, id, &buf);
    if (grn_obj_is_table(ctx, range)) {
      for (i = 0; i < grn_vector_size(ctx, &buf); i++) {
        grn_id group_id;
        grn_id record_id;
        unsigned int weight;
        group_id = grn_uvector_get_element(ctx, &buf, i, &weight);
        record_id = grn_table_get(ctx, target_records_table, &group_id, sizeof(grn_id));
        if (record_id != GRN_ID_NIL) {
          if (synonym_table && to_synonym_column) {
            GRN_BULK_REWIND(&id_buf);
            record_id = grn_table_get(ctx, synonym_table, &group_id, sizeof(grn_id));
            if (record_id) {
              grn_obj_get_value(ctx, to_synonym_column, record_id, &id_buf);
              if (GRN_RECORD_VALUE(&id_buf) != GRN_ID_NIL) {
                grn_uvector_add_element(ctx, &write_buf, GRN_RECORD_VALUE(&id_buf), weight);
              } else {
                grn_uvector_add_element(ctx, &write_buf, group_id, weight);
              }
            } else {
              grn_uvector_add_element(ctx, &write_buf, group_id, weight);
            }
          } else {
            grn_uvector_add_element(ctx, &write_buf, group_id, weight);
          }
        }
      }
    } else {
      for (i = 0; i < grn_vector_size(ctx, &buf); i++) {
        const char *content;
        unsigned int content_length;
        content_length = grn_vector_get_element(ctx, &buf, i,
                                                &content, NULL, &range_id);
        if (grn_table_get(ctx, target_records_table, content, content_length) != GRN_ID_NIL) {
          grn_vector_add_element(ctx, &write_buf,
                                 content, content_length,
                                 0, range_id);
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
group_counter_apply_temp_column(grn_ctx *ctx, group_counter *g, grn_obj *res) {
  return apply_temp_column(ctx, g->target_column, g->key_type,
                           g->top_n_table, g->count_table, g->to_synonym_column,
                           res);
}

static grn_rc
selector_group_filter(grn_ctx *ctx, GNUC_UNUSED grn_obj *table, GNUC_UNUSED grn_obj *index,
                      GNUC_UNUSED int nargs, grn_obj **args,
                      grn_obj *res, GNUC_UNUSED grn_operator op)
{
  grn_obj *target_table = table;
  const char *expr_str = NULL;
  unsigned int expr_str_len = 0;
  uint32_t top_n = 10;
  unsigned int n_hits;
  grn_rc rc = GRN_SUCCESS;
  grn_obj *target_column = NULL;

  if (nargs < 2 || nargs > 5) {
    GRN_PLUGIN_ERROR(ctx, GRN_INVALID_ARGUMENT,
                     "group_filter(): wrong number of arguments (%d for 1..3)",
                     nargs - 1);
    return GRN_INVALID_ARGUMENT;
  }
  target_column = args[1];

  if (!target_column) {
    GRN_PLUGIN_ERROR(ctx, GRN_INVALID_ARGUMENT,
                     "group_filter(): 1st argument coludn't find column");
    return GRN_INVALID_ARGUMENT;
  }

  if (nargs >= 3) {
    if ((args[2]->header.type == GRN_BULK &&
         ((args[2]->header.domain == GRN_DB_INT32)))) {
      top_n = GRN_UINT32_VALUE(args[2]);
    } else {
      GRN_PLUGIN_ERROR(ctx, GRN_INVALID_ARGUMENT,
                       "group_filter(): 2nd argument must be UINT");
      return GRN_INVALID_ARGUMENT;
    }
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
    char key_name[GRN_TABLE_MAX_KEY_SIZE];
    unsigned int key_name_size;

    result.limit = 1;
    result.flags = GRN_TABLE_GROUP_CALC_COUNT;
    if (target_column->header.flags & GRN_OBJ_WITH_WEIGHT) {
      result.flags |= GRN_TABLE_GROUP_CALC_SUM;
    }
    result.op = 0;
    result.max_n_subrecs = 0;
    result.key_begin = 0;
    result.key_end = 0;
    result.calc_target = NULL;

    key_name_size = grn_column_name(ctx, target_column, key_name, GRN_TABLE_MAX_KEY_SIZE);
    keys = grn_table_sort_key_from_str(ctx,
                                       key_name,
                                       key_name_size,
                                       target_table, &n_keys);
    if (!keys) {
      goto exit;
    }
    result.key_end = n_keys - 1;
    if (n_keys > 1) {
      result.max_n_subrecs = 1;

    }
    grn_table_group(ctx, target_table, keys, n_keys, &result, 1);
    rc = ctx->rc;
    if (rc != GRN_SUCCESS) {
      if (result.table) {
        grn_obj_unlink(ctx, result.table);
      }
      goto exit;
    }

    if (keys) {
      grn_table_sort_key_close(ctx, keys, n_keys);
    }

    if (result.table) {
      group_counter group_counter;

      if (group_counter_init(ctx, &group_counter, result.table, table, target_column) == GRN_SUCCESS) {
         rc = group_counter_load(ctx, &group_counter, expr_str, expr_str_len);
         if (rc == GRN_SUCCESS) {
           rc = group_counter_sort_and_slice(ctx, &group_counter, top_n);
         }
         if (rc == GRN_SUCCESS) {
           rc = group_counter_select_with_top_n_group_records(ctx, &group_counter, res, op);
         }
         if (rc == GRN_SUCCESS) {
           rc = group_counter_apply_temp_column(ctx, &group_counter, res);
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

static grn_rc
extract_keywords(grn_ctx *ctx, grn_obj *table, grn_obj *column,
                 grn_obj *values, grn_obj *keywords, size_t *n_keywords,
                 grn_obj *extract_expr, grn_obj *record)
{
  grn_rc rc = GRN_SUCCESS;
  GRN_EXPR_CREATE_FOR_QUERY(ctx, table, extract_expr, record);
  rc = grn_expr_parse(ctx,
                      extract_expr,
                      GRN_TEXT_VALUE(values),
                      GRN_TEXT_LEN(values),
                      column,
                      GRN_OP_MATCH,
                      GRN_OP_OR,
                      GRN_EXPR_SYNTAX_QUERY|GRN_EXPR_ALLOW_PRAGMA|GRN_EXPR_ALLOW_COLUMN);
  if (rc != GRN_SUCCESS) {
    GRN_PLUGIN_ERROR(ctx, GRN_NO_MEMORY_AVAILABLE,
                     "values_filter(): failed to parse query");
    return rc;
  }
  grn_expr_get_keywords(ctx, extract_expr, keywords);
  *n_keywords = GRN_BULK_VSIZE(keywords) / sizeof(grn_obj *);

  return rc;
}

static grn_rc
selector_values_filter(grn_ctx *ctx, GNUC_UNUSED grn_obj *table, GNUC_UNUSED grn_obj *index,
                       GNUC_UNUSED int nargs, grn_obj **args,
                       grn_obj *res, GNUC_UNUSED grn_operator op)
{
  grn_obj *column;
  grn_obj *values = NULL;
  grn_rc rc = GRN_SUCCESS;
  grn_obj *synonyms = NULL;

  if (nargs < 2) {
    GRN_PLUGIN_ERROR(ctx, GRN_INVALID_ARGUMENT,
                     "values_filter(): wrong number of arguments (%d for 1..3)",
                     nargs - 1);
    return GRN_INVALID_ARGUMENT;
  }
  column = args[1];

  if (nargs >= 3) {
    if ((args[2]->header.type == GRN_BULK &&
         grn_type_id_is_text_family(ctx, args[2]->header.domain))) {
      values = args[2];
    } else {
      GRN_PLUGIN_ERROR(ctx, GRN_INVALID_ARGUMENT,
                       "values_filter(): 2nd argument must be text");
      return GRN_INVALID_ARGUMENT;
    }
  }

  if (nargs >= 4) {
    if (args[3]->header.type == GRN_TABLE_HASH_KEY) {
      synonyms = args[3];
    }
  }

  if (GRN_TEXT_LEN(values) > 0) {
    grn_obj *values_table = NULL;
    grn_obj keywords;
    grn_obj *extract_expr = NULL;
    grn_obj *record;
    size_t i, n_keywords;
    grn_obj *range = NULL;

    grn_obj *synonym_table = NULL;
    grn_obj *to_synonym_column = NULL;

    if (!grn_obj_is_column(ctx, column)) {
      GRN_PLUGIN_ERROR(ctx, GRN_NO_MEMORY_AVAILABLE,
                       "values_filter(): if use string value in 2nd argument, 1st argument must be column instead of string");
      goto exit_values;
    }

    range = grn_ctx_at(ctx, grn_obj_get_range(ctx, column));

    GRN_PTR_INIT(&keywords, GRN_OBJ_VECTOR, GRN_ID_NIL);
    rc = extract_keywords(ctx, table, column,
                          values, &keywords, &n_keywords,
                          extract_expr, record);
    if (rc != GRN_SUCCESS) {
      goto exit_values;
    }

    values_table = grn_table_create(ctx, NULL, 0, NULL, GRN_OBJ_TABLE_HASH_KEY,
                                    range, NULL);
    if (!values_table) {
      rc = GRN_NO_MEMORY_AVAILABLE;
      GRN_PLUGIN_ERROR(ctx, GRN_NO_MEMORY_AVAILABLE,
                       "values_filter(): couldn't create table");
      goto exit_values;
    }

    if (synonyms) {
      synonym_table = grn_table_create(ctx, NULL, 0, NULL, GRN_OBJ_TABLE_HASH_KEY,
                                          range, NULL);
      if (!synonym_table) {
        rc = GRN_NO_MEMORY_AVAILABLE;
        GRN_PLUGIN_ERROR(ctx, GRN_NO_MEMORY_AVAILABLE,
                         "values_filter(): couldn't create synonym table");
        goto exit_values;
      }

      to_synonym_column = grn_column_create(ctx, synonym_table,
                                            GRN_GROUP_FILTER_TO_SYNONYM,
                                            GRN_GROUP_FILTER_TO_SYNONYM_LEN,
                                            NULL,
                                            GRN_OBJ_COLUMN_SCALAR,
                                            range);
      if (!to_synonym_column) {
        rc = GRN_NO_MEMORY_AVAILABLE;
        GRN_PLUGIN_ERROR(ctx, GRN_NO_MEMORY_AVAILABLE,
                         "values_filter(): couldn't open column");
        goto exit_values;
      }
    }

    if (grn_obj_is_table(ctx, range)) {
      grn_obj record;

      GRN_RECORD_INIT(&record, 0, grn_obj_id(ctx, range));
      for (i = 0; i < n_keywords; i++) {
        grn_id id;
        grn_obj *keyword;
        keyword = GRN_PTR_VALUE_AT(&keywords, i);
        id = grn_table_get(ctx, range, GRN_BULK_HEAD(keyword), GRN_BULK_VSIZE(keyword));
        if (id != GRN_ID_NIL) {
          grn_table_add(ctx, values_table, &id, sizeof(grn_id), NULL);

          if (synonyms) {
            grn_id hid;
            grn_obj *value;
            hid = grn_hash_get(ctx, (grn_hash *)synonyms, GRN_BULK_HEAD(keyword), GRN_BULK_VSIZE(keyword), (void **)&value);
            if (hid) {
              grn_id synonym_source_id;
              synonym_source_id = grn_table_get(ctx, range,
                                                GRN_BULK_HEAD(keyword),
                                                GRN_BULK_VSIZE(keyword));
              if (synonym_source_id != GRN_ID_NIL) {
                grn_obj synonym_words;
                size_t k, n_words;
                grn_obj *synonym_expr = NULL;
                grn_obj *synonym_record;

                grn_table_add(ctx, values_table, &synonym_source_id, sizeof(grn_id), NULL);

                GRN_PTR_INIT(&synonym_words, GRN_OBJ_VECTOR, GRN_ID_NIL);
                rc = extract_keywords(ctx, table, column,
                                      value, &synonym_words, &n_words,
                                      synonym_expr, synonym_record);
                if (rc != GRN_SUCCESS) {
                  GRN_OBJ_FIN(ctx, &synonym_words);
                  grn_obj_unlink(ctx, synonym_expr);
                  goto exit_values;
                }

                for (k = 0; k < n_words; k++) {
                  grn_obj *synonym_word;
                  grn_id synonym_target_id;
                  grn_id synonym_id;
                  synonym_word = GRN_PTR_VALUE_AT(&synonym_words, k);

                  synonym_target_id = grn_table_get(ctx, range,
                                                    GRN_BULK_HEAD(synonym_word),
                                                    GRN_BULK_VSIZE(synonym_word));
                  if (synonym_target_id != GRN_ID_NIL) {
                    grn_table_add(ctx, values_table, &synonym_target_id, sizeof(grn_id), NULL);

                    synonym_id = grn_table_add(ctx, synonym_table, &synonym_target_id, sizeof(grn_id), NULL);
                    if (synonym_id != GRN_ID_NIL) {
                      GRN_BULK_REWIND(&record);
                      GRN_RECORD_SET(ctx, &record, id);
                      grn_obj_set_value(ctx, to_synonym_column, synonym_id, &record, GRN_OBJ_SET);
                    }
                  }
                }
                GRN_OBJ_FIN(ctx, &synonym_words);
                grn_obj_unlink(ctx, synonym_expr);
              }
            }
          }
        }
      }
      GRN_OBJ_FIN(ctx, &record);
    } else {
      for (i = 0; i < n_keywords; i++) {
        grn_obj *keyword;
        keyword = GRN_PTR_VALUE_AT(&keywords, i);
        grn_table_add(ctx, values_table, GRN_BULK_HEAD(keyword), GRN_BULK_VSIZE(keyword), NULL);
      }
    }
    rc = select_with_target_records(ctx, table, column, values_table, grn_obj_id(ctx, range),
                                    res, op);
    if (rc == GRN_SUCCESS) {
      rc = apply_temp_column(ctx, column, range, values_table,
                             synonym_table, to_synonym_column, res);
    }

exit_values:
    if (to_synonym_column) {
      grn_obj_unlink(ctx, to_synonym_column);
    }
    if (synonym_table) {
      grn_obj_unlink(ctx, synonym_table);
    }

    if (values_table) {
      grn_obj_unlink(ctx, values_table);
    }
    if (extract_expr) {
      grn_obj_unlink(ctx, extract_expr);
    }
    GRN_OBJ_FIN(ctx, &keywords);
  }

  return rc;
}

static grn_obj *
func_is_asc_pair(grn_ctx *ctx, int n_args, grn_obj **args,
                 grn_user_data *user_data)
{
  grn_obj *value = NULL;
  grn_obj *domain1, *domain2;

  value = grn_plugin_proc_alloc(ctx, user_data, GRN_DB_BOOL, 0);
  if (!value) {
    return NULL;
  }

  if (n_args != 2) {
    GRN_BOOL_SET(ctx, value, GRN_FALSE);
    goto exit;
  }
  domain1 = grn_ctx_at(ctx, args[0]->header.domain);
  domain2 = grn_ctx_at(ctx, args[1]->header.domain);
  if (!(grn_obj_is_table(ctx, domain1) && grn_obj_is_table(ctx, domain2))) {
    GRN_BOOL_SET(ctx, value, GRN_FALSE);
    goto exit;
  }
  if (domain1 != domain2) {
    GRN_BOOL_SET(ctx, value, GRN_FALSE);
    goto exit;
  }

  if (GRN_RECORD_VALUE(args[0]) < GRN_RECORD_VALUE(args[1])) {
    GRN_BOOL_SET(ctx, value, GRN_TRUE);
  }

exit :
  return value;
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

  {
    grn_obj *selector_proc;

    selector_proc = grn_proc_create(ctx, "values_filter", -1, GRN_PROC_FUNCTION,
                                    NULL, NULL, NULL, 0, NULL);
    grn_proc_set_selector(ctx, selector_proc, selector_values_filter);
    grn_proc_set_selector_operator(ctx, selector_proc, GRN_OP_EQUAL);
  }

  grn_proc_create(ctx, "is_asc_pair", -1, GRN_PROC_FUNCTION,
                  func_is_asc_pair,
                  NULL, NULL, 0, NULL);

  return ctx->rc;
}

grn_rc
GRN_PLUGIN_FIN(GNUC_UNUSED grn_ctx *ctx)
{
  return GRN_SUCCESS;
}
